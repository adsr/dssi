/* -*- c-basic-offset: 4 -*-  vi:set ts=8 sts=4 sw=4: */

/* dssi_example_host.c
 *
 * Disposable Soft Synth Interface
 * Constructed by Chris Cannam, Steve Harris and Sean Bolton
 *
 * This is an example DSSI host.  It listens for MIDI events on an
 * ALSA sequencer port, delivers them to DSSI synths and outputs the
 * result via JACK.
 *
 * This program expects the names of up to 16 DSSI synth plugins, in
 * the form '<dll-name>/<label>',* to be provided on the command line.
 * If just '<dll-name>' is provided, the first plugin in the DLL is
 * is used.  MIDI channels are assigned to each plugin instance, in
 * order, beginning with channel 0 (zero-based).  A plugin may be
 * easily instantiated multiple times by preceding its name and label
 * with a dash followed immediately by the desired number of instances,
 * e.g. '-3 my_plugins.so/zoomy' would create three instances of the
 * 'zoomy' plugin.
 *
 * This example file is in the public domain.
*/

#include <ladspa.h>
#include "dssi.h"
#include <alsa/asoundlib.h>
#include <alsa/seq.h>
#include <jack/jack.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <dirent.h>

#include <lo/lo.h>

#include "dssi_example_host.h"

#include "message_buffer.h"

static snd_seq_t *alsaClient;

static jack_client_t *jackClient;
static jack_port_t **inputPorts, **outputPorts;

static d3h_dll_t     *dlls;

static d3h_plugin_t  *plugins;
static int            plugin_count = 0;

static d3h_instance_t instances[D3H_MAX_INSTANCES];
static int            instance_count = 0;

static LADSPA_Handle    *instanceHandles;
static snd_seq_event_t **instanceEventBuffers;
static unsigned long    *instanceEventCounts;

static int insTotal, outsTotal;
static float **pluginInputBuffers, **pluginOutputBuffers;

static int controlInsTotal, controlOutsTotal;
static float *pluginControlIns, *pluginControlOuts;
static d3h_instance_t *channel2instance[D3H_MAX_CHANNELS]; /* maps MIDI channel to instance */
static d3h_instance_t **pluginControlInInstances;          /* maps global control in # to instance */
static unsigned long *pluginControlInPortNumbers;          /* maps global control in # to instance LADSPA port # */
static int *pluginPortUpdated;                             /* indexed by global control in # */

static char osc_path_tmp[1024];

lo_server_thread serverThread;

static sigset_t _signals;

#define EVENT_BUFFER_SIZE 1024
static snd_seq_event_t midiEventBuffer[EVENT_BUFFER_SIZE]; /* ring buffer */
static int midiEventReadIndex = 0, midiEventWriteIndex = 0;

static pthread_mutex_t midiEventBufferMutex = PTHREAD_MUTEX_INITIALIZER;

LADSPA_Data get_port_default(const LADSPA_Descriptor *plugin, int port);

void osc_error(int num, const char *m, const char *path);

int osc_message_handler(const char *path, const char *types, lo_arg **argv, int
		      argc, void *data, void *user_data) ;

void
signalHandler(int sig)
{
    fprintf(stderr, "dssi_example_host: signal caught, exiting\n");
    kill(0, sig);
    exit(0);
}

void
midi_callback()
{
    snd_seq_event_t *ev = 0;

    pthread_mutex_lock(&midiEventBufferMutex);

    do {
	if (snd_seq_event_input(alsaClient, &ev) > 0) {

	    if (midiEventReadIndex == midiEventWriteIndex + 1) {
		fprintf(stderr, "dssi_example_host: Warning: MIDI event buffer overflow!\n");
		continue;
	    }

	    midiEventBuffer[midiEventWriteIndex] = *ev;

	    ev = &midiEventBuffer[midiEventWriteIndex];

	    if (ev->type == SND_SEQ_EVENT_NOTEON && ev->data.note.velocity == 0) {
		ev->type =  SND_SEQ_EVENT_NOTEOFF;
	    }

	    /* We don't need to handle EVENT_NOTE here, because ALSA
	       won't ever deliver them on the sequencer queue -- it
	       unbundles them into NOTE_ON and NOTE_OFF when they're
	       dispatched.  We would only need worry about them when
	       retrieving MIDI events from some other source. */

	    midiEventWriteIndex = (midiEventWriteIndex + 1) % EVENT_BUFFER_SIZE;
	}
	
    } while (snd_seq_event_input_pending(alsaClient, 0) > 0);

    pthread_mutex_unlock(&midiEventBufferMutex);
}

void
setControl(d3h_instance_t *instance, long controlIn, snd_seq_event_t *event)
{
    long port = pluginControlInPortNumbers[controlIn];

    const LADSPA_Descriptor *p = instance->plugin->descriptor->LADSPA_Plugin;

    LADSPA_PortRangeHintDescriptor d = p->PortRangeHints[port].HintDescriptor;

    LADSPA_Data lb = p->PortRangeHints[port].LowerBound;

    LADSPA_Data ub = p->PortRangeHints[port].UpperBound;

    float value = (float)event->data.control.value;

    if (!LADSPA_IS_HINT_BOUNDED_BELOW(d)) {
	if (!LADSPA_IS_HINT_BOUNDED_ABOVE(d)) {
	    /* unbounded: might as well leave the value alone. */
	} else {
	    /* bounded above only. just shift the range. */
	    value = ub - 127.0f + value;
	}
    } else {
	if (!LADSPA_IS_HINT_BOUNDED_ABOVE(d)) {
	    /* bounded below only. just shift the range. */
	    value = lb + value;
	} else {
	    /* bounded both ends.  more interesting. */
	    /* XXX !!! todo: fill in logarithmic, sample rate &c */
	    value = lb + ((ub - lb) * value / 127.0f);
	}
    }
    
    printf("dssi_example_host: %s MIDI controller %d=%d -> control in %ld=%f\n",
	   instance->friendly_name, event->data.control.param,
           event->data.control.value, controlIn, value);

    pluginControlIns[controlIn] = value;
    pluginPortUpdated[controlIn] = 1;
}

int
audio_callback(jack_nframes_t nframes, void *arg)
{
    int i;
    d3h_instance_t *instance;

    /* Not especially pretty or efficient */

    for (i = 0; i < instance_count; i++) {
        instanceEventCounts[i] = 0;
    }

    for ( ; midiEventReadIndex != midiEventWriteIndex;
         midiEventReadIndex = (midiEventReadIndex + 1) % EVENT_BUFFER_SIZE) {

	snd_seq_event_t *ev = &midiEventBuffer[midiEventReadIndex];

        if (!snd_seq_ev_is_channel_type(ev)) {
            /* discard non-channel oriented messages */
            continue;
        }

        instance = channel2instance[ev->data.note.channel];
        if (!instance) {
            /* discard messages intended for channels we aren't using */
            continue;
        }
        i = instance->number;

        /* stop processing incoming MIDI if an instance's event buffer is
         * full. */
	if (instanceEventCounts[i] == EVENT_BUFFER_SIZE)
            break;

	/* We can't exercise the plugin's support for the frame offset
	 * count, because we don't know at what frame times the
	 * events were intended to arrive. */
	ev->time.tick = 0;

	if (ev->type == SND_SEQ_EVENT_CONTROLLER) {

	    int controller = ev->data.control.param;
#ifdef DEBUG
	    MB_MESSAGE("%s CC %d(0x%02x) = %d\n", instance->friendly_name,
                       controller, controller, ev->data.control.value);
#endif

	    if (controller == 0) { // bank select MSB

		instance->pendingBankMSB = ev->data.control.value;

	    } else if (controller == 32) { // bank select LSB

		instance->pendingBankLSB = ev->data.control.value;

	    } else if (controller > 0 && controller < MIDI_CONTROLLER_COUNT) {

		long controlIn = instance->controllerMap[controller];
		if (controlIn >= 0) {

                    /* controller is mapped to LADSPA port, update the port */
		    setControl(instance, controlIn, ev);

		} else {

                    /* controller is not mapped, so pass the event through to plugin */
                    instanceEventBuffers[i][instanceEventCounts[i]] = *ev;
                    instanceEventCounts[i]++;
                }
	    }

	} else if (ev->type == SND_SEQ_EVENT_PGMCHANGE) {
	    
	    instance->pendingProgramChange = ev->data.control.value;
	    instance->uiNeedsProgramUpdate = 1;

	} else {

            instanceEventBuffers[i][instanceEventCounts[i]] = *ev;
            instanceEventCounts[i]++;
	}
    }

    /* process pending program changes */
    for (i = 0; i < instance_count; i++) {
        instance = &instances[i];

        if (instance->pendingProgramChange >= 0) {

            int pc = instance->pendingProgramChange;
            int msb = instance->pendingBankMSB;
            int lsb = instance->pendingBankLSB;

            //!!! gosh, I don't know this -- need to check with the specs:
            // if you only send one of MSB/LSB controllers, should the
            // other go to zero or remain as it was?  Assume it remains as
            // it was, for now.

            if (lsb >= 0) {
                if (msb >= 0) {
                    instance->currentBank = lsb + 128 * msb;
                } else {
                    instance->currentBank = lsb + 128 * (instance->currentBank / 128);
                }
            } else if (msb >= 0) {
                instance->currentBank = (instance->currentBank % 128) + 128 * msb;
            }

            instance->currentProgram = pc;

            instance->pendingProgramChange = -1;
            instance->pendingBankMSB = -1;
            instance->pendingBankLSB = -1;

            if (instance->plugin->descriptor->select_program) {
                instance->plugin->descriptor->
                    select_program(instanceHandles[instance->number],
                                   instance->currentBank,
                                   instance->currentProgram);
            }
        }
    }

    /* call run_synth() or run_multiple_synths() for all instances */
    i = 0;
    while (i < instance_count) {
        if (instances[i].plugin->descriptor->run_multiple_synths) {
            instances[i].plugin->descriptor->run_multiple_synths
                (instances[i].plugin->instances,
                 instanceHandles + i,
                 nframes,
                 instanceEventBuffers + i,
                 instanceEventCounts + i);
            i += instances[i].plugin->instances;
        } else {
            instances[i].plugin->descriptor->run_synth(instanceHandles[i],
                                                       nframes,
                                                       instanceEventBuffers[i],
                                                       instanceEventCounts[i]);
            i++;
        }
    }

    assert(sizeof(LADSPA_Data) == sizeof(jack_default_audio_sample_t));

    for (i = 0; i < outsTotal; ++i) {

	jack_default_audio_sample_t *buffer =
	    jack_port_get_buffer(outputPorts[i], nframes);
	
	memcpy(buffer, pluginOutputBuffers[i], nframes * sizeof(LADSPA_Data));
    }

    return 0;
}

char *
load(const char *dllName, void **dll) /* returns directory where dll found */
{
    char *dssiPath = getenv("DSSI_PATH");
    char *ladspaPath = getenv("LADSPA_PATH");
    char *path, *origPath, *element, *message;
    void *handle = 0;

    if (!dssiPath && !ladspaPath) {
	fprintf(stderr, "dssi_example_host: Error: Neither DSSI_PATH nor LADSPA_PATH is set\n");
	return 0;
    } else if (!dssiPath) {
	fprintf(stderr, "dssi_example_host: Warning: DSSI_PATH not set, using LADSPA_PATH only\n");
    }

    path = (char *)malloc((dssiPath   ? strlen(dssiPath)       : 0) +
			  (ladspaPath ? strlen(ladspaPath) + 1 : 0) + 1);
			  
    sprintf(path, "%s%s%s",
	    dssiPath ? dssiPath : "",
	    ladspaPath ? ":" : "",
	    ladspaPath ? ladspaPath : "");

    origPath = path;
    *dll = 0;

    while ((element = strtok(path, ":")) != 0) {

	path = 0;

	if (element[0] != '/') {
	    fprintf(stderr, "dssi_example_host: Ignoring relative element %s in path\n", element);
	    continue;
	}

	fprintf(stderr, "dssi_example_host: Looking for %s in %s... ", dllName, element);

	char *filePath = (char *)malloc(strlen(element) + strlen(dllName) + 2);
	sprintf(filePath, "%s/%s", element, dllName);

	if ((handle = dlopen(filePath, RTLD_NOW))) {  /* real-time programs should not use RTLD_LAZY */
	    fprintf(stderr, "found\n");
	    *dll = handle;
            free(filePath);
            path = strdup(element);
            free(origPath);
	    return path;
	}

        message = dlerror();
        if (message) {
            fprintf(stderr, "\ndssi_example_host: nope (%s)\n", message);
        } else {
            fprintf(stderr, "\ndssi_example_host: nope\n");
        }

        free(filePath);
    }

    free(origPath);
    return 0;
}

static int
instance_sort_cmp(const void *a, const void *b)
{
    d3h_instance_t *ia = (d3h_instance_t *)a;
    d3h_instance_t *ib = (d3h_instance_t *)b;

    if (ia->plugin->number != ib->plugin->number) {
        return ia->plugin->number - ib->plugin->number;
    } else {
        return ia->channel - ib->channel;
    }
}

void
startGUI(const char *directory, const char *dllName, const char *label,
	 const char *oscUrl)
{
    struct dirent *entry;
    char *dllBase = strdup(dllName);
    char *subpath;
    DIR *subdir;
    char *filename;
    struct stat buf;
	
    if (strlen(dllBase) > 3 &&
        !strcasecmp(dllBase + strlen(dllBase) - 3, ".so")) {
	dllBase[strlen(dllBase) - 3] = '\0';
    }

    subpath = (char *)malloc(strlen(directory) + strlen(dllBase) + 2);
    sprintf(subpath, "%s/%s", directory, dllBase);
    free(dllBase);

    if (!(subdir = opendir(subpath))) {
	fprintf(stderr, "dssi_example_host: can't open plugin GUI directory \"%s\"\n", subpath);
	free(subpath);
	return;
    }

    while ((entry = readdir(subdir))) {
	
	if (entry->d_name[0] == '.') continue;
	if (strncmp(entry->d_name, label, strlen(label))) continue;

	filename = (char *)malloc(strlen(subpath) + strlen(entry->d_name) + 2);
	sprintf(filename, "%s/%s", subpath, entry->d_name);

	if (stat(filename, &buf)) {
	    perror("stat failed");
	    free(filename);
	    continue;
	}

	if (S_ISREG(buf.st_mode) &&
	    (buf.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {

	    fprintf(stderr, "dssi_example_host: trying to execute GUI at \"%s\"\n",
		    filename);

	    if (fork() == 0) {
		execlp(filename, filename, oscUrl, dllName, label, 0);
		perror("exec failed");
		exit(1);
	    }

	    free(filename);
	    free(subpath);
	    return;
	}

	free(filename);
    }	

    fprintf(stderr, "dssi_example_host: no GUI found for plugin \"%s\" in \"%s/\"\n",
	    label, subpath);
    free(subpath);
}

void
query_programs(d3h_instance_t *instance)
{
    int i;

    /* free old lot */
    if (instance->pluginPrograms) {
	free((char *)instance->pluginPrograms);
	instance->pluginPrograms = NULL;
	instance->pluginProgramCount = 0;
    }

    instance->pendingBankLSB = -1;
    instance->pendingBankMSB = -1;
    instance->pendingProgramChange = -1;

    if (instance->plugin->descriptor->get_program &&
        instance->plugin->descriptor->select_program) {

	/* Count the plugins first */
	for (i = 0; instance->plugin->descriptor->
                        get_program(instanceHandles[instance->number], i, 0); ++i);

	if (i > 0) {
	    instance->pluginProgramCount = i;
	    instance->pluginPrograms = (DSSI_Program_Descriptor *)
		malloc(i * sizeof(DSSI_Program_Descriptor));
	    while (i > 0) {
		--i;
		instance->plugin->descriptor->
		    get_program(instanceHandles[instance->number], i,
				instance->pluginPrograms + i);
                printf("dssi_example_host: %s program %d is MIDI bank %lu program %lu, named '%s'\n",
                       instance->friendly_name, i,
                       instance->pluginPrograms[i].Bank,
                       instance->pluginPrograms[i].Program,
                       instance->pluginPrograms[i].Name);
	    }
	    // select program at index 0
            // -FIX- this doesn't belong here, and should try to remember the current bank/program anyway */
	    instance->currentBank = instance->pluginPrograms[0].Bank;
	    instance->currentProgram = instance->pluginPrograms[0].Program;
	    instance->plugin->descriptor->
                select_program(instanceHandles[instance->number],
                               instance->currentBank, instance->currentProgram);
	    instance->uiNeedsProgramUpdate = 1;
	}
    }
}

int
main(int argc, char **argv)
{
    int portid;
    int npfd;
    struct pollfd *pfd;

    d3h_dll_t *dll;
    d3h_plugin_t *plugin;
    d3h_instance_t *instance;
    void *pluginObject;
    char *dllName;
    char *label;
    const char **ports;
    char *tmp;
    char *url;
    int i, reps, j;
    int in, out, controlIn, controlOut;

    setsid();
    sigemptyset (&_signals);
    sigaddset(&_signals, SIGHUP);
    sigaddset(&_signals, SIGINT);
    sigaddset(&_signals, SIGQUIT);
    sigaddset(&_signals, SIGPIPE);
    sigaddset(&_signals, SIGTERM);
    sigaddset(&_signals, SIGUSR1);
    sigaddset(&_signals, SIGUSR2);
    pthread_sigmask(SIG_BLOCK, &_signals, 0);

    fprintf(stderr, "dssi_example_host: dssi_example_host starting...\n");

    insTotal = outsTotal = controlInsTotal = controlOutsTotal = 0;

    /* Parse args and report usage */

    if (argc < 2) {
	fprintf(stderr, "dssi_example_host: Usage: %s [-#] dllname[/label] [...]\n", argv[0]);
	return 2;
    }

    reps = 1;
    for (i = 1; i < argc; i++) {

        if (instance_count >= D3H_MAX_INSTANCES) {
            fprintf(stderr, "dssi_example_host: too many plugin instances specified\n");
            return 2;
        }

        /* parse repetition count */
        if (argv[i][0] == '-') {
            reps = atoi(&argv[i][1]);
            if (reps > 0) {
                continue;
            } else {
                reps = 1;
            }
        }

        /* parse dll name, plus a label if supplied */
        tmp = strchr(argv[i], '/');
        if (tmp) {
            dllName = calloc(1, tmp - argv[i] + 1);
            strncpy(dllName, argv[i], tmp - argv[i]);
            label = strdup(tmp + 1);
        } else {
            dllName = strdup(argv[i]);
            label = NULL;
        }

        /* check if we've seen this plugin before */
        for (plugin = plugins; plugin; plugin = plugin->next) {
            if (label) {
                if (!strcmp(dllName, plugin->dll->name) &&
                    !strcmp(label,   plugin->label))
                    break;
            } else {
               if (!strcmp(dllName, plugin->dll->name) &&
                   plugin->is_first_in_dll)
                   break;
            }
        }

        if (plugin) {
            /* have already seen this plugin */

            free(dllName);
            free(label);

        } else {
            /* this is a new plugin */

            plugin = (d3h_plugin_t *)calloc(1, sizeof(d3h_plugin_t));
            plugin->number = plugin_count;
            plugin->label = label;

            /* check if we've seen this dll before */
            for (dll = dlls; dll; dll = dll->next) {
                if (!strcmp(dllName, dll->name))
                    break;
            }
            if (!dll) {
                /* this is a new dll */
                dll = (d3h_dll_t *)calloc(1, sizeof(d3h_dll_t));
                dll->name = dllName;
                
                dll->directory = load(dllName, &pluginObject);
                if (!dll->directory || !pluginObject) {
                    fprintf(stderr, "dssi_example_host: Error: Failed to load plugin DLL %s\n", dllName);
                    return 1;
                }
                
                dll->descfn = (DSSI_Descriptor_Function)dlsym(pluginObject,
                                                              "dssi_descriptor");
                if (!dll->descfn) {
                    fprintf(stderr, "dssi_example_host: Error: %s is not a DSSI plugin DLL\n", dllName);
                    return 1;
                } 

                dll->next = dlls;
                dlls = dll;
            }
            plugin->dll = dll;

            /* get the plugin descriptor */
            j = 0;
            while ((plugin->descriptor = dll->descfn(j++))) {
                if (!plugin->label ||
                    !strcmp(plugin->descriptor->LADSPA_Plugin->Label,
                            plugin->label))
                    break;
            }
            if (!plugin->descriptor) {
                fprintf(stderr, "dssi_example_host: Error: Plugin label %s not found in DLL %s\n",
                        plugin->label ? plugin->label : "(none)", dllName);
                return 1;
            }
            plugin->is_first_in_dll = (j = 1);
            if (!plugin->label) {
                plugin->label = strdup(plugin->descriptor->LADSPA_Plugin->Label);
            }

            /* Count number of i/o buffers and ports required */
            plugin->ins = 0;
            plugin->outs = 0;
            plugin->controlIns = 0;
            plugin->controlOuts = 0;
 
            for (j = 0; j < plugin->descriptor->LADSPA_Plugin->PortCount; j++) {

                LADSPA_PortDescriptor pod =
                    plugin->descriptor->LADSPA_Plugin->PortDescriptors[j];

                if (LADSPA_IS_PORT_AUDIO(pod)) {

                    if (LADSPA_IS_PORT_INPUT(pod)) ++plugin->ins;
                    else if (LADSPA_IS_PORT_OUTPUT(pod)) ++plugin->outs;

                } else if (LADSPA_IS_PORT_CONTROL(pod)) {

                    if (LADSPA_IS_PORT_INPUT(pod)) ++plugin->controlIns;
                    else if (LADSPA_IS_PORT_OUTPUT(pod)) ++plugin->controlOuts;
                }
            }

            /* finish up new plugin */
            plugin->instances = 0;
            plugin->next = plugins;
            plugins = plugin;
            plugin_count++;
        }

        /* set up instances */
        for (j = 0; j < reps; j++) {
            if (instance_count < D3H_MAX_INSTANCES) {
                instance = &instances[instance_count];

                instance->plugin = plugin;
                instance->channel = instance_count;
                tmp = (char *)malloc(strlen(plugin->dll->name) +
                                     strlen(plugin->label) + 9);
                instance->friendly_name = tmp;
                strcpy(tmp, plugin->dll->name);
                if (strlen(tmp) > 3 &&
                    !strcasecmp(tmp + strlen(tmp) - 3, ".so")) {
                    tmp = tmp + strlen(tmp) - 3;
                } else {
                    tmp = tmp + strlen(tmp);
                }
                sprintf(tmp, "/%s/chan%02d", plugin->label, instance->channel);
                instance->firstControlIn = controlInsTotal;
                instance->pluginProgramCount = 0;
                instance->pluginPrograms = NULL;
                instance->currentBank = 0;
                instance->currentProgram = 0;
                instance->pendingBankLSB = -1;
                instance->pendingBankMSB = -1;
                instance->pendingProgramChange = -1;
                instance->uiTarget = NULL;
                instance->ui_initial_show_sent = 0;
                instance->uiNeedsProgramUpdate = 0;
                instance->ui_osc_control_path = NULL;
                instance->ui_osc_program_path = NULL;
                instance->ui_osc_show_path = NULL;

                insTotal += plugin->ins;
                outsTotal += plugin->outs;
                controlInsTotal += plugin->controlIns;
                controlOutsTotal += plugin->controlOuts;

                plugin->instances++;
                instance_count++;
            } else {
                fprintf(stderr, "dssi_example_host: too many plugin instances specified\n");
                return 2;
            }
        }
        reps = 1;
    }

    /* sort array of instances to group them by plugin */
    if (instance_count > 1) {
        qsort(instances, instance_count, sizeof(d3h_instance_t), instance_sort_cmp);
    }

    /* build channel2instance[] while showing what our instances are */
    for (i = 0; i < D3H_MAX_CHANNELS; i++)
        channel2instance[i] = NULL;
    for (i = 0; i < instance_count; i++) {
        instance = &instances[i];
        instance->number = i;
        channel2instance[instance->channel] = instance;
        fprintf(stderr, "dssi_example_host: instance %2d on channel %2d, plugin %2d is '%s'\n",
                i, instance->channel, instance->plugin->number,
                instance->friendly_name);
    }

    /* Create buffers and JACK client and ports */

    if ((jackClient = jack_client_new("dssi_example_host")) == 0) {
        fprintf(stderr, "dssi_example_host: Error: Failed to connect to JACK server\n");
	return 1;
    }

    inputPorts = (jack_port_t **)malloc(insTotal * sizeof(jack_port_t *));
    pluginInputBuffers = (float **)malloc(insTotal * sizeof(float *));
    pluginControlIns = (float *)calloc(controlInsTotal, sizeof(float));
    pluginControlInInstances =
        (d3h_instance_t **)malloc(controlInsTotal * sizeof(d3h_instance_t *));
    pluginControlInPortNumbers =
        (unsigned long *)malloc(controlInsTotal * sizeof(unsigned long));
    pluginPortUpdated = (int *)malloc(controlInsTotal * sizeof(int));

    outputPorts = (jack_port_t **)malloc(outsTotal * sizeof(jack_port_t *));
    pluginOutputBuffers = (float **)malloc(outsTotal * sizeof(float *));
    pluginControlOuts = (float *)calloc(controlOutsTotal, sizeof(float));

    instanceHandles = (LADSPA_Handle *)malloc(instance_count *
                                              sizeof(LADSPA_Handle));
    instanceEventBuffers = (snd_seq_event_t **)malloc(instance_count *
                                                      sizeof(snd_seq_event_t *));
    instanceEventCounts = (unsigned long *)malloc(instance_count *
                                                  sizeof(unsigned long));

    for (i = 0; i < instance_count; i++) {
        instanceEventBuffers[i] = (snd_seq_event_t *)malloc(EVENT_BUFFER_SIZE *
                                                            sizeof(snd_seq_event_t));
        instances[i].pluginPortControlInNumbers =
            (int *)malloc(instances[i].plugin->descriptor->LADSPA_Plugin->PortCount *
                          sizeof(int));
    }

    for (in = 0; in < insTotal; in++) {
        /* !FIX! this to have more descriptive names */
        char portName[20];
        sprintf(portName, "in_%02d", in + 1);
        inputPorts[in] = jack_port_register(jackClient, portName,
					    JACK_DEFAULT_AUDIO_TYPE,
					    JackPortIsInput, 0);
	pluginInputBuffers[in] =
	    (float *)calloc(jack_get_buffer_size(jackClient), sizeof(float));
    }

    for (out = 0; out < outsTotal; out++) {
        /* !FIX! this to have more descriptive names */
	char portName[20];
	sprintf(portName, "out_%02d", out + 1);
	outputPorts[out] = jack_port_register(jackClient, portName,
					      JACK_DEFAULT_AUDIO_TYPE,
					      JackPortIsOutput, 0);
	pluginOutputBuffers[out] = 
	    (float *)calloc(jack_get_buffer_size(jackClient), sizeof(float));
    }

    jack_set_process_callback(jackClient, audio_callback, 0);

    /* Instantiate plugins */

    for (i = 0; i < instance_count; i++) {
        plugin = instances[i].plugin;
        instanceHandles[i] = plugin->descriptor->LADSPA_Plugin->instantiate
            (plugin->descriptor->LADSPA_Plugin, jack_get_sample_rate(jackClient));

        if (!instanceHandles[i]) {
            fprintf(stderr, "dssi_example_host: Error: Failed to instantiate instance %d!, plugin %s\n",
                    i, plugin->label);
            return 1;
        }
    }

    /* Create OSC thread */

    serverThread = lo_server_thread_new(NULL, osc_error);
    snprintf((char *)osc_path_tmp, 31, "/dssi");
    tmp = lo_server_thread_get_url(serverThread);
    url = (char *)malloc(strlen(tmp) + strlen(osc_path_tmp));
    sprintf(url, "%s%s", tmp, osc_path_tmp + 1);
    printf("dssi_example_host: registering %s\n", url);
    free(tmp);

    lo_server_thread_add_method(serverThread, NULL, NULL, osc_message_handler,
				NULL);
    lo_server_thread_start(serverThread);

    /* Connect and activate plugins */

    for (in = 0; in < insTotal; in++) {
        pluginPortUpdated[in] = 0;
    }

    in = out = controlIn = controlOut = 0;

    for (i = 0; i < instance_count; i++) {   /* i is instance number */
        instance = &instances[i];

        for (j = 0; j < MIDI_CONTROLLER_COUNT; j++) {
            instance->controllerMap[j] = -1;
        }

        plugin = instance->plugin;
        for (j = 0; j < plugin->descriptor->LADSPA_Plugin->PortCount; j++) {  /* j is LADSPA port number */

            LADSPA_PortDescriptor pod =
                plugin->descriptor->LADSPA_Plugin->PortDescriptors[j];

            instance->pluginPortControlInNumbers[j] = -1;

            if (LADSPA_IS_PORT_AUDIO(pod)) {

                if (LADSPA_IS_PORT_INPUT(pod)) {
                    plugin->descriptor->LADSPA_Plugin->connect_port
                        (instanceHandles[i], j, pluginInputBuffers[in++]);

                } else if (LADSPA_IS_PORT_OUTPUT(pod)) {
                    plugin->descriptor->LADSPA_Plugin->connect_port
                        (instanceHandles[i], j, pluginOutputBuffers[out++]);
                }

            } else if (LADSPA_IS_PORT_CONTROL(pod)) {

                if (LADSPA_IS_PORT_INPUT(pod)) {

                    if (plugin->descriptor->get_midi_controller_for_port) {

                        int controller = plugin->descriptor->
                            get_midi_controller_for_port(instanceHandles[i], j);

                        if (controller == 0) {
                            MB_MESSAGE
                                ("Buggy plugin: wants mapping for bank MSB\n");
                        } else if (controller == 32) {
                            MB_MESSAGE
                                ("Buggy plugin: wants mapping for bank LSB\n");
                        } else if (DSSI_IS_CC(controller)) {
                            instance->controllerMap[DSSI_CC_NUMBER(controller)]
                                = controlIn;
                        }
                    }

                    pluginControlInInstances[controlIn] = instance;
                    pluginControlInPortNumbers[controlIn] = j;
                    instance->pluginPortControlInNumbers[j] = controlIn;

                    pluginControlIns[controlIn] = get_port_default
                        (plugin->descriptor->LADSPA_Plugin, j);

                    plugin->descriptor->LADSPA_Plugin->connect_port
                        (instanceHandles[i], j, &pluginControlIns[controlIn++]);

                } else if (LADSPA_IS_PORT_OUTPUT(pod)) {
                    plugin->descriptor->LADSPA_Plugin->connect_port
                        (instanceHandles[i], j, &pluginControlOuts[controlOut++]);
                }
            }
        }  /* 'for (j...'  LADSPA port number */

        if (plugin->descriptor->LADSPA_Plugin->activate) {
            plugin->descriptor->LADSPA_Plugin->activate(instanceHandles[i]);
        }
    } /* 'for (i...' instance number */
    assert(in == insTotal);
    assert(out == outsTotal);
    assert(controlIn == controlInsTotal);
    assert(controlOut == controlOutsTotal);

    /* Look up synth programs */

    for (i = 0; i < instance_count; i++) {
        query_programs(&instances[i]);
    }

    /* Create ALSA MIDI port */

#if !(defined(__MACH__) && defined(__APPLE__))
    if (snd_seq_open(&alsaClient, "hw", SND_SEQ_OPEN_DUPLEX, 0) < 0) {
	fprintf(stderr, "dssi_example_host: Error: Failed to open ALSA sequencer interface\n");
	return 1;
    }

    snd_seq_set_client_name(alsaClient, "dssi_example_host");

    if ((portid = snd_seq_create_simple_port
	 (alsaClient, "dssi_example_host",
	  SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE, 0)) < 0) {
	fprintf(stderr, "dssi_example_host: Error: Failed to create ALSA sequencer port\n");
	return 1;
    }

    npfd = snd_seq_poll_descriptors_count(alsaClient, POLLIN);
    pfd = (struct pollfd *)alloca(npfd * sizeof(struct pollfd));
    snd_seq_poll_descriptors(alsaClient, pfd, npfd, POLLIN);
#endif

    mb_init("host: ");

    if (jack_activate(jackClient)) {
        fprintf (stderr, "cannot activate jack client");
        exit(1);
    }

    /* activate JACK and connect ports */
    /* !FIX! this to more intelligently connect ports: */
    ports = jack_get_ports(jackClient, NULL, NULL,
                           JackPortIsPhysical|JackPortIsInput);
    if (ports && ports[0]) {
        for (i = 0, j = 0; i < outsTotal; ++i) {
            if (jack_connect(jackClient, jack_port_name(outputPorts[i]),
                             ports[j])) {
                fprintf (stderr, "cannot connect output port %d\n", i);
            }
            if (!ports[++j]) j = 0;
        }
        free(ports);
    }

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGHUP, signalHandler);
    signal(SIGQUIT, signalHandler);
    pthread_sigmask(SIG_UNBLOCK, &_signals, 0);

    /* Attempt to locate and start up a GUI for the plugin -- but
     * continue even if we can't */
    /* -FIX- Ack! So many windows all at once! */
    for (i = 0; i < instance_count; i++) {
        plugin = instances[i].plugin;
        snprintf(osc_path_tmp, 1024, "%s/%s", url, instances[i].friendly_name);
        startGUI(plugin->dll->directory, plugin->dll->name,
                 plugin->descriptor->LADSPA_Plugin->Label, osc_path_tmp);
    }

    MB_MESSAGE("Ready\n");

    while (1) {
#if !(defined(__MACH__) && defined(__APPLE__))
	if (poll(pfd, npfd, 100) > 0) {
	    midi_callback();
	}
#endif

	/* Race conditions here, because the programs and ports are
	   updated from the audio thread.  We at least try to minimise
	   trouble by copying out before the expensive OSC call */

        for (i = 0; i < instance_count; i++) {
            instance = &instances[i];
            if (instance->uiNeedsProgramUpdate && instance->pendingProgramChange < 0) {
                int bank = instance->currentBank;
                int program = instance->currentProgram;
                instance->uiNeedsProgramUpdate = 0;
                if (instance->uiTarget) {
                    lo_send(instance->uiTarget, instance->ui_osc_program_path, "ii", bank, program);
                }
            }
        }

	for (i = 0; i < controlInsTotal; ++i) {
	    if (pluginPortUpdated[i]) {
                instance = pluginControlInInstances[i];
		int port = pluginControlInPortNumbers[i];
		float value = pluginControlIns[i];
		pluginPortUpdated[i] = 0;
		if (instance->uiTarget) {
		    lo_send(instance->uiTarget, instance->ui_osc_control_path, "if", port, value);
		}
	    }
	}
    }

    return 0;
}

LADSPA_Data get_port_default(const LADSPA_Descriptor *plugin, int port)
{
    float fs = jack_get_sample_rate(jackClient);
    LADSPA_PortRangeHint hint = plugin->PortRangeHints[port];
    float lower = hint.LowerBound *
	(LADSPA_IS_HINT_SAMPLE_RATE(hint.HintDescriptor) ? fs : 1.0f);
    float upper = hint.UpperBound *
	(LADSPA_IS_HINT_SAMPLE_RATE(hint.HintDescriptor) ? fs : 1.0f);

    if (!LADSPA_IS_HINT_HAS_DEFAULT(hint.HintDescriptor)) {
	if (!LADSPA_IS_HINT_BOUNDED_BELOW(hint.HintDescriptor) ||
	    !LADSPA_IS_HINT_BOUNDED_ABOVE(hint.HintDescriptor)) {
	    /* No hint, its not bounded, wild guess */
	    return 0.0f;
	}

	if (lower <= 0.0f && upper >= 0.0f) {
	    /* It spans 0.0, 0.0 is often a good guess */
	    return 0.0f;
	}

	/* No clues, return minimum */
	return lower;
    }

    /* Try all the easy ones */
    
    if (LADSPA_IS_HINT_DEFAULT_0(hint.HintDescriptor)) {
	return 0.0f;
    } else if (LADSPA_IS_HINT_DEFAULT_1(hint.HintDescriptor)) {
	return 1.0f;
    } else if (LADSPA_IS_HINT_DEFAULT_100(hint.HintDescriptor)) {
	return 100.0f;
    } else if (LADSPA_IS_HINT_DEFAULT_440(hint.HintDescriptor)) {
	return 440.0f;
    }

    /* All the others require some bounds */

    if (LADSPA_IS_HINT_BOUNDED_BELOW(hint.HintDescriptor)) {
	if (LADSPA_IS_HINT_DEFAULT_MINIMUM(hint.HintDescriptor)) {
	    return lower;
	}
    }
    if (LADSPA_IS_HINT_BOUNDED_ABOVE(hint.HintDescriptor)) {
	if (LADSPA_IS_HINT_DEFAULT_MAXIMUM(hint.HintDescriptor)) {
	    return upper;
	}
	if (LADSPA_IS_HINT_BOUNDED_BELOW(hint.HintDescriptor)) {
	    if (LADSPA_IS_HINT_DEFAULT_LOW(hint.HintDescriptor)) {
		return lower * 0.75f + upper * 0.25f;
	    } else if (LADSPA_IS_HINT_DEFAULT_MIDDLE(hint.HintDescriptor)) {
		return lower * 0.5f + upper * 0.5f;
	    } else if (LADSPA_IS_HINT_DEFAULT_HIGH(hint.HintDescriptor)) {
		return lower * 0.25f + upper * 0.75f;
	    }
	}
    }

    /* fallback */
    return 0.0f;
}

void osc_error(int num, const char *msg, const char *path)
{
    fprintf(stderr, "dssi_example_host: liblo server error %d in path %s: %s\n", num, path, msg);
}

// smbolton: Hmm, from the OpenSound Control Specification, v1.0:
// 
//     An application that does use any additional [non-standard]
//     argument types must encode them with the OSC Type Tags in this
//     table:
// 
//     OSC Type Tag   Type of corresponding argument
// 
//     m              4 byte MIDI message. Bytes from MSB to LSB are:
//                    port id, status byte, data1, data2
// 
// which would mean we should start encoding from &argv[0]->m[1], and
// not allow running status.

int
osc_midi_handler(d3h_instance_t *instance, lo_arg **argv)
{
    static snd_midi_event_t *alsaCoder = NULL;
    static snd_seq_event_t alsaEncodeBuffer[10];
    long count;
    snd_seq_event_t *ev = &alsaEncodeBuffer[0];

    printf("dssi_example_host: OSC: got midi request for %s (%02x %02x %02x %02x)\n",
           instance->friendly_name, argv[0]->m[0], argv[0]->m[1], argv[0]->m[2], argv[0]->m[3]);

    if (!alsaCoder) {
        if (snd_midi_event_new(10, &alsaCoder)) {
            fprintf(stderr, "dssi_example_host: Failed to initialise ALSA MIDI coder!\n");
            return 0;
        }
    }

    snd_midi_event_reset_encode(alsaCoder);

    count = snd_midi_event_encode
	(alsaCoder, (argv[0]->m) + 1, 3, alsaEncodeBuffer);

    if (!count || !snd_seq_ev_is_channel_type(ev)) {
        return 0;
    }

    /* substitute correct MIDI channel */
    ev->data.note.channel = instance->channel;
    
    if (ev->type == SND_SEQ_EVENT_NOTEON && ev->data.note.velocity == 0) {
        ev->type =  SND_SEQ_EVENT_NOTEOFF;
    }
        
    pthread_mutex_lock(&midiEventBufferMutex);

    if (midiEventReadIndex == midiEventWriteIndex + 1) {

        fprintf(stderr, "dssi_example_host: Warning: MIDI event buffer overflow!\n");

    } else if (ev->type == SND_SEQ_EVENT_CONTROLLER &&
               (ev->data.control.param == 0 || ev->data.control.param == 32)) {

        fprintf(stderr, "dssi_example_host: Warning: %s UI sent bank select controller (should use /program OSC call), ignoring\n",
                instance->friendly_name);

    } else if (ev->type == SND_SEQ_EVENT_PGMCHANGE) {

        fprintf(stderr, "dssi_example_host: Warning: %s UI sent program change (should use /program OSC call), ignoring\n",
                instance->friendly_name);

    } else {

        midiEventBuffer[midiEventWriteIndex] = *ev;
        midiEventWriteIndex = (midiEventWriteIndex + 1) % EVENT_BUFFER_SIZE;

    }

    pthread_mutex_unlock(&midiEventBufferMutex);

    return 0;
}

int
osc_control_handler(d3h_instance_t *instance, lo_arg **argv)
{
    int port = argv[0]->i;
    LADSPA_Data value = argv[1]->f;

    if (port < 0 || port > instance->plugin->descriptor->LADSPA_Plugin->PortCount) {
	fprintf(stderr, "dssi_example_host: OSC: %s port number (%d) is out of range\n",
                instance->friendly_name, port);
	return 0;
    }
    if (instance->pluginPortControlInNumbers[port] == -1) {
	fprintf(stderr, "dssi_example_host: OSC: %s port %d is not a control in\n",
                instance->friendly_name, port);
	return 0;
    }
    pluginControlIns[instance->pluginPortControlInNumbers[port]] = value;
    printf("dssi_example_host: OSC: %s port %d = %f\n", instance->friendly_name,
           port, value);
    
    return 0;
}

int
osc_program_handler(d3h_instance_t *instance, lo_arg **argv)
{
    int bank = argv[0]->i;
    int program = argv[1]->i;
    int i;
    int found = 0;

    for (i = 0; i < instance->pluginProgramCount; ++i) {
	if (instance->pluginPrograms[i].Bank == bank &&
	    instance->pluginPrograms[i].Program == program) {
	    printf("dssi_example_host: OSC: %s setting bank %d, program %d, name %s\n",
                   instance->friendly_name, bank, program,
                   instance->pluginPrograms[i].Name);
	    found = 1;
	    break;
	}
    }

    if (!found) {
	printf("dssi_example_host: OSC: %s UI requested unknown program: bank %d, program %d: sending to plugin anyway (plugin should ignore it)\n",
		instance->friendly_name, bank, program);
    }

    instance->pendingBankMSB = bank / 128;
    instance->pendingBankLSB = bank % 128;
    instance->pendingProgramChange = program;

    return 0;
}

int
osc_configure_handler(d3h_instance_t *instance, lo_arg **argv)
{
    const char *key = (const char *)&argv[0]->s;
    const char *value = (const char *)&argv[1]->s;
    char *message;

    /* This is the simplest legal implementation of configure in a
     * DSSI host.  The host has the option to remember the set of
     * (key,value) pairs associated with a particular instance, so
     * that if it wants to restore the "same" instance on another
     * occasion it can just call configure() on it for each of those
     * pairs and so restore state without any input from a GUI.  Any
     * real-world GUI host will probably want to do that.  This host
     * doesn't have any concept of restoring an instance from one run
     * to the next, so we don't bother remembering these at all. */

    if (instance->plugin->descriptor->configure) {

	message = instance->plugin->descriptor->configure
                      (instanceHandles[instance->number], key, value);
        if (message) {
            printf("dssi_example_host: on configure '%s' '%s', plugin '%s' returned '%s'\n",
                   key, value, instance->friendly_name, message);
            free(message);
        }

	/* configure invalidates bank and program information, so
	   we should do this again now: */
	query_programs(instance);
    }

    return 0;
}

int
osc_update_handler(d3h_instance_t *instance, lo_arg **argv)
{
    const char *url = (char *)&argv[0]->s;
    const char *path;
    unsigned int i;
    char *host, *port;

    printf("dssi_example_host: OSC: got update request from <%s>\n", url);

    if (instance->uiTarget) lo_address_free(instance->uiTarget);
    host = lo_url_get_hostname(url);
    port = lo_url_get_port(url);
    instance->uiTarget = lo_address_new(host, port);
    free(host);
    free(port);

    path = lo_url_get_path(url);

    if (instance->ui_osc_control_path) free(instance->ui_osc_control_path);
    instance->ui_osc_control_path = (char *)malloc(strlen(path) + 10);
    sprintf(instance->ui_osc_control_path, "%s/control", path);

    if (instance->ui_osc_program_path) free(instance->ui_osc_program_path);
    instance->ui_osc_program_path = (char *)malloc(strlen(path) + 10);
    sprintf(instance->ui_osc_program_path, "%s/program", path);

    if (instance->ui_osc_show_path) free(instance->ui_osc_show_path);
    instance->ui_osc_show_path = (char *)malloc(strlen(path) + 10);
    sprintf(instance->ui_osc_show_path, "%s/show", path);

    free((char *)path);

    /* -FIX- should send the current program here, no? */

    for (i = 0; i < instance->plugin->controlIns; i++) {
        int in = i + instance->firstControlIn;
	int port = pluginControlInPortNumbers[in];
	lo_send(instance->uiTarget, instance->ui_osc_control_path, "if", port,
                pluginControlIns[in]);
    }

    if (!instance->ui_initial_show_sent) {
	lo_send(instance->uiTarget, instance->ui_osc_show_path, "");
	instance->ui_initial_show_sent = 1;
    }

    /* At this point a more substantial host might also call
     * configure() on the UI to set any state that it had remembered
     * for the plugin instance.  But we don't remember state for
     * plugin instances (see our own configure() implementation in
     * osc_configure_handler), and so we have nothing to send. */

    return 0;
}

int osc_debug_handler(const char *path, const char *types, lo_arg **argv,
                      int argc, void *data, void *user_data)
{
    int i;

    printf("dssi_example_host: got unhandled OSC message:\npath: <%s>\n", path);
    for (i=0; i<argc; i++) {
        printf("dssi_example_host: arg %d '%c' ", i, types[i]);
        lo_arg_pp(types[i], argv[i]);
        printf("\n");
    }
    printf("dssi_example_host:\n");

    return 1;
}

int osc_message_handler(const char *path, const char *types, lo_arg **argv,
                        int argc, void *data, void *user_data)
{
    int i;
    d3h_instance_t *instance = NULL;
    const char *method;

    if (strncmp(path, "/dssi/", 6))
        return osc_debug_handler(path, types, argv, argc, data, user_data);

    for (i = 0; i < instance_count; i++) {
        if (!strncmp(path + 6, instances[i].friendly_name,
                     strlen(instances[i].friendly_name))) {
            instance = &instances[i];
            break;
        }
    }
    if (!instance)
        return osc_debug_handler(path, types, argv, argc, data, user_data);

    method = path + 6 + strlen(instance->friendly_name);
    if (*method != '/' || *(method + 1) == 0)
        return osc_debug_handler(path, types, argv, argc, data, user_data);
    method++;

    if (!strcmp(method, "configure") && argc == 2 && !strcmp(types, "ss")) {

        return osc_configure_handler(instance, argv);

    } else if (!strcmp(method, "control") && argc == 2 && !strcmp(types, "if")) {

        return osc_control_handler(instance, argv);

    } else if (!strcmp(method, "midi") && argc == 1 && !strcmp(types, "m")) {

        return osc_midi_handler(instance, argv);

    } else if (!strcmp(method, "program") && argc == 2 && !strcmp(types, "ii")) {

        return osc_program_handler(instance, argv);

    } else if (!strcmp(method, "update") && argc == 1 && !strcmp(types, "s")) {

        return osc_update_handler(instance, argv);

    }
    return osc_debug_handler(path, types, argv, argc, data, user_data);
}


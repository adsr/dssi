/* -*- c-basic-offset: 4 -*-  vi:set ts=8 sts=4 sw=4: */

/* dssi_example_host.c

   Disposable Soft Synth Interface
   Constructed by Chris Cannam and Steve Harris

   This is an example DSSI host.  It listens for MIDI events on an
   ALSA sequencer port, delivers them to a DSSI synth and outputs the
   result via JACK.

   This program expects the DSSI synth plugin DLL name and label to be
   provided on the command line.  If the label is missing, it will use
   the first plugin in the given DLL.

   This example file is in the public domain.
*/

#include <dssi.h>
#include <alsa/asoundlib.h>
#include <alsa/seq.h>
#include <jack/jack.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <dirent.h>

#include <lo/lo.h>

#include "message_buffer.h"

static snd_seq_t *alsaClient;

static jack_client_t *jackClient;
static jack_port_t **inputPorts, **outputPorts;

static int ins, outs;
static float **pluginInputBuffers, **pluginOutputBuffers;

static int controlIns, controlOuts;
static float *pluginControlIns, *pluginControlOuts;
static unsigned long *pluginControlInPortNumbers;
static int *pluginPortControlInNumbers;
static int *pluginPortUpdated;

static LADSPA_Handle pluginHandle = 0;
static const DSSI_Descriptor *pluginDescriptor = 0;
static const char osc_path[32];

static int pluginProgramCount = 0;
static DSSI_Program_Descriptor *pluginPrograms = 0;

typedef struct {
    long bank;
    long program;
    int pendingBankLSB;
    int pendingBankMSB;
    int pendingProgram;
    int programUpdated;
} ProgramData;

static ProgramData *programData;
static int channelCount;

lo_server_thread serverThread;
lo_target uiTarget;
static char *gui_osc_control_path = 0;
static char *gui_osc_program_path = 0;
static char *gui_osc_show_path = 0;

static sigset_t _signals;

#define EVENT_BUFFER_SIZE 1024
static snd_seq_event_t midiEventBuffer[EVENT_BUFFER_SIZE]; /* ring buffer */
static int midiEventReadIndex = 0, midiEventWriteIndex = 0;

#define MIDI_CONTROLLER_COUNT 128
static long controllerMap[MIDI_CONTROLLER_COUNT]; /* contains indices into pluginControlIns */

static pthread_mutex_t midiEventBufferMutex = PTHREAD_MUTEX_INITIALIZER;

LADSPA_Data get_port_default(const LADSPA_Descriptor *plugin, int port);

void osc_error(int num, const char *m, const char *path);

int osc_control_handler(const char *path, const char *types, lo_arg **argv, int argc,
			void *data, void *user_data) ;
int osc_midi_handler(const char *path, const char *types, lo_arg **argv, int argc,
		     void *data, void *user_data) ;
int osc_update_handler(const char *path, const char *types, lo_arg **argv, int
		       argc, void *data, void *user_data) ;
int osc_program_handler(const char *path, const char *types, lo_arg **argv, int
			argc, void *data, void *user_data) ;
int osc_configure_handler(const char *path, const char *types, lo_arg **argv, int
			  argc, void *data, void *user_data) ;
int osc_exiting_handler(const char *path, const char *types, lo_arg **argv, int argc,
			void *data, void *user_data) ;
int osc_debug_handler(const char *path, const char *types, lo_arg **argv, int
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
setControl(long controlIn, snd_seq_event_t *event)
{
    long port = pluginControlInPortNumbers[controlIn];

    LADSPA_PortRangeHintDescriptor d =
	pluginDescriptor->LADSPA_Plugin->PortRangeHints[port].HintDescriptor;

    LADSPA_Data lb = 
	pluginDescriptor->LADSPA_Plugin->PortRangeHints[port].LowerBound;

    LADSPA_Data ub = 
	pluginDescriptor->LADSPA_Plugin->PortRangeHints[port].UpperBound;
    
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
    
    printf("dssi_example_host: MIDI controller %d=%d -> control in %ld=%f\n",
	   event->data.control.param, event->data.control.value,
	   controlIn, value);

    pluginControlIns[controlIn] = value;
    pluginPortUpdated[pluginControlInPortNumbers[controlIn]] = 1;
}

int
audio_callback(jack_nframes_t nframes, void *arg)
{
    static snd_seq_event_t processEventBuffer[EVENT_BUFFER_SIZE];
    int i = 0, count = 0;

    /* Not especially pretty or efficient */

    while (midiEventReadIndex != midiEventWriteIndex) {

	if (count == EVENT_BUFFER_SIZE) break;

	snd_seq_event_t *ev = &midiEventBuffer[midiEventReadIndex];
	
	/* We disregard the MIDI channel throughout: DSSI synths are
	   single-channel affairs */

	if (ev->type == SND_SEQ_EVENT_CONTROLLER) {

	    int controller = ev->data.control.param;
#ifdef DEBUG
	    MB_MESSAGE("CC %d(0x%02x) = %d\n", controller, controller,
		    ev->data.control.value);
#endif

	    if (controller == 0) { // bank select MSB

		int channel = ev->data.control.channel;
		if (pluginDescriptor->ChannelCount == 0) channel = 0;
		if (channel < channelCount) {
		    programData[channel].pendingBankMSB = ev->data.control.value;
		}

	    } else if (controller == 32) { // bank select LSB

		int channel = ev->data.control.channel;
		if (pluginDescriptor->ChannelCount == 0) channel = 0;
		if (channel < channelCount) {
		    programData[channel].pendingBankLSB = ev->data.control.value;
		}

	    } else if (controller > 0 && controller < MIDI_CONTROLLER_COUNT) {

		long controlIn = controllerMap[controller];
		if (controlIn >= 0) {

                    /* controller is mapped to LADSPA port, update the port */
		    setControl(controlIn, ev);

		} else {

                    /* controller is not mapped, so pass the event through to plugin */
                    processEventBuffer[count] = midiEventBuffer[midiEventReadIndex];
                    ++count;
		}
	    }

	} else if (ev->type == SND_SEQ_EVENT_PGMCHANGE) {

	    int channel = ev->data.control.channel;
	    if (pluginDescriptor->ChannelCount == 0) channel = 0;
	    if (channel < channelCount) {
		programData[channel].pendingProgram = ev->data.control.value;
	    }

	} else {
	
	    processEventBuffer[count] = midiEventBuffer[midiEventReadIndex];
	    ++count;
	}

	midiEventReadIndex = (midiEventReadIndex + 1) % EVENT_BUFFER_SIZE;
    }

    for (i = 0; i < count; ++i) {
	/* We can't exercise the plugin's support for the frame offset
	   count here, because we don't know at what frame times the
	   events were intended to arrive. */
	processEventBuffer[i].time.tick = 0; 
    }

    for (i = 0; i < channelCount; ++i) {

	if (programData[i].pendingProgram >= 0) {

	    int pc  = programData[i].pendingProgram;
	    int msb = programData[i].pendingBankMSB;
	    int lsb = programData[i].pendingBankLSB;

	    programData[i].pendingProgram = -1;
	    programData[i].pendingBankMSB = -1;
	    programData[i].pendingBankLSB = -1;

	    //!!! gosh, I don't know this -- need to check with the specs:
	    // if you only send one of MSB/LSB controllers, should the
	    // other go to zero or remain as it was?  Assume it remains as
	    // it was, for now.

	    if (lsb >= 0) {
		if (msb >= 0) {
		    programData[i].bank = lsb + 128 * msb;
		} else {
		    programData[i].bank = lsb + 128 * (programData[i].bank / 128);
		}
	    } else if (msb >= 0) {
		programData[i].bank = (programData[i].bank % 128) + 128 * msb;
	    }

	    programData[i].program = pc;

	    if (pluginDescriptor->select_program) {
		pluginDescriptor->select_program(pluginHandle, i,
						 programData[i].bank,
						 programData[i].program);
	    }

	    programData[i].programUpdated = 1;
	}
    }

    pluginDescriptor->run_synth(pluginHandle,
				nframes,
				processEventBuffer,
				count);

    assert(sizeof(LADSPA_Data) == sizeof(jack_default_audio_sample_t));
 
    for (i = 0; i < outs; ++i) {

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
    char *path, *origPath, *element;
    void *handle = 0;

    if (!dssiPath && !ladspaPath) {
	fprintf(stderr, "dssi_example_host: Error: Neither DSSI_PATH nor LADSPA_PATH is set\n");
	return 0;
    } else if (!dssiPath) {
	fprintf(stderr, "dssi_example_host: Warning: DSSI_PATH not set, using LADSPA_PATH only\n");
    }

    path = (char *)malloc((dssiPath   ? strlen(dssiPath)       : 0) +
			  (ladspaPath ? strlen(ladspaPath) + 1 : 0)) + 1;
			  
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

	(void)dlerror(); // clear error state

	if ((handle = dlopen(filePath, RTLD_LAZY))) {
	    fprintf(stderr, "found\n");
	    *dll = handle;
	    return strdup(element);
	}
	
	const char *err = dlerror();
	if (err) fprintf(stderr, "nope (%s)\n", err);
	else fprintf(stderr, "nope\n");
    }
    
    return 0;
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
	
    if (!strcasecmp(dllBase + strlen(dllBase) - 3, ".so")) {
	dllBase[strlen(dllBase) - 3] = '\0';
    }

    subpath = (char *)malloc(strlen(directory) + strlen(dllBase) + 2);
    sprintf(subpath, "%s/%s", directory, dllBase);

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
	    closedir(subdir);
	    return;
	}

	free(filename);
    }	

    fprintf(stderr, "dssi_example_host: no GUI found for plugin \"%s\" in \"%s/\"\n",
	    label, subpath);
    free(subpath);
    closedir(subdir);
}

void
query_programs()
{
    int i;

    /* free old lot */
    if (pluginPrograms) {
	free((char *)pluginPrograms);
	pluginPrograms = 0;
	pluginProgramCount = 0;
    }

    fprintf(stderr, "dssi_example_host: querying programs\n");

    for (i = 0; i < channelCount; ++i) {
	programData[i].pendingBankLSB = -1;
	programData[i].pendingBankMSB = -1;
	programData[i].pendingProgram = -1;
    }

    if (pluginDescriptor->get_program && pluginDescriptor->select_program) {

	/* Count the plugins first */
	for (i = 0; pluginDescriptor->get_program(pluginHandle, i); ++i);

	fprintf(stderr, "dssi_example_host: %d programs found\n", i);

	if (i > 0) {
	    pluginProgramCount = i;
	    pluginPrograms = (DSSI_Program_Descriptor *)
		malloc(i * sizeof(DSSI_Program_Descriptor));
	    while (i > 0) {
		--i;
		pluginPrograms[i] = *pluginDescriptor->get_program(pluginHandle, i);
                printf("dssi_example_host: program %d is MIDI bank %lu program %lu, named '%s'\n",
                       i, pluginPrograms[i].Bank, pluginPrograms[i].Program,
                       pluginPrograms[i].Name);
	    }
	    // select program at index 0 for all channels
	    for (i = 0; i < channelCount; ++i) {
		programData[i].bank = pluginPrograms[0].Bank;
		programData[i].program = pluginPrograms[0].Program;
		pluginDescriptor->select_program(pluginHandle, i,
						 programData[i].bank,
						 programData[i].program);
		programData[i].programUpdated = 1;
	    }
	}
    }
}

int
main(int argc, char **argv)
{
    int portid;
    int npfd;
    struct pollfd *pfd;

    char *dllName;
    char *label = 0;
    void *pluginObject = 0;
    char *directory;
    const char **ports;
    char path_buffer[32];
    char *tmp;
    char *url;
    int i;

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

    /* Parse args and report usage */

    if (argc < 2 || argc > 3) {
	fprintf(stderr, "dssi_example_host: Usage: %s dllname [label]\n", argv[0]);
	return 2;
    }

    dllName = argv[1];
    if (argc > 2) label = argv[2];

    /* Load plugin DLL and look for requested plugin */

    directory = load(dllName, &pluginObject);

    if (!directory || !pluginObject) {
	fprintf(stderr, "dssi_example_host: Error: Failed to load plugin DLL %s\n", dllName);
	return 1;
    }

    DSSI_Descriptor_Function descfn = (DSSI_Descriptor_Function)
	dlsym(pluginObject, "dssi_descriptor");

    if (!descfn) {
	fprintf(stderr, "dssi_example_host: Error: %s is not a DSSI plugin DLL\n", dllName);
	return 1;
    } 

    for (i = 0; ; ++i) {
	pluginDescriptor = descfn(i);
	if (!pluginDescriptor) break;
	if (!label ||
	    !strcmp(pluginDescriptor->LADSPA_Plugin->Label, label)) break;
    }

    if (!pluginDescriptor) {
	fprintf(stderr, "dssi_example_host: Error: Plugin label %s not found in DLL %s\n",
		label ? label : "(none)", dllName);
	fprintf(stderr, "dssi_example_host: Available label(s) are:\n");
	for (i = 0; ; ++i) {
	    pluginDescriptor = descfn(i);
	    if (!pluginDescriptor) {
		if (i == 0) fprintf(stderr, "(none)\n");
		break;
	    }
	    fprintf(stderr, "dssi_example_host: %d: %s\n", i,
		    pluginDescriptor->LADSPA_Plugin->Label);
	}
	return 1;
    }

    /* Count number of i/o buffers and ports required */

    ins = outs = controlIns = controlOuts = 0;

    for (i = 0; i < pluginDescriptor->LADSPA_Plugin->PortCount; ++i) {

	LADSPA_PortDescriptor pod =
	    pluginDescriptor->LADSPA_Plugin->PortDescriptors[i];

	if (LADSPA_IS_PORT_AUDIO(pod)) {

	    if (LADSPA_IS_PORT_INPUT(pod)) ++ins;
	    else if (LADSPA_IS_PORT_OUTPUT(pod)) ++outs;

	} else if (LADSPA_IS_PORT_CONTROL(pod)) {

	    if (LADSPA_IS_PORT_INPUT(pod)) ++controlIns;
	    else if (LADSPA_IS_PORT_OUTPUT(pod)) ++controlOuts;
	}
    }

    /* Create buffers and JACK client and ports */

    if ((jackClient = jack_client_new("dssi_example_host")) == 0) {
        fprintf(stderr, "dssi_example_host: Error: Failed to connect to JACK server\n");
	return 1;
    }

    inputPorts = (jack_port_t **)malloc(ins * sizeof(jack_port_t *));
    pluginInputBuffers = (float **)malloc(ins * sizeof(float *));
    pluginControlIns = (float *)calloc(controlIns, sizeof(float));
    pluginControlInPortNumbers =
	(unsigned long *)malloc(controlIns * sizeof(unsigned long));
    pluginPortControlInNumbers =
	(int *)malloc(pluginDescriptor->LADSPA_Plugin->PortCount *
				sizeof(int));
    pluginPortUpdated =
	(int *)malloc(pluginDescriptor->LADSPA_Plugin->PortCount *
				sizeof(int));

    outputPorts = (jack_port_t **)malloc(outs * sizeof(jack_port_t *));
    pluginOutputBuffers = (float **)malloc(outs * sizeof(float *));
    pluginControlOuts = (float *)calloc(controlOuts, sizeof(float));
    
    for (i = 0; i < ins; ++i) {
	char portName[20];
	sprintf(portName, "in %d", i+1);
	inputPorts[i] = jack_port_register(jackClient, portName,
					   JACK_DEFAULT_AUDIO_TYPE,
					   JackPortIsInput, 0);
	pluginInputBuffers[i] =
	    (float *)calloc(jack_get_buffer_size(jackClient), sizeof(float));
    }
    
    for (i = 0; i < outs; ++i) {
	char portName[20];
	sprintf(portName, "out %d", i+1);
	outputPorts[i] = jack_port_register(jackClient, portName,
					    JACK_DEFAULT_AUDIO_TYPE,
					    JackPortIsOutput, 0);
	pluginOutputBuffers[i] = 
	    (float *)calloc(jack_get_buffer_size(jackClient), sizeof(float));
    }

    jack_set_process_callback(jackClient, audio_callback, 0);

    channelCount = pluginDescriptor->ChannelCount;
    if (channelCount == 0) channelCount = 1;
    programData = (ProgramData *)calloc(channelCount, sizeof(ProgramData));

    /* Instantiate plugin */

    pluginHandle = pluginDescriptor->LADSPA_Plugin->instantiate
	(pluginDescriptor->LADSPA_Plugin, jack_get_sample_rate(jackClient));

    if (!pluginHandle) {
	fprintf(stderr, "dssi_example_host: Error: Failed to instantiate plugin!\n");
	return 1;
    }

    /* Create OSC thread */

    serverThread = lo_server_thread_new(NULL, osc_error);
    snprintf((char *)osc_path, 31, "/dssi/test.1");
    tmp = lo_server_thread_get_url(serverThread);
    url = (char *)malloc(strlen(tmp) + strlen(osc_path));
    sprintf(url, "%s%s", tmp, osc_path + 1);
    printf("dssi_example_host: registering %s\n", url);
    free(tmp);

    snprintf(path_buffer, 31, "%s/control", osc_path);
    lo_server_thread_add_method(serverThread, path_buffer, "if", osc_control_handler, NULL);

    snprintf(path_buffer, 31, "%s/midi", osc_path);
    lo_server_thread_add_method(serverThread, path_buffer, "m", osc_midi_handler, NULL);

    snprintf(path_buffer, 31, "%s/update", osc_path);
    lo_server_thread_add_method(serverThread, path_buffer, "s", osc_update_handler, NULL);

    snprintf(path_buffer, 31, "%s/program", osc_path);
    lo_server_thread_add_method(serverThread, path_buffer, "iii", osc_program_handler, NULL);

    snprintf(path_buffer, 31, "%s/exiting", osc_path);
    lo_server_thread_add_method(serverThread, path_buffer, "", osc_exiting_handler, NULL);

    snprintf(path_buffer, 31, "%s/configure", osc_path);
    lo_server_thread_add_method(serverThread, path_buffer, "ss", osc_configure_handler, NULL);

    lo_server_thread_add_method(serverThread, NULL, NULL, osc_debug_handler,
				NULL);
    lo_server_thread_start(serverThread);

    /* Connect and activate plugin */

    ins = outs = controlIns = controlOuts = 0;
    
    for (i = 0; i < MIDI_CONTROLLER_COUNT; ++i) {
	controllerMap[i] = -1;
    }

    for (i = 0; i < pluginDescriptor->LADSPA_Plugin->PortCount; ++i) {

	LADSPA_PortDescriptor pod =
	    pluginDescriptor->LADSPA_Plugin->PortDescriptors[i];

	pluginPortControlInNumbers[i] = -1;
	pluginPortUpdated[i] = 0;

	if (LADSPA_IS_PORT_AUDIO(pod)) {

	    if (LADSPA_IS_PORT_INPUT(pod)) {
		pluginDescriptor->LADSPA_Plugin->connect_port
		    (pluginHandle, i, pluginInputBuffers[ins++]);

	    } else if (LADSPA_IS_PORT_OUTPUT(pod)) {
		pluginDescriptor->LADSPA_Plugin->connect_port
		    (pluginHandle, i, pluginOutputBuffers[outs++]);
	    }

	} else if (LADSPA_IS_PORT_CONTROL(pod)) {

	    if (LADSPA_IS_PORT_INPUT(pod)) {

		if (pluginDescriptor->get_midi_controller_for_port) {

		    int controller = pluginDescriptor->
			get_midi_controller_for_port(pluginHandle, i);

		    if (controller == 0) {
			MB_MESSAGE
			    ("Buggy plugin: wants mapping for bank MSB\n");
		    } else if (controller == 32) {
			MB_MESSAGE
			    ("Buggy plugin: wants mapping for bank LSB\n");
		    } else if (DSSI_IS_CC(controller)) {
			controllerMap[DSSI_CC_NUMBER(controller)] = controlIns;
		    }
		}

		pluginControlInPortNumbers[controlIns] = i;
		pluginPortControlInNumbers[i] = controlIns;

		pluginControlIns[controlIns] = get_port_default
		    (pluginDescriptor->LADSPA_Plugin, i);

		pluginDescriptor->LADSPA_Plugin->connect_port
		    (pluginHandle, i, &pluginControlIns[controlIns++]);

	    } else if (LADSPA_IS_PORT_OUTPUT(pod)) {
		pluginDescriptor->LADSPA_Plugin->connect_port
		    (pluginHandle, i, &pluginControlOuts[controlOuts++]);
	    }
	}
    }

    if (pluginDescriptor->LADSPA_Plugin->activate) {
	pluginDescriptor->LADSPA_Plugin->activate(pluginHandle);
    }

    /* Look up synth programs */
    
    query_programs();

    /* Create ALSA MIDI port */

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

    mb_init("host: ");

    if (jack_activate(jackClient)) {
        fprintf (stderr, "cannot activate jack client");
        exit(1);
    }

    ports = jack_get_ports(jackClient, NULL, NULL,
                                      JackPortIsPhysical|JackPortIsInput);    
    for (i = 0; i < outs; ++i) {
      if (ports) {
          if (ports[i]) {
              if (jack_connect(jackClient, jack_port_name(outputPorts[i]),
			       ports[i])) {
                    fprintf (stderr, "cannot connect output port %d\n", i);
              }
          } else {
              free(ports);
              ports = NULL;
          }
      }
    }
    if (ports) free(ports);

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGHUP, signalHandler);
    signal(SIGQUIT, signalHandler);
    pthread_sigmask(SIG_UNBLOCK, &_signals, 0);

    /* Attempt to locate and start up a GUI for the plugin -- but
       continue even if we can't */

    startGUI(directory, dllName, pluginDescriptor->LADSPA_Plugin->Label, url);

    MB_MESSAGE("Ready\n");

    while (1) {
	if (poll(pfd, npfd, 100) > 0) {
	    midi_callback();
	}

	/* Race conditions here, because the programs and ports are
	   updated from the audio thread.  We at least try to minimise
	   trouble by copying out before the expensive OSC call */

	if (uiTarget) {

	    for (i = 0; i < channelCount; ++i) {
		if (programData[i].programUpdated) {
		    int bank = programData[i].bank;
		    int program = programData[i].program;
		    programData[i].programUpdated = 0;
		    lo_send(uiTarget, gui_osc_program_path, "iii", i, bank, program);
		}
	    }

	    for (i = 0; i < controlIns; ++i) {
		if (pluginPortUpdated[pluginControlInPortNumbers[i]]) {
		    int port = pluginControlInPortNumbers[i];
		    float value = pluginControlIns[i];
		    pluginPortUpdated[pluginControlInPortNumbers[i]] = 0;
		    lo_send(uiTarget, gui_osc_control_path, "if", port, value);
		}
	    }
	}
    }
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
    printf("dssi_example_host: liblo server error %d in path %s: %s\n", num, path, msg);
}

int osc_midi_handler(const char *path, const char *types, lo_arg **argv, int argc,
		     void *data, void *user_data) 
{
    /* Normally a host would have one of these per plugin instance */
    static snd_midi_event_t *alsaCoder = 0;
    static snd_seq_event_t alsaEncodeBuffer[10];
    long count, i;

    if (!alsaCoder) {
	snd_midi_event_new(10, &alsaCoder);
	if (!alsaCoder) {
	    fprintf(stderr, "dssi_example_host: Failed to initialise ALSA MIDI coder!\n");
	    return 0;
	}
    }

    count = snd_midi_event_encode
	(alsaCoder, argv[0]->m, 4, alsaEncodeBuffer);
    
    pthread_mutex_lock(&midiEventBufferMutex);

    for (i = 0; i < count; ++i) {
	
	snd_seq_event_t *ev = &alsaEncodeBuffer[i];

	if (midiEventReadIndex == midiEventWriteIndex + 1) {
	    fprintf(stderr, "dssi_example_host: Warning: MIDI event buffer overflow!\n");
	    continue;
	}

	if (ev->type == SND_SEQ_EVENT_CONTROLLER) {

	    int controller = ev->data.control.param;
	    
	    if (controller == 0 || controller == 32) {
		fprintf(stderr, "dssi_example_host: Warning: UI sent bank select controller (should use /program OSC call), ignoring\n");
		continue;
	    }

	} else if (ev->type == SND_SEQ_EVENT_PGMCHANGE) {

	    fprintf(stderr, "dssi_example_host: Warning: UI sent program change (should use /program OSC call), ignoring\n");
	    continue;
	}

	midiEventBuffer[midiEventWriteIndex] = *ev;
	ev = &midiEventBuffer[midiEventWriteIndex];
	
	if (ev->type == SND_SEQ_EVENT_NOTEON && ev->data.note.velocity == 0) {
	    ev->type =  SND_SEQ_EVENT_NOTEOFF;
	}
	
	midiEventWriteIndex = (midiEventWriteIndex + 1) % EVENT_BUFFER_SIZE;
    }

    pthread_mutex_unlock(&midiEventBufferMutex);

    return 0;
}


int osc_control_handler(const char *path, const char *types, lo_arg **argv, int argc,
			void *data, void *user_data) 
{
    int port = argv[0]->i;
    LADSPA_Data value = argv[1]->f;

    if (port < 0 || port > pluginDescriptor->LADSPA_Plugin->PortCount) {
	fprintf(stderr, "dssi_example_host: OSC: port number (%d) is out of range\n", port);
	return 0;
    }
    if (pluginPortControlInNumbers[port] == -1) {
	fprintf(stderr, "dssi_example_host: OSC: port %d is not a control in\n", port);
	return 0;
    }
    pluginControlIns[pluginPortControlInNumbers[port]] = value;
    printf("dssi_example_host: OSC: port %d = %f\n", port, value);
    
    return 0;
}

int osc_program_handler(const char *path, const char *types, lo_arg **argv, int argc,
			void *data, void *user_data) 
{
    int channel = argv[0]->i;
    int bank = argv[1]->i;
    int program = argv[2]->i;
    int i;
    int found = 0;

    if (pluginDescriptor->ChannelCount == 0) channel = 0;

    if (channel < 0 || channel >= channelCount) {
	printf("dssi_example_host: OSC: UI requested program change on invalid channel %d (range is 0-%d)\n", channel, channelCount);
	return 1;
    }

    for (i = 0; i < pluginProgramCount; ++i) {
	if (pluginPrograms[i].Bank == bank &&
	    pluginPrograms[i].Program == program) {
	    printf("dssi_example_host: OSC: setting bank %d, program %d, name %s\n", bank, program,
		   pluginPrograms[i].Name);
	    found = 1;
	    break;
	}
    }

    if (!found) {
	printf("dssi_example_host: OSC: UI requested unknown program: bank %d, program %d: sending to plugin anyway (plugin should ignore it)\n",
		bank, program);
    }

    programData[channel].pendingBankMSB = bank / 128;
    programData[channel].pendingBankLSB = bank % 128;
    programData[channel].pendingProgram = program;

    return 0;
}

int osc_configure_handler(const char *path, const char *types, lo_arg **argv, int argc,
			  void *data, void *user_data) 
{
    const char *key = (const char *)&argv[0]->s;
    const char *value = (const char *)&argv[1]->s;
    char *message;

    /* This is the simplest legal implementation of configure in a
       DSSI host.  The host has the option to remember the set of
       (key,value) pairs associated with a particular instance, so
       that if it wants to restore the "same" instance on another
       occasion it can just call configure() on it for each of those
       pairs and so restore state without any input from a GUI.  Any
       real-world GUI host will probably want to do that.  This host
       doesn't have any concept of restoring an instance from one run
       to the next, so we don't bother remembering these at all. */
    
    if (pluginDescriptor->configure) {

	message = pluginDescriptor->configure(pluginHandle, key, value);
        if (message) {
            printf("dssi_example_host: on configure '%s' '%s', plugin returned '%s'\n",
                   message, key, value);
            free(message);
        }

	/* configure invalidates bank and program information, so
	   we should do this again now: */
	query_programs();
    }

    return 0;
}

int osc_update_handler(const char *path, const char *types, lo_arg **argv, int argc,
		       void *data, void *user_data) 
{
    const char *url = (char *)&argv[0]->s;
    unsigned int i;
    char *host, *port;
    static int first_update = 1; /* should be per-UI, but we only support one */

    printf("dssi_example_host: OSC: got update request from <%s>\n", url);

    host = lo_url_get_hostname(url);
    port = lo_url_get_port(url);
    uiTarget = lo_target_new(host, port);
    free(host);
    free(port);

    path = lo_url_get_path(url);

    if (gui_osc_control_path) free(gui_osc_control_path);
    gui_osc_control_path = (char *)malloc(strlen(path) + 10);
    sprintf(gui_osc_control_path, "%s/control", path);

    if (gui_osc_program_path) free(gui_osc_program_path);
    gui_osc_program_path = (char *)malloc(strlen(path) + 10);
    sprintf(gui_osc_program_path, "%s/program", path);

    if (gui_osc_show_path) free(gui_osc_show_path);
    gui_osc_show_path = (char *)malloc(strlen(path) + 10);
    sprintf(gui_osc_show_path, "%s/show", path);

    free((char *)path);

    for (i = 0; i < controlIns; i++) {
	int port = pluginControlInPortNumbers[i];
	lo_send(uiTarget, gui_osc_control_path, "if", port, pluginControlIns[i]);
    }

    for (i = 0; i < channelCount; ++i) {
	lo_send(uiTarget, gui_osc_program_path, "iii", i,
		programData[i].bank, programData[i].program);
    }

    if (first_update) {
	lo_send(uiTarget, gui_osc_show_path, "");
	first_update = 0;
    }

    /* At this point a more substantial host might also call
       configure() on the UI to set any state that it had remembered
       for the plugin instance.  But we don't remember state for
       plugin instances (see our own configure() implementation in
       osc_configure_handler), and so we have nothing to send. */

    return 0;
}

int osc_exiting_handler(const char *path, const char *types, lo_arg **argv,
		      int argc, void *data, void *user_data)
{
    printf("dssi_example_host: UI is exiting; let's go too\n");
    exit(0);
}

int osc_debug_handler(const char *path, const char *types, lo_arg **argv,
		      int argc, void *data, void *user_data)
{
    int i;

    printf("dssi_example_host: got unhandled OSC message:\npath: <%s>\n", path);
    for (i=0; i<argc; i++) {
        printf("dssi_example_host: arg %d '%c' ", i, types[i]);
        lo_arg_pp(types[i], argv[i]);
        printf("dssi_example_host: \n"); 
    }
    printf("dssi_example_host: \n");

    return 1;
}

/* -*- c-basic-offset: 4 -*-  vi:set ts=8 sts=4 sw=4: */

/* dssi_example_host.c

   Disposable Soft Synth Interface version 0.1
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
#include <lo/lo.h>

#include "message_buffer.h"
#include "osc_url.h"

static snd_seq_t *alsaClient;

static jack_client_t *jackClient;
static jack_port_t **inputPorts, **outputPorts;

static int ins, outs;
static float **pluginInputBuffers, **pluginOutputBuffers;

static int controlIns, controlOuts;
static float *pluginControlIns, *pluginControlOuts;
static unsigned long *pluginControlInPortNumbers;
static int *pluginPortControlInNumbers;

static LADSPA_Handle pluginHandle = 0;
static const DSSI_Descriptor *pluginDescriptor = 0;
static const char osc_path[32];

lo_server_thread serverThread;
lo_target uiTarget;

#define EVENT_BUFFER_SIZE 1024
static snd_seq_event_t midiEventBuffer[EVENT_BUFFER_SIZE]; /* ring buffer */
static int midiEventReadIndex = 0, midiEventWriteIndex = 0;

#define MIDI_CONTROLLER_COUNT 128
static long controllerMap[MIDI_CONTROLLER_COUNT]; /* contains indices into pluginControlIns */

LADSPA_Data get_port_default(const LADSPA_Descriptor *plugin, int port);

void osc_error(int num, const char *m, const char *path);

int osc_handler(const char *path, const char *types, lo_arg **argv, int argc,
                 void *data, void *user_data) ;
int update_handler(const char *path, const char *types, lo_arg **argv, int
		    argc, void *data, void *user_data) ;
int debug_handler(const char *path, const char *types, lo_arg **argv, int
		    argc, void *data, void *user_data) ;
char *osc_url_get_hostname(const char *url);
char *osc_url_get_port(const char *url);

void
midi_callback()
{
    snd_seq_event_t *ev = 0;

    do {
	if (snd_seq_event_input(alsaClient, &ev) > 0) {

	    if (midiEventReadIndex == midiEventWriteIndex + 1) {
		fprintf(stderr, "Warning: MIDI event buffer overflow!\n");
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
    
    printf("MIDI controller %d=%d -> control in %ld=%f\n",
	   event->data.control.param, event->data.control.value,
	   controlIn, value);

    pluginControlIns[controlIn] = value;
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
	
	if (ev->type == SND_SEQ_EVENT_CONTROLLER) {

	    int controller = ev->data.control.param;
#ifdef DEBUG
	    MB_MESSAGE("CC %d(0x%02x) = %d\n", controller, controller,
		    ev->data.control.value);
#endif

	    /* need to check for bank select, and also handle program
	       changes around here */

	    if (controller == 0 || controller == 32) { // bank: ignore for now

	    } else if (controller > 0 && controller < MIDI_CONTROLLER_COUNT) {

		long controlIn = controllerMap[controller];
		if (controlIn >= 0) {
		    setControl(controlIn, ev);
		}
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

void *
load(const char *dllName)
{
    char *dssiPath = getenv("DSSI_PATH");
    char *ladspaPath = getenv("LADSPA_PATH");
    char *path, *origPath, *element;
    void *handle = 0;

    if (!dssiPath && !ladspaPath) {
	fprintf(stderr, "Error: Neither DSSI_PATH nor LADSPA_PATH is set\n");
	return 0;
    } else if (!dssiPath) {
	fprintf(stderr, "Warning: DSSI_PATH not set, using LADSPA_PATH only\n");
    }

    path = (char *)malloc((dssiPath   ? strlen(dssiPath)       : 0) +
			  (ladspaPath ? strlen(ladspaPath) + 1 : 0)) + 1;
			  
    sprintf(path, "%s%s%s",
	    dssiPath ? dssiPath : "",
	    ladspaPath ? ":" : "",
	    ladspaPath ? ladspaPath : "");

    origPath = path;

    while ((element = strtok(path, ":")) != 0) {

	path = 0;

	if (element[0] != '/') {
	    fprintf(stderr, "Ignoring relative element %s in path\n", element);
	    continue;
	}

	fprintf(stderr, "Looking for %s in %s... ", dllName, element);

	char *filePath = (char *)malloc(strlen(element) + strlen(dllName) + 2);
	sprintf(filePath, "%s/%s", element, dllName);

	if ((handle = dlopen(filePath, RTLD_LAZY))) {
	    fprintf(stderr, "found\n");
	    return handle;
	}

	fprintf(stderr, "nope\n");
    }
    
    return 0;
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
    const char **ports;
    char update_path[32];
    int i;

    /* Parse args and report usage */

    if (argc < 2 || argc > 3) {
	fprintf(stderr, "Usage: %s dllname [label]\n", argv[0]);
	return 2;
    }

    dllName = argv[1];
    if (argc > 2) label = argv[2];

    /* Load plugin DLL and look for requested plugin */

    if (!(pluginObject = load(dllName))) {
	fprintf(stderr, "Error: Failed to load plugin DLL %s\n", dllName);
	return 1;
    }

    DSSI_Descriptor_Function descfn = (DSSI_Descriptor_Function)
	dlsym(pluginObject, "dssi_descriptor");

    if (!descfn) {
	fprintf(stderr, "Error: %s is not a DSSI plugin DLL\n", dllName);
	return 1;
    } 

    for (i = 0; ; ++i) {
	pluginDescriptor = descfn(i);
	if (!pluginDescriptor) break;
	if (!label ||
	    !strcmp(pluginDescriptor->LADSPA_Plugin->Label, label)) break;
    }

    if (!pluginDescriptor) {
	fprintf(stderr, "Error: Plugin label %s not found in DLL %s\n",
		label ? label : "(none)", dllName);
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
        fprintf(stderr, "Error: Failed to connect to JACK server\n");
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

    /* Instantiate and connect plugin */

    pluginHandle = pluginDescriptor->LADSPA_Plugin->instantiate
	(pluginDescriptor->LADSPA_Plugin, jack_get_sample_rate(jackClient));

    if (!pluginHandle) {
	fprintf(stderr, "Error: Failed to instantiate plugin!\n");
	return 1;
    }

    /* Create OSC thread */

    serverThread = lo_server_thread_new("4444", osc_error);
    //snprintf((char *)osc_path, 31, "/dssi/%d.1", (int)getpid());
    snprintf((char *)osc_path, 31, "/dssi/test.1");
    snprintf(update_path, 31, "%s/update", osc_path);
    printf("registering osc://localhost:4444%s\n", osc_path);
    lo_server_thread_add_method(serverThread, osc_path, "if", osc_handler,
				NULL);
    lo_server_thread_add_method(serverThread, update_path, "s", update_handler,
				NULL);
    lo_server_thread_add_method(serverThread, NULL, NULL, debug_handler,
				NULL);
    lo_server_thread_start(serverThread);

    ins = outs = controlIns = controlOuts = 0;
    
    for (i = 0; i < MIDI_CONTROLLER_COUNT; ++i) {
	controllerMap[i] = -1;
    }

    for (i = 0; i < pluginDescriptor->LADSPA_Plugin->PortCount; ++i) {

	LADSPA_PortDescriptor pod =
	    pluginDescriptor->LADSPA_Plugin->PortDescriptors[i];

	pluginPortControlInNumbers[i] = -1;

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

    /* Create ALSA MIDI port */

    if (snd_seq_open(&alsaClient, "hw", SND_SEQ_OPEN_DUPLEX, 0) < 0) {
	fprintf(stderr, "Error: Failed to open ALSA sequencer interface\n");
	return 1;
    }

    snd_seq_set_client_name(alsaClient, "dssi_example_host");

    if ((portid = snd_seq_create_simple_port
	 (alsaClient, "dssi_example_host",
	  SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE, 0)) < 0) {
	fprintf(stderr, "Error: Failed to create ALSA sequencer port\n");
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
                      ports[0])) {
                    fprintf (stderr, "cannot connect output port %d\n", i);
              }
          } else {
              free(ports);
              ports = NULL;
          }
      }
    }
    if (ports) free(ports);

    MB_MESSAGE("Ready\n");

    while (1) {
	if (poll(pfd, npfd, 100000) > 0) {
	    midi_callback();
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
    printf("liblo server error %d in path %s: %s\n", num, path, msg);
}

int osc_handler(const char *path, const char *types, lo_arg **argv, int argc,
                 void *data, void *user_data) 
{
    int port = argv[0]->i;
    LADSPA_Data value = argv[1]->f;

    if (port < 0 || port > pluginDescriptor->LADSPA_Plugin->PortCount) {
	fprintf(stderr, "OSC: port number (%d) is out of range\n", port);
	return 0;
    }
    if (pluginPortControlInNumbers[port] == -1) {
	fprintf(stderr, "OSC: port %d is not a control in\n", port);
	return 0;
    }
    pluginControlIns[pluginPortControlInNumbers[port]] = value;
    printf("OSC: port %d = %f\n", port, value);
    
    return 0;
}

int update_handler(const char *path, const char *types, lo_arg **argv, int argc,
                 void *data, void *user_data) 
{
    const char *url = &argv[0]->s;
    unsigned int i;
    char *host, *port;

    printf("OSC: got update request from <%s>\n", url);
    host = osc_url_get_hostname(url);
    port = osc_url_get_port(url);
    uiTarget = lo_target_new(host, port);
    free(host);
    free(port);
    for (i=0; i<controlIns; i++) {
	int port = pluginControlInPortNumbers[i];
	lo_send(uiTarget, osc_path, "if", port, pluginControlIns[i]);
    }

    return 0;
}

int debug_handler(const char *path, const char *types, lo_arg **argv,
                    int argc, void *data, void *user_data)
{
    int i;

    printf("got unhandled OSC message:\npath: <%s>\n", path);
    for (i=0; i<argc; i++) {
        printf("arg %d '%c' ", i, types[i]);
        lo_arg_pp(types[i], argv[i]);
        printf("\n"); 
    }
    printf("\n");

    return 1;
}

/* -*- c-basic-offset: 4 -*- */

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

static snd_seq_t *alsaClient;

static jack_client_t *jackClient;
static jack_port_t **inputPorts, **outputPorts;

static int ins, outs;
static float **pluginInputBuffers, **pluginOutputBuffers;

static int controlIns, controlOuts;
static float *pluginControlIns, *pluginControlOuts;

static LADSPA_Handle pluginHandle = 0;
static const DSSI_Descriptor *pluginDescriptor = 0;

#define EVENT_BUFFER_SIZE 1024
static snd_seq_event_t midiEventBuffer[EVENT_BUFFER_SIZE]; /* ring buffer */
static int midiEventReadIndex = 0, midiEventWriteIndex = 0;

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

int
audio_callback(jack_nframes_t nframes, void *arg)
{
    static snd_seq_event_t processEventBuffer[EVENT_BUFFER_SIZE];
    int i = 0, count = 0;

    while (midiEventReadIndex != midiEventWriteIndex) {

	if (count == EVENT_BUFFER_SIZE) break;
	
	processEventBuffer[count] = midiEventBuffer[midiEventReadIndex];
	midiEventReadIndex = (midiEventReadIndex + 1) % EVENT_BUFFER_SIZE;

	++count;
    }

    /* ... also need to do controller mapping */

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

    ins = outs = controlIns = controlOuts = 0;

    for (i = 0; i < pluginDescriptor->LADSPA_Plugin->PortCount; ++i) {

	LADSPA_PortDescriptor pod =
	    pluginDescriptor->LADSPA_Plugin->PortDescriptors[i];

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

    if (jack_activate(jackClient)) {
        fprintf (stderr, "cannot activate jack client");
        exit(1);
    }

    printf("Ready\n");

    while (1) {
	if (poll(pfd, npfd, 100000) > 0) {
	    midi_callback();
	}  
    }
}


/* -*- c-basic-offset: 4 -*-  vi:set ts=8 sts=4 sw=4: */

/* trivial_sampler.c

   Disposable Hosted Soft Synth API
   Constructed by Chris Cannam, Steve Harris and Sean Bolton

   A straightforward DSSI plugin sampler.

   This example file is in the public domain.
*/

#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <math.h>

#include "dssi.h"
#include "ladspa.h"

#include <sndfile.h>
#include <samplerate.h>
#include <pthread.h>

static LADSPA_Descriptor *samplerLDescriptor = NULL;
static DSSI_Descriptor *samplerDDescriptor = NULL;

#define Sampler_OUTPUT 0
#define Sampler_BASE_PITCH 1
#define Sampler_SUSTAIN 2
#define Sampler_COUNT 3

#define MIDI_NOTES 128
#define MAX_SAMPLE_COUNT 1048576

typedef struct {
    LADSPA_Data *output;
    LADSPA_Data *basePitch;
    LADSPA_Data *sustain;
    float       *sampleData;
    size_t       sampleCount;
    int          sampleRate;
    long         onsets[MIDI_NOTES];
    char         velocities[MIDI_NOTES];
    long         sampleNo;
    pthread_mutex_t mutex;
} Sampler;

static void runSampler(LADSPA_Handle instance, unsigned long sample_count,
		       snd_seq_event_t *events, unsigned long EventCount);

const LADSPA_Descriptor *ladspa_descriptor(unsigned long index)
{
    switch (index) {
    case 0:
	return samplerLDescriptor;
    default:
	return NULL;
    }
}

const DSSI_Descriptor *dssi_descriptor(unsigned long index)
{
    switch (index) {
    case 0:
	return samplerDDescriptor;
    default:
	return NULL;
    }
}

static void cleanupSampler(LADSPA_Handle instance)
{
    free(instance);
}

static void connectPortSampler(LADSPA_Handle instance, unsigned long port,
			       LADSPA_Data * data)
{
    Sampler *plugin;

    plugin = (Sampler *) instance;
    switch (port) {
    case Sampler_OUTPUT:
	plugin->output = data;
	break;
    case Sampler_BASE_PITCH:
	plugin->basePitch = data;
	break;
    case Sampler_SUSTAIN:
	plugin->sustain = data;
	break;
    }
}

static LADSPA_Handle instantiateSampler(const LADSPA_Descriptor * descriptor,
				   unsigned long s_rate)
{
    Sampler *plugin_data = (Sampler *) malloc(sizeof(Sampler));

    plugin_data->sampleRate = s_rate;
    plugin_data->sampleData = 0;
    plugin_data->sampleCount = 0;

    pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    memcpy(&plugin_data->mutex, &m, sizeof(pthread_mutex_t));

    return (LADSPA_Handle) plugin_data;
}

static void activateSampler(LADSPA_Handle instance)
{
    Sampler *plugin_data = (Sampler *) instance;
    unsigned int i;

    plugin_data->sampleNo = 0;

    for (i = 0; i < MIDI_NOTES; i++) {
	plugin_data->onsets[i] = -1;
    }
}

static void runSamplerWrapper(LADSPA_Handle instance,
			 unsigned long sample_count)
{
    runSampler(instance, sample_count, NULL, 0);
}

static void addSample(Sampler *plugin_data, int n,
		      LADSPA_Data *const output,
		      unsigned long pos, unsigned long count)
{
    float ratio = 1.0;
    float gain = 1.0;
    unsigned long i, s;

    if (plugin_data->basePitch && n != *plugin_data->basePitch) {
	ratio = powf(1.059463094, n - *plugin_data->basePitch);
    }

    if (pos + plugin_data->sampleNo < plugin_data->onsets[n]) return;

    gain = (float)plugin_data->velocities[n] / 127.0f;

    for (i = 0, s = pos + plugin_data->sampleNo - plugin_data->onsets[n];
	 i < count;
	 ++i, ++s) {

	unsigned long rs = (unsigned long)(s * ratio);

	if (rs >= plugin_data->sampleCount) {
	    plugin_data->onsets[n] = -1;
	    break;
	}

	output[pos + i] += gain * plugin_data->sampleData[rs];
    }
}

static void runSampler(LADSPA_Handle instance, unsigned long sample_count,
		       snd_seq_event_t *events, unsigned long event_count)
{
    Sampler *plugin_data = (Sampler *) instance;
    LADSPA_Data *const output = plugin_data->output;
    unsigned long pos;
    unsigned long count;
    unsigned long event_pos;
    int i;

    memset(output, 0, sample_count * sizeof(float));

    if (pthread_mutex_trylock(&plugin_data->mutex)) {
	plugin_data->sampleNo += sample_count;
	return;
    }

    if (!plugin_data->sampleData || !plugin_data->sampleCount) {
	plugin_data->sampleNo += sample_count;
	pthread_mutex_unlock(&plugin_data->mutex);
	return;
    }

    for (pos = 0, event_pos = 0; pos < sample_count; ) {

	while (event_pos < event_count
	       && pos >= events[event_pos].time.tick) {

	    if (events[event_pos].type == SND_SEQ_EVENT_NOTEON) {
		snd_seq_ev_note_t n = events[event_pos].data.note;
		if (n.velocity > 0) {
		    plugin_data->onsets[n.note] =
			plugin_data->sampleNo + events[event_pos].time.tick;
		    plugin_data->velocities[n.note] = n.velocity;
		} else {
		    plugin_data->onsets[n.note] = -1;
		}
	    } else if (events[event_pos].type == SND_SEQ_EVENT_NOTEOFF &&
		       (!plugin_data->sustain || !*plugin_data->sustain)) {
		snd_seq_ev_note_t n = events[event_pos].data.note;
		plugin_data->onsets[n.note] = -1;
	    }

	    ++event_pos;
	}

	count = sample_count - pos;
	if (event_pos < event_count &&
	    events[event_pos].time.tick < sample_count) {
	    count = events[event_pos].time.tick - pos;
	}

	for (i = 0; i < MIDI_NOTES; ++i) {
	    if (plugin_data->onsets[i] >= 0) {
		addSample(plugin_data, i, output, pos, count);
	    }
	}

	pos += count;
    }

    plugin_data->sampleNo += sample_count;
    pthread_mutex_unlock(&plugin_data->mutex);
}

int getControllerSampler(LADSPA_Handle instance, unsigned long port)
{
    if (port == Sampler_BASE_PITCH) return DSSI_CC(12);
    else if (port == Sampler_SUSTAIN) return DSSI_CC(64);
    return DSSI_NONE;
}

char *
dssi_configure_message(const char *fmt, ...)
{
    va_list args;
    char buffer[256];

    va_start(args, fmt);
    vsnprintf(buffer, 256, fmt, args);
    va_end(args);
    return strdup(buffer);
}

char *samplerLoad(Sampler *plugin_data, const char *path)
{
    SF_INFO info;
    SNDFILE *file;
    size_t samples = 0;
    float *tmpFrames, *tmpSamples, *tmpResamples, *tmpOld;
    size_t i;

    info.format = 0;
    file = sf_open(path, SFM_READ, &info);

    if (!file) {
	return dssi_configure_message
	    ("error: unable to load sample file '%s'", path);
    }
    
    if (info.frames > MAX_SAMPLE_COUNT) {
	return dssi_configure_message
	    ("error: sample file '%s' is too large (%ld frames, maximum is %ld)",
	     path, info.frames, MAX_SAMPLE_COUNT);
    }

    samples = info.frames;

    tmpSamples = (float *)malloc(samples * sizeof(float));
    if (info.channels == 1) {
	tmpFrames = 0;
	sf_read_float(file, tmpSamples, samples);
    } else {
	tmpFrames = (float *)malloc(samples * info.channels * sizeof(float));
	sf_readf_float(file, tmpFrames, samples);
	for (i = 0; i < info.frames; ++i) {
	    tmpSamples[i] = tmpFrames[i * info.channels];
	}
	free(tmpFrames);
    }

    tmpResamples = 0;
    if (info.samplerate != plugin_data->sampleRate) {
	double ratio = (double)plugin_data->sampleRate / (double)info.samplerate;
	size_t targetSamples = (size_t)(samples * ratio);
	SRC_DATA data;
	tmpResamples = (float *)malloc((targetSamples + 1) * sizeof(float));
	memset(tmpResamples, 0, (targetSamples + 1) * sizeof(float));
	data.data_in = tmpSamples;
	data.data_out = tmpResamples;
	data.input_frames = samples;
	data.output_frames = targetSamples;
	data.src_ratio = ratio;
	if (!src_simple(&data, SRC_SINC_BEST_QUALITY, 1)) {
	    free(tmpSamples);
	    tmpSamples = tmpResamples;
	    samples = targetSamples;
	} else {
	    free(tmpResamples);
	}
    }

    pthread_mutex_lock(&plugin_data->mutex);

    tmpOld = plugin_data->sampleData;
    plugin_data->sampleData = tmpSamples;
    plugin_data->sampleCount = samples;

    pthread_mutex_unlock(&plugin_data->mutex);

    if (tmpOld) {
	free(tmpOld);
    }

    sf_close(file);

    if (tmpFrames) free(tmpFrames);

    printf("loaded %s\n", path);

    return NULL;
}

char *samplerConfigure(LADSPA_Handle instance, const char *key, const char *value)
{
    Sampler *plugin_data = (Sampler *)instance;
    
    if (!strcmp(key, "load")) {
	return samplerLoad(plugin_data, value);
    }

    return strdup("error: unrecognized configure key");
}

void _init()
{
    char **port_names;
    LADSPA_PortDescriptor *port_descriptors;
    LADSPA_PortRangeHint *port_range_hints;

    samplerLDescriptor =
	(LADSPA_Descriptor *) malloc(sizeof(LADSPA_Descriptor));
    if (samplerLDescriptor) {
	samplerLDescriptor->UniqueID = 6543;
	samplerLDescriptor->Label = "trivial_sampler";
	samplerLDescriptor->Properties = 0;
	samplerLDescriptor->Name = "Simple Mono Sampler";
	samplerLDescriptor->Maker = "Chris Cannam <cannam@all-day-breakfast.com>";
	samplerLDescriptor->Copyright = "GPL";
	samplerLDescriptor->PortCount = Sampler_COUNT;

	port_descriptors = (LADSPA_PortDescriptor *)
				calloc(samplerLDescriptor->PortCount, sizeof
						(LADSPA_PortDescriptor));
	samplerLDescriptor->PortDescriptors =
	    (const LADSPA_PortDescriptor *) port_descriptors;

	port_range_hints = (LADSPA_PortRangeHint *)
				calloc(samplerLDescriptor->PortCount, sizeof
						(LADSPA_PortRangeHint));
	samplerLDescriptor->PortRangeHints =
	    (const LADSPA_PortRangeHint *) port_range_hints;

	port_names = (char **) calloc(samplerLDescriptor->PortCount, sizeof(char *));
	samplerLDescriptor->PortNames = (const char **) port_names;

	/* Parameters for output */
	port_descriptors[Sampler_OUTPUT] = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
	port_names[Sampler_OUTPUT] = "Output";
	port_range_hints[Sampler_OUTPUT].HintDescriptor = 0;

	/* Parameters for tune */
	port_descriptors[Sampler_BASE_PITCH] = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
	port_names[Sampler_BASE_PITCH] = "Base MIDI Pitch";
	port_range_hints[Sampler_BASE_PITCH].HintDescriptor =
	    LADSPA_HINT_INTEGER |
	    LADSPA_HINT_DEFAULT_MIDDLE |
	    LADSPA_HINT_BOUNDED_BELOW | 
	    LADSPA_HINT_BOUNDED_ABOVE;
	port_range_hints[Sampler_BASE_PITCH].LowerBound = 0;
	port_range_hints[Sampler_BASE_PITCH].UpperBound = 120; // not 127, as we want 120/2 = 60 as the default

	/* Parameters for sustain */
	port_descriptors[Sampler_SUSTAIN] = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
	port_names[Sampler_SUSTAIN] = "Sustain on/off";
	port_range_hints[Sampler_SUSTAIN].HintDescriptor =
			LADSPA_HINT_DEFAULT_MINIMUM |
			LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE;
	port_range_hints[Sampler_SUSTAIN].LowerBound = 0.0f;
	port_range_hints[Sampler_SUSTAIN].UpperBound = 127.0f;

	samplerLDescriptor->activate = activateSampler;
	samplerLDescriptor->cleanup = cleanupSampler;
	samplerLDescriptor->connect_port = connectPortSampler;
	samplerLDescriptor->deactivate = NULL;
	samplerLDescriptor->instantiate = instantiateSampler;
	samplerLDescriptor->run = runSamplerWrapper;
	samplerLDescriptor->run_adding = NULL;
	samplerLDescriptor->set_run_adding_gain = NULL;
    }

    samplerDDescriptor = (DSSI_Descriptor *) malloc(sizeof(DSSI_Descriptor));
    if (samplerDDescriptor) {
	samplerDDescriptor->DSSI_API_Version = 1;
	samplerDDescriptor->LADSPA_Plugin = samplerLDescriptor;
	samplerDDescriptor->configure = samplerConfigure;
	samplerDDescriptor->get_program = NULL;
	samplerDDescriptor->get_midi_controller_for_port = getControllerSampler;
	samplerDDescriptor->select_program = NULL;
	samplerDDescriptor->run_synth = runSampler;
	samplerDDescriptor->run_synth_adding = NULL;
	samplerDDescriptor->run_multiple_synths = NULL;
	samplerDDescriptor->run_multiple_synths_adding = NULL;
    }
}

void _fini()
{
    if (samplerLDescriptor) {
	free((LADSPA_PortDescriptor *) samplerLDescriptor->PortDescriptors);
	free((char **) samplerLDescriptor->PortNames);
	free((LADSPA_PortRangeHint *) samplerLDescriptor->PortRangeHints);
	free(samplerLDescriptor);
    }
    if (samplerDDescriptor) {
	free(samplerDDescriptor);
    }
}

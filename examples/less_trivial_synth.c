/* -*- c-basic-offset: 4 -*-  vi:set ts=8 sts=4 sw=4: */

/* less_trivial_synth.c

   Disposable Hosted Soft Synth API version 0.1
   Constructed by Chris Cannam and Steve Harris

   This is an example DSSI synth plugin written by Steve Harris.

   This example file is in the public domain.
*/

#include <stdlib.h>
#include <limits.h>
#include <string.h>

#include <math.h>
#include <stdio.h>

#include "dssi.h"
#include "ladspa.h"

#define LTS_OUTPUT  0
#define LTS_FREQ    1
#define LTS_ATTACK  2
#define LTS_DECAY   3
#define LTS_SUSTAIN 4
#define LTS_RELEASE 5
#define LTS_COUNT   6 /* must be 1 + higest value above */

#define MIDI_NOTES 128

#define GLOBAL_GAIN 0.25f

static LADSPA_Descriptor *ltsLDescriptor = NULL;
static DSSI_Descriptor *ltsDDescriptor = NULL;

typedef enum {
    inactive = 0,
    attack,
    decay,
    sustain,
    release
} state_t;

typedef struct {
    state_t state;
    float amp;
    float env;
    float env_d;
    double phase;
    int counter;
    int next_event;
} note_data;

typedef struct {
    LADSPA_Data freq;
    LADSPA_Data attack;
    LADSPA_Data decay;
    LADSPA_Data sustain;
    LADSPA_Data release;
} synth_vals;

typedef struct {
    LADSPA_Data *output;
    LADSPA_Data *freq;
    LADSPA_Data *attack;
    LADSPA_Data *decay;
    LADSPA_Data *sustain;
    LADSPA_Data *release;
    note_data data[MIDI_NOTES];
    float omega[MIDI_NOTES];
    float fs;
} LTS;

static void runLTS(LADSPA_Handle instance, unsigned long sample_count,
		  snd_seq_event_t * events, unsigned long EventCount);

static void run_voice(LTS *p, synth_vals *vals, int note, note_data *d,
		      LADSPA_Data *out);

const LADSPA_Descriptor *ladspa_descriptor(unsigned long index)
{
    switch (index) {
    case 0:
	return ltsLDescriptor;
    default:
	return NULL;
    }
}

const DSSI_Descriptor *dssi_descriptor(unsigned long index)
{
    switch (index) {
    case 0:
	return ltsDDescriptor;
    default:
	return NULL;
    }
}

static void cleanupLTS(LADSPA_Handle instance)
{
    free(instance);
}

static void connectPortLTS(LADSPA_Handle instance, unsigned long port,
			  LADSPA_Data * data)
{
    LTS *plugin;

    plugin = (LTS *) instance;
    switch (port) {
    case LTS_OUTPUT:
	plugin->output = data;
	break;
    case LTS_FREQ:
	plugin->freq = data;
	break;
    case LTS_ATTACK:
	plugin->attack = data;
	break;
    case LTS_DECAY:
	plugin->decay = data;
	break;
    case LTS_SUSTAIN:
	plugin->sustain = data;
	break;
    case LTS_RELEASE:
	plugin->release = data;
	break;
    }
}

static LADSPA_Handle instantiateLTS(const LADSPA_Descriptor * descriptor,
				   unsigned long s_rate)
{
    unsigned int i;

    LTS *plugin_data = (LTS *) malloc(sizeof(LTS));

    plugin_data->fs = s_rate;

    for (i=0; i<MIDI_NOTES; i++) {
	    plugin_data->omega[i] = M_PI * 2.0 / (double)s_rate *
				    pow(2.0, (i-69.0) / 12.0);
    }

    return (LADSPA_Handle) plugin_data;
}

static void activateLTS(LADSPA_Handle instance)
{
    LTS *plugin_data = (LTS *) instance;
    unsigned int i;

    for (i=0; i<MIDI_NOTES; i++) {
	plugin_data->data[i].state = inactive;
    }
}

static void runLTSWrapper(LADSPA_Handle instance,
			 unsigned long sample_count)
{
    runLTS(instance, sample_count, NULL, 0);
}

static void runLTS(LADSPA_Handle instance, unsigned long sample_count,
		  snd_seq_event_t *events, unsigned long event_count)
{
    LTS *plugin_data = (LTS *) instance;
    LADSPA_Data *const output = plugin_data->output;
    synth_vals vals;
    note_data *data = plugin_data->data;
    unsigned long pos;
    unsigned long event_pos;
    unsigned long note;

    vals.attack = *(plugin_data->attack) * plugin_data->fs;
    vals.decay = *(plugin_data->decay) * plugin_data->fs;
    vals.sustain = *(plugin_data->sustain) * 0.01f;
    vals.release = *(plugin_data->release) * plugin_data->fs;

/* XXX hacks 'til we have port control */
    if (*(plugin_data->freq) < 1.0) {
	vals.freq = 440.0f;
    } else {
	vals.freq = *(plugin_data->freq);
    }
    if (vals.attack < 1.0f) {
	vals.attack = 0.1f * plugin_data->fs;
    }
    if (vals.decay < 1.0f) {
	vals.decay = 0.1f * plugin_data->fs;
    }
    if (vals.sustain < 1.0f) {
	vals.sustain = 0.5f;
    }
    if (vals.release < 1.0f) {
	vals.release = plugin_data->fs;
    }
/* XXX end of hacks */

    for (pos = 0, event_pos = 0; pos < sample_count; pos++) {
	while (event_pos < event_count
	       && pos == events[event_pos].time.tick) {
	    if (events[event_pos].type == SND_SEQ_EVENT_NOTEON) {
		snd_seq_ev_note_t n = events[event_pos].data.note;

		if (n.velocity > 0) {
		    data[n.note].amp = n.velocity * GLOBAL_GAIN / 127.0f;
		    data[n.note].state = attack;
		    data[n.note].env = 0.0;
		    data[n.note].env_d = GLOBAL_GAIN / vals.attack;
		    data[n.note].phase = 0.0;
		    data[n.note].counter = 0;
		    data[n.note].next_event = vals.attack;
		} else {
		    data[n.note].state = release;
		    data[n.note].env_d = -vals.sustain * data[n.note].amp /
					 vals.release;
		    data[n.note].counter = 0;
		    data[n.note].next_event = vals.release;
		}
	    } else if (events[event_pos].type == SND_SEQ_EVENT_NOTEOFF) {
		snd_seq_ev_note_t n = events[event_pos].data.note;

		data[n.note].state = release;
		data[n.note].env_d = -vals.sustain * data[n.note].amp /
				     vals.release;
		data[n.note].counter = 0;
		data[n.note].next_event = vals.release;
	    }
	    event_pos++;
	}

	/* this is a crazy way to run a synths inner loop, I've
	   just done it this way so its really obvious whats going on */
	output[pos] = 0.0f;
	for (note = 0; note < MIDI_NOTES; note++) {
	    run_voice(plugin_data, &vals, note, &data[note], &output[pos]);
	}
    }
}

static void run_voice(LTS *p, synth_vals *vals, int note, note_data *d,
		      LADSPA_Data *out)
{
    if (d->state == inactive) {
	return;
    }

    d->phase += p->omega[note] * vals->freq;
    if (d->phase > M_PI * 2.0) {
	d->phase -= M_PI * 2.0;
    }
    d->env += d->env_d;

    switch (d->state) {
    case inactive:
	break;

    case attack:
    case decay:
    case release:
	*out += sin(d->phase) * d->amp * d->env;
	break;

    case sustain:
	*out += sin(d->phase) * d->amp * vals->sustain;
	break;
    }

    if ((d->counter)++ >= d->next_event) {
	switch (d->state) {
	case inactive:
	    break;
            
	case attack:
	    d->state = decay;
	    d->env_d = (1.0f - vals->sustain) / vals->decay;
	    d->counter = 0;
	    d->next_event = vals->sustain;
	    break;

	case decay:
	    d->state = sustain;
	    d->env_d = 0.0f;
	    d->counter = 0;
	    d->next_event = INT_MAX;
	    break;

	case sustain:
	    d->counter = 0;
	    break;

	case release:
	    d->state = inactive;
	    break;

	default:
	    printf("state error! (%d)\n", d->state);
	    d->state = inactive;
	    break;
	}
    }
}

void _init()
{
    char **port_names;
    LADSPA_PortDescriptor *port_descriptors;
    LADSPA_PortRangeHint *port_range_hints;

    ltsLDescriptor =
	(LADSPA_Descriptor *) malloc(sizeof(LADSPA_Descriptor));
    if (ltsLDescriptor) {
	ltsLDescriptor->UniqueID = 24;
	ltsLDescriptor->Label = "LTS";
	ltsLDescriptor->Properties = 0;
	ltsLDescriptor->Name = "Less Trivial synth";
	ltsLDescriptor->Maker = "Steve Harris <steve@plugin.org.uk>";
	ltsLDescriptor->Copyright = "Public Domain";
	ltsLDescriptor->PortCount = LTS_COUNT;

	port_descriptors = (LADSPA_PortDescriptor *)
				calloc(ltsLDescriptor->PortCount, sizeof
						(LADSPA_PortDescriptor));
	ltsLDescriptor->PortDescriptors =
	    (const LADSPA_PortDescriptor *) port_descriptors;

	port_range_hints = (LADSPA_PortRangeHint *)
				calloc(ltsLDescriptor->PortCount, sizeof
						(LADSPA_PortRangeHint));
	ltsLDescriptor->PortRangeHints =
	    (const LADSPA_PortRangeHint *) port_range_hints;

	port_names = (char **) calloc(ltsLDescriptor->PortCount, sizeof(char *));
	ltsLDescriptor->PortNames = (const char **) port_names;

	/* Parameters for output */
	port_descriptors[LTS_OUTPUT] = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
	port_names[LTS_OUTPUT] = "Output";
	port_range_hints[LTS_OUTPUT].HintDescriptor = 0;

	/* Parameters for freq */
	port_descriptors[LTS_FREQ] = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
	port_names[LTS_FREQ] = "A tuning frequency (Hz)";
	port_range_hints[LTS_FREQ].HintDescriptor = LADSPA_HINT_DEFAULT_440 |
			LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE;
	port_range_hints[LTS_FREQ].LowerBound = 410;
	port_range_hints[LTS_FREQ].UpperBound = 460;

	/* Parameters for attack */
	port_descriptors[LTS_ATTACK] = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
	port_names[LTS_ATTACK] = "Attack time (s)";
	port_range_hints[LTS_ATTACK].HintDescriptor =
			LADSPA_HINT_DEFAULT_MIDDLE |
			LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE;
	port_range_hints[LTS_ATTACK].LowerBound = 0.01f;
	port_range_hints[LTS_ATTACK].UpperBound = 4.0f;

	/* Parameters for decay */
	port_descriptors[LTS_DECAY] = port_descriptors[LTS_ATTACK];
	port_names[LTS_DECAY] = "Decay time (s)";
	port_range_hints[LTS_DECAY].HintDescriptor =
			port_range_hints[LTS_ATTACK].HintDescriptor;
	port_range_hints[LTS_DECAY].LowerBound =
			port_range_hints[LTS_ATTACK].LowerBound;
	port_range_hints[LTS_DECAY].UpperBound =
			port_range_hints[LTS_ATTACK].UpperBound;

	/* Parameters for sustain */
	port_descriptors[LTS_SUSTAIN] = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
	port_names[LTS_SUSTAIN] = "Sustain level (%)";
	port_range_hints[LTS_SUSTAIN].HintDescriptor =
			LADSPA_HINT_DEFAULT_MIDDLE |
			LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE;
	port_range_hints[LTS_SUSTAIN].LowerBound = 0.0f;
	port_range_hints[LTS_SUSTAIN].UpperBound = 100.0f;

	/* Parameters for release */
	port_descriptors[LTS_RELEASE] = port_descriptors[LTS_ATTACK];
	port_names[LTS_RELEASE] = "Decay time (s)";
	port_range_hints[LTS_RELEASE].HintDescriptor =
			port_range_hints[LTS_ATTACK].HintDescriptor;
	port_range_hints[LTS_RELEASE].LowerBound =
			port_range_hints[LTS_ATTACK].LowerBound;
	port_range_hints[LTS_RELEASE].UpperBound =
			port_range_hints[LTS_ATTACK].UpperBound;

	ltsLDescriptor->activate = activateLTS;
	ltsLDescriptor->cleanup = cleanupLTS;
	ltsLDescriptor->connect_port = connectPortLTS;
	ltsLDescriptor->deactivate = NULL;
	ltsLDescriptor->instantiate = instantiateLTS;
	ltsLDescriptor->run = runLTSWrapper;
	ltsLDescriptor->run_adding = NULL;
	ltsLDescriptor->set_run_adding_gain = NULL;
    }

    ltsDDescriptor = (DSSI_Descriptor *) malloc(sizeof(DSSI_Descriptor));
    if (ltsDDescriptor) {
	ltsDDescriptor->DSSI_API_Version = 1;
	ltsDDescriptor->LADSPA_Plugin = ltsLDescriptor;
	ltsDDescriptor->configure = NULL;
	ltsDDescriptor->get_program = NULL;
	ltsDDescriptor->select_program = NULL;
	ltsDDescriptor->run_synth = runLTS;
    }
}

void _fini()
{
    if (ltsLDescriptor) {
	free((LADSPA_PortDescriptor *) ltsLDescriptor->PortDescriptors);
	free((char **) ltsLDescriptor->PortNames);
	free((LADSPA_PortRangeHint *) ltsLDescriptor->PortRangeHints);
	free(ltsLDescriptor);
    }
    if (ltsDDescriptor) {
	free(ltsDDescriptor);
    }
}

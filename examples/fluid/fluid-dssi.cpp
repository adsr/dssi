/* -*- c-basic-offset: 4 -*-  vi:set ts=8 sts=4 sw=4: */

/* fluid-dssi.c
 * A DSSI plugin wrapper for FluidSynth.
 * Copyright (C) 2004 Chris Cannam
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307, USA.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <dirent.h>

#include <ladspa.h>
#include <dssi.h>
#include <fluidsynth.h>

#include <string>
#include <vector>

#include "fluid-dssi.h"

using namespace std;

struct FluidSynthInstance {

    LADSPA_Data **audioPorts;
    LADSPA_Data **controlPorts;
    LADSPA_Data  *controlBackups;

    DSSI_Program_Descriptor *programs;
    int programCount;

    fluid_synth_t *synth;
    int soundFontId;

};

struct FluidDescriptorSet {
    
    FluidDescriptorSet();
    ~FluidDescriptorSet();

    vector<DSSI_Descriptor *> descriptors;
};

static FluidDescriptorSet descriptors;
    

static void runSynth(LADSPA_Handle, unsigned long, snd_seq_event_t *, unsigned long);
static void updatePrograms(FluidSynthInstance *);

    
static void
connectPort(LADSPA_Handle instance, unsigned long port, LADSPA_Data *data)
{
    FluidSynthInstance *synth = (FluidSynthInstance *)instance;

    if (port <= AUDIO_PORT_COUNT) {
	synth->audioPorts[port] = data;
    } else {
	synth->controlPorts[port - AUDIO_PORT_COUNT] = data;
    }
}

static void
activate(LADSPA_Handle instance)
{
    FluidSynthInstance *synth = (FluidSynthInstance *)instance;
    updatePrograms(synth);
}

static void
run(LADSPA_Handle instance, unsigned long nframes)
{
    runSynth(instance, nframes, NULL, 0);
}

void
deactivate(LADSPA_Handle instance)
{
    FluidSynthInstance *synth = (FluidSynthInstance *)instance;

    if (synth->programs) free(synth->programs);
    synth->programs = 0;
    synth->programCount = 0;
}

static void
cleanup(LADSPA_Handle instance)
{
    FluidSynthInstance *synth = (FluidSynthInstance *)instance;
    if (synth->audioPorts) free(synth->audioPorts);
    if (synth->controlPorts) free(synth->controlPorts);
    if (synth->controlBackups) free(synth->controlBackups);
    if (synth->synth) delete_fluid_synth(synth->synth);
    free(synth);
}

static void
updatePrograms(FluidSynthInstance *synth)
{
    int i;
    fluid_sfont_t *soundFont = 0;
    fluid_preset_t preset;

    if (synth->programCount > 0) free(synth->programs);
    synth->programs = 0;
    synth->programCount = 0;

    soundFont = fluid_synth_get_sfont_by_id(synth->synth, synth->soundFontId);
    if (!soundFont) {
	if (synth->soundFontId >= 0) {
	    fprintf(stderr, "updatePrograms: no soundfont with id %d!\n", 
		    synth->soundFontId);
	}
	return;
    }

    soundFont->iteration_start(soundFont);
    while (soundFont->iteration_next(soundFont, &preset)) ++synth->programCount;
    if (synth->programCount == 0) {
	fprintf(stderr, "updatePrograms: soundfont has no presets!\n");
	return;
    }

    synth->programs = (DSSI_Program_Descriptor *)calloc
	(synth->programCount, sizeof(DSSI_Program_Descriptor));

    i = 0;
    soundFont->iteration_start(soundFont);
    while (soundFont->iteration_next(soundFont, &preset)) {
	synth->programs[i].Bank = preset.get_banknum(&preset);
	synth->programs[i].Program = preset.get_num(&preset);
	synth->programs[i].Name = preset.get_name(&preset);
	++i;
    }
}	
	
static char *
configure(LADSPA_Handle instance, const char *key, const char *value)
{
    return strdup("error: unrecognized configure key");
}

static const DSSI_Program_Descriptor *
getProgram(LADSPA_Handle instance, unsigned long index)
{
    FluidSynthInstance *synth = (FluidSynthInstance *)instance;

    if (index >= 0 && index < (unsigned long)synth->programCount) {
	return &synth->programs[index];
    }
    
    return NULL;

}

static void
selectProgram(LADSPA_Handle instance, 
	      unsigned long channel,
	      unsigned long bank,
	      unsigned long program)
{
    FluidSynthInstance *synth = (FluidSynthInstance *)instance;
    if (synth->soundFontId < 0) return;
    fluid_synth_program_select(synth->synth, channel, synth->soundFontId, bank, program);
}

static int
getMidiController(LADSPA_Handle instance, unsigned long port)
{
    return DSSI_NONE;
}

static void
runSynth(LADSPA_Handle instance, unsigned long nframes,
	 snd_seq_event_t *events, unsigned long nevents)
{
    FluidSynthInstance *synth = (FluidSynthInstance *)instance;
    unsigned long i = 0;
    unsigned long ei = 0;
    unsigned long s;

    while (i < nframes) {

	while (ei < nevents && i == events[ei].time.tick) {

	    snd_seq_event_t *ev = &events[ei];

	    switch (ev->type) {

	    case SND_SEQ_EVENT_NOTEOFF:
		fluid_synth_noteoff(synth->synth, ev->data.note.channel,
				    ev->data.note.note);
		break;

	    case SND_SEQ_EVENT_NOTEON:
		fluid_synth_noteon(synth->synth, ev->data.note.channel,
				   ev->data.note.note, ev->data.note.velocity);
		break;

	    case SND_SEQ_EVENT_CONTROLLER:
		fluid_synth_cc(synth->synth, ev->data.control.channel,
			       ev->data.control.param, ev->data.control.value);
		break;

	    case SND_SEQ_EVENT_PITCHBEND:
		fluid_synth_pitch_bend(synth->synth, ev->data.control.channel,
				       ev->data.control.value);
		break;

	    default:
		break;
	    }

	    ++ei;
        }

	s = nframes;
        if (ei < nevents && events[ei].time.tick - i < s) {
            s = events[ei].time.tick - i;
        }
        if (nframes - i < s) {
            s = nframes - i;
        }

        fluid_synth_process(synth->synth, s, 0, 0, 2, synth->audioPorts);
        i += s;
    }
}

const LADSPA_Descriptor *
ladspa_descriptor(unsigned long index)
{
    if (index >= descriptors.descriptors.size()) return 0;
    return descriptors.descriptors[index]->LADSPA_Plugin;
}

const DSSI_Descriptor *
dssi_descriptor(unsigned long index)
{
    if (index >= descriptors.descriptors.size()) return 0;
    return descriptors.descriptors[index];
}

vector<string>
getSF2Path()
{
    vector<string> path;

    string spath;
    char *cpath = getenv("SF2_PATH");
    if (cpath) spath = cpath;

    if (spath == "") {
	char *home = getenv("HOME");
	if (home) {
	    spath = string(home) +
		"/sf2:/usr/local/share/sf2:/usr/share/sf2";
	} else {
	    spath = "/usr/local/share/sf2:/usr/share/sf2";
	}
    }

    string::size_type index = 0, newindex = 0;

    while ((newindex = spath.find(':', index)) >= 0 && newindex < spath.size()) {
	path.push_back(spath.substr(index, newindex - index));
	index = newindex + 1;
    }
    
    path.push_back(spath.substr(index));

    return path;
}

vector<string>
scanSF2()
{
    vector<string> fonts;
    vector<string> path(getSF2Path());
    
    for (vector<string>::iterator i = path.begin(); i != path.end(); ++i) {

	DIR *dir = 0;
	struct dirent *entry = 0;

	if (!(dir = opendir((*i).c_str()))) continue;

	while ((entry = readdir(dir))) {
	    if (entry->d_name[0] == '.') continue;
	    if (strlen(entry->d_name) < 5 ||
		strcasecmp(entry->d_name + strlen(entry->d_name) - 4, ".sf2"))
		continue;
	    string fileName = entry->d_name;
	    fonts.push_back(fileName.substr(0, fileName.length() - 4));
	}

	closedir(dir);
    }

    return fonts;
}

string
locateSF2(string name)
{
    vector<string> path(getSF2Path());

    for (vector<string>::iterator i = path.begin(); i != path.end(); ++i) {

	DIR *dir = 0;
	struct dirent *entry = 0;

	if (!(dir = opendir((*i).c_str()))) continue;

	while ((entry = readdir(dir))) {
	    if (entry->d_name[0] == '.') continue;
	    if (strlen(entry->d_name) < 5 ||
		strcasecmp(entry->d_name + strlen(entry->d_name) - 4, ".sf2"))
		continue;
	    string fileName = entry->d_name;
	    if (fileName.substr(0, fileName.length() - 4) == name) {
		return *i + "/" + fileName;
	    }
	}

	closedir(dir);
    }

    return "";
}

static LADSPA_Handle
instantiate(const LADSPA_Descriptor *descriptor, unsigned long sample_rate)
{
    string label = string(descriptor->Label);
    string sf2Name = "";
    string::size_type i = label.find(':');
    if (i < label.size() - 1) sf2Name = label.substr(i+1);
    
    fluid_settings_t *settings = new_fluid_settings();
    if (!settings) return 0;

    fluid_settings_setnum(settings, "synth.sample-rate", sample_rate);

    fluid_synth_t *synth = new_fluid_synth(settings);
    if (!synth) return 0;

    FluidSynthInstance *instance = 
	(FluidSynthInstance *)malloc(sizeof(FluidSynthInstance));
    if (!instance) return 0;

    instance->audioPorts = 
	(LADSPA_Data **)calloc(AUDIO_PORT_COUNT, sizeof(LADSPA_Data *));
    instance->controlPorts =
	(LADSPA_Data **)calloc(TOTAL_PORT_COUNT - AUDIO_PORT_COUNT, sizeof(LADSPA_Data *));
    instance->controlBackups =
	(LADSPA_Data  *)calloc(TOTAL_PORT_COUNT - AUDIO_PORT_COUNT, sizeof(LADSPA_Data));
    instance->synth = synth;
    instance->programs = 0;
    instance->programCount = 0;

    if (!instance->audioPorts || !instance->controlPorts || !instance->controlBackups) {
	return 0;
    }

    if (sf2Name != "") {
	string fileName = locateSF2(sf2Name);
	if (fileName == "") {
	    printf("error: unable to locate soundfont %s\n", sf2Name.c_str());
	} else {
	    instance->soundFontId = fluid_synth_sfload(synth, fileName.c_str(), 0);
	    if (!instance->soundFontId) {
		printf("error: unable to load soundfont %s\n", fileName.c_str());
	    }
	    fluid_synth_program_reset(synth);
	}
    } else {
	instance->soundFontId = -1;
    }

    return (LADSPA_Handle)instance;
}

FluidDescriptorSet::FluidDescriptorSet()
{
    vector<string> fonts(scanSF2());
    int id = 0;
    
    for (vector<string>::iterator i = fonts.begin(); i != fonts.end(); ++i) {

	char **port_names;
	LADSPA_Descriptor *ladspaDescriptor;
	LADSPA_PortDescriptor *port_descriptors;
	LADSPA_PortRangeHint *port_range_hints;
	DSSI_Descriptor *dssiDescriptor;

	ladspaDescriptor = (LADSPA_Descriptor *)malloc(sizeof(LADSPA_Descriptor));

	if (!ladspaDescriptor) break;

	ladspaDescriptor->UniqueID = id++;
	ladspaDescriptor->Label = strdup((string("fluid:") + *i).c_str());
	ladspaDescriptor->Properties = 0;
	ladspaDescriptor->Name = strdup((string("FluidSynth ") + *i).c_str());
	ladspaDescriptor->Maker = "Chris Cannam <cannam@all-day-breakfast.com>";
	ladspaDescriptor->Copyright = "GPL";
	ladspaDescriptor->PortCount = TOTAL_PORT_COUNT;

	port_descriptors = (LADSPA_PortDescriptor *)
	    calloc(ladspaDescriptor->PortCount, sizeof
		   (LADSPA_PortDescriptor));
	ladspaDescriptor->PortDescriptors =
	    (const LADSPA_PortDescriptor *) port_descriptors;

	port_range_hints = (LADSPA_PortRangeHint *)
	    calloc(ladspaDescriptor->PortCount, sizeof
		   (LADSPA_PortRangeHint));
	ladspaDescriptor->PortRangeHints =
	    (const LADSPA_PortRangeHint *) port_range_hints;

	port_names = (char **) calloc(ladspaDescriptor->PortCount, sizeof(char *));
	ladspaDescriptor->PortNames = (const char **) port_names;

	port_descriptors[PORT_OUTPUT_LEFT] = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
	port_names[PORT_OUTPUT_LEFT] = "Output L";
	port_range_hints[PORT_OUTPUT_LEFT].HintDescriptor = 0;
	
	port_descriptors[PORT_OUTPUT_RIGHT] = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
	port_names[PORT_OUTPUT_RIGHT] = "Output R";
	port_range_hints[PORT_OUTPUT_RIGHT].HintDescriptor = 0;

	for (int j = AUDIO_PORT_COUNT; j < TOTAL_PORT_COUNT; ++j) {
	    port_descriptors[j] = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
	}

	port_names[PORT_REVERB_SWITCH] = "Reverb on/off";
	port_range_hints[PORT_REVERB_SWITCH].HintDescriptor =
	    LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE |
	    LADSPA_HINT_TOGGLED | LADSPA_HINT_INTEGER | LADSPA_HINT_DEFAULT_MAXIMUM;
	port_range_hints[PORT_REVERB_SWITCH].LowerBound = 0.0;
	port_range_hints[PORT_REVERB_SWITCH].UpperBound = 1.0;

	port_names[PORT_REVERB_ROOMSIZE] = "Reverb Room Size";
	port_range_hints[PORT_REVERB_ROOMSIZE].HintDescriptor = 
	    LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE |
	    RANGE_REVERB_ROOMSIZE_DEFAULT;
	port_range_hints[PORT_REVERB_ROOMSIZE].LowerBound = RANGE_REVERB_ROOMSIZE_MIN;
	port_range_hints[PORT_REVERB_ROOMSIZE].UpperBound = RANGE_REVERB_ROOMSIZE_MAX;

	port_names[PORT_REVERB_DAMPING] = "Reverb Damping";
	port_range_hints[PORT_REVERB_DAMPING].HintDescriptor = 
	    LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE |
	    RANGE_REVERB_DAMPING_DEFAULT;
	port_range_hints[PORT_REVERB_DAMPING].LowerBound = RANGE_REVERB_DAMPING_MIN;
	port_range_hints[PORT_REVERB_DAMPING].UpperBound = RANGE_REVERB_DAMPING_MAX;

	port_names[PORT_REVERB_LEVEL] = "Reverb Level";
	port_range_hints[PORT_REVERB_LEVEL].HintDescriptor = 
	    LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE |
	    RANGE_REVERB_LEVEL_DEFAULT;
	port_range_hints[PORT_REVERB_LEVEL].LowerBound = RANGE_REVERB_LEVEL_MIN;
	port_range_hints[PORT_REVERB_LEVEL].UpperBound = RANGE_REVERB_LEVEL_MAX;

	port_names[PORT_REVERB_WIDTH] = "Reverb Width";
	port_range_hints[PORT_REVERB_WIDTH].HintDescriptor = 
	    LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE |
	    RANGE_REVERB_WIDTH_DEFAULT;
	port_range_hints[PORT_REVERB_WIDTH].LowerBound = RANGE_REVERB_WIDTH_MIN;
	port_range_hints[PORT_REVERB_WIDTH].UpperBound = RANGE_REVERB_WIDTH_MAX;
	
	port_names[PORT_CHORUS_SWITCH] = "Chorus on/off";
	port_range_hints[PORT_CHORUS_SWITCH].HintDescriptor =
	    LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE |
	    LADSPA_HINT_TOGGLED | LADSPA_HINT_INTEGER | LADSPA_HINT_DEFAULT_MAXIMUM;
	port_range_hints[PORT_CHORUS_SWITCH].LowerBound = 0.0;
	port_range_hints[PORT_CHORUS_SWITCH].UpperBound = 1.0;

	port_names[PORT_CHORUS_NUMBER] = "Chorus Number";
	port_range_hints[PORT_CHORUS_NUMBER].HintDescriptor = 
	    LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_INTEGER |
	    RANGE_CHORUS_NUMBER_DEFAULT;
	port_range_hints[PORT_CHORUS_NUMBER].LowerBound = RANGE_CHORUS_NUMBER_MIN;
	port_range_hints[PORT_CHORUS_NUMBER].UpperBound = RANGE_CHORUS_NUMBER_MAX;

	port_names[PORT_CHORUS_LEVEL] = "Chorus Level";
	port_range_hints[PORT_CHORUS_LEVEL].HintDescriptor = 
	    LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | 
	    RANGE_CHORUS_LEVEL_DEFAULT;
	port_range_hints[PORT_CHORUS_LEVEL].LowerBound = RANGE_CHORUS_LEVEL_MIN;
	port_range_hints[PORT_CHORUS_LEVEL].UpperBound = RANGE_CHORUS_LEVEL_MAX;

	port_names[PORT_CHORUS_SPEED] = "Chorus Speed (Hz)";
	port_range_hints[PORT_CHORUS_SPEED].HintDescriptor = 
	    LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE |
	    RANGE_CHORUS_SPEED_DEFAULT;
	port_range_hints[PORT_CHORUS_SPEED].LowerBound = RANGE_CHORUS_SPEED_MIN;
	port_range_hints[PORT_CHORUS_SPEED].UpperBound = RANGE_CHORUS_SPEED_MAX;

	port_names[PORT_CHORUS_DEPTH] = "Chorus Depth";
	port_range_hints[PORT_CHORUS_DEPTH].HintDescriptor = 
	    LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE |
	    RANGE_CHORUS_DEPTH_DEFAULT;
	port_range_hints[PORT_CHORUS_DEPTH].LowerBound = RANGE_CHORUS_DEPTH_MIN;
	port_range_hints[PORT_CHORUS_DEPTH].UpperBound = RANGE_CHORUS_DEPTH_MAX;

	port_names[PORT_CHORUS_TYPE] = "Chorus Type (0 = sine, 1 = triangle)";
	port_range_hints[PORT_CHORUS_TYPE].HintDescriptor = 
	    LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_INTEGER | 
	    RANGE_CHORUS_TYPE_DEFAULT;
	port_range_hints[PORT_CHORUS_TYPE].LowerBound = RANGE_CHORUS_TYPE_MIN;
	port_range_hints[PORT_CHORUS_TYPE].UpperBound = RANGE_CHORUS_TYPE_MAX;
	
	ladspaDescriptor->instantiate = instantiate;
	ladspaDescriptor->connect_port = connectPort;
	ladspaDescriptor->activate = activate;
	ladspaDescriptor->run = run;
	ladspaDescriptor->run_adding = NULL;
	ladspaDescriptor->set_run_adding_gain = NULL;
	ladspaDescriptor->deactivate = deactivate;
	ladspaDescriptor->cleanup = cleanup;

	dssiDescriptor = (DSSI_Descriptor *) malloc(sizeof(DSSI_Descriptor));
	if (!dssiDescriptor) break;

        dssiDescriptor->DSSI_API_Version = 1;
        dssiDescriptor->LADSPA_Plugin = ladspaDescriptor;
	dssiDescriptor->ChannelCount = 16;
        dssiDescriptor->configure = configure;
        dssiDescriptor->get_program = getProgram;
        dssiDescriptor->select_program = selectProgram;
        dssiDescriptor->get_midi_controller_for_port = getMidiController;
        dssiDescriptor->run_synth = runSynth;
        dssiDescriptor->run_synth_adding = NULL;

	descriptors.push_back(dssiDescriptor);
    }
}
    
FluidDescriptorSet::~FluidDescriptorSet()
{
    for (vector<DSSI_Descriptor *>::iterator i = descriptors.begin();
	 i != descriptors.end(); ++i) {

	const LADSPA_Descriptor *ld = (*i)->LADSPA_Plugin;

	free((void *)ld->Label);
	free((void *)ld->Name);
	free((LADSPA_PortDescriptor *) ld->PortDescriptors);
	free((char **) ld->PortNames);
	free((LADSPA_PortRangeHint *) ld->PortRangeHints);
	free((void *)ld);

	free(*i);
    }

    descriptors.clear();
}   


void _init()
{
}

void _fini()
{
}



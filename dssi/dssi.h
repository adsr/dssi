/* -*- c-basic-offset: 4 -*- */

/* dssi.h

   Disposable Soft Synth Interface version 0.1
   Copyright (c) 2004 Chris Cannam and Steve Harris
   
   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public License
   as published by the Free Software Foundation; either version 2.1 of
   the License, or (at your option) any later version.
   
   This library is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
   Lesser General Public License for more details.
   
   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
   USA.
*/

#ifndef DSSI_INCLUDED
#define DSSI_INCLUDED

#include <ladspa.h>
#include <alsa/seq_event.h>

#define DSSI_VERSION "0.1"
#define DSSI_VERSION_MAJOR 0
#define DSSI_VERSION_MINOR 1

#ifdef __cplusplus
extern "C" {
#endif

/* Rationale:

   There is a need for an API that supports hosted MIDI soft synths
   with GUIs in Linux audio applications.  We hope that in time the
   GMPI initiative will comprehensively address this need, but the
   requirement for Linux applications to be able to support simple
   hosted synths is here now, and GMPI is not.  This proposal (the
   "Disposable Soft Synth Interface" or DSSI, pronounced "dizzy") aims
   to provide the simplest possible interim solution in a way that we
   hope will prove compelling enough to support now, yet not so
   compelling as to supplant GMPI or any other comprehensive future
   proposal.

   For simplicity and familiarity, this API is based as far as
   possible on existing work -- the LADSPA plugin API for control
   values and audio processing, and the ALSA sequencer event types for
   MIDI event communication.  The GUI part of the proposal is quite
   new, but may also be applicable retroactively to LADSPA plugins
   that do not otherwise support this synth interface.
*/

typedef struct _DSSI_Program_Descriptor {

    /** Bank number for this program.  Note that DSSI does not support
        MIDI-style separation of bank LSB and MSB values.  There is no
        restriction on the set of available banks: the numbers do not
        need to be contiguous, there does not need to be a bank 0, etc */
    unsigned long Bank;

    /** Program number (unique within its bank) for this program.
	There is no restriction on the set of available programs: the
	numbers do not need to be contiguous, there does not need to
	be a program 0, etc. */
    unsigned long Program;

    /** Name of the program.  The host should be aware that a call to
	configure() on a synth may invalidate this pointer entirely. */
    const char * Name;

} DSSI_Program_Descriptor;


typedef struct _DSSI_Descriptor {

    /**
     * This member indicates the DSSI API level used by this plugin.
     * If we're lucky, this will never be needed.  For now all plugins
     * must set it to 1.
     */
    int DSSI_API_Version;

    /**
     * A DSSI synth plugin consists of a LADSPA plugin plus an
     * additional framework for controlling program settings and
     * transmitting MIDI events.  A plugin must fully implement the
     * LADSPA descriptor fields as well as the required LADSPA
     * functions including instantiate() and (de)activate().  It
     * should also implement run(), with the same behaviour as if
     * run_synth() (below) were called with no synth events.
     *
     * In order to instantiate a synth the host calls the LADSPA
     * instantiate function, passing in this LADSPA_Descriptor
     * pointer.  The returned LADSPA_Handle is used as the argument
     * for the DSSI functions below as well as for the LADSPA ones.
     */
    const LADSPA_Descriptor *LADSPA_Plugin;

    /**
     * This member is a function pointer that sends a piece of
     * configuration data to the plugin.  The key argument specifies
     * some aspect of the synth's configuration that is to be changed,
     * and the value argument specifies a new value for it.
     *
     * This call is intended to set some session-scoped aspect of a
     * plugin's behaviour, for example to tell the plugin to load
     * sample data from a particular file.  The plugin should act
     * immediately on the request.  The return value may be delivered
     * to the GUI, shown to the user, or delivered elsewhere,
     * depending on which agent requested this configuration change:
     * the host does not interpret it, but will free it after use if
     * it is non-NULL.
     *
     * Calls to configure() are not automated as timed events.
     * Instead, a host should remember the last value associated with
     * each key passed to configure() during a given session for a
     * given plugin instance, and should call configure() with the
     * correct value for each key the next time it instantiates the
     * "same" plugin instance, for example on reloading a project in
     * which the plugin was used before.  Plugins should note that a
     * host may typically instantiate a plugin multiple times with the
     * same configuration values, and should share data between
     * instances where practical.
     *
     * Calling configure() completely invalidates the program and bank
     * information last obtained from the plugin.
     */
     char *(*configure)(LADSPA_Handle Instance,
			const char *key,
			const char *value);

    /**
     * This member is a function pointer that returns a description of
     * a program (named preset sound) available on this synth.  A
     * plugin that does not support programs at all should set this
     * member to NULL.
     *
     * The Index argument is an index into the plugin's list of
     * programs, not a program number as represented by the Program
     * field of the DSSI_Program_Descriptor.  This function must
     * return NULL if given an argument out of range, so that the host
     * can use it to query the number of programs as well as their
     * properties.
     *
     * (The distinction between the program number and the index
     * argument to this function is needed to support synths that use
     * non-contiguous program or bank numbers.)
     */
    const DSSI_Program_Descriptor *(*get_program)(LADSPA_Handle Instance,
						  unsigned long Index);

    /**
     * This member is a function pointer that selects a new program
     * for this synth.  The program change should take effect
     * immediately at the start of the next run_synth() call.  (This
     * means that a host providing the capability of changing programs
     * between any two notes on a track must vary the block size so as
     * to place the program change at the right place.  A host that
     * wanted to avoid this would probably just instantiate a plugin
     * for each program.)
     * 
     * A plugin that does not support programs at all should set this
     * member NULL.  Plugins should ignore a select_program() call
     * with an invalid bank or program.
     *
     * A plugin is not required to select any particular default
     * program on activate(): it's the host's duty to set a program
     * explicitly.  The current program is invalidated by any call to
     * configure().
     */
    void (*select_program)(LADSPA_Handle Instance,
			   unsigned long Bank,
			   unsigned long Program);

    /**
     * This member is a function pointer that returns the MIDI
     * controller number or NRPN that should be mapped to the given
     * input control port.  If the given port should not have any MIDI
     * controller mapped to it, the function should return DSSI_NONE.
     * The behaviour of this function is undefined if the given port
     * number does not correspond to an input control port.  A plugin
     * that does not want MIDI controllers mapped to ports at all may
     * set this member NULL.
     *
     * Correct values can be got using the macros DSSI_CC(num) and
     * DSSI_NRPN(num) as appropriate, and values can be combined using
     * bitwise OR: e.g. DSSI_CC(23) | DSSI_NRPN(1069) means the port
     * should respond to CC #23 and NRPN #1069.
     *
     * The host is responsible for doing proper scaling from MIDI
     * controller and NRPN value ranges to port ranges according to
     * the plugin's LADSPA port hints.  Hosts should not deliver
     * through run_synth any MIDI controller events that have already
     * been mapped to control port values.
     *
     * A plugin should not attempt to request mappings from
     * controllers 0 or 32 (MIDI Bank Select MSB and LSB).
     */
    int (*get_midi_controller_for_port)(LADSPA_Handle Instance,
					unsigned long Port);

    /**
     * This member is a function pointer that runs a synth for a
     * block.  This is identical in function to the LADSPA run()
     * function, except that it also supplies events to the synth.
     *
     * The Events pointer points to a block of EventCount ALSA
     * sequencer events, which is used to communicate MIDI and related
     * events to the synth.  Each event is timestamped relative to the
     * start of the block, (mis)using the ALSA "tick time" field as a
     * frame count. The host is responsible for ensuring that events
     * with differing timestamps are already ordered by time.
     *
     * See also the notes on activation, port connection etc in
     * ladpsa.h, in the context of the LADSPA run() function.
     *
     * Note Events
     * ~~~~~~~~~~~
     * There are two minor requirements aimed at making the plugin
     * writer's life as simple as possible:
     * 
     * 1. A host must never send events of type SND_SEQ_EVENT_NOTE.
     * Notes should always be sent as separate SND_SEQ_EVENT_NOTE_ON
     * and NOTE_OFF events.  A plugin should discard any one-point
     * NOTE events it sees.
     * 
     * 2. A host must not attempt to switch notes off by sending
     * zero-velocity NOTE_ON events.  It should always send true
     * NOTE_OFFs.  It is the host's responsibility to remap events in
     * cases where an external MIDI source has sent it zero-velocity
     * NOTE_ONs.
     *
     * Bank and Program Events
     * ~~~~~~~~~~~~~~~~~~~~~~~
     * Hosts must map MIDI Bank Select MSB and LSB (0 and 32)
     * controllers and MIDI Program Change events onto the banks and
     * programs specified by the plugin, using the DSSI select_program
     * call.  No host should ever deliver a program change or bank
     * select controller to a plugin via run_synth.
     */
    void (*run_synth)(LADSPA_Handle    Instance,
		      unsigned long    SampleCount,
		      snd_seq_event_t *Events,
		      unsigned long    EventCount);

    /**
     * This member is a function pointer that runs an instance of a
     * synth for a block, adding its outputs to the values already
     * present at the output ports.  This is provided for symmetry
     * with LADSPA run_adding(), and is equally optional.  A plugin
     * that does not provide it must set this member to NULL.
     */
    void (*run_synth_adding)(LADSPA_Handle    Instance,
			     unsigned long    SampleCount,
			     snd_seq_event_t *Events,
			     unsigned long    EventCount);

} DSSI_Descriptor;

const DSSI_Descriptor *dssi_descriptor(unsigned long Index);
  
typedef const DSSI_Descriptor *(*DSSI_Descriptor_Function)(unsigned long Index);

/*
 * Macros to specify particular MIDI controllers in return values from
 * get_midi_controller_for_port()
 */

#define DSSI_CC_BITS			0x20000000
#define DSSI_NRPN_BITS			0x40000000

#define DSSI_NONE			-1
#define DSSI_CONTROLLER_IS_SET(n)	(DSSI_NONE != (n))

#define DSSI_CC(n)			(DSSI_CC_BITS | (n))
#define DSSI_IS_CC(n)			(DSSI_CC_BITS & (n))
#define DSSI_CC_NUMBER(n)		((n) & 0x7f)

#define DSSI_NRPN(n)			(DSSI_NRPN_BITS | ((n) << 7))
#define DSSI_IS_NRPN(n)			(DSSI_NRPN_BITS & (n))
#define DSSI_NRPN_NUMBER(n)		(((n) >> 7) & 0x3fff)

#ifdef __cplusplus
}
#endif

#endif /* DSSI_INCLUDED */


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

#ifndef _FLUID_DSSI_H_INCLUDED_
#define _FLUID_DSSI_H_INCLUDED_

typedef enum {

    PORT_OUTPUT_LEFT     = 0,
    PORT_OUTPUT_RIGHT    = 1,

    AUDIO_PORT_COUNT     = 2

} AudioPortNumber;

typedef enum {

    PORT_REVERB_SWITCH   = 2, /* = AUDIO_PORT_COUNT */
    PORT_REVERB_ROOMSIZE = 3,
    PORT_REVERB_DAMPING  = 4,
    PORT_REVERB_LEVEL    = 5,
    PORT_REVERB_WIDTH    = 6,

    PORT_CHORUS_SWITCH   = 7,
    PORT_CHORUS_NUMBER   = 8,
    PORT_CHORUS_LEVEL    = 9,
    PORT_CHORUS_SPEED    = 10,
    PORT_CHORUS_DEPTH    = 11,
    PORT_CHORUS_TYPE     = 12,

    TOTAL_PORT_COUNT     = 13

} ControlPortNumber;

#define RANGE_REVERB_ROOMSIZE_MIN     0.0
#define RANGE_REVERB_ROOMSIZE_MAX     1.0
#define RANGE_REVERB_ROOMSIZE_DEFAULT LADSPA_HINT_DEFAULT_LOW

#define RANGE_REVERB_DAMPING_MIN      0.0
#define RANGE_REVERB_DAMPING_MAX      1.0
#define RANGE_REVERB_DAMPING_DEFAULT  LADSPA_HINT_DEFAULT_MINIMUM

#define RANGE_REVERB_LEVEL_MIN        0.0
#define RANGE_REVERB_LEVEL_MAX        1.0
#define RANGE_REVERB_LEVEL_DEFAULT    LADSPA_HINT_DEFAULT_HIGH

#define RANGE_REVERB_WIDTH_MIN        0.0
#define RANGE_REVERB_WIDTH_MAX        1.0
#define RANGE_REVERB_WIDTH_DEFAULT    LADSPA_HINT_DEFAULT_MIDDLE

#define RANGE_CHORUS_NUMBER_MIN       0.0
#define RANGE_CHORUS_NUMBER_MAX       12.0
#define RANGE_CHORUS_NUMBER_DEFAULT   LADSPA_HINT_DEFAULT_LOW

#define RANGE_CHORUS_LEVEL_MIN        0.0
#define RANGE_CHORUS_LEVEL_MAX        4.0
#define RANGE_CHORUS_LEVEL_DEFAULT    LADSPA_HINT_DEFAULT_MIDDLE

#define RANGE_CHORUS_SPEED_MIN        0.29
#define RANGE_CHORUS_SPEED_MAX        5
#define RANGE_CHORUS_SPEED_DEFAULT    LADSPA_HINT_DEFAULT_MINIMUM

#define RANGE_CHORUS_DEPTH_MIN        0.0
#define RANGE_CHORUS_DEPTH_MAX        32.0
#define RANGE_CHORUS_DEPTH_DEFAULT    LADSPA_HINT_DEFAULT_LOW

#define RANGE_CHORUS_TYPE_MIN         0.0
#define RANGE_CHORUS_TYPE_MAX         1.0
#define RANGE_CHORUS_TYPE_DEFAULT     LADSPA_HINT_DEFAULT_MINIMUM

#endif

/* audio.h
 * - stereo->mono downmixing
 * - resampling
 *
 * $Id: audio.h,v 1.3 2003/03/15 02:24:18 karl Exp $
 *
 * Copyright (c) 2001 Michael Smith <msmith@xiph.org>
 *
 * This program is distributed under the terms of the GNU General
 * Public License, version 2. You may use, modify, and redistribute
 * it under the terms of this license. A copy should be included
 * with this source.
 */

#ifndef __AUDIO_H
#define __AUDIO_H

#include "resample.h"

typedef struct {
    float *buffer;
    int buflen;
} downmix_state;

typedef struct {
    resampler_state resampler;
    int channels;

    float **buffers;
    int buffill;
    int bufsize;

    float **convbuf;
    int convbuflen;
} resample_state;

downmix_state *downmix_initialise(void);
void downmix_clear(downmix_state *s);
void downmix_buffer(downmix_state *s, signed char *buf, int len, int be);
void downmix_buffer_float(downmix_state *s, float **buf, int samples);

resample_state *resample_initialise(int channels, int infreq, int outfreq);
void resample_clear(resample_state *s);
void resample_buffer(resample_state *s, signed char *buf, int buflen, int be);
void resample_buffer_float(resample_state *s, float **buf, int buflen);
void resample_finish(resample_state *s);

#endif


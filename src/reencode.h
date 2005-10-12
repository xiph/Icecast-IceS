/* reencode.h
 * - reencoding functions
 *
 * $Id: reencode.h,v 1.7 2004/01/13 16:35:27 karl Exp $
 *
 * Copyright (c) 2001 Michael Smith <msmith@xiph.org>
 *
 * This program is distributed under the terms of the GNU General
 * Public License, version 2. You may use, modify, and redistribute
 * it under the terms of this license. A copy should be included
 * with this source.
 */

#ifndef __REENCODE_H
#define __REENCODE_H

#include <ogg/ogg.h>
#include <vorbis/codec.h>

#include "cfgparse.h"
#include "stream.h"
#include "encode.h"
#include "audio.h"

typedef struct {
    int out_min_br;
    int out_nom_br;
    int out_max_br;
    float quality;
    int managed;

    int out_samplerate;
    int out_channels;

    int in_samplerate;
    int in_channels;

    int current_serial;
    int need_headers;

    ogg_stream_state os;
    vorbis_info vi;
    vorbis_comment vc;
    vorbis_dsp_state vd;
    vorbis_block vb;
    int max_samples_ppage;

    encoder_state *encoder;
    downmix_state *downmix;
    resample_state *resamp;

} reencode_state;

reencode_state *reencode_init(instance_t *stream);
int reencode_page(reencode_state *s, ref_buffer *buf,
        unsigned char **outbuf, int *outlen);
void reencode_clear(reencode_state *s);



#endif /* __REENCODE_H */


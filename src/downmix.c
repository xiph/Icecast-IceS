/* downmix.c
 * stereo->mono downmixing
 *
 * $Id: downmix.c,v 1.1 2002/07/20 12:52:06 msmith Exp $
 *
 * Copyright (c) 2001 Michael Smith <msmith@labyrinth.net.au>
 *
 * This program is distributed under the terms of the GNU General
 * Public License, version 2. You may use, modify, and redistribute
 * it under the terms of this license. A copy should be included
 * with this source.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "downmix.h"

#define MODULE "downmix/"
#include "logging.h"

downmix_state *downmix_initialise(void) {
    downmix_state *state = calloc(1, sizeof(downmix_state));

    LOG_INFO0("Enabling mono->stereo downmixing");

    return state;
}

void downmix_clear(downmix_state *s) {
    if(s) {
        free(s->buffer);
        free(s);
    }
}

void downmix_buffer(downmix_state *s, signed char *buf, int len, int be)
{
    int samples = len/4;
    int i;

    if(samples > s->buflen) {
        s->buffer = realloc(s->buffer, samples * sizeof(float));
        s->buflen = samples;
    }

    if(be) {
        for(i=0; i < samples; i++) {
            s->buffer[i] = (((buf[4*i]<<8) | (buf[4*i + 1]&0xff)) +
                           ((buf[4*i + 2]<<8) | (buf[4*i + 3]&0xff)))/65536.f;
        }
    }
    else {
        for(i=0; i < samples; i++) {
            s->buffer[i] = (((buf[4*i + 1]<<8) | (buf[4*i]&0xff)) +
                           ((buf[4*i + 3]<<8) | (buf[4*i + 2]&0xff)))/65536.f;
        }
    }
}




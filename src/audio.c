/* audio.c
 * stereo->mono downmixing
 * resampling
 *
 * $Id: audio.c,v 1.5 2002/08/17 05:17:57 msmith Exp $
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
#include "audio.h"

#include "resample.h"

#define MODULE "audio/"
#include "logging.h"

downmix_state *downmix_initialise(void) {
    downmix_state *state = calloc(1, sizeof(downmix_state));

    LOG_INFO0("Enabling stereo->mono downmixing");

    return state;
}

void downmix_clear(downmix_state *s) {
    if(s) {
        free(s->buffer);
        free(s);
    }
}

void downmix_buffer_float(downmix_state *s, float **buf, int samples)
{
    int i;

    if(samples > s->buflen) {
        s->buffer = realloc(s->buffer, samples * sizeof(float));
        s->buflen = samples;
    }

    for(i=0; i < samples; i++) {
        s->buffer[i] = (buf[0][i] + buf[1][i])*0.5;
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

resample_state *resample_initialise(int channels, int infreq, int outfreq)
{
    resample_state *state = calloc(1, sizeof(resample_state));

    if(res_init(&state->resampler, channels, outfreq, infreq, RES_END)) {
        LOG_ERROR0("Couldn't initialise resampler to specified frequency\n");
        return NULL;
    }

    state->buffers = calloc(channels, sizeof(float *));
    state->convbuf = calloc(channels, sizeof(float *));
    state->channels = channels;

    LOG_INFO3("Initialised resampler for %d channels, from %d Hz to %d Hz",
            channels, infreq, outfreq);

    return state;
}

void resample_clear(resample_state *s)
{
    int c;

    if(s) {
        if(s->buffers) {
            for(c=0; c<s->channels; c++)
                free(s->buffers[c]);
            free(s->buffers);
        }
        if(s->convbuf) {
            for(c=0; c<s->channels; c++)
                free(s->convbuf[c]);
            free(s->convbuf);
        }
        res_clear(&s->resampler);
        free(s);
    }
}

void resample_buffer(resample_state *s, signed char *buf, int buflen, int be)
{
    int c,i;
    buflen /= 2*s->channels; /* bytes -> samples conversion */

    if(s->convbuflen < buflen) {
        s->convbuflen = buflen;
        for(c=0; c < s->channels; c++)
            s->convbuf[c] = realloc(s->convbuf[c], buflen * sizeof(float));
    }

    if(be) {
        for(i=0; i < buflen; i++) {
            for(c=0; c < s->channels; c++) {
                s->convbuf[c][i] = ((buf[2*(i*s->channels + c)]<<8) |
                                    (0x00ff&(int)buf[2*(i*s->channels + c)+1]))/
                    32768.f;
            }
        }
    }
    else {
        for(i=0; i < buflen; i++) {
            for(c=0; c < s->channels; c++) {
                s->convbuf[c][i] = ((buf[2*(i*s->channels + c) + 1]<<8) |
                                    (0x00ff&(int)buf[2*(i*s->channels + c)]))/
                    32768.f;
            }
        }
    }

    resample_buffer_float(s, s->convbuf, buflen);
}

void resample_buffer_float(resample_state *s, float **buf, int buflen)
{
    int c;
    int res;

    s->buffill = res_push_check(&s->resampler, buflen);
    if(s->buffill <= 0) {
        LOG_ERROR1("Fatal reencoding error: res_push_check returned %d", 
                s->buffill);
    }

    if(s->bufsize < s->buffill) {
        s->bufsize = s->buffill;
        for(c=0; c<s->channels; c++)
            s->buffers[c] = realloc(s->buffers[c], s->bufsize * sizeof(float));
    }

    if((res = res_push(&s->resampler, s->buffers, (float const **)buf, buflen))
            != s->buffill) {
        LOG_ERROR2("Internal error in resampling: returned number of samples %d"
                   ", expected %d", res, s->buffill);
        s->buffill = res;
        return;
    }

}

void resample_finish(resample_state *s)
{
    int ret;

    if(!s->buffers[0])
        return;

    ret = res_drain(&s->resampler, s->buffers);

    if(ret > s->bufsize) {
        LOG_ERROR0("Fatal error in resampler: buffers too small");
        return;
    }

    s->buffill = ret;
}







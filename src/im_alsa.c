/* im_alsa.c
 * - Raw PCM input from ALSA devices
 *
 * $Id: im_alsa.c,v 1.8 2004/03/01 20:58:02 karl Exp $
 *
 * by Jason Chu <jchu@uvic.ca>, based
 * on im_oss.c which is...
 * Copyright (c) 2001 Michael Smith <msmith@xiph.org>
 *
 * This program is distributed under the terms of the GNU General
 * Public License, version 2. You may use, modify, and redistribute
 * it under the terms of this license. A copy should be included
 * with this source.
 */

#ifdef HAVE_CONFIG_H
 #include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <ogg/ogg.h>
#include <sys/soundcard.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <fcntl.h>


#include <thread/thread.h>
#include "cfgparse.h"
#include "stream.h"
#include "metadata.h"
#include "inputmodule.h"

#define ALSA_PCM_NEW_HW_PARAMS_API
#include "im_alsa.h"

#define MODULE "input-alsa/"
#include "logging.h"

#define SAMPLES 8192

static void close_module(input_module_t *mod)
{
    if(mod)
    {
        if(mod->internal)
        {
            im_alsa_state *s = mod->internal;
            if(s->fd != NULL)
                snd_pcm_close(s->fd);
            thread_mutex_destroy(&s->metadatalock);
            free(s);
        }
        free(mod);
    }
}
static int event_handler(input_module_t *mod, enum event_type ev, void *param)
{
    im_alsa_state *s = mod->internal;

    switch(ev)
    {
        case EVENT_SHUTDOWN:
            close_module(mod);
            break;
        case EVENT_NEXTTRACK:
            s->newtrack = 1;
            break;
        case EVENT_METADATAUPDATE:
            thread_mutex_lock(&s->metadatalock);
            if(s->metadata)
            {
                char **md = s->metadata;
                while(*md)
                    free(*md++);
                free(s->metadata);
            }
            s->metadata = (char **)param;
            s->newtrack = 1;
            thread_mutex_unlock(&s->metadatalock);
            break;
        default:
            LOG_WARN1("Unhandled event %d", ev);
            return -1;
    }

    return 0;
}

static void metadata_update(void *self, vorbis_comment *vc)
{
    im_alsa_state *s = self;
    char **md;

    thread_mutex_lock(&s->metadatalock);

    md = s->metadata;

    if(md)
    {
        while(*md)
            vorbis_comment_add(vc, *md++);
    }

    thread_mutex_unlock(&s->metadatalock);
}

/* Core streaming function for this module
 * This is what actually produces the data which gets streamed.
 *
 * returns:  >0  Number of bytes read
 *            0  Non-fatal error.
 *           <0  Fatal error.
 */
static int alsa_read(void *self, ref_buffer *rb)
{
    int result;
    im_alsa_state *s = self;

    rb->buf = malloc(SAMPLES*2*s->channels);
    if(!rb->buf)
        return -1;
    result = snd_pcm_readi(s->fd, rb->buf, SAMPLES);

    if (result >= 0)
    {
        rb->len = result*s->frame_bytes;
        rb->aux_data = s->rate*s->channels*2;
        if (s->newtrack)
        {
            rb->critical = 1;
            s->newtrack = 0;
        }
        return rb->len;
    }

    free (rb->buf);
    if (result == -EINTR)
        return 0;
    if (result == -EPIPE)
    {
        snd_pcm_prepare(s->fd);
        return 0;
    }
    LOG_ERROR1("snd_pcm_readi failed: %s", snd_strerror (result));
    return -1;
}

input_module_t *alsa_open_module(module_param_t *params)
{
    input_module_t *mod = calloc(1, sizeof(input_module_t));
    im_alsa_state *s;
    module_param_t *current;
    char *device = "plughw:0,0"; /* default device */
    snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;
    int use_metadata = 1; /* Default to on */
    unsigned int exact_rate;
    int dir;

    snd_pcm_stream_t stream = SND_PCM_STREAM_CAPTURE;
    snd_pcm_hw_params_t *hwparams;

    int err;

    mod->type = ICES_INPUT_PCM;
    mod->subtype = INPUT_PCM_LE_16;
    mod->getdata = alsa_read;
    mod->handle_event = event_handler;
    mod->metadata_update = metadata_update;

    mod->internal = calloc(1, sizeof(im_alsa_state));
    s = mod->internal;

    s->fd = NULL; /* Set it to something invalid, for now */
    s->rate = 44100; /* Defaults */
    s->channels = 2; 
    s->buffer_time = 500000;
    s->periods = -1;

    thread_mutex_create(&s->metadatalock);

    current = params;

    while(current)
    {
        if(!strcmp(current->name, "rate"))
            s->rate = atoi(current->value);
        else if(!strcmp(current->name, "channels"))
            s->channels = atoi(current->value);
        else if(!strcmp(current->name, "device"))
            device = current->value;
        else if(!strcmp(current->name, "metadata"))
            use_metadata = atoi(current->value);
        else if(!strcmp(current->name, "metadatafilename"))
            ices_config->metadata_filename = current->value;
        else if(!strcmp(current->name, "buffer-time"))
            s->buffer_time = atoi (current->value) * 1000;
        else if(!strcmp(current->name, "periods"))
            s->periods = atoi (current->value);
        else
            LOG_WARN1("Unknown parameter %s for alsa module", current->name);

        current = current->next;
    }

    snd_pcm_hw_params_alloca(&hwparams);

    if ((err = snd_pcm_open(&s->fd, device, stream, 0)) < 0)
    {
        LOG_ERROR2("Failed to open audio device %s: %s", device, snd_strerror(err));
        goto fail;
    }

    if ((err = snd_pcm_hw_params_any(s->fd, hwparams)) < 0)
    {
        LOG_ERROR1("Failed to initialize hwparams: %s", snd_strerror(err));
        goto fail;
    }
    if ((err = snd_pcm_hw_params_set_access(s->fd, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
    {
        LOG_ERROR1("Error setting access: %s", snd_strerror(err));
        goto fail;
    }
    if ((err = snd_pcm_hw_params_set_format(s->fd, hwparams, format)) < 0)
    {
        LOG_ERROR1("Sample format not available: %s", snd_strerror(err));
        goto fail;
    }
    exact_rate = s->rate;
    err = snd_pcm_hw_params_set_rate_near(s->fd, hwparams, &exact_rate, 0);
    if (err < 0)
    {
        LOG_ERROR2("Could not set sample rate to %d: %s", exact_rate, snd_strerror(err));
        goto fail;
    }
    if (exact_rate != s->rate)
    {
        LOG_WARN2("samplerate %d Hz not supported by your hardware try using "
                "%d instead", s->rate, exact_rate);
        goto fail;
    }
    if ((err = snd_pcm_hw_params_set_channels(s->fd, hwparams, s->channels)) < 0)
    {
        LOG_ERROR1("Error setting channels: %s", snd_strerror(err));
        goto fail;
    }
    if ((err = snd_pcm_hw_params_set_buffer_time_near(s->fd, hwparams, &s->buffer_time, &dir)) < 0)
    {
        LOG_ERROR2("Error setting buffer time %u: %s", s->buffer_time, snd_strerror(err));
        goto fail;
    }
    if (s->periods > 0)
    {
        err = snd_pcm_hw_params_set_periods(s->fd, hwparams, s->periods, 0);
        if (err < 0)
        {
            LOG_ERROR2("Error setting %u periods: %s", s->periods, snd_strerror(err));
            goto fail;
        }
    }
    if ((err = snd_pcm_hw_params(s->fd, hwparams)) < 0)
    {
        LOG_ERROR1("Error setting HW params: %s", snd_strerror(err));
        goto fail;
    }

    /* We're done, and we didn't fail! */
    LOG_INFO1 ("Opened audio device %s", device);
    LOG_INFO3 ("using %d channel(s), %d Hz, buffer %u ms ",
            s->channels, s->rate, s->buffer_time/1000);

    s->frame_bytes = s->channels * (snd_pcm_format_width(format) / 8);
    if(use_metadata)
    {
        LOG_INFO0("Starting metadata update thread");
        if(ices_config->metadata_filename)
            thread_create("im_alsa-metadata", metadata_thread_signal, mod, 1);
        else
            thread_create("im_alsa-metadata", metadata_thread_stdin, mod, 1);
    }

    return mod;

fail:
    close_module(mod); /* safe, this checks for valid contents */
    return NULL;
}



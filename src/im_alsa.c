/* im_alsa.c
 * - Raw PCM input from ALSA devices
 *
 * $Id: im_alsa.c,v 1.6 2004/01/11 03:11:05 karl Exp $
 *
 * by Jason Chu <jchu@uvic.ca>, based
 * on im_oss.c which is...
 * Copyright (c) 2001 Michael Smith <msmith@labyrinth.net.au>
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

    rb->len = result*4;
    rb->aux_data = s->rate*s->channels*2;

    if(s->newtrack)
    {
        rb->critical = 1;
        s->newtrack = 0;
    }

    if (result == -EPIPE)
    {
        snd_pcm_prepare(s->fd);
        return 0;
    }
    else if (result == -EBADFD)
    {
        LOG_ERROR0("Bad descriptor passed to snd_pcm_readi");
        free(rb->buf);
        return -1;
    }

    return rb->len;
}

input_module_t *alsa_open_module(module_param_t *params)
{
    input_module_t *mod = calloc(1, sizeof(input_module_t));
    im_alsa_state *s;
    module_param_t *current;
    char *device = "plughw:0,0"; /* default device */
    int format = AFMT_S16_LE;
    int channels, rate;
    int use_metadata = 1; /* Default to on */
    unsigned int buffered_time;

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
    if ((err = snd_pcm_hw_params_set_format(s->fd, hwparams, SND_PCM_FORMAT_S16_LE)) < 0)
    {
        LOG_ERROR1("Couldn't set sample format to SND_PCM_FORMAT_S16_LE: %s", snd_strerror(err));
        goto fail;
    }
    if ((err = snd_pcm_hw_params_set_rate_near(s->fd, hwparams, &s->rate, 0)) < 0)
    {
        LOG_ERROR1("Error setting rate: %s", snd_strerror(err));
        goto fail;
    }
    if ((err = snd_pcm_hw_params_set_channels(s->fd, hwparams, s->channels)) < 0)
    {
        LOG_ERROR1("Error setting channels: %s", snd_strerror(err));
        goto fail;
    }
    if ((err = snd_pcm_hw_params_set_periods(s->fd, hwparams, 2, 0)) < 0)
    {
        LOG_ERROR1("Error setting periods: %s", snd_strerror(err));
        goto fail;
    }
    buffered_time = 500000;
    if ((err = snd_pcm_hw_params_set_buffer_time_near(s->fd, hwparams, &buffered_time, 0)) < 0)
    {
        LOG_ERROR1("Error setting buffersize: %s", snd_strerror(err));
        goto fail;
    }
    if ((err = snd_pcm_hw_params(s->fd, hwparams)) < 0)
    {
        LOG_ERROR1("Error setting HW params: %s", snd_strerror(err));
        goto fail;
    }

    /* We're done, and we didn't fail! */
    LOG_INFO3("Opened audio device %s at %d channel(s), %d Hz", 
            device, s->channels, s->rate);

    if(use_metadata)
    {
        if(ices_config->metadata_filename)
            thread_create("im_alsa-metadata", metadata_thread_signal, mod, 1);
        else
            thread_create("im_alsa-metadata", metadata_thread_stdin, mod, 1);
        LOG_INFO0("Started metadata update thread");
    }

    return mod;

fail:
    close_module(mod); /* safe, this checks for valid contents */
    return NULL;
}



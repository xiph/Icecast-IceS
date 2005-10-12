/* im_sun.c
 * - Raw PCM input from Solaris audio devices
 *
 * $Id: im_sun.c,v 1.13 2004/01/17 04:24:10 karl Exp $
 *
 * by Ciaran Anscomb <ciarana@rd.bbc.co.uk>, based
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
#include <sys/audioio.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#ifdef HAVE_STROPTS_H
#include <stropts.h>
#endif
#include <fcntl.h>

#include "cfgparse.h"
#include "stream.h"
#include "inputmodule.h"
#include "metadata.h"

#include "im_sun.h"

#define MODULE "input-sun/"
#include "logging.h"

#define BUFSIZE 8192

static void close_module(input_module_t *mod)
{
    if(mod)
    {
        if(mod->internal)
        {
            im_sun_state *s = mod->internal;
            if(s->fd >= 0)
                close(s->fd);
            thread_mutex_destroy(&s->metadatalock);
            free(s);
        }
        free(mod);
    }
}
static int event_handler(input_module_t *mod, enum event_type ev, void *param)
{
    im_sun_state *s = mod->internal;

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
    im_sun_state *s = self;
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
static int sun_read(void *self, ref_buffer *rb)
{
    int result;
    im_sun_state *s = self;
    unsigned char *i, j;

    rb->buf = malloc(BUFSIZE*2*s->device_info.record.channels);
    if(!rb->buf)
        return -1;
    result = read(s->fd, rb->buf, BUFSIZE*2*s->device_info.record.channels);

    rb->len = result;
    rb->aux_data = s->device_info.record.sample_rate*s->device_info.record.channels*2;

    if(s->newtrack)
    {
        rb->critical = 1;
        s->newtrack = 0;
    }

    if(result == -1 && errno == EINTR)
    {
        return 0; /* Non-fatal error */
    }
    else if(result <= 0)
    {
        if(result == 0)
            LOG_INFO0("Reached EOF, no more data available");
        else
            LOG_ERROR1("Error reading from audio device: %s", strerror(errno));
        free(rb->buf);
        return -1;
    }

    return rb->len;
}

input_module_t *sun_open_module(module_param_t *params)
{
    input_module_t *mod = calloc(1, sizeof(input_module_t));
    im_sun_state *s;
    module_param_t *current;
    char *device = "/dev/audio"; /* default device */
    int sample_rate = 44100;
    int channels = 2;
    int use_metadata = 1; /* Default to on */

    mod->type = ICES_INPUT_PCM;
#ifdef WORDS_BIGENDIAN
    mod->subtype = INPUT_PCM_BE_16;
#else
    mod->subtype = INPUT_PCM_LE_16;
#endif
    mod->getdata = sun_read;
    mod->handle_event = event_handler;
    mod->metadata_update = metadata_update;

    mod->internal = calloc(1, sizeof(im_sun_state));
    s = mod->internal;

    s->fd = -1; /* Set it to something invalid, for now */

    thread_mutex_create(&s->metadatalock);

    current = params;

    while (current) {
        if (!strcmp(current->name, "rate"))
            sample_rate = s->device_info.record.sample_rate = atoi(current->value);
        else if (!strcmp(current->name, "channels"))
            channels = s->device_info.record.channels = atoi(current->value);
        else if (!strcmp(current->name, "device"))
            device = current->value;
        else if (!strcmp(current->name, "metadata"))
            use_metadata = atoi(current->value);
        else if(!strcmp(current->name, "metadatafilename"))
            ices_config->metadata_filename = current->value;
        else
            LOG_WARN1("Unknown parameter %s for sun module", current->name);
        current = current->next;
    }

    /* First up, lets open the audio device */
    if((s->fd = open(device, O_RDONLY, 0)) < 0) {
        LOG_ERROR2("Failed to open audio device %s: %s", 
                device, strerror(errno));
        goto fail;
    }

    /* Try and set up what we want */
    AUDIO_INITINFO(&s->device_info);
    s->device_info.record.sample_rate = sample_rate;
    s->device_info.record.channels = channels; 
    s->device_info.record.precision = 16;
    s->device_info.record.encoding = AUDIO_ENCODING_LINEAR;
    s->device_info.record.port = AUDIO_LINE_IN;
    s->device_info.record.pause = 0;

    if (ioctl(s->fd, AUDIO_SETINFO, &s->device_info) < 0) {
        LOG_ERROR2("Failed to configure audio device %s: %s",
                device, strerror(errno));
        goto fail;
    }
#ifdef __sun
    ioctl (s->fd, I_FLUSH, FLUSHR);
#endif
#ifdef __OpenBSD__
    ioctl (s->fd, AUDIO_FLUSH, NULL);
#endif

    /* Check all went according to plan */
    if (s->device_info.record.sample_rate != sample_rate) {
        LOG_ERROR0("Couldn't set sampling rate");
        goto fail;
    }
    if (s->device_info.record.channels != channels) {
        LOG_ERROR0("Couldn't set number of channels");
        goto fail;
    }
    if (s->device_info.record.precision != 16) {
        LOG_ERROR0("Couldn't set 16 bit precision");
        goto fail;
    }
    if (s->device_info.record.encoding != AUDIO_ENCODING_LINEAR) {
        LOG_ERROR0("Couldn't set linear encoding");
        goto fail;
    }

    /* We're done, and we didn't fail! */
    LOG_INFO3("Opened audio device %s at %d channel(s), %d Hz", 
            device, channels, sample_rate);

    if(use_metadata)
    {
        LOG_INFO0("Starting metadata update thread");
        if(ices_config->metadata_filename)
            thread_create("im_sun-metadata", metadata_thread_signal, mod, 1);
        else
            thread_create("im_sun-metadata", metadata_thread_stdin, mod, 1);
    }

    return mod;

fail:
    close_module(mod); /* safe, this checks for valid contents */
    return NULL;
}


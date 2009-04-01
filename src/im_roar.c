/* im_oss.c
 * - Raw PCM/Ogg Vorbis input from RoarAudio
 *
 * Copyright (c) 2001 Michael Smith <msmith@labyrinth.net.au>
 * Copyright (c) 2009 Philipp Schafft <lion@lion.leolix.org>
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
#include <fcntl.h>


#include <thread/thread.h>
#include "cfgparse.h"
#include "stream.h"
#include "metadata.h"
#include "inputmodule.h"

#include "im_roar.h"

#define MODULE "input-roar/"
#include "logging.h"

#define BUFSIZE 2048

/* Some platforms (freebsd) don't define this, so just define it to something
 * that should be treated the same
 */
#ifndef ERESTART
#define ERESTART EINTR
#endif

static void close_module(input_module_t *mod)
{
    if(mod)
    {
        if(mod->internal)
        {
            im_roar_state *s = mod->internal;

            if(s->fd >= 0)
                close(s->fd);


            thread_mutex_destroy(&s->metadatalock);
            free(s);

            roar_disconnect(&s->con);
        }
        free(mod);
    }
}

static int event_handler(input_module_t *mod, enum event_type ev, void *param)
{
    im_roar_state *s = mod->internal;

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
    im_roar_state *s = self;
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
static int roar_read(void *self, ref_buffer *rb)
{
    int result;
    im_roar_state *s = self;

    rb->buf = malloc(BUFSIZE*2*s->channels);
    if(!rb->buf)
        return -1;

    result = read(s->fd, rb->buf, BUFSIZE * 2 * s->channels);

    rb->len = result;
    rb->aux_data = s->rate * s->channels * 2;

    if(s->newtrack)
    {
        rb->critical = 1;
        s->newtrack  = 0;
    }

    if(result == -1 && (errno == EINTR || errno == ERESTART))
    {
        return 0; /* Non-fatal error */
    }
    else if(result <= 0)
    {
        if(result == 0)
            LOG_INFO0("Reached EOF, no more data available");
        else
            LOG_ERROR1("Error reading from sound server: %s", strerror(errno));
        free(rb->buf);
        return -1;
    }

    return rb->len;
}

input_module_t *roar_open_module(module_param_t *params)
{
    input_module_t *mod = calloc(1, sizeof(input_module_t));
    im_roar_state *s;
    module_param_t *current;
    char * server = NULL;
    int    codec  = ROAR_CODEC_DEFAULT;
    int    bits   = 16;
    int    dir    = ROAR_DIR_MONITOR;
    int    use_metadata = 1; /* Default to on */

    mod->getdata = roar_read;
    mod->handle_event = event_handler;
    mod->metadata_update = metadata_update;

    mod->internal = calloc(1, sizeof(im_roar_state));
    s = mod->internal;

    s->fd = -1; /* Set it to something invalid, for now */
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
        else if(!strcmp(current->name, "codec"))
            codec = roar_str2codec(current->value);
        else if(!strcmp(current->name, "device"))
            server = current->value;
        else if(!strcmp(current->name, "metadata"))
            use_metadata = atoi(current->value);
        else if(!strcmp(current->name, "metadatafilename"))
            ices_config->metadata_filename = current->value;
        else
            LOG_WARN1("Unknown parameter %s for roar module", current->name);

        current = current->next;
    }

    mod->type = ICES_INPUT_PCM;

    switch (codec) {
        case ROAR_CODEC_PCM_LE:
          mod->subtype = INPUT_PCM_LE_16;
         break;
        case ROAR_CODEC_PCM_BE:
          mod->subtype = INPUT_PCM_BE_16;
         break;
        case ROAR_CODEC_OGG_GENERAL:
          LOG_WARN0("Codec may not work, specify ogg_vorbis for Vorbis streaming");
        case ROAR_CODEC_OGG_VORBIS:
          mod->type = ICES_INPUT_VORBIS;
          // we do not set mod->subtype here, strange design ices2 has...
         break;
        case -1:
         LOG_ERROR0("Unknown Codec");
         return NULL;
        default:
         LOG_ERROR1("Unsupported Codec: %s", roar_codec2str(codec));
         return NULL;
    }


    /* First up, lets open the audio device */
    if ( roar_simple_connect(&s->con, server, "ices2") == -1 ) {
        LOG_ERROR2("Failed to open sound server %s: %s",
                server, strerror(errno));
        goto fail;
    }

    /* Now, set the required parameters on that device */
    if ( (s->fd = roar_simple_new_stream_obj(&s->con, &s->stream, s->rate, s->channels, bits, codec, dir)) == -1 ) {
        LOG_ERROR2("Failed to create a new stream on sound server %s: %s",
                server, strerror(errno));
        goto fail;
    }

    /* We're done, and we didn't fail! */
    LOG_INFO3("Opened sound server at %s at %d channel(s), %d Hz", 
            server, s->channels, s->rate);

    if(use_metadata)
    {
        LOG_INFO0("Starting metadata update thread");
        if(ices_config->metadata_filename)
            thread_create("im_roar-metadata", metadata_thread_signal, mod, 1);
        else
            thread_create("im_roar-metadata", metadata_thread_stdin, mod, 1);
    }

    return mod;

fail:
    close_module(mod); /* safe, this checks for valid contents */
    return NULL;
}



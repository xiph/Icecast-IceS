/* im_roar.c
 * - Raw PCM/Ogg Vorbis input from RoarAudio
 *
 * Copyright (c) 2001      Michael Smith <msmith@labyrinth.net.au>
 * Copyright (c) 2009-2012 Philipp Schafft <lion@lion.leolix.org>
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

#ifdef ROAR_FT_SONAME_LIBROAR2
#define _set_flags roar_stream_set_flags
#else
#define _set_flags roar_stream_set_flags2
#endif

#define MODULE "input-roar/"
#include "logging.h"

#define BUFSIZE 2048

static void close_module(input_module_t *mod)
{
    if(mod)
    {
        if(mod->internal)
        {
            im_roar_state *s = mod->internal;

            if(s->vss)
                roar_vs_close(s->vss, ROAR_VS_TRUE, NULL);

            if (s->plugins)
            {
                roar_plugincontainer_appsched_trigger(s->plugins, ROAR_DL_APPSCHED_FREE);
                roar_plugincontainer_unref(s->plugins);
            }


            thread_mutex_destroy(&s->metadatalock);
            free(s);
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
    ssize_t result;
    int err;
    im_roar_state *s = self;

    roar_plugincontainer_appsched_trigger(s->plugins, ROAR_DL_APPSCHED_UPDATE);

    rb->buf = malloc(BUFSIZE * roar_info2framesize(&s->info)/8);
    if(!rb->buf)
        return -1;

    result = roar_vs_read(s->vss, rb->buf, BUFSIZE * roar_info2framesize(&s->info)/8, &err);

    rb->len = result;
    rb->aux_data = roar_info2bitspersec(&s->info)/8;

    if(s->newtrack)
    {
        rb->critical = 1;
        s->newtrack  = 0;
    }

    if(result == -1 && err == ROAR_ERROR_INTERRUPTED)
    {
        return 0; /* Non-fatal error */
    }
    else if(result <= 0)
    {
        if(result == 0)
            LOG_INFO0("Reached EOF, no more data available");
        else
            LOG_ERROR1("Error reading from sound server: %s", roar_vs_strerr(err));
        free(rb->buf);
        rb->buf = NULL;
        return -1;
    }

    return rb->len;
}

static void roar_plugin_load(input_module_t *mod, const char *nameargs)
{
    im_roar_state *s;
    struct roar_dl_librarypara *para;
    char *name, *args;

    s = mod->internal;

    name = strdup(nameargs);
    args = strstr(name, " ");
    if (args)
    {
        *args = 0;
        args++;
    }

    para = roar_dl_para_new(args, mod, IM_ROAR_APPNAME, IM_ROAR_ABIVERSION);
    if (!para)
    {
        LOG_WARN2("Can not create parameters for plugin %s: %s", name, roar_error2str(roar_error));
        free(name);
        return;
    }

    if (roar_plugincontainer_load(s->plugins, name, para) == -1)
    {
        LOG_WARN2("Can not load plugin %s: %s", name, roar_error2str(roar_error));
    }

    roar_dl_para_unref(para);
    free(name);
}

input_module_t *roar_open_module(module_param_t *params)
{
    input_module_t *mod = calloc(1, sizeof(input_module_t));
    im_roar_state *s;
    module_param_t *current;
    const char * server = NULL;
    int    dir    = ROAR_DIR_MONITOR;
    enum { MD_NONE = 0, MD_FILE = 1, MD_STREAM = 2 } use_metadata = MD_STREAM;
    int err;

    mod->getdata = roar_read;
    mod->handle_event = event_handler;
    mod->metadata_update = metadata_update;

    mod->internal = calloc(1, sizeof(im_roar_state));
    s = mod->internal;

    if(roar_profile2info(&s->info, "default") == -1)
    {
        LOG_ERROR1("Failed to get default audio profile: %s",
                roar_error2str(roar_error));
        return NULL;
    }
    s->info.bits = 16;

    s->vss       = NULL;

    s->plugins   = roar_plugincontainer_new_simple(IM_ROAR_APPNAME, IM_ROAR_ABIVERSION);
    if (!s->plugins)
    {
        LOG_ERROR1("Failed to create plugin container: %s",
                roar_error2str(roar_error));
        return NULL;
    }

    thread_mutex_create(&s->metadatalock);

    current = params;

    while(current)
    {
        if(!strcmp(current->name, "rate"))
            s->info.rate = roar_str2rate(current->value);
        else if(!strcmp(current->name, "channels"))
            s->info.channels = roar_str2channels(current->value);
        else if(!strcmp(current->name, "codec"))
            s->info.codec = roar_str2codec(current->value);
        else if(!strcmp(current->name, "aiprofile")) {
            if (roar_profile2info(&s->info, current->value) == -1) {
                LOG_WARN2("Can not get audio info profile %s: %s", current->value, roar_error2str(roar_error));
            }
            s->info.bits = 16;
        } else if(!strcmp(current->name, "dir")) {
            if ( !strcasecmp(current->value, "monitor") ) {
                dir = ROAR_DIR_MONITOR;
            } else if ( !strcasecmp(current->value, "record") ) {
                dir = ROAR_DIR_RECORD;
            } else {
                LOG_WARN2("Unknown value %s for parameter %s for roar module", current->value, current->name);
            }
        } else if(!strcmp(current->name, "device") || !strcmp(current->name, "server"))
            server = current->value;
        else if(!strcmp(current->name, "metadata")) {
            if ( !strcasecmp(current->value, "none") ) {
                use_metadata = MD_NONE;
            } else if ( !strcasecmp(current->value, "file") ) {
                use_metadata = MD_FILE;
            } else if ( !strcasecmp(current->value, "stream") ) {
                use_metadata = MD_STREAM;
            } else {
                use_metadata = atoi(current->value);
            }
        } else if(!strcmp(current->name, "metadatafilename")) {
            ices_config->metadata_filename = current->value;
            use_metadata = MD_FILE;
        } else if(!strcmp(current->name, "plugin")) {
            roar_plugin_load(mod, current->value);
        } else
            LOG_WARN1("Unknown parameter %s for roar module", current->name);

        current = current->next;
    }

    mod->type = ICES_INPUT_PCM;

    switch (s->info.codec) {
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
         LOG_ERROR1("Unsupported Codec: %s", roar_codec2str(s->info.codec));
         return NULL;
    }

    roar_plugincontainer_appsched_trigger(s->plugins, ROAR_DL_APPSCHED_INIT);

    /* Open the VS connection */
    if ( (s->vss = roar_vs_new(server, IM_ROAR_PROGNAME, &err)) == NULL ) {
        LOG_ERROR2("Failed to open sound server %s: %s",
                server, roar_vs_strerr(err));
        goto fail;
    }

    /* Now, set the required parameters on that device */
    if ( roar_vs_stream(s->vss, &s->info, dir, &err) == -1 ) { 
        LOG_ERROR2("Failed to create a new stream on sound server %s: %s",
                server, roar_vs_strerr(err));
        goto fail;
    }

    if ( _set_flags(roar_vs_connection_obj(s->vss, NULL), roar_vs_stream_obj(s->vss, NULL),
                                ROAR_FLAG_META, ROAR_RESET_FLAG) != 0 ) {
        LOG_WARN0("Can not reset metadata flag from stream");
    }

    /* We're done, and we didn't fail! */
    LOG_INFO3("Opened sound server at %s at %d channel(s), %d Hz", 
            server, s->info.channels, s->info.rate);

    switch (use_metadata) {
     case MD_NONE:
      break;
     case MD_FILE:
        LOG_INFO0("Starting metadata update thread");
        if(ices_config->metadata_filename)
            thread_create("im_roar-metadata", metadata_thread_signal, mod, 1);
        else
            thread_create("im_roar-metadata", metadata_thread_stdin, mod, 1);
      break;
     case MD_STREAM:
        if ( _set_flags(roar_vs_connection_obj(s->vss, NULL), roar_vs_stream_obj(s->vss, NULL),
                                    ROAR_FLAG_META, ROAR_SET_FLAG) != 0 ) {
            LOG_WARN0("Can not set metadata flag from stream");
        }
      break;
    }

    return mod;

fail:
    close_module(mod); /* safe, this checks for valid contents */
    return NULL;
}



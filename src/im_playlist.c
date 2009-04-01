/* playlist.c
 * - Basic playlist functionality
 *
 * $Id: im_playlist.c,v 1.15 2004/01/12 23:39:39 karl Exp $
 *
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
#include <ogg/ogg.h>

#include <thread/thread.h>

#include "cfgparse.h"
#include "stream.h"
#include "event.h"

#include "inputmodule.h"
#include "input.h"
#include "im_playlist.h"

#include "playlist_basic.h"

#define MODULE "playlist-builtin/"
#include "logging.h"

#define BUFSIZE 4096

typedef struct _module 
{
    char *name;
    int (*init)(module_param_t *, playlist_state_t *);
} module;

static module modules[] = {
    { "basic", playlist_basic_initialise},
    { "script", playlist_script_initialise},
    {NULL,NULL}
};

static void close_module(input_module_t *mod)
{
    if (mod == NULL) return;

    if (mod->internal) 
    {
        playlist_state_t *pl = (playlist_state_t *)mod->internal;
        pl->clear(pl->data);
        ogg_sync_clear(&pl->oy);
        free(pl);
    }
    free(mod);
}

static int event_handler(input_module_t *mod, enum event_type ev, void *param)
{
    switch(ev)
    {
        case EVENT_SHUTDOWN:
            close_module(mod);
            break;
        case EVENT_NEXTTRACK:
            LOG_INFO0("Moving to next file in playlist.");
            ((playlist_state_t *)mod->internal)->nexttrack = 1;
            break;
        default:
            LOG_WARN1("Unhandled event %d", ev);
            return -1;
    }

    return 0;
}

/* Core streaming function for this module
 * This is what actually produces the data which gets streamed.
 *
 * returns:  >0  Number of bytes read
 *            0  Non-fatal error.
 *           <0  Fatal error.
 */
static int playlist_read(void *self, ref_buffer *rb)
{
    playlist_state_t *pl = (playlist_state_t *)self;
    int bytes;
    unsigned char *buf;
    char *newfn;
    int result;
    ogg_page og;

    if (pl->errors > 5) 
    {
        LOG_WARN0("Too many consecutive errors - exiting");
        return -1;
    }

    if (!pl->current_file || pl->nexttrack) 
    {
        pl->nexttrack = 0;

        if (pl->current_file && strcmp (pl->filename, "-"))
        {
            fclose(pl->current_file);
            pl->current_file = NULL;
        }

        newfn = pl->get_filename(pl->data);
        if (!newfn)
        {
            LOG_INFO0("No more filenames available, end of playlist");
            return -1; /* No more files available */
        }

        if (strcmp (newfn, "-"))
        {
            if (!pl->allow_repeat && pl->filename && !strcmp(pl->filename, newfn))
            {
                LOG_ERROR0("Cannot play same file twice in a row, skipping");
                pl->errors++;
                pl->free_filename (pl->data, newfn);
                return 0;
            }
            pl->free_filename(pl->data, pl->filename);
            pl->filename = newfn;

            pl->current_file = fopen(pl->filename, "rb");
            if (!pl->current_file) 
            {
                LOG_WARN2("Error opening file \"%s\": %s",pl->filename, strerror(errno));
                pl->errors++;
                return 0;
            }
            LOG_INFO1("Currently playing \"%s\"", pl->filename);
        }
        else
        {
            LOG_INFO0("Currently playing from stdin");
            pl->current_file = stdin;
            pl->free_filename(pl->data, pl->filename);
            pl->filename = newfn;
        }

        /* Reinit sync, so that dead data from previous file is discarded */
        ogg_sync_clear(&pl->oy);
        ogg_sync_init(&pl->oy);
    }
    input_sleep ();

    while(1)
    {
        result = ogg_sync_pageout(&pl->oy, &og);
        if(result < 0)
            LOG_WARN1("Corrupt or missing data in file (%s)", pl->filename);
        else if(result > 0)
        {
            if (ogg_page_bos (&og))
            {
               if (ogg_page_serialno (&og) == pl->current_serial)
                   LOG_WARN1 ("detected duplicate serial number reading \"%s\"", pl->filename);

               pl->current_serial = ogg_page_serialno (&og);
            }
            if (input_calculate_ogg_sleep (&og) < 0)
            {
                pl->nexttrack = 1;
                return 0;
            }
            rb->len = og.header_len + og.body_len;
            rb->buf = malloc(rb->len);
            rb->aux_data = og.header_len;

            memcpy(rb->buf, og.header, og.header_len);
            memcpy(rb->buf+og.header_len, og.body, og.body_len);
            if(ogg_page_granulepos(&og)==0)
                rb->critical = 1;
            break;
        }

        /* If we got to here, we didn't have enough data. */
        buf = ogg_sync_buffer(&pl->oy, BUFSIZE);
        bytes = fread(buf,1, BUFSIZE, pl->current_file);
        if (bytes <= 0) 
        {
            if (feof(pl->current_file)) 
            {
                pl->nexttrack = 1;
                return playlist_read(pl,rb);
            } 
            else 
            {
                LOG_ERROR2("Read error from \"%s\": %s", pl->filename, strerror(errno));
                fclose(pl->current_file);
                pl->current_file=NULL;
                pl->errors++;
                return 0; 
            }
        }
        else
            ogg_sync_wrote(&pl->oy, bytes);
    }

    pl->errors=0;

    return rb->len;
}

input_module_t *playlist_open_module(module_param_t *params)
{
    input_module_t *mod = calloc(1, sizeof(input_module_t));
    playlist_state_t *pl;
    module_param_t *current;
    int (*init)(module_param_t *, playlist_state_t *)=NULL;

    mod->type = ICES_INPUT_VORBIS;
    mod->getdata = playlist_read;
    mod->handle_event = event_handler;
    mod->metadata_update = NULL; /* Not used for playlists */

    mod->internal = calloc(1, sizeof(playlist_state_t));
    pl = (playlist_state_t *)mod->internal;

    current = params;
    while(current)
    {
        if (!strcmp(current->name, "type"))
        {
            int current_module = 0;

            while(modules[current_module].init)
            {
                if(!strcmp(current->value, modules[current_module].name))
                {
                    init = modules[current_module].init; 
                    break;
                }
                current_module++;
            }
            
            if(!init)
            {
                LOG_ERROR1("Unknown playlist type \"%s\"", current->value);
                goto fail;
            }
        }
        current = current->next;
    }

    if(init)
    {
        if(init(params, pl))
        {
            LOG_ERROR0("Playlist initialisation failed");
            goto fail;
        }
        else 
        {
            ogg_sync_init(&pl->oy);
            return mod; /* Success. Finished initialising */
        }
    }
    else
        LOG_ERROR0("No playlist type given, cannot initialise playlist module");

fail:
    if (mod) 
    {
        if (mod->internal)
            free(mod->internal);
        free(mod);
    }

    return NULL;
}



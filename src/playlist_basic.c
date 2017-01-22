/* playlist_basic.c
 * - Simple built-in unscripted playlist
 *
 * $Id: playlist_basic.c,v 1.13 2003/08/13 00:58:02 karl Exp $
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
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cfgparse.h"
#include "inputmodule.h"
#include "im_playlist.h"
#include "playlist_basic.h"

#define MODULE "playlist-basic/"
#include "logging.h"

static void shuffle(char **buf, size_t len)
{
    size_t i, range;
    long int d;
    char *temp;

    for (i = 0; i < len; i++)
    {
        range = len - i;
        /*
         * Only accept a random number if it is smaller than the largest
         * multiple of our range - reduces PRNG bias
         */
        do {
            d = random();
        } while (d > (RAND_MAX - (long int)(RAND_MAX % range)));

        /*
         * The range starts at the item we want to shuffle, excluding
         * already shuffled items
         */
        d = i + ((size_t)d % range);

        temp = buf[d];
        buf[d] = buf[i];
        buf[i] = temp;
    }
    LOG_DEBUG0("Playlist has been shuffled");

    LOG_DEBUG1("Playlist contains %d songs:", len);
    for (i = 0; i < len; i++)
    {
        LOG_DEBUG2("%u: %s", i+1, buf[i]);
    }
}

static int load_playlist(basic_playlist *data)
{
    FILE *file;
    char buf[1024];
    int buflen;
    char *ret;
    int is_next_entry = 1;

    file = fopen(data->file, "rb");

    if (file == NULL) 
    {
        LOG_ERROR2("Playlist file %s could not be opened: %s", 
                data->file, strerror(errno));
        return -1;
    }

    if(data->pl) 
    {
        int i;
        for(i = 0; i < data->len; i++)
            free(data->pl[i]);
        free(data->pl);
    }
    data->pl = NULL;
    data->len = 0;
    buflen = 0;
    while (1) 
    {
        if((ret=fgets(buf, sizeof(buf), file)) == NULL) break;
        if(ret[0]==0) break;

        if(ret[0]=='\n' || (ret[0]=='\r' && ret[1]=='\n'))
            continue;

        if(ret[0] == '#') /* Commented out entry */
            continue;

        ret[strlen(ret)-1] = 0;

        /* De-fuck windows files. */
        if(strlen(ret) > 0 && ret[strlen(ret)-1] == '\r')
            ret[strlen(ret)-1] = 0;

        if (data->type == PLAYLIST_VCLT)
        {
            if (!strcmp(ret, "=="))
            {
                is_next_entry = 1;
                continue;
            }

            if (!is_next_entry)
                continue;

            if (!!strncasecmp(ret, "FILENAME=", 9))
                continue;
            ret += 9;
            is_next_entry = 0;
        }

        if(buflen < data->len+1)
        {
            buflen += 100;
            data->pl = realloc(data->pl, buflen*sizeof(char *));
        }

        data->pl[data->len++] = strdup(ret);
    }

    if (!data->len)
    {
        LOG_ERROR1("Playlist file %s does not contain any track",
                data->file);
        return -1;
    }

    if(data->random)
        shuffle(data->pl, data->len);

    return 0;
}

static void playlist_basic_clear(void *data)
{
    basic_playlist *pl = data;
    if(pl)
    {
        if(pl->pl) 
        {
            int i;
            for(i=0; i < pl->len; i++)
                free(pl->pl[i]);
            free(pl->pl);
        }
        free(pl);
    }
}

static char *playlist_basic_get_next_filename(void *data)
{
    basic_playlist *pl = (basic_playlist *)data;
    char *ptr = NULL;
    int reload_playlist = 0;
    struct stat st;

    if (stat(pl->file, &st)) 
    {
        LOG_ERROR2("Couldn't stat file \"%s\": %s", pl->file, strerror(errno));
        return NULL;
    }

    if (pl->pl)
    {
        if (st.st_mtime != pl->mtime)
        {
            reload_playlist = 1;
            LOG_INFO1("Reloading playlist after file \"%s\" changed", pl->file);
            pl->mtime = st.st_mtime;
        }
    }
    else
    {
        LOG_INFO1("Loading playlist from file \"%s\"", pl->file);
        reload_playlist = 1;
        pl->mtime = st.st_mtime;
    }

    if (reload_playlist)
    {
        if (load_playlist(pl) < 0)
            return NULL;
        if (pl->restartafterreread)
            pl->pos = 0;
    }

    if (pl->pos >= pl->len)  /* reached the end of the potentially updated list */
    {
        if (pl->once) 
            return NULL;

        pl->pos = 0;
        if (pl->random)
            shuffle(pl->pl, pl->len);
    }

    ptr = pl->pl [pl->pos++];

    return strdup(ptr);
}

static void playlist_basic_free_filename(void *data, char *fn)
{
   (void)data;
   free (fn);
}

static basic_playlist_type _str2type(const char * type) {
    if ( !strcmp(type, "basic") )
        return PLAYLIST_BASIC;
    if ( !strcmp(type, "m3u") )
        return PLAYLIST_M3U;
    if ( !strcmp(type, "vclt") )
        return PLAYLIST_VCLT;
    return PLAYLIST_INVALID;
}

int playlist_basic_initialise(module_param_t *params, playlist_state_t *pl)
{
    basic_playlist *data;

    pl->get_filename = playlist_basic_get_next_filename;
    pl->clear = playlist_basic_clear;
    pl->free_filename = playlist_basic_free_filename;
    pl->file_ended = NULL;

    pl->data = calloc(1, sizeof(basic_playlist));
    data = (basic_playlist *)pl->data;

    while (params != NULL) {
        if (!strcmp(params->name, "file")) 
            data->file = params->value;
        else if (!strcmp(params->name, "random")) 
            data->random = atoi(params->value);
        else if(!strcmp(params->name, "once"))
            data->once = atoi(params->value);
        else if(!strcmp(params->name, "allow-repeats"))
            pl->allow_repeat = atoi(params->value);
        else if(!strcmp(params->name, "restart-after-reread"))
            data->restartafterreread = atoi(params->value);
        else if(!strcmp(params->name, "type"))
            data->type = _str2type(params->value);
        else 
        {
            LOG_WARN1("Unknown parameter to playlist input module: %s", 
                    params->name);
        }
        params = params->next;
    }

    if (!data->file) 
    {
        LOG_ERROR0("No filename specified for playlist module");
        free(data);
        return -1;
    }

    if (data->type == PLAYLIST_INVALID)
    {
        LOG_ERROR0("Playlist type invalid for playlist module");
        free(data);
        return -1;
    }

    return 0;
}

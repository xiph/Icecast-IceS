/* playlist_basic.c
 * - Simple built-in unscripted playlist
 *
 * $Id: playlist_basic.c,v 1.7 2002/08/10 04:26:52 msmith Exp $
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
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config.h"
#include "inputmodule.h"
#include "im_playlist.h"
#include "playlist_basic.h"

#define MODULE "playlist-basic/"
#include "logging.h"

static void shuffle(char **buf, int len)
{
	/* From ices1 src/playlist_basic/rand.c */
	int n,d;
	char *temp;

	n = len;
	while(n > 1)
	{
		d = (int)(((double) rand() / RAND_MAX) * n);
		temp = buf[d];
		buf[d] = buf[n-1];
		buf[n-1] = temp;
		--n;
	}
}

static int load_playlist(basic_playlist *data)
{
	FILE *file;
	struct stat st;
	char buf[1024];
	int buflen;

	if(stat(data->file, &st)) 
	{
		LOG_ERROR2("Couldn't stat file \"%s\": %s", data->file, strerror(errno));
		return -1;
	}
	data->mtime = st.st_mtime;

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
		if(fgets(buf,1024, file) == NULL) break;
		if(buf[0]==0) break;

        if(buf[0]=='\n' || (buf[0]=='\r' && buf[1]=='\n'))
            continue;

        if(buf[0] == '#') /* Commented out entry */
            continue;

		buf[strlen(buf)-1] = 0;

		/* De-fuck windows files. */
		if(strlen(buf) > 0 && buf[strlen(buf)-1] == '\r')
			buf[strlen(buf)-1] = 0;

		if(buflen < data->len+1)
		{
			buflen += 100;
			data->pl = realloc(data->pl, buflen*sizeof(char *));
		}

		data->pl[data->len++] = strdup(buf);
	}

	if(data->random)
		shuffle(data->pl, data->len);


	return 0;
}

void playlist_basic_clear(void *data)
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

char *playlist_basic_get_next_filename(void *data)
{
	basic_playlist *pl = (basic_playlist *)data;
	struct stat st;

	if (!pl->pl) return NULL;

	if(stat(pl->file, &st)) 
	{
		LOG_ERROR2("Couldn't stat file \"%s\": %s", pl->file, strerror(errno));
		return NULL;
	}

	if(st.st_mtime != pl->mtime)
	{
		LOG_INFO1("Reloading playlist after file \"%s\" changed", pl->file);
		if(load_playlist(pl) < 0)
            return NULL;
        if(pl->restartafterreread)
            pl->pos = 0;
	}

	if (pl->pos < pl->len) 
	{
		return pl->pl[pl->pos++];
	} 
	else if(!pl->once)
	{
		if(pl->random)
			shuffle(pl->pl, pl->len);
		pl->pos = 1;
		return pl->pl[0];
	}
	else
		return NULL; /* Once-through mode, at end */
}

void playlist_basic_free_filename(void *data, char *fn)
{
}

int playlist_basic_initialise(module_param_t *params, playlist_state_t *pl)
{
	basic_playlist *data;

	pl->get_filename = playlist_basic_get_next_filename;
	pl->clear = playlist_basic_clear;
    pl->free_filename = playlist_basic_free_filename;

	pl->data = calloc(1, sizeof(basic_playlist));
	data = (basic_playlist *)pl->data;

	while (params != NULL) {
		if (!strcmp(params->name, "file")) 
		{
			if (data->file) free(data->file);
			data->file = params->value;
		} 
		else if (!strcmp(params->name, "random")) 
			data->random = atoi(params->value);
		else if(!strcmp(params->name, "once"))
			data->once = atoi(params->value);
		else if(!strcmp(params->name, "restart-after-reread"))
			data->restartafterreread = atoi(params->value);
		else if(!strcmp(params->name, "type"))
			; /* We recognise this, but don't want to do anything with it */
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

	return load_playlist(data);
}


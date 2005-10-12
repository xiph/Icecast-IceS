/* im_playlist.h
 * - Basic playlist functionality
 *
 * $Id: im_playlist.h,v 1.6 2004/01/12 23:39:39 karl Exp $
 *
 * Copyright (c) 2001 Michael Smith <msmith@xiph.org>
 *
 * This program is distributed under the terms of the GNU General
 * Public License, version 2. You may use, modify, and redistribute
 * it under the terms of this license. A copy should be included
 * with this source.
 */

#ifndef __IM_PLAYLIST_H__
#define __IM_PLAYLIST_H__

#include "inputmodule.h"
#include <ogg/ogg.h>

typedef struct _playlist_state_tag
{
    FILE *current_file;
    char *filename; /* Currently streaming file */
    int errors; /* Consecutive errors */
    int current_serial;
    int nexttrack;
    int allow_repeat;
    ogg_sync_state oy;
    
    char *(*get_filename)(void *data); /* returns the next desired filename */
    void (*free_filename)(void *data, char *fn); /* Called when im_playlist is
                                                    done with this filename */
    void (*clear)(void *data); /* module clears self here */

    void *data; /* Internal data for this particular playlist module */

} playlist_state_t;

input_module_t *playlist_open_module(module_param_t *params);

#endif  /* __IM_PLAYLIST_H__ */

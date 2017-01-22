/* playlist_basic.h
 * - Simple unscripted playlist
 *
 * $Id: playlist_basic.h,v 1.5 2003/03/16 14:21:49 msmith Exp $
 *
 * Copyright (c) 2001 Michael Smith <msmith@xiph.org>
 *
 * This program is distributed under the terms of the GNU General
 * Public License, version 2. You may use, modify, and redistribute
 * it under the terms of this license. A copy should be included
 * with this source.
 */

#ifndef __PLAYLIST_BASIC_H__
#define __PLAYLIST_BASIC_H__

typedef enum
{
    PLAYLIST_INVALID,
    PLAYLIST_BASIC,
    PLAYLIST_M3U,
    PLAYLIST_VCLT,
} basic_playlist_type;

typedef struct
{
    char **pl;
    int len;
    int pos;
    char *file; /* Playlist file */
    time_t mtime;
    int random;
    int once;
    int restartafterreread;
    basic_playlist_type type; /* Playlist type */

} basic_playlist;

int playlist_basic_initialise(module_param_t *params, playlist_state_t *pl);
int playlist_script_initialise(module_param_t *params, playlist_state_t *pl);


#endif  /* __PLAYLIST_BASIC_H__ */

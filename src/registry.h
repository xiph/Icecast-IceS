/* registry.h
 * - Registry of input/output/processing modules.
 *
 * $Id: registry.h,v 1.1 2002/02/07 09:11:12 msmith Exp $
 *
 * Copyright (c) 2001-2002 Michael Smith <msmith@labyrinth.net.au>
 *
 * This program is distributed under the terms of the GNU General
 * Public License, version 2. You may use, modify, and redistribute
 * it under the terms of this license. A copy should be included
 * with this source.
 */

#ifndef __REGISTRY_H__
#define __REGISTRY_H__

#include "process.h"
#include "config.h"
#include "im_playlist.h"
#include "im_stdinpcm.h"
#include "stream.h"
#include "encode.h"

#ifdef HAVE_OSS
#include "im_oss.h"
#endif 

/*
#ifdef HAVE_SUN_AUDIO
#include "im_sun.h"
#endif
*/
typedef struct _module
{   
        char *name;
        open_func open;
} module;

static module registered_modules[] = {
    { "encode", encode_open_module},
    { "stream", stream_open_module},
    { "playlist", playlist_open_module},
    { "stdinpcm", stdin_open_module}, 
#ifdef HAVE_OSS
    { "oss", oss_open_module}, 
#endif
#ifdef HAVE_SUN_AUDIO 
    { "sun", sun_open_module},
#endif
    {NULL,NULL}
};

#endif /* __REGISTRY_H__ */


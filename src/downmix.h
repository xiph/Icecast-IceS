/* downmix.h
 * - stereo->mono downmixing
 *
 * $Id: downmix.h,v 1.1 2002/07/20 12:52:06 msmith Exp $
 *
 * Copyright (c) 2001 Michael Smith <msmith@labyrinth.net.au>
 *
 * This program is distributed under the terms of the GNU General
 * Public License, version 2. You may use, modify, and redistribute
 * it under the terms of this license. A copy should be included
 * with this source.
 */

#ifndef __DOWNMIX_H
#define __DOWNMIX_H

typedef struct {
    float *buffer;
    int buflen;
} downmix_state;

downmix_state *downmix_initialise(void);
void downmix_clear(downmix_state *s);
void downmix_buffer(downmix_state *s, signed char *buf, int len, int be);

#endif


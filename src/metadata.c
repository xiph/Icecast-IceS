/* metadata.c
 * - Metadata manipulation
 *
 * $Id: metadata.c,v 1.11 2003/03/22 02:27:55 karl Exp $
 *
 * Copyright (c) 2001 Michael Smith <msmith@labyrinth.net.au>
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

#include "cfgparse.h"
#include "inputmodule.h"
#include "event.h"
#include "thread/thread.h"

#define MODULE "metadata/"
#include "logging.h"

volatile int metadata_update_signalled = 0;

void *metadata_thread_stdin(void *arg)
{
    char buf[1024];
    input_module_t *mod = arg;

    while(1)
    {
        char **md = NULL;
        int comments = 0;

        while(fgets(buf, 1024, stdin))
        {
            if(buf[0] == '\n')
                break;
            else
            {
                if(buf[strlen(buf)-1] == '\n')
                    buf[strlen(buf)-1] = 0;
                md = realloc(md, (comments+2)*sizeof(char *));
                md[comments] = malloc(strlen(buf)+1);

                memcpy(md[comments], buf, strlen(buf)+1);
                comments++;
            }
        }

        if(md) /* Don't update if there's nothing there */
        {
            md[comments]=0;

            /* Now, let's actually use the new data */
            LOG_INFO0("Updating metadata");
            mod->handle_event(mod,EVENT_METADATAUPDATE,md);
        }

    }
}

void *metadata_thread_signal(void *arg)
{
    char buf[1024];
    input_module_t *mod = arg;

    while(1)
    {
        char **md = NULL;
        int comments = 0;
        FILE *file;

        while(metadata_update_signalled == 0){
            thread_cond_wait(&ices_config->event_pending_cond);
        }

        metadata_update_signalled = 0;

        file = fopen(ices_config->metadata_filename, "r");
        if(!file) {
            LOG_WARN2("Failed to open file %s for metadata update: %s", 
                    ices_config->metadata_filename, strerror(errno));
            continue;
        }

        while(fgets(buf, 1024, file))
        {
            if(buf[0] == '\n')
                break;
            else
            {
                if(buf[strlen(buf)-1] == '\n')
                    buf[strlen(buf)-1] = 0;
                md = realloc(md, (comments+2)*sizeof(char *));
                md[comments] = malloc(strlen(buf)+1);

                memcpy(md[comments], buf, strlen(buf)+1);
                comments++;
                LOG_INFO2 ("tag %d is %s", comments, buf);
            }
        }

        fclose(file);

        if(md) /* Don't update if there's nothing there */
        {
            md[comments]=0;

            /* Now, let's actually use the new data */
            LOG_INFO0("Updating metadata");
            mod->handle_event(mod,EVENT_METADATAUPDATE,md);
        }

    }
}



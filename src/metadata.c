/* metadata.c
 * - Metadata manipulation
 *
 * $Id: metadata.c,v 1.13 2004/02/24 15:39:14 karl Exp $
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
#include <unistd.h>
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

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

    if (ices_config->background)
    {
        LOG_INFO0("Metadata thread shutting down, tried to use "
                "stdin from background");
        return NULL;
    }
    while(1)
    {
        char **md = NULL;
        int comments = 0;
        int wait_for_data = 1;

        /* wait for data */
        while (wait_for_data)
        {
            struct timeval t;
            fd_set fds;
            FD_ZERO (&fds);
            FD_SET (0, &fds);
            t.tv_sec = 0;
            t.tv_usec = 150;
            
            switch (select (1, &fds, NULL, NULL, &t))
            {
            case 1:
                wait_for_data = 0;
            case 0:
                break;
            default:
                if (errno != EAGAIN)
                {
                    LOG_INFO1 ("shutting down thread (%d)", errno);
                    return NULL; /* problem get out quick */
                }
                break;
            }

            if (ices_config->shutdown)
            {
                LOG_INFO0 ("metadata thread shutting down");
                return NULL;
            }
        }
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

        while(metadata_update_signalled == 0)
        {
            thread_cond_wait(&ices_config->event_pending_cond);
            if (ices_config->shutdown)
            {
                LOG_INFO0 ("metadata thread shutting down");
                return NULL;
            }
            LOG_DEBUG0("meta thread wakeup");
        }

        metadata_update_signalled = 0;

        file = fopen(ices_config->metadata_filename, "r");
        if(!file) {
            LOG_WARN2("Failed to open file \"%s\" for metadata update: %s", 
                    ices_config->metadata_filename, strerror(errno));
            continue;
        }

        LOG_DEBUG1("reading metadata from \"%s\"", ices_config->metadata_filename);
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
        else
            LOG_INFO0("No metadata has been read");

    }
}



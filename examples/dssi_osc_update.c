/*
 *  This program is in the public domain
 *
 *  $Id$
 */

#include <unistd.h>
#include <libgen.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <lo/lo.h>

#include "osc_url.h"

static volatile int done = 0;

int update_handler(const char *path, const char *types, lo_arg **argv,
                    int argc, void *data, void *user_data)
{
    printf("port %d = %f\n", argv[0]->i, argv[1]->f);

    return 0;
}

void osc_error(int num, const char *msg, const char *path)
{
    printf("liblo server error %d in path %s: %s\n", num, path, msg);
}

int main(int argc, char *argv[])
{
    lo_server_thread st;
    lo_target t;
    char *host, *port, *path;
    char full_path[256];
    char *my_url = "osc://localhost:4445/";

    if (argc != 2) {
	fprintf(stderr, "usage: %s <osc url>\n", argv[0]);
	return 1;
    }

    host = osc_url_get_hostname(argv[1]);
    port = osc_url_get_port(argv[1]);
    path = osc_url_get_path(argv[1]);
    t = lo_target_new(host, port);

    snprintf(full_path, 255, "%s/update", path);

    st = lo_server_thread_new("4445", NULL);
printf("registering %s\n", path);
    lo_server_thread_add_method(st, path, "if", update_handler,
				osc_error);
    lo_server_thread_start(st);

    printf("sending osc://%s:%s%s \"%s\"\n", host, port, full_path, my_url);
    lo_send(t, full_path, "s", my_url);
    free(host);
    free(port);
    free(path);

    /* quit if we go 1 second without an OSC update message */
    while (!done) {
	done = 1;
	sleep(100);
    }

    return 0;
}

/* vi:set ts=8 sts=4 sw=4: */
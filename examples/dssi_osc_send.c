/*
 *  This program is in the public domain
 *
 *  $Id$
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <lo/lo.h>

#include "osc_url.h"

int main(int argc, char *argv[])
{
    lo_target t;
    char *host, *port, *path;
    int ladspa_port;
    float value;

    if (argc != 4) {
	fprintf(stderr, "usage: %s <osc url> <port> <value>\n", argv[0]);
	return 1;
    }

    host = osc_url_get_hostname(argv[1]);
    port = osc_url_get_port(argv[1]);
    path = osc_url_get_path(argv[1]);
    ladspa_port = atoi(argv[2]);
    value = atof(argv[3]);
    t = lo_target_new(host, port);
    printf("sending osc://%s:%s%s %d %f\n", host, port, path, ladspa_port,
	   value);
    lo_send(t, path, "if", ladspa_port, value);
    if (lo_target_errno(t)) {
	printf("liblo error: %s\n", lo_target_errstr(t));
    }
    free(host);
    free(port);
    free(path);

    return 0;
}

/* vi:set ts=8 sts=4 sw=4: */

/*
 *  This program is in the public domain
 *
 *  $Id$
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <lo/lo.h>

int main(int argc, char *argv[])
{
    lo_address a;
    char *host, *port, *path;
    int ladspa_port;
    float value;

    if (argc != 4) {
	fprintf(stderr, "usage: %s <osc url> <port> <value>\n", argv[0]);
	return 1;
    }

    host = lo_url_get_hostname(argv[1]);
    port = lo_url_get_port(argv[1]);
    path = lo_url_get_path(argv[1]);
    ladspa_port = atoi(argv[2]);
    value = atof(argv[3]);
    a = lo_address_new(host, port);
    printf("sending osc.udp://%s:%s%s %d %f\n", host, port, path, ladspa_port,
	   value);
    lo_send(a, path, "if", ladspa_port, value);
    if (lo_address_errno(a)) {
	printf("liblo error: %s\n", lo_address_errstr(a));
    }
    free(host);
    free(port);
    free(path);

    return 0;
}

/* vi:set ts=8 sts=4 sw=4: */

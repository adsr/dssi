/*
 *  This program is piblic domain
 *
 *  $Id$
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

char *osc_url_get_hostname(const char *url)
{
    char *hostname = malloc(strlen(url));

    if (sscanf(url, "osc://%[^:/]", hostname)) {
        return hostname;
    }

    /* doesnt look like an OSC URL */
    return NULL;
}

char *osc_url_get_port(const char *url)
{
    char *port = malloc(strlen(url));

    if (sscanf(url, "osc://%[^:]:%[0-9]", port, port)) {
        return port;
    }

    /* doesnt look like an OSC URL with port number */
    return NULL;
}

char *osc_url_get_path(const char *url)
{
    char *path = malloc(strlen(url));

    if (sscanf(url, "osc://%[^:]:%[0-9]%s", path, path, path)) {
        return path;
    }

    /* doesnt look like an OSC URL with port number and path*/
    return NULL;
}

/* vi:set ts=8 sts=4 sw=4: */

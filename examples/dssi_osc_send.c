/*
 *  This program is in the public domain
 *
 *  $Id$
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <lo/lo.h>

void
usage(char *program_name)
{
    char *base_name = strrchr(program_name, '/');
    
    if (base_name && *(base_name + 1) != 0) {
        base_name += 1;
    } else {
        base_name = program_name;
    }
    fprintf(stderr, "usage: %s [option] <OSC URL> <values>\n\n", program_name);
    fprintf(stderr, "example 'control' message (set control port 1 to 0.1):\n\n"
                    "  %s    osc.udp://localhost:19383/dssi/test.1/control 1 0.1\n\n", base_name);
    fprintf(stderr, "or:\n\n"
                    "  %s -c osc.udp://localhost:19383/dssi/test.1         1 0.1\n\n", base_name);
    fprintf(stderr, "example 'program' message (select bank 0 program number 7):\n\n"
                    "  %s -p osc.udp://localhost:19383/dssi/test.1 0 7\n\n", base_name);
    fprintf(stderr, "example 'midi' message (send a note on, middle C, velocity 64):\n\n"
                    "  %s -m osc.udp://localhost:19383/dssi/test.1 144 60 64\n\n", base_name);
    fprintf(stderr, "example 'configure' message (send key 'load' and value '/tmp/patches.pat'):\n\n"
                    "  %s -C osc.udp://localhost:19383/dssi/test.1 load /tmp/patches.pat\n\n", base_name);
    exit(1);
}

int main(int argc, char *argv[])
{
    lo_address a;
    char *url, *host, *port, *path;
    char mode = '-';
    char full_path[256];

    int ladspa_port, int0, int1;
    float value;
    unsigned char midi[4];

    if (argc < 4) {
        usage(argv[0]);
        /* does not return */
    }
    url = argv[1];
    if (argv[1][0] == '-') {
        if (argv[1][1] == 'c' ||
            argv[1][1] == 'p' ||
            argv[1][1] == 'm' ||
            argv[1][1] == 'C') {
            mode = argv[1][1];
            url = argv[2];
        } else {
            usage(argv[0]);
        }
    }

    host = lo_url_get_hostname(url);
    port = lo_url_get_port(url);
    path = lo_url_get_path(url);
    a = lo_address_new(host, port);

    switch (mode) {

      case '-': /* old 'control' syntax */
        if (argc != 4) usage(argv[0]);
        ladspa_port = atoi(argv[2]);
        value = atof(argv[3]);
        printf("sending osc.udp://%s:%s%s %d %f\n", host, port, path,
               ladspa_port, value);
        lo_send(a, path, "if", ladspa_port, value);
        break;

      case 'c': /* new 'control' syntax */
        if (argc != 5) usage(argv[0]);
        snprintf(full_path, 255, "%s/control", path);
        ladspa_port = atoi(argv[3]);
        value = atof(argv[4]);
        printf("sending osc.udp://%s:%s%s %d %f\n", host, port, full_path,
               ladspa_port, value);
        lo_send(a, full_path, "if", ladspa_port, value);
        break;

      case 'p': /* send 'program' message */
        if (argc != 5) usage(argv[0]);
        snprintf(full_path, 255, "%s/program", path);
        int0 = atoi(argv[3]);
        int1 = atoi(argv[4]);
        printf("sending osc.udp://%s:%s%s %d %d\n", host, port, full_path,
               int0, int1);
        lo_send(a, full_path, "ii", int0, int1);
        break;

      case 'm': /* send 'midi' message */
        if (argc > 7) usage(argv[0]);
        snprintf(full_path, 255, "%s/midi", path);
        midi[0] = atoi(argv[3]);
        midi[1] = argc > 4 ? atoi(argv[4]) : 0;
        midi[2] = argc > 5 ? atoi(argv[5]) : 0;
        midi[3] = argc > 6 ? atoi(argv[6]) : 0;
        printf("sending osc.udp://%s:%s%s %02x %02x %02x %02x\n", host, port, full_path,
               midi[0], midi[1], midi[2], midi[3]);
        lo_send(a, full_path, "m", midi);
        break;

      case 'C': /* send 'configure' message */
        if (argc != 5) usage(argv[0]);
        snprintf(full_path, 255, "%s/configure", path);
        printf("sending osc.udp://%s:%s%s \"%s\" \"%s\"\n", host, port, full_path,
               argv[3], argv[4]);
        lo_send(a, full_path, "ss", argv[3], argv[4]);
        break;
    }

    if (lo_address_errno(a)) {
	printf("liblo error: %s\n", lo_address_errstr(a));
    }
    free(host);
    free(port);
    free(path);

    return 0;
}

/* vi:set ts=8 sts=4 sw=4: */

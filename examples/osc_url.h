#ifndef OSC_URL_H
#define OSC_URL_H

#ifdef __cplusplus
extern "C" {
#endif

/* these all return char *'s that must be free()'d after use */

char *osc_url_get_hostname(const char *url);
char *osc_url_get_port(const char *url);
char *osc_url_get_path(const char *url);

#ifdef __cplusplus
}
#endif

#endif
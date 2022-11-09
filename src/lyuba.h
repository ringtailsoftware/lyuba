#ifndef LYUBA_H
#define LYUBA_H 1

#include <stdbool.h>

typedef void (*lyuba_auth_cb_t)(bool ok, const char *authToken);
typedef void (*lyuba_toot_cb_t)(bool ok);
typedef void (*lyuba_search_cb_t)(bool ok, const char *content);

void lyuba_init(void);
void lyuba_loop(void);

void lyuba_authenticate(lyuba_auth_cb_t cb);
void lyuba_toot(const char *authToken, const char *msg, lyuba_toot_cb_t cb);
void lyuba_searchTag(const char *authToken, const char *tag, lyuba_search_cb_t cb);
const char *lyuba_getAuthToken(void);

#endif


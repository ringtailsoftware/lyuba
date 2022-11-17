#ifndef LYUBA_H
#define LYUBA_H 1

#include <stdbool.h>
#include "httpc.h"

typedef void (*lyuba_auth_cb_t)(bool ok, const char *authToken);
typedef void (*lyuba_toot_cb_t)(bool ok);
typedef void (*lyuba_stream_cb_t)(bool ok, const char *username, const char *content);

typedef struct {
    const char *host;
    const char *username;
    const char *password;
    const char *client_id;
    const char *client_secret;
    bool authGetToken;
// FIXME move authCb into lyuba_conn_t (wrapper for httpc_req, may need an extra userdata in it for this?)
    lyuba_auth_cb_t authCb; // needs storing here for 2-phase auth
    char negotiated_bearer_access_token[256];
} lyuba_t;

typedef httpc_req_t * lyuba_conn_t;

lyuba_t *lyuba_init(const char *host, const char *username, const char *password);
void lyuba_term(lyuba_t *lyuba);
void lyuba_loop(lyuba_t *lyuba);
void lyuba_authenticate(lyuba_t *lyuba, lyuba_auth_cb_t cb);
const char *lyuba_getAuthToken(lyuba_t *lyuba);
void lyuba_toot(lyuba_t *lyuba, const char *authToken, const char *msg, lyuba_toot_cb_t cb);
lyuba_conn_t lyuba_stream(lyuba_t *lyuba, const char *authToken, const char *tag, lyuba_stream_cb_t cb);
void lyuba_close(lyuba_t *lyuba, lyuba_conn_t conn);

#endif


#ifndef HTTPC_H
#define HTTPC_H 1

#include "linebuffer.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

#define HTTP_TIMEOUT_MS 60000

typedef enum {
    HTTPC_ERR_OK = 0,
    HTTPC_ERR_FAIL = 1
} httpc_err_t;

typedef struct httpc_req_s httpc_req_t;

typedef httpc_err_t (*httpc_data_cb_t)(httpc_err_t err, httpc_req_t *req, int status_code, const char *data, size_t len);

typedef enum {
    HTTPC_REQ_STATE_RUNNABLE,
    HTTPC_REQ_STATE_CLOSEABLE,
    HTTPC_REQ_STATE_KILLABLE,
    HTTPC_REQ_STATE_DEAD
} httpc_req_state_t;

struct httpc_req_s {
    httpc_req_state_t state;
    esp_http_client_config_t config;
    esp_http_client_handle_t client;
    size_t httpBufMaxLen;
    size_t httpBufLen;
    char *httpBuf;
    char *postBuf;
    httpc_data_cb_t dataCb;
    struct httpc_req_s *prev;
    struct httpc_req_s *next;
    linebuffer_t *lb;
    void *userdata;
    size_t userdataLen;
    bool autoResume;    // hack to workaround "E (33430) TRANSPORT_BASE: esp_tls_conn_read error, errno=No more processes", HTTPS dropping connection in is_async mode
};

httpc_err_t httpc_init(void);
void httpc_loop(void);
httpc_req_t *httpc_get(const char *host, const char *path, const char *auth, size_t maxLen, bool linebuffered, httpc_data_cb_t dataCb, void *userdata, size_t userdataLen, bool isEndlessStream);
httpc_req_t *httpc_post(const char *host, const char *path, const char *auth, const char *postData, size_t maxLen, bool linebuffered, httpc_data_cb_t dataCb, void *userdata, size_t userdataLen);
httpc_err_t httpc_close(httpc_req_t *req);

#endif



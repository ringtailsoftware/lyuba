#include <Wire.h>
#include <Arduino.h>
#include "ctype.h"
#include "cJSON.h"
#include "linebuffer.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

#include "linebuffer.h"
#include "httpc.h"
#include "esp_task_wdt.h"

//#define HTTPC_DEBUG 1
#define HTTPC_TASK_PRIORITY tskIDLE_PRIORITY
#define HTTPC_TASK_STACK_SIZE 4096

#define LOCK_WAIT_TICKS 10000

static TaskHandle_t httpc_task_handle;

static bool inited = false;
static httpc_req_t *reqs_ll_head = NULL; // linked list of requests, head

static SemaphoreHandle_t userSemaphore = NULL;

static void lock_ll(void) {
    if (xSemaphoreTake(userSemaphore, (TickType_t)LOCK_WAIT_TICKS) != pdTRUE ) {
        Serial.printf("*** LOCK FAILED, FIXME\r\n");    // shouldn't happen
    }
}

static void unlock_ll(void) {
    xSemaphoreGive(userSemaphore);
}

void httpc_loop_internal(void);
httpc_err_t httpc_init_internal(void);

static void httpc_task_function(void * pvParameter) {
    esp_task_wdt_init(60, false);
    httpc_init_internal();
    while(1) {
        esp_task_wdt_reset();
        httpc_loop_internal();
        vTaskDelay(50);
//        Serial.printf("*");
    }
}

httpc_err_t httpc_init(void) {
    if (pdPASS != xTaskCreate(httpc_task_function, "httpc", HTTPC_TASK_STACK_SIZE, NULL, HTTPC_TASK_PRIORITY, &httpc_task_handle)) {
        return HTTPC_ERR_FAIL;
    } else {
        return HTTPC_ERR_OK;
    }
}

void httpc_loop(void) {
    // FIXME
}


static void httpc_ll_push(httpc_req_t *req) {
#ifdef HTTPC_DEBUG
    Serial.printf("httpc_ll_push %p\r\n", req);
#endif
    // add to front of list
    if (NULL == reqs_ll_head) {
        reqs_ll_head = req;
        req->prev = NULL;
        req->next = NULL;
    } else {
        reqs_ll_head->prev = req;
        req->prev = NULL;
        req->next = reqs_ll_head;
        reqs_ll_head = req;
    }
}

static void httpc_ll_remove(httpc_req_t *req) {
#ifdef HTTPC_DEBUG
    Serial.printf("httpc_ll_remove %p\r\n", req);
#endif
    if (NULL == req->prev) {    // first item in list
        reqs_ll_head = req->next;
    } else if (NULL == req->next) {    // last item in list
        req->prev->next = NULL;
    } else {    // mid-list
        req->prev->next = req->next;
        req->next->prev = req->prev;
    }
}

httpc_err_t httpc_init_internal(void) {
    if (inited) {
        return HTTPC_ERR_OK;
    }
    inited = true;
    reqs_ll_head = NULL;
    userSemaphore = xSemaphoreCreateMutex();
    return HTTPC_ERR_OK;
}

static void httpc_dispose(httpc_req_t *req) {
#ifdef HTTPC_DEBUG
    Serial.printf("httpc_dispose %p\r\n", req);
#endif
    if (NULL != req) {
        if (NULL != req->httpBuf) {
            free(req->httpBuf);
        }
        if (NULL != req->lb) {
            linebuffer_term(req->lb);
            free(req->lb);
        }
        if (NULL != req->postBuf) {
            free(req->postBuf);
        }
        if (NULL != req->userdata) {
            free(req->userdata);
        }
        free(req);
    }
}

httpc_err_t httpc_close(httpc_req_t *req) {
#ifdef HTTPC_DEBUG
    Serial.printf("httpc_close (setting HTTPC_REQ_STATE_CLOSEABLE) %p\r\n", req);
#endif
//    lock_ll();

    if (NULL != req) {
        if (NULL != reqs_ll_head) {
            httpc_req_t *rp = reqs_ll_head;
            while(rp != NULL) {
                // only update if req appears in ll, so it's safe to call httpc_close() after connection has been destroyed
                if (rp == req) {
                    if (req->state == HTTPC_REQ_STATE_RUNNABLE) {
                        req->state = HTTPC_REQ_STATE_CLOSEABLE;
                    }
                    break;
                } else {
                    rp = rp->next;
                }
            }
        }
    }
//    unlock_ll();
    return HTTPC_ERR_OK;
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    httpc_req_t *req = (httpc_req_t *)evt->user_data;

    esp_task_wdt_reset();

    switch(evt->event_id) {
        default:
        break;
        case HTTP_EVENT_ERROR:
            Serial.printf("HTTP_EVENT_ERROR req=%p\r\n", req);
            req->state = HTTPC_REQ_STATE_CLOSEABLE;
            break;
        case HTTP_EVENT_ON_CONNECTED:
#ifdef HTTPC_DEBUG
            Serial.printf("HTTP_EVENT_ON_CONNECTED\r\n");
#endif
            break;
        case HTTP_EVENT_HEADER_SENT:
#ifdef HTTPC_DEBUG
            Serial.printf("HTTP_EVENT_HEADER_SENT\r\n");
#endif
            break;
        case HTTP_EVENT_ON_HEADER:
#ifdef HTTPC_DEBUG
            Serial.printf("HTTP_EVENT_ON_HEADER, key=%s, value=%s\r\n", evt->header_key, evt->header_value);
#endif
            break;
        case HTTP_EVENT_ON_DATA:
#ifdef HTTPC_DEBUG
            Serial.printf("HTTP_EVENT_ON_DATA, len=%d\r\n", evt->data_len);
#endif
            if (req->state != HTTPC_REQ_STATE_RUNNABLE) {
                esp_http_client_close(req->client);
                break;
            }

            if (req->httpBufMaxLen == 0) {
                // pass buffer straight to cb
                req->dataCb(HTTPC_ERR_OK, req, esp_http_client_get_status_code(req->client), (const char *)evt->data, evt->data_len);
            } else {
                if (req->lb != NULL) {  // linebuffered
                    if (0 != linebuffer_write(req->lb, (const char *)evt->data, evt->data_len)) {
                        req->dataCb(HTTPC_ERR_FAIL, req, esp_http_client_get_status_code(req->client), NULL, 0);
                    }
                } else { // accumulate for big final send
                    if ((req->httpBufMaxLen-1) - req->httpBufLen >= evt->data_len) {
                        memcpy(req->httpBuf + req->httpBufLen, evt->data, evt->data_len);
                        req->httpBufLen += evt->data_len;
                    } else {
                        Serial.printf("** httpBuf too small (%d)\r\n", req->httpBufMaxLen);
                        req->dataCb(HTTPC_ERR_FAIL, req, esp_http_client_get_status_code(req->client), NULL, 0);
                    }
                }
            }
            break;
        case HTTP_EVENT_ON_FINISH:
#ifdef HTTPC_DEBUG
            Serial.printf("** HTTP_EVENT_ON_FINISH\r\n");
#endif
            if (req->httpBufMaxLen == 0 || req->lb != NULL) {
                req->dataCb(HTTPC_ERR_OK, req, esp_http_client_get_status_code(req->client), NULL, 0);
            } else {
                // null terminate buffer
                req->httpBuf[req->httpBufLen] = '\0';
                req->dataCb(HTTPC_ERR_OK, req, esp_http_client_get_status_code(req->client), req->httpBuf, req->httpBufLen);
            }
            if (req->state == HTTPC_REQ_STATE_RUNNABLE) {    // once by user closed, don't reopen
                // if notAutoresuming or didn't get "200 OK"
                if (!req->autoResume || esp_http_client_get_status_code(req->client) != 200) {  // don't keep retrying if we get a 401!
                    req->state = HTTPC_REQ_STATE_CLOSEABLE;
                }
            }
            break;
        case HTTP_EVENT_DISCONNECTED:
#ifdef HTTPC_DEBUG
            Serial.printf("HTTP_EVENT_DISCONNECTED\r\n");
#endif
            req->state = HTTPC_REQ_STATE_KILLABLE;

            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                Serial.printf("Last esp error code: 0x%x\r\n", err);
                Serial.printf("Last mbedtls failure: 0x%x\r\n", mbedtls_err);
            }
            break;
    }
    return ESP_OK;
}

void httpc_loop_internal(void) {
    uint32_t err;
    httpc_req_t *req;

    if (!inited) {
        Serial.println("Err httpc_loop called before httpc_init!\r\n");
    }

    lock_ll();

#ifdef HTTPC_DEBUG_VERBOSE
    // dump ll
    req = reqs_ll_head;
    if (NULL != reqs_ll_head) {
        Serial.printf("ll\r\n");
        while(req != NULL) {
            Serial.printf("%p <- (%p %d) -> %p\r\n", req->prev, req, req->state, req->next);
            req = req->next;
        }
    }
#endif

    // make a pass to close and cleanup 
    req = reqs_ll_head;
    while(req != NULL) {
        switch(req->state) {
            case HTTPC_REQ_STATE_RUNNABLE:
            break;
            case HTTPC_REQ_STATE_CLOSEABLE:
#ifdef HTTPC_DEBUG
                Serial.printf("req %p HTTPC_REQ_STATE_CLOSEABLE -> esp_http_client_close\r\n", req);
#endif
                esp_http_client_close(req->client);
            break;
            case HTTPC_REQ_STATE_KILLABLE:
#ifdef HTTPC_DEBUG
                Serial.printf("req %p HTTPC_REQ_STATE_KILLABLE -> esp_http_client_cleanup\r\n", req);
#endif
                esp_http_client_cleanup(req->client);
                req->state = HTTPC_REQ_STATE_DEAD;
            break;
            case HTTPC_REQ_STATE_DEAD:
            break;
        }
        req = req->next;
    }

    // make a pass to remove first dead/stale connection
    req = reqs_ll_head;
    while(req != NULL) {
        if (req->state == HTTPC_REQ_STATE_DEAD) {
#ifdef HTTPC_DEBUG
            Serial.printf("req %p HTTPC_REQ_STATE_DEAD -> ll_remove/dispose\r\n", req);
#endif
            httpc_ll_remove(req);
            httpc_dispose(req);
            break;
        } else {
            req = req->next;
        }
    }

    // make a pass to process all running connections
    req = reqs_ll_head;
    while(req != NULL) {
        switch(req->state) {
            case HTTPC_REQ_STATE_RUNNABLE:
                esp_err_t err;
                unlock_ll();    // esp_http_client_perform() may block for a long time, preventing new connections being added
                err = esp_http_client_perform(req->client);
                lock_ll();
                if (err != ESP_ERR_HTTP_EAGAIN) {
#ifdef HTTPC_DEBUG
                    Serial.printf("*** esp_http_client_perform err %d\r\n", (int)err);
#endif
                }
            break;
            case HTTPC_REQ_STATE_CLOSEABLE:
            case HTTPC_REQ_STATE_KILLABLE:
            case HTTPC_REQ_STATE_DEAD:
            break;
        }
        req = req->next;
    }
    unlock_ll();
}

static int lineCb(linebuffer_t *lb, const char *line, void *userdata) {
    httpc_req_t *req = (httpc_req_t *)userdata;
#ifdef HTTPC_DEBUG
    Serial.printf("line='%s'\r\n", line);
#endif
    req->dataCb(HTTPC_ERR_OK, req, esp_http_client_get_status_code(req->client), line, strlen(line));
    return 0;
}

static httpc_req_t *httpc_request(const char *host, const char *path, const char *auth, size_t maxLen, bool linebuffered, httpc_data_cb_t dataCb, void *userdata, size_t userdataLen, esp_http_client_method_t method, const char *post_data, bool isEndlessStream) {
    httpc_req_t *req = NULL;
    uint32_t err;

#ifdef HTTPC_DEBUG
    Serial.printf("httpc_request host=%s path=%s auth=%s\r\n", host==NULL?"":host, path==NULL?"":path, auth==NULL?"":auth);
#endif

    if (host == NULL || path == NULL || dataCb == NULL) {
        Serial.println("httpc_request bad args");
        return NULL;
    }
    if (NULL == (req = (httpc_req_t *)malloc(sizeof(httpc_req_t)))) {
        Serial.println("httpc_request out of mem");
        return NULL;
    }
    memset(req, 0x00, sizeof(httpc_req_t));
    req->httpBufMaxLen = maxLen;
    req->dataCb = dataCb;

    if (isEndlessStream) {
        req->autoResume = true;
    }

    req->userdataLen = userdataLen;
    if (userdataLen > 0) {  // clone userdata into req
        if (NULL == (req->userdata = malloc(userdataLen))) {
            Serial.println("httpc_request out of mem userdata");
            httpc_dispose(req);
            return NULL;
        }
#ifdef HTTPC_DEBUG
    Serial.printf("httpc_request: cloning userdata %d\r\n", userdataLen);
#endif
        memcpy(req->userdata, userdata, userdataLen);
    }

    if (maxLen > 0) {
        if (linebuffered) {
            if (NULL == (req->lb = (linebuffer_t *)malloc(sizeof(linebuffer_t)))) {
                Serial.println("httpc_request out of mem lb");
                httpc_dispose(req);
                return NULL;
            }
            if (0 != linebuffer_init(req->lb, maxLen, lineCb)) {
                Serial.println("Linebuffer init failed!");
                httpc_dispose(req);
                return NULL;
            }
            linebuffer_set_userdata(req->lb, req);
        } else {
            if (NULL == (req->httpBuf = (char *)malloc(req->httpBufMaxLen))) {
                Serial.printf("httpc_request out of mem (buf %d)\r\n", (int)req->httpBufMaxLen);
                httpc_dispose(req);
                return NULL;
            }
        }
    }

    if (NULL != post_data) {
        if (NULL == (req->postBuf = strdup(post_data))) {
            Serial.printf("httpc_request out of mem postdata\r\n");
            httpc_dispose(req);
            return NULL;
        }
    }

    req->config.transport_type = HTTP_TRANSPORT_OVER_SSL;
    req->config.host = host;
    req->config.path = path;
    req->config.event_handler = http_event_handler;
    req->config.crt_bundle_attach = esp_crt_bundle_attach;
    req->config.is_async = true;
    req->config.user_data = (void *)req;

    req->client = esp_http_client_init(&req->config);

    esp_http_client_set_timeout_ms(req->client, HTTP_TIMEOUT_MS);
    esp_http_client_set_method(req->client, method);

    if (auth != NULL) {
        esp_http_client_set_header(req->client, "Authorization", auth);
    }

    if (NULL != req->postBuf) {
        esp_http_client_set_header(req->client, "Content-Type", "application/x-www-form-urlencoded");
#ifdef HTTPC_DEBUG
        Serial.printf("POST path=%s data=%s\r\n", path, req->postBuf);
#endif
        esp_http_client_set_post_field(req->client, req->postBuf, strlen(req->postBuf));
    }

    req->state = HTTPC_REQ_STATE_RUNNABLE;
    lock_ll();
    httpc_ll_push(req);
    unlock_ll();

    return req;
}

httpc_req_t *httpc_get(const char *host, const char *path, const char *auth, size_t maxLen, bool linebuffered, httpc_data_cb_t dataCb, void *userdata, size_t userdataLen, bool isEndlessStream) {
    return httpc_request(host, path, auth, maxLen, linebuffered, dataCb, userdata, userdataLen, HTTP_METHOD_GET, NULL, isEndlessStream);
}

httpc_req_t *httpc_post(const char *host, const char *path, const char *auth, const char *postData, size_t maxLen, bool linebuffered, httpc_data_cb_t dataCb, void *userdata, size_t userdataLen) {
    return httpc_request(host, path, auth, maxLen, linebuffered, dataCb, userdata, userdataLen, HTTP_METHOD_POST, postData, false);
}



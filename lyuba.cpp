#include <Wire.h>
#include <Arduino.h>
#include "ctype.h"
#include "cJSON.h"
#include "lyuba.h"
#include "Preferences.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

#define MASTODON_CLIENT_NAME "lyuba"
#define MASTODON_CLIENT_URL "http://github.com/ringtailsoftware/lyuba"

static Preferences preferences_lyuba;

//#define LYUBA_DEBUG 1

typedef enum {
    LYUBASTATE_INIT,
    LYUBASTATE_APP_SETUP,
    LYUBASTATE_APP_SETUP_WAITRSP,
    LYUBASTATE_APP_SETUP_DONE,
    LYUBASTATE_TOKEN_SETUP,
    LYUBASTATE_TOKEN_SETUP_WAITRSP,
    LYUBASTATE_TOKEN_SETUP_DONE,
    LYUBASTATE_READY,
    LYUBASTATE_READY_NOTIFY,
    LYUBASTATE_TOOT_SETUP,
    LYUBASTATE_TOOT_SETUP_WAITRSP,
    LYUBASTATE_SEARCHTAG_SETUP,
    LYUBASTATE_SEARCHTAG_SETUP_WAITRSP,
    LYUBASTATE_SEARCHTAG_SETUP_DONE,
} web_state_t;

#define MAX_HTTP_OUTPUT_BUFFER 8192 // largest HTTP response we can handle, smaller would be ok without search

static volatile lyuba_auth_cb_t authCb = NULL;
static volatile lyuba_toot_cb_t tootCb = NULL;
static volatile lyuba_search_cb_t searchCb = NULL;

static web_state_t lyubastate = LYUBASTATE_INIT;

static uint8_t httpBuf[MAX_HTTP_OUTPUT_BUFFER];
static int httpBufCount = 0;

static char client_id[128];
static char client_secret[128];
static char access_token[256];
static char negotiated_bearer_access_token[256];
static char postBuf[1300];  // largest HTTP POST we can make
static char tagBuf[128];

static const char *host = NULL;
static const char *pem = NULL;
static const char *username = NULL;
static const char *password = NULL;

esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
        default:
        break;
        case HTTP_EVENT_ERROR:
#ifdef LYUBA_DEBUG
            Serial.printf("HTTP_EVENT_ERROR\r\n");
#endif
            break;
        case HTTP_EVENT_ON_CONNECTED:
#ifdef LYUBA_DEBUG
            Serial.printf("HTTP_EVENT_ON_CONNECTED\r\n");
#endif
            break;
        case HTTP_EVENT_HEADER_SENT:
#ifdef LYUBA_DEBUG
            Serial.printf("HTTP_EVENT_HEADER_SENT\r\n");
#endif
            break;
        case HTTP_EVENT_ON_HEADER:
#ifdef LYUBA_DEBUG
            Serial.printf("HTTP_EVENT_ON_HEADER, key=%s, value=%s\r\n", evt->header_key, evt->header_value);
            break;
#endif
        case HTTP_EVENT_ON_DATA:
#ifdef LYUBA_DEBUG
            Serial.printf("HTTP_EVENT_ON_DATA, len=%d\r\n", evt->data_len);
#endif

            if ((MAX_HTTP_OUTPUT_BUFFER-1) - httpBufCount >= evt->data_len) {
                memcpy(httpBuf + httpBufCount, evt->data, evt->data_len);
                httpBufCount += evt->data_len;
            } else {
                Serial.printf("** httpBuf too small (%d)\r\n", MAX_HTTP_OUTPUT_BUFFER-1);
                switch(lyubastate) {
                    case LYUBASTATE_APP_SETUP_WAITRSP:
                    case LYUBASTATE_TOKEN_SETUP_WAITRSP:
                        if (NULL != authCb) {
                            authCb(false, NULL);
                        }
                    break;
                    default:
                    break;
                }
                return ESP_FAIL;
            }
            break;
        case HTTP_EVENT_ON_FINISH:
#ifdef LYUBA_DEBUG
            Serial.printf("HTTP_EVENT_ON_FINISH\r\n");
#endif
            // null terminate buffer
            httpBuf[httpBufCount] = '\0';
            switch(lyubastate) {
                case LYUBASTATE_APP_SETUP_WAITRSP:
                    lyubastate = LYUBASTATE_APP_SETUP_DONE;
                break;
                case LYUBASTATE_TOKEN_SETUP_WAITRSP:
                    lyubastate = LYUBASTATE_TOKEN_SETUP_DONE;
                break;
                case LYUBASTATE_TOOT_SETUP_WAITRSP:
                    lyubastate = LYUBASTATE_READY_NOTIFY;
                break;
                case LYUBASTATE_SEARCHTAG_SETUP_WAITRSP:
                    lyubastate = LYUBASTATE_SEARCHTAG_SETUP_DONE;
                break;
                default:
                    Serial.printf("Unexpected status on HTTP FINISH\r\n");
                    lyubastate = LYUBASTATE_INIT;
                    return ESP_FAIL;
                break;
            }
            break;
        case HTTP_EVENT_DISCONNECTED:
#ifdef LYUBA_DEBUG
            Serial.printf("HTTP_EVENT_DISCONNECTED\r\n");
#endif
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

static bool post_request(const char *path, const char *post_data, bool useBearer) {
    uint32_t err;
    esp_http_client_config_t config;

    memset(&config, 0x00, sizeof(config));
    config.transport_type = HTTP_TRANSPORT_OVER_SSL;
    config.host = host;
    config.path = path;
    config.event_handler = _http_event_handler;
    config.cert_pem = pem;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);

    // clear response buffer
    httpBufCount = 0;
    // make request
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
    if (useBearer) {
        esp_http_client_set_header(client, "Authorization", negotiated_bearer_access_token);
    }
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    err = esp_http_client_perform(client);
    if (err == ESP_OK) {
#ifdef LYUBA_DEBUG
        Serial.printf("HTTP POST Status = %d, content_length = %d\r\n",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
#endif
        if (esp_http_client_get_status_code(client) == 200) {
            esp_http_client_cleanup(client);
            return true;
        } else {
            Serial.printf("HTTP status code %d\r\n", esp_http_client_get_status_code(client));
            esp_http_client_cleanup(client);
            return false;
        }
    } else {
        Serial.printf("HTTP POST request failed: %s\r\n", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }
}

static bool get_request(const char *path, const char *post_data, bool useBearer) {
    uint32_t err;
    esp_http_client_config_t config;

    memset(&config, 0x00, sizeof(config));
    config.transport_type = HTTP_TRANSPORT_OVER_SSL;
    config.host = host;
    config.path = path;
    config.event_handler = _http_event_handler;
    config.cert_pem = pem;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);

    // clear response buffer
    httpBufCount = 0;
    // make request
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
    if (useBearer) {
        esp_http_client_set_header(client, "Authorization", negotiated_bearer_access_token);
    }
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    err = esp_http_client_perform(client);
    if (err == ESP_OK) {
#ifdef LYUBA_DEBUG
        Serial.printf("HTTP POST Status = %d, content_length = %d\r\n",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
#endif
        if (esp_http_client_get_status_code(client) == 200) {
            esp_http_client_cleanup(client);
            return true;
        } else {
            Serial.printf("HTTP status code %d\r\n", esp_http_client_get_status_code(client));
            esp_http_client_cleanup(client);
            return false;
        }
    } else {
        Serial.printf("HTTP POST request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }
}


void lyuba_init(const char *_host, const char *_pem, const char *_username, const char *_password) {
    host = _host;
    pem = _pem;
    username = _username;
    password = _password;
    lyubastate = LYUBASTATE_INIT;
    httpBufCount = 0;
    preferences_lyuba.begin("lyuba", false);
}

static bool lyubastate_app_setup(void) {
    snprintf(postBuf, sizeof(postBuf), "client_name=%s&redirect_uris=urn:ietf:wg:oauth:2.0:oob&scopes=write read follow&website=%s", MASTODON_CLIENT_NAME, MASTODON_CLIENT_URL);
    return post_request("/api/v1/apps", postBuf, false);
}

static bool lyubastate_token_setup(void) {
    snprintf(postBuf, sizeof(postBuf), "client_id=%s&client_secret=%s&grant_type=password&username=%s&password=%s&scope=write read follow", client_id, client_secret, username, password);
    return post_request("/oauth/token", postBuf, false);
}

static bool lyubastate_toot_setup(void) {
    // postBuf already contains status=... from lyuba_toot()
    return post_request("/api/v1/statuses", postBuf, true);
}

static bool lyubastate_search_setup(void) {
    // postBuf already contains tag from lyuba_search()
    char url[512];
    snprintf(url, sizeof(url), "/api/v1/timelines/tag/%s", tagBuf);
    return get_request(url, "limit=1", true);
}

void lyuba_loop(void) {
    cJSON *json;

    switch(lyubastate) {
        case LYUBASTATE_INIT:
        break;

        case LYUBASTATE_APP_SETUP:
            lyubastate = LYUBASTATE_APP_SETUP_WAITRSP;
            if (!lyubastate_app_setup()) {
                lyubastate = LYUBASTATE_INIT;
                if (NULL != authCb) {
                    authCb(false, NULL);
                }
            }
        break;

        case LYUBASTATE_APP_SETUP_WAITRSP:
        break;

        case LYUBASTATE_APP_SETUP_DONE:
            if (NULL == (json = cJSON_Parse((const char *)httpBuf))) {
                Serial.printf("LYUBASTATE_APP_SETUP_DONE: Bad JSON\r\n");
                lyubastate = LYUBASTATE_INIT;
                if (NULL != authCb) {
                    authCb(false, NULL);
                }
                break;
            } else {
                cJSON *json_client_id, *json_client_secret;
                if (NULL == (json_client_id = cJSON_GetObjectItem(json, "client_id"))) {
                    Serial.printf("LYUBASTATE_APP_SETUP_DONE: missing client_id\r\n");
                    lyubastate = LYUBASTATE_INIT;
                    if (NULL != authCb) {
                        authCb(false, NULL);
                    }
                    cJSON_Delete(json);
                    break;
                }
                if (NULL == (json_client_secret = cJSON_GetObjectItem(json, "client_secret"))) {
                    Serial.printf("LYUBASTATE_APP_SETUP_DONE: missing client_secret\r\n");
                    lyubastate = LYUBASTATE_INIT;
                    if (NULL != authCb) {
                        authCb(false, NULL);
                    }
                    cJSON_Delete(json);
                    break;
                }
                if (!cJSON_IsString(json_client_id) || !cJSON_IsString(json_client_secret)) {
                    Serial.printf("LYUBASTATE_APP_SETUP_DONE: bad types in json\r\n");
                    lyubastate = LYUBASTATE_INIT;
                    if (NULL != authCb) {
                        authCb(false, NULL);
                    }
                    cJSON_Delete(json);
                    break;
                }
#ifdef LYUBA_DEBUG
                Serial.printf("client_id='%s', client_secret='%s'\r\n", json_client_id->valuestring, json_client_secret->valuestring);
#endif
                strncpy(client_id, json_client_id->valuestring, sizeof(client_id));
                strncpy(client_secret, json_client_secret->valuestring, sizeof(client_secret));
                cJSON_Delete(json);
                lyubastate = LYUBASTATE_TOKEN_SETUP;
            }
        break;

        case LYUBASTATE_TOKEN_SETUP:
            lyubastate = LYUBASTATE_TOKEN_SETUP_WAITRSP;
            if (!lyubastate_token_setup()) {
                lyubastate = LYUBASTATE_INIT;
                if (NULL != authCb) {
                    authCb(false, NULL);
                }
            }
        break;

        case LYUBASTATE_TOKEN_SETUP_WAITRSP:
        break;

        case LYUBASTATE_TOKEN_SETUP_DONE:
            if (NULL == (json = cJSON_Parse((const char *)httpBuf))) {
                Serial.printf("LYUBASTATE_TOKEN_SETUP_DONE: Bad JSON\r\n");
                lyubastate = LYUBASTATE_INIT;
                if (NULL != authCb) {
                    authCb(false, NULL);
                }
                break;
            } else {
                cJSON *json_access_token;
                if (NULL == (json_access_token = cJSON_GetObjectItem(json, "access_token"))) {
                    Serial.printf("LYUBASTATE_TOKEN_SETUP_DONE: missing access_token\r\n");
                    lyubastate = LYUBASTATE_INIT;
                    if (NULL != authCb) {
                        authCb(false, NULL);
                    }
                    cJSON_Delete(json);
                    break;
                }
                if (!cJSON_IsString(json_access_token)) {
                    Serial.printf("LYUBASTATE_TOKEN_SETUP_DONE: bad types in json\r\n");
                    lyubastate = LYUBASTATE_INIT;
                    if (NULL != authCb) {
                        authCb(false, NULL);
                    }
                    cJSON_Delete(json);
                    break;
                }
#ifdef LYUBA_DEBUG
                Serial.printf("access_token='%s'\r\n", json_access_token->valuestring);
#endif
                strncpy(access_token, json_access_token->valuestring, sizeof(access_token));
                snprintf(negotiated_bearer_access_token, sizeof(negotiated_bearer_access_token), "Bearer %s", json_access_token->valuestring);
                cJSON_Delete(json);
                lyubastate = LYUBASTATE_READY;

                // write new auth data to flash
                preferences_lyuba.putString("auth", negotiated_bearer_access_token);
                // pass back to user
                if (NULL != authCb) {
                    authCb(true, negotiated_bearer_access_token);
                }
            }
        break;

        case LYUBASTATE_READY_NOTIFY:
            lyubastate = LYUBASTATE_READY;
            if (NULL != tootCb) {
                tootCb(true);
            }
        break;

        case LYUBASTATE_READY:
        break;

        case LYUBASTATE_TOOT_SETUP:
            lyubastate = LYUBASTATE_TOOT_SETUP_WAITRSP;
            if (!lyubastate_toot_setup()) {
                lyubastate = LYUBASTATE_READY;
                if (NULL != tootCb) {
                    tootCb(false);
                }
            }
        break;

        case LYUBASTATE_TOOT_SETUP_WAITRSP:
        break;

        case LYUBASTATE_SEARCHTAG_SETUP:
            lyubastate = LYUBASTATE_SEARCHTAG_SETUP_WAITRSP;
            if (!lyubastate_search_setup()) {
                lyubastate = LYUBASTATE_READY;
                if (NULL != searchCb) {
                    searchCb(false, NULL);
                }
            }
        break;

        case LYUBASTATE_SEARCHTAG_SETUP_WAITRSP:
        break;

        case LYUBASTATE_SEARCHTAG_SETUP_DONE:
            lyubastate = LYUBASTATE_READY;
            if (NULL == (json = cJSON_Parse((const char *)httpBuf))) {
                Serial.printf("LYUBASTATE_SEARCHTAG_SETUP_DONE: Bad JSON\r\n");
                lyubastate = LYUBASTATE_INIT;
                if (NULL != searchCb) {
                    searchCb(false, NULL);
                }
                break;
            } else {
                cJSON *json_status, *json_content;
                if (!cJSON_IsArray(json) || cJSON_GetArraySize(json) < 1) {
                    Serial.printf("LYUBASTATE_SEARCHTAG_SETUP_DONE: bad types in json statuses\r\n");
                    lyubastate = LYUBASTATE_INIT;
                    if (NULL != searchCb) {
                        searchCb(false, NULL);
                    }
                    cJSON_Delete(json);
                    break;
                }
                if (NULL == (json_status = cJSON_GetArrayItem(json, 0))) {
                    Serial.printf("LYUBASTATE_SEARCHTAG_SETUP_DONE: bad json status\r\n");
                    lyubastate = LYUBASTATE_INIT;
                    if (NULL != searchCb) {
                        searchCb(false, NULL);
                    }
                    cJSON_Delete(json);
                    break;
                }
                if (NULL == (json_content = cJSON_GetObjectItem(json_status, "content"))) {
                    Serial.printf("LYUBASTATE_SEARCHTAG_SETUP_DONE: bad json status\r\n");
                    lyubastate = LYUBASTATE_INIT;
                    if (NULL != searchCb) {
                        searchCb(false, NULL);
                    }
                    cJSON_Delete(json);
                    break;
                }

#ifdef LYUBA_DEBUG
                Serial.printf("content='%s'\r\n", json_content->valuestring);
#endif
                // reuse postBuf, passing it back to user
                strncpy(postBuf, json_content->valuestring, sizeof(postBuf));
                cJSON_Delete(json);
                lyubastate = LYUBASTATE_READY;
                // pass back to user
                if (NULL != searchCb) {
                    searchCb(true, (const char *)postBuf);
                }
            }
        break;
    }
}

void lyuba_toot(const char *user_bearer_access_token, const char *msg, lyuba_toot_cb_t _tootCb) {
    if (NULL == user_bearer_access_token || NULL == msg) {
        if (_tootCb != NULL) {
            _tootCb(false);
        }
        return;
    }

    if (!(lyubastate == LYUBASTATE_READY || lyubastate == LYUBASTATE_INIT)) {
        Serial.println("Can't toot, busy");
        if (_tootCb != NULL) {
            _tootCb(false);
        }
        return;
    } else {
        snprintf(postBuf, sizeof(postBuf), "status=%s", msg);
        strncpy(negotiated_bearer_access_token, user_bearer_access_token, sizeof(negotiated_bearer_access_token));
        tootCb = _tootCb;
        lyubastate = LYUBASTATE_TOOT_SETUP;
    }
}

void lyuba_searchTag(const char *user_bearer_access_token, const char *tag, lyuba_search_cb_t _searchCb) {
    if (NULL == user_bearer_access_token || NULL == tag) {
        if (_searchCb != NULL) {
            _searchCb(false, NULL);
        }
        return;
    }

    if (!(lyubastate == LYUBASTATE_READY || lyubastate == LYUBASTATE_INIT)) {
        Serial.println("Can't search, busy");
        if (_searchCb != NULL) {
            _searchCb(false, NULL);
        }
        return;
    } else {
        snprintf(tagBuf, sizeof(tagBuf), tag);
        strncpy(negotiated_bearer_access_token, user_bearer_access_token, sizeof(negotiated_bearer_access_token));
        searchCb = _searchCb;
        lyubastate = LYUBASTATE_SEARCHTAG_SETUP;
    }
}

void lyuba_authenticate(lyuba_auth_cb_t _authCb) {
    if (lyubastate == LYUBASTATE_INIT || lyubastate == LYUBASTATE_READY) {
        authCb = _authCb;
        lyubastate = LYUBASTATE_APP_SETUP;
    } else {
        Serial.println("Can't authenticate, busy");
        if (NULL != _authCb) {
            _authCb(false, NULL);
        }
    }
}

const char *lyuba_getAuthToken(void) {
    if (0 == preferences_lyuba.getString("auth", negotiated_bearer_access_token, sizeof(negotiated_bearer_access_token))) {
        return NULL;
    } else {
        return negotiated_bearer_access_token;
    }
}


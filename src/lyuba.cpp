#include <Wire.h>
#include <Arduino.h>
#include "shell.h"
#include "ctype.h"
#include "cJSON.h"
#include "userconfig.h"
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
    LYUBASTATE_READY_TO_TOOT,
    LYUBASTATE_READY_TO_TOOT_NOTIFY,
    LYUBASTATE_TOOT_SETUP,
    LYUBASTATE_TOOT_SETUP_WAITRSP,
} web_state_t;

#define MAX_HTTP_OUTPUT_BUFFER 2048 // largest HTTP response we can handle

static volatile lyuba_auth_cb_t authCb = NULL;
static volatile lyuba_toot_cb_t tootCb = NULL;

static web_state_t lyubastate = LYUBASTATE_INIT;

static uint8_t httpBuf[MAX_HTTP_OUTPUT_BUFFER];
static int httpBufCount = 0;

static char client_id[128];
static char client_secret[128];
static char access_token[256];
static char negotiated_bearer_access_token[256];
static char postBuf[1300];  // largest HTTP POST we can make

esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
        default:
        break;
#ifdef LYUBA_DEBUG
        case HTTP_EVENT_ERROR:
            Serial.printf("HTTP_EVENT_ERROR\r\n");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            Serial.printf("HTTP_EVENT_ON_CONNECTED\r\n");
            break;
        case HTTP_EVENT_HEADER_SENT:
            Serial.printf("HTTP_EVENT_HEADER_SENT\r\n");
            break;
        case HTTP_EVENT_ON_HEADER:
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
                    lyubastate = LYUBASTATE_READY_TO_TOOT_NOTIFY;
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
    config.host = MASTODON_HOST;
    config.path = path;
    config.event_handler = _http_event_handler;

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
            return true;
        } else {
            Serial.printf("HTTP status code %d\r\n", esp_http_client_get_status_code(client));
            return false;
        }
    } else {
        Serial.printf("HTTP POST request failed: %s", esp_err_to_name(err));
        return false;
    }
}

void lyuba_init(void) {
    lyubastate = LYUBASTATE_INIT;
    httpBufCount = 0;
    preferences_lyuba.begin("lyuba", false);
}

static bool lyubastate_app_setup(void) {
    snprintf(postBuf, sizeof(postBuf), "client_name=%s&redirect_uris=urn:ietf:wg:oauth:2.0:oob&scopes=write read follow&website=%s", MASTODON_CLIENT_NAME, MASTODON_CLIENT_URL);
    return post_request("/api/v1/apps", postBuf, false);
}

static bool lyubastate_token_setup(void) {
    snprintf(postBuf, sizeof(postBuf), "client_id=%s&client_secret=%s&grant_type=password&username=%s&password=%s&scope=write read follow", client_id, client_secret, MASTODON_USERNAME, MASTODON_PASSWORD);
    return post_request("/oauth/token", postBuf, false);
}

static bool lyubastate_toot_setup(void) {
    // postBuf already contains status=... from lyuba_toot()
    return post_request("/api/v1/statuses", postBuf, true);
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
                lyubastate = LYUBASTATE_READY_TO_TOOT;

                // write new auth data to flash
                preferences_lyuba.putString("auth", negotiated_bearer_access_token);
                // pass back to user
                if (NULL != authCb) {
                    authCb(true, negotiated_bearer_access_token);
                }
            }
        break;

        case LYUBASTATE_READY_TO_TOOT_NOTIFY:
            lyubastate = LYUBASTATE_READY_TO_TOOT;
            if (NULL != tootCb) {
                tootCb(true);
            }
        break;

        case LYUBASTATE_READY_TO_TOOT:
        break;

        case LYUBASTATE_TOOT_SETUP:
            lyubastate = LYUBASTATE_TOOT_SETUP_WAITRSP;
            if (!lyubastate_toot_setup()) {
                lyubastate = LYUBASTATE_READY_TO_TOOT;
                if (NULL != tootCb) {
                    tootCb(false);
                }
            }
        break;

        case LYUBASTATE_TOOT_SETUP_WAITRSP:
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

    if (!(lyubastate == LYUBASTATE_READY_TO_TOOT || lyubastate == LYUBASTATE_INIT)) {
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

void lyuba_authenticate(lyuba_auth_cb_t _authCb) {
    if (lyubastate == LYUBASTATE_INIT || lyubastate == LYUBASTATE_READY_TO_TOOT) {
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


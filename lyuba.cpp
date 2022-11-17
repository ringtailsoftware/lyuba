#include <Wire.h>
#include <Arduino.h>
#include "ctype.h"
#include "cJSON.h"
#include "lyuba.h"
#include "linebuffer.h"
#include "Preferences.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif
#include <esp32/rom/crc.h>
#include "esp_task_wdt.h"

#define MASTODON_CLIENT_NAME "lyuba"
#define MASTODON_CLIENT_URL "http://github.com/ringtailsoftware/lyuba"

//#define LYUBA_DEBUG 1

static Preferences preferences_lyuba;

// struct for userdata containing multiple elements
typedef struct {
    lyuba_auth_cb_t authCb;
    lyuba_t *lyuba;
} lyuba_auth_cb_t_with_lyuba_t;

typedef struct {
    lyuba_toot_cb_t tootCb;
    lyuba_t *lyuba;
} lyuba_toot_cb_t_with_lyuba_t;

typedef struct {
    lyuba_stream_cb_t streamCb;
    lyuba_t *lyuba;
} lyuba_stream_cb_t_with_lyuba_t;


void lyuba_term(lyuba_t *lyuba) {
    if (NULL != lyuba) {
        free(lyuba);
    }
}

lyuba_t *lyuba_init(const char *host, const char *username, const char *password) {
    lyuba_t *lyuba = NULL;

    preferences_lyuba.begin("lyuba", false);

#ifdef LYUBA_DEBUG
    Serial.printf("lyuba_init host=%s\r\n", host);
#endif

    if (HTTPC_ERR_OK != httpc_init()) {
        Serial.printf("lyuba_init httpc_init\r\n");
        return NULL;
    }

    if (NULL == (lyuba = (lyuba_t *)malloc(sizeof(lyuba_t)))) {
        Serial.printf("lyuba_init out of mem\r\n");
        return NULL;
    }
    memset(lyuba, 0x00, sizeof(lyuba_t));

    if (NULL == (lyuba->host = (const char *)strdup(host))) {
        Serial.printf("lyuba_init out of mem host\r\n");
        lyuba_term(lyuba);
        return NULL;
    }

    if (NULL != username) {
        if (NULL == (lyuba->username = (const char *)strdup(username))) {
            Serial.printf("lyuba_init out of mem username\r\n");
            lyuba_term(lyuba);
            return NULL;
        }
    }

    if (NULL != password) {
        if (NULL == (lyuba->password = (const char *)strdup(password))) {
            Serial.printf("lyuba_init out of mem password\r\n");
            lyuba_term(lyuba);
            return NULL;
        }
    }

    return lyuba;
}

static httpc_err_t authTokenPostCb(httpc_err_t err, httpc_req_t *req, int status_code, const char *data, size_t len) {
    cJSON *json;
    lyuba_auth_cb_t_with_lyuba_t *userdata = (lyuba_auth_cb_t_with_lyuba_t *)req->userdata;

#ifdef LYUBA_DEBUG
    Serial.printf("authTokenPostCb! status=%d err=%d len=%d\r\n", status_code, (int)err, (int)len);
    Serial.printf("data='%s'\r\n", data);
#endif

    if (NULL == (json = cJSON_Parse((const char *)data))) {
        Serial.printf("authTokenPostCb: Bad JSON\r\n");
        if (NULL != userdata->authCb) {
            userdata->authCb(false, NULL);
        }
        httpc_close(req);
        return HTTPC_ERR_FAIL;
    } else {
        cJSON *json_access_token;
        if (NULL == (json_access_token = cJSON_GetObjectItem(json, "access_token"))) {
            Serial.printf("authTokenPostCb: missing access_token\r\n");
            if (NULL != userdata->authCb) {
                userdata->authCb(false, NULL);
            }
            cJSON_Delete(json);
            httpc_close(req);
            return HTTPC_ERR_FAIL;
        }
        if (!cJSON_IsString(json_access_token)) {
            Serial.printf("authTokenPostCb: bad types in json\r\n");
            if (NULL != userdata->authCb) {
                userdata->authCb(false, NULL);
            }
            cJSON_Delete(json);
            httpc_close(req);
            return HTTPC_ERR_FAIL;
        }
#ifdef LYUBA_DEBUG
        Serial.printf("access_token='%s'\r\n", json_access_token->valuestring);
#endif
        snprintf(userdata->lyuba->negotiated_bearer_access_token, sizeof(userdata->lyuba->negotiated_bearer_access_token), "Bearer %s", json_access_token->valuestring);
        cJSON_Delete(json);

        // write new auth data to flash
        char key[512];
        snprintf(key, sizeof(key), "%s@%s", userdata->lyuba->username != NULL ? userdata->lyuba->username : "", userdata->lyuba->host);
#ifdef LYUBA_DEBUG
        Serial.printf("prefs key='%s'\r\n", key);
#endif
        uint32_t keyCRC = (~crc32_le((uint32_t)~(0xffffffff), (const uint8_t*)key, strlen(key)))^0xffffffff; // only allows 15 byte keys, so CRC it
        snprintf(key, sizeof(key), "%08X", keyCRC);
#ifdef LYUBA_DEBUG
        Serial.printf("prefs CRC key='%s'\r\n", key);
#endif
        preferences_lyuba.putString(key, userdata->lyuba->negotiated_bearer_access_token);
        // pass back to user
        if (NULL != userdata->authCb) {
#ifdef LYUBA_DEBUG
            Serial.printf("calling authCb true '%s'\r\n", userdata->lyuba->negotiated_bearer_access_token);
#endif
            userdata->authCb(true, userdata->lyuba->negotiated_bearer_access_token);
        }
    }

    return HTTPC_ERR_OK;
}

void lyuba_loop(lyuba_t *lyuba) {
    esp_task_wdt_reset();
    if (lyuba->authGetToken) {
        lyuba->authGetToken = false;

        char postBuf[1024];
        snprintf(postBuf, sizeof(postBuf), "client_id=%s&client_secret=%s&grant_type=password&username=%s&password=%s&scope=write read follow", lyuba->client_id, lyuba->client_secret, lyuba->username, lyuba->password);

        lyuba_auth_cb_t_with_lyuba_t userdata;
        userdata.authCb = lyuba->authCb;
        userdata.lyuba = lyuba;

        if (NULL == httpc_post(lyuba->host, "/oauth/token", NULL, postBuf, 4096, false, authTokenPostCb, (void *)&userdata, sizeof(lyuba_auth_cb_t_with_lyuba_t))) {
            Serial.printf("post err\r\n");
            if (NULL != lyuba->authCb) {
                lyuba->authCb(false, NULL);
            }
        } else {
            Serial.printf("post ok\r\n");
        }
    }

    httpc_loop();
}

const char *lyuba_getAuthToken(lyuba_t *lyuba) {
    char key[512];
    snprintf(key, sizeof(key), "%s@%s", lyuba->username != NULL ? lyuba->username : "", lyuba->host);
    uint32_t keyCRC = (~crc32_le((uint32_t)~(0xffffffff), (const uint8_t*)key, strlen(key)))^0xffffffff;    // only allowed 15 byte keys, so CRC it
    snprintf(key, sizeof(key), "%08X", keyCRC);

#ifdef LYUBA_DEBUG
    Serial.printf("lyuba_getAuthToken key='%s'\r\n", key);
#endif
    if (0 == preferences_lyuba.getString(key, lyuba->negotiated_bearer_access_token, sizeof(lyuba->negotiated_bearer_access_token))) {
        return NULL;
    } else {
        return lyuba->negotiated_bearer_access_token;
    }
}

static httpc_err_t tootPostCb(httpc_err_t err, httpc_req_t *req, int status_code, const char *data, size_t len) {
    lyuba_toot_cb_t_with_lyuba_t *userdata = (lyuba_toot_cb_t_with_lyuba_t *)req->userdata;

    if (status_code == 200) {
        userdata->tootCb(true);
    } else {
        Serial.printf("tootPostCb: status_code=%d\r\n", status_code);
        userdata->tootCb(false);
    }
    return HTTPC_ERR_OK;
}

void lyuba_toot(lyuba_t *lyuba, const char *user_bearer_access_token, const char *msg, lyuba_toot_cb_t _tootCb) {
    size_t msgLen;
    char *postBuf;
    char *prefix = "status=";

    lyuba_toot_cb_t_with_lyuba_t userdata;
    userdata.tootCb = _tootCb;
    userdata.lyuba = lyuba;

    msgLen = strlen(msg);
    if (NULL == (postBuf = (char *)malloc(msgLen + strlen(prefix) + 1))) {
        _tootCb(false);
        return;
    }
    
    snprintf(postBuf, msgLen + strlen(prefix) + 1, "%s%s", prefix, msg);
    if (NULL == httpc_post(lyuba->host, "/api/v1/statuses", user_bearer_access_token, postBuf, 4096, false, tootPostCb, (void *)&userdata, sizeof(lyuba_toot_cb_t_with_lyuba_t))) {
        Serial.printf("post err\r\n");
        _tootCb(false);
    } else {
        Serial.printf("post ok\r\n");
    }

    free(postBuf);
}

// remove HTML tags from a string
static bool stripHTML(const char *in, char *out, size_t outlen) {
    bool inTag = false;
    char c;
    while((c = *in++)) {
        if (!inTag) {
            if (c == '<') {
                inTag = true;
            } else {
                if (outlen-- == 0) {
                    return false;
                }
                *out++ = c;
            }
        } else {
            if (c == '>') {
                inTag = false;
            }
        }
    }
    if (outlen-- == 0) {
        return false;
    }
    *out++ = '\0';
    return true;
}

static httpc_err_t streamLineCb(httpc_err_t err, httpc_req_t *req, int status_code, const char *line, size_t len) {
    lyuba_stream_cb_t_with_lyuba_t *userdata = (lyuba_stream_cb_t_with_lyuba_t *)req->userdata;

    if (err == HTTPC_ERR_OK) {
        if (NULL != line) {
#ifdef LYUBA_DEBUG
//            Serial.printf("streamLineCb '%s'\r\n", line);
#endif
            cJSON *json;
            if (0==strncmp(line, "data:", 5)) {
                if (NULL != (json = cJSON_Parse(line + 5))) {
                    cJSON *json_content, *json_account, *json_account_username;
                    if (NULL != (json_content = cJSON_GetObjectItem(json, "content"))) {
                        if (NULL != (json_account = cJSON_GetObjectItem(json, "account"))) {
                            if (NULL != (json_account_username = cJSON_GetObjectItem(json_account, "username"))) {
#ifdef LYUBA_DEBUG
//                                Serial.printf("username='%s' content='%s'\r\n", json_account_username->valuestring, json_content->valuestring);
#endif
                                if (userdata->streamCb != NULL) {
                                    // strip html from content
                                    char *stripBuf;
                                    size_t stripBufLen = strlen(json_content->valuestring)+1;
                                    if (NULL == (stripBuf = (char*)malloc(stripBufLen))) {
                                        Serial.printf("Out of mem HTML strip");
                                    } else {
                                        stripHTML(json_content->valuestring, stripBuf, stripBufLen);
                                        userdata->streamCb(true, json_account_username->valuestring, stripBuf);
                                        free(stripBuf);
                                    }
                                }
                            }
                        }
                    }
                    cJSON_Delete(json);
                } else {
                    Serial.printf("json parse failure '%s'\r\n", line+5);
                }
            }

        }
    }
    return HTTPC_ERR_OK;
}

void lyuba_close(lyuba_t *lyuba, lyuba_conn_t conn) {
    httpc_req_t *req = conn;    // this works as same type, will break if/when lyuba_conn_t becomes a struct
    httpc_close(req);
}

lyuba_conn_t lyuba_stream(lyuba_t *lyuba, const char *authToken, const char *tag, lyuba_stream_cb_t cb) {
    char path[512];
    lyuba_stream_cb_t_with_lyuba_t userdata;
    httpc_req_t *req;

    userdata.lyuba = lyuba;
    userdata.streamCb = cb;

    snprintf(path, sizeof(path), "/api/v1/streaming/%s", tag);

    if (NULL == (req = httpc_get(lyuba->host, path, authToken, 16384, true, streamLineCb, (void *)&userdata, sizeof(lyuba_stream_cb_t_with_lyuba_t), true))) {
        Serial.printf("stream get err\r\n");
        cb(false, NULL, NULL);
    } else {
        Serial.printf("stream get ok\r\n");
    }
    return req;
}

static httpc_err_t authAppPostCb(httpc_err_t err, httpc_req_t *req, int status_code, const char *data, size_t len) {
    cJSON *json;
    lyuba_auth_cb_t_with_lyuba_t *userdata = (lyuba_auth_cb_t_with_lyuba_t *)req->userdata;

    Serial.printf("authAppPostCb! status=%d err=%d len=%d\r\n", status_code, (int)err, (int)len);
    Serial.printf("data='%s'\r\n", data);

    if (NULL == (json = cJSON_Parse(data))) {
        Serial.printf("authAppPostCb: Bad JSON\r\n");
        if (NULL != userdata->authCb) {
            userdata->authCb(false, NULL);
        }
        return HTTPC_ERR_FAIL;
    } else {
        cJSON *json_client_id, *json_client_secret;
        if (NULL == (json_client_id = cJSON_GetObjectItem(json, "client_id"))) {
            Serial.printf("authAppPostCb: missing client_id\r\n");
            if (NULL != userdata->authCb) {
                userdata->authCb(false, NULL);
            }
            cJSON_Delete(json);
            return HTTPC_ERR_FAIL;
        }
        if (NULL == (json_client_secret = cJSON_GetObjectItem(json, "client_secret"))) {
            Serial.printf("authAppPostCb: missing client_secret\r\n");
            if (NULL != userdata->authCb) {
                userdata->authCb(false, NULL);
            }
            cJSON_Delete(json);
            return HTTPC_ERR_FAIL;
        }
        if (!cJSON_IsString(json_client_id) || !cJSON_IsString(json_client_secret)) {
            Serial.printf("authAppPostCb: bad types in json\r\n");
            if (NULL != userdata->authCb) {
                userdata->authCb(false, NULL);
            }
            cJSON_Delete(json);
            return HTTPC_ERR_FAIL;
        }
#ifdef LYUBA_DEBUG
        Serial.printf("client_id='%s', client_secret='%s'\r\n", json_client_id->valuestring, json_client_secret->valuestring);
#endif

        if (NULL == (userdata->lyuba->client_id = strdup(json_client_id->valuestring))) {
            Serial.printf("Out of mem for client id\r\n");
            cJSON_Delete(json);
            return HTTPC_ERR_FAIL;
        }
        if (NULL == (userdata->lyuba->client_secret = strdup(json_client_secret->valuestring))) {
            Serial.printf("Out of mem for client id\r\n");
            cJSON_Delete(json);
            return HTTPC_ERR_FAIL;
        }

        // kick off next phase
        userdata->lyuba->authGetToken = true;
        userdata->lyuba->authCb = userdata->authCb;

        cJSON_Delete(json);
    }

    return HTTPC_ERR_OK;
}

void lyuba_authenticate(lyuba_t *lyuba, lyuba_auth_cb_t _authCb) {
    char postBuf[1024];

    lyuba_auth_cb_t_with_lyuba_t userdata;
    userdata.authCb = _authCb;
    userdata.lyuba = lyuba;

    snprintf(postBuf, sizeof(postBuf), "client_name=%s&redirect_uris=urn:ietf:wg:oauth:2.0:oob&scopes=write read follow&website=%s", MASTODON_CLIENT_NAME, MASTODON_CLIENT_URL);
    if (NULL == httpc_post(lyuba->host, "/api/v1/apps", NULL, postBuf, 4096, false, authAppPostCb, (void *)&userdata, sizeof(lyuba_auth_cb_t_with_lyuba_t))) {
        Serial.printf("post err\r\n");
        _authCb(false, NULL);
    } else {
        Serial.printf("post ok\r\n");
    }
}



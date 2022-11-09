#include <Wire.h>
#include <Arduino.h>
#include "shell.h"
#include "lyuba.h"

const char *authToken = NULL;

static void auth_callback(bool ok, const char *_authToken) {
    if (ok) {
        Serial.println("Authorisation OK, ready to toot");
        authToken = _authToken;
    } else {
        Serial.println("Authorisation failure");
    }
}

static void toot_callback(bool ok) {
    if (ok) {
        Serial.println("Tooted OK");
    } else {
        Serial.println("Toot failure");
    }
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


static void search_callback(bool ok, const char *content) {
    if (ok) {
        Serial.println("Search OK");
        char rawtext[1024];
        if (!stripHTML(content, rawtext, sizeof(rawtext))) {
            Serial.println("HTML tag strip failed!");
        } else {
            Serial.println(rawtext);
        }
    } else {
        Serial.println("Search failure");
    }
}

static void cmd_getauth(uint8_t argc, const char **argv) {
    authToken = lyuba_getAuthToken();
    if (authToken != NULL) {
        Serial.printf("Retrieved stored token OK: '%s'\r\n", authToken);
    } else {
        Serial.println("No stored auth token");
    }
}

static void cmd_auth(uint8_t argc, const char **argv) {
    lyuba_authenticate(auth_callback);
}

static void cmd_toot(uint8_t argc, const char **argv) {
    if (argc > 0) {
        lyuba_toot(authToken, argv[0], toot_callback);
    }
}

static void cmd_search(uint8_t argc, const char **argv) {
    if (argc > 0) {
        lyuba_searchTag(authToken, argv[0], search_callback);
    }
}

const struct cmdtable_s cmdtab[] = {
    {"getauth", "", cmd_getauth},
    {"auth", "", cmd_auth},
    {"toot", "", cmd_toot},
    {"search", "", cmd_search},
    {NULL, NULL, NULL},
};


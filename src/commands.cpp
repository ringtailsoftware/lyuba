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

const struct cmdtable_s cmdtab[] = {
    {"getauth", "", cmd_getauth},
    {"auth", "", cmd_auth},
    {"toot", "", cmd_toot},
    {NULL, NULL, NULL},
};


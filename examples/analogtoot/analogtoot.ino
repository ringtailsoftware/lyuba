#include <WiFi.h>
#include <lyuba.h>

// Reads analog pin every 10s and toots the value
#define ANALOG_PIN 2    // Analog pin to read

// UPDATE ALL OF THE FOLLOWING FOR YOUR WIFI AND MASTODON ACCOUNT
#define WIFI_SSID "myssid"
#define WIFI_PASSWORD "mypassword"
#define MASTODON_USERNAME "myemail@mydomain.com"
#define MASTODON_PASSWORD "secretpassword"
#define MASTODON_HOST "fosstodon.org"

static const char *authToken = NULL;
static uint32_t lastTootedTime = 0;
static lyuba_t *lyuba = NULL;

static void authCb(bool ok, const char *_authToken) {
    if (ok) {
        Serial.println("Authorisation OK, ready to toot");
        authToken = _authToken;
    } else {
        Serial.println("Authorisation failure");
    }
}

void connectToWiFi(const char * ssid, const char * pwd) {
    Serial.println("Connecting to WiFi network: " + String(ssid));

    WiFi.begin(ssid, pwd);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println();
    Serial.println("WiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
}

void setup(void) {
    Serial.begin(115200);

    // set the resolution to 12 bits (0-4096)
    analogReadResolution(12);

    connectToWiFi(WIFI_SSID, WIFI_PASSWORD);

    lyuba = lyuba_init(MASTODON_HOST, MASTODON_USERNAME, MASTODON_PASSWORD);
    if (lyuba == NULL) {
        Serial.printf("lyuba_init failed!");
        while(1) {
            delay(1000);
        }
    }

    authToken = lyuba_getAuthToken(lyuba);
    if (authToken != NULL) {
        Serial.printf("Retrieved stored token OK: '%s'\r\n", authToken);
    } else {
        lyuba_authenticate(lyuba, authCb);
    }
}

static void tootCb(bool ok) {
    if (ok) {
        Serial.println("Tooted OK");
    } else {
        Serial.println("Toot failure");
    }
}

void loop(void) {
    lyuba_loop(lyuba);
    if (authToken != NULL) {    // have authenticated
        if (millis() > lastTootedTime + 10000) {
            char str[128];
            snprintf(str, sizeof(str), "Analog read %d/4096", analogRead(ANALOG_PIN));
            Serial.printf("Tooting: '%s'\r\n", str);
            lyuba_toot(lyuba, authToken, str, tootCb);
            lastTootedTime = millis();
        }
    }
}


#include <WiFi.h>
#include <lyuba.h>

// Authenticate with Mastodon and toot "Hello world"

// UPDATE ALL OF THE FOLLOWING FOR YOUR WIFI AND MASTODON ACCOUNT
#define WIFI_SSID "myssid"
#define WIFI_PASSWORD "mypassword"
#define MASTODON_USERNAME "myemail@mydomain.com"
#define MASTODON_PASSWORD "secretpassword"
#define MASTODON_HOST "fosstodon.org"

static const char *authToken = NULL;
static bool haveTooted = false;

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
    connectToWiFi(WIFI_SSID, WIFI_PASSWORD);

    lyuba_init(MASTODON_HOST, MASTODON_USERNAME, MASTODON_PASSWORD);
    authToken = lyuba_getAuthToken();
    if (authToken != NULL) {
        Serial.printf("Retrieved stored token OK: '%s'\r\n", authToken);
    } else {
        lyuba_authenticate(authCb);
    }
}

static void tootCb(bool ok) {
    if (ok) {
        Serial.println("Tooted OK");
        haveTooted = true;
    } else {
        Serial.println("Toot failure");
    }
}

void loop(void) {
    lyuba_loop();
    if (!haveTooted) {  // have not yet tooted
        if (authToken != NULL) {    // have authenticated
            lyuba_toot(authToken, "Hello world", tootCb);
        }
    }
}


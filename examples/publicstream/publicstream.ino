#include <WiFi.h>
#include <lyuba.h>

// UPDATE ALL OF THE FOLLOWING FOR YOUR WIFI AND MASTODON ACCOUNT

#define WIFI_SSID "myssid"
#define WIFI_PASSWORD "mypassword"
#define MASTODON_USERNAME "myemail@mydomain.com"
#define MASTODON_PASSWORD "secretpassword"
#define MASTODON_HOST "fosstodon.org"


static const char *authToken = NULL;
static lyuba_t *lyuba = NULL;
bool startedStreaming = false;

static void authCb(bool ok, const char *_authToken) {
    if (ok) {
        Serial.println("Authorisation OK, ready to toot");
        authToken = _authToken;
    } else {
        Serial.println("Authorisation failure");
    }
}

static void stream_callback(bool ok, const char *username, const char *content) {
    if (ok) {
        Serial.printf("stream_callback (%s) : %s\r\n", username, content );
    } else {
        Serial.println("Streaming failed/stopped");
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

void loop(void) {
    lyuba_loop(lyuba);

    if (authToken != NULL) {
        if (!startedStreaming) {
            lyuba_stream(lyuba, authToken, "public", stream_callback);
            startedStreaming = true;
        }
    }
}


#include <WiFi.h>
#include <lyuba.h>

// UPDATE ALL OF THE FOLLOWING FOR YOUR WIFI AND MASTODON ACCOUNT
#define WIFI_SSID "myssid"
#define WIFI_PASSWORD "mypassword"
// Pre-arranged access token, from "Preferences" -> "Development" in Mastodon. Add "Bearer " in front of the access token
#define MASTODON_TOKEN "Bearer abc123"
#define MASTODON_HOST "fosstodon.org"


static lyuba_t *lyuba = NULL;
static bool haveTooted = false;

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

    lyuba = lyuba_init(MASTODON_HOST, NULL, NULL);
    if (lyuba == NULL) {
        Serial.printf("lyuba_init failed!");
        while(1) {
            delay(1000);
        }
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
    lyuba_loop(lyuba);

    if (!haveTooted) {  // have not yet tooted
        lyuba_toot(lyuba, MASTODON_TOKEN, "Hello world", tootCb);
        haveTooted = true;
    }
}


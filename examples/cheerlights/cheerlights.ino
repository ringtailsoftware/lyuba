#include <WiFi.h>
#include <lyuba.h>
#include <Adafruit_NeoPixel.h>

#define NUMPIXELS 1     // number of neopixels connected 
#define PIXEL_PIN 21    // neopixel DIN pin

// UPDATE ALL OF THE FOLLOWING FOR YOUR WIFI AND MASTODON ACCOUNT
#define WIFI_SSID "myssid"
#define WIFI_PASSWORD "mypassword"
#define MASTODON_USERNAME "myemail@mydomain.com"
#define MASTODON_PASSWORD "secretpassword"
#define MASTODON_HOST "fosstodon.org"

Adafruit_NeoPixel pixels(NUMPIXELS, PIXEL_PIN, NEO_GRB + NEO_KHZ800);

typedef struct {
    const char *name;
    uint8_t r, g, b;
} cheerlights_colour_t;

cheerlights_colour_t cheerlights_colours[] = {
    {"red", 255, 0, 0},
    {"pink", 255, 192, 203},
    {"green", 0, 255, 0},
    {"blue", 0, 0, 255},
    {"cyan", 0, 255, 255},
    {"white", 255, 255, 255},
    {"warmwhite", 255, 223, 223},
    {"oldlace", 255, 223, 223},
    {"purple", 128, 0, 128},
    {"magenta", 255, 0, 255},
    {"yellow", 255, 255, 0},
    {"orange", 255, 165, 0},
    {NULL, 0, 0, 0}
};

uint32_t lastSearchTime = 0;
static const char *authToken = NULL;

void setColour(const char *strContainingColourName) {
    cheerlights_colour_t *col = cheerlights_colours;
    while(NULL != col->name) {
        if (0!=strstr(strContainingColourName, col->name)) {
            int i;
            Serial.printf("Setting red=%d, green=%d, blue=%d\r\n", col->r, col->g, col->b);
            for (i=0;i<NUMPIXELS;i++) {
                pixels.setPixelColor(i, col->r, col->g, col->b);
            }
            pixels.show();
            pixels.show();  // https://github.com/adafruit/Adafruit_NeoPixel/issues/159#issuecomment-578382457
            return;
        }
        col++;
    }
    Serial.printf("Unknown colour '%s'\r\n", strContainingColourName);
}

static void authCb(bool ok, const char *_authToken) {
    if (ok) {
        Serial.println("Authorisation OK, ready to toot");
        authToken = _authToken;
    } else {
        Serial.println("Authorisation failure");
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

static void cheerlights_callback(bool ok, const char *content) {
    if (ok) {
        Serial.println("Search OK");
        char rawtext[128];
        if (!stripHTML(content, rawtext, sizeof(rawtext))) {
            Serial.println("HTML tag strip failed!");
        } else {
            Serial.println(rawtext);
            setColour(rawtext);
        }
    } else {
        Serial.println("Search failure");
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

    pixels.begin();
    pixels.clear();

    lyuba_init(MASTODON_HOST, MASTODON_USERNAME, MASTODON_PASSWORD);
    authToken = lyuba_getAuthToken();
    if (authToken != NULL) {
        Serial.printf("Retrieved stored token OK: '%s'\r\n", authToken);
    } else {
        lyuba_authenticate(authCb);
    }
}

void loop(void) {
    lyuba_loop();

    if (millis() > lastSearchTime + 5000) {
        lastSearchTime = millis();
        if (authToken != NULL) {
            lyuba_searchTag(authToken, "cheerlights", cheerlights_callback);
        }
    }
}

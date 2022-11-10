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
// UPDATE THE SECURITY CERTIFICATE BELOW TO MATCH YOUR MASTODON_HOST
// To fetch the certificate, run:
//    openssl s_client -showcerts -connect fosstodon.org:443 </dev/null 2>/dev/null|openssl x509 -outform PEM | sed -e 's/^/"/' | sed -e 's/$/\\n"/' | sed -e '$ ! s/$/ \\/'
#define MASTODON_PEM \
"-----BEGIN CERTIFICATE-----\n"\
"MIIDYDCCAkigAwIBAgIJAPYntPZTGaMwMA0GCSqGSIb3DQEBCwUAMEUxCzAJBgNV\n"\
"BAYTAlBUMRMwEQYDVQQIDApTb21lLVN0YXRlMSEwHwYDVQQKDBhJbnRlcm5ldCBX\n"\
"aWRnaXRzIFB0eSBMdGQwHhcNMjExMTAxMTExNTIyWhcNMjIxMTAxMTExNTIyWjBF\n"\
"MQswCQYDVQQGEwJQVDETMBEGA1UECAwKU29tZS1TdGF0ZTEhMB8GA1UECgwYSW50\n"\
"ZXJuZXQgV2lkZ2l0cyBQdHkgTHRkMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIB\n"\
"CgKCAQEAxh6zRZ18qAXiqEikc/GA5LfQYLJY7JgMG3jdKe/u1okBTPIVfotgCSC3\n"\
"2kZe1LOxRJARbqiyCRcck0rJAdHTG0sIrpy5M/SU2QbhdXabVxrIWYq5vhJiaW4+\n"\
"+9KV7818d9MDrY1QjlMMi7mXpdew5X3L+tqze1xNCuHnSee7mVmB4eLda/di2tuT\n"\
"DGo2BiZERWecm/sAyOWLnjYQohGWbh4ZqstRgP6IFZ8KuBIhegMccu2Rm1ZnHsxg\n"\
"y0ReVDP80PWfmgsDEkUN9jGNlEXCTD88y3lrDmyX7ZYdy2vWHAG1pJFhw4cW8pm1\n"\
"2/+AhvaWiIlwJ7QZMdLC/n1iqJGLcwIDAQABo1MwUTAdBgNVHQ4EFgQUsrVv9RiF\n"\
"vHqdRLOX9Dz5GjC40aAwHwYDVR0jBBgwFoAUsrVv9RiFvHqdRLOX9Dz5GjC40aAw\n"\
"DwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAQEAEJtOPe/naN3jvnCc\n"\
"NpkXYP5ctSbMw0ww0CmC5msk+ezlVwXqbUeZXRMR+XBNq9cBhPbnWKEXFmLhGMHP\n"\
"9oqVmaknXZ8LZhuGPqRmFvXQMz00PBGebHCqmp2RgGrGRfBJYoVcY85UgWZ/bCa/\n"\
"LLOfkAZnNG7OrVicxvOxKem34AcN51qmKqDqyRqhREFqXEYBQQXX771USI1sUJtU\n"\
"CZ57iSDF+QQiWXdjth6aFmdcYNCuu8X8TYhyvTr18KJuNYrPt31RPJaQMFdFl19E\n"\
"mi6J6h4TgzfQD4RyTyEkh+ZtpDkTDpuET8coKDTt4yb7U7hExGQ0W0UXs3vZxTBq\n"\
"zXq8+Q==\n"\
"-----END CERTIFICATE-----\n"

static const char *authToken = NULL;
static uint32_t lastTootedTime = 0;

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

    lyuba_init(MASTODON_HOST, MASTODON_PEM, MASTODON_USERNAME, MASTODON_PASSWORD);
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
    } else {
        Serial.println("Toot failure");
    }
}

void loop(void) {
    lyuba_loop();
    if (authToken != NULL) {    // have authenticated
        if (millis() > lastTootedTime + 10000) {
            char str[128];
            snprintf(str, sizeof(str), "Analog read %d/4096", analogRead(ANALOG_PIN));
            Serial.printf("Tooting: '%s'\r\n", str);
            lyuba_toot(authToken, str, tootCb);
            lastTootedTime = millis();
        }
    }
}


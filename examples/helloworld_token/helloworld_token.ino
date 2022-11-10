#include <WiFi.h>
#include <lyuba.h>


// UPDATE ALL OF THE FOLLOWING FOR YOUR WIFI AND MASTODON ACCOUNT
#define WIFI_SSID "myssid"
#define WIFI_PASSWORD "mypassword"
// Pre-arranged access token, from "Preferences" -> "Development" in Mastodon. Add "Bearer " in front of the access token
#define MASTODON_TOKEN "Bearer abc123"
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

    lyuba_init(MASTODON_HOST, MASTODON_PEM, NULL, NULL);
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
        lyuba_toot(MASTODON_TOKEN, "Hello world", tootCb);
    }
}


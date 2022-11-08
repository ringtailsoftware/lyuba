# Lyuba, Mastodon tooter for ESP32

Lyuba is a minimal library for tooting (posting) to the Mastodon social network.

## Getting started

Edit src/userconfig.h to configure WiFi and Mastodon credentials

    #define WIFI_SSID "myssid"
    #define WIFI_PASSWORD "wifipassword"

    #define MASTODON_HOST "fosstodon.org"
    #define MASTODON_USERNAME "my@email.com"
    #define MASTODON_PASSWORD "mastodonpassword"

Build the code and flash it to an attached ESP32

    pio run --target upload

Alternatively, build the code using docker by running

    make

The binaries will be produced in `artefacts/` and can be flashed manually to the ESP32.

To interact, connect a serial terminal, 8N1@115200bps.

Setup Lyuba as an authenticated app, type:

    auth

You should see

    Authorisation OK, ready to toot

Send a toot, type:

    toot Hello World!

## Storage of access token

The `auth` command sets Lyuba authentication up by registering an app with your Mastodon account. To avoid re-registering every time, the credentials (access token) are stored in the ESP32 flash using the `Preferences` module. After rebooting, the credentials can be retrieved and re-used. Reboot, then type:

    getauth

You should see:

    Retrieved stored token OK: 'Bearer....'

You can then,

    toot A new message

## API

See `lyuba.h` for the API prototypes.

To initialise the library, call:

    lyuba_init();

In your sketch's main loop, regularly call:

    lyuba_loop();

To start the authentication process, call:

    lyuba_authenticate(authCb);

Where `authCb` is a callback function. If `NULL` is used, authentication will occur but no callback will be made to the sketch:

    void authCb(bool ok, const char *authToken) {}
   
On successful authentication, `ok`=`true` and `authToken` will contain the authentication credentials. On failure `ok`=`false`

To read back a saved authentication token:

    const char *authToken = lyuba_getAuthToken();

`lyuba_getAuthToken()` will return NULL if no token as been setup. If this is the case, call `lyuba_authenticate()`.

To toot, call:

    lyuba_toot(authToken, "Hello World!", tootCb);

Where `tootCb` is a callback function. If `NULL` is used, tooting will occur but no callback will be made to the sketch:

    void tootCb(bool ok)

On successful tooting, `ok`=`true`. On failure `ok`=`false`

## Notes

 - Lyuba should be considered insecure. No certificate checks are performed and your Mastodon password is baked into your firmware
 - Lyuba is not thread safe, it expects to be used in an asynchronous manner by a single task

## License

Lyuba is MIT licensed.

See other source files for their respective licenses

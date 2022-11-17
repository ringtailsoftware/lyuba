# Lyuba ESP32 Mastodon Library

Arduino library for communicating with the Mastodon social network.

https://fosstodon.org/web/@lyuba

Toby Jaffey @tobyjaffey@mastodon.me.uk

Lyuba supports sending toots and polling for the most recent status matching a hashtag.

After downloading, rename folder to 'lyuba' and install in Arduino Libraries folder. Restart Arduino IDE, then open `File->Sketchbook->Library->lyuba->helloworld` sketch.

Compatibility notes: ESP32 only (ESP32 variants should work but are untesed)

## Installation

 1. Download the repository as a zip file
 2. In the Arduino IDE, navigate to Sketch -> Include Library -> Add .ZIP Library

## Example sketches

 - `helloworld`, authenticate using username and password, send a single toot
 - `helloworld_token`, authenticate using an access token
 - `analogtoot`, authenticate using username and password, send a toot every 10s with an analog sensor reading
 - `publicstream`, authenticate using username and password, monitor public stream and print every toot
 - `cheerlights`, authenticate using username and password, monitor "#cheerlights" hashtag, parse colour name and show it on a single neopixel

## Configuring sketches

All sketches must be setup for your WiFi network and Mastodon account. At the top of each sketch, update as follows:

 - `WIFI_SSID` your WiFi ssid
 - `WIFI_PASSWORD` your WiFi password
 - `MASTODON_USERNAME` your Mastodon username (username@domain.tld)
 - `MASTODON_PASSWORD` your Mastodon password
 - `MASTODON_HOST` the hostname of the Mastodon instance you use, e.g. "fosstodon.org" or "mastodon.social"

If you are using token authentication instead of username and password, `MASTODON_TOKEN` should have the string "Bearer " followed by your token. To generate a token go to your Mastodon instance web site, click "Preferences" -> "Development" -> "New application". Give the application a name, everything else is optional. Click "Submit", then click on your new application in the list. Read off the "Your access token", prefix it with "Bearer " and place in `MASTODON_TOKEN`.

## Storage of access token

The `lyuba_auth()` call sets Lyuba authentication up by registering an app with your Mastodon account. To avoid re-registering every time, the credentials (access token) are stored in the ESP32 flash using the `Preferences` module. After rebooting, the credentials can be retrieved and re-used with `lyuba_getAuthToken()`.

## API

See `lyuba.h` for the API prototypes.

To initialise the library, call:

    lyuba_t *myLyuba = lyuba_init(const char *host, const char *username, const char *password);

`host` is the Mastodon server to connect to, `username` and `password` are the credentials and may be `NULL` if token authenticated is to be used. Returns a `lyuba_t*` which must be passed to other API calls.

In your sketch's main loop, regularly call:

    lyuba_loop(myLyuba);

To start the authentication process, call:

    lyuba_authenticate(myLyuba, authCb);

Where `authCb` is a callback function. If `NULL` is used, authentication will occur but no callback will be made to the sketch:

    void authCb(bool ok, const char *authToken) {}
   
On successful authentication, `ok`=`true` and `authToken` will contain the authentication credentials. On failure `ok`=`false`

To read back a saved authentication token:

    const char *authToken = lyuba_getAuthToken(myLyuba);

`lyuba_getAuthToken()` will return NULL if no token as been setup. If this is the case, call `lyuba_authenticate()`.

To toot, call:

    lyuba_toot(myLyuba, authToken, "Hello World!", tootCb);

Where `tootCb` is a callback function. If `NULL` is used, tooting will occur but no callback will be made to the sketch:

    void tootCb(bool ok)

On successful tooting, `ok`=`true`. On failure `ok`=`false`

To stream all public toots, call:

    lyuba_conn_t *myConn = lyuba_stream(myLyuba, authToken, "public", streamCb);

To stream a hashtag, call:

    lyuba_conn_t *myConn = lyuba_stream(myLyuba, authToken, "hashtag?tag=cheerlights", streamCb);

For more details of available streams, see https://docs.joinmastodon.org/methods/timelines/streaming/

Where `streamCb` is a callback function which is passed each toot, pre-stripped of HTML tags:

    void streamCb(bool ok, const char *username, const char *content) { }

To close a stream, call:

    lyuba_close(myConn);

## Notes

 - Lyuba should be considered insecure. Your Mastodon password is baked into your firmware unless token authentication is used
 - Lyuba is not thread safe, it expects to be used in an asynchronous manner by a single task
 - Running mulitple streams and requests concurrently is not well tested

## License

Lyuba is MIT licensed

cJSON Copyright (c) 2009-2017 Dave Gamble and cJSON contributors

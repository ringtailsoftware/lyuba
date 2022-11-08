#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include "esp_system.h"
#include "esp_wifi.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <esp_task_wdt.h>
#include "shell.h"
#include "lyuba.h"
#include "userconfig.h"

void setup() {
    Serial.begin(115200);
    shell_init();
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    WiFi.setTxPower(WIFI_POWER_7dBm);

    while (WiFi.status() != WL_CONNECTED) {
        Serial.print('.');
        delay(1000);
    }

    Serial.println(WiFi.localIP());
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 1);

    lyuba_init();
}

void loop() {
    shell_loop();
    lyuba_loop();
}



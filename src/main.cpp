#include <WiFi.h>
#include <ESPmDNS.h>
#include <SPIFFS.h>
#include "cpp/httpd.h"
#include "cpp/websocketd.h"
#include "cpp/ota.h"
#include "cpp/status.h"

#ifndef WIFI_SSID
#define WIFI_SSID "FallbackSSID"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "FallbackPassword"
#endif

void setup(){
  Serial.begin(115200);
  delay(100);
  Serial.println("Booting PizzaOrder ESP32...");
  Serial.printf("ssid %s; password: %s\n", WIFI_SSID, WIFI_PASSWORD);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int tries=0; while (WiFi.status()!=WL_CONNECTED && tries<60){ delay(500); Serial.print("."); tries++; }
  Serial.println();
  Serial.print("IP: "); Serial.println(WiFi.localIP());
  WiFi.setHostname("pizzaorder");
  if (MDNS.begin("pizzaorder")) Serial.println("mDNS ok");

  if (!SPIFFS.begin(true)){
    Serial.println("SPIFFS mount failed");
  }
  // load pizzas/settings
  randomSeed(esp_random());
  load_state_from_fs();

  setup_httpd();
  setup_websocketd();
  setup_ota();

  Serial.print("Manager-Link: http://"); Serial.print(WiFi.localIP()); Serial.print("/manage.html?token="); Serial.println(g_state.cfg.manager_token);
}

void loop(){
  loop_httpd();
  loop_websocketd();
  loop_ota();
}

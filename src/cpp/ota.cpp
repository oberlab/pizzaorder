#include <ArduinoOTA.h>
#include <WiFi.h>

void setup_ota(){
  ArduinoOTA.setHostname("pizzaorder");
  ArduinoOTA.begin();
}

void loop_ota(){ ArduinoOTA.handle(); }


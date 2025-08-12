#include <WebServer.h>
#include <SPIFFS.h>
#include <Arduino.h>
#include "httpd.h"
#include "status.h"
#include <ArduinoJson.h>

WebServer httpd(80);

static String contentTypeFor(const String &filename){
  if (filename.endsWith(".html")) return "text/html";
  if (filename.endsWith(".css")) return "text/css";
  if (filename.endsWith(".js")) return "application/javascript";
  if (filename.endsWith(".png")) return "image/png";
  if (filename.endsWith(".ico")) return "image/x-icon";
  if (filename.endsWith(".json")) return "application/json";
  return "text/plain";
}

static bool servePath(String path){
  if (path.endsWith("/")) path += "index.html";
  String full = "/httpd" + path; // serve from /httpd in SPIFFS
  if (!SPIFFS.exists(full)) return false;
  File f = SPIFFS.open(full, "r");
  if (!f) return false;
  httpd.streamFile(f, contentTypeFor(path));
  f.close();
  return true;
}

void setup_httpd(){
  httpd.on("/api/state", [](){
    // minimal primer state for clients
    DynamicJsonDocument doc(8192);
    JsonObject out = doc.createNestedObject("data");
    JsonArray arr = out.createNestedArray("pizzas");
    for (auto &p: g_state.pizzas){
      JsonObject o = arr.createNestedObject();
      o["id"]=p.id; o["name"]=p.name; o["price"]=p.price;
      JsonArray ing = o.createNestedArray("ingredients"); for (auto &s: p.ingredients) ing.add(s);
    }
    out["ordering_open"] = g_state.cfg.ordering_open;
    if (g_state.cfg.paypal_email.length()) out["paypal_email"] = g_state.cfg.paypal_email;
    doc["type"] = "state";
    String s; serializeJson(doc, s);
    httpd.send(200, "application/json", s);
  });
  httpd.on("/api/pizzas/export", [](){
    // export current pizzas JSON
    httpd.sendHeader("Content-Disposition","attachment; filename=\"pizze.json\"");
    File f = SPIFFS.open("/pizze.json","r");
    if (f){ httpd.streamFile(f, "application/json"); f.close(); }
    else httpd.send(200,"application/json","{\"pizzas\":[]}");
  });

  httpd.onNotFound([](){
    if (!servePath(httpd.uri())){
      httpd.send(404, "text/plain", "Not Found");
    }
  });
  httpd.begin();
}

void loop_httpd(){ httpd.handleClient(); }

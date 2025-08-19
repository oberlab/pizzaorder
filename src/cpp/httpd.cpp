#include <WebServer.h>
#include <SPIFFS.h>
#include <Arduino.h>
#include <WiFi.h>
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
  // Collect a few headers for richer diagnostics on 404s
  static const char* HDRS[] = {
    "Host", "User-Agent", "Accept", "Accept-Language", "Accept-Encoding",
    "Origin", "Referer", "Connection", "Upgrade",
    "Sec-WebSocket-Version", "Sec-WebSocket-Key"
  };
  httpd.collectHeaders(HDRS, sizeof(HDRS)/sizeof(HDRS[0]));

  // Light-weight handlers to avoid noisy 404 logs from common browser requests
  httpd.on("/favicon.ico", [](){ httpd.send(204); });
  httpd.on("/ws", [](){ httpd.send(426, "text/plain", "WebSocket: bitte Port 81 verwenden"); });
  httpd.on("/manage.html", [](){
    // mark manager as logged in when token matches
    bool ok = false;
    if (httpd.hasArg("token")){
      String tok = httpd.arg("token");
      if (tok == g_state.cfg.manager_token) ok = true;
    }
    if (ok){
      g_manager_logged_in = true;
    }
    if (!servePath("/manage.html")){
      httpd.send(404, "text/plain", "Not Found");
    }
  });
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
    // Detailed diagnostics for requests ohne Handler/Datei
    String uri = httpd.uri();
    HTTPMethod m = httpd.method();
    const char* mname = (m==HTTP_GET?"GET": m==HTTP_POST?"POST": m==HTTP_PUT?"PUT": m==HTTP_DELETE?"DELETE": m==HTTP_PATCH?"PATCH": m==HTTP_OPTIONS?"OPTIONS":"OTHER");
    IPAddress rip = httpd.client().remoteIP();
    Serial.printf("HTTP 404: %s %s from %s\n", mname, uri.c_str(), rip.toString().c_str());
    int nArgs = httpd.args();
    for (int i=0;i<nArgs;i++){
      Serial.printf("  arg[%s]=%s\n", httpd.argName(i).c_str(), httpd.arg(i).c_str());
    }
    int nHdr = httpd.headers();
    for (int i=0;i<nHdr;i++){
      Serial.printf("  hdr[%s]=%s\n", httpd.headerName(i).c_str(), httpd.header(i).c_str());
    }
    bool served = servePath(uri);
    if (!served){
      httpd.send(404, "text/plain", "Not Found");
    }
  });
  httpd.begin();
}

void loop_httpd(){ httpd.handleClient(); }

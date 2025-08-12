#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <map>
#include "websocketd.h"
#include "status.h"

static WebSocketsServer ws(81);
static std::map<uint8_t, String> clientSid; // ws client -> sid
static std::map<uint8_t, String> clientName; // ws client -> name

static String genSid(){ return String("s")+String(random(0xFFFFFF), HEX); }

static void state_json_for(uint8_t num, DynamicJsonDocument &doc){
  JsonObject d = doc.createNestedObject("data");
  // pizzas
  JsonArray arr = d.createNestedArray("pizzas");
  for (auto &p: g_state.pizzas){
    JsonObject o = arr.createNestedObject();
    o["id"] = p.id; o["name"] = p.name; o["price"] = p.price;
    JsonArray ing = o.createNestedArray("ingredients"); for (auto &s: p.ingredients) ing.add(s);
  }
  d["ordering_open"] = g_state.cfg.ordering_open;
  if (g_state.cfg.paypal_email.length()) d["paypal_email"] = g_state.cfg.paypal_email;
  if (g_state.cfg.manager_token.length()) d["manager_link"] = String("/manage.html?token=")+g_state.cfg.manager_token;

  // orders (manager view) - for now include for everyone to keep it simple
  JsonObject orders = d.createNestedObject("orders");
  for (auto &okv : g_state.orders){
    JsonObject oo = orders.createNestedObject(okv.first);
    oo["name"] = okv.second.name;
    JsonObject items = oo.createNestedObject("items");
    for (auto &ikv : okv.second.items) items[ikv.first] = ikv.second;
    oo["paid"] = okv.second.paid;
    if (okv.second.method.length()) oo["method"] = okv.second.method;
  }

  String sid = clientSid[num];
  if (sid.length()){
    Order *o=nullptr;
    auto it = g_state.orders.find(sid);
    if (it!=g_state.orders.end()) o=&(it->second);
    JsonObject my = d.createNestedObject("my");
    my["name"] = clientName[num];
    JsonObject items = my.createNestedObject("items");
    if (o){ for (auto &kv: o->items) items[kv.first] = kv.second; my["paid"]=o->paid; my["method"]=o->method; }
    else { my["paid"]=false; my["method"]=nullptr; }
  }
}

void ws_broadcast_state(){
  for (auto &kv: clientSid){
    DynamicJsonDocument doc(16384);
    doc["type"] = "state";
    state_json_for(kv.first, doc);
    String s; serializeJson(doc, s);
    ws.sendTXT(kv.first, s);
  }
}

static void handle_msg(uint8_t num, JsonObject msg){
  const char* type = msg["type"] | "";
  if (!strcmp(type, "hello")){
    // establish sid for this client if not yet present
    if (!clientSid[num].length()) clientSid[num] = genSid();
    DynamicJsonDocument auth(256);
    auth["type"] = "auth"; auth["ok"] = true;
    String s; serializeJson(auth, s); ws.sendTXT(num, s);
    DynamicJsonDocument doc(8192); doc["type"]="state"; state_json_for(num, doc); s=""; serializeJson(doc,s); ws.sendTXT(num,s);
    return;
  }
  if (!strcmp(type, "set_user")){
    clientName[num] = (const char*) (msg["name"] | "");
    // update order name if exists
    auto it = g_state.orders.find(clientSid[num]);
    if (it!=g_state.orders.end()) it->second.name = clientName[num];
    ws_broadcast_state(); return;
  }
  if (!strcmp(type, "add_item")){
    if (!g_state.cfg.ordering_open) return;
    String pid = (const char*)(msg["pizza_id"]|"");
    int delta = msg["delta"].is<int>()? msg["delta"].as<int>():0;
    if (!pid.length() || delta==0) return;
    String sid = clientSid[num]; if (!sid.length()) clientSid[num]=sid=genSid();
    Order &o = g_state.orders[sid];
    if (o.name.length()==0) o.name = clientName[num];
    int cur = o.items[pid];
    cur += delta; if (cur<0) cur=0;
    if (cur==0) o.items.erase(pid); else o.items[pid]=cur;
    o.paid=false; o.method="";
    if (o.items.empty()) g_state.orders.erase(sid);
    ws_broadcast_state(); return;
  }
  if (!strcmp(type, "payment_started")){
    String sid = clientSid[num]; auto it = g_state.orders.find(sid); if (it!=g_state.orders.end()) { it->second.paid=false; it->second.method="paypal_started"; }
    ws_broadcast_state(); return;
  }
  if (!strcmp(type, "mark_paid")){
    // for demo we accept from any client; in production restrict to manager
    String sid = msg.containsKey("sid") ? msg["sid"].as<String>() : clientSid[num];
    if (!sid.length()) sid = clientSid[num];
    String method = msg.containsKey("method") ? msg["method"].as<String>() : String("cash");
    auto it = g_state.orders.find(sid);
    if (it!=g_state.orders.end()) {
      it->second.paid=true; it->second.method=method;
    }
    ws_broadcast_state(); return;
  }
  if (!strcmp(type, "mark_unpaid")){
    String sid = msg.containsKey("sid") ? msg["sid"].as<String>() : clientSid[num];
    if (!sid.length()) sid = clientSid[num];
    auto it = g_state.orders.find(sid);
    if (it!=g_state.orders.end()) {
      it->second.paid=false; it->second.method="";
    }
    ws_broadcast_state(); return;
  }
  if (!strcmp(type, "set_pizzas")){
    // TODO auth; accept for now
    g_state.pizzas.clear();
    if (msg["pizzas"].is<JsonArray>()){
      for (JsonObject o : msg["pizzas"].as<JsonArray>()){
        Pizza p; p.id = (const char*)(o["id"]|""); p.name=(const char*)(o["name"]|""); p.price = o["price"].is<float>()? o["price"].as<float>():0.0f;
        if (o["ingredients"].is<JsonArray>()) for (JsonVariant v: o["ingredients"].as<JsonArray>()) p.ingredients.push_back(String((const char*)v));
        if (!p.id.length()) p.id = String("p")+String(random(10000));
        if (!p.name.length()) p.name = p.id;
        g_state.pizzas.push_back(p);
      }
    }
    save_pizzas_to_fs();
    ws_broadcast_state(); return;
  }
  if (!strcmp(type, "set_paypal")){
    g_state.cfg.paypal_email = (const char*)(msg["email"]|"");
    save_settings_to_fs();
    ws_broadcast_state(); return;
  }
  if (!strcmp(type, "close_orders")){
    g_state.cfg.ordering_open = false; save_settings_to_fs(); ws_broadcast_state(); return;
  }
  if (!strcmp(type, "open_orders")){
    g_state.cfg.ordering_open = true; save_settings_to_fs(); ws_broadcast_state(); return;
  }
}

static void onWsEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length){
  switch(type){
    case WStype_CONNECTED:{
      if (!clientSid[num].length()) clientSid[num] = genSid();
      DynamicJsonDocument doc(1024); doc["type"]="auth"; doc["ok"]=true; String s; serializeJson(doc,s); ws.sendTXT(num,s);
      DynamicJsonDocument st(8192); st["type"]="state"; state_json_for(num, st); s=""; serializeJson(st,s); ws.sendTXT(num,s);
    }break;
    case WStype_TEXT:{
      DynamicJsonDocument doc(8192);
      auto err = deserializeJson(doc, payload, length);
      if (!err && doc.is<JsonObject>()) handle_msg(num, doc.as<JsonObject>());
    }break;
    case WStype_DISCONNECTED:{
      clientSid.erase(num); clientName.erase(num);
    }break;
    default:break;
  }
}

void setup_websocketd(){
  ws.begin();
  ws.onEvent(onWsEvent);
}

void loop_websocketd(){ ws.loop(); }

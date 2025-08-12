#include <Arduino.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include "status.h"

State g_state;

static const char* PIZZAS_PATH = "/pizze.json";
static const char* SETTINGS_PATH = "/settings.json"; // kept for compatibility; ignored for runtime

String random_token(size_t n){
  static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  String out; out.reserve(n);
  for(size_t i=0;i<n;i++){ out += alphabet[random(0,64)]; }
  return out;
}

static void default_pizzas(){
  g_state.pizzas.clear();
  auto add=[&](const char* id,const char* name, std::initializer_list<const char*> ings, float price){
    Pizza p; p.id=id; p.name=name; p.price=price; for(auto s:ings) p.ingredients.push_back(String(s)); g_state.pizzas.push_back(p);
  };
  add("marg","Margherita",{"Tomate","Mozzarella","Basilikum"},7.5);
  add("salami","Salami",{"Tomate","Mozzarella","Salami"},8.5);
  add("funghi","Funghi",{"Tomate","Mozzarella","Champignons"},8.5);
  add("hawaii","Hawaii",{"Tomate","Mozzarella","Schinken","Ananas"},9.0);
  add("tonno","Tonno",{"Tomate","Mozzarella","Thunfisch","Zwiebel"},9.5);
  add("quattro","Quattro Formaggi",{"Mozzarella","Gorgonzola","Parmesan","Emmentaler"},10.5);
  add("diavola","Diavola",{"Tomate","Mozzarella","Scharfe Salami","Peperoni"},9.5);
  add("veggie","Vegetariana",{"Tomate","Mozzarella","Paprika","Oliven","Zwiebel"},9.0);
}

void load_state_from_fs(){
  // Settings are non-persistent: we may read initial defaults from file,
  // but we never persist changes and we always generate a fresh token.
  g_state.cfg.ordering_open = true;
  g_state.cfg.paypal_email = "";
  if (SPIFFS.exists(SETTINGS_PATH)){
    File f = SPIFFS.open(SETTINGS_PATH, "r");
    if (f){
      DynamicJsonDocument doc(2048);
      if (!deserializeJson(doc, f)){
        if (doc["ordering_open"].is<bool>()) g_state.cfg.ordering_open = doc["ordering_open"].as<bool>();
        if (doc.containsKey("paypal_email")) g_state.cfg.paypal_email = (const char*)doc["paypal_email"];
      }
      f.close();
    }
  }
  // Always new token per boot
  g_state.cfg.manager_token = random_token();
  // pizzas
  if (SPIFFS.exists(PIZZAS_PATH)){
    File f = SPIFFS.open(PIZZAS_PATH, "r");
    if (f){
      DynamicJsonDocument doc(8192);
      if (!deserializeJson(doc, f)){
        g_state.pizzas.clear();
        JsonArray arr = (doc.containsKey("pizzas") && doc["pizzas"].is<JsonArray>()) ? doc["pizzas"].as<JsonArray>() : doc.as<JsonArray>();
        for (JsonObject o : arr){
          Pizza p; p.id = (const char*)(o["id"]|""); p.name=(const char*)(o["name"]|""); p.price = o["price"].is<float>()? o["price"].as<float>():0.0f;
          if (o["ingredients"].is<JsonArray>()){
            for (JsonVariant v : o["ingredients"].as<JsonArray>()) p.ingredients.push_back(String((const char*)v));
          }
          if (p.id.length()==0) p.id = String("p")+String(random(10000));
          if (p.name.length()==0) p.name = p.id;
          g_state.pizzas.push_back(p);
        }
      }
      f.close();
    }
  } else {
    default_pizzas();
  }
}

void save_pizzas_to_fs(){
  DynamicJsonDocument doc(8192);
  JsonArray arr = doc.createNestedArray("pizzas");
  for (auto &p: g_state.pizzas){
    JsonObject o = arr.createNestedObject();
    o["id"] = p.id;
    o["name"] = p.name;
    o["price"] = p.price;
    JsonArray ing = o.createNestedArray("ingredients");
    for (auto &s : p.ingredients) ing.add(s);
  }
  File f = SPIFFS.open(PIZZAS_PATH, FILE_WRITE);
  if (f){ serializeJsonPretty(doc, f); f.close(); }
}

void save_settings_to_fs(){
  // Intentionally no-op: settings are ephemeral/not persisted
}

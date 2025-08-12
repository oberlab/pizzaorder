#pragma once
#include <Arduino.h>
#include <vector>
#include <map>

struct Pizza {
  String id;
  String name;
  std::vector<String> ingredients;
  float price = 0.0f;
};

struct Order {
  String name;
  std::map<String,int> items; // pid -> qty
  bool paid = false;
  String method; // "cash" | "paypal" | "paypal_started" | ""
};

struct Settings {
  bool ordering_open = true;
  String paypal_email; // or "me:handle"
  String manager_token; // generated at boot
};

struct State {
  std::vector<Pizza> pizzas;
  std::map<String, Order> orders; // keyed by sid
  Settings cfg;
};

extern State g_state;

void load_state_from_fs();
void save_pizzas_to_fs();
void save_settings_to_fs();

// utilities
String random_token(size_t n = 22);


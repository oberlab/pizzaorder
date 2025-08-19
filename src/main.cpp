#include <WiFi.h>
#include <ESPmDNS.h>
#include <SPIFFS.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_SH110X.h>
#include <qrcode.h>
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

// OLED 1.3" I2C: viele Module nutzen SH1106 statt SSD1306
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

enum class DisplayType { NONE, SH1106, SSD1306 };
static DisplayType g_disp_type = DisplayType::NONE;
static Adafruit_SH1106G g_disp_sh1106(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
static Adafruit_SSD1306 g_disp_ssd1306(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

static void disp_clear(){
  if (g_disp_type == DisplayType::SH1106) g_disp_sh1106.clearDisplay();
  else if (g_disp_type == DisplayType::SSD1306) g_disp_ssd1306.clearDisplay();
}
static void disp_display(){
  if (g_disp_type == DisplayType::SH1106) g_disp_sh1106.display();
  else if (g_disp_type == DisplayType::SSD1306) g_disp_ssd1306.display();
}
static void disp_setTextSize(uint8_t s){
  if (g_disp_type == DisplayType::SH1106) g_disp_sh1106.setTextSize(s);
  else if (g_disp_type == DisplayType::SSD1306) g_disp_ssd1306.setTextSize(s);
}
static void disp_setTextColor(uint16_t c){
  if (g_disp_type == DisplayType::SH1106) g_disp_sh1106.setTextColor(c);
  else if (g_disp_type == DisplayType::SSD1306) g_disp_ssd1306.setTextColor(c);
}
static void disp_setCursor(int16_t x, int16_t y){
  if (g_disp_type == DisplayType::SH1106) g_disp_sh1106.setCursor(x, y);
  else if (g_disp_type == DisplayType::SSD1306) g_disp_ssd1306.setCursor(x, y);
}
static size_t disp_println(const String &s){
  if (g_disp_type == DisplayType::SH1106) return g_disp_sh1106.println(s);
  else if (g_disp_type == DisplayType::SSD1306) return g_disp_ssd1306.println(s);
  return 0;
}
static void disp_fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color){
  if (g_disp_type == DisplayType::SH1106) g_disp_sh1106.fillRect(x, y, w, h, color);
  else if (g_disp_type == DisplayType::SSD1306) g_disp_ssd1306.fillRect(x, y, w, h, color);
}

static void draw_qr(const char* text){
  if (!text || !*text) return;
  QRCode qrcode;
  // Version 5 passt i. d. R. für einen kurzen Link; bei Bedarf erhöhen
  const uint8_t version = 5;
  uint8_t qrcodeData[qrcode_getBufferSize(version)];
  if (qrcode_initText(&qrcode, qrcodeData, version, ECC_MEDIUM, text) != 0){
    return;
  }
  disp_clear();
  int margin = 2;
  int scaleX = (SCREEN_WIDTH - 2*margin) / qrcode.size;
  int scaleY = (SCREEN_HEIGHT - 2*margin) / qrcode.size;
  int scale = (scaleX < scaleY ? scaleX : scaleY);
  if (scale < 1) scale = 1;
  int qrSize = qrcode.size * scale;
  int x0 = (SCREEN_WIDTH - qrSize) / 2;
  int y0 = (SCREEN_HEIGHT - qrSize) / 2;
  for (uint8_t y = 0; y < qrcode.size; y++){
    for (uint8_t x = 0; x < qrcode.size; x++){
      if (qrcode_getModule(&qrcode, x, y)){
        disp_fillRect(x0 + x*scale, y0 + y*scale, scale, scale, SSD1306_WHITE);
      }
    }
  }
  disp_display();
}

void setup(){
  Serial.begin(115200);
  delay(100);
  Serial.println("Booting PizzaOrder ESP32...");
  Serial.printf("ssid %s; password: %s\n", WIFI_SSID, WIFI_PASSWORD);
  // OLED init (I2C SDA=21, SCL=22, Adresse meist 0x3C)
  Wire.begin();
  Wire.setClock(400000); // optional schneller I2C
  // Versuche zuerst SH1106 (häufig bei 1.3")
  if (g_disp_sh1106.begin(0x3C, true)){
    g_disp_type = DisplayType::SH1106;
  } else if (g_disp_sh1106.begin(0x3D, true)){
    g_disp_type = DisplayType::SH1106;
  } else if (g_disp_ssd1306.begin(SSD1306_SWITCHCAPVCC, 0x3C)){
    g_disp_type = DisplayType::SSD1306;
  } else if (g_disp_ssd1306.begin(SSD1306_SWITCHCAPVCC, 0x3D)){
    g_disp_type = DisplayType::SSD1306;
  } else {
    Serial.println("OLED init failed (SH1106/SSD1306)");
  }
  if (g_disp_type != DisplayType::NONE){
    disp_clear();
    disp_setTextSize(1);
    disp_setTextColor(SSD1306_WHITE);
    disp_setCursor(0, 0);
    disp_println("PizzaOrder booting...");
    disp_display();
  }
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
  // QR-Code des Manager-Links auf OLED anzeigen
  {
    String url = String("http://") + WiFi.localIP().toString() + "/manage.html?token=" + g_state.cfg.manager_token;
    draw_qr(url.c_str());
  }
}

static bool s_shown_user_qr = false;

void loop(){
  loop_httpd();
  loop_websocketd();
  loop_ota();
  // Wenn Manager eingeloggt: normalen Link anzeigen (nur einmal umschalten)
  if (g_manager_logged_in && !s_shown_user_qr){
    s_shown_user_qr = true;
    String url = String("http://") + WiFi.localIP().toString() + "/";
    draw_qr(url.c_str());
  }
}

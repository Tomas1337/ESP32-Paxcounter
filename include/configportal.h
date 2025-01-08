#ifndef _CONFIGPORTAL_H
#define _CONFIGPORTAL_H

#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "globals.h"
// #include "wifiscan.h"  // For wifi_sniffer_stop()

// Extern declarations
extern bool config_portal_active;

// Function declarations
void init_config_portal();
void start_config_portal();
void handle_config_portal();
bool is_config_portal_active();
void save_config_to_spiffs(const char* ssid, const char* password, const char* mqtt_server, uint16_t mqtt_port);
bool load_config_from_spiffs();

// Constants
#define CONFIG_AP_SSID "ESP32-Config"
#define CONFIG_AP_PASSWORD "12345678"
#define CONFIG_PORTAL_TIMEOUT 300000  // 5 minutes timeout
#define CONFIG_FILE "/config.json"
#define BUTTON_PRESS_THRESHOLD 5      // Number of presses to trigger config mode
#define BUTTON_PRESS_TIMEOUT 3000     // Time window for button presses in ms

#endif 
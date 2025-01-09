#ifndef CONFIGPORTAL_H
#define CONFIGPORTAL_H

#include <WiFi.h>
#include <WebServer.h>
#include "esp_spiffs.h"

// Configuration portal settings
#define CONFIG_AP_SSID "ESP32-Paxcounter"
#define CONFIG_AP_PASSWORD "configure123"
#define CONFIG_PORTAL_TIMEOUT 300  // 5 minutes timeout
#define CONFIG_FILE_PATH "/spiffs/config.json"

// Function declarations
void init_config_portal();
void start_config_portal();
void handle_config_portal();
bool is_config_portal_active();
void save_wifi_config(const char* ssid, const char* password, const char* mqtt_server, 
                    int mqtt_port, const char* mqtt_topic);

// External variables
extern volatile bool config_portal_active;

#endif // CONFIGPORTAL_H 
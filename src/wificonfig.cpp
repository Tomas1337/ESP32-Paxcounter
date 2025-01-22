#include "wificonfig.h"
#include "globals.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>

// Global WiFi configuration instance
WiFiConfig wifiConfig;

static const char* const WIFI_TAG = "WIFI_CONFIG";

bool loadWiFiConfig() {
    if (!SPIFFS.exists("/config.json")) {
        ESP_LOGI(WIFI_TAG, "No config file found, using defaults");
        return false;
    }

    File configFile = SPIFFS.open("/config.json", "r");
    if (!configFile) {
        ESP_LOGE(WIFI_TAG, "Failed to open config file");
        return false;
    }

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, configFile);
    configFile.close();

    if (error) {
        ESP_LOGE(WIFI_TAG, "Failed to parse config file: %s", error.c_str());
        return false;
    }

    // Update WiFi configuration
    wifiConfig.ssid = doc["wifi_ssid"].as<String>();
    wifiConfig.password = doc["wifi_password"].as<String>();
    wifiConfig.mqtt_server = doc["mqtt_server"].as<String>();
    wifiConfig.mqtt_topic = doc["mqtt_topic"].as<String>();
    wifiConfig.mqtt_port = doc["mqtt_port"] | MQTT_PORT;  // Use default if not specified

    ESP_LOGI(WIFI_TAG, "Loaded WiFi config - SSID: %s, MQTT Server: %s", 
            wifiConfig.ssid.c_str(), wifiConfig.mqtt_server.c_str());
    return true;
}

bool saveWiFiConfig() {
    StaticJsonDocument<512> doc;
    
    doc["wifi_ssid"] = wifiConfig.ssid;
    doc["wifi_password"] = wifiConfig.password;
    doc["mqtt_server"] = wifiConfig.mqtt_server;
    doc["mqtt_topic"] = wifiConfig.mqtt_topic;
    doc["mqtt_port"] = wifiConfig.mqtt_port;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
        ESP_LOGE(WIFI_TAG, "Failed to open config file for writing");
        return false;
    }

    if (serializeJson(doc, configFile) == 0) {
        ESP_LOGE(WIFI_TAG, "Failed to write config file");
        configFile.close();
        return false;
    }

    configFile.close();
    ESP_LOGI(WIFI_TAG, "Saved WiFi configuration to SPIFFS");
    return true;
} 
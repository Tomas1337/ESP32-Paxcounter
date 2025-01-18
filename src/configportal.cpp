#include "configportal.h"
#include "wificonfig.h"
#include "esp_log.h"
#include <WiFiManager.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

static const char* CONFIG_TAG = "CONFIG_PORTAL";
bool portalActive = false;

WiFiManager wifiManager;

// Custom parameters for MQTT configuration
WiFiManagerParameter custom_mqtt_server("mqtt_server", "MQTT Server", "", 40);
WiFiManagerParameter custom_mqtt_port("mqtt_port", "MQTT Port", "1883", 6);
WiFiManagerParameter custom_mqtt_topic("mqtt_topic", "MQTT Topic", "paxcounter", 40);

// Callback when configuration is saved
void saveConfigCallback() {
    ESP_LOGI(CONFIG_TAG, "Configuration needs to be saved");
    
    // Read custom parameters
    String mqtt_server = custom_mqtt_server.getValue();
    String mqtt_port = custom_mqtt_port.getValue();
    String mqtt_topic = custom_mqtt_topic.getValue();
    
    // Save configuration to file
    File configFile = SPIFFS.open("/config.json", "w");
    if (configFile) {
        StaticJsonDocument<512> doc;
        doc["wifi_ssid"] = WiFi.SSID();
        doc["wifi_password"] = WiFi.psk();
        doc["mqtt_server"] = mqtt_server;
        doc["mqtt_topic"] = mqtt_topic;
        doc["mqtt_port"] = mqtt_port.toInt();
        
        serializeJson(doc, configFile);
        configFile.close();
        ESP_LOGI(CONFIG_TAG, "Configuration saved successfully");
    } else {
        ESP_LOGE(CONFIG_TAG, "Failed to open config file for writing");
    }
}

void startConfigPortal() {
    if (portalActive) {
        ESP_LOGW(CONFIG_TAG, "Config portal already active");
        return;
    }

    libpax_counter_stop();  // Stop sniffing during configuration
    ESP_LOGI(CONFIG_TAG, "Starting configuration portal");
    
    // Set config save notify callback
    wifiManager.setSaveConfigCallback(saveConfigCallback);
    
    // Add custom parameters
    wifiManager.addParameter(&custom_mqtt_server);
    wifiManager.addParameter(&custom_mqtt_port);
    wifiManager.addParameter(&custom_mqtt_topic);
    
    // Load saved values if they exist
    if (SPIFFS.exists("/config.json")) {
        File configFile = SPIFFS.open("/config.json", "r");
        if (configFile) {
            StaticJsonDocument<512> doc;
            DeserializationError error = deserializeJson(doc, configFile);
            
            if (!error) {
                // Set default values for custom parameters
                custom_mqtt_server.setValue(doc["mqtt_server"].as<const char*>(), 40);
                custom_mqtt_port.setValue(String(doc["mqtt_port"].as<int>()).c_str(), 6);
                custom_mqtt_topic.setValue(doc["mqtt_topic"].as<const char*>(), 40);
            }
            configFile.close();
        }
    }
    
    // Set portal timeout (optional, 180 seconds)
    wifiManager.setConfigPortalTimeout(180);
    
    // Set AP name
    String apName = "ESP32-Pax-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    
    // Start config portal
    portalActive = true;
    if (!wifiManager.startConfigPortal(apName.c_str())) {
        ESP_LOGI(CONFIG_TAG, "Failed to connect or timeout");
        delay(3000);
        ESP.restart();
    }
    
    ESP_LOGI(CONFIG_TAG, "Connected to WiFi");
    portalActive = false;
} 
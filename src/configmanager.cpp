#include "configmanager.h"
#include <ArduinoJson.h>
#include <SPIFFS.h>

static const char* CONFIG_FILE = "/config.json";
static const char* TAG = "CONFIG";

void saveConfiguration() {
    File file = SPIFFS.open(CONFIG_FILE, "w");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open config file for writing");
        return;
    }

    StaticJsonDocument<512> doc;
    doc["adrmode"] = cfg.adrmode;
    doc["loradr"] = cfg.loradr;
    doc["txpower"] = cfg.txpower;
    doc["countermode"] = cfg.countermode;

    if (serializeJson(doc, file) == 0) {
        ESP_LOGE(TAG, "Failed to write config to file");
    }
    file.close();
}

void loadConfiguration() {
    if (!SPIFFS.exists(CONFIG_FILE)) {
        ESP_LOGI(TAG, "No configuration file found, using defaults");
        initialize_config();
        return;
    }

    File file = SPIFFS.open(CONFIG_FILE, "r");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open config file for reading");
        initialize_config();
        return;
    }

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        ESP_LOGE(TAG, "Failed to parse config file");
        initialize_config();
        return;
    }

    cfg.adrmode = doc["adrmode"] | 0;
    cfg.loradr = doc["loradr"] | 5;
    cfg.txpower = doc["txpower"] | 14;
    cfg.countermode = doc["countermode"] | 0;
}

void eraseConfiguration() {
    if (SPIFFS.exists(CONFIG_FILE)) {
        SPIFFS.remove(CONFIG_FILE);
        ESP_LOGI(TAG, "Configuration erased");
    }
    initialize_config();
}

void initialize_config() {
    cfg.adrmode = 0;
    cfg.loradr = 5;
    cfg.txpower = 14;
    cfg.countermode = 0;
    ESP_LOGI(TAG, "Configuration initialized with defaults");
}
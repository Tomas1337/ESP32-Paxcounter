#include <Arduino.h>
#include "globals.h"
#include "button.h"
#include "configmanager.h"
#include "configportal.h"
#include <SPIFFS.h>

// Global variables
configData_t cfg;
const uint8_t PROGMEM version[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const char* MAIN_TAG = "MAIN";

void setup() {
    Serial.begin(115200);
    ESP_LOGI(MAIN_TAG, "Starting ESP32 Configuration Portal");
    
    // Initialize SPIFFS
    if(!SPIFFS.begin(true)) {
        ESP_LOGE(MAIN_TAG, "SPIFFS Mount Failed");
        return;
    }
    
    // Initialize button
    button_init();
    
    // Load configuration
    loadConfiguration();
}

void loop() {
    // Handle button press
    handle_button_press();
    delay(10); // Small delay to prevent watchdog issues
}
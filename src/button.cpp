#ifdef HAS_BUTTON

#include "globals.h"
#include "button.h"
#include "configportal.h"
#include "mqtthandler.h"

static const char* BUTTON_TAG = "BUTTON";
static volatile uint8_t button_press_count = 0;
static volatile unsigned long last_button_press = 0;
static Ticker buttonTimer;
static bool last_button_state = HIGH;  // Track previous button state

#define BUTTON_PRESS_TIMEOUT 3000  // 3 seconds timeout between presses
#define BUTTON_PRESS_THRESHOLD 5   // Number of presses needed to trigger config portal
#define BUTTON_CHECK_INTERVAL 0.05  // Check button every 50ms

// Simple button press handler - no IRAM usage
void handle_button_press() {
    unsigned long current_time = millis();
    
    // Reset counter if timeout
    if (current_time - last_button_press > BUTTON_PRESS_TIMEOUT) {
        button_press_count = 0;
    }
    
    button_press_count++;
    last_button_press = current_time;
    
    ESP_LOGI(BUTTON_TAG, "Button press %d/%d", button_press_count, BUTTON_PRESS_THRESHOLD);
    
    if (button_press_count >= BUTTON_PRESS_THRESHOLD) {
        ESP_LOGI(BUTTON_TAG, "Starting config portal");
        
        // Disconnect from any existing WiFi connection
        if (WiFi.status() == WL_CONNECTED) {
            ESP_LOGI(BUTTON_TAG, "Disconnecting from WiFi for config portal");
            WiFi.disconnect(true);
            delay(100); // Give WiFi time to disconnect
        }
        
        // Set flag and start portal
        config_portal_active = true;
        button_press_count = 0;
        
        // Directly start the portal
        start_config_portal();
    }
}

// Timer callback to check button state
void check_button() {
    bool current_state = digitalRead(HAS_BUTTON);
    
    // Only trigger on transition from HIGH to LOW (button press)
    if (current_state == LOW && last_button_state == HIGH) {
        ESP_LOGD(BUTTON_TAG, "Button press detected");
        handle_button_press();
    }
    
    last_button_state = current_state;
}

void button_init(void) {
    ESP_LOGI(BUTTON_TAG, "Starting button on GPIO %d", HAS_BUTTON);
    pinMode(HAS_BUTTON, INPUT_PULLUP);
    last_button_state = HIGH;  // Initialize last state
    
    // Start timer to check button state every 50ms
    buttonTimer.attach(BUTTON_CHECK_INTERVAL, check_button);
    ESP_LOGI(BUTTON_TAG, "Button timer started with interval %.2fs", BUTTON_CHECK_INTERVAL);
}

#endif
#include "button.h"
#include "configportal.h"
#include "esp_log.h"

static const char* BUTTON_TAG = "BUTTON";
static volatile uint8_t buttonPressCount = 0;
static volatile unsigned long lastButtonPress = 0;
static const int BUTTON_PIN = 0; // Boot button
static const int DEBOUNCE_TIME = 200; // milliseconds

void IRAM_ATTR button_isr() {
    unsigned long currentMillis = millis();
    if (currentMillis - lastButtonPress > DEBOUNCE_TIME) {
        buttonPressCount = (buttonPressCount + 1) % 255; // Prevent overflow
        lastButtonPress = currentMillis;
    }
}

void button_init() {
    pinMode(BUTTON_PIN, INPUT);
    attachInterrupt(BUTTON_PIN, button_isr, FALLING);
    ESP_LOGI(BUTTON_TAG, "Button initialized on pin %d", BUTTON_PIN);
}

void handle_button_press() {
    if (buttonPressCount >= 5) {
        ESP_LOGI(BUTTON_TAG, "Starting configuration portal");
        startConfigPortal();
        buttonPressCount = 0;
    }
}

uint8_t get_button_press_count() {
    return buttonPressCount;
}

void reset_button_press_count() {
    buttonPressCount = 0;
}
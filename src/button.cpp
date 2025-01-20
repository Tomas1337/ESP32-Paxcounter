#ifdef HAS_BUTTON

#include "globals.h"
#include "button.h"
#include "configportal.h"
#include "esp_log.h"

static volatile uint8_t buttonPressCount = 0;
static volatile uint32_t lastButtonPress = 0;
static const char* const BUTTON_TAG = "BUTTON";

// Keep only the ISR in IRAM
void IRAM_ATTR handle_button_press() {
    uint32_t now = millis();
    if ((now - lastButtonPress) > 300) {
        lastButtonPress = now;
        buttonPressCount++;
        
        // Check if we've reached 5 presses within the time window
        if (buttonPressCount == 5) {
            // Set maintenance mode - this will be handled by cyclic task
            RTC_runmode = RUNMODE_MAINTENANCE;
            buttonPressCount = 0;  // Reset the count
        }
    }
}

// Regular functions don't need IRAM
int get_button_press_count() {
    return buttonPressCount;
}

void reset_button_press_count() {
    // Reset count after a timeout period
    uint32_t now = millis();
    if ((now - lastButtonPress) > 5000) { // 5 second timeout
        buttonPressCount = 0;
    }
}

void button_init(void) {
    pinMode(HAS_BUTTON, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(HAS_BUTTON), handle_button_press, FALLING);
    ESP_LOGI(BUTTON_TAG, "Button handler initialized on pin %d", HAS_BUTTON);
}

#endif

#ifdef HAS_BUTTON

#include "globals.h"
#include "button.h"
#include "configportal.h"
#include "esp_log.h"

static volatile uint8_t buttonPressCount = 0;
static volatile uint32_t lastButtonPress = 0;

// Keep only the ISR in IRAM
void handle_button_press() {
    uint32_t now = millis();
    if ((now - lastButtonPress) > 300) {
        lastButtonPress = now;
        buttonPressCount++;
    }
}

// Regular functions don't need IRAM
uint8_t get_button_press_count() {
    return buttonPressCount;
}

void reset_button_press_count() {
    buttonPressCount = 0;
}

void button_init(void) {
    pinMode(HAS_BUTTON, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(HAS_BUTTON), handle_button_press, FALLING);
}

#endif
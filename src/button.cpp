#ifdef HAS_BUTTON

#include "globals.h"
#include "button.h"
#include "configportal.h"
#include "esp_log.h"

static volatile uint8_t buttonPressCount = 0;
static volatile uint32_t lastButtonPress = 0;

// Minimal ISR in IRAM
void IRAM_ATTR handle_button_press() {
    uint32_t now = millis();
    if ((now - lastButtonPress) > 300) {
        lastButtonPress = now;
        buttonPressCount++;
    }
}

// Move these to flash
uint8_t __attribute__((noinline)) get_button_press_count() {
    return buttonPressCount;
}

void __attribute__((noinline)) reset_button_press_count() {
    buttonPressCount = 0;
}

void __attribute__((noinline)) button_init(void) {
    pinMode(HAS_BUTTON, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(HAS_BUTTON), handle_button_press, FALLING);
}

#endif
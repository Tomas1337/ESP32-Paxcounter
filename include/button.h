#ifndef _BUTTON_H
#define _BUTTON_H

#include "globals.h"

// Function declarations
void button_init(void);
void handle_button_press(void);
uint8_t get_button_press_count(void);
void reset_button_press_count(void);

#endif // _BUTTON_H
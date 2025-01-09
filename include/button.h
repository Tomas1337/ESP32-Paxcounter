#ifndef _BUTTON_H
#define _BUTTON_H

#include "globals.h"

#ifndef BUTTON_ACTIVEHIGH
#define BUTTON_ACTIVEHIGH 0
#endif

#ifndef BUTTON_PULLUP
#define BUTTON_PULLUP 1
#endif

void button_init(void);
void handle_button_press(void);

#endif
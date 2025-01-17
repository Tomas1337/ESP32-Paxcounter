#ifndef _BUTTON_H
#define _BUTTON_H

#ifdef HAS_BUTTON
    void button_init(void);
    void IRAM_ATTR handle_button_press(void);
    uint8_t get_button_press_count(void);
    void reset_button_press_count(void);
#endif

#endif
#ifndef _BUTTON_H
#define _BUTTON_H

#define HAS_BUTTON 0
#ifdef HAS_BUTTON
    void button_init(void);
    void IRAM_ATTR handle_button_press(void);
    int get_button_press_count(void);
    void reset_button_press_count(void);
#endif
#endif
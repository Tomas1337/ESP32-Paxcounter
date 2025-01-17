#ifndef _GLOBALS_H
#define _GLOBALS_H

#include <Arduino.h>
#include <WiFi.h>
#include "esp_log.h"

// Basic configuration structure
typedef struct {
    uint8_t adrmode;          // 0=disabled, 1=enabled
    uint8_t loradr;           // 0-15, lora datarate
    uint8_t txpower;          // 2-15, lora tx power
    uint8_t countermode;      // 0=cyclic, 1=cumulative, 2=cyclic confirmed
} configData_t;

extern configData_t cfg;

#endif // _GLOBALS_H

#ifndef WIFI_HOOKS_H
#define WIFI_HOOKS_H

#include <stdint.h>

// Declare the hook function without extern "C" since it's used by C++ code in libpax
void wifi_packet_handler_hook(uint8_t* mac, int8_t rssi);

#endif // WIFI_HOOKS_H 
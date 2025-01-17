#include "wifi_hooks.h"
#include "esp_log.h"

// This needs to be visible to libpax
void __attribute__((weak)) wifi_packet_handler_hook(uint8_t* mac, int8_t rssi) {
    if (!mac) return;
    
#if (VERBOSE)
    ESP_LOGD("WIFI_HOOK", "WiFi packet: MAC=%02x:%02x:%02x:%02x:%02x:%02x RSSI=%d",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], rssi);
#endif
} 
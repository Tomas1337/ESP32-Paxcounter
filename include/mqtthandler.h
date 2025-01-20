#ifndef _MQTTHANDLER_H
#define _MQTTHANDLER_H

#include <WiFi.h>
#include <PubSubClient.h>
#include "globals.h"
#include "esp_log.h"
#include "esp_spiffs.h"

// Structure for probe data
struct ProbeData {
    uint32_t pax;
    uint32_t wifi_count;
    uint32_t ble_count;
    uint32_t timestamp;
};

// Function declarations
void pax_mqtt_init(void);
void pax_mqtt_loop(void);
void pax_mqtt_enqueue(uint16_t pax, uint16_t wifi_count, uint16_t ble_count);
void pax_mqtt_enqueue_device(const uint8_t* mac, int8_t rssi, bool is_wifi);
void pax_mqtt_send_data();
void pax_mqtt_send_devices();
// void pax_mqtt_disconnect(void);

extern volatile bool shouldSendMQTT;
extern TaskHandle_t paxMqttTaskHandle;

#endif
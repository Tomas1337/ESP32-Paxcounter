#include "mqtthandler.h"
#include "wificonfig.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "libpax_helpers.h"
#include "globals.h"
#include "configportal.h"
#include "button.h"
#include <time.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include "paxcounter.conf"

// All strings in flash
static const char* const MQTT_TAG PROGMEM = "MQTT_HANDLER";
static const char* const CONFIG_AP_NAME PROGMEM = "ESP32-Config";

// Message types for both count and device data
struct CountMessage {
    uint16_t pax;
    uint16_t wifi_count;
    uint16_t ble_count;
    uint32_t timestamp;
} __attribute__((packed));

struct DeviceMessage {
    uint8_t mac[6];
    int8_t rssi;
    bool is_wifi;
    uint32_t timestamp;
} __attribute__((packed));

// Static allocations
TaskHandle_t paxMqttTaskHandle = NULL;
static SemaphoreHandle_t wifiSemaphore = NULL;
static QueueHandle_t countQueue = NULL;
static QueueHandle_t deviceQueue = NULL;
static wifi_mode_t wifiState = WIFI_MODE_NULL;
static WiFiClient wifiClient;
static PubSubClient mqttClient(wifiClient);
static StaticJsonDocument<200> jsonDoc;
static char jsonBuffer[200];

#define COUNT_QUEUE_SIZE 50
#define DEVICE_QUEUE_SIZE 70



void pax_mqtt_enqueue_device(const uint8_t* mac, int8_t rssi, bool is_wifi) {
    if (!mac) {
        ESP_LOGE(MQTT_TAG, "Invalid MAC address pointer");
        return;
    }

    DeviceMessage msg;
    memcpy(msg.mac, mac, 6);
    msg.rssi = rssi;
    msg.is_wifi = is_wifi;
    msg.timestamp = millis();

    if (xQueueSend(deviceQueue, &msg, 0) != pdTRUE) {
        ESP_LOGD(MQTT_TAG, "Device queue full, dropping packet and triggering send task");
        if (paxMqttTaskHandle) {
            xTaskNotify(paxMqttTaskHandle, SENDCYCLE_IRQ, eSetBits);
        }
    } else {
        ESP_LOGD(MQTT_TAG, "Device queued: Type=%s", is_wifi ? "WiFi" : "BLE");
    }
}

void pax_mqtt_enqueue(uint16_t pax_count, uint16_t wifi_count, uint16_t ble_count) {
    CountMessage msg = {
        .pax = pax_count,
        .wifi_count = wifi_count,
        .ble_count = ble_count,
        .timestamp = millis()
    };
    
    if (xQueueSend(countQueue, &msg, 0) != pdTRUE) {
        ESP_LOGW(MQTT_TAG, "Count queue full (%d messages)", COUNT_QUEUE_SIZE);
    } else {
        ESP_LOGD(MQTT_TAG, "Count data queued: pax=%d, wifi=%d, ble=%d", 
                pax_count, wifi_count, ble_count);
    }
}

bool pax_mqtt_connect() {
    if (WiFi.status() != WL_CONNECTED) {
        ESP_LOGE(MQTT_TAG, "Cannot connect to MQTT - WiFi not connected");
        return false;
    }
    
    if (!mqttClient.connected()) {
        ESP_LOGD(MQTT_TAG, "Connecting to MQTT broker...");
        mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
        mqttClient.setKeepAlive(MQTT_KEEPALIVE);
        
        if (mqttClient.connect(MQTT_CLIENTNAME)) {
            ESP_LOGI(MQTT_TAG, "Connected to MQTT broker");
            return true;
        } else {
            ESP_LOGE(MQTT_TAG, "Failed to connect to MQTT broker");
            return false;
        }
    }
    return true;
}

// Function to send all queued messages
void send_queued_messages() {
    ESP_LOGI(MQTT_TAG, "Starting to send queued messages...");
    
    // Connect to WiFi first
    WiFi.mode(WIFI_STA);
    ESP_LOGI(MQTT_TAG, "Connecting to WiFi...");
    WiFi.begin(wifiConfig.ssid.c_str(), wifiConfig.password.c_str());
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        vTaskDelay(pdMS_TO_TICKS(500));
        ESP_LOGI(MQTT_TAG, "Attempting to connect to WiFi... (%d)", attempts + 1);
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        ESP_LOGI(MQTT_TAG, "WiFi connected successfully!");
        
        if (pax_mqtt_connect()) {
            // Send count data
            CountMessage count_msg;
            while (xQueueReceive(countQueue, &count_msg, 0) == pdTRUE) {
                StaticJsonDocument<256> doc;
                JsonObject data = doc.createNestedObject("data");
                data["pax"] = count_msg.pax;
                data["wifi"] = count_msg.wifi_count;
                data["ble"] = count_msg.ble_count;
                time_t now;
                time(&now);
                data["timestamp"] = now;
                
                char buffer[256];
                serializeJson(doc, buffer);
                
                if (mqttClient.publish(wifiConfig.mqtt_topic.c_str(), buffer)) {
                    ESP_LOGD(MQTT_TAG, "Successfully sent count data");
                } else {
                    ESP_LOGE(MQTT_TAG, "Failed to send count data");
                }
                
                // Small delay between messages
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            
            // Send device data
            DeviceMessage device_msg;
            while (xQueueReceive(deviceQueue, &device_msg, 0) == pdTRUE) {
                StaticJsonDocument<256> doc;
                JsonObject device = doc.createNestedObject("device");
                
                char mac[18];
                snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
                        device_msg.mac[0], device_msg.mac[1], device_msg.mac[2],
                        device_msg.mac[3], device_msg.mac[4], device_msg.mac[5]);
                
                device["mac"] = mac;
                device["rssi"] = device_msg.rssi;
                device["type"] = device_msg.is_wifi ? "wifi" : "ble";
                time_t now;
                time(&now);
                device["timestamp"] = now;
                
                char buffer[256];
                serializeJson(doc, buffer);
                
                if (mqttClient.publish(MQTT_DEVICE_TOPIC, buffer)) {
                    ESP_LOGD(MQTT_TAG, "Successfully sent device data");
                } else {
                    ESP_LOGE(MQTT_TAG, "Failed to send device data");
                }
                
                // Small delay between messages
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            
            mqttClient.disconnect();
        }
        
        ESP_LOGI(MQTT_TAG, "Disconnecting WiFi...");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    } else {
        ESP_LOGE(MQTT_TAG, "Failed to connect to WiFi");
    }
}

// MQTT task that handles sending queued messages
static void mqtt_task(void* parameter) {
    uint32_t ulNotificationValue;
    const TickType_t xMaxBlockTime = pdMS_TO_TICKS(60000); // 60 second timeout

    for(;;) {
        // Wait for notification from setSendIRQ
        if (xTaskNotifyWait(0x00, ULONG_MAX, &ulNotificationValue, xMaxBlockTime) == pdTRUE) {
            if (ulNotificationValue & SENDCYCLE_IRQ) {
                ESP_LOGI(MQTT_TAG, "Send IRQ received, processing queued messages");
                send_queued_messages();
            }
        }
        // Small task delay to prevent watchdog triggers
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void pax_mqtt_init() {
    wifiSemaphore = xSemaphoreCreateMutex();
    countQueue = xQueueCreate(COUNT_QUEUE_SIZE, sizeof(CountMessage));
    deviceQueue = xQueueCreate(DEVICE_QUEUE_SIZE, sizeof(DeviceMessage));
    
    if (!wifiSemaphore || !countQueue || !deviceQueue) {
        ESP_LOGE(MQTT_TAG, "Failed to create MQTT resources");
        return;
    }

    // Create MQTT task
    BaseType_t result = xTaskCreatePinnedToCore(
        mqtt_task,          // Task function
        "mqtt_task",        // Task name
        4096,              // Stack size (bytes)
        NULL,              // Parameter to pass
        1,                 // Task priority
        &paxMqttTaskHandle,// Task handle
        1                  // Core ID (1 = non-protocol CPU)
    );

    if (result != pdPASS) {
        ESP_LOGE(MQTT_TAG, "Failed to create MQTT task");
        return;
    }

    ESP_LOGI(MQTT_TAG, "MQTT Handler initialized successfully");
}

void pax_mqtt_loop() {
    vTaskDelay(pdMS_TO_TICKS(10));
} 
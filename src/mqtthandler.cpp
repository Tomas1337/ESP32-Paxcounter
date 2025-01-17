#include "mqtthandler.h"
#include "wificonfig.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "libpax_helpers.h"
#include "globals.h"
#include "wifi_hooks.h"
#include "configportal.h"
#include "button.h"
#include <time.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <SPIFFS.h>

// All strings in flash
static const char* const MQTT_TAG PROGMEM = "MQTT_HANDLER";
static const char* const CONFIG_AP_NAME PROGMEM = "ESP32-Config";

// Minimize variable size
struct MQTTMessage {
    uint16_t pax;
    uint16_t wifi_count;
    uint16_t ble_count;
    uint32_t timestamp;
} __attribute__((packed));

// Static allocations
static TaskHandle_t paxMqttTaskHandle = NULL;
static SemaphoreHandle_t wifiSemaphore = NULL;
static QueueHandle_t mqttMessageQueue = NULL;
static wifi_mode_t wifiState = WIFI_MODE_NULL;
static WiFiClient wifiClient;
static PubSubClient mqttClient(wifiClient);
static StaticJsonDocument<200> jsonDoc;
static char jsonBuffer[200];

// Static variables
static volatile uint32_t lastButtonPress = 0;
static volatile uint8_t buttonPressCount = 0;

#ifndef MQTT_TRIGGER_PIN
#define MQTT_TRIGGER_PIN 2
#endif

// Minimal ISR
void IRAM_ATTR buttonISR() {
    uint32_t now = millis();
    if ((now - lastButtonPress) > 300) {
        lastButtonPress = now;
        if (buttonPressCount < 255) buttonPressCount++;
    }
}

// All other functions moved to flash
static bool __attribute__((noinline)) switchWiFiMode(wifi_mode_t mode) {
    if (!xSemaphoreTake(wifiSemaphore, pdMS_TO_TICKS(1000))) return false;
    
    WiFi.disconnect(true);
    WiFi.mode(WIFI_MODE_NULL);
    vTaskDelay(pdMS_TO_TICKS(100));

    bool result = true;
    switch (mode) {
        case WIFI_MODE_AP:
            result = WiFi.mode(WIFI_MODE_AP);
            if (result) WiFi.softAP(CONFIG_AP_NAME);
            break;
        case WIFI_MODE_STA:
            result = WiFi.mode(WIFI_MODE_STA);
            break;
        default:
            WiFi.mode(WIFI_MODE_NULL);
            break;
    }
    wifiState = result ? mode : WIFI_MODE_NULL;
    xSemaphoreGive(wifiSemaphore);
    return result;
}

static void __attribute__((noinline)) handleWiFiConnection() {
    if (WiFi.status() != WL_CONNECTED) {
        switchWiFiMode(WIFI_MODE_STA);
        WiFi.begin(wifiConfig.ssid.c_str(), wifiConfig.password.c_str());
        
        uint8_t attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts++ < 20) {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        
        if (WiFi.status() != WL_CONNECTED) {
            switchWiFiMode(WIFI_MODE_NULL);
        }
    }
}

static void __attribute__((noinline)) handleMQTTConnection() {
    if (!mqttClient.connected()) {
        mqttClient.setServer(wifiConfig.mqtt_server.c_str(), wifiConfig.mqtt_port);
        mqttClient.connect(clientId);
    }
}

static void __attribute__((noinline)) paxMqttTask(void* parameter) {
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for (;;) {
#ifdef HAS_BUTTON
        if (get_button_press_count() >= 5) {
            reset_button_press_count();
            switchWiFiMode(WIFI_MODE_AP);
            startConfigPortal();
            continue;
        }
#endif

        if (uxQueueMessagesWaiting(mqttMessageQueue) > 0) {
            handleWiFiConnection();
            if (WiFi.status() == WL_CONNECTED) {
                handleMQTTConnection();
                if (mqttClient.connected()) {
                    MQTTMessage msg;
                    while (xQueueReceive(mqttMessageQueue, &msg, 0) == pdTRUE) {
                        jsonDoc.clear();
                        jsonDoc["pax"] = msg.pax;
                        jsonDoc["wifi"] = msg.wifi_count;
                        jsonDoc["ble"] = msg.ble_count;
                        jsonDoc["timestamp"] = msg.timestamp;

                        size_t len = serializeJson(jsonDoc, jsonBuffer, sizeof(jsonBuffer));
                        mqttClient.publish(wifiConfig.mqtt_topic.c_str(), jsonBuffer, len);
                        vTaskDelay(pdMS_TO_TICKS(50));
                    }
                    mqttClient.disconnect();
                }
            }
            switchWiFiMode(WIFI_MODE_NULL);
        }

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(100));
    }
}

void __attribute__((noinline)) pax_mqtt_init() {
    wifiSemaphore = xSemaphoreCreateMutex();
    mqttMessageQueue = xQueueCreate(32, sizeof(MQTTMessage));
    
    if (!wifiSemaphore || !mqttMessageQueue) return;

    pinMode(MQTT_TRIGGER_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(MQTT_TRIGGER_PIN), buttonISR, FALLING);

    xTaskCreatePinnedToCore(paxMqttTask, "mqtt_task", 4096, NULL, 2, &paxMqttTaskHandle, 1);
}

void __attribute__((noinline)) pax_mqtt_enqueue(unsigned short pax_count, unsigned short wifi_count, unsigned short ble_count) {
    if (!mqttMessageQueue) return;

    MQTTMessage msg = {
        .pax = pax_count,
        .wifi_count = wifi_count,
        .ble_count = ble_count,
        .timestamp = millis()
    };

    if (xQueueSend(mqttMessageQueue, &msg, pdMS_TO_TICKS(100)) != pdTRUE && paxMqttTaskHandle) {
        xTaskNotify(paxMqttTaskHandle, 1, eSetBits);
    }
}

void __attribute__((noinline)) pax_mqtt_loop() {
    vTaskDelay(pdMS_TO_TICKS(10));
} 
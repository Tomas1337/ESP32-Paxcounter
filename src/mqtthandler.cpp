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
#define MQTT_QUEUE_SIZE 20  // Increased from 10 to 20

// Static variables
static volatile uint32_t lastButtonPress = 0;
static volatile uint8_t buttonPressCount = 0;

// Timer for cyclic sending
static TimerHandle_t mqttSendTimer = NULL;


// Current probe counts - protected by mutex
static struct {
    uint32_t pax;
    uint32_t wifi_count;
    uint32_t ble_count;
    uint32_t timestamp;
} currentCounts = {0};

// Device detection buffer - protected by mutex
static struct {
    DeviceData devices[50];  // Buffer for device detections
    size_t count;
} deviceBuffer = {0};

static portMUX_TYPE countsMux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE deviceMux = portMUX_INITIALIZER_UNLOCKED;

// FreeRTOS primitives
static QueueHandle_t mqttCyclicQueue = NULL;  // Separate queue for cyclic events
static portMUX_TYPE mqttMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool shouldSendMQTT = false;

// ISR handler for button press
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
        // If the configuration portal is active, skip sending data
        if (portalActive) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

#ifdef HAS_BUTTON
        // If the button was pressed 5 times, start the config portal
        if (get_button_press_count() >= 5) {
            reset_button_press_count();
            switchWiFiMode(WIFI_MODE_AP);
            startConfigPortal();
            continue;
        }
#endif

        // Check if there's a message in the queue
        if (uxQueueMessagesWaiting(mqttMessageQueue) > 0) {
            // Ensure WiFi is connected
            handleWiFiConnection();
            if (WiFi.status() == WL_CONNECTED) {
                // Ensure MQTT is connected
                handleMQTTConnection();
                if (mqttClient.connected()) {
                    MQTTMessage msg;
                    // Pull each MQTT message from the queue
                    while (xQueueReceive(mqttMessageQueue, &msg, 0) == pdTRUE) {
                        // Clear the JSON document and create a nested "data" object
                        jsonDoc.clear();
                        JsonObject data = jsonDoc.createNestedObject("data");

                        data["pax"]       = msg.pax;
                        data["wifi"]      = msg.wifi_count;
                        data["ble"]       = msg.ble_count;
                        data["timestamp"] = msg.timestamp;

                        // Serialize and publish
                        size_t len = serializeJson(jsonDoc, jsonBuffer, sizeof(jsonBuffer));
                        mqttClient.publish(wifiConfig.mqtt_topic.c_str(), jsonBuffer, len);

                        // Small delay to ensure messages don't clobber each other
                        vTaskDelay(pdMS_TO_TICKS(50));
                    }
                    // Disconnect MQTT after sending all messages
                    mqttClient.disconnect();
                }
            }
            // Switch back to null mode when done
            switchWiFiMode(WIFI_MODE_NULL);
        }

        // Wait for next cycle
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

void pax_mqtt_loop() {
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(MQTT_TAG, "MQTT Handler initialized successfully");

}

void pax_mqtt_enqueue(uint16_t pax_count, uint16_t wifi_count, uint16_t ble_count) {
    MQTTMessage msg = {
        .pax = pax_count,
        .wifi_count = wifi_count,
        .ble_count = ble_count,
        .timestamp = millis()
    };
    
    UBaseType_t queueCount = uxQueueMessagesWaiting(mqttMessageQueue);
    ESP_LOGD(MQTT_TAG, "Current queue size before enqueue: %d/%d", queueCount, MQTT_QUEUE_SIZE);
    
    if (xQueueSend(mqttMessageQueue, &msg, 0) != pdTRUE) {
        ESP_LOGW(MQTT_TAG, "MQTT message queue is full (%d messages), triggering send", MQTT_QUEUE_SIZE);
        // Queue is full, trigger immediate send
        uint32_t currentTime = millis();
        xQueueSend(mqttCyclicQueue, &currentTime, 0);
    } else {
        ESP_LOGD(MQTT_TAG, "Message queued: pax=%d, wifi=%d, ble=%d (queue size now: %d/%d)", 
        pax_count, wifi_count, ble_count, uxQueueMessagesWaiting(mqttMessageQueue), MQTT_QUEUE_SIZE);
    }
}

void pax_mqtt_enqueue_device(const uint8_t* mac, int8_t rssi, bool is_wifi) {
    if (!mac) {
        ESP_LOGE(MQTT_TAG, "Invalid MAC address pointer");
        return;
    }

    // Create temporary device data outside critical section
    DeviceData tempDevice;
    memcpy(tempDevice.mac, mac, 6);
    tempDevice.rssi = rssi;
    tempDevice.is_wifi = is_wifi;
    tempDevice.timestamp = millis();

    // Minimize time in critical section
    portENTER_CRITICAL(&deviceMux);
    if (deviceBuffer.count < 20) {  // Increased buffer size to 50
        deviceBuffer.devices[deviceBuffer.count] = tempDevice;
        deviceBuffer.count++;
        
        // If buffer is getting full (80% capacity), trigger a send
        if (deviceBuffer.count >= 15) {  // 80% of 50
            portEXIT_CRITICAL(&deviceMux);
            ESP_LOGW(MQTT_TAG, "Device buffer near full (%d devices), triggering send", deviceBuffer.count);
            uint32_t currentTime = millis();
            xQueueSend(mqttCyclicQueue, &currentTime, 0);
        } else {
            portEXIT_CRITICAL(&deviceMux);
        }
        
        ESP_LOGI(MQTT_TAG, "Enqueued device: Type=%s, Buffer size=%d/%d", 
                is_wifi ? "WiFi" : "BLE", deviceBuffer.count, 50);
    } else {
        portEXIT_CRITICAL(&deviceMux);
        ESP_LOGW(MQTT_TAG, "Device buffer full (50 devices), dropping packet");
    }
}

bool pax_mqtt_connect() {
    if (WiFi.status() != WL_CONNECTED) {
        ESP_LOGE(MQTT_TAG, "Cannot connect to MQTT - WiFi not connected");
        return false;
    }
    
    if (!mqttClient.connected()) {
        ESP_LOGI(MQTT_TAG, "Connecting to MQTT broker...");
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
    ESP_LOGI(MQTT_TAG, "Starting to send queued messages... Queue size: %d/%d", 
            uxQueueMessagesWaiting(mqttMessageQueue), MQTT_QUEUE_SIZE);
    
    // Connect to WiFi first
    WiFi.mode(WIFI_STA);
    ESP_LOGI(MQTT_TAG, "Connecting to WiFi...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        vTaskDelay(pdMS_TO_TICKS(500));
        ESP_LOGI(MQTT_TAG, "Attempting to connect to WiFi... (%d)", attempts + 1);
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        ESP_LOGI(MQTT_TAG, "WiFi connected successfully!");
        
        // Send all queued messages
        MQTTMessage msg;
        int messagesSent = 0;
        while (xQueueReceive(mqttMessageQueue, &msg, 0) == pdTRUE) {
            messagesSent++;
            ESP_LOGI(MQTT_TAG, "Sending message %d, Queue remaining: %d/%d", 
                    messagesSent, uxQueueMessagesWaiting(mqttMessageQueue), MQTT_QUEUE_SIZE);
            
            // Update current counts for sending
            portENTER_CRITICAL(&countsMux);
            currentCounts = {msg.pax, msg.wifi_count, msg.ble_count, msg.timestamp};
            portEXIT_CRITICAL(&countsMux);
            
            // Send both count data and device detections
            pax_mqtt_send_data();
            pax_mqtt_send_devices();
            
            vTaskDelay(pdMS_TO_TICKS(100)); // Small delay between messages
        }
        
        ESP_LOGI(MQTT_TAG, "Sent %d messages. Queue is now empty.", messagesSent);
        
        ESP_LOGI(MQTT_TAG, "Disconnecting WiFi...");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    } else {
        ESP_LOGE(MQTT_TAG, "Failed to connect to WiFi, data not sent. Queue size remains: %d/%d",
                uxQueueMessagesWaiting(mqttMessageQueue), MQTT_QUEUE_SIZE);
    }
}

void pax_mqtt_send_data() {
    if (!pax_mqtt_connect()) {
        ESP_LOGE(MQTT_TAG, "Cannot send data - MQTT connection failed");
        return;
    }
    
    StaticJsonDocument<256> doc;
    JsonObject data = doc.createNestedObject("data");
    
    portENTER_CRITICAL(&countsMux);
    data["pax"] = currentCounts.pax;
    data["wifi"] = currentCounts.wifi_count;
    data["ble"] = currentCounts.ble_count;
    // Use current time instead of millis
    time_t now;
    time(&now);
    data["timestamp"] = now;
    portEXIT_CRITICAL(&countsMux);
    
    char buffer[256];
    serializeJson(doc, buffer);
    
    if (mqttClient.publish(MQTT_OUTTOPIC, buffer)) {
        ESP_LOGI(MQTT_TAG, "Successfully sent count data to MQTT broker");
    } else {
        ESP_LOGE(MQTT_TAG, "Failed to publish count data to MQTT broker");
    }
}

void pax_mqtt_send_devices() {
    if (!pax_mqtt_connect()) {
        return;
    }
    
    // Copy data we want to send while in critical section
    DeviceData devices_to_send[50];
    size_t count;
    
    portENTER_CRITICAL(&deviceMux);
    count = deviceBuffer.count;
    memcpy(devices_to_send, deviceBuffer.devices, count * sizeof(DeviceData));
    deviceBuffer.count = 0;  // Clear buffer after copying
    portEXIT_CRITICAL(&deviceMux);
    
    // Now send each device outside of critical section
    for (size_t i = 0; i < count; i++) {
        StaticJsonDocument<256> doc;
        JsonObject device = doc.createNestedObject("device");
        
        char mac[18];
        snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
                devices_to_send[i].mac[0], devices_to_send[i].mac[1],
                devices_to_send[i].mac[2], devices_to_send[i].mac[3],
                devices_to_send[i].mac[4], devices_to_send[i].mac[5]);
        
        device["mac"] = mac;
        device["rssi"] = devices_to_send[i].rssi;
        device["type"] = devices_to_send[i].is_wifi ? "wifi" : "ble";
        // Use current time instead of millis
        time_t now;
        time(&now);
        device["timestamp"] = now;
        
        char buffer[256];
        serializeJson(doc, buffer);
        
        // Allow other tasks to run between publishes
        vTaskDelay(pdMS_TO_TICKS(10));
        
        if (mqttClient.publish(MQTT_DEVICE_TOPIC, buffer)) {
            ESP_LOGI(MQTT_TAG, "Successfully sent device data to MQTT broker");
        } else {
            ESP_LOGE(MQTT_TAG, "Failed to publish device data to MQTT broker");
            // Try to reconnect if we lost connection
            if (!pax_mqtt_connect()) {
                ESP_LOGE(MQTT_TAG, "Lost MQTT connection and failed to reconnect");
                break;
            }
        }
    }
    
    mqttClient.disconnect();
}

// Hook function implementation for WiFi sniffer
void IRAM_ATTR wifi_packet_handler_hook(uint8_t* mac, int8_t rssi) {
    if (!mac) return;
    
    // Minimize logging in ISR context
    pax_mqtt_enqueue_device(mac, rssi, true);
} 
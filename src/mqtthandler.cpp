#include <ArduinoJson.h>
#include "mqtthandler.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "libpax_helpers.h"
#include "globals.h"
#include <time.h>

#define MQTT_QUEUE_SIZE 20  // Increased from 10 to 20

#ifndef MQTT_SERVER
#define MQTT_SERVER MQTT_SERVER
#endif

#ifndef MQTT_PORT
#define MQTT_PORT MQTT_PORT
#endif

#ifndef MQTT_OUTTOPIC
#define MQTT_OUTTOPIC MQTT_OUTTOPIC
#endif


#ifndef MQTT_TRIGGER_PIN
#define MQTT_TRIGGER_PIN MQTT_TRIGGER_PIN
#endif

#ifndef MQTT_TRIGGER_MODE
#define MQTT_TRIGGER_MODE MQTT_TRIGGER_MODE
#endif

#ifndef MQTT_SEND_INTERVAL
#define MQTT_SEND_INTERVAL MQTT_SEND_INTERVAL
#endif

static const char* MQTT_TAG = "MQTT_HANDLER";

// Static task handles
static TaskHandle_t paxMqttTaskHandle = NULL;

// Timer for cyclic sending
static TimerHandle_t mqttSendTimer = NULL;

// Queue for storing messages until they are sent
static QueueHandle_t mqttMessageQueue = NULL;

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

WiFiClient paxWifiClient;
PubSubClient paxMqttClient(paxWifiClient);

// FreeRTOS primitives
static QueueHandle_t mqttButtonQueue = NULL;
static QueueHandle_t mqttCyclicQueue = NULL;  // Separate queue for cyclic events
static portMUX_TYPE mqttMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool shouldSendMQTT = false;

// Message structure for the queue
struct MQTTMessage {
    uint32_t pax;
    uint32_t wifi_count;
    uint32_t ble_count;
    uint32_t timestamp;
};

// ISR handler for button press
void IRAM_ATTR buttonISR() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    uint32_t pressTime = millis();
    
    portENTER_CRITICAL_ISR(&mqttMux);
    static uint32_t lastPressTime = 0;
    if ((pressTime - lastPressTime) > 300) {
        lastPressTime = pressTime;
        xQueueSendFromISR(mqttButtonQueue, &pressTime, &xHigherPriorityTaskWoken);
    }
    portEXIT_CRITICAL_ISR(&mqttMux);
    
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
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

void paxMqttTask(void *pvParameters) {
    ESP_LOGI(MQTT_TAG, "MQTT Task started");
    
    for(;;) {
        uint32_t eventTime;
        BaseType_t receivedFromButton = xQueueReceive(mqttButtonQueue, &eventTime, 0);
        BaseType_t receivedFromCyclic = xQueueReceive(mqttCyclicQueue, &eventTime, receivedFromButton ? 0 : pdMS_TO_TICKS(100));
        
        if (receivedFromButton || receivedFromCyclic) {
            if (receivedFromButton) {
                ESP_LOGI(MQTT_TAG, "Button press detected at %lu ms", eventTime);
            } else {
                ESP_LOGI(MQTT_TAG, "Cyclic send triggered at %lu ms", eventTime);
            }
            
            send_queued_messages();
        }
        taskYIELD();
    }
}

// Timer callback function
void mqttSendTimerCallback(TimerHandle_t xTimer) {
    if (MQTT_SEND_INTERVAL > 0) {  // Only trigger if cyclic sending is enabled
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        uint32_t currentTime = millis();
        xQueueSendFromISR(mqttCyclicQueue, &currentTime, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR();
        }
    }
}

void pax_mqtt_init() {
    ESP_LOGI(MQTT_TAG, "Initializing MQTT Handler...");
    
    mqttButtonQueue = xQueueCreate(5, sizeof(uint32_t));
    mqttCyclicQueue = xQueueCreate(5, sizeof(uint32_t));
    mqttMessageQueue = xQueueCreate(MQTT_QUEUE_SIZE, sizeof(MQTTMessage));
    
    if (mqttButtonQueue == NULL || mqttCyclicQueue == NULL || mqttMessageQueue == NULL) {
        ESP_LOGE(MQTT_TAG, "Failed to create queues");
        return;
    }
    
    // Create and start timer if cyclic sending is enabled
    if (MQTT_SEND_INTERVAL > 0) {
        mqttSendTimer = xTimerCreate(
            "MQTTSendTimer",
            pdMS_TO_TICKS(MQTT_SEND_INTERVAL * 1000),  // Convert seconds to milliseconds
            pdTRUE,  // Auto reload
            (void *)0,
            mqttSendTimerCallback
        );
        
        if (mqttSendTimer == NULL) {
            ESP_LOGE(MQTT_TAG, "Failed to create MQTT send timer");
        } else {
            if (xTimerStart(mqttSendTimer, 0) != pdPASS) {
                ESP_LOGE(MQTT_TAG, "Failed to start MQTT send timer");
            } else {
                ESP_LOGI(MQTT_TAG, "MQTT cyclic sending enabled, interval: %d seconds", MQTT_SEND_INTERVAL);
            }
        }
    } else {
        ESP_LOGI(MQTT_TAG, "MQTT cyclic sending disabled");
    }
    
    pinMode(MQTT_TRIGGER_PIN, MQTT_TRIGGER_MODE);
    ESP_LOGI(MQTT_TAG, "Setting up trigger button on pin %d", MQTT_TRIGGER_PIN);
    
    BaseType_t xReturned = xTaskCreatePinnedToCore(
        paxMqttTask,
        "PAX_MQTT_Task",
        8192,
        NULL,
        1,
        &paxMqttTaskHandle,
        1
    );
    
    if (xReturned != pdPASS) {
        ESP_LOGE(MQTT_TAG, "Failed to create MQTT task");
        return;
    }
    
    attachInterrupt(digitalPinToInterrupt(MQTT_TRIGGER_PIN), buttonISR, FALLING);
    
    ESP_LOGI(MQTT_TAG, "MQTT Handler initialized successfully");
}

void pax_mqtt_loop() {
    vTaskDelay(pdMS_TO_TICKS(10));
}

void pax_mqtt_enqueue(uint32_t pax_count, uint32_t wifi_count, uint32_t ble_count) {
    MQTTMessage msg = {
        .pax = pax_count,
        .wifi_count = wifi_count,
        .ble_count = ble_count,
        .timestamp = millis()
    };
    
    UBaseType_t queueCount = uxQueueMessagesWaiting(mqttMessageQueue);
    ESP_LOGI(MQTT_TAG, "Current queue size before enqueue: %d/%d", queueCount, MQTT_QUEUE_SIZE);
    
    if (xQueueSend(mqttMessageQueue, &msg, 0) != pdTRUE) {
        ESP_LOGW(MQTT_TAG, "MQTT message queue is full (%d messages), triggering send", MQTT_QUEUE_SIZE);
        // Queue is full, trigger immediate send
        uint32_t currentTime = millis();
        xQueueSend(mqttCyclicQueue, &currentTime, 0);
    } else {
        ESP_LOGI(MQTT_TAG, "Message queued: pax=%d, wifi=%d, ble=%d (queue size now: %d/%d)", 
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
    
    if (!paxMqttClient.connected()) {
        ESP_LOGI(MQTT_TAG, "Connecting to MQTT broker...");
        paxMqttClient.setServer(MQTT_SERVER, MQTT_PORT);
        paxMqttClient.setKeepAlive(MQTT_KEEPALIVE);
        
        if (paxMqttClient.connect(MQTT_CLIENTNAME)) {
            ESP_LOGI(MQTT_TAG, "Connected to MQTT broker");
            return true;
        } else {
            ESP_LOGE(MQTT_TAG, "Failed to connect to MQTT broker");
            return false;
        }
    }
    return true;
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
    
    if (paxMqttClient.publish(MQTT_OUTTOPIC, buffer)) {
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
        
        if (paxMqttClient.publish(MQTT_DEVICE_TOPIC, buffer)) {
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
    
    paxMqttClient.disconnect();
}

// Hook function implementation for WiFi sniffer
void IRAM_ATTR wifi_packet_handler_hook(uint8_t* mac, int8_t rssi) {
    if (!mac) return;
    
    // Minimize logging in ISR context
    pax_mqtt_enqueue_device(mac, rssi, true);
} 
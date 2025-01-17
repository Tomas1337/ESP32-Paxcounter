#include "configportal.h"
#include "wificonfig.h"
#include "esp_log.h"
#include <WebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

static const char* CONFIG_TAG = "CONFIG_PORTAL";
static WebServer server(80);
static bool portalActive = false;

// Move the HTML string to flash memory
static const char CONFIG_HTML[] PROGMEM = R"(
<!DOCTYPE html>
<html>
<head>
    <title>ESP32 Configuration</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial; margin: 20px; }
        .input-group { margin-bottom: 15px; }
        label { display: block; margin-bottom: 5px; }
        input { width: 100%; padding: 8px; margin-bottom: 10px; }
        button { background-color: #4CAF50; color: white; padding: 10px 20px; border: none; cursor: pointer; }
        button:hover { background-color: #45a049; }
    </style>
</head>
<body>
    <h2>ESP32 Configuration</h2>
    <form method="POST" action="/save">
        <div class="input-group">
            <label for="ssid">WiFi SSID:</label>
            <input type="text" id="ssid" name="ssid" required>
        </div>
        <div class="input-group">
            <label for="password">WiFi Password:</label>
            <input type="password" id="password" name="password">
        </div>
        <div class="input-group">
            <label for="mqtt_server">MQTT Server:</label>
            <input type="text" id="mqtt_server" name="mqtt_server" required>
        </div>
        <div class="input-group">
            <label for="mqtt_port">MQTT Port:</label>
            <input type="number" id="mqtt_port" name="mqtt_port" value="1883" required>
        </div>
        <div class="input-group">
            <label for="mqtt_topic">MQTT Topic:</label>
            <input type="text" id="mqtt_topic" name="mqtt_topic" required>
        </div>
        <button type="submit">Save Configuration</button>
    </form>
</body>
</html>
)";

void IRAM_ATTR handleRoot() {
    server.send_P(200, "text/html", CONFIG_HTML);
}

void IRAM_ATTR handleSave() {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    String mqtt_server = server.arg("mqtt_server");
    String mqtt_topic = server.arg("mqtt_topic");
    int mqtt_port = server.arg("mqtt_port").toInt();

    // Save configuration to file
    File configFile = SPIFFS.open("/config.json", "w");
    if (configFile) {
        StaticJsonDocument<512> doc;
        doc["wifi_ssid"] = ssid;
        doc["wifi_password"] = password;
        doc["mqtt_server"] = mqtt_server;
        doc["mqtt_topic"] = mqtt_topic;
        doc["mqtt_port"] = mqtt_port;
        
        serializeJson(doc, configFile);
        configFile.close();
        ESP_LOGI(CONFIG_TAG, "Configuration saved successfully");
    } else {
        ESP_LOGE(CONFIG_TAG, "Failed to open config file for writing");
    }

    server.send(200, "text/plain", "Configuration saved. Device will restart...");
    delay(1000);
    ESP.restart();
}

void startConfigPortal() {
    if (portalActive) {
        ESP_LOGW(CONFIG_TAG, "Config portal already active");
        return;
    }

    ESP_LOGI(CONFIG_TAG, "Starting configuration portal");
    
    // Start AP mode
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP32-Config");
    
    // Configure web server
    server.on("/", HTTP_GET, handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    
    server.begin();
    portalActive = true;
    
    ESP_LOGI(CONFIG_TAG, "Configuration portal started at 192.168.4.1");
} 
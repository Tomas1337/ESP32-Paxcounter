#include "configportal.h"
#include <ArduinoJson.h>
#include "esp_log.h"

static WebServer portal_server(80);
volatile bool config_portal_active = false;
static unsigned long portal_start_time = 0;

// HTML for the configuration page
const char CONFIG_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>ESP32 Paxcounter Configuration</title>
    <style>
        body { font-family: Arial; margin: 20px; }
        .container { max-width: 400px; margin: 0 auto; }
        input[type=text], input[type=password], input[type=number] { 
            width: 100%; 
            padding: 12px 20px; 
            margin: 8px 0; 
            display: inline-block; 
            border: 1px solid #ccc; 
            box-sizing: border-box; 
        }
        button { 
            background-color: #4CAF50; 
            color: white; 
            padding: 14px 20px; 
            margin: 8px 0; 
            border: none; 
            cursor: pointer; 
            width: 100%; 
        }
        button:hover { opacity: 0.8; }
    </style>
</head>
<body>
    <div class="container">
        <h2>ESP32 Paxcounter Configuration</h2>
        <form action="/save" method="POST">
            <h3>WiFi Settings</h3>
            <label for="ssid">WiFi SSID:</label>
            <input type="text" id="ssid" name="ssid" required>
            
            <label for="password">WiFi Password:</label>
            <input type="password" id="password" name="password" required>
            
            <h3>MQTT Settings</h3>
            <label for="mqtt_server">MQTT Server:</label>
            <input type="text" id="mqtt_server" name="mqtt_server" required>
            
            <label for="mqtt_port">MQTT Port:</label>
            <input type="number" id="mqtt_port" name="mqtt_port" value="1883" required>
            
            <label for="mqtt_topic">MQTT Topic:</label>
            <input type="text" id="mqtt_topic" name="mqtt_topic" required>
            
            <button type="submit">Save Configuration</button>
        </form>
    </div>
</body>
</html>
)rawliteral";

void handleRoot() {
    portal_server.send(200, "text/html", CONFIG_HTML);
}

void handleSave() {
    String ssid = portal_server.arg("ssid");
    String password = portal_server.arg("password");
    String mqtt_server = portal_server.arg("mqtt_server");
    String mqtt_topic = portal_server.arg("mqtt_topic");
    int mqtt_port = portal_server.arg("mqtt_port").toInt();

    save_wifi_config(ssid.c_str(), password.c_str(), mqtt_server.c_str(), 
                    mqtt_port, mqtt_topic.c_str());

    portal_server.send(200, "text/plain", "Configuration saved. Device will restart...");
    delay(2000);
    ESP.restart();
}

void init_config_portal() {
    // Initialize SPIFFS if not already initialized
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        return;
    }
    
    // Setup configuration portal endpoints
    portal_server.on("/", HTTP_GET, handleRoot);
    portal_server.on("/save", HTTP_POST, handleSave);
}

void start_config_portal() {
    if (!config_portal_active) {
        WiFi.mode(WIFI_AP);
        WiFi.softAP(CONFIG_AP_SSID, CONFIG_AP_PASSWORD);
        
        portal_server.begin();
        config_portal_active = true;
        portal_start_time = millis();
        
        ESP_LOGI(TAG, "Configuration portal started at IP: %s", 
                WiFi.softAPIP().toString().c_str());
    }
}

void handle_config_portal() {
    if (config_portal_active) {
        portal_server.handleClient();
        
        // Check for timeout
        if (millis() - portal_start_time > CONFIG_PORTAL_TIMEOUT * 1000) {
            ESP_LOGI(TAG, "Configuration portal timed out");
            config_portal_active = false;
            WiFi.softAPdisconnect(true);
            ESP.restart();
        }
    }
}

bool is_config_portal_active() {
    return config_portal_active;
}

void save_wifi_config(const char* ssid, const char* password, const char* mqtt_server, 
                    int mqtt_port, const char* mqtt_topic) {
    // Create JSON document
    StaticJsonDocument<512> doc;
    doc["wifi_ssid"] = ssid;
    doc["wifi_password"] = password;
    doc["mqtt_server"] = mqtt_server;
    doc["mqtt_port"] = mqtt_port;
    doc["mqtt_topic"] = mqtt_topic;

    // Serialize JSON to string
    String jsonString;
    serializeJson(doc, jsonString);

    // Open file for writing
    FILE* f = fopen(CONFIG_FILE_PATH, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open config file for writing");
        return;
    }

    // Write to file
    size_t written = fwrite(jsonString.c_str(), 1, jsonString.length(), f);
    fclose(f);

    if (written == jsonString.length()) {
        ESP_LOGI(TAG, "Configuration saved successfully");
    } else {
        ESP_LOGE(TAG, "Failed to write configuration");
    }
} 
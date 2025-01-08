#include "configportal.h"
#include "esp_log.h"
#include <LittleFS.h>

static const char* CONFIG_TAG = "CONFIG_PORTAL";
static AsyncWebServer* server = nullptr;
bool config_portal_active = false;
static unsigned long config_portal_start_time = 0;

// HTML content
static const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
  <title>ESP32 Configuration</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    html { font-family: Arial; display: inline-block; text-align: center; }
    body { margin: 0; }
    .content { padding: 20px; }
    .card { background-color: white; box-shadow: 2px 2px 12px 1px rgba(140,140,140,.5); }
    .button { padding: 15px 50px; font-size: 24px; border-radius: 10px; margin: 10px; }
    .input-group { margin: 10px; }
    input { padding: 5px; font-size: 16px; width: 200px; }
  </style>
</head>
<body>
  <div class="content">
    <h1>ESP32 Configuration Portal</h1>
    <div class="card">
      <form action="/save" method="POST">
        <div class="input-group">
          <label>WiFi SSID:</label><br>
          <input type="text" name="ssid" required><br>
        </div>
        <div class="input-group">
          <label>WiFi Password:</label><br>
          <input type="password" name="password" required><br>
        </div>
        <div class="input-group">
          <label>MQTT Server:</label><br>
          <input type="text" name="mqtt_server" required><br>
        </div>
        <div class="input-group">
          <label>MQTT Port:</label><br>
          <input type="number" name="mqtt_port" value="1883" required><br>
        </div>
        <div class="input-group">
          <input type="submit" value="Save Configuration" class="button">
        </div>
      </form>
    </div>
  </div>
</body>
</html>
)rawliteral";

void init_config_portal() {
    // Initialize LittleFS
    if(!LittleFS.begin(true)) {
        ESP_LOGE(CONFIG_TAG, "An error occurred while mounting LittleFS");
        return;
    }
    
    // Load configuration if exists
    load_config_from_spiffs();
}

void start_config_portal() {
    if (server != nullptr) {
        delete server;
    }
    
    server = new AsyncWebServer(80);
    
    // Stop WiFi scanning
    // wifi_sniffer_stop();
    
    // Set up AP mode
    WiFi.mode(WIFI_AP);
    WiFi.softAP(CONFIG_AP_SSID, CONFIG_AP_PASSWORD);
    
    IPAddress IP = WiFi.softAPIP();
    ESP_LOGI(CONFIG_TAG, "AP IP address: %s", IP.toString().c_str());
    
    // Route for root
    server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send_P(200, "text/html", index_html);
    });
    
    // Route for form submission
    server->on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
        String ssid, password, mqtt_server;
        uint16_t mqtt_port = 1883;
        
        if (request->hasParam("ssid", true)) {
            ssid = request->getParam("ssid", true)->value();
        }
        if (request->hasParam("password", true)) {
            password = request->getParam("password", true)->value();
        }
        if (request->hasParam("mqtt_server", true)) {
            mqtt_server = request->getParam("mqtt_server", true)->value();
        }
        if (request->hasParam("mqtt_port", true)) {
            mqtt_port = request->getParam("mqtt_port", true)->value().toInt();
        }
        
        save_config_to_spiffs(ssid.c_str(), password.c_str(), mqtt_server.c_str(), mqtt_port);
        
        request->send(200, "text/plain", "Configuration saved. Device will restart in 5 seconds...");
        delay(5000);
        ESP.restart();
    });
    
    server->begin();
    config_portal_active = true;
    config_portal_start_time = millis();
    
    ESP_LOGI(CONFIG_TAG, "Configuration portal started");
}

void handle_config_portal() {
    if (!config_portal_active) return;
    
    // Check for timeout
    if (millis() - config_portal_start_time > CONFIG_PORTAL_TIMEOUT) {
        ESP_LOGI(CONFIG_TAG, "Configuration portal timed out");
        ESP.restart();
    }
}

bool is_config_portal_active() {
    return config_portal_active;
}

void save_config_to_spiffs(const char* ssid, const char* password, const char* mqtt_server, uint16_t mqtt_port) {
    StaticJsonDocument<512> doc;
    
    doc["wifi_ssid"] = ssid;
    doc["wifi_password"] = password;
    doc["mqtt_server"] = mqtt_server;
    doc["mqtt_port"] = mqtt_port;
    
    File configFile = LittleFS.open(CONFIG_FILE, "w");
    if (!configFile) {
        ESP_LOGE(CONFIG_TAG, "Failed to open config file for writing");
        return;
    }
    
    serializeJson(doc, configFile);
    configFile.close();
    ESP_LOGI(CONFIG_TAG, "Configuration saved to LittleFS");
}

bool load_config_from_spiffs() {
    if (!LittleFS.exists(CONFIG_FILE)) {
        ESP_LOGI(CONFIG_TAG, "No configuration file found");
        return false;
    }
    
    File configFile = LittleFS.open(CONFIG_FILE, "r");
    if (!configFile) {
        ESP_LOGE(CONFIG_TAG, "Failed to open config file for reading");
        return false;
    }
    
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, configFile);
    configFile.close();
    
    if (error) {
        ESP_LOGE(CONFIG_TAG, "Failed to parse config file");
        return false;
    }
    
    // Update the configuration values
    const char* ssid = doc["wifi_ssid"] | "";
    const char* password = doc["wifi_password"] | "";
    const char* mqtt_server = doc["mqtt_server"] | "";
    uint16_t mqtt_port = doc["mqtt_port"] | 1883;
    
    ESP_LOGI(CONFIG_TAG, "Configuration loaded from LittleFS");
    return true;
} 
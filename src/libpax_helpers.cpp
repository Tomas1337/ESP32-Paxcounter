#include "libpax_helpers.h"
#include "mqtthandler.h"
#include "esp_wifi.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "libpax_api.h"

static const char* PAX_TAG = "PAX_HELPER";

// libpax payload
struct count_payload_t count_from_libpax;

// WiFi sniffer packet handler
void wifi_sniffer_packet_handler(void* buff, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;

    const wifi_promiscuous_pkt_t *ppkt = (wifi_promiscuous_pkt_t *)buff;
    if (!ppkt) return;

    // Extract MAC address from packet
    const uint8_t *mac = ppkt->payload + 10;  // MAC address starts at offset 10
    int8_t rssi = ppkt->rx_ctrl.rssi;

    // Queue device data for MQTT
    pax_mqtt_enqueue_device(mac, rssi, true);
}

// BLE packet handler
static void ble_packet_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    if (event != ESP_GAP_BLE_SCAN_RESULT_EVT) return;
    if (!param || param->scan_rst.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) return;

    // Queue device data for MQTT
    pax_mqtt_enqueue_device(param->scan_rst.bda, param->scan_rst.rssi, false);
}

// Callback for libpax counter updates
void pax_counter_callback() {
    // Get current counts before they change
    struct count_payload_t current_count;
    if (libpax_counter_count(&current_count) == 0) {
        ESP_LOGI(PAX_TAG, "Counter callback triggered with counts - PAX: %d, WiFi: %d, BLE: %d", 
                 current_count.pax, current_count.wifi_count, current_count.ble_count);
        
        // Queue count data for MQTT and trigger send
        pax_mqtt_enqueue(current_count.pax, current_count.wifi_count, current_count.ble_count);
        
        // This will trigger sending of both count data and any queued device data
        setSendIRQ();
    }
}

void init_libpax(void) {
    ESP_LOGI(PAX_TAG, "Initializing libpax with callback...");
    
    // Initialize libpax with our callback
    int result = libpax_counter_init(pax_counter_callback, &count_from_libpax, cfg.sendcycle,
                    cfg.countermode);
                    
    if (result == 0) {
        ESP_LOGI(PAX_TAG, "Starting libpax counter...");
        
        // Register device callback for MQTT
        libpax_register_device_callback([](uint8_t *mac, int8_t rssi, bool is_wifi) {
            pax_mqtt_enqueue_device(mac, rssi, is_wifi);
        });
        
        // Set up WiFi sniffer if enabled
        if (cfg.wifiscan) {
            esp_wifi_set_promiscuous(true);
            esp_wifi_set_promiscuous_rx_cb(wifi_sniffer_packet_handler);
        }
        
        // Set up BLE scanner if enabled
        if (cfg.blescan) {
            esp_ble_gap_register_callback(ble_packet_handler);
        }
        
        // Start the counter
        result = libpax_counter_start();
        if (result == 0) {
            ESP_LOGI(PAX_TAG, "Libpax initialization complete");
        } else {
            ESP_LOGE(PAX_TAG, "Failed to start libpax counter: %d", result);
        }
    } else {
        ESP_LOGE(PAX_TAG, "Failed to initialize libpax: %d", result);
    }
}
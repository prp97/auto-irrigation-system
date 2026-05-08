#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h> 
#include <esp_log.h>
#include <si7021.h>
#include <i2cdev.h>
#include <esp_system.h>
#include <esp_err.h>
#include <string.h>
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include <stdbool.h>

#include "freertos/queue.h"

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <nvs_flash.h>
#include <mqtt_client.h>

#include "esp_ota_ops.h"
#include <inttypes.h>

#include <esp_mac.h>
#include "mender-client.h"
#include "mender-flash.h"

static const char *TAG = "sensor-node";

#define VERSION "1.0.0"

#define LED_GPIO 2

#define TOPIC_STATUS "sed/G03/auto-irrigation-system/status"
// #define TOPIC_LED "sed/G03/actuador/led"
#define TOPIC_TEMP "sed/G03/auto-irrigation-system/sensor/temp"
#define TOPIC_HUM "sed/G03/auto-irrigation-system/sensor/hum"

static i2c_dev_t dev = {0}; // Global variable to access from callback

// WiFi
void wifi_init_sta(void)
{
    // Initialize network stack and event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // Default WiFi configuration
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Configure SSID and Password from menuconfig (if defined)
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .password = CONFIG_ESP_WIFI_PASSWORD,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // Start WiFi
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI("WIFI", "Connecting to %s...", CONFIG_ESP_WIFI_SSID);
    esp_wifi_connect();
}

// MQTT
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Sensor node connected to Broker");
        // Publish "Online" message for consistency with LWT
        esp_mqtt_client_publish(client, TOPIC_STATUS, "Online", 0, 1, 1);
       
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "Error on MQTT stack");
        break;

    default:
        break;
    }
}

static esp_mqtt_client_handle_t mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        // .broker.address.uri = "mqtt://test.mosquitto.org",
        .broker.address.uri = "mqtt://broker.hivemq.com",
        .session.last_will = {
            .topic = TOPIC_STATUS,
            .msg = "Offline",
            .msg_len = strlen("Offline"),
            .qos = 1,
            .retain = 1,
        },
        .session.keepalive = 10, // Quick disconnection detection
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
    return client;
}

// --- Data collection task and MQTT publishing of sensor data
void collect_data_task(void *pvParameters) {
  // Take MQTT client handle from app_main
  esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t) pvParameters;
  char payload[16]; // Buffer for temperature and humidity strings
   
  float temperature = 25.0;
  float humidity = 50.0;
  esp_err_t res1 = ESP_OK;
  esp_err_t res2 = ESP_OK;

  while (1) {
    // Reading sensor data: temperature and humidity
    res1 = si7021_measure_temperature(&dev, &temperature);
    res2 = si7021_measure_humidity(&dev, &humidity);

    if (res1 == ESP_OK && res2 == ESP_OK) {
      ESP_LOGI(TAG, "Temperature: %.2f C, Humidity: %.2f %%", temperature, humidity);
      
      // Publish humidity
      snprintf(payload, sizeof(payload), "%.2f", humidity);
      if (client != NULL) {
          esp_mqtt_client_publish(client, TOPIC_HUM, payload, 0, 1, 0);
      }
      // Publish temperature
      snprintf(payload, sizeof(payload), "%.2f", temperature);
      if (client != NULL) {
          esp_mqtt_client_publish(client, TOPIC_TEMP, payload, 0, 1, 0);
      }
    } 
    else {
      ESP_LOGE(TAG, "Error reading temperature: %d (%s)", res1, esp_err_to_name(res1));
      ESP_LOGE(TAG, "Error reading humidity: %d (%s)", res2, esp_err_to_name(res2));
    }
    // Wait 10 seconds
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}

// Mender congiguration and callbacks
mender_client_config_t mender_config;
mender_client_callbacks_t mender_callbacks;
mender_keystore_t mender_identity[2];
char mender_mac_address[18];

// --- Callbacks
static mender_err_t mender_network_connect_cb(void) { return MENDER_OK; }
static mender_err_t mender_network_release_cb(void) { return MENDER_OK; }
static mender_err_t mender_auth_failure_cb(void) { return MENDER_OK; }

static mender_err_t mender_deployment_status_cb(mender_deployment_status_t status, char *desc)
{
    ESP_LOGI("MENDER", "Deployment status: %s", desc ? desc : "Unknown");
    return MENDER_OK;
}

static mender_err_t mender_auth_success_cb(void)
{
    ESP_LOGI("MENDER", "Authentication successful with the server");
    // This is VITAL: it tells the ESP32 that the firmware works and should not revert to the previous version
    // return mender_flash_confirm_image();
    return MENDER_OK;
}

static mender_err_t mender_restart_cb(void)
{
    ESP_LOGI("MENDER", "Restarting system as requested by OTA...");
    esp_restart();
    return MENDER_OK;
}

// --- Diagnostic function: Checks if we are connected to Wi-Fi
bool perform_health_check()
{
    wifi_ap_record_t ap_info;
    // If we can read the AP info, we are connected
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
    {
        ESP_LOGI("HEALTH", "Connected to AP with RSSI: %d", ap_info.rssi);
        return true;
    }
    ESP_LOGE("HEALTH", "Wi-Fi connection failed.");
    return false;
}

// --- Survival logic
void check_and_commit_ota()
{
    esp_ota_img_states_t ota_state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK)
    {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY)
        {
            ESP_LOGI("OTA", "Image in trial period. Starting self-diagnosis...");
            if (perform_health_check())
            {
                ESP_LOGI("OTA", "Health verified. Marking firmware as VALID.");
                esp_ota_mark_app_valid_cancel_rollback();
            }
            else
            {
                ESP_LOGE("OTA", "Health check failed. Forcing Rollback and restarting...");
                esp_ota_mark_app_invalid_rollback_and_reboot();
            }
        }
    }
}

// MAIN
void app_main()
{
    // Initialize I2C
    ESP_ERROR_CHECK(i2cdev_init());
    ESP_ERROR_CHECK(si7021_init_desc(&dev, 0, 10, 8));

    // Initialize WiFi
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    wifi_init_sta(); // Connect to network

    vTaskDelay(pdMS_TO_TICKS(10000)); // Wait a bit for WiFi to connect before starting Mender or use Event Groups

    // Check OTA status and commit if we are in the trial period
    check_and_commit_ota();
    // --- START MENDER CONFIGURATION ---
    // 1. Get the MAC address (Mender requires it as a unique identifier)
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    sprintf(mender_mac_address, "%02x:%02x:%02x:%02x:%02x:%02x",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    // 2. Identity
    mender_identity[0].name = "mac";
    mender_identity[0].value = mender_mac_address;
    mender_identity[1].name = NULL;
    mender_identity[1].value = NULL;
    // 3. Configure parameters
    mender_config.identity = mender_identity;
    mender_config.artifact_name = VERSION;
    mender_config.device_type = "esp32c3";
    mender_config.host = CONFIG_MENDER_SERVER_HOST;
    mender_config.tenant_token = CONFIG_MENDER_SERVER_TENANT_TOKEN;
    mender_config.authentication_poll_interval = 60;
    mender_config.update_poll_interval = 60;
    mender_config.recommissioning = false;
    // 4. Define mandatory callbacks
    mender_callbacks.network_connect = mender_network_connect_cb;
    mender_callbacks.network_release = mender_network_release_cb;
    mender_callbacks.authentication_success = mender_auth_success_cb;
    mender_callbacks.authentication_failure = mender_auth_failure_cb;
    mender_callbacks.deployment_status = mender_deployment_status_cb;
    mender_callbacks.restart = mender_restart_cb;
    // 5. Start the Mender client in the background
    ESP_LOGI("MENDER", "Starting Mender client with MAC: %s", mender_mac_address);
    if (mender_client_init(&mender_config, &mender_callbacks) == MENDER_OK)
    {
        mender_client_activate(); // This starts Mender in the background!
    }
    else
    {
        ESP_LOGE("MENDER", "Failed to initialize Mender");
    }
    // --- END MENDER CONFIGURATION ---

    // Configure partition and version
    const esp_partition_t *running = esp_ota_get_running_partition();
    printf("--- SYSTEM STARTED ---\n");
    printf("Firmware Version: %s\n", VERSION);
    printf("Running from partition: %s\n", running->label);
    printf("Offset Address: 0x%08" PRIx32 "\n", running->address);

    // Start MQTT
    // Wait a bit before starting MQTT or use Event Groups
    // A simple solution for the lab is a small delay,
    // although the ideal is to register a handler for IP_EVENT_STA_GOT_IP
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_mqtt_client_handle_t client = mqtt_app_start();

    
    // Create reading task
    xTaskCreate(collect_data_task, "collect_data_task", 4096, (void*) client, 5, NULL);

}

#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h> 
#include <esp_log.h>
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

static const char *TAG = "actuator-node";

#define VERSION "1.0.1"

#define ACTUATOR_GPIO 2
#define ACTUATOR_ACTIVE_LOW true 
#define TOPIC_STATUS "sed/G03/auto-irrigation-system/status"
#define TOPIC_TEMP "sed/G03/auto-irrigation-system/sensor/temp"
#define TOPIC_HUM "sed/G03/auto-irrigation-system/sensor/hum"
#define TOPIC_ACTION "sed/G03/auto-irrigation-system/actuator/action"

// Set thresholsd values remotly from NodeRed dashboard
#define TOPIC_SET_TEMP "sed/G03/auto-irrigation-system/actuator/temp"
#define TOPIC_SET_HUM "sed/G03/auto-irrigation-system/actuator/hum"

// Thresholds for decision making
#define TEMP_THRESHOLD 22.0f
#define HUM_THRESHOLD 50.0f

static float temp_threshold = TEMP_THRESHOLD;
static float hum_threshold = HUM_THRESHOLD;

static float g_last_temp = 0.0f;
static float g_last_hum = 0.0f;
static bool g_have_temp = false;
static bool g_have_hum = false;

bool turn_on = false;

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

    // Configure SSID and Password from menuconfig
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
        ESP_LOGI(TAG, "Actuator node connected to Broker");
        // Publish "Online" message for consistency with LWT
        esp_mqtt_client_publish(client, TOPIC_STATUS, "Online", 0, 1, 1);

        // Subscribe to temperature and humidity sensor topics
        esp_mqtt_client_subscribe(client, TOPIC_TEMP, 1);
        esp_mqtt_client_subscribe(client, TOPIC_HUM, 1);

        // Subscribe to actuator set thresholds topics
        esp_mqtt_client_subscribe(client, TOPIC_SET_TEMP, 1);
        esp_mqtt_client_subscribe(client, TOPIC_SET_HUM, 1);

        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "Message received in Topic: %.*s", event->topic_len, event->topic);
        // ESP_LOGI(TAG, "Data: %.*s", event->data_len, event->data);

        // Copy payload to a null-terminated buffer
        char buf[32];
        int len = event->data_len < (int)sizeof(buf) - 1 ? event->data_len : (int)sizeof(buf) - 1;
        memcpy(buf, event->data, len);
        buf[len] = '\0';


        // Convert to float with validation 
        char *endptr = NULL;
        float value = strtof(buf, &endptr);

        if (endptr == buf)
        {
            ESP_LOGW(TAG, "Payload is not a valid float: '%s'", buf);
            break;
        }

        // Determine which topic the message was for (topic is not NUL-terminated)
        if (event->topic_len == (int)strlen(TOPIC_TEMP) && strncmp(event->topic, TOPIC_TEMP, event->topic_len) == 0)
        {
            g_last_temp = value;
            g_have_temp = true;
            ESP_LOGI(TAG, "Temperature received: %.2f", g_last_temp);
        }
        else if (event->topic_len == (int)strlen(TOPIC_HUM) && strncmp(event->topic, TOPIC_HUM, event->topic_len) == 0)
        {
            g_last_hum = value;
            g_have_hum = true;
            ESP_LOGI(TAG, "Humidity received: %.2f", g_last_hum);
        }
        else if (event->topic_len == (int)strlen(TOPIC_SET_HUM) && strncmp(event->topic, TOPIC_SET_HUM, event->topic_len) == 0)
        {
            hum_threshold = value;
            ESP_LOGI(TAG, "Humidity threshold set to: %.2f", hum_threshold);
        }
        else if (event->topic_len == (int)strlen(TOPIC_SET_TEMP) && strncmp(event->topic, TOPIC_SET_TEMP, event->topic_len) == 0)
        {
            temp_threshold = value;
            ESP_LOGI(TAG, "Temperature threshold set to: %.2f", temp_threshold);
        }
        else
        {
            ESP_LOGI(TAG, "Message on unrelated topic: %.*s", event->topic_len, event->topic);
            break;
        }

        /*
        Decision making:
        If humidity is available and it's below 40.0%
            turn on the actuator.
        If humidity is not available
            but temperature is above 22.0°C
            turn on the actuator
        */

        // Turn on when humidity below 40.0
        if (g_have_hum)
        {
            if (g_last_hum < hum_threshold)
            {
                turn_on = true;
            }
            else
            {
                turn_on = false;
            }
        }
        // If humidity is not available check temperature
        else
        {
            if (g_have_temp && g_last_temp > temp_threshold)
            {
                turn_on = true;
            }
            else
            {
                turn_on = false;
            }
        }

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
        .session.keepalive = 10, // Detección rápida de desconexión
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
    return client;
}

// --- Actuator control task and MQTT publishing of action state
void action_relay_task(void *pvParameters)
{
    esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t) pvParameters;
    char payload[16];

    while (1)
    {
        gpio_set_level(ACTUATOR_GPIO, turn_on ? 1 : 0);
        ESP_LOGI(TAG, "Actuator %s (temp=%.2f, hum=%.2f)", turn_on ? "ON" : "OFF", g_have_temp ? g_last_temp : -1.0f, g_have_hum ? g_last_hum : -1.0f);
        
        if (turn_on)
        {
            snprintf(payload, sizeof(payload), "%.2f", turn_on ? 1.0 : 0.0);
        }
        else
        {
            snprintf(payload, sizeof(payload), "%.2f", turn_on ? 1.0 : 0.0);
        }
        // Publish action
        if (client != NULL) {
            esp_mqtt_client_publish(client, TOPIC_ACTION, payload, 0, 1, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(10000)); // Wait 10 seconds before checking again (or reacting to new data) to avoid rapid toggling
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
void app_main(void)
{
    // Configurar GPIO
    gpio_reset_pin(ACTUATOR_GPIO);
    gpio_set_direction(ACTUATOR_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(ACTUATOR_GPIO, 0); //

    // Iniciar WiFi
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    wifi_init_sta(); // Conectamos a la red SED

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
    mender_config.device_type = "esp32";
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


    // Init MQTT
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_mqtt_client_handle_t client = mqtt_app_start();

    // Dejar la app corriendo
    // while (1)
    // {
    //     vTaskDelay(pdMS_TO_TICKS(10000));
    // }

    xTaskCreate(action_relay_task, "action_relay_task", 4096, (void*) client, 5, NULL);
}

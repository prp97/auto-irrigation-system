#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_task_wdt.h>
#include <nvs_flash.h>
#include <esp_ota_ops.h>
#include <inttypes.h>

#include "wifi_manager.h"
#include "ota_updates_manager.h"
#include "mqtt_manager.h"
#include "sensor_manager.h"

static const char *TAG = "sensor-node";

#define VERSION "s_1.0.0"

// MAIN
void app_main()
{
    init_sensor_gpio(); // Initialize I2C and sensor descriptor

    // Configure partition and version
    const esp_partition_t *running = esp_ota_get_running_partition();
    printf("--- SYSTEM STARTED ---\n");
    printf("Firmware Version: %s\n", VERSION);
    printf("Running from partition: %s\n", running->label);
    printf("Offset Address: 0x%08" PRIx32 "\n", running->address);

    // Initialize WiFi
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init_sta(); // Connect to network

    // Wait for WiFi connection with verification
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    vTaskDelay(pdMS_TO_TICKS(10000)); // Initial wait

    // Init OTA with Mender
    ota_updates_init(VERSION);

    // Start MQTT
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_mqtt_client_handle_t client = mqtt_app_start();

    // Configure watchdog: reboot if a task hangs for more than 30s
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 30000,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    esp_task_wdt_reconfigure(&wdt_config);

    // Create queue: holds up to 5 sensor readings
    sensor_queue = xQueueCreate(5, sizeof(sensor_data_t));

    // Task 1: reads the sensor every 10s -> puts data in queue
    xTaskCreate(read_sensor_task, "read_sensor_task", 4096, NULL, 5, NULL);

    // Task 2: waits for data in queue -> publishes via MQTT
    xTaskCreate(publish_task, "publish_task", 4096, (void *)client, 5, NULL);
}

#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_task_wdt.h>
#include <nvs_flash.h>
#include <mqtt_client.h>
#include <esp_ota_ops.h>
#include <inttypes.h>

// Custom modules
#include "wifi_manager.h"
#include "ota_updates_manager.h"
#include "mqtt_manager.h"
#include "actuator_manager.h"

#define VERSION "a_1.0.1"

void app_main(void)
{
    // Configure GPIO
    init_actuator_gpio();

    // Create mutex to protect shared actuator state
    actuator_mutex = xSemaphoreCreateMutex();
    if (actuator_mutex == NULL)
    {
        printf("ERROR: Failed to create actuator mutex\n");
        return;
    }

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

    vTaskDelay(pdMS_TO_TICKS(10000)); // Wait a bit for WiFi to connect before starting Mender or use Event Groups

    // Init OTA with Mender
    ota_updates_init(VERSION);

    // Init MQTT
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_mqtt_client_handle_t client = mqtt_app_start();

    xTaskCreate(action_relay_task, "action_relay_task", 4096, (void *)client, 5, NULL);
    
    xTaskCreate(survival_monitor_task, "survival_monitor_task", 4096, (void *)client, 5, NULL);
}

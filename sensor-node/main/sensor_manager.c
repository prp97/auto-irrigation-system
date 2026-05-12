#include "sensor_manager.h"
#include "mqtt_manager.h"
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <si7021.h>
#include <i2cdev.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

i2c_dev_t dev = {0};
QueueHandle_t sensor_queue = NULL;

static const char *TAG = "sensor-node";

void init_sensor_gpio(void)
{
    ESP_ERROR_CHECK(i2cdev_init());
    ESP_ERROR_CHECK(si7021_init_desc(&dev, 0, 10, 8));
}

// --- Task 1: reads the sensor and puts data into the queue
void read_sensor_task(void *pvParameters)
{
    esp_task_wdt_add(NULL); // register this task in the watchdog

    sensor_data_t data;

    while (1)
    {
        esp_err_t res1 = si7021_measure_temperature(&dev, &data.temperature);
        esp_err_t res2 = si7021_measure_humidity(&dev, &data.humidity);

        if (res1 == ESP_OK && res2 == ESP_OK)
        {
            ESP_LOGI(TAG, "Sensor read OK: %.2f C, %.2f %%", data.temperature, data.humidity);
            // Put data in queue don't wait if queue is full (0 = no wait)
            if (xQueueSend(sensor_queue, &data, 0) != pdTRUE)
            {
                ESP_LOGW(TAG, "Queue full, dropping reading");
            }
        }
        else
        {
            ESP_LOGE(TAG, "Sensor read error: temp=%s, hum=%s",
                     esp_err_to_name(res1), esp_err_to_name(res2));
        }

        esp_task_wdt_reset(); // tell watchdog "still alive"
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

// --- Task 2: takes data from the queue and publishes via MQTT
void publish_task(void *pvParameters)
{
    esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t)pvParameters;
    esp_task_wdt_add(NULL); // register this task in the watchdog

    sensor_data_t data;
    char payload[16];

    while (1)
    {
        // Wait up to 15s for a new reading
        if (xQueueReceive(sensor_queue, &data, pdMS_TO_TICKS(15000)) == pdTRUE)
        {
            if (client != NULL)
            {
                snprintf(payload, sizeof(payload), "%.2f", data.humidity);
                esp_mqtt_client_publish(client, TOPIC_HUM, payload, 0, 1, 0);

                snprintf(payload, sizeof(payload), "%.2f", data.temperature);
                esp_mqtt_client_publish(client, TOPIC_TEMP, payload, 0, 1, 0);

                ESP_LOGI(TAG, "Published: temp=%.2f, hum=%.2f", data.temperature, data.humidity);
            }
        }

        esp_task_wdt_reset(); // tell watchdog "still alive"
    }
}

#include "actuator_manager.h"
#include "mqtt_manager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <driver/gpio.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <stdio.h>

static const char *TAG = "actuator-node";

SemaphoreHandle_t actuator_mutex = NULL;

static float temp_threshold = TEMP_THRESHOLD;
static float hum_threshold = HUM_THRESHOLD;

static float g_last_temp = 0.0f;
static float g_last_hum = 0.0f;
static bool g_have_temp = false;
static bool g_have_hum = false;
static int64_t g_last_sensor_data_us = 0;

bool turn_on = false;

void init_actuator_gpio(void)
{
    gpio_reset_pin(ACTUATOR_GPIO);
    gpio_set_direction(ACTUATOR_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(ACTUATOR_GPIO, 0);
}

void update_actuator_state(void)
{
    /*
    Decision making:
    If humidity is available and it's below 20.0%
        turn on the actuator.
    If humidity is not available
        but temperature is above 22.0°C
        turn on the actuator
    */

    // Turn on when humidity below 20.0
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
}

void update_actuator_temperature(float temp)
{
    if (xSemaphoreTake(actuator_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        g_last_temp = temp;
        g_have_temp = true;
        g_last_sensor_data_us = esp_timer_get_time();
        xSemaphoreGive(actuator_mutex);
    }
    ESP_LOGI(TAG, "Temperature updated: %.2f °C", temp);
    update_actuator_state();
}

void update_actuator_humidity(float hum)
{
    if (xSemaphoreTake(actuator_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        g_last_hum = hum;
        g_have_hum = true;
        g_last_sensor_data_us = esp_timer_get_time();
        xSemaphoreGive(actuator_mutex);
    }
    ESP_LOGI(TAG, "Humidity updated: %.2f %%", hum);
    update_actuator_state();
}

void set_actuator_temp_threshold(float threshold)
{
    temp_threshold = threshold;
    ESP_LOGI(TAG, "Temperature threshold set to: %.2f °C", threshold);
    update_actuator_state();
}

void set_actuator_hum_threshold(float threshold)
{
    hum_threshold = threshold;
    ESP_LOGI(TAG, "Humidity threshold set to: %.2f %%", threshold);
    update_actuator_state();
}

void invalidate_actuator_data(void)
{
    if (xSemaphoreTake(actuator_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        if (g_have_temp || g_have_hum)
        {
            ESP_LOGW(TAG, "Invalidating sensor data");
            g_have_temp = false;
            g_have_hum = false;
            turn_on = false;
        }
        xSemaphoreGive(actuator_mutex);
    }
}

bool get_actuator_state(void)
{
    return turn_on;
}

float get_actuator_last_temp(void)
{
    return g_last_temp;
}

float get_actuator_last_hum(void)
{
    return g_last_hum;
}

bool has_actuator_temp(void)
{
    return g_have_temp;
}

bool has_actuator_hum(void)
{
    return g_have_hum;
}

int64_t get_actuator_last_data_time(void)
{
    return g_last_sensor_data_us;
}

void survival_monitor_task(void *pvParameters)
{
    esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t)pvParameters;
    int64_t last_no_data_reported_sec = -1;
    bool was_in_no_data_state = false;

    while (1)
    {

        bool is_mqtt_connected = mqtt_connected();
        int64_t local_last_data_us;

        if (xSemaphoreTake(actuator_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            local_last_data_us = g_last_sensor_data_us;
            xSemaphoreGive(actuator_mutex);
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        if (is_mqtt_connected && client != NULL && local_last_data_us > 0)
        {
            int64_t elapsed_us = esp_timer_get_time() - local_last_data_us;
            int64_t elapsed_sec = elapsed_us / 1000000;

            if (elapsed_sec >= NO_DATA_TIMEOUT_SEC)
            {
                invalidate_actuator_data();

                if (elapsed_sec >= last_no_data_reported_sec + NO_DATA_REPORT_PERIOD_SEC)
                {
                    publish_elapsed_seconds(client, elapsed_us);
                    last_no_data_reported_sec = elapsed_sec;
                    was_in_no_data_state = true;
                }
            }
            else if (was_in_no_data_state)
            {
                // Sensor data recovered: clear the survival indicator on the dashboard
                esp_mqtt_client_publish(client, TOPIC_SURVIVAL, "null", 0, 1, 0);
                was_in_no_data_state = false;
                last_no_data_reported_sec = -1;
                ESP_LOGI(TAG, "Sensor data recovered, survival alert cleared");
            }
        }

        if (!is_mqtt_connected)
        {
            last_no_data_reported_sec = -1;
            was_in_no_data_state = false;
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

// --- Actuator control task and MQTT publishing of action state
void action_relay_task(void *pvParameters)
{
    esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t)pvParameters;
    char payload[16];
    bool local_turn_on;
    float local_temp, local_hum;
    bool local_have_temp, local_have_hum;

    while (1)
    {
        // Read shared state safely with mutex
        if (xSemaphoreTake(actuator_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            local_turn_on = turn_on;
            local_temp = g_last_temp;
            local_hum = g_last_hum;
            local_have_temp = g_have_temp;
            local_have_hum = g_have_hum;
            xSemaphoreGive(actuator_mutex);
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }

        gpio_set_level(ACTUATOR_GPIO, local_turn_on ? 1 : 0);
        ESP_LOGI(TAG, "Actuator %s (temp=%.2f, hum=%.2f)",
                 local_turn_on ? "ON" : "OFF",
                 local_have_temp ? local_temp : -1.0f,
                 local_have_hum ? local_hum : -1.0f);

        snprintf(payload, sizeof(payload), "%.2f", local_turn_on ? 1.0 : 0.0);

        if (client != NULL)
        {
            esp_mqtt_client_publish(client, TOPIC_ACTION, payload, 0, 0, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

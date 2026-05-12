#include "mqtt_manager.h"
#include "actuator_manager.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <string.h>
#include <inttypes.h>

static const char *TAG = "actuator-node";

static bool g_mqtt_connected = false;

void publish_elapsed_seconds(esp_mqtt_client_handle_t client, int64_t elapsed_us)
{
    char payload[64];
    int64_t elapsed_sec = elapsed_us / 1000000;
    snprintf(payload, sizeof(payload), "{\"no_sensor_data_seconds\":%" PRId64 "}", elapsed_sec);
    ESP_LOGW(TAG, "No sensor data for %" PRId64 " seconds", elapsed_sec);
    esp_mqtt_client_publish(client, TOPIC_SURVIVAL, payload, 0, 1, 0);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Actuator node connected to Broker");
        g_mqtt_connected = true;

        int64_t last_data_time = get_actuator_last_data_time();

        if (last_data_time > 0)
        {
            int64_t no_data_for_us = esp_timer_get_time() - last_data_time;
            publish_elapsed_seconds(client, no_data_for_us);
        }

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
            update_actuator_temperature(value);
        }
        else if (event->topic_len == (int)strlen(TOPIC_HUM) && strncmp(event->topic, TOPIC_HUM, event->topic_len) == 0)
        {
            update_actuator_humidity(value);
        }
        else if (event->topic_len == (int)strlen(TOPIC_SET_HUM) && strncmp(event->topic, TOPIC_SET_HUM, event->topic_len) == 0)
        {
            set_actuator_hum_threshold(value);
        }
        else if (event->topic_len == (int)strlen(TOPIC_SET_TEMP) && strncmp(event->topic, TOPIC_SET_TEMP, event->topic_len) == 0)
        {
            set_actuator_temp_threshold(value);
        }
        else
        {
            ESP_LOGI(TAG, "Message on unrelated topic: %.*s", event->topic_len, event->topic);
            break;
        }

        update_actuator_state();

        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected from Broker");
        g_mqtt_connected = false;
        invalidate_actuator_data();
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "Error on MQTT stack");
        break;

    default:
        break;
    }
}

esp_mqtt_client_handle_t mqtt_app_start(void)
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
        .session.keepalive = 10, // detection of disconnection
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
    return client;
}

bool mqtt_connected(void)
{
    return g_mqtt_connected;
}
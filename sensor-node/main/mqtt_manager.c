#include "mqtt_manager.h"
#include <esp_log.h>
#include <string.h>

static const char *TAG = "sensor-node";

static bool g_mqtt_connected = false;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Sensor node connected to Broker");
        
        g_mqtt_connected = true;
        
        // Publish "Online" message for consistency with LWT
        esp_mqtt_client_publish(client, TOPIC_STATUS, "Online", 0, 1, 1);
        
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Sensor node disconnected from Broker");
        
        g_mqtt_connected = false;
        
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
        .session.keepalive = 10, // Quick disconnection detection
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

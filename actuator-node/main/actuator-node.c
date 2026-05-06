#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h> // Librería de Timers
#include <esp_log.h>
#include <si7021.h>
#include <i2cdev.h>

#include "esp_console.h"
#include "argtable3/argtable3.h"

#include "freertos/queue.h"

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <nvs_flash.h>
#include <mqtt_client.h>
#include <stdlib.h>

static const char *TAG = "actuator-node";
#define ACTUATOR_GPIO 2
#define TOPIC_STATUS "sed/G03/status"
#define TOPIC_TEMP "sed/G03/sensor/temp"

void wifi_init_sta(void)
{
    // Inicializar la pila de red y el bucle de eventos
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // Configuración por defecto del WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Configurar SSID y Password desde el menuconfig
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .password = CONFIG_ESP_WIFI_PASSWORD,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // Arrancar el WiFi
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI("WIFI", "Conectando a %s...", CONFIG_ESP_WIFI_SSID);
    esp_wifi_connect();
}

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
        // Subscribe to temperature topic
		esp_mqtt_client_subscribe(client, TOPIC_TEMP, 1);
		break;

	case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "Message received in Topic: %.*s", event->topic_len, event->topic);
        ESP_LOGI(TAG, "Data: %.*s", event->data_len, event->data);

        /* Copy payload to a null-terminated buffer */
        char buf[32];
        int len = event->data_len < (int)sizeof(buf) - 1 ? event->data_len : (int)sizeof(buf) - 1;
        memcpy(buf, event->data, len);
        buf[len] = '\0';

        /* Convert to float with validation */
        char *endptr = NULL;
        float temp = strtof(buf, &endptr);
        if (endptr == buf) {
            ESP_LOGW(TAG, "Payload is not a valid float: '%s'", buf);
            break;
        }

        ESP_LOGI(TAG, "Temperature received: %.2f", temp);

        /* Simple action: activate actuator if temp < 22.0 */
        if (temp < 22.0f) {
            gpio_set_level(ACTUATOR_GPIO, 1);
            ESP_LOGI(TAG, "Actuator ON");
        } else {
            gpio_set_level(ACTUATOR_GPIO, 0);
            ESP_LOGI(TAG, "Actuator OFF");
        }
        break;
    
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "Error en el stack MQTT");
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

void app_main(void)
{
	// Configurar GPIO
	gpio_reset_pin(ACTUATOR_GPIO);
	gpio_set_direction(ACTUATOR_GPIO, GPIO_MODE_OUTPUT);
	gpio_set_level(ACTUATOR_GPIO, 0);


    // Iniciar WiFi
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    wifi_init_sta(); // Conectamos a la red SED


	// Iniciar MQTT
	vTaskDelay(pdMS_TO_TICKS(5000));
    esp_mqtt_client_handle_t client = mqtt_app_start();

	// Dejar la app corriendo
	while (1) {
		vTaskDelay(pdMS_TO_TICKS(10000));
	}
}

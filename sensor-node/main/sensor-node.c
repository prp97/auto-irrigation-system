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

static const char *TAG = "sensor-node";

// Sustituye GXX por tu grupo, p.e. G01:
#define LED_GPIO 2
#define TOPIC_STATUS "sed/G03/status"
#define TOPIC_LED "sed/G03/actuador/led"
#define TOPIC_TEMP "sed/G03/sensor/temp"

static i2c_dev_t dev = {0}; // Variable global para acceder desde el callback

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
        ESP_LOGI(TAG, "Conectado al Broker");
        // Publicar mensaje "Online" para coherencia con LWT
        esp_mqtt_client_publish(client, TOPIC_STATUS, "Online", 0, 1, 1);
        // Suscribirse al tópico del LED
        esp_mqtt_client_subscribe(client, TOPIC_LED, 1);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "Mensaje recibido en Tópico: %.*s", event->topic_len, event->topic);
        ESP_LOGI(TAG, "Datos: %.*s", event->data_len, event->data);

        // Procesar comando para el LED
        if (strncmp(event->data, "ON", event->data_len) == 0)
        {
            gpio_set_level(LED_GPIO, 1);
            ESP_LOGI(TAG, "LED encendido");
        }
        else if (strncmp(event->data, "OFF", event->data_len) == 0)
        {
            gpio_set_level(LED_GPIO, 0);
            ESP_LOGI(TAG, "LED apagado");
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

void task_lectura(void *pvParameters) {
  // Recuperamos el cliente MQTT pasado desde app_main
  esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t) pvParameters;
  char payload[16]; // Buffer para el texto de la temperatura
  // Código de lectura de temperatura y humedad
  
  float temperature = 25.0;
  float humidity = 50.0;
  esp_err_t res1 = ESP_OK;
  esp_err_t res2 = ESP_OK;

  while (1) {
    // Leer valores del sensor
    res1 = si7021_measure_temperature(&dev, &temperature);
    res2 = si7021_measure_humidity(&dev, &humidity);

    if (res1 == ESP_OK && res2 == ESP_OK) {
      ESP_LOGI(TAG, "Temperatura: %.2f C, Humedad: %.2f %%", temperature, humidity);
      // Convertir float a string
      snprintf(payload, sizeof(payload), "%.2f", temperature);            
      // PUBLICAR: Si el cliente existe, enviamos el dato
      if (client != NULL) {
        esp_mqtt_client_publish(client, TOPIC_TEMP, payload, 0, 1, 0);
      }
      // =====================================================
    } else {
      ESP_LOGE(TAG, "Error leyendo la temperatura: %d (%s)", res1, esp_err_to_name(res1));
      ESP_LOGE(TAG, "Error leyendo la humedad: %d (%s)", res2, esp_err_to_name(res2));
    }
    // Esperar 10 segundos
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}


void app_main()
{
    // Iniciar I2C y configurar el LED
    ESP_ERROR_CHECK(i2cdev_init());
    ESP_ERROR_CHECK(si7021_init_desc(&dev, 0, 10, 8));

    // Configuramos el GPIO2 para visualizar alarma en LED:
    gpio_reset_pin(GPIO_NUM_2);                       // Ejercicio: Dónde está definido GPIO_NUM_2?
    gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT); // Y GPIO_MODE_OUTPUT?
    // ...

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
    // Esperar un poco antes de iniciar MQTT o usar Event Groups
    // Una solución sencilla para el lab es un pequeño retardo,
    // aunque lo ideal es registrar un handler para IP_EVENT_STA_GOT_IP
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_mqtt_client_handle_t client = mqtt_app_start();

    // Crear tarea de lectura
    xTaskCreate(task_lectura, "task_lectura", 4096, (void*) client, 5, NULL);

}

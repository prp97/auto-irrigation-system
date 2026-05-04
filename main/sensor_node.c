
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

static float temp_threshold = 35.0; // Umbral por defecto

// Estructura para parsear argumentos del comando
static struct {
    struct arg_dbl *val;
    struct arg_end *end;
} threshold_args;


typedef struct {
    float temperature;
    float humidity;
} sensor_data_t;

static QueueHandle_t sensor_queue;

static const char *TAG = "timer_demo";
static i2c_dev_t dev = { 0 }; // Variable global para acceder desde el callback

// Función Callback del Timer
static void timer_callback(void* arg) {
  sensor_data_t data;
  esp_err_t res1 = si7021_measure_temperature(&dev, &data.temperature);
  esp_err_t res2 = si7021_measure_humidity(&dev, &data.humidity);  
  if (res1 == ESP_OK && res2 == ESP_OK) {
    xQueueSend(sensor_queue, &data, 0);
  }
}

// Función que se ejecuta al escribir "set_threshold <valor>"
static int do_set_threshold(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **) &threshold_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, threshold_args.end, argv[0]);
        return 1;
    }
    temp_threshold = (float)threshold_args.val->dval[0];
    printf("Nuevo umbral configurado: %.2f C\n", temp_threshold);
    return 0;
}

// Esta tarea estará bloqueada esperando a que lleguen datos a la cola. 
// Cuando llegan, los procesa (comprueba umbrales, imprime, etc.).
void sensor_processing_task(void *pvParameters) {
  sensor_data_t received_data;
  while (1) {
    // Espera infinita hasta que llegue algo a la cola
    if (xQueueReceive(sensor_queue, &received_data, portMAX_DELAY)) {
      // Aquí va la lógica de control
      if (received_data.temperature > temp_threshold) {
        ESP_LOGI(TAG, "%.2f C > %.2f C %%", received_data.temperature, temp_threshold);
        gpio_set_level(GPIO_NUM_2, 1);
      } else {
        ESP_LOGI(TAG, "%.2f C < %.2f C %%", received_data.temperature, temp_threshold);
        gpio_set_level(GPIO_NUM_2, 0);
      }
    }
  }
}

void register_commands() {
    threshold_args.val = arg_dbl1(NULL, NULL, "<t>", "Umbral de temperatura");
    threshold_args.end = arg_end(1);

    const esp_console_cmd_t cmd = {
        .command = "set_threshold",
        .help = "Configura el umbral de la alarma LED",
        .hint = NULL,
        .func = &do_set_threshold,
        .argtable = &threshold_args
    };
    esp_console_cmd_register(&cmd);
}

void sensor_node_init() {
  ESP_ERROR_CHECK(i2cdev_init());
  ESP_ERROR_CHECK(si7021_init_desc(&dev, 0, 21, 22));

  // -- 3.6 ---
  // Configuramos el GPIO2 para visualizar alarma en LED:
  gpio_reset_pin(GPIO_NUM_2); // Ejercicio: Dónde está definido GPIO_NUM_2?
  gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT); // Y GPIO_MODE_OUTPUT?
  
  // Crear cola para 10 elementos de tipo sensor_data_t
  sensor_queue = xQueueCreate(10, sizeof(sensor_data_t));
  
  if (sensor_queue != NULL) {
  xTaskCreate(sensor_processing_task, "proc_task", 4096, NULL, 10, NULL);
  }

  // --- 3.5 --- 
  esp_console_repl_t *repl = NULL;
  esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
  esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
  register_commands(); // Registrar nuestros comandos
  printf("\n--- Consola Interactiva Lista ---\n");
  printf("Escribe 'help' para ver los comandos.\n");
  
  ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));
  ESP_ERROR_CHECK(esp_console_start_repl(repl));
  // ---

  // Definición de argumentos del timer
  const esp_timer_create_args_t periodic_timer_args = {
    .callback = &timer_callback,
    .name = "sensor_timer"
  };

  esp_timer_handle_t periodic_timer;
  ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));

  // Arrancar el timer periódico (en microsegundos). 1000000 us = 1 segundo
  ESP_LOGI(TAG, "Iniciando timer de 1 segundo...");
  ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 1000000));
}
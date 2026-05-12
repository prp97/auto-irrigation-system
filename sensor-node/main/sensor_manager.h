#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <i2cdev.h>
#include <mqtt_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

extern i2c_dev_t dev;

extern QueueHandle_t sensor_queue;

typedef struct
{
    float temperature;
    float humidity;
} sensor_data_t;

void init_sensor_gpio(void);

void read_sensor_task(void *pvParameters);

void publish_task(void *pvParameters);

#endif // SENSOR_MANAGER_H

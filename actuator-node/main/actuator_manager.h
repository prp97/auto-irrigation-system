#ifndef ACTUATOR_MANAGER_H
#define ACTUATOR_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include <mqtt_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define ACTUATOR_GPIO 2

// Thresholds for decision making
#define TEMP_THRESHOLD 22.0f
#define HUM_THRESHOLD 20.0f

#define NO_DATA_TIMEOUT_SEC 30
#define NO_DATA_REPORT_PERIOD_SEC 10

extern SemaphoreHandle_t actuator_mutex;

void init_actuator_gpio(void);
void update_actuator_state(void);
void update_actuator_temperature(float temp);
void update_actuator_humidity(float hum);
void set_actuator_temp_threshold(float threshold);
void set_actuator_hum_threshold(float threshold);
void invalidate_actuator_data(void);
bool get_actuator_state(void);
float get_actuator_last_temp(void);
float get_actuator_last_hum(void);
bool has_actuator_temp(void);
bool has_actuator_hum(void);
int64_t get_actuator_last_data_time(void);

void survival_monitor_task(void *pvParameters);
void action_relay_task(void *pvParameters);

#endif // ACTUATOR_MANAGER_H

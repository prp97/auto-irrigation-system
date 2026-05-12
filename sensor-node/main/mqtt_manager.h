#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <mqtt_client.h>
#include <stdbool.h>

// MQTT Topics
#define TOPIC_STATUS "sed/G03/auto-irrigation-system/status"
#define TOPIC_TEMP "sed/G03/auto-irrigation-system/sensor/temp"
#define TOPIC_HUM "sed/G03/auto-irrigation-system/sensor/hum"

esp_mqtt_client_handle_t mqtt_app_start(void);

bool mqtt_connected(void);

#endif // MQTT_MANAGER_H

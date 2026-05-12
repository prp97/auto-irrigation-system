#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <mqtt_client.h>

// MQTT Topics
#define TOPIC_STATUS "sed/G03/auto-irrigation-system/status"
#define TOPIC_TEMP "sed/G03/auto-irrigation-system/sensor/temp"
#define TOPIC_HUM "sed/G03/auto-irrigation-system/sensor/hum"
#define TOPIC_SET_TEMP "sed/G03/auto-irrigation-system/actuator/temp"
#define TOPIC_SET_HUM "sed/G03/auto-irrigation-system/actuator/hum"
#define TOPIC_ACTION "sed/G03/auto-irrigation-system/actuator/action"
#define TOPIC_SURVIVAL "sed/G03/auto-irrigation-system/actuator/survival"

esp_mqtt_client_handle_t mqtt_app_start(void);

bool mqtt_connected(void);

void publish_elapsed_seconds(esp_mqtt_client_handle_t client, int64_t elapsed_us);

#endif // MQTT_MANAGER_H

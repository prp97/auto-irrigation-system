#ifndef OTA_UPDATES_MANAGER_H
#define OTA_UPDATES_MANAGER_H

#include <esp_err.h>

void check_and_commit_ota(void);

void ota_updates_init(const char *version);

#endif // OTA_UPDATES_MANAGER_H

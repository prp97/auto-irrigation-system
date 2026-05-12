#include "ota_updates_manager.h"
#include <stdio.h>
#include <string.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_ota_ops.h>
#include <esp_mac.h>
#include <esp_system.h>
#include "mender-client.h"
#include "mender-flash.h"

// Mender congiguration and callbacks

mender_client_config_t mender_config;
mender_client_callbacks_t mender_callbacks;
mender_keystore_t mender_identity[2];
char mender_mac_address[18];
static char mender_artifact_name[32]; // Buffer for version string

// --- Callbacks

static mender_err_t mender_network_connect_cb(void) { return MENDER_OK; }
static mender_err_t mender_network_release_cb(void) { return MENDER_OK; }
static mender_err_t mender_auth_failure_cb(void) { return MENDER_OK; }

static mender_err_t mender_deployment_status_cb(mender_deployment_status_t status, char *desc)
{
    ESP_LOGI("MENDER", "Deployment status: %s", desc ? desc : "Unknown");
    return MENDER_OK;
}

static mender_err_t mender_auth_success_cb(void)
{
    ESP_LOGI("MENDER", "Authentication successful with the server");
    
    // This is VITAL: it tells the ESP32 that the firmware works and should not revert to the previous version
    return mender_flash_confirm_image();
    // return MENDER_OK;
}

static mender_err_t mender_restart_cb(void)
{
    ESP_LOGI("MENDER", "Restarting system as requested by OTA...");
    esp_restart();
    return MENDER_OK;
}

// --- Diagnostic function: Checks if we are connected to Wi-Fi
bool perform_health_check()
{
    wifi_ap_record_t ap_info;
    
    // If we can read the AP info, we are connected
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
    {
        ESP_LOGI("HEALTH", "Connected to AP with RSSI: %d", ap_info.rssi);
    
        return true;
    }
    
    ESP_LOGE("HEALTH", "Wi-Fi connection failed.");
    
    return false;
}

// --- Survival logic
void check_and_commit_ota()
{
    esp_ota_img_states_t ota_state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK)
    {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY)
        {
            ESP_LOGI("OTA", "Image in trial period. Starting self-diagnosis...");
            if (perform_health_check())
            {
                ESP_LOGI("OTA", "Health verified. Marking firmware as VALID.");
                esp_ota_mark_app_valid_cancel_rollback();
            }
            else
            {
                ESP_LOGE("OTA", "Health check failed. Forcing Rollback and restarting...");
                esp_ota_mark_app_invalid_rollback_and_reboot();
            }
        }
    }
}

void ota_updates_init(const char *version)
{
    // Check OTA status and commit if we are in the trial period
    check_and_commit_ota();

    // --- START MENDER CONFIGURATION ---
    
    // 1. Get the MAC address (Mender requires it as a unique identifier)
    
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    sprintf(mender_mac_address, "%02x:%02x:%02x:%02x:%02x:%02x",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    // 2. Identity
    
    mender_identity[0].name = "mac";
    mender_identity[0].value = mender_mac_address;
    mender_identity[1].name = NULL;
    mender_identity[1].value = NULL;
    
    // 3. Configure parameters
    
    // Copy version to avoid const qualifier warning
    snprintf(mender_artifact_name, sizeof(mender_artifact_name), "%s", version);
    mender_config.identity = mender_identity;
    mender_config.artifact_name = mender_artifact_name;
    mender_config.device_type = "esp32c3";
    mender_config.host = CONFIG_MENDER_SERVER_HOST;
    mender_config.tenant_token = CONFIG_MENDER_SERVER_TENANT_TOKEN;
    mender_config.authentication_poll_interval = 60;
    mender_config.update_poll_interval = 60;
    mender_config.recommissioning = false;
    
    // 4. Define mandatory callbacks

    mender_callbacks.network_connect = mender_network_connect_cb;
    mender_callbacks.network_release = mender_network_release_cb;
    mender_callbacks.authentication_success = mender_auth_success_cb;
    mender_callbacks.authentication_failure = mender_auth_failure_cb;
    mender_callbacks.deployment_status = mender_deployment_status_cb;
    mender_callbacks.restart = mender_restart_cb;
    
    // 5. Start the Mender client in the background
    ESP_LOGI("MENDER", "Starting Mender client with MAC: %s", mender_mac_address);
    
    if (mender_client_init(&mender_config, &mender_callbacks) == MENDER_OK)
    {
        mender_client_activate(); // This starts Mender in the background!
    }
    else
    {
        ESP_LOGE("MENDER", "Failed to initialize Mender");
    }
}
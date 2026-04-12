#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "ble_keyboard.h"
#include "http_control.h"
#include "wifi_manager.h"

static const char *TAG = "keybridge";

void app_main(void)
{
    esp_err_t ret;

    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(TAG, "Starting Wi-Fi station");
    ESP_ERROR_CHECK(wifi_manager_init());

    ESP_LOGI(TAG, "Starting BLE keyboard");
    ESP_ERROR_CHECK(ble_keyboard_init());

    ESP_LOGI(TAG, "Starting HTTP control server");
    ESP_ERROR_CHECK(http_control_start());
}

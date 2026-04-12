#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/ip4_addr.h"
#include "lwip/apps/netbiosns.h"
#include "mdns.h"

#include "sdkconfig.h"
#include "wifi_manager.h"

static const char *TAG = "wifi_manager";
static bool s_wifi_connected;
static int s_retry_count;
static char s_ip_addr[16];
static esp_event_handler_instance_t s_wifi_any_id;
static esp_event_handler_instance_t s_wifi_got_ip;

static esp_err_t advertise_hostname(esp_netif_t *netif)
{
    mdns_txt_item_t service_txt[] = {
        {"path", "/"},
    };

    ESP_RETURN_ON_ERROR(esp_netif_set_hostname(netif, CONFIG_APP_MDNS_HOSTNAME), TAG, "failed to set STA hostname");
    ESP_RETURN_ON_ERROR(mdns_init(), TAG, "mdns init failed");
    ESP_RETURN_ON_ERROR(mdns_hostname_set(CONFIG_APP_MDNS_HOSTNAME), TAG, "mdns hostname failed");
    ESP_RETURN_ON_ERROR(mdns_instance_name_set(CONFIG_APP_BLE_DEVICE_NAME), TAG, "mdns instance failed");
    ESP_RETURN_ON_ERROR(
        mdns_service_add(CONFIG_APP_BLE_DEVICE_NAME, "_http", "_tcp", CONFIG_APP_HTTP_PORT, service_txt, 1),
        TAG,
        "mdns http service failed"
    );

    netbiosns_init();
    netbiosns_set_name(CONFIG_APP_MDNS_HOSTNAME);

    ESP_LOGI(TAG, "advertising http://%s.local:%d", CONFIG_APP_MDNS_HOSTNAME, CONFIG_APP_HTTP_PORT);
    return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected = false;
        s_ip_addr[0] = '\0';

        if (s_retry_count < CONFIG_APP_WIFI_MAXIMUM_RETRY) {
            ++s_retry_count;
            ESP_LOGW(TAG, "Wi-Fi disconnected, retry %d/%d", s_retry_count, CONFIG_APP_WIFI_MAXIMUM_RETRY);
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "Wi-Fi failed after %d retries", CONFIG_APP_WIFI_MAXIMUM_RETRY);
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;

        s_retry_count = 0;
        s_wifi_connected = true;
        snprintf(s_ip_addr, sizeof(s_ip_addr), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Wi-Fi connected, IP=%s", s_ip_addr);
    }
}

esp_err_t wifi_manager_init(void)
{
    esp_netif_t *netif;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t wifi_config = {0};
    wifi_auth_mode_t authmode = strlen(CONFIG_APP_WIFI_PASSWORD) == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    netif = esp_netif_create_default_wifi_sta();
    if (netif == NULL) {
        return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(advertise_hostname(netif), TAG, "hostname advertisement setup failed");
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &s_wifi_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &s_wifi_got_ip));

    strlcpy((char *)wifi_config.sta.ssid, CONFIG_APP_WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, CONFIG_APP_WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = authmode;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "joining SSID=%s", CONFIG_APP_WIFI_SSID);
    return ESP_OK;
}

bool wifi_manager_is_connected(void)
{
    return s_wifi_connected;
}

const char *wifi_manager_get_ip(void)
{
    return s_ip_addr[0] == '\0' ? "" : s_ip_addr;
}

const char *wifi_manager_get_hostname(void)
{
    return CONFIG_APP_MDNS_HOSTNAME;
}

const char *wifi_manager_get_ssid(void)
{
    return CONFIG_APP_WIFI_SSID;
}

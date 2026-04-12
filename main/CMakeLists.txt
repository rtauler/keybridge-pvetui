set(srcs
    "app_main.c"
    "ble_keyboard.c"
    "esp_hid_gap.c"
    "http_control.c"
    "wifi_manager.c"
)

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "."
    REQUIRES bt esp_event esp_hid esp_http_server esp_netif esp_wifi nvs_flash
)

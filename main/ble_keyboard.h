#ifndef BLE_KEYBOARD_H
#define BLE_KEYBOARD_H

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

typedef struct {
    const char *id;
    const char *label;
} ble_keyboard_action_t;

esp_err_t ble_keyboard_init(void);
bool ble_keyboard_is_connected(void);
const char *ble_keyboard_get_device_name(void);
const ble_keyboard_action_t *ble_keyboard_get_actions(size_t *count);
esp_err_t ble_keyboard_send_action(const char *action_id);
esp_err_t ble_keyboard_send_text(const char *text, size_t length);

#endif

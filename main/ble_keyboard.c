#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_bt.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_hidd.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "ble_keyboard.h"
#include "esp_hid_gap.h"
#include "sdkconfig.h"

typedef struct {
    uint8_t modifier;
    uint8_t keycode;
} key_combo_t;

typedef struct {
    esp_hidd_dev_t *hid_dev;
    bool connected;
} ble_keyboard_state_t;

static const char *TAG = "ble_keyboard";

static ble_keyboard_state_t s_state;

static const uint8_t s_keyboard_report_map[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,
    0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x03, 0x95, 0x05,
    0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05,
    0x91, 0x02, 0x95, 0x01, 0x75, 0x03, 0x91, 0x03,
    0x95, 0x05, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
    0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
    0xC0,
};

static esp_hid_raw_report_map_t s_report_maps[] = {
    {
        .data = s_keyboard_report_map,
        .len = sizeof(s_keyboard_report_map),
    },
};

static esp_hid_device_config_t s_hid_config = {
    .vendor_id = 0x16C0,
    .product_id = 0x05DF,
    .version = 0x0100,
    .device_name = CONFIG_APP_BLE_DEVICE_NAME,
    .manufacturer_name = "Keybridge",
    .serial_number = "keybridge-pve",
    .report_maps = s_report_maps,
    .report_maps_len = 1,
};

enum {
    MOD_LEFT_CTRL = 0x01,
    MOD_LEFT_ALT = 0x04,
    MOD_LEFT_SHIFT = 0x02,
};

enum {
    KEY_TAB = 0x2B,
    KEY_SPACE = 0x2C,
    KEY_MINUS = 0x2D,
    KEY_EQUAL = 0x2E,
    KEY_LEFT_BRACKET = 0x2F,
    KEY_RIGHT_BRACKET = 0x30,
    KEY_BACKSLASH = 0x31,
    KEY_SEMICOLON = 0x33,
    KEY_APOSTROPHE = 0x34,
    KEY_GRAVE = 0x35,
    KEY_COMMA = 0x36,
    KEY_PERIOD = 0x37,
    KEY_SLASH = 0x38,
    KEY_ENTER = 0x28,
    KEY_ESCAPE = 0x29,
    KEY_LEFT_ARROW = 0x50,
    KEY_DOWN_ARROW = 0x51,
    KEY_UP_ARROW = 0x52,
    KEY_RIGHT_ARROW = 0x4F,
};

#define LETTER_KEY(ch) ((uint8_t)(0x04 + ((ch) - 'a')))
#define DIGIT_KEY(ch) ((uint8_t)(((ch) == '0') ? 0x27 : (0x1E + ((ch) - '1'))))

typedef struct {
    const char *id;
    key_combo_t combo;
} action_binding_t;

typedef struct {
    const char *id;
    const key_combo_t *sequence;
    size_t length;
} sequence_binding_t;

static const ble_keyboard_action_t s_actions[] = {
    { "navigate_left", "Left Arrow: Navigate Left" },
    { "navigate_down", "Down Arrow: Navigate Down" },
    { "navigate_up", "Up Arrow: Navigate Up" },
    { "navigate_right", "Right Arrow: Navigate Right" },
    { "view_1", "Alt+1: Switch View 1" },
    { "view_2", "Alt+2: Switch View 2" },
    { "view_3", "Alt+3: Switch View 3" },
    { "view_4", "Alt+4: Storage" },
    { "select", "Enter: Select" },
    { "escape", "Esc: Cancel / Back" },
    { "ssh_shell", "s: SSH Shell" },
    { "context_menu", "m: Context Menu" },
    { "auto_refresh", "a: Auto-refresh" },
    { "jump_top_pane", "Ctrl+B, Up Arrow: Jump Top Pane" },
    { "jump_bottom_pane", "Ctrl+B, Down Arrow: Jump Down Pane" },
    { "select_lxc_to_htop", "Ctrl+B, H: Select LXC to htop" },
    { "close_session", "Ctrl+C: Close Session" },
};

static const action_binding_t s_bindings[] = {
    { "navigate_left", { 0, KEY_LEFT_ARROW } },
    { "navigate_down", { 0, KEY_DOWN_ARROW } },
    { "navigate_up", { 0, KEY_UP_ARROW } },
    { "navigate_right", { 0, KEY_RIGHT_ARROW } },
    { "view_1", { MOD_LEFT_ALT, DIGIT_KEY('1') } },
    { "view_2", { MOD_LEFT_ALT, DIGIT_KEY('2') } },
    { "view_3", { MOD_LEFT_ALT, DIGIT_KEY('3') } },
    { "view_4", { MOD_LEFT_ALT, DIGIT_KEY('4') } },
    { "select", { 0, KEY_ENTER } },
    { "escape", { 0, KEY_ESCAPE } },
    { "ssh_shell", { 0, LETTER_KEY('s') } },
    { "context_menu", { 0, LETTER_KEY('m') } },
    { "auto_refresh", { 0, LETTER_KEY('a') } },
};

static const key_combo_t s_jump_top_pane_sequence[] = {
    { MOD_LEFT_CTRL, LETTER_KEY('b') },
    { 0, KEY_UP_ARROW },
};

static const key_combo_t s_jump_bottom_pane_sequence[] = {
    { MOD_LEFT_CTRL, LETTER_KEY('b') },
    { 0, KEY_DOWN_ARROW },
};

static const key_combo_t s_select_lxc_to_htop_sequence[] = {
    { MOD_LEFT_CTRL, LETTER_KEY('b') },
    { 0, LETTER_KEY('h') },
};

static const key_combo_t s_close_session_sequence[] = {
    { MOD_LEFT_CTRL, LETTER_KEY('c') },
};

static const sequence_binding_t s_sequence_bindings[] = {
    { "jump_top_pane", s_jump_top_pane_sequence, sizeof(s_jump_top_pane_sequence) / sizeof(s_jump_top_pane_sequence[0]) },
    { "jump_bottom_pane", s_jump_bottom_pane_sequence, sizeof(s_jump_bottom_pane_sequence) / sizeof(s_jump_bottom_pane_sequence[0]) },
    { "select_lxc_to_htop", s_select_lxc_to_htop_sequence, sizeof(s_select_lxc_to_htop_sequence) / sizeof(s_select_lxc_to_htop_sequence[0]) },
    { "close_session", s_close_session_sequence, sizeof(s_close_session_sequence) / sizeof(s_close_session_sequence[0]) },
};

static const action_binding_t *find_action(const char *action_id)
{
    size_t i;

    for (i = 0; i < sizeof(s_bindings) / sizeof(s_bindings[0]); ++i) {
        if (strcmp(s_bindings[i].id, action_id) == 0) {
            return &s_bindings[i];
        }
    }

    return NULL;
}

static const sequence_binding_t *find_sequence_action(const char *action_id)
{
    size_t i;

    for (i = 0; i < sizeof(s_sequence_bindings) / sizeof(s_sequence_bindings[0]); ++i) {
        if (strcmp(s_sequence_bindings[i].id, action_id) == 0) {
            return &s_sequence_bindings[i];
        }
    }

    return NULL;
}

static void send_key_combo(key_combo_t combo)
{
    uint8_t report[8] = {0};

    report[0] = combo.modifier;
    report[2] = combo.keycode;

    esp_hidd_dev_input_set(s_state.hid_dev, 0, 1, report, sizeof(report));
    vTaskDelay(pdMS_TO_TICKS(30));

    memset(report, 0, sizeof(report));
    esp_hidd_dev_input_set(s_state.hid_dev, 0, 1, report, sizeof(report));
}

static void send_key_sequence(const key_combo_t *sequence, size_t length)
{
    size_t i;

    for (i = 0; i < length; ++i) {
        send_key_combo(sequence[i]);
        vTaskDelay(pdMS_TO_TICKS(60));
    }
}

static bool ascii_to_key_combo(char ch, key_combo_t *combo)
{
    if (combo == NULL) {
        return false;
    }

    if (ch >= 'a' && ch <= 'z') {
        combo->modifier = 0;
        combo->keycode = LETTER_KEY(ch);
        return true;
    }

    if (ch >= 'A' && ch <= 'Z') {
        combo->modifier = MOD_LEFT_SHIFT;
        combo->keycode = LETTER_KEY((char)(ch - 'A' + 'a'));
        return true;
    }

    if (ch >= '1' && ch <= '9') {
        combo->modifier = 0;
        combo->keycode = DIGIT_KEY(ch);
        return true;
    }

    switch (ch) {
    case '0':
        *combo = (key_combo_t){ 0, DIGIT_KEY('0') };
        return true;
    case '\n':
    case '\r':
        *combo = (key_combo_t){ 0, KEY_ENTER };
        return true;
    case '\t':
        *combo = (key_combo_t){ 0, KEY_TAB };
        return true;
    case ' ':
        *combo = (key_combo_t){ 0, KEY_SPACE };
        return true;
    case '-':
        *combo = (key_combo_t){ 0, KEY_MINUS };
        return true;
    case '_':
        *combo = (key_combo_t){ MOD_LEFT_SHIFT, KEY_MINUS };
        return true;
    case '=':
        *combo = (key_combo_t){ 0, KEY_EQUAL };
        return true;
    case '+':
        *combo = (key_combo_t){ MOD_LEFT_SHIFT, KEY_EQUAL };
        return true;
    case '[':
        *combo = (key_combo_t){ 0, KEY_LEFT_BRACKET };
        return true;
    case '{':
        *combo = (key_combo_t){ MOD_LEFT_SHIFT, KEY_LEFT_BRACKET };
        return true;
    case ']':
        *combo = (key_combo_t){ 0, KEY_RIGHT_BRACKET };
        return true;
    case '}':
        *combo = (key_combo_t){ MOD_LEFT_SHIFT, KEY_RIGHT_BRACKET };
        return true;
    case '\\':
        *combo = (key_combo_t){ 0, KEY_BACKSLASH };
        return true;
    case '|':
        *combo = (key_combo_t){ MOD_LEFT_SHIFT, KEY_BACKSLASH };
        return true;
    case ';':
        *combo = (key_combo_t){ 0, KEY_SEMICOLON };
        return true;
    case ':':
        *combo = (key_combo_t){ MOD_LEFT_SHIFT, KEY_SEMICOLON };
        return true;
    case '\'':
        *combo = (key_combo_t){ 0, KEY_APOSTROPHE };
        return true;
    case '"':
        *combo = (key_combo_t){ MOD_LEFT_SHIFT, KEY_APOSTROPHE };
        return true;
    case '`':
        *combo = (key_combo_t){ 0, KEY_GRAVE };
        return true;
    case '~':
        *combo = (key_combo_t){ MOD_LEFT_SHIFT, KEY_GRAVE };
        return true;
    case ',':
        *combo = (key_combo_t){ 0, KEY_COMMA };
        return true;
    case '<':
        *combo = (key_combo_t){ MOD_LEFT_SHIFT, KEY_COMMA };
        return true;
    case '.':
        *combo = (key_combo_t){ 0, KEY_PERIOD };
        return true;
    case '>':
        *combo = (key_combo_t){ MOD_LEFT_SHIFT, KEY_PERIOD };
        return true;
    case '/':
        *combo = (key_combo_t){ 0, KEY_SLASH };
        return true;
    case '?':
        *combo = (key_combo_t){ MOD_LEFT_SHIFT, KEY_SLASH };
        return true;
    case '!':
        *combo = (key_combo_t){ MOD_LEFT_SHIFT, DIGIT_KEY('1') };
        return true;
    case '@':
        *combo = (key_combo_t){ MOD_LEFT_SHIFT, DIGIT_KEY('2') };
        return true;
    case '#':
        *combo = (key_combo_t){ MOD_LEFT_SHIFT, DIGIT_KEY('3') };
        return true;
    case '$':
        *combo = (key_combo_t){ MOD_LEFT_SHIFT, DIGIT_KEY('4') };
        return true;
    case '%':
        *combo = (key_combo_t){ MOD_LEFT_SHIFT, DIGIT_KEY('5') };
        return true;
    case '^':
        *combo = (key_combo_t){ MOD_LEFT_SHIFT, DIGIT_KEY('6') };
        return true;
    case '&':
        *combo = (key_combo_t){ MOD_LEFT_SHIFT, DIGIT_KEY('7') };
        return true;
    case '*':
        *combo = (key_combo_t){ MOD_LEFT_SHIFT, DIGIT_KEY('8') };
        return true;
    case '(':
        *combo = (key_combo_t){ MOD_LEFT_SHIFT, DIGIT_KEY('9') };
        return true;
    case ')':
        *combo = (key_combo_t){ MOD_LEFT_SHIFT, DIGIT_KEY('0') };
        return true;
    default:
        return false;
    }
}

static void ble_hidd_event_callback(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    esp_hidd_event_t event = (esp_hidd_event_t)id;
    esp_hidd_event_data_t *param = (esp_hidd_event_data_t *)event_data;

    switch (event) {
    case ESP_HIDD_START_EVENT:
        ESP_LOGI(TAG, "BLE HID started");
        esp_hid_ble_gap_adv_start();
        break;
    case ESP_HIDD_CONNECT_EVENT:
        s_state.connected = true;
        ESP_LOGI(TAG, "BLE HID connected");
        break;
    case ESP_HIDD_PROTOCOL_MODE_EVENT:
        ESP_LOGI(TAG, "protocol mode map=%u mode=%u",
                 param->protocol_mode.map_index, param->protocol_mode.protocol_mode);
        break;
    case ESP_HIDD_CONTROL_EVENT:
        ESP_LOGI(TAG, "control event map=%u control=%u",
                 param->control.map_index, param->control.control);
        break;
    case ESP_HIDD_OUTPUT_EVENT:
        ESP_LOGI(TAG, "output event report_id=%u len=%u",
                 param->output.report_id, param->output.length);
        break;
    case ESP_HIDD_FEATURE_EVENT:
        ESP_LOGI(TAG, "feature event report_id=%u len=%u",
                 param->feature.report_id, param->feature.length);
        break;
    case ESP_HIDD_DISCONNECT_EVENT:
        s_state.connected = false;
        ESP_LOGI(TAG, "BLE HID disconnected");
        esp_hid_ble_gap_adv_start();
        break;
    case ESP_HIDD_STOP_EVENT:
        s_state.connected = false;
        ESP_LOGI(TAG, "BLE HID stopped");
        break;
    default:
        break;
    }
}

static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_store_config_init(void);

esp_err_t ble_keyboard_init(void)
{
    esp_err_t ret;

    ret = esp_hid_gap_init(HID_DEV_MODE);
    ESP_RETURN_ON_ERROR(ret, TAG, "hid gap init failed");

    ret = esp_hid_ble_gap_adv_init(ESP_HID_APPEARANCE_KEYBOARD, s_hid_config.device_name);
    ESP_RETURN_ON_ERROR(ret, TAG, "adv init failed");

    ret = esp_hidd_dev_init(&s_hid_config, ESP_HID_TRANSPORT_BLE, ble_hidd_event_callback, &s_state.hid_dev);
    ESP_RETURN_ON_ERROR(ret, TAG, "hid device init failed");

    ble_store_config_init();
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    nimble_port_freertos_init(ble_host_task);

    return ESP_OK;
}

bool ble_keyboard_is_connected(void)
{
    return s_state.connected;
}

const char *ble_keyboard_get_device_name(void)
{
    return s_hid_config.device_name;
}

const ble_keyboard_action_t *ble_keyboard_get_actions(size_t *count)
{
    if (count != NULL) {
        *count = sizeof(s_actions) / sizeof(s_actions[0]);
    }

    return s_actions;
}

esp_err_t ble_keyboard_send_action(const char *action_id)
{
    const action_binding_t *binding;
    const sequence_binding_t *sequence_binding;

    if (!s_state.connected) {
        return ESP_ERR_INVALID_STATE;
    }

    binding = find_action(action_id);
    if (binding != NULL) {
        send_key_combo(binding->combo);
        ESP_LOGI(TAG, "sent action=%s", action_id);
        return ESP_OK;
    }

    sequence_binding = find_sequence_action(action_id);
    if (sequence_binding == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    send_key_sequence(sequence_binding->sequence, sequence_binding->length);
    ESP_LOGI(TAG, "sent action=%s", action_id);
    return ESP_OK;
}

esp_err_t ble_keyboard_send_text(const char *text, size_t length)
{
    size_t i;
    key_combo_t combo;

    if (!s_state.connected) {
        return ESP_ERR_INVALID_STATE;
    }

    if (text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    for (i = 0; i < length; ++i) {
        if (!ascii_to_key_combo(text[i], &combo)) {
            ESP_LOGW(TAG, "unsupported text character 0x%02x at index %u", (unsigned char)text[i], (unsigned)i);
            return ESP_ERR_NOT_SUPPORTED;
        }
    }

    for (i = 0; i < length; ++i) {
        ascii_to_key_combo(text[i], &combo);
        send_key_combo(combo);
        vTaskDelay(pdMS_TO_TICKS(45));
    }

    ESP_LOGI(TAG, "sent text length=%u", (unsigned)length);
    return ESP_OK;
}

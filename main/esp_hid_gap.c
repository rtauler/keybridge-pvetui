/*
 * Derived from the ESP-IDF esp_hid_device example and reduced to the NimBLE BLE-HID path.
 */

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_bt.h"
#include "esp_check.h"
#include "esp_log.h"

#include "esp_hid_gap.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_sm.h"
#include "host/util/util.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"

#define GATT_SVR_SVC_HID_UUID 0x1812

static const char *TAG = "ESP_HID_GAP";
static SemaphoreHandle_t s_bt_cb_semaphore;
static SemaphoreHandle_t s_ble_cb_semaphore;
static struct ble_hs_adv_fields s_adv_fields;

esp_err_t esp_hid_ble_gap_adv_init(uint16_t appearance, const char *device_name)
{
    ble_uuid16_t *uuid16;

    memset(&s_adv_fields, 0, sizeof(s_adv_fields));
    s_adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    s_adv_fields.appearance = appearance;
    s_adv_fields.appearance_is_present = 1;
    s_adv_fields.tx_pwr_lvl_is_present = 1;
    s_adv_fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    s_adv_fields.name = (uint8_t *)device_name;
    s_adv_fields.name_len = strlen(device_name);
    s_adv_fields.name_is_complete = 1;

    uuid16 = malloc(sizeof(*uuid16));
    ESP_RETURN_ON_FALSE(uuid16 != NULL, ESP_ERR_NO_MEM, TAG, "uuid alloc failed");
    memset(uuid16, 0, sizeof(*uuid16));
    uuid16->u.type = BLE_UUID_TYPE_16;
    uuid16->value = GATT_SVR_SVC_HID_UUID;
    s_adv_fields.uuids16 = uuid16;
    s_adv_fields.num_uuids16 = 1;
    s_adv_fields.uuids16_is_complete = 1;

    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist |= BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;

    return ESP_OK;
}

static int hid_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "connection %s; status=%d",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.status);
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnect; reason=%d", event->disconnect.reason);
        return 0;
    case BLE_GAP_EVENT_CONN_UPDATE:
        ESP_LOGI(TAG, "connection updated; status=%d", event->conn_update.status);
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "advertise complete; reason=%d", event->adv_complete.reason);
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "subscribe event; conn_handle=%d attr_handle=%d",
                 event->subscribe.conn_handle, event->subscribe.attr_handle);
        return 0;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "mtu update event; conn_handle=%d mtu=%d",
                 event->mtu.conn_handle, event->mtu.value);
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        MODLOG_DFLT(INFO, "encryption change event; status=%d", event->enc_change.status);
        rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
        assert(rc == 0);
        return 0;
    case BLE_GAP_EVENT_NOTIFY_TX:
        MODLOG_DFLT(INFO, "notify_tx event; conn_handle=%d attr_handle=%d status=%d",
                    event->notify_tx.conn_handle, event->notify_tx.attr_handle,
                    event->notify_tx.status);
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        assert(rc == 0);
        ble_store_util_delete_peer(&desc.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        struct ble_sm_io pkey = {0};

        ESP_LOGI(TAG, "passkey action=%d", event->passkey.params.action);
        if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            pkey.action = event->passkey.params.action;
            pkey.passkey = 123456;
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            ESP_LOGI(TAG, "display passkey %06" PRIu32 ", rc=%d", pkey.passkey, rc);
        } else if (event->passkey.params.action == BLE_SM_IOACT_INPUT) {
            pkey.action = event->passkey.params.action;
            pkey.passkey = 123456;
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            ESP_LOGI(TAG, "input fallback passkey rc=%d", rc);
        } else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            pkey.action = event->passkey.params.action;
            pkey.numcmp_accept = 1;
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            ESP_LOGI(TAG, "numeric comparison accepted rc=%d", rc);
        }
        return 0;
    }
    default:
        return 0;
    }
}

esp_err_t esp_hid_ble_gap_adv_start(void)
{
    int rc;
    struct ble_gap_adv_params adv_params;
    int32_t adv_duration_ms = 180000;

    rc = ble_gap_adv_set_fields(&s_adv_fields);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error setting advertisement data; rc=%d", rc);
        return rc;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(30);
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(50);

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, adv_duration_ms, &adv_params, hid_gap_event, NULL);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error enabling advertisement; rc=%d", rc);
    }
    return rc;
}

static esp_err_t init_low_level(uint8_t mode)
{
    esp_err_t ret;
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

#if CONFIG_IDF_TARGET_ESP32
    bt_cfg.mode = mode;
#endif

    ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_bt_controller_mem_release failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_bt_controller_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_enable(mode);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_bt_controller_enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_nimble_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_nimble_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

static esp_err_t deinit_low_level(void)
{
    esp_err_t ret;

    ret = esp_nimble_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_nimble_deinit failed: %s", esp_err_to_name(ret));
    }

    ret = esp_bt_controller_disable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_bt_controller_disable failed: %s", esp_err_to_name(ret));
    }

    ret = esp_bt_controller_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_bt_controller_deinit failed: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t esp_hid_gap_deinit(void)
{
    esp_err_t ret = deinit_low_level();

    if (s_bt_cb_semaphore != NULL) {
        vSemaphoreDelete(s_bt_cb_semaphore);
        s_bt_cb_semaphore = NULL;
    }

    if (s_ble_cb_semaphore != NULL) {
        vSemaphoreDelete(s_ble_cb_semaphore);
        s_ble_cb_semaphore = NULL;
    }

    return ret;
}

esp_err_t esp_hid_gap_init(uint8_t mode)
{
    if (mode != HIDD_BLE_MODE) {
        ESP_LOGE(TAG, "unsupported mode=%u", mode);
        return ESP_ERR_INVALID_ARG;
    }

    if (s_bt_cb_semaphore != NULL || s_ble_cb_semaphore != NULL) {
        ESP_LOGE(TAG, "already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    s_bt_cb_semaphore = xSemaphoreCreateBinary();
    s_ble_cb_semaphore = xSemaphoreCreateBinary();
    if (s_bt_cb_semaphore == NULL || s_ble_cb_semaphore == NULL) {
        if (s_bt_cb_semaphore != NULL) {
            vSemaphoreDelete(s_bt_cb_semaphore);
            s_bt_cb_semaphore = NULL;
        }
        if (s_ble_cb_semaphore != NULL) {
            vSemaphoreDelete(s_ble_cb_semaphore);
            s_ble_cb_semaphore = NULL;
        }
        return ESP_ERR_NO_MEM;
    }

    return init_low_level(mode);
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"

#include "ble_keyboard.h"
#include "http_control.h"
#include "sdkconfig.h"
#include "wifi_manager.h"

static const char *TAG = "http_control";

#define STATUS_BUF_SIZE 2048

static const char INDEX_HTML[] =
"<!doctype html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Keybridge PVE</title>"
"<style>"
":root{color-scheme:light;background:#ece8dc;color:#162126;font-family:Verdana,sans-serif;--panel:#f7f1e4;--panel-edge:#d8cdb6;--remote:#243640;--remote-2:#17242b;--accent:#d56b2d;--white:#fff;}"
"body{margin:0;background:radial-gradient(circle at top,#fff8dd 0,#efe8d6 42%,#ddd5c4 100%);}"
".wrap{max-width:920px;margin:0 auto;padding:18px 14px 32px;}"
"h1{font-size:1.7rem;margin:0 0 8px;}p{margin:0 0 16px;line-height:1.4;color:#31444b;}"
".remote{background:linear-gradient(180deg,#314852,#1c2b32);border-radius:34px;padding:20px 16px 18px;box-shadow:0 24px 60px rgba(14,23,28,.22),inset 0 1px 0 rgba(255,255,255,.12);}"
".cluster{display:grid;gap:16px;align-items:start;}"
".dpad{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px;max-width:340px;margin:0 auto;}"
".dpad .spacer{min-height:92px;}"
".actions{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px;}"
"button{appearance:none;border:none;border-radius:22px;padding:12px 10px;background:linear-gradient(180deg,#3d5660,#263840);color:var(--white);font-size:1rem;font-weight:700;min-height:92px;box-shadow:0 12px 24px rgba(7,12,15,.28),inset 0 1px 0 rgba(255,255,255,.08);display:flex;flex-direction:column;align-items:center;justify-content:center;gap:8px;text-align:center;}"
"button.primary{background:linear-gradient(180deg,#d97a3d,#b75722);}"
"button:active{transform:translateY(1px);}"
".icon{font-size:1.9rem;line-height:1;color:#fff;}"
".icon.small{font-size:1.5rem;}"
".label{display:block;font-size:.88rem;line-height:1.15;color:#fff;letter-spacing:.01em;}"
".status{background:var(--panel);border:2px solid var(--panel-edge);border-radius:22px;padding:14px 16px;margin-top:16px;box-shadow:0 10px 30px rgba(22,33,38,.08);}"
".status strong{display:block;font-size:.78rem;letter-spacing:.08em;text-transform:uppercase;color:#7b6d56;margin-bottom:8px;}"
".status-line{margin:4px 0;color:#26363c;}"
"@media (min-width:720px){.cluster{grid-template-columns:minmax(300px,360px) minmax(0,1fr);}.actions{grid-template-columns:repeat(3,minmax(0,1fr));}}"
"</style></head><body><div class='wrap'>"
"<h1>Keybridge PVE</h1><p>Send Proxmox-oriented keyboard shortcuts from your phone.</p>"
"<div class='remote'><div class='cluster'>"
"<div class='dpad'>"
"<div class='spacer'></div>"
"<button data-action='navigate_up'><span class='icon'>&uarr;</span><span class='label'>Navigate Up</span></button>"
"<div class='spacer'></div>"
"<button data-action='navigate_left'><span class='icon'>&larr;</span><span class='label'>Navigate Left</span></button>"
"<button class='primary' data-action='select'><span class='icon'>OK</span><span class='label'>Select</span></button>"
"<button data-action='navigate_right'><span class='icon'>&rarr;</span><span class='label'>Navigate Right</span></button>"
"<div class='spacer'></div>"
"<button data-action='navigate_down'><span class='icon'>&darr;</span><span class='label'>Navigate Down</span></button>"
"<div class='spacer'></div>"
"</div>"
"<div class='actions'>"
"<button data-action='prev_view'><span class='icon small'>&#9664;</span><span class='label'>Previous View</span></button>"
"<button data-action='next_view'><span class='icon small'>&#9654;</span><span class='label'>Next View</span></button>"
"<button data-action='context_menu'><span class='icon small'>&#9776;</span><span class='label'>Context Menu</span></button>"
"<button data-action='global_menu'><span class='icon small'>&#9679;</span><span class='label'>Main Menu</span></button>"
"<button data-action='search'><span class='icon small'>&#8981;</span><span class='label'>Search</span></button>"
"<button data-action='auto_refresh'><span class='icon small'>&#8635;</span><span class='label'>Refresh Toggle</span></button>"
"<button data-action='view_1'><span class='icon small'>1</span><span class='label'>Server View</span></button>"
"<button data-action='view_2'><span class='icon small'>2</span><span class='label'>Guests View</span></button>"
"<button data-action='view_3'><span class='icon small'>3</span><span class='label'>Tasks View</span></button>"
"<button data-action='ssh_shell'><span class='icon small'>&gt;_</span><span class='label'>SSH Shell</span></button>"
"<button data-action='vnc_console'><span class='icon small'>&#9635;</span><span class='label'>VNC Console</span></button>"
"<button data-action='help'><span class='icon small'>?</span><span class='label'>Help</span></button>"
"<button data-action='quit'><span class='icon small'>&#10005;</span><span class='label'>Close Window</span></button>"
"</div></div>"
"<div class='status' id='status'>Loading status...</div>"
"</div></div>"
"<script>"
"async function refreshStatus(){const r=await fetch('/api/status');const s=await r.json();"
"document.getElementById('status').innerHTML="
"'<strong>System Status</strong>' +"
"'<div class=\"status-line\">Wi-Fi: <b>'+(s.wifi_connected?'connected':'disconnected')+'</b> on <b>'+s.ssid+'</b></div>' +"
"'<div class=\"status-line\">IP: <b>'+(s.ip||'pending')+'</b></div>' +"
"'<div class=\"status-line\">Host: <b>'+(s.hostname?s.hostname+'.local':'pending')+'</b></div>' +"
"'<div class=\"status-line\">BLE: <b>'+(s.ble_connected?'connected':'waiting for host pairing')+'</b></div>' +"
"'<div class=\"status-line\">Device: <b>'+s.device_name+'</b></div>';}"
"async function sendAction(action){const r=await fetch('/api/action',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action})});"
"if(!r.ok){const t=await r.text();alert(t||('Request failed: '+r.status));}await refreshStatus();}"
"document.querySelectorAll('button[data-action]').forEach((btn)=>btn.addEventListener('click',()=>sendAction(btn.dataset.action)));"
"refreshStatus();setInterval(refreshStatus,3000);"
"</script></body></html>";

static esp_err_t send_json_string(httpd_req_t *req, const char *body)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, body);
    return ESP_OK;
}

static esp_err_t index_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    size_t count;
    size_t i;
    int written;
    char *body = calloc(1, STATUS_BUF_SIZE);
    const ble_keyboard_action_t *items;

    if (body == NULL) {
        return ESP_ERR_NO_MEM;
    }

    items = ble_keyboard_get_actions(&count);
    written = snprintf(
        body,
        STATUS_BUF_SIZE,
        "{\"wifi_connected\":%s,\"ssid\":\"%s\",\"ip\":\"%s\",\"hostname\":\"%s\",\"ble_connected\":%s,"
        "\"device_name\":\"%s\",\"actions\":[",
        wifi_manager_is_connected() ? "true" : "false",
        wifi_manager_get_ssid(),
        wifi_manager_get_ip(),
        wifi_manager_get_hostname(),
        ble_keyboard_is_connected() ? "true" : "false",
        ble_keyboard_get_device_name()
    );
    if (written < 0 || written >= STATUS_BUF_SIZE) {
        free(body);
        return ESP_ERR_NO_MEM;
    }

    for (i = 0; i < count; ++i) {
        written += snprintf(
            body + written,
            STATUS_BUF_SIZE - written,
            "%s{\"id\":\"%s\",\"label\":\"%s\"}",
            i == 0 ? "" : ",",
            items[i].id,
            items[i].label
        );
        if (written < 0 || written >= STATUS_BUF_SIZE) {
            free(body);
            return ESP_ERR_NO_MEM;
        }
    }

    written += snprintf(body + written, STATUS_BUF_SIZE - written, "]}");
    if (written < 0 || written >= STATUS_BUF_SIZE) {
        free(body);
        return ESP_ERR_NO_MEM;
    }

    send_json_string(req, body);
    free(body);
    return ESP_OK;
}

static esp_err_t action_post_handler(httpd_req_t *req)
{
    char buffer[128];
    int offset = 0;
    int received;
    esp_err_t err;
    char *action_ptr;
    char *value_start;
    char *value_end;

    if (req->content_len <= 0 || req->content_len >= (int)sizeof(buffer)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid request body");
        return ESP_FAIL;
    }

    while (offset < req->content_len) {
        received = httpd_req_recv(req, buffer + offset, req->content_len - offset);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "failed to read request body");
            return ESP_FAIL;
        }
        offset += received;
    }
    buffer[offset] = '\0';

    action_ptr = strstr(buffer, "\"action\"");
    if (action_ptr == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing action");
        return ESP_FAIL;
    }

    value_start = strchr(action_ptr, ':');
    if (value_start == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid JSON");
        return ESP_FAIL;
    }
    value_start = strchr(value_start, '"');
    if (value_start == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid JSON");
        return ESP_FAIL;
    }
    ++value_start;

    value_end = strchr(value_start, '"');
    if (value_end == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid JSON");
        return ESP_FAIL;
    }
    *value_end = '\0';

    err = ble_keyboard_send_action(value_start);

    if (err == ESP_ERR_NOT_FOUND) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "unknown action");
        return ESP_FAIL;
    }

    if (err == ESP_ERR_INVALID_STATE) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_sendstr(req, "BLE keyboard is not connected");
        return ESP_FAIL;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "action dispatch failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "action failed");
        return ESP_FAIL;
    }

    send_json_string(req, "{\"ok\":true}");
    return ESP_OK;
}

esp_err_t http_control_start(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_get_handler,
    };
    httpd_uri_t status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = status_get_handler,
    };
    httpd_uri_t action_uri = {
        .uri = "/api/action",
        .method = HTTP_POST,
        .handler = action_post_handler,
    };

    config.server_port = CONFIG_APP_HTTP_PORT;
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_RETURN_ON_ERROR(httpd_start(&server, &config), TAG, "httpd start failed");
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &index_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &status_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &action_uri));

    ESP_LOGI(TAG, "HTTP server started on port %d", CONFIG_APP_HTTP_PORT);
    return ESP_OK;
}

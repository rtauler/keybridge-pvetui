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
":root{color-scheme:light;background:#f2efe7;color:#162126;font-family:Verdana,sans-serif;}"
"body{margin:0;background:radial-gradient(circle at top,#fff7df,#f2efe7 55%,#e3ddd1);}"
".wrap{max-width:860px;margin:0 auto;padding:20px 14px 32px;}"
"h1{font-size:1.7rem;margin:0 0 8px;}p{margin:0 0 10px;line-height:1.4;}"
".status{background:#fffaf0;border:2px solid #d7ccb5;border-radius:18px;padding:14px 16px;margin:14px 0 18px;box-shadow:0 10px 30px rgba(22,33,38,.08);}"
".grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px;}"
"button{appearance:none;border:none;border-radius:18px;padding:16px 12px;background:linear-gradient(180deg,#204d57,#16353c);color:#fff;font-size:1rem;font-weight:700;min-height:68px;box-shadow:0 10px 18px rgba(22,53,60,.18);}"
"button:active{transform:translateY(1px);}small{display:block;color:#5a6a6f;margin-top:6px;}"
"@media (min-width:720px){.grid{grid-template-columns:repeat(3,minmax(0,1fr));}}"
"</style></head><body><div class='wrap'>"
"<h1>Keybridge PVE</h1><p>Send Proxmox-oriented keyboard shortcuts from your phone.</p>"
"<div class='status' id='status'>Loading status...</div>"
"<div class='grid'>"
"<button data-action='navigate_left'>h<br><small>Navigate Left</small></button>"
"<button data-action='navigate_down'>j<br><small>Navigate Down</small></button>"
"<button data-action='navigate_up'>k<br><small>Navigate Up</small></button>"
"<button data-action='navigate_right'>l<br><small>Navigate Right</small></button>"
"<button data-action='view_1'>Alt+1<br><small>View 1</small></button>"
"<button data-action='view_2'>Alt+2<br><small>View 2</small></button>"
"<button data-action='view_3'>Alt+3<br><small>View 3</small></button>"
"<button data-action='select'>Enter<br><small>Select</small></button>"
"<button data-action='prev_view'>[<br><small>Previous View</small></button>"
"<button data-action='next_view'>]<br><small>Next View</small></button>"
"<button data-action='ssh_shell'>s<br><small>SSH Shell</small></button>"
"<button data-action='vnc_console'>v<br><small>VNC Console</small></button>"
"<button data-action='context_menu'>m<br><small>Context Menu</small></button>"
"<button data-action='global_menu'>g<br><small>Global Menu</small></button>"
"<button data-action='search'>/<br><small>Search</small></button>"
"<button data-action='auto_refresh'>a<br><small>Auto-refresh</small></button>"
"<button data-action='help'>?<br><small>Help</small></button>"
"<button data-action='quit'>q<br><small>Quit</small></button>"
"</div></div>"
"<script>"
"async function refreshStatus(){const r=await fetch('/api/status');const s=await r.json();"
"document.getElementById('status').innerHTML="
"'Wi-Fi: <b>'+(s.wifi_connected?'connected':'disconnected')+'</b> on <b>'+s.ssid+'</b><br>' +"
"'IP: <b>'+(s.ip||'pending')+'</b><br>' +"
"'BLE: <b>'+(s.ble_connected?'connected':'waiting for host pairing')+'</b><br>' +"
"'Device: <b>'+s.device_name+'</b>';}"
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
        "{\"wifi_connected\":%s,\"ssid\":\"%s\",\"ip\":\"%s\",\"ble_connected\":%s,"
        "\"device_name\":\"%s\",\"actions\":[",
        wifi_manager_is_connected() ? "true" : "false",
        wifi_manager_get_ssid(),
        wifi_manager_get_ip(),
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

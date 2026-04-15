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
":root{color-scheme:dark;--bg:#05070a;--panel:#0d1218;--panel-2:#131a22;--button:#1a232d;--button-2:#212c37;--button-active:#2a3643;--accent:#d96b2b;--accent-2:#b6541d;--border:#2d3946;--text:#f3f7fb;--muted:#94a3b4;--ok:#54d08a;--warn:#f0b35a;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;}"
"*{box-sizing:border-box;-webkit-tap-highlight-color:transparent;}"
"body{margin:0;background:linear-gradient(180deg,#0a0f15 0,#05070a 100%);color:var(--text);}"
".wrap{max-width:430px;min-height:100svh;margin:0 auto;padding:10px 10px 12px;display:flex;}"
".remote{display:grid;gap:8px;flex:1;align-content:start;}"
".section{background:var(--panel);border:1px solid var(--border);border-radius:18px;padding:9px;box-shadow:0 12px 32px rgba(0,0,0,.28);}"
".views{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:6px;}"
".dpad{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:6px;}"
".actions{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:6px;}"
"button{appearance:none;border:1px solid var(--border);border-radius:16px;padding:8px 6px;background:linear-gradient(180deg,var(--button-2),var(--button));color:var(--text);font-size:.9rem;font-weight:700;min-height:58px;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:4px;text-align:center;touch-action:manipulation;}"
"button.wide{min-height:52px;}"
"button.primary{background:linear-gradient(180deg,var(--accent),var(--accent-2));border-color:#a64d1b;}"
"button.utility{background:linear-gradient(180deg,#263241,#1a232d);}"
"button.danger{background:linear-gradient(180deg,#5a2323,#3d1616);border-color:#6e2c2c;}"
"button:active{transform:scale(.985);background:var(--button-active);}"
".spacer{min-height:58px;}"
".icon{font-size:1.45rem;line-height:1;color:var(--text);}"
".icon.small{font-size:1.02rem;letter-spacing:.03em;}"
".label{display:block;font-size:.75rem;line-height:1.05;color:var(--text);}"
".status{background:var(--panel-2);border:1px solid var(--border);border-radius:16px;padding:9px 12px;}"
".status strong{display:block;font-size:.66rem;letter-spacing:.08em;text-transform:uppercase;color:var(--muted);margin-bottom:4px;}"
".status-line{display:flex;justify-content:space-between;gap:10px;padding:2px 0;color:#d9e2ec;font-size:.8rem;}"
".status-line b{color:var(--text);font-weight:700;text-align:right;}"
"@media (min-width:700px){.wrap{max-width:900px;}.remote{grid-template-columns:1.1fr .9fr;align-items:start;}.section.nav{grid-row:span 2;}}"
"</style></head><body><div class='wrap'><div class='remote'>"
"<div class='section'><div class='views'>"
"<button class='wide' data-action='view_1'><span class='icon small'>1</span><span class='label'>Server</span></button>"
"<button class='wide' data-action='view_2'><span class='icon small'>2</span><span class='label'>Guests</span></button>"
"<button class='wide' data-action='view_3'><span class='icon small'>3</span><span class='label'>Tasks</span></button>"
"<button class='wide' data-action='view_4'><span class='icon small'>4</span><span class='label'>Storage</span></button>"
"</div></div>"
"<div class='section nav'><div class='dpad'>"
"<button class='utility' data-action='escape'><span class='icon small'>Esc</span><span class='label'>Back</span></button>"
"<button data-action='navigate_up'><span class='icon'>&uarr;</span><span class='label'>Up</span></button>"
"<button class='utility' data-action='context_menu'><span class='icon small'>&#9776;</span><span class='label'>Menu</span></button>"
"<button data-action='navigate_left'><span class='icon'>&larr;</span><span class='label'>Left</span></button>"
"<button class='primary' data-action='select'><span class='icon small'>OK</span><span class='label'>Select</span></button>"
"<button data-action='navigate_right'><span class='icon'>&rarr;</span><span class='label'>Right</span></button>"
"<div class='spacer'></div>"
"<button data-action='navigate_down'><span class='icon'>&darr;</span><span class='label'>Down</span></button>"
"<div class='spacer'></div>"
"</div></div>"
"<div class='section'><div class='actions'>"
"<button data-action='ssh_shell'><span class='icon small'>&gt;_</span><span class='label'>SSH</span></button>"
"<button data-action='auto_refresh'><span class='icon small'>&#8635;</span><span class='label'>Refresh</span></button>"
"<button data-action='jump_top_pane'><span class='icon'>&uarr;</span><span class='label'>Jump top pane</span></button>"
"<button data-action='jump_bottom_pane'><span class='icon'>&darr;</span><span class='label'>Jump down pane</span></button>"
"<button data-action='select_lxc_to_htop'><span class='icon small'>H</span><span class='label'>Select LXC to htop</span></button>"
"<button class='danger' data-action='close_session'><span class='icon small'>^C</span><span class='label'>Close session</span></button>"
"</div></div>"
"<div class='status' id='status'>Loading status...</div>"
"</div></div>"
"<script>"
"async function refreshStatus(){const r=await fetch('/api/status');const s=await r.json();"
"document.getElementById('status').innerHTML="
"'<strong>System Status</strong>' +"
"'<div class=\"status-line\"><span>Wi-Fi</span><b>'+(s.wifi_connected?'connected':'disconnected')+(s.ssid?' on '+s.ssid:'')+'</b></div>' +"
"'<div class=\"status-line\"><span>IP</span><b>'+(s.ip||'pending')+'</b></div>' +"
"'<div class=\"status-line\"><span>Host</span><b>'+(s.hostname?s.hostname+'.local':'pending')+'</b></div>' +"
"'<div class=\"status-line\"><span>BLE</span><b>'+(s.ble_connected?'connected':'waiting')+'</b></div>' +"
"'<div class=\"status-line\"><span>Device</span><b>'+s.device_name+'</b></div>';}"
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

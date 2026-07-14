#include "web_server_bridge.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

static const char *TAG = "web_cfg";

// ── 配网HTML页面（手机端自适应） ──────────────────────────────────
static const char CONFIG_HTML[] = R"raw(
<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>WiFi配网</title>
<style>
  *{margin:0;padding:0;box-sizing:border-box;}
  body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;
       background:#f0f2f5;display:flex;justify-content:center;
       align-items:center;min-height:100vh;padding:20px;}
  .card{background:#fff;border-radius:16px;padding:30px 24px;
        width:100%;max-width:380px;box-shadow:0 4px 24px rgba(0,0,0,.08);}
  h2{text-align:center;color:#1a1a1a;margin-bottom:8px;font-size:22px;}
  .sub{text-align:center;color:#888;font-size:13px;margin-bottom:24px;}
  label{display:block;font-size:14px;color:#333;margin-bottom:6px;font-weight:500;}
  input{width:100%;padding:12px 14px;border:1.5px solid #e0e0e0;
         border-radius:10px;font-size:15px;margin-bottom:18px;
         transition:border .2s;outline:none;}
  input:focus{border-color:#4a90d9;}
  button{width:100%;padding:13px;background:#4a90d9;color:#fff;
          border:none;border-radius:10px;font-size:16px;font-weight:600;
          cursor:pointer;transition:background .2s;}
  button:active{background:#357abd;}
  .note{text-align:center;color:#aaa;font-size:12px;margin-top:16px;}
</style>
</head>
<body>
<div class="card">
  <h2>&#128246; WiFi 配网</h2>
  <p class="sub">请输入您要连接的路由器信息</p>
  <form method="post" action="/config">
    <label for="ssid">WiFi 名称 (SSID)</label>
    <input type="text" id="ssid" name="ssid" placeholder="请输入WiFi名称" required maxlength="32">
    <label for="pass">WiFi 密码</label>
    <input type="password" id="pass" name="pass" placeholder="请输入WiFi密码" required maxlength="64">
    <button type="submit">保存并连接</button>
  </form>
  <p class="note">ESP32 将自动切换到 STA 模式连接路由器</p>
</div>
</body>
</html>
)raw";

// ── 全局状态 ─────────────────────────────────────────────────────
static httpd_handle_t g_server       = NULL;
static wifi_config_submit_cb g_cb    = NULL;
static TimerHandle_t g_defer_timer   = NULL;
static char g_pending_ssid[33]       = {0};
static char g_pending_pass[65]       = {0};

// ── 延迟回调定时器（避免在HTTP handler内直接stop服务器导致死锁） ──
static void defer_timer_cb(TimerHandle_t xTimer)
{
    if (g_cb && strlen(g_pending_ssid) > 0) {
        ESP_LOGI(TAG, "延迟触发配网回调: SSID=%s", g_pending_ssid);
        g_cb(g_pending_ssid, g_pending_pass);
    }
}

// ── URL解码 ──────────────────────────────────────────────────────
static void url_decode(char *dst, const char *src, size_t dst_size)
{
    char *d = dst;
    const char *s = src;
    while (*s && (size_t)(d - dst) < dst_size - 1) {
        if (*s == '%' && s[1] && s[2]) {
            char hex[3] = {s[1], s[2], '\0'};
            *d++ = (char)strtol(hex, NULL, 16);
            s += 3;
        } else if (*s == '+') {
            *d++ = ' ';
            s++;
        } else {
            *d++ = *s++;
        }
    }
    *d = '\0';
}

// ── 解析POST body中的键值对 ─────────────────────────────────────
static bool parse_key_val(const char *body, int body_len,
                          char *ssid_out, size_t ssid_size,
                          char *pass_out, size_t pass_size)
{
    // body 格式: ssid=xxx&pass=yyy (URL-encoded)
    char buf[512];
    if (body_len >= sizeof(buf)) body_len = sizeof(buf) - 1;
    memcpy(buf, body, body_len);
    buf[body_len] = '\0';

    char *pair = strtok(buf, "&");
    while (pair) {
        char *eq = strchr(pair, '=');
        if (eq) {
            *eq = '\0';
            char val[256];
            url_decode(val, eq + 1, sizeof(val));
            if (strcmp(pair, "ssid") == 0) {
                strncpy(ssid_out, val, ssid_size - 1);
                ssid_out[ssid_size - 1] = '\0';
            } else if (strcmp(pair, "pass") == 0) {
                strncpy(pass_out, val, pass_size - 1);
                pass_out[pass_size - 1] = '\0';
            }
        }
        pair = strtok(NULL, "&");
    }
    return (strlen(ssid_out) > 0 && strlen(pass_out) > 0);
}

// ── GET / → 返回配网页面 ────────────────────────────────────────
static esp_err_t get_root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, CONFIG_HTML, strlen(CONFIG_HTML));
    return ESP_OK;
}

// ── POST /config → 接收SSID/密码并回调 ──────────────────────────
static esp_err_t post_config_handler(httpd_req_t *req)
{
    char body[512];
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    body[received] = '\0';

    char ssid[33] = {0};
    char pass[65] = {0};

    if (parse_key_val(body, received, ssid, sizeof(ssid), pass, sizeof(pass))) {
        ESP_LOGI(TAG, "收到配网: SSID=%s", ssid);

        // 返回成功页面
        const char *ok_html =
            "<!DOCTYPE html><html lang=\"zh\"><head><meta charset=\"UTF-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0\">"
            "<title>提交成功</title><style>"
            "body{font-family:-apple-system,sans-serif;display:flex;"
            "justify-content:center;align-items:center;height:100vh;"
            "background:#f0f2f5;text-align:center;}"
            ".ok{color:#4caf50;font-size:48px;}.msg{font-size:16px;color:#333;}"
            "</style></head><body><div>"
            "<div class=\"ok\">&#10004;</div>"
            "<p class=\"msg\">配网信息已提交<br>ESP32 正在连接路由器…</p>"
            "</div></body></html>";
        httpd_resp_set_type(req, "text/html; charset=utf-8");
        httpd_resp_send(req, ok_html, strlen(ok_html));

        // 保存凭证，用定时器延迟触发回调
        // （避免在HTTP handler内直接stop服务器导致httpd_stop死锁）
        strncpy(g_pending_ssid, ssid, sizeof(g_pending_ssid) - 1);
        strncpy(g_pending_pass, pass, sizeof(g_pending_pass) - 1);
        if (g_defer_timer) {
            xTimerStart(g_defer_timer, 0);
        }
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "缺少 SSID 或密码");
    }
    return ESP_OK;
}

// ── 注册URI路由 ─────────────────────────────────────────────────
static void register_routes(void)
{
    httpd_uri_t root = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = get_root_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(g_server, &root);

    httpd_uri_t config = {
        .uri       = "/config",
        .method    = HTTP_POST,
        .handler   = post_config_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(g_server, &config);
}

// ── 公开接口 ─────────────────────────────────────────────────────
void web_server_reg_wifi_config_cb(wifi_config_submit_cb cb)
{
    g_cb = cb;
}

void web_server_start(uint16_t port)
{
    // 如果已经运行则先停掉
    if (g_server) {
        web_server_stop();
    }

    // 创建一次性延迟定时器（100ms后触发，避免HTTP handler内死锁）
    if (!g_defer_timer) {
        g_defer_timer = xTimerCreate("cfg_defer", pdMS_TO_TICKS(100),
                                     pdFALSE, NULL, defer_timer_cb);
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.lru_purge_enable = true;
    config.max_req_hdr_len = 1024;  // 默认512不够，手机浏览器请求头较长

    if (httpd_start(&g_server, &config) == ESP_OK) {
        register_routes();
        ESP_LOGI(TAG, "配网服务器已启动，端口: %d", port);
    } else {
        ESP_LOGE(TAG, "配网服务器启动失败");
        g_server = NULL;
    }
}

void web_server_stop(void)
{
    // 停止延迟定时器（如果正在等待触发）
    if (g_defer_timer) {
        xTimerStop(g_defer_timer, 0);
    }

    if (g_server) {
        httpd_stop(g_server);  
        g_server = NULL;
        ESP_LOGI(TAG, "配网服务器已停止");
    }
}

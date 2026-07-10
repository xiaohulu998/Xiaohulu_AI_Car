

1.WiFi配网流程

ESP32 启动
  │
  ▼
main.c: app_main() → wifi_conect_init()
  │
  ├─ 初始化 NVS → netif → event_loop（全局仅一次）
  ├─ 注册回调 on_wifi_config_submit / on_sta_wifi_connected
  │
  ├─ NVS 中有已保存 WiFi ？
  │   ├─ YES → wifi_start_sta_mode(ssid, pass)
  │   │         ├─ 成功 → on_sta_wifi_connected() → 【开始数据传输】
  │   │         └─ 失败 → 回退到 AP 配网模式 ↓
  │   │
  │   └─ NO  → wifi_start_ap_mode()
  │             ├─ 开启 AP 热点："ESP32_WIFI_CONFIG" / "12345678"
  │             ├─ 启动 HTTP 服务器（端口 80）
  │             │
  │             ▼
  │          📱 手机连接热点 → 浏览器打开 http://192.168.4.1/
  │             │
  │             ▼
  │          📱 输入路由器 SSID + 密码 → 提交
  │             │
  │             ▼
  │          POST /config → 100ms 延迟定时器
  │             │
  │             ▼ (HTTP handler 已安全返回，无死锁)
  │          on_wifi_config_submit()
  │             ├─ 保存到 NVS
  │             ├─ wifi_stop_all()（停止HTTP服务器 + 注销事件处理器）
  │             └─ wifi_start_sta_mode(ssid, pass)
  │                   ├─ 成功 → on_sta_wifi_connected() → 【数据传输】
  │                   └─ 失败 → 重新开启 AP 配网
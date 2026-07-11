
还需完善内容：1.将nvs初始化挪进mian，避免被其他组件频繁初始化；
            2.增加联网成功后业务函数；



📁 文件分层架构
components/BSP/WIFI/
├── wifi.h / wifi.c              ← 总调度层（入口）:统一入口，编排整体流程，连接各层回调
├── wifi_mode.h / wifi_mode.c    ← WiFi 模式管理层（AP / STA 切换）:AP/STA 模式的启动、切换、停止，事件处理
├── wifi_nvs.h / wifi_nvs.c      ← NVS 持久化存储层（SSID/密码 存取）:WiFi 凭据的 NVS 闪存读写，断电不丢失
└── web_server_bridge.h / web_server_bridge.c  ← Web 配网服务器层 :HTTP 配网页面服务，接收手机提交的 SSID/密码

🔄 完整数据流
wifi_conect_init()                          ← 唯一对外入口
│
├─ ① 系统级初始化（全局仅一次）
│   ├─ nvs_flash_init()                     ← NVS 闪存初始化
│   ├─ esp_netif_init()                     ← LWIP 协议栈初始化
│   └─ esp_event_loop_create_default()      ← 创建系统事件循环
│
├─ ② 注册回调函数（连接各层）
│   ├─ web_server_reg_wifi_config_cb(on_wifi_config_submit)
│   │   └─ Web 收到配网信息 → 触发 on_wifi_config_submit()
│   └─ wifi_set_sta_connect_cb(on_sta_wifi_connected)
│       └─ STA 联网成功获取 IP → 触发 on_sta_wifi_connected()
│
└─ ③ 判断 NVS 是否有已保存的 WiFi
    │
    ├─ 有保存 → wifi_start_sta_mode(ssid, pass)
    │   ├─ 成功（10秒内获取IP）→ on_sta_wifi_connected() → 业务代码
    │   └─ 失败 → wifi_start_ap_mode() → 进入配网流程
    │
    └─ 无保存 → wifi_start_ap_mode() → 进入配网流程

🔌 AP 配网模式流程 （wifi_start_ap_mode）
wifi_start_ap_mode()
│
├─ wifi_stop_all()                         ← 先清理当前 WiFi 状态
│   ├─ web_server_stop()                   ← 停 Web 服务
│   ├─ 注销事件处理器
│   └─ esp_wifi_stop() + esp_wifi_deinit()
│
├─ esp_netif_create_default_wifi_ap()      ← 创建 AP 网络接口
├─ esp_wifi_init()                         ← WiFi 驱动初始化
├─ 配置 AP 参数：
│   ├─ SSID: "ESP32_WIFI_CONFIG"
│   ├─ 密码: "12345678"
│   ├─ 加密: WPA2_PSK
│   └─ 最大连接数: 4
├─ esp_wifi_set_mode(WIFI_MODE_AP)
├─ esp_wifi_start()
└─ web_server_start(80)                    ← 启动 HTTP 配网页面

手机端体验流程：
1. 手机连接热点 ESP32_WIFI_CONFIG（密码 12345678）
2. 浏览器打开 192.168.4.1
3. 看到移动端适配的配网页（内嵌 HTML/CSS）
4. 输入路由器 SSID 和密码 → 点击"保存并连接"
5. 表单 POST 到 /config

📡 STA 联网模式流程 （wifi_start_sta_mode）
wifi_start_sta_mode(ssid, pass)
│
├─ wifi_stop_all()                         ← 先清理当前 WiFi 状态
├─ xEventGroupCreate()                     ← 创建 FreeRTOS 事件组（同步等待）
├─ esp_netif_create_default_wifi_sta()     ← 创建 STA 网络接口
├─ esp_wifi_init()
├─ 注册事件处理器：
│   ├─ WIFI_EVENT → wifi_event_handler
│   │   └─ WIFI_EVENT_STA_DISCONNECTED → 自动重连 esp_wifi_connect()
│   └─ IP_EVENT → wifi_event_handler
│       └─ IP_EVENT_STA_GOT_IP → 设事件标志 + 回调 on_sta_wifi_connected()
│
├─ esp_wifi_set_mode(WIFI_MODE_STA)
├─ esp_wifi_start() + esp_wifi_connect()
│
└─ 阻塞等待 10 秒
    ├─ 拿到 IP → return true  ✅
    └─ 超时 → return false     ❌

🌐 Web 配网服务器层 （web_server_bridge.c）
方法
路径
功能
GET
/
返回移动端配网 HTML 页面
POST
/config
接收 SSID/密码，触发回调

POST /config 到达
│
├─ 解析 POST body（URL decode + key=value 解析）
├─ 返回"提交成功"HTML 给手机
├─ 保存 SSID/密码到全局变量 g_pending_ssid / g_pending_pass
└─ 启动 FreeRTOS 一次性定时器（100ms 后触发）
    │
    └─ defer_timer_cb()
        └─ g_cb(ssid, pass)               ← 在 HTTP handler 外部执行回调
            │
            └─ on_wifi_config_submit()     ← 回到 wifi.c 的配网提交处理


💾 NVS 持久化存储层（wifi_nvs.c）

接口	功能	关键逻辑
wifi_nvs_load()	读取凭据	SSID 为空视为无效，不设 valid 标志
wifi_nvs_save()	保存凭据	空 SSID 拒绝写入
wifi_nvs_clear()	清除凭据	擦除整个命名空间（恢复出厂/重新配网）
命名空间："wifi_info"
键："ssid" / "pass"
结构体 wifi_store_t：ssid[33] + password[65] + valid 标志

🎯 配网提交回调 （on_wifi_config_submit）

on_wifi_config_submit(ssid, pass)
│
├─ ① wifi_nvs_save(&store)                ← 持久化保存凭据
│
├─ ② wifi_stop_all()                      ← 关闭 AP + Web 服务
│
└─ ③ wifi_start_sta_mode(ssid, pass)      ← 切换到 STA 尝试联网
      │
      ├─ 成功 → 事件触发 on_sta_wifi_connected()
      │         └─ TODO: 在这里加你的 TCP/MQTT/HTTP 业务代码
      │
      └─ 失败 → wifi_start_ap_mode()      ← 回退到 AP 配网，让用户重试

📊 状态机总结

                    ┌──────────────────────────────────┐
                    │         wifi_conect_init()        │
                    │         (唯一入口)                 │
                    └──────────────┬───────────────────┘
                                   │
                          NVS 有保存WiFi?
                          ┌─────┴─────┐
                          │           │
                         有          无
                          │           │
                          ▼           ▼
                   ┌──────────┐  ┌──────────┐
                   │ STA 模式  │  │ AP 模式   │
                   │ (连路由器)│  │ (开热点)  │
                   └────┬─────┘  └────┬─────┘
                        │              │
                   ┌────┴────┐   手机连热点配网
                  成功      失败       │
                   │        │      POST /config
                   ▼        ▼          │
              ┌────────┐ ┌──────────┐  │
              │ 业务代码│ │ AP 配网  │◄─┘
              │ 运行   │ │ (回退)   │
              └────────┘ └──────────┘
                          
              断线 → 自动重连（WIFI_EVENT_STA_DISCONNECTED）

⚡ 一句话总结
这是一个智能 WiFi 配网系统：上电后自动检测是否有历史凭据→有就直接连路由器、没有就开热点等手机配网→配网成功后持久化凭据→下次上电自动联网。整个过程对用户来说只需第一次用手机连热点填个表单，之后完全无感。
# 🚗 Xiaohulu AI 智能小车

> 基于 ESP32-S3-N16R8 | ESP-IDF v5.4.x | FreeRTOS

---

## 硬件平台

| 项目 | 参数 |
|------|------|
| 主控 | ESP32-S3 |
| Flash | 16 MB |
| PSRAM | 8 MB (Octal) |
| CPU | 240 MHz 双核 |
| 分区表 | `16m_ota.csv` (双 OTA 分区) |

---

## 已实现功能

### ☁️ OneNET Studio 物联网平台

- **MQTT 连接** — 设备鉴权、属性上报/下发/获取
- **物模型** — 支持 `property/set`、`property/get`、`property/post`
- **OTA 远程升级** — 版本上报 → 任务检测 → 固件下载 → 自动重启

**物模型属性：**

| 属性 | 类型 | 说明 | 硬件 |
|------|------|------|------|
| `LightSwitch` | bool | LED 开关 | GPIO 15 (PWM) |
| `Brightness` | int | LED 亮度 (0–100) | GPIO 15 (PWM) |
| `RGBColor` | object | WS2812 颜色 | GPIO 38 (RMT ×3) |

### 📡 WiFi 配网

- **智能配网** — 优先读取 IDF 内置 NVS 中的 WiFi 配置直连，无密码或重连失败自动回退 AP 配网
- **AP 配网模式** — 热点 `ESP32S3_AP` / `qwer1234`，网页 `http://192.168.100.1`
- **WebSocket + JSON 协议** — 扫描周边 WiFi、选择并连接路由器
- **SPIFFS 网页** — `apcfg.html` 打包到 `html` 分区
- **NVS 持久化** — 利用 IDF 内置 `esp_wifi_set_storage(WIFI_STORAGE_FLASH)` 自动保存/恢复 WiFi 配置

```
wifi_apcfg_init()
    │
    ├─ wifi_manager_init()          // WiFi 框架初始化 + NVS 持久化
    ├─ load_html_from_spiffs()      // 提前加载网页到内存
    ├─ 创建 apcfg_task              // 配网任务（等 APCFG_BIT 信号）
    │
    └─ esp_wifi_get_config()        // 读 IDF 内置 NVS
         │
         ├── 有保存 ──→ wifi_manager_connect(saved)
         │                    │
         │                    ├── 连接成功 → 完成
         │                    └── 重连 6 次均失败
         │                         └── wifi_apcfg_start()  // 回退配网
         │
         └── 没保存 ──→ wifi_apcfg_start()  // 直接配网

配网收到密码 → esp_wifi_set_config() 自动保存到 NVS → 连接
```

### 💡 外设驱动

| 驱动 | 接口 | 说明 |
|------|------|------|
| WS2812 | GPIO 38 (RMT) | 3 颗灯珠，独立 RGB + 亮度控制 |
| LEDC PWM | GPIO 15 | 12-bit PWM 调光 |

---

## 目录结构

```
├── main/
│   └── main.c                            # 应用入口
├── components/
│   ├── BSP/
│   │   ├── src/
│   │   │   ├── bsp_board.c               # 板级硬件初始化
│   │   │   ├── bsp_audio.c               # 音频驱动
│   │   │   └── bsp_ws2812.c              # WS2812 驱动
│   │   └── include/
│   │       ├── bsp_board.h
│   │       ├── bsp_audio.h
│   │       └── bsp_ws2812.h
│   └── Middlewares/
│       ├── include/
│       │   ├── wifi_manager.h            # WiFi + AP 配网 + WebSocket 接口
│       │   ├── mqtt_onenet.h             # OneNET MQTT + 物模型 + OTA 接口
│       │   ├── onenet_mqtt_key.h         # 平台三元组密钥
│       │   ├── ble.h
│       │   └── audio.h
│       ├── src/
│       │   ├── network/
│       │   │   ├── wifi_manager.c        # WiFi STA/AP/扫描/配网/WebSocket
│       │   │   └── mqtt_onenet.c         # MQTT + 物模型 + Token + OTA
│       │   ├── audio/
│       │   ├── ble/
│       │   └── car/
│       └── HTML/
│           └── apcfg.html                # 配网网页
├── 16m_ota.csv                           # OTA 分区表
└── sdkconfig                             # ESP-IDF 配置
```

---

## 快速开始

```bash
# 编译
idf.py build

# 烧录 + 串口监视
idf.py -p COMx flash monitor
```

> 修改 `apcfg.html` 后需重新烧录（网页存储在 SPIFFS 分区）。

---

## 启动流程

```
app_main()
 ├─ wifi_apcfg_init(wifi_state_handle)    // 初始化 + 自动判断配网方式
 │     │
 │     ├─ WiFi 框架启动 + NVS 持久化
 │     ├─ 检查是否有保存的 WiFi 配置
 │     │    ├─ 有 → 直接连接路由器
 │     │    └─ 无 → 开启 AP 热点 + WebSocket 配网
 │     │
 │     └─ 连接成功 → 回调 wifi_state_handle(WIFI_STATE_CONNECTED)
 │                          │
 │                          ├─ onenet_dm_init()          // 初始化 LED/WS2812
 │                          └─ onenet_mqtt_start()       // 连接 OneNET MQTT
 │                                └─ 订阅主题 → 上报属性
```

---

## WebSocket JSON 协议

### 网页 → 设备（上行）

**启动扫描：**

```json
{
  "scan": "start"
}
```

**提交要连接的路由器：**

```json
{
  "ssid": "你的路由器名称",
  "password": "你的密码"
}
```

开放网络时 `password` 可为空字符串 `""`（字段仍需存在）。

### 设备 → 网页（下行）

**扫描结果：**

```json
{
  "wifi_list": [
    {
      "ssid": "test1",
      "rssi": -23,
      "encrypted": true
    },
    {
      "ssid": "test2",
      "rssi": -70,
      "encrypted": false
    }
  ]
}
```

| 字段 | 含义 |
|------|------|
| `ssid` | 热点名称 |
| `rssi` | 信号强度（dBm，越大越好） |
| `encrypted` | `true` 需密码 / `false` 开放网络 |

### 收发处理位置

| 方向 | 处理函数 | 文件 |
|------|----------|------|
| 收网页 JSON | `ws_receive_cb()` | `wifi_manager.c` |
| 扫描完成上报 | `wifi_scan_cb()` | `wifi_manager.c` |
| 真正连接路由器 | `apcfg_task()` → `wifi_manager_connect()` | `wifi_manager.c` |

> 连接动作不在 WebSocket 回调里直接做，而是置事件位由 `apcfg_task` 异步执行，避免在 WS 底层回调里关闭服务器。

---

## 手机配网操作步骤

1. 给板子上电，串口应看到：
   - `wifi_manager: wifi_manager 初始化完成`
   - `ap_wifi: 无保存的 WiFi 密码，进入 AP 配网模式`
2. 手机连接 WiFi 热点：`ESP32S3_AP` / 密码 `qwer1234`
3. 浏览器打开：`http://192.168.100.1`
4. 页面显示 WebSocket 已连接后，点 **扫描 WiFi**
5. 在列表中点选路由器，或手动输入 SSID
6. 输入密码（开放网络可留空），点 **保存并连接**
7. 设备关闭配网页服务器，切 STA 连路由器，密码自动保存到 NVS
8. 串口出现 `获取到 IP 地址` / `WIFI 连接成功` 即成功
9. 下次上电将自动用保存的密码直连，无需重新配网

---

## 分区表

| 分区 | 类型 | 大小 | 说明 |
|------|------|------|------|
| nvs | data | 24 KB | 系统 NVS（含 WiFi 配置） |
| phy_init | data | 4 KB | RF 校准 |
| otadata | data | 8 KB | OTA 状态 |
| ota_0 | app | 2 MB | 固件槽 0 |
| ota_1 | app | 2 MB | 固件槽 1 |
| html | spiffs | 64 KB | 配网网页 |

---

## 关键回调

`main.c` 注册：

```c
void wifi_state_handle(WIFI_STATE state);
```

| 状态 | 含义 | 典型日志 |
|------|------|----------|
| `WIFI_STATE_CONNECTED` | STA 已获取 IP | `WIFI 连接成功` |
| `WIFI_STATE_DISCONNECTED` | 曾连上后断开 | `WIFI 连接失败` |

由 `wifi_manager.c` 事件处理在 `IP_EVENT_STA_GOT_IP` / `WIFI_EVENT_STA_DISCONNECTED` 中触发。

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

- **AP 配网模式** — 热点 `ESP32S3_AP` / `qwer1234`，网页 `http://192.168.100.1`
- **WebSocket + JSON 协议** — 扫描周边 WiFi、选择并连接路由器
- **SPIFFS 网页** — `apcfg.html` 打包到 `html` 分区

### 💡 外设驱动

| 驱动 | 接口 | 说明 |
|------|------|------|
| WS2812 | GPIO 38 (RMT) | 3 颗灯珠，独立 RGB + 亮度控制 |
| LEDC PWM | GPIO 15 | 12-bit PWM 调光 |

---

## 目录结构

```
├── main/
│   └── main.c                  # 应用入口
├── components/BSP/
│   ├── IOT/                    # OneNET 云平台
│   │   ├── onenet_mqtt.*       #   MQTT 连接与主题订阅
│   │   ├── onenet_dm.*         #   物模型数据处理
│   │   ├── onenet_ota.*        #   OTA 远程升级
│   │   ├── onenet_token.*      #   Token 鉴权
│   │   └── wifi_manager.*      #   WiFi STA/AP/扫描
│   ├── WIFI_M/                 # WebSocket 配网
│   │   ├── ap_wifi.*           #   配网业务逻辑
│   │   └── ws_server.*         #   HTTP + WebSocket 服务
│   ├── WS2812/                 # WS2812 驱动 (RMT)
│   └── HTML/
│       └── apcfg.html          # 配网网页
├── 16m_ota.csv                 # OTA 分区表
└── sdkconfig                   # ESP-IDF 配置
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
 ├─ NVS 初始化
 ├─ WiFi STA 连接路由器
 ├─ OneNET 物模型初始化（LED / WS2812）
 ├─ 等待 WiFi 获取 IP
 └─ 启动 MQTT 连接 → 订阅主题 → 上报属性
```

---

## 分区表

| 分区 | 类型 | 大小 | 说明 |
|------|------|------|------|
| nvs | data | 24 KB | 系统 NVS |
| phy_init | data | 4 KB | RF 校准 |
| otadata | data | 8 KB | OTA 状态 |
| ota_0 | app | 2 MB | 固件槽 0 |
| ota_1 | app | 2 MB | 固件槽 1 |
| html | spiffs | 64 KB | 配网网页 |

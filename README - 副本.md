# 项目名称：基于 ESP32-S3-N16R8 的 xiaohulu_AI 智能小车

开发环境：ESP-IDF v5.4.x + FreeRTOS  
当前配网模块：`components/BSP/WIFI_M/`（WebSocket + JSON）

---

# 一、WiFi 配网流程（当前实现）

## 1.1 关键参数

| 项目 | 值 |
|------|-----|
| AP 热点 SSID | `ESP32S3_AP` |
| AP 热点密码 | `qwer1234` |
| 设备 AP 地址 | `192.168.100.1` |
| 配网页 | `http://192.168.100.1/` |
| WebSocket | `ws://192.168.100.1/ws` |
| 网页资源分区 | `html`（SPIFFS，见 `16m.csv`） |
| 网页源文件 | `components/BSP/HTML/apcfg.html` |

## 1.2 总体流程图

```text
ESP32 启动
    |
    v
main: app_main()
    |
    +--> nvs_flash_init()
    |
    +--> ap_wifi_init(wifi_state_handle)
    |       |
    |       +--> wifi_manager_init()     // 启动 STA 框架（空 SSID 不自动连）
    |       +--> SPIFFS 加载 apcfg.html  // /spiffs/apcfg.html → 内存
    |       +--> 创建事件组 + ap_wifi_task
    |
    +--> ap_wifi_apcfg()
            |
            +--> wifi_manager_ap()       // APSTA，热点 ESP32S3_AP
            +--> web_ws_start()          // HTTP:/  +  WS:/ws
                    |
                    v
            手机连接热点 ESP32S3_AP / qwer1234
                    |
                    v
            浏览器打开 http://192.168.100.1
                    |
                    v
            页面建立 WebSocket: /ws
                    |
          +---------+---------+
          |                   |
          v                   v
     [扫描 WiFi]          [保存并连接]
          |                   |
          v                   v
  发送 JSON:            发送 JSON:
  {"scan":"start"}      {"ssid":"...","password":"..."}
          |                   |
          v                   v
  ws_receive_cb         ws_receive_cb
  → wifi_manager_scan   保存 ssid/password
          |             置位 APCFG_BIT
          v                   |
  scan_task 扫描周边          v
          |             ap_wifi_task 收到事件
          v                   |
  wifi_scan_cb                +--> web_ws_stop()
  组装 wifi_list              +--> wifi_manager_connect()
          |                         |
          v                         v
  WS 下发列表              获取到 IP ?
  网页点选 SSID              /        \
                          是          否
                           |           |
                           v           v
              WIFI_STATE_CONNECTED   最多重试 6 次
              (Wifi connected)       (connect fail, retry)
```

## 1.3 模块职责

| 文件 | 作用 |
|------|------|
| `main/main.c` | 启动入口：NVS → `ap_wifi_init` → `ap_wifi_apcfg` |
| `WIFI_M/wifi_manager.c` | WiFi 底层：STA/AP 切换、扫描、连接、事件回调 |
| `WIFI_M/ap_wifi.c` | 配网业务：加载网页、WebSocket 收发 JSON、转接扫描/连接 |
| `WIFI_M/ws_server.c` | HTTP + WebSocket 服务器 |
| `HTML/apcfg.html` | 配网页前端（JSON 协议） |
| `参考.json` | 网页 ↔ 设备 JSON 协议示例 |

## 1.4 启动时序（代码对应）

```
app_main()
  ├─ nvs_flash_init()
  ├─ ap_wifi_init(wifi_state_handle)
  │    ├─ wifi_manager_init(f)          // STA 框架先起来，空 SSID 不自动连
  │    ├─ init_web_page_buffer()        // SPIFFS: /spiffs/apcfg.html → RAM
  │    ├─ xEventGroupCreate()
  │    └─ xTaskCreatePinnedToCore(ap_wifi_task)  // 等 APCFG_BIT 再连路由
  └─ ap_wifi_apcfg()
       ├─ wifi_manager_ap()             // APSTA + 热点 ESP32S3_AP
       └─ web_ws_start()                // 网页 + WebSocket
```

> 说明：当前版本**每次启动都进入 AP 配网**（`main.c` 直接调 `ap_wifi_apcfg()`）。  
> 尚未接入「NVS 有账号则直连 STA」逻辑；后续可在 `main` 里按是否保存过 WiFi 分支。

---

# 二、WebSocket JSON 协议

与 `参考.json` / `ap_wifi.c` / `apcfg.html` 一致。

## 2.1 网页 → 设备（上行）

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

## 2.2 设备 → 网页（下行）

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

## 2.3 收发处理位置

| 方向 | 处理函数 | 文件 |
|------|----------|------|
| 收网页 JSON | `ws_receive_cb()` | `ap_wifi.c` |
| 扫描完成上报 | `wifi_scan_cb()` | `ap_wifi.c` |
| 真正连接路由器 | `ap_wifi_task()` → `wifi_manager_connect()` | `ap_wifi.c` |

> 注意：连接动作**不在** WebSocket 回调里直接做，而是置事件位，由 `ap_wifi_task` 异步执行，避免在 WS 底层回调里关服/切模式。

---

# 三、手机配网操作步骤

1. 给板子上电，串口应看到：
   - `wifi_manager: wifi_init finished.`
   - `main: 进入 AP 配网模式`
2. 手机连接 WiFi 热点：`ESP32S3_AP` / 密码 `qwer1234`
3. 浏览器打开：`http://192.168.100.1`
4. 页面显示 WebSocket 已连接后，点 **扫描 WiFi**
5. 在列表中点选路由器，或手动输入 SSID
6. 输入密码（开放网络可留空），点 **保存并连接**
7. 设备关闭配网页服务器，切 STA 连路由器
8. 串口出现 `Get ip address` / `Wifi connected` 即成功

---

# 四、分区与烧录注意

分区表：`16m.csv`

| 名称 | 类型 | 说明 |
|------|------|------|
| `nvs` | data/nvs | 系统 NVS |
| `phy_init` | data/phy | RF 校准 |
| `factory` | app | 应用程序 |
| `html` | data/spiffs | 配网页（当前 64KB / `0x10000`） |

网页通过 CMake 打包：

```cmake
# components/BSP/CMakeLists.txt
spiffs_create_partition_image(html "${CMAKE_CURRENT_SOURCE_DIR}/HTML" FLASH_IN_PROJECT)
```

因此：

- 修改 `apcfg.html` 后，需要**重新编译并烧录**（含 `html` 分区），只烧 app 不会更新网页。
- `html` 分区大小必须 ≥ 打包后的 SPIFFS 镜像；过小会在生成 `html.bin` 时失败。

常用命令（ESP-IDF 终端 / VS Code ESP-IDF 插件）：

```bash
idf.py build
idf.py -p COMx flash
idf.py -p COMx monitor
# 或一步：
idf.py -p COMx flash monitor
```

串口以 VS Code 配置为准（当前工程常见为 `COM7`）。

---

# 五、关键状态回调

`main.c` 注册：

```c
void wifi_state_handle(WIFI_STATE state);
```

| 状态 | 含义 | 典型日志 |
|------|------|----------|
| `WIFI_STATE_CONNECTED` | STA 已获取 IP | `Wifi connected` |
| `WIFI_STATE_DISCONNECTED` | 曾连上后断开 | `Wifi disconnected` |

由 `wifi_manager.c` 事件处理在 `IP_EVENT_STA_GOT_IP` / `WIFI_EVENT_STA_DISCONNECTED` 中触发。

---

# 六、与旧版配网的差异（已废弃路径）

旧文档/旧代码（`components/BSP/WIFI/` + `wifi_conect_init()`）特点：

- HTTP POST 表单提交，`192.168.4.1`
- AP SSID：`ESP32_WIFI_CONFIG`
- 启动时读 NVS 决定 STA 直连或 AP 配网

**当前主路径已切换到 `WIFI_M`：**

- WebSocket + JSON
- AP：`ESP32S3_AP` @ `192.168.100.1`
- 支持扫描列表回传
- 启动默认进 AP 配网（NVS 自动重连待后续补充）

请以本 README 与 `WIFI_M` 源码为准。

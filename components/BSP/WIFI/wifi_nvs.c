/**
 * @file    wifi_nvs.c
 * @brief   Wi-Fi 凭据的 NVS 持久化存储
 *
 * 将 STA 模式下配网成功的 SSID 与密码存入 NVS（Non-Volatile Storage），
 * 下次上电可直接读取并自动联网，无需重新配网。
 *
 * 对外提供三个接口：
 *   - wifi_nvs_load()  读取凭据
 *   - wifi_nvs_save()  保存凭据
 *   - wifi_nvs_clear() 清除凭据（恢复出厂 / 重新配网时使用）
 */

#include "wifi_nvs.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>

/* NVS 命名空间：所有 Wi-Fi 相关键值对存放在 "wifi_info" 下 */
#define NVS_NAMESPACE "wifi_info"

/* 键名：SSID / 密码 */
#define KEY_SSID "ssid"
#define KEY_PASS "pass"

/* ========================================================================
 *  对外 API
 * ======================================================================== */

/**
 * @brief  从 NVS 读取 Wi-Fi 凭据
 * @param  out_cfg  输出参数，存放读取到的凭据
 * @return true 读取成功且 SSID 非空，false 读取失败或凭据为空
 *
 * 注意：SSID 为空时视为无效凭据，不会设置 valid 标志。
 */
bool wifi_nvs_load(wifi_store_t *out_cfg)
{
    /* 先清零，避免读到脏数据 */
    memset(out_cfg, 0, sizeof(wifi_store_t));

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return false;  /* 命名空间不存在或 NVS 未初始化 */

    /* 读取 SSID */
    size_t len = WIFI_SSID_LEN;
    err = nvs_get_str(handle, KEY_SSID, out_cfg->ssid, &len);
    if (err != ESP_OK || strlen(out_cfg->ssid) == 0)
    {
        /* SSID 读不到或为空 → 无效凭据，直接返回 */
        nvs_close(handle);
        return false;
    }

    /* 读取密码（可选字段，即使为空也不影响 valid 状态） */
    len = WIFI_PASS_LEN;
    nvs_get_str(handle, KEY_PASS, out_cfg->password, &len);

    nvs_close(handle);

    /* 标记凭据有效 */
    out_cfg->valid = true;
    return true;
}

/**
 * @brief  将 Wi-Fi 凭据写入 NVS
 * @param  cfg  待保存的凭据
 * @return true 保存成功，false 参数无效或 NVS 写入失败
 */
bool wifi_nvs_save(const wifi_store_t *cfg)
{
    /* 参数校验：空指针或无 SSID 不保存 */
    if (!cfg || strlen(cfg->ssid) == 0) return false;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return false;

    /* 写入 SSID 和密码 */
    nvs_set_str(handle, KEY_SSID, cfg->ssid);
    nvs_set_str(handle, KEY_PASS, cfg->password);

    /* 提交修改并关闭 */
    nvs_commit(handle);
    nvs_close(handle);
    return true;
}

/**
 * @brief  清除 NVS 中所有 Wi-Fi 凭据
 *
 * 适用场景：重新配网 / 恢复出厂设置 / 切换网络。
 * NVS 命名空间不存在时安全跳过。
 */
void wifi_nvs_clear(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK)
    {
        /* 擦除整个命名空间下的所有键值对 */
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

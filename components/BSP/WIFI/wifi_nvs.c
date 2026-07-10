#include "wifi_nvs.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>

#define NVS_NAMESPACE "wifi_info"
#define KEY_SSID "ssid"
#define KEY_PASS "pass"

bool wifi_nvs_load(wifi_store_t *out_cfg)
{
    memset(out_cfg, 0, sizeof(wifi_store_t));
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if(err != ESP_OK) return false;

    size_t len = WIFI_SSID_LEN;
    err = nvs_get_str(handle, KEY_SSID, out_cfg->ssid, &len);
    if(err != ESP_OK || strlen(out_cfg->ssid) == 0)
    {
        nvs_close(handle);
        return false;
    }
    len = WIFI_PASS_LEN;
    nvs_get_str(handle, KEY_PASS, out_cfg->password, &len);
    nvs_close(handle);
    out_cfg->valid = true;
    return true;
}

bool wifi_nvs_save(const wifi_store_t *cfg)
{
    if(!cfg || strlen(cfg->ssid) == 0) return false;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if(err != ESP_OK) return false;

    nvs_set_str(handle, KEY_SSID, cfg->ssid);
    nvs_set_str(handle, KEY_PASS, cfg->password);
    nvs_commit(handle);
    nvs_close(handle);
    return true;
}

void wifi_nvs_clear(void)
{
    nvs_handle_t handle;
    if(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK)
    {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
    }
}
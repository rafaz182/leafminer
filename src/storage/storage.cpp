#include <Preferences.h>
#include "storage.h"
#include "utils/log.h"

Preferences preferences;

const char TAG_STORAGE[13] = "Storage";

void storage_setup()
{
    bool success = preferences.begin("config", false);
    l_info(TAG_STORAGE, "Setup: %s", success ? "OK" : "ERROR");
}

void storage_save(const Configuration &conf)
{
    preferences.putString("wifi_ssid", conf.wifi_ssid.c_str());
    preferences.putString("wifi_password", conf.wifi_password.c_str());
    preferences.putString("wallet_address", conf.wallet_address.c_str());
    preferences.putString("pool_password", conf.pool_password.c_str());
    preferences.putString("pool_url", conf.pool_url.c_str());
    preferences.putUInt("pool_port", conf.pool_port);
    preferences.putString("blink_enabled", conf.blink_enabled.c_str());
    preferences.putUInt("blink_bright", conf.blink_brightness);
    preferences.putString("lcd_on_start", conf.lcd_on_start.c_str());
    preferences.putString("auto_update", conf.auto_update.c_str());
    preferences.end();
}

void storage_load(Configuration *conf)
{
    conf->wifi_ssid = preferences.getString("wifi_ssid", "AP 803").c_str();
    conf->wifi_password = preferences.getString("wifi_password", "rafaz01053").c_str();
    conf->wallet_address = preferences.getString("wallet_address", "bc1q4kagj74fgtkfnkym0fr8cppvd6uv7jrplt4xg7").c_str();
    conf->pool_password = "x";//preferences.getString("pool_password", "x").c_str();
    conf->pool_url = "pool.nerdminer.io";//preferences.getString("pool_url", "public-pool.io").c_str();
    conf->pool_port = (u_int32_t)3333;//preferences.getUInt("pool_port", 21496);
    conf->blink_enabled = preferences.getString("blink_enabled", "on").c_str();
    conf->blink_brightness = preferences.getUInt("blink_bright", 256);
    conf->lcd_on_start = preferences.getString("lcd_on_start", "on").c_str();
    conf->auto_update = "off";//preferences.getString("auto_update", "on").c_str();
}

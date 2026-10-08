/* Truck-07 · Classic Bluetooth (SPP) test · ESP-IDF 5.x
   Phone app: "Serial Bluetooth Terminal" (Android)
   Commands: on / off / anything else is echoed back                     */
#include <stdio.h>
#include <string.h>
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_spp_api.h"
#include "driver/gpio.h"

#define TAG         "BT"
#define DEVICE_NAME "SHIVA BL-T09"
#define LED_PIN     GPIO_NUM_2

static void send_text(uint32_t handle, const char *txt)
{
    esp_spp_write(handle, strlen(txt), (uint8_t *)txt);
}

/* ---------- SPP events (connect, data, disconnect) ---------- */
static void spp_cb(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    switch (event) {
    case ESP_SPP_INIT_EVT:                       // SPP ready -> start server
        esp_spp_start_srv(ESP_SPP_SEC_AUTHENTICATE, ESP_SPP_ROLE_SLAVE, 0, "SPP_SERVER");
        break;

    case ESP_SPP_START_EVT:                      // server running -> become visible
        esp_bt_gap_set_device_name(DEVICE_NAME);
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        ESP_LOGI(TAG, "Ready. Pair your phone with \"%s\"", DEVICE_NAME);
        break;

    case ESP_SPP_SRV_OPEN_EVT:                   // phone connected
        ESP_LOGI(TAG, "Phone connected");
        send_text(param->srv_open.handle, "Hello from Truck-07!\r\n");
        break;

    case ESP_SPP_DATA_IND_EVT: {                 // data received from phone
        char msg[64];
        int len = param->data_ind.len < 63 ? param->data_ind.len : 63;
        memcpy(msg, param->data_ind.data, len);
        msg[len] = '\0';
        msg[strcspn(msg, "\r\n")] = '\0';        // remove Enter
        ESP_LOGI(TAG, "Got: %s", msg);

        uint32_t h = param->data_ind.handle;
        if (strcmp(msg, "on") == 0)       { gpio_set_level(LED_PIN, 1); send_text(h, "LED ON\r\n"); }
        else if (strcmp(msg, "off") == 0) { gpio_set_level(LED_PIN, 0); send_text(h, "LED OFF\r\n"); }
        else {
            char reply[80];
            snprintf(reply, sizeof(reply), "Echo: %s\r\n", msg);
            send_text(h, reply);
        }
        break;
    }

    case ESP_SPP_CLOSE_EVT:                      // phone disconnected
        ESP_LOGI(TAG, "Phone disconnected");
        break;

    default:
        break;
    }
}

/* ---------- GAP events (pairing) ---------- */
static void gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_CFM_REQ_EVT:                 // "pair?" -> say yes automatically
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        ESP_LOGI(TAG, "Pairing %s", param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS ? "OK" : "FAILED");
        break;
    default:
        break;
    }
}

void app_main(void)
{
    // 1. LED
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);

    // 2. NVS (Bluetooth stores pairing keys here)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // 3. Bluetooth controller (radio) - Classic only, free BLE memory
    esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));

    // 4. Bluedroid (Bluetooth software stack)
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    // 5. Callbacks + simple pairing (no PIN, "Just Works")
    esp_bt_gap_register_callback(gap_cb);
    esp_spp_register_callback(spp_cb);
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_NONE;
    esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &iocap, sizeof(iocap));

    // 6. Start SPP (serial port profile)
    esp_spp_cfg_t spp_cfg = {
        .mode = ESP_SPP_MODE_CB,
        .enable_l2cap_ertm = true,
        .tx_buffer_size = 0,
    };
    ESP_ERROR_CHECK(esp_spp_enhanced_init(&spp_cfg));
}
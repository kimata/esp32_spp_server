#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "esp_gatts_api.h"

#define UART_NUM UART_NUM_0

#define SPP_DATA_MAX_LEN           (512)
#define SPP_CMD_MAX_LEN            (20)
#define SPP_STATUS_MAX_LEN         (20)

typedef enum {
    SPP_IDX_SVC,

    SPP_IDX_SPP_DATA_RECV_CHAR,
    SPP_IDX_SPP_DATA_RECV_VAL,

    SPP_IDX_SPP_DATA_NOTIFY_CHAR,
    SPP_IDX_SPP_DATA_NOTIFY_VAL,
    SPP_IDX_SPP_DATA_NOTIFY_CFG,

    SPP_IDX_SPP_COMMAND_CHAR,
    SPP_IDX_SPP_COMMAND_VAL,

    SPP_IDX_SPP_STATUS_CHAR,
    SPP_IDX_SPP_STATUS_VAL,
    SPP_IDX_SPP_STATUS_CFG,

    SPP_IDX_NB,
} spp_index_t;

#define ESP_SPP_APP_ID              0x56
#define SPP_PROFILE_NUM             1
#define SPP_PROFILE_APP_IDX         0

#define TAG  "ESP32_BLE_SPP"

typedef struct gatts_spp_status {
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t connection_id;
    uint16_t is_connected;
    uint16_t is_notify_enabled;
    uint16_t mtu_size;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle;
    esp_bt_uuid_t char_uuid;
    esp_gatt_perm_t perm;
    esp_gatt_char_prop_t property;
    uint16_t descr_handle;
    esp_bt_uuid_t descr_uuid;
} gatts_spp_status_t;

void handle_uart_remote_data(uint8_t *str, uint32_t len);
void handle_uart_remote_data_prep(uint8_t *str, uint32_t len);
void handle_uart_remote_data_exec();
void handle_command(uint8_t *str, uint32_t len);


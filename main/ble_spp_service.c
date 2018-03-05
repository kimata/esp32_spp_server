#include "esp32_spp_server.h"

#include "esp_gap_ble_api.h"
#include "esp_gatt_defs.h"
#include "esp_gatts_api.h"
#include "esp_log.h"

#define SPP_SVC_INST_ID                     0

#define ESP_GATT_UUID_SPP_DATA_RECEIVE      0xABF1
#define ESP_GATT_UUID_SPP_DATA_NOTIFY       0xABF2
#define ESP_GATT_UUID_SPP_COMMAND_RECEIVE   0xABF3
#define ESP_GATT_UUID_SPP_COMMAND_NOTIFY    0xABF4

#define CHAR_DECLARATION_SIZE               (sizeof(uint8_t))

static const uint16_t PRIM_SERVICE_UUID         = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t CHAR_DECL_UUID            = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t CHAR_CLIENT_CONFIG_UUID   = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

static const uint8_t  CHAR_PROP_READ_NOTIFY     = ESP_GATT_CHAR_PROP_BIT_READ|ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t  CHAR_PROP_READ_WRITE      = ESP_GATT_CHAR_PROP_BIT_WRITE_NR|ESP_GATT_CHAR_PROP_BIT_READ;

static const uint16_t SPP_SERVICE_UUID = 0xABF0;

static const uint16_t SPP_DATA_RECV_UUID        = ESP_GATT_UUID_SPP_DATA_RECEIVE;
static const uint8_t  SPP_DATA_RECV_VAL[20]     = { 0x00 };

static const uint16_t SPP_DATA_NOTIFY_UUID      = ESP_GATT_UUID_SPP_DATA_NOTIFY;
static const uint8_t  SPP_DATA_NOTIFY_VAL[20]   = { 0x00 };
static const uint8_t  SPP_DATA_NOTIFY_CCC[2]    = { 0x00, 0x00 };

static const uint16_t SPP_COMMAND_UUID          = ESP_GATT_UUID_SPP_COMMAND_RECEIVE;
static const uint8_t  SPP_COMMAND_VAL[10]       = {0x00};

static const uint16_t SPP_STATUS_UUID           = ESP_GATT_UUID_SPP_COMMAND_NOTIFY;
static const uint8_t  SPP_STATUS_VAL[10]        = { 0x00 };
static const uint8_t  SPP_STATUS_CCC[2]         = { 0x00, 0x00 };

#define DEVICE_NAME          "ESP_SPP_SERVER"

static const uint8_t SPP_ADV_DATA[23] = {
    0x02, // Length
    0x01, // AD Type (Flags)
    0x06, // Flags (LE General Discoverable Mode, BR/EDR Not Supported)

    0x03, // Length
    0x03, // AD Type (Complete List of 16-bit Service Class UUIDs)
    0xF0,
    0xAB,

    0x0F, // Length
    0x09, // AD Type (Complete Local Name)
    // E    S    P    _    S    P    P    _    S    E    R    V    E    R
    0x45,0x53,0x50,0x5f,0x53,0x50,0x50,0x5f,0x53,0x45,0x52,0x56,0x45,0x52
};

static esp_ble_adv_params_t spp_adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x40,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

uint16_t spp_handle_table[SPP_IDX_NB];

const esp_gatts_attr_db_t SPP_GATT_DB[SPP_IDX_NB] =
{
    // Service Declaration
    [SPP_IDX_SVC] = {
        { ESP_GATT_AUTO_RSP },
        {
            ESP_UUID_LEN_16, (uint8_t *)&PRIM_SERVICE_UUID,
            ESP_GATT_PERM_READ,
            sizeof(SPP_SERVICE_UUID), sizeof(SPP_SERVICE_UUID),
            (uint8_t *)&SPP_SERVICE_UUID
        }
    },

    // Data Receive: characteristic declaration
    [SPP_IDX_SPP_DATA_RECV_CHAR] = {
        { ESP_GATT_AUTO_RSP },
        {
            ESP_UUID_LEN_16, (uint8_t *)&CHAR_DECL_UUID,
            ESP_GATT_PERM_READ,
            CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
            (uint8_t *)&CHAR_PROP_READ_WRITE
        }
    },

    // Data Receive: characteristic value
    [SPP_IDX_SPP_DATA_RECV_VAL] = {
        { ESP_GATT_AUTO_RSP },
        {
            ESP_UUID_LEN_16, (uint8_t *)&SPP_DATA_RECV_UUID,
            ESP_GATT_PERM_WRITE,
            SPP_DATA_MAX_LEN, sizeof(SPP_DATA_RECV_VAL),
            (uint8_t *)SPP_DATA_RECV_VAL
        }
    },

    // Data Notify: characteristic declaration
    [SPP_IDX_SPP_DATA_NOTIFY_CHAR] = {
        { ESP_GATT_AUTO_RSP },
        {
            ESP_UUID_LEN_16, (uint8_t *)&CHAR_DECL_UUID,
            ESP_GATT_PERM_READ,
            CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
            (uint8_t *)&CHAR_PROP_READ_NOTIFY
        }
    },

    // Data notify: characteristic value
    [SPP_IDX_SPP_DATA_NTY_VAL] = {
        { ESP_GATT_AUTO_RSP },
        {
            ESP_UUID_LEN_16, (uint8_t *)&SPP_DATA_NOTIFY_UUID,
            ESP_GATT_PERM_READ,
            SPP_DATA_MAX_LEN, sizeof(SPP_DATA_NOTIFY_VAL),
            (uint8_t *)SPP_DATA_NOTIFY_VAL
        }
    },

    // Data notify: client characteristic configuration descriptor
    [SPP_IDX_SPP_DATA_NTF_CFG] = {
        { ESP_GATT_AUTO_RSP },
        {
            ESP_UUID_LEN_16, (uint8_t *)&CHAR_CLIENT_CONFIG_UUID,
            ESP_GATT_PERM_READ|ESP_GATT_PERM_WRITE,
            sizeof(uint16_t), sizeof(SPP_DATA_NOTIFY_CCC),
            (uint8_t *)SPP_DATA_NOTIFY_CCC
        }
    },

    // Command: characteristic declaration
    [SPP_IDX_SPP_COMMAND_CHAR] = {
        { ESP_GATT_AUTO_RSP },
        {
            ESP_UUID_LEN_16, (uint8_t *)&CHAR_DECL_UUID,
            ESP_GATT_PERM_READ,
            CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
            (uint8_t *)&CHAR_PROP_READ_WRITE
        }
    },

    // Command: characteristic value
    [SPP_IDX_SPP_COMMAND_VAL] = {
        { ESP_GATT_AUTO_RSP },
        {
            ESP_UUID_LEN_16, (uint8_t *)&SPP_COMMAND_UUID,
            ESP_GATT_PERM_READ|ESP_GATT_PERM_WRITE,
            SPP_CMD_MAX_LEN, sizeof(SPP_COMMAND_VAL),
            (uint8_t *)SPP_COMMAND_VAL
        }
    },

    // Status: characteristic declaration
    [SPP_IDX_SPP_STATUS_CHAR] = {
        { ESP_GATT_AUTO_RSP },
        {
            ESP_UUID_LEN_16, (uint8_t *)&CHAR_DECL_UUID,
            ESP_GATT_PERM_READ,
            CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
            (uint8_t *)&CHAR_PROP_READ_NOTIFY
        }
    },

    // Status: characteristic value
    [SPP_IDX_SPP_STATUS_VAL] = {
        { ESP_GATT_AUTO_RSP },
        {
            ESP_UUID_LEN_16, (uint8_t *)&SPP_STATUS_UUID,
            ESP_GATT_PERM_READ,
            SPP_STATUS_MAX_LEN, sizeof(SPP_STATUS_VAL),
            (uint8_t *)SPP_STATUS_VAL
        }
    },

    // Status: client characteristic configuration descriptor
    [SPP_IDX_SPP_STATUS_CFG] = {
        { ESP_GATT_AUTO_RSP },
        {
            ESP_UUID_LEN_16, (uint8_t *)&CHAR_CLIENT_CONFIG_UUID,
            ESP_GATT_PERM_READ|ESP_GATT_PERM_WRITE,
            sizeof(uint16_t), sizeof(SPP_STATUS_CCC),
            (uint8_t *)SPP_STATUS_CCC
        }
    },
};

static void gatts_profile_event_handler(esp_gatts_cb_event_t event,
                                        esp_gatt_if_t gatts_if,
                                        esp_ble_gatts_cb_param_t *param);

gatts_profile_inst_t spp_profile_tab[SPP_PROFILE_NUM] = {
    [SPP_PROFILE_APP_IDX] = {
        .gatts_cb = gatts_profile_event_handler,
        .gatts_if = ESP_GATT_IF_NONE,
        .mtu_size = 23,
    },
};

gatts_profile_inst_t *gatts_profile()
{
    return &(spp_profile_tab[SPP_PROFILE_APP_IDX]);
}

uint16_t gatts_handle(spp_index_t index)
{
    return spp_handle_table[SPP_IDX_SPP_DATA_NTY_VAL];
}

void gatts_event_handler(esp_gatts_cb_event_t event,
                         esp_gatt_if_t gatts_if,
                         esp_ble_gatts_cb_param_t *param)
{
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            gatts_profile()->gatts_if = gatts_if;
        } else {
            ESP_LOGE(TAG, "Failed to regist application.");
            return;
        }
    }

    for (int i = 0; i < SPP_PROFILE_NUM; i++) {
        if ((gatts_if == ESP_GATT_IF_NONE) ||
            (gatts_if == spp_profile_tab[i].gatts_if)) {
            if (spp_profile_tab[i].gatts_cb != NULL) {
                spp_profile_tab[i].gatts_cb(event, gatts_if, param);
            }
        }
    }
}

static uint8_t find_gatts_index(uint16_t handle)
{
    for (uint32_t i = 0; i < SPP_IDX_NB ; i++) {
        if (handle == spp_handle_table[i]) {
            return i;
        }
    }
    return 0xFF;
}

void handle_gatts_write_event(uint8_t res, esp_ble_gatts_cb_param_t *param)
{
    if (param->write.is_prep == true) {
        switch (res) {
        case SPP_IDX_SPP_DATA_RECV_VAL:
            handle_uart_remote_data_prep(param->write.value, param->write.len);
            break;
        default:
            break;
        }
    } else {
        switch (res) {
        case SPP_IDX_SPP_COMMAND_VAL:
            handle_command(param->write.value, param->write.len);
            break;
        case SPP_IDX_SPP_DATA_NTF_CFG:
            if (param->write.len != 2) {
                break;
            }
            if ((param->write.value[0] == 0x01) && (param->write.value[1] == 0x00)){
                gatts_profile()->is_notify_enabled = true;
            } else if ((param->write.value[0] == 0x00) && (param->write.value[1] == 0x00)) {
                gatts_profile()->is_notify_enabled = false;
            }
            break;
        case SPP_IDX_SPP_DATA_RECV_VAL:
            handle_uart_remote_data(param->write.value, param->write.len);
            break;
        default:
            break;
        }
    }
}

void handle_gatts_exec_write_event(esp_ble_gatts_cb_param_t *param)
{
    if (param->exec_write.exec_write_flag) {
        handle_uart_remote_data_exec();
    }
}

void gatts_profile_event_handler(esp_gatts_cb_event_t event,
                                 esp_gatt_if_t gatts_if,
                                        esp_ble_gatts_cb_param_t *param)
{
    esp_ble_gatts_cb_param_t *p_data = (esp_ble_gatts_cb_param_t *) param;
    uint8_t res = find_gatts_index(p_data->read.handle);

    switch (event) {
    case ESP_GATTS_REG_EVT:
        esp_ble_gap_set_device_name(DEVICE_NAME);
        esp_ble_gap_config_adv_data_raw((uint8_t *)SPP_ADV_DATA, sizeof(SPP_ADV_DATA));
        esp_ble_gatts_create_attr_tab(SPP_GATT_DB, gatts_if, SPP_IDX_NB, SPP_SVC_INST_ID);
        break;
    case ESP_GATTS_READ_EVT:
        if (res == SPP_IDX_SPP_STATUS_VAL) {
            // TODO: client read the status characteristic
        }
        break;
    case ESP_GATTS_WRITE_EVT:
        handle_gatts_write_event(res, p_data);
        break;
    case ESP_GATTS_EXEC_WRITE_EVT:
        handle_gatts_exec_write_event(p_data);
        break;
    case ESP_GATTS_MTU_EVT:
        gatts_profile()->mtu_size = p_data->mtu.mtu;
        break;
    case ESP_GATTS_CONNECT_EVT:
        gatts_profile()->connection_id = p_data->connect.conn_id;
        gatts_profile()->gatts_if = gatts_if;
        gatts_profile()->is_connected = true;
        break;
    case ESP_GATTS_DISCONNECT_EVT:
        gatts_profile()->is_connected = false;
        gatts_profile()->is_notify_enabled = false;
        esp_ble_gap_start_advertising(&spp_adv_params);
        break;
    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        if (param->add_attr_tab.status != ESP_GATT_OK){
            ESP_LOGE(TAG, "Failed to create attribute table.");
            break;
        }
        memcpy(spp_handle_table, param->add_attr_tab.handles, sizeof(spp_handle_table));
        esp_ble_gatts_start_service(spp_handle_table[SPP_IDX_SVC]);
        break;
    default:
        break;
    }
}

void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(&spp_adv_params);
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "Failed to start advertising.");
        }
        break;
    default:
        break;
    }
}

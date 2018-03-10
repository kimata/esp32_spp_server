#include "esp32_spp_server.h"
#include "ble_spp_service.h"
#include "str_buf.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "driver/uart.h"

#include "esp_bt.h"
#include "esp_bt_main.h"

#include "esp_log.h"
#include "esp_system.h"

#include "nvs_flash.h"

static xQueueHandle cmd_queue = NULL;

////////////////////////////////////////////////////////////////////////////////
// UART function
static void uart_write(uint8_t *str, uint32_t len)
{
    uart_write_bytes(UART_NUM, (char *)str, len);
}

static void uart_read(uint8_t *buf, uint32_t len)
{
    uart_read_bytes(UART_NUM, buf, len, portMAX_DELAY);
}

////////////////////////////////////////////////////////////////////////////////
// UART handler: Local to Remote
// Read UART data and send it to remote via BLE.
void handle_uart_local_data(uint8_t *str, uint32_t len)
{
    uint8_t *buf;
    uint32_t mtu_size = gatts_spp_status()->mtu_size;

    if (len <= (mtu_size - 3)) {
        esp_ble_gatts_send_indicate(gatts_spp_status()->gatts_if,
                                    gatts_spp_status()->connection_id,
                                    gatts_handle(SPP_IDX_SPP_DATA_NOTIFY_VAL),
                                    len,
                                    str,
                                    false);
    } else {
        uint32_t current_num = 0;
        uint32_t max_data_size = mtu_size - 3;
        uint32_t total_num = (len - 1) / max_data_size + 1;

        buf = (uint8_t *)malloc(max_data_size*sizeof(uint8_t));
        if (buf == NULL) {
            ESP_LOGE(TAG_SPP, "Failed to malloc at %s.", __func__);
            return;
        }

        while (current_num < total_num) {
            uint32_t data_size;

            if (current_num == (total_num - 1)) {
                data_size = len % max_data_size;
            } else {
                data_size = max_data_size;
            }
            memcpy(buf, str, data_size);

            esp_ble_gatts_send_indicate(gatts_spp_status()->gatts_if,
                                        gatts_spp_status()->connection_id,
                                        gatts_handle(SPP_IDX_SPP_DATA_NOTIFY_VAL),
                                        data_size,
                                        buf, false);

            vTaskDelay(20 / portTICK_PERIOD_MS);
            current_num++;
            str += max_data_size;
        }
        free(buf);
    }
}

void uart_task(void *pvParameters)
{
    static QueueHandle_t uart_queue = NULL;
    uart_event_t event;

    uart_driver_install(UART_NUM, 4096, 8192, 10,&uart_queue,0);

    while (1) {
        uint8_t *buf;
        uint32_t len;

        if (xQueueReceive(uart_queue, (void * )&event, (portTickType)portMAX_DELAY) == pdFALSE) {
            continue;
        }
        if ((event.type != UART_DATA) || (event.size == 0)) {
            continue;
        }

        len = event.size;
        buf = (uint8_t *)malloc(sizeof(uint8_t)*len);
        if (buf == NULL) {
            ESP_LOGE(TAG_SPP, "Failed to malloc at %s.", __func__);
            break;
        }
        uart_read(buf, len);

        do {
            if (!gatts_spp_status()->is_connected) {
                ESP_LOGI(TAG_SPP, "BLE is NOT connected.");
                break;
            }
            if (!gatts_spp_status()->is_notify_enabled) {
                ESP_LOGI(TAG_SPP, "Data Notify is NOT enabled.");
                break;
            }
            handle_uart_local_data(buf, len);
        } while (0);
        free(buf);
    }
    vTaskDelete(NULL);
}

////////////////////////////////////////////////////////////////////////////////
// UART handler: Remote to Local
// Receive UART data via BLE and write data.
void handle_uart_remote_data(uint8_t *str, uint32_t len)
{
    uart_write(str, len);
}

void handle_uart_remote_data_prep(uint8_t *str, uint32_t len)
{
    str_buf_store(str, len);
}

void handle_uart_remote_data_exec()
{
    str_buf_iter(uart_write);
    str_buf_clear();

}

////////////////////////////////////////////////////////////////////////////////
// Command handler
void handle_command(uint8_t *str, uint32_t len)
{
    uint8_t *spp_cmd_buff;

    spp_cmd_buff = (uint8_t *)malloc(sizeof(uint8_t)*(len));

    if(spp_cmd_buff == NULL){
        ESP_LOGE(TAG_SPP, "Failed to malloc at %s.", __func__);
        return;
    }
    memcpy(spp_cmd_buff, str, len);
    spp_cmd_buff[len-1] = '\0'; // NOTE: a measures if there is a bug on client

    xQueueSend(cmd_queue, &spp_cmd_buff, 10/portTICK_PERIOD_MS);
}

void command_task(void * arg)
{
    uint8_t * cmd_id;
    cmd_queue = xQueueCreate(10, sizeof(uint32_t));

    while (1) {
        vTaskDelay(50 / portTICK_PERIOD_MS);
        if (xQueueReceive(cmd_queue, &cmd_id, portMAX_DELAY) == pdFALSE) {
            continue;
        }
        esp_log_buffer_char(TAG_SPP,(char *)(cmd_id),strlen((char *)cmd_id));
        free(cmd_id);
    }
    vTaskDelete(NULL);
}

////////////////////////////////////////////////////////////////////////////////
// Initializer
static void uart_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };

    uart_param_config(UART_NUM, &uart_config);
    uart_set_pin(UART_NUM,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

////////////////////////////////////////////////////////////////////////////////
// Command
static void spp_task_init(void)
{
    xTaskCreate(uart_task, "uart_task", 2048, (void*)UART_NUM, 8, NULL);
    xTaskCreate(command_task, "command_task", 2048, NULL, 10, NULL);
}

////////////////////////////////////////////////////////////////////////////////
void app_main()
{
    esp_err_t ret;
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    esp_ble_gatts_register_callback(gatts_event_handler);
    esp_ble_gap_register_callback(gap_event_handler);
    esp_ble_gatts_app_register(ESP_SPP_APP_ID);

    ESP_LOGI(TAG_SPP, "BLE intialization is done.");

    uart_init();
    spp_task_init();

    ESP_LOGI(TAG_SPP, "Task is started.");

    return;
}

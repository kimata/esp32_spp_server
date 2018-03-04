#include "ble_spp_server.h"

#include <stdlib.h>
#include <stdint.h>

#include "esp_log.h"

typedef struct str_buf_node {
    uint32_t len;
    uint8_t *str;
    struct str_buf_node *next;
} str_buf_node_t;

typedef struct str_buf {
    uint32_t node_count;
    uint32_t buff_size;
    str_buf_node_t *first_node;
    str_buf_node_t *last_node;
} str_buf_t;

static str_buf_t str_buf = {
    .node_count = 0,
    .buff_size  = 0,
    .first_node = NULL,
    .last_node  = NULL,
};

static str_buf_node_t *str_buf_make_node(uint8_t *str, uint32_t len)
{
    str_buf_node_t *node = (str_buf_node_t *)malloc(sizeof(str_buf_node_t));

    if (node == NULL) {
        ESP_LOGE(TAG, "Failed to malloc at %s.\n", __func__);
        return NULL;
    }

    node->len = len;
    node->str = (uint8_t *)malloc(len);
    if (node->str == NULL) {
        ESP_LOGE(TAG, "Failed to malloc at %s.\n", __func__);
        free(node);
        return NULL;
    }
    memcpy(node->str, str, len);
    node->next = NULL;

    return node;
}

void str_buf_store(uint8_t *str, uint32_t len)
{
    str_buf_node_t *node = str_buf_make_node(str, len);

    str_buf.buff_size += node->len;
    if (str_buf.last_node != NULL) {
        str_buf.last_node->next = node;
    }
    str_buf.last_node = node;

    if (str_buf.node_count == 0) {
        str_buf.first_node = node;
    }
    str_buf.node_count++;
}

void str_buf_clear(void)
{
    str_buf_node_t *node = str_buf.first_node;

    while (node != NULL) {
        free(node->str);
        free(node);
        node = node->next;
    }
    str_buf.node_count = 0;
    str_buf.buff_size = 0;
    str_buf.first_node = NULL;
    str_buf.last_node = NULL;
}

void str_buf_iter(void (*func)(uint8_t *, uint32_t))
{
    str_buf_node_t *node = str_buf.first_node;

    while (node != NULL) {
        func(node->str, node->len);
        node = node->next;
    }
}

#include <stdint.h>

void str_buf_store(uint8_t *str, uint32_t len);
void str_buf_clear(void);
void str_buf_iter(void (*func)(uint8_t *, uint32_t));

#ifndef B_OLED_H
#define B_OLED_H

#include <stdint.h>
#include "b_soft_i2c.h"

/* 屏幕页式显存的总字节数 */
#define OLED_FRAME_BYTES  (128U * 8U)

/* 初始化屏幕，并清空显示内容 */
I2C_Status OLED_Init(void);

/* 用相同字节填充全部显存 */
I2C_Status OLED_Fill(uint8_t value);

/*
 * 发送完整的页式显存
 * 排列方式：第零页的一百二十八列，随后依次为其余各页
 * 数据长度必须为 OLED_FRAME_BYTES
 */
I2C_Status OLED_WriteFrame(const uint8_t *page_buf, uint16_t len);

#endif

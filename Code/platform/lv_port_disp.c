#include "lv_port_disp.h"
#include "b_oled.h"
#include <string.h>

#define DISP_WIDTH          128U
#define DISP_HEIGHT         64U
#define DISP_PIXEL_BYTES    (DISP_WIDTH * DISP_HEIGHT / 8U)
#define DISP_DRAW_BYTES     (DISP_PIXEL_BYTES + 8U)

/* 使用三十二位数组，满足当前配置要求的四字节对齐 */
static uint32_t s_draw_buf[DISP_DRAW_BYTES / sizeof(uint32_t)];
static uint8_t s_page_buf[DISP_PIXEL_BYTES];
static lv_display_t *s_display;

/* 供调试器观察，不通过这些变量跨任务操作界面 */
volatile I2C_Status g_lvgl_oled_status = I2C_STATUS_NOT_READY;
volatile uint32_t g_lvgl_flush_count;
volatile uint32_t g_lvgl_flush_errors;

static void DisplayFlush(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    uint16_t x;
    uint16_t y;
    uint16_t src_index;
    uint16_t dst_index;
    const uint8_t *pixels;

    g_lvgl_flush_count++;

    /* 本适配层只支持完整的一百二十八乘六十四像素画面 */
    if ((area->x1 != 0) || (area->y1 != 0) || (area->x2 != 127) || (area->y2 != 63))
    {
        g_lvgl_oled_status = I2C_STATUS_PARAM;
        g_lvgl_flush_errors++;
        lv_display_flush_ready(display);
        return;
    }

    /* 跳过单色绘制缓冲区开头的八字节调色板 */
    pixels = px_map + 8U;
    memset(s_page_buf, 0, sizeof(s_page_buf));

    for (y = 0U; y < DISP_HEIGHT; y++)
    {
        for (x = 0U; x < DISP_WIDTH; x++)
        {
            /* 源数据每行十六字节，最高位对应左侧像素 */
            src_index = y * (DISP_WIDTH / 8U) + x / 8U;

            if ((pixels[src_index] & (uint8_t)(0x80U >> (x & 7U))) != 0U)
            {
                /* 目标数据先按页，再按列排列，最低位对应页顶像素 */
                dst_index = (y / 8U) * DISP_WIDTH + x;
                s_page_buf[dst_index] |= (uint8_t)(1U << (y & 7U));
            }
        }
    }

    g_lvgl_oled_status = OLED_WriteFrame(s_page_buf, (uint16_t)sizeof(s_page_buf));

    if (g_lvgl_oled_status != I2C_STATUS_OK)
    {
        g_lvgl_flush_errors++;
    }

    /* 同步发送已经结束，此时缓冲区可以重新使用 */
    lv_display_flush_ready(display);
}

lv_display_t *LV_PortDisp_Init(void)
{
    if (s_display != NULL)
    {
        return s_display;
    }

    g_lvgl_oled_status = OLED_Init();
    if (g_lvgl_oled_status != I2C_STATUS_OK)
    {
        return NULL;
    }

    s_display = lv_display_create(DISP_WIDTH, DISP_HEIGHT);
    if (s_display == NULL)
    {
        return NULL;
    }

    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_I1);
    lv_display_set_buffers(s_display, s_draw_buf, NULL, sizeof(s_draw_buf), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(s_display, DisplayFlush);

    return s_display;
}

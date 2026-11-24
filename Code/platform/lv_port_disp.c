#include "stm32f1xx_hal.h"
#include "lv_port_disp.h"
#include "b_oled.h"
#include <string.h>

#define DISP_WIDTH          128U                                // 屏幕水平分辨率，单位：像素
#define DISP_HEIGHT         64U                                 // 屏幕垂直分辨率，单位：像素
#define DISP_PIXEL_BYTES    (DISP_WIDTH * DISP_HEIGHT / 8U)     // I1 格式每像素占 1 bit，全屏像素数据占 1024 字节
#define DISP_DRAW_BYTES     (DISP_PIXEL_BYTES + 8U)             // LVGL I1 绘图缓冲区额外预留 8 字节调色板，共 1032 字节

static uint32_t s_draw_buf[DISP_DRAW_BYTES / sizeof(uint32_t)]; // LVGL 绘图缓冲区，使用 uint32_t 数组保证至少 4 字节对齐，共 1032 字节
static uint8_t s_page_buf[DISP_PIXEL_BYTES]; // OLED 页格式缓冲区，存放转换后的 1024 字节像素数据
static lv_display_t *s_display; // LVGL 显示设备对象指针，静态初始化为 NULL

volatile I2C_Status g_lvgl_oled_status = I2C_STATUS_NOT_READY; // OLED 初始化或最近一次刷屏的通信状态
volatile uint32_t g_lvgl_flush_count; // 刷新回调累计调用次数，初始值为 0
volatile uint32_t g_lvgl_flush_errors; // 刷新回调累计错误次数，初始值为 0
volatile uint32_t g_oled_write_ms; // 最近一次整帧写入的耗时，单位：毫秒

/**
 * @brief  LVGL 显示刷新回调，将 I1 像素数据转换为 OLED 页格式并同步发送
 * @param  display  LVGL 显示设备对象指针
 * @param  area     本次刷新区域，当前仅支持完整的 128×64 像素画面
 * @param  px_map   LVGL 绘图缓冲区指针，前 8 字节为调色板，后续为像素数据
 * @return 无
 * @note   源像素按行排列，每字节最高位对应左侧像素；目标像素按页排列，
 *         每字节最低位对应页顶像素。无论发送成功或失败，均需调用
 *         lv_display_flush_ready()，通知 LVGL 本次刷新处理结束、缓冲区可复用。
 */
static void DisplayFlush(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    uint16_t x; // 当前像素的横坐标，范围为 0～127
    uint16_t y; // 当前像素的纵坐标，范围为 0～63
    uint16_t src_index; // 当前像素在 LVGL 行排列数据中的字节索引，不包含调色板偏移
    uint16_t dst_index; // 当前像素在 OLED 页排列缓冲区中的字节索引
    const uint8_t *pixels; // 指向跳过 8 字节调色板后的像素数据，通过此指针只读访问
    uint32_t start_ms; // 整帧写入开始时刻，单位：毫秒

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

    start_ms = HAL_GetTick(); // 记录整帧写入开始时刻
    g_lvgl_oled_status = OLED_WriteFrame(s_page_buf, (uint16_t)sizeof(s_page_buf));
    g_oled_write_ms = HAL_GetTick() - start_ms; // 计算本次写入的实际经过时间

    if (g_lvgl_oled_status != I2C_STATUS_OK)
    {
        g_lvgl_flush_errors++;
    }

    /* 同步发送已经结束，此时缓冲区可以重新使用 */
    lv_display_flush_ready(display);
}

/**
 * @brief  初始化 OLED 硬件并创建、配置 LVGL 显示设备
 * @param  无
 * @return 成功时返回 LVGL 显示设备对象指针；OLED 初始化失败或显示设备创建失败时返回 NULL
 * @note   调用前需完成软件 I2C 初始化和 lv_init()，并由 UI 任务统一调用。
 *         显示设备使用 128×64 分辨率、I1 单色格式、单绘图缓冲区和全屏刷新模式，
 *         通过 DisplayFlush() 完成像素格式转换与 OLED 数据发送。
 *         重复调用时直接返回已创建的显示设备对象，不重复初始化。
 */
lv_display_t *LV_PortDisp_Init(void)
{
    if (s_display != NULL)
    {
        return s_display;
    }

    g_lvgl_oled_status = OLED_Init();// 初始化 OLED 硬件
    if (g_lvgl_oled_status != I2C_STATUS_OK)
    {
        return NULL;
    }

    s_display = lv_display_create(DISP_WIDTH, DISP_HEIGHT);// 创建显示设备对象
    if (s_display == NULL)
    {
        return NULL;
    }

    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_I1);// 设置显示设备颜色格式
    lv_display_set_buffers(s_display, s_draw_buf, NULL, sizeof(s_draw_buf), LV_DISPLAY_RENDER_MODE_FULL);// 设置显示设备绘图缓冲区
    lv_display_set_flush_cb(s_display, DisplayFlush);// 设置显示设备刷新回调函数

    return s_display;
}

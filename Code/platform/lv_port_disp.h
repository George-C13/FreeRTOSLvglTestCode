#ifndef LV_PORT_DISP_H
#define LV_PORT_DISP_H

#include "lvgl.h"

/* 创建显示设备，失败时返回空指针，只能由界面任务调用 */
lv_display_t *LV_PortDisp_Init(void);

#endif

/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "lv_port_disp.h"
#include "b_soft_i2c.h"
#include "b_dht11.h"
#include "s_sensor.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum
{
    UI_HOME_TITLE = 0, // 首页标题
    UI_HOME_TEMP,      // 温度显示
    UI_HOME_HUMI,      // 湿度显示
    UI_HOME_STATUS,    // 传感器状态
    UI_HOME_RUNTIME,   // 运行时间
    UI_HOME_COUNT      // 首页 Label 总数量，不作为有效控件编号
} UiHomeLabelId;// 首页 Label ID

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
/* 任务创建 */
osThreadId_t sensorTaskHandle; // 采集任务句柄
static const osThreadAttr_t sensorTask_attributes = // 采集任务创建属性
{
    .name = "sensorTask", // 调试器中显示的任务名称
    .stack_size = 1024U, // 任务栈大小，CMSIS-RTOS2 此字段单位为字节
    .priority = osPriorityNormal // 高于当前 UI 任务的 BelowNormal 优先级
};

/* 变量定义 */
volatile uint8_t g_ui_state; // 界面任务运行阶段，供调试器观察
static lv_obj_t *s_home_labels[UI_HOME_COUNT]; // 首页控件指针数组，静态初始化为空指针

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for uiTask */
osThreadId_t uiTaskHandle;
const osThreadAttr_t uiTask_attributes = {
  .name = "uiTask",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static uint8_t Ui_HomeCreate(void);// 首页创建
static void Ui_HomeUpdate(void); // 首页更新
static void StartSensorTask(void *argument);// 传感器任务

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void StartUiTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of uiTask */
  uiTaskHandle = osThreadNew(StartUiTask, NULL, &uiTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  sensorTaskHandle = osThreadNew(StartSensorTask, NULL, &sensorTask_attributes); // 创建独立采集任务

  if (sensorTaskHandle == NULL)
  {
      Error_Handler(); // 创建失败，停止运行并便于调试定位
  }

  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  /* Infinite loop */
  for(;;)
  {
    HAL_GPIO_WritePin(GPIOC,GPIO_PIN_2, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOC,GPIO_PIN_3, GPIO_PIN_RESET);
    osDelay(500);
    HAL_GPIO_WritePin(GPIOC,GPIO_PIN_3, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOC,GPIO_PIN_2, GPIO_PIN_RESET);
    osDelay(500);
  }
  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_StartUiTask */
/**
* @brief Function implementing the uiTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartUiTask */
void StartUiTask(void *argument)
{
  /* USER CODE BEGIN StartUiTask */
  uint32_t last_update_ms; // 上一次计时更新的基准时刻，单位：毫秒
  uint32_t now_ms; // 当前时刻，单位：毫秒
  uint32_t elapsed_seconds; // 距离计时基准已经经过的完整秒数
  uint32_t run_seconds = 0U; // 界面初始化完成后的累计运行秒数

  (void)argument;
  g_ui_state = 1U;

  I2C_MasterInit();
  osDelay(100U);

  lv_init();

  /* 使用已有的毫秒时基，不再另外调用滴答递增接口 */
  lv_tick_set_cb(HAL_GetTick);

  g_ui_state = 2U;
  if (LV_PortDisp_Init() == NULL)
  {
      g_ui_state = 0xE1U;

      for (;;)
      {
          osDelay(1000U);
      }
  }

  g_ui_state = 3U; // 开始创建首页

  if (Ui_HomeCreate() == 0U)
  {
      g_ui_state = 0xE2U; // 首页创建失败

      for (;;)
      {
          osDelay(1000U); // 停止后续初始化，并让其他任务运行
      }
  }

  last_update_ms = HAL_GetTick(); // 首页创建完成，从此刻开始计时
  g_ui_state = 4U; // 首页初始化完成

  for (;;)
  {
      now_ms = HAL_GetTick(); // 获取当前毫秒时刻
      elapsed_seconds = (now_ms - last_update_ms) / 1000U; // 计算已经过去的完整秒数

      if (elapsed_seconds != 0U)
      {
          run_seconds += elapsed_seconds; // 累加实际经过的秒数，任务延迟时也能补上
          last_update_ms += elapsed_seconds * 1000U; // 推进计时基准，保留不足一秒的时间余量

          lv_label_set_text_fmt(s_home_labels[UI_HOME_RUNTIME], "RUN : %lu s", (unsigned long)run_seconds); // 更新首页运行时间
      }
      Ui_HomeUpdate(); // 从服务获取快照，有新结果时更新首页
      lv_timer_handler(); // 处理 LVGL 定时器、绘制和刷新
      osDelay(10U); // 当前配置下阻塞约 10 毫秒
  }
  /* USER CODE END StartUiTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
/**
 * @brief  创建温湿度监测首页，并设置初始文字、样式和位置
 * @param  无
 * @return 创建成功返回 1；Label 创建返回空指针时，清理已创建的控件并返回 0
 * @note   由 UI 任务在 LVGL 和显示设备初始化完成后调用一次。
 *         本函数只负责创建界面，不采集传感器数据，也不执行周期刷新。
 */
static uint8_t Ui_HomeCreate(void)
{
    uint8_t i; // 当前创建或清理的 Label 索引

    static const char * const initial_texts[UI_HOME_COUNT] = // 按首页元素编号排列的初始文字
    {
        "DHT11 MONITOR", // UI_HOME_TITLE：首页标题
        "TEMP: -- C",    // UI_HOME_TEMP：尚未取得有效温度
        "HUM : -- %RH",  // UI_HOME_HUMI：尚未取得有效湿度
        "STAT: WAIT",    // UI_HOME_STATUS：等待接入传感器
        "RUN : 0 s"     // UI_HOME_RUNTIME：初始运行时间
    };

    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), LV_PART_MAIN); // 设置屏幕背景为黑色
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, LV_PART_MAIN); // 设置背景完全不透明

    for (i = 0U; i < UI_HOME_COUNT; i++)
    {
        s_home_labels[i] = lv_label_create(lv_screen_active()); // 在当前屏幕上创建 Label

        if (s_home_labels[i] == NULL)
        {
            while (i > 0U)
            {
                i--;
                lv_obj_delete(s_home_labels[i]); // 删除本次已经创建的 Label
                s_home_labels[i] = NULL; // 清空已失效的控件指针
            }

            return 0U;
        }

        lv_obj_set_style_text_font(s_home_labels[i], &lv_font_unscii_8, LV_PART_MAIN); // 使用单色点阵字体
        lv_obj_set_style_text_color(s_home_labels[i], lv_color_white(), LV_PART_MAIN); // 设置文字颜色为白色
        lv_obj_set_style_text_align(s_home_labels[i], LV_TEXT_ALIGN_LEFT, LV_PART_MAIN); // 设置文字左对齐

        lv_obj_set_size(s_home_labels[i], 120, 10); // 设置控件宽度为 120、高度为 10 像素
        lv_label_set_text(s_home_labels[i], initial_texts[i]); // 设置初始文字
        lv_obj_align(s_home_labels[i], LV_ALIGN_TOP_LEFT, 4, (int32_t)(12U + 10U * i)); // 设置左侧偏移为 4 像素、顶部偏移为 12 像素，各行间隔 10 像素
    }

    lv_obj_set_style_text_align(s_home_labels[UI_HOME_TITLE], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN); // 将标题文字居中

    return 1U;
}

/**
 * @brief  获取传感器快照，并按需更新首页
 * @param  无
 * @return 无
 * @note   只由 UI 任务调用，临时快照不作为共享缓存保存。
 */
static void Ui_HomeUpdate(void)
{
    SensorSnapshot snapshot; // 本次显示使用的临时快照
    int32_t temperature; // 温度绝对值乘以 10，使用 32 位类型便于安全取负
    static uint32_t last_sequence; // UI 已处理的发布序号，初始值为 0

    if (Sensor_GetLatest(&snapshot) == 0U)
    {
        return;
    }

    if (snapshot.sequence == last_sequence)
    {
        return; // 没有新结果，保持当前文字
    }

    last_sequence = snapshot.sequence;

    if (snapshot.valid != 0U)
    {
        temperature = snapshot.temperature_x10;

        if (temperature < 0)
        {
            temperature = -temperature;
        }

        lv_label_set_text_fmt(s_home_labels[UI_HOME_TEMP], "TEMP: %s%lu.%lu C", (snapshot.temperature_x10 < 0) ? "-" : "", (unsigned long)(temperature / 10), (unsigned long)(temperature % 10)); // 显示带符号的一位小数温度
        lv_label_set_text_fmt(s_home_labels[UI_HOME_HUMI], "HUM : %u.%u %%RH", (unsigned int)(snapshot.humidity_x10 / 10U), (unsigned int)(snapshot.humidity_x10 % 10U)); // 使用 %% 输出一个百分号
    }
    else
    {
        lv_label_set_text(s_home_labels[UI_HOME_TEMP], "TEMP: -- C"); // 尚无有效温度
        lv_label_set_text(s_home_labels[UI_HOME_HUMI], "HUM : -- %RH"); // 尚无有效湿度
    }

    if ((snapshot.last_status == DHT11_STATUS_OK) && (snapshot.valid != 0U))
    {
        lv_label_set_text(s_home_labels[UI_HOME_STATUS], "STAT: OK"); // 本次采集成功
    }
    else if (snapshot.valid != 0U)
    {
        lv_label_set_text(s_home_labels[UI_HOME_STATUS], "STAT: OLD"); // 本次失败，显示的是上次成功值
    }
    else
    {
        lv_label_set_text(s_home_labels[UI_HOME_STATUS], "STAT: ERR"); // 尚无有效数据且采集失败
    }
}

/**
 * @brief  初始化传感器服务，并周期触发采集
 * @param  argument  任务参数，当前未使用
 * @return 无，任务持续循环运行
 * @note   温湿度采集及数据缓存均由服务处理，任务只负责调度。
 */
static void StartSensorTask(void *argument)
{
    (void)argument;

    if (Sensor_Init() == 0U)
    {
        for (;;)
        {
            osDelay(1000U); // 初始化失败后阻塞，UI 仍可获取错误状态
        }
    }

    osDelay(1100U); // 等待传感器上电稳定

    for (;;)
    {
        Sensor_CollectOnce(); // 完成一次采集并发布结果
        osDelay(3000U); // 当前配置下阻塞约 3 秒
    }
}

/* USER CODE END Application */


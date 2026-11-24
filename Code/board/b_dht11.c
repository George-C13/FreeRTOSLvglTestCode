/**
 * @file    b_dht11.c
 * @brief   DHT11 轮询驱动：起始、响应、数据接收及校验
 * @author  George Cai
 * @date    2026-10-29
 * @version MAJOR_SOFT_VER.MINOR_SOFT_VER
 * @details 使用 PB0 开漏输出，通过 DWT 计数器实现有超时的微秒级电平等待。
 *          本版本使用同步轮询方式接收 40 位数据，供单次读取联调使用。
 * @note    DATA 需要外部上拉，所有接口由单个任务调用。
 */

#include "b_dht11.h"
#include "main.h"
#include <stddef.h>

/*===========================================================================
 * 宏定义
 *===========================================================================*/
#define DHT11_START_DELAY_MS       20U    // 主机起始低电平等待时间，单位：毫秒
#define DHT11_START_LIMIT_US       30000U // 起始低电平长度检查上限，单位：微秒
#define DHT11_RELEASE_TIMEOUT_US   20U    // 释放 DATA 后等待上升的超时时间，单位：微秒
#define DHT11_RESPONSE_TIMEOUT_US  120U   // 各响应阶段等待超时时间，单位：微秒

#define DHT11_DATA_TIMEOUT_US      120U  // 单个数据阶段等待超时时间，单位：微秒
#define DHT11_BIT_THRESHOLD_US     50U   // 区分数据位 0 和 1 的高电平时长阈值，单位：微秒

/*===========================================================================
 * 类型定义
 *===========================================================================*/

/*===========================================================================
 * 私有变量
 *===========================================================================*/
static uint32_t s_dht11_cycles_per_us; // 每微秒对应的内核时钟周期数
static uint8_t s_dht11_ready; // 驱动初始化完成标志，1 表示 DWT 计数器可用

/*===========================================================================
 * 私有函数声明
 *===========================================================================*/
static uint8_t DHT11_WaitLevel(GPIO_PinState level, uint32_t timeout_us);
static DHT11_Status DHT11_Transfer(uint8_t *raw);
static DHT11_Status DHT11_ReadByte(uint8_t *value);

/*===========================================================================
 * 公共函数实现
 *===========================================================================*/
/**
 * @brief  初始化 DHT11 驱动使用的 DWT 计数器，并释放 DATA 引脚
 * @param  无
 * @return 成功返回 DHT11_STATUS_OK；计数器不可用时返回 DHT11_STATUS_NOT_READY
 * @note   不清零共享的 DWT 周期计数器，初始化后不要改变内核时钟频率。
 */
DHT11_Status DHT11_Init(void)
{
    uint32_t start; // 检查 DWT 是否运行时使用的起始计数值

    s_dht11_ready = 0U;
    SystemCoreClockUpdate();

    if ((SystemCoreClock < 1000000U) || ((SystemCoreClock % 1000000U) != 0U))
    {
        return DHT11_STATUS_NOT_READY;
    }

    s_dht11_cycles_per_us = SystemCoreClock / 1000000U;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB();
    __ISB();

    start = DWT->CYCCNT;
    __NOP();
    __NOP();
    __NOP();

    if (DWT->CYCCNT == start)
    {
        return DHT11_STATUS_NOT_READY;
    }

    HAL_GPIO_WritePin(DHT11_DATA_GPIO_Port, DHT11_DATA_Pin, GPIO_PIN_SET); // 开漏输出写 1，释放 DATA
    s_dht11_ready = 1U;

    return DHT11_STATUS_OK;
}

/**
 * @brief  发送起始信号并检测响应，不接收温湿度数据
 * @param  无
 * @return 响应正常返回 DHT11_STATUS_OK；失败返回对应状态码
 * @note   仅用于独立检测响应；DHT11_Read() 内部已经包含同样的响应过程。
 */
DHT11_Status DHT11_Probe(void)
{
    return DHT11_Transfer(NULL);
}

/**
 * @brief  读取 5 字节数据并完成校验，成功后更新温湿度结果
 * @param  data  用于保存温湿度及原始数据的结构体指针
 * @return 成功返回 DHT11_STATUS_OK；失败返回对应状态码，不修改输出结构体
 * @note   温湿度以实际值乘以 10 的整数保存，避免引入浮点格式化。
 *         温度小数字节的最高位按奥松 V1.3 手册解析为负温度标志。
 */
DHT11_Status DHT11_Read(DHT11_Data *data)
{
    uint8_t raw[5] = {0U}; // 本次接收到的 5 字节原始数据
    uint8_t checksum; // 前四字节相加后保留低 8 位的校验值
    uint8_t i; // 复制校验成功数据时使用的数组索引
    DHT11_Status status; // 本次通信结果

    if (data == NULL)
    {
        return DHT11_STATUS_PARAM;
    }

    status = DHT11_Transfer(raw); // 完成起始信号、响应检测和 40 位数据接收
    if (status != DHT11_STATUS_OK)
    {
        return status;
    }

    checksum = (uint8_t)(raw[0] + raw[1] + raw[2] + raw[3]);
    if (checksum != raw[4])
    {
        return DHT11_STATUS_CHECKSUM;
    }

    data->humidity_x10 = (uint16_t)((uint16_t)raw[0] * 10U + raw[1]);
    data->temperature_x10 = (int16_t)((uint16_t)raw[2] * 10U + (raw[3] & 0x7FU));

    if ((raw[3] & 0x80U) != 0U)
    {
        data->temperature_x10 = (int16_t)(-data->temperature_x10); // 最高位为 1，表示负温度
    }

    for (i = 0U; i < 5U; i++)
    {
        data->raw[i] = raw[i]; // 只保留完整接收且校验成功的原始数据
    }

    return DHT11_STATUS_OK;
}

/*===========================================================================
 * 私有函数实现
 *===========================================================================*/
/**
 * @brief  执行一次完整通信，按需接收数据
 * @param  raw  指向 5 字节接收数组；传入 NULL 时仅检测响应
 * @return 成功返回 DHT11_STATUS_OK；失败返回对应状态码
 * @note   起始信号期间允许中断；仅响应与数据接收阶段屏蔽中断。
 *         所有等待都有超时，返回前恢复 PRIMASK 并保持 DATA 释放。
 */
static DHT11_Status DHT11_Transfer(uint8_t *raw)
{
    uint8_t i; // 当前接收的字节索引，范围为 0～4
    uint32_t start_cycles; // 起始低电平开始时的 DWT 计数值
    uint32_t primask; // 响应检测前保存的中断屏蔽状态
    DHT11_Status status = DHT11_STATUS_OK; // 本次响应检测结果

    if (s_dht11_ready == 0U)
    {
        return DHT11_STATUS_NOT_READY;
    }

    if ((__get_IPSR() != 0U) || (__get_PRIMASK() != 0U) || (__get_BASEPRI() != 0U))
    {
        return DHT11_STATUS_CONTEXT;
    }

    if (HAL_GPIO_ReadPin(DHT11_DATA_GPIO_Port, DHT11_DATA_Pin) != GPIO_PIN_SET)
    {
        return DHT11_STATUS_BUS_BUSY;
    }

    HAL_GPIO_WritePin(DHT11_DATA_GPIO_Port, DHT11_DATA_Pin, GPIO_PIN_RESET); // 主机拉低 DATA，产生起始信号
    start_cycles = DWT->CYCCNT;
    HAL_Delay(DHT11_START_DELAY_MS); // 保持起始低电平，中断和任务调度仍然允许

    primask = __get_PRIMASK();
    __disable_irq(); // 保护响应与数据接收过程，避免任务切换或普通中断打断采样

    if ((uint32_t)(DWT->CYCCNT - start_cycles) >= (DHT11_START_LIMIT_US * s_dht11_cycles_per_us))
    {
        status = DHT11_STATUS_START_TIMEOUT;
    }
    else
    {
        HAL_GPIO_WritePin(DHT11_DATA_GPIO_Port, DHT11_DATA_Pin, GPIO_PIN_SET); // 释放 DATA，让上拉电阻和传感器控制线路

        if (DHT11_WaitLevel(GPIO_PIN_SET, DHT11_RELEASE_TIMEOUT_US) == 0U)
        {
            status = DHT11_STATUS_RELEASE_TIMEOUT;
        }
        else if (DHT11_WaitLevel(GPIO_PIN_RESET, DHT11_RESPONSE_TIMEOUT_US) == 0U)
        {
            status = DHT11_STATUS_RESPONSE_LOW_TIMEOUT;
        }
        else if (DHT11_WaitLevel(GPIO_PIN_SET, DHT11_RESPONSE_TIMEOUT_US) == 0U)
        {
            status = DHT11_STATUS_RESPONSE_HIGH_TIMEOUT;
        }
        else if (DHT11_WaitLevel(GPIO_PIN_RESET, DHT11_RESPONSE_TIMEOUT_US) == 0U)
        {
            status = DHT11_STATUS_RESPONSE_END_TIMEOUT;
        }
    }

    if ((status == DHT11_STATUS_OK) && (raw != NULL))
    {
        for (i = 0U; i < 5U; i++)
        {
            status = DHT11_ReadByte(&raw[i]); // 高位先接收，连续读取 5 字节

            if (status != DHT11_STATUS_OK)
            {
                break;
            }
        }

        if ((status == DHT11_STATUS_OK) && (DHT11_WaitLevel(GPIO_PIN_SET, DHT11_DATA_TIMEOUT_US) == 0U))
        {
            status = DHT11_STATUS_DATA_TIMEOUT; // 等待传感器结束最后一段低电平并释放 DATA
        }
    }

    HAL_GPIO_WritePin(DHT11_DATA_GPIO_Port, DHT11_DATA_Pin, GPIO_PIN_SET); // 无论成功或失败，主机都保持释放 DATA
    __set_PRIMASK(primask); // 恢复进入响应检测前的中断状态

    return status;
}

/**
 * @brief  在指定超时时间内等待 DATA 达到目标电平
 * @param  level       要等待的目标电平
 * @param  timeout_us  超时时间，单位：微秒，当前只传入 20 或 120
 * @return 检测到目标电平返回 1；等待超时返回 0
 * @note   超时使用 DWT 周期差计算，在屏蔽中断时仍能计时；不依赖 HAL 毫秒中断。
 */
static uint8_t DHT11_WaitLevel(GPIO_PinState level, uint32_t timeout_us)
{
    uint32_t start = DWT->CYCCNT; // 本次等待开始时的内核周期计数值
    uint32_t timeout_cycles = timeout_us * s_dht11_cycles_per_us; // 超时时间对应的内核周期数

    while (HAL_GPIO_ReadPin(DHT11_DATA_GPIO_Port, DHT11_DATA_Pin) != level)
    {
        if ((uint32_t)(DWT->CYCCNT - start) >= timeout_cycles)
        {
            return 0U;
        }
    }

    return 1U;
}

/**
 * @brief  测量每位高电平持续时间，高位先行接收一个字节
 * @param  value  接收字节的保存地址，由内部调用保证非空
 * @return 成功返回 DHT11_STATUS_OK；电平等待超时返回 DHT11_STATUS_DATA_TIMEOUT
 * @note   调用时应处于当前数据位的低电平阶段，期间不允许任务切换。
 */
static DHT11_Status DHT11_ReadByte(uint8_t *value)
{
    uint8_t bit; // 当前接收的数据位索引，范围为 0～7
    uint8_t result = 0U; // 当前正在组装的字节
    uint32_t high_start; // 检测到高电平时的 DWT 周期计数值
    uint32_t high_cycles; // 本位高电平持续的内核周期数

    for (bit = 0U; bit < 8U; bit++)
    {
        if (DHT11_WaitLevel(GPIO_PIN_SET, DHT11_DATA_TIMEOUT_US) == 0U)
        {
            return DHT11_STATUS_DATA_TIMEOUT;
        }

        high_start = DWT->CYCCNT;
        if (DHT11_WaitLevel(GPIO_PIN_RESET, DHT11_DATA_TIMEOUT_US) == 0U)
        {
            return DHT11_STATUS_DATA_TIMEOUT;
        }

        high_cycles = (uint32_t)(DWT->CYCCNT - high_start);
        result = (uint8_t)(result << 1U); // 之前接收的位左移，为新的一位腾出最低位

        if (high_cycles >= (DHT11_BIT_THRESHOLD_US * s_dht11_cycles_per_us))
        {
            result |= 1U; // 高电平较长，判断为数据位 1；否则最低位保持为 0
        }
    }

    *value = result;
    return DHT11_STATUS_OK;
}

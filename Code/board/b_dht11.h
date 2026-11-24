#ifndef B_DHT11_H
#define B_DHT11_H

#include <stdint.h>

typedef enum
{
    DHT11_STATUS_OK = 0,            // 操作成功
    DHT11_STATUS_NOT_READY,         // 驱动尚未初始化，或 DWT 计数器不可用
    DHT11_STATUS_CONTEXT,           // 调用环境不正确，例如中断内或屏蔽中断期间
    DHT11_STATUS_BUS_BUSY,          // 起始前 DATA 未处于高电平
    DHT11_STATUS_START_TIMEOUT,     // 起始低电平被任务抢占等因素延长到 30 毫秒及以上
    DHT11_STATUS_RELEASE_TIMEOUT,   // MCU 释放 DATA 后，未检测到线路升高
    DHT11_STATUS_RESPONSE_LOW_TIMEOUT,  // 未检测到传感器响应低电平
    DHT11_STATUS_RESPONSE_HIGH_TIMEOUT, // 响应低电平结束超时
    DHT11_STATUS_RESPONSE_END_TIMEOUT,  // 响应高电平结束超时
    DHT11_STATUS_PARAM,                 // 输出指针为空
    DHT11_STATUS_DATA_TIMEOUT,          // 数据位接收或帧结束等待超时
    DHT11_STATUS_CHECKSUM               // 数据校验失败
} DHT11_Status;

typedef struct
{
    int16_t temperature_x10; // 温度乘以 10，例如 254 表示 25.4 摄氏度
    uint16_t humidity_x10; // 相对湿度乘以 10，例如 530 表示 53.0 %RH
    uint8_t raw[5]; // 最近一次校验成功的原始数据，依次为湿度、温度和校验字节
} DHT11_Data;

/**
 * @brief  初始化 DHT11 驱动使用的 DWT 计数器，并释放 DATA 引脚
 * @param  无
 * @return 成功返回 DHT11_STATUS_OK；计数器不可用时返回 DHT11_STATUS_NOT_READY
 * @note   调用前需完成系统时钟和 GPIO 初始化，PB0 应配置为带外部上拉的开漏输出。
 */
DHT11_Status DHT11_Init(void);

/**
 * @brief  发送起始信号并检测 DHT11 响应，不接收温湿度数据
 * @param  无
 * @return 响应正常返回 DHT11_STATUS_OK；失败返回对应的状态码
 * @note   只能在中断允许、HAL 时基运行的任务上下文中调用，且仅由一个任务使用。
 *         传感器上电后先等待至少 1 秒，相邻请求间隔建议不少于 3 秒。
 *         本函数返回成功后，传感器仍会继续发送数据，本步骤暂不解析。
 */
DHT11_Status DHT11_Probe(void);

/**
 * @brief  读取一帧 DHT11 数据，校验后输出温湿度及原始字节
 * @param  data  接收结果的结构体指针，不能为 NULL
 * @return 成功返回 DHT11_STATUS_OK；失败返回对应状态码，保留原输出数据
 * @note   在 DHT11_Init() 成功、上电稳定至少 1 秒后，由单个任务调用。
 *         本函数包含完整起始和响应过程，调用前不要再紧接着调用 DHT11_Probe()。
 *         相邻请求建议间隔至少 3 秒；当前轮询版在响应和接收期间屏蔽中断。
 */
DHT11_Status DHT11_Read(DHT11_Data *data);

#endif

#include "s_sensor.h"
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include <stddef.h>

/*===========================================================================
 * 宏定义
 *===========================================================================*/


/*===========================================================================
 * 类型定义
 *===========================================================================*/

/*===========================================================================
 * 私有变量
 *===========================================================================*/
static SensorSnapshot s_latest = // 服务内部唯一缓存，其余成员自动初始化为 0
{
    .last_status = DHT11_STATUS_NOT_READY // 尚未发布采集结果
};

/*===========================================================================
 * 私有函数声明
 *===========================================================================*/
static void Sensor_Publish(const DHT11_Data *data, DHT11_Status status);

/*===========================================================================
 * 公共函数实现
 *===========================================================================*/
/**
 * @brief  初始化传感器驱动，失败时保存错误状态
 * @param  无
 * @return 初始化成功返回 1；失败返回 0
 * @note   由采集任务调用一次，成功后仍需等待传感器上电稳定。
 */
uint8_t Sensor_Init(void)
{
    DHT11_Status status = DHT11_Init(); // 底层驱动初始化结果

    if (status != DHT11_STATUS_OK)
    {
        Sensor_Publish(NULL, status); // 保存初始化错误，供 UI 获取
        return 0U;
    }
    return 1U;
}

/**
 * @brief  完成一次温湿度采集，并更新服务缓存
 * @param  无
 * @return 无
 * @note   由单个采集任务调用，采集间隔由任务保证。
 */
void Sensor_CollectOnce(void)
{
    DHT11_Data sample; // 本次采集的临时数据，只有成功时才读取其内容
    DHT11_Status status; // 本次驱动读取结果

    status = DHT11_Read(&sample); // 在缓存临界区外完成硬件通信
    Sensor_Publish(&sample, status); // 失败时不会读取 sample 中的温湿度
}

/**
 * @brief  复制传感器最新快照，不发起硬件采集
 * @param  out  保存快照的结构体指针，不能为 NULL
 * @return 成功复制返回 1；参数错误返回 0
 * @note   仅供任务调用，返回后仍需检查 valid 和 last_status。
 */
uint8_t Sensor_GetLatest(SensorSnapshot *out)
{
    if (out == NULL)
    {
        return 0U;
    }

    taskENTER_CRITICAL(); // 保护整份快照复制，避免读到部分新、部分旧的数据
    *out = s_latest;
    taskEXIT_CRITICAL(); // 复制完成，后续显示处理在临界区外执行

    return 1U;
}

 /*===========================================================================
 * 私有函数实现
 *===========================================================================*/
/**
 * @brief  发布采集结果，失败时保留历史有效温湿度
 * @param  data    成功时指向有效采样；失败时允许为 NULL，且不读取其内容
 * @param  status  本次采集结果
 * @return 无
 * @note   由单个采集任务调用，临界区内只更新缓存。
 */
static void Sensor_Publish(const DHT11_Data *data, DHT11_Status status)
{
    uint32_t now_ms = HAL_GetTick(); // 本次发布前取得的 HAL 毫秒时刻

    if ((status == DHT11_STATUS_OK) && (data == NULL))
    {
        status = DHT11_STATUS_PARAM;
    }

    taskENTER_CRITICAL(); // 保护整份缓存更新

    if (status == DHT11_STATUS_OK)
    {
        s_latest.temperature_x10 = data->temperature_x10;
        s_latest.humidity_x10 = data->humidity_x10;
        s_latest.last_success_ms = now_ms;
        s_latest.valid = 1U;
    }

    s_latest.last_status = status;
    s_latest.sequence++;

    taskEXIT_CRITICAL(); // 更新完成，恢复正常任务调度
}

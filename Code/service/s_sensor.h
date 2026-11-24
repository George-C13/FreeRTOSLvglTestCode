#ifndef S_SENSOR_H
#define S_SENSOR_H

#include <stdint.h>
#include "b_dht11.h"

typedef struct
{
    int16_t temperature_x10; // 最近一次成功采样的温度乘以 10
    uint16_t humidity_x10; // 最近一次成功采样的相对湿度乘以 10
    uint32_t last_success_ms; // 最近一次成功采样的 HAL 时刻，单位：毫秒
    uint32_t sequence; // 发布序号，每次发布结果后增加 1，失败也增加
    DHT11_Status last_status; // 最近一次采集操作的结果
    uint8_t valid; // 是否至少取得过一次有效数据，1 表示有历史有效样本
} SensorSnapshot;

uint8_t Sensor_Init(void);
void Sensor_CollectOnce(void);
uint8_t Sensor_GetLatest(SensorSnapshot *out);

#endif
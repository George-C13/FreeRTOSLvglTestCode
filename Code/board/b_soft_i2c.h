
#ifndef _B_SOFT_I2C_H_
#define _B_SOFT_I2C_H_

#include <stdint.h>

typedef enum
{
    I2C_STATUS_OK = 0,       /* 操作成功 */
    I2C_STATUS_PARAM,        /* 参数错误 */
    I2C_STATUS_NOT_READY,    /* 尚未初始化 */
    I2C_STATUS_BUS_BUSY,     /* 总线没有处于空闲状态 */
    I2C_STATUS_ADDR_NACK,    /* 器件地址没有得到应答 */
    I2C_STATUS_DATA_NACK     /* 地址后的字节没有得到应答 */
} I2C_Status;


void I2C_MasterInit(void);
I2C_Status I2C_MasterTransmit(uint8_t addr7, const uint8_t *buf, uint16_t len);
I2C_Status I2C_MasterReceive(uint8_t addr7, uint8_t *buf, uint16_t len);

/* 检测指定的七位器件地址是否应答 */
I2C_Status I2C_Probe(uint8_t addr7);

#endif

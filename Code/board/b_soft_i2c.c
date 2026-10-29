/**
 * @file    b_soft_i2c_.c
 * @brief   
 * @author  George Cai
 * @date    2026-10-27
 * @version MAJOR_SOFT_VER.MINOR_SOFT_VER
 * @details
 * 
 * @note
 */

#include "b_soft_i2c.h"
#include "main.h"
#include <stddef.h> // for NULL

/*===========================================================================
 * 宏定义
 *===========================================================================*/
#define I2C_SCL_Set()           HAL_GPIO_WritePin(OLED_SCL_GPIO_Port, OLED_SCL_Pin, GPIO_PIN_SET)
#define I2C_SCL_Reset()         HAL_GPIO_WritePin(OLED_SCL_GPIO_Port, OLED_SCL_Pin, GPIO_PIN_RESET)
#define I2C_SDA_Set()           HAL_GPIO_WritePin(OLED_SDA_GPIO_Port, OLED_SDA_Pin, GPIO_PIN_SET)
#define I2C_SDA_Reset()         HAL_GPIO_WritePin(OLED_SDA_GPIO_Port, OLED_SDA_Pin, GPIO_PIN_RESET)

#define I2C_Read_SCL()          HAL_GPIO_ReadPin(OLED_SCL_GPIO_Port, OLED_SCL_Pin)
#define I2C_Read_SDA()          HAL_GPIO_ReadPin(OLED_SDA_GPIO_Port, OLED_SDA_Pin)

/*===========================================================================
 * 类型定义
 *===========================================================================*/

/*===========================================================================
 * 私有变量
 *===========================================================================*/
static uint32_t s_i2c_cycles_per_us;// 每微秒对应的内核时钟周期数
static uint8_t s_i2c_ready;// I2C是否初始化成功

/*===========================================================================
 * 私有函数声明
 *===========================================================================*/
static uint8_t I2C_DelayInit(void);
static void delay_us(uint32_t us);
static void I2C_Start(void);
static void I2C_Stop(void);
static uint8_t I2C_Wait_ACK(void);
static void I2C_ACK(void);
static void I2C_NACK(void);
static void I2C_Send_Byte(uint8_t Data);
static uint8_t I2C_Read_Byte(void);
static I2C_Status I2C_CheckReady(uint8_t addr7);

/*===========================================================================
 * 公共函数实现
 *===========================================================================*/
 
 void I2C_MasterInit(void)
 {
     s_i2c_ready = 0U;
 
     if (I2C_DelayInit() == 0U)
     {
         Error_Handler();
         return;
     }
 
     I2C_SCL_Set();
     I2C_SDA_Set();
     delay_us(10U);
 
     s_i2c_ready = 1U;
 }

I2C_Status I2C_MasterTransmit(uint8_t addr7, const uint8_t *buf, uint16_t len)
{
    uint16_t i;
    I2C_Status status;

    if ((buf == NULL) || (len == 0U))
    {
        return I2C_STATUS_PARAM;
    }

    status = I2C_CheckReady(addr7);
    if (status != I2C_STATUS_OK)
    {
        return status;
    }

    I2C_Start();

    I2C_Send_Byte((uint8_t)(addr7 << 1));

    if (I2C_Wait_ACK() != 0U)
    {
        I2C_Stop();
        return I2C_STATUS_ADDR_NACK;
    }

    for (i = 0U; i < len; i++)
    {
        I2C_Send_Byte(buf[i]);

        if (I2C_Wait_ACK() != 0U)
        {
            /* 本次传输失败，结束事务，不再发送后续字节 */
            I2C_Stop();
            return I2C_STATUS_DATA_NACK;
        }
    }

    I2C_Stop();

    return I2C_STATUS_OK;
}

I2C_Status I2C_MasterReceive(uint8_t addr7, uint8_t *buf, uint16_t len)
{
    uint16_t i;
    I2C_Status status;

    /* 检查接收缓冲区和数据长度 */
    if ((buf == NULL) || (len == 0U))
    {
        return I2C_STATUS_PARAM;
    }

    /* 检查地址、初始化状态和总线空闲状态 */
    status = I2C_CheckReady(addr7);
    if (status != I2C_STATUS_OK)
    {
        return status;
    }

    I2C_Start();

    /* 发送器件地址，最低位置 1，表示读方向 */
    I2C_Send_Byte((uint8_t)((addr7 << 1) | 0x01U));

    /* 检查器件是否应答读地址 */
    if (I2C_Wait_ACK() != 0U)
    {
        I2C_Stop();
        return I2C_STATUS_ADDR_NACK;
    }

    for (i = 0U; i < len; i++)
    {
        /* 接收一个字节，随后由主机产生第九个时钟 */
        buf[i] = I2C_Read_Byte();

        if (i == (len - 1U))
        {
            /* 最后一个字节发送 NACK，表示结束接收 */
            I2C_NACK();
        }
        else
        {
            /* 发送 ACK，请求器件继续发送下一个字节 */
            I2C_ACK();
        }
    }

    I2C_Stop();

    return I2C_STATUS_OK;
}

I2C_Status I2C_Probe(uint8_t addr7)
{
    I2C_Status status;

    /* 检查地址、初始化状态和总线空闲状态 */
    status = I2C_CheckReady(addr7);
    if (status != I2C_STATUS_OK)
    {
        return status;
    }

    I2C_Start();

    /* 七位地址左移一位，最低位为零，表示写方向 */
    I2C_Send_Byte((uint8_t)(addr7 << 1));

    if (I2C_Wait_ACK() == 0U)
    {
        status = I2C_STATUS_OK;
    }
    else
    {
        status = I2C_STATUS_ADDR_NACK;
    }

    /* 无论是否应答，都结束本次探测 */
    I2C_Stop();

    return status;
}

/*===========================================================================
 * 静态私有函数实现
 *===========================================================================*/
static uint8_t I2C_DelayInit(void)
{
    uint32_t start;

    SystemCoreClockUpdate();

    /* 当前实现要求内核频率为整数 MHz */
    if ((SystemCoreClock < 1000000U) ||
        ((SystemCoreClock % 1000000U) != 0U))
    {
        return 0U;
    }

    s_i2c_cycles_per_us = SystemCoreClock / 1000000U;

    /* 开启跟踪模块及周期计数器，不清零共享计数值 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB();
    __ISB();

    /* 检查计数器是否确实在运行 */
    start = DWT->CYCCNT;
    __NOP();
    __NOP();
    __NOP();

    return (DWT->CYCCNT != start) ? 1U : 0U;
}

static void delay_us(uint32_t us)
{
    uint32_t part;
    uint32_t ticks;
    uint32_t start;

    while (us != 0U)
    {
        /* 长延时分段处理，避免乘法溢出 */
        part = (us > 1000U) ? 1000U : us;
        ticks = part * s_i2c_cycles_per_us;

        start = DWT->CYCCNT;

        /* 无符号差值允许计数器跨过一次回绕 */
        while ((uint32_t)(DWT->CYCCNT - start) < ticks)
        {
            __NOP();
        }

        us -= part;
    }
}

static void I2C_Start(void)
{
    I2C_SDA_Set();
    delay_us(10);
    
    I2C_SCL_Set();
    delay_us(10);
    
    I2C_SDA_Reset();
    delay_us(10);
    
    I2C_SCL_Reset();
    delay_us(10);
}

static void I2C_Stop(void)
{
    I2C_SDA_Reset();
    delay_us(10);
    
    I2C_SCL_Set(); 
    delay_us(10);
    
    I2C_SDA_Set();
    delay_us(10);
}

static uint8_t I2C_Wait_ACK(void)
{
    uint8_t AckBit;
    
    I2C_SDA_Set();
    delay_us(10);
    
    I2C_SCL_Set();
    delay_us(10);
    
    AckBit = I2C_Read_SDA();
    delay_us(10);
    
    I2C_SCL_Reset();
    delay_us(1);
    
    return AckBit;
}

static void I2C_ACK(void)
{
    I2C_SDA_Reset();
    delay_us(10);
    
    I2C_SCL_Set();
    delay_us(10);
    
    I2C_SCL_Reset();
    delay_us(10);
}

static void I2C_NACK(void)
{
    I2C_SDA_Set();
    delay_us(10);
    
    I2C_SCL_Set();
    delay_us(10);
    
    I2C_SCL_Reset();
    delay_us(10);
}

static void I2C_Send_Byte(uint8_t Data)
{
    uint8_t i;
    
    for (i = 0;i < 8;i++)
    {
        if (Data & 0x80)
        {
            I2C_SDA_Set();
            delay_us(10);
        }
        else
        {
            I2C_SDA_Reset();
            delay_us(10);
        }
        Data <<= 1;
        
        I2C_SCL_Set();
        delay_us(10);
        
        I2C_SCL_Reset();
        delay_us(10);
    }
}

static uint8_t I2C_Read_Byte(void)
{
    uint8_t i,receive = 0x00;
    
    I2C_SDA_Set();
    delay_us(10);
    
    for (i = 0;i < 8;i++)
    {
        receive <<= 1;
        
        I2C_SCL_Set();
        delay_us(10);
        
        if (I2C_Read_SDA())
        {
            receive |= 0x01;
        }
        
        I2C_SCL_Reset();
        delay_us(10);
    }
    return receive;
}

static I2C_Status I2C_CheckReady(uint8_t addr7)
{
    if ((addr7 < 0x08U) || (addr7 > 0x77U))
    {
        return I2C_STATUS_PARAM;
    }

    if (s_i2c_ready == 0U)
    {
        return I2C_STATUS_NOT_READY;
    }

    /* 正常空闲状态下，SCL 和 SDA 都应为高电平 */
    if ((I2C_Read_SCL() != GPIO_PIN_SET) ||
        (I2C_Read_SDA() != GPIO_PIN_SET))
    {
        return I2C_STATUS_BUS_BUSY;
    }

    return I2C_STATUS_OK;
}

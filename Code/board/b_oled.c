/**
 * @file    b_oled.c
 * @brief   
 * @author  George Cai
 * @date    2026-10-27
 * @version MAJOR_SOFT_VER.MINOR_SOFT_VER
 * @details
 * 
 * @note
 */

#include "b_oled.h"
#include <stddef.h>

/*===========================================================================
 * 宏定义
 *===========================================================================*/
#define OLED_ADDR7       0x3CU
#define OLED_WIDTH       128U
#define OLED_PAGE_COUNT  8U

/*===========================================================================
 * 类型定义
 *===========================================================================*/

/*===========================================================================
 * 私有变量
 *===========================================================================*/

/*===========================================================================
 * 私有函数声明
 *===========================================================================*/

/*===========================================================================
 * 公共函数实现
 *===========================================================================*/
/**
 * @brief  配置 OLED 控制器，清空显存并开启显示
 * @param  无
 * @return 全部操作成功时返回 I2C_STATUS_OK；失败时返回首个失败操作的状态
 * @note   调用前需完成软件 I2C 初始化，并确保 OLED 供电及硬件复位已稳定。
 *         当前配置适用于 128×64 OLED，采用页寻址模式，清屏成功后才开启显示。
 */
I2C_Status OLED_Init(void)
{
    I2C_Status status;

    /* 首字节为控制字节，其余内容按命令及命令参数解析 */
    static const uint8_t init_cmd[] =
    {
        0x00U,
        0xAEU,         /* 关闭显示，配置期间不显示内容 */
        0xD5U, 0x80U,  /* 设置显示时钟分频和振荡频率 */
        0xA8U, 0x3FU,  /* 设置为六十四行扫描 */
        0xD3U, 0x00U,  /* 设置显示偏移为零 */
        0x40U,         /* 设置显示起始行为零 */
        0x8DU, 0x14U,  /* 开启内部电荷泵 */
        0x20U, 0x02U,  /* 选择页寻址模式 */
        0xA1U,         /* 设置列映射方向 */
        0xC8U,         /* 设置行扫描方向 */
        0xDAU, 0x12U,  /* 设置适用于本屏的行引脚配置 */
        0x81U, 0x20U,  /* 设置较低对比度 */
        0xD9U, 0xF1U,  /* 设置预充电周期 */
        0xDBU, 0x40U,  /* 设置公共电极取消选择电压 */
        0x2EU,         /* 关闭滚动 */
        0xA4U,         /* 恢复按显存内容显示 */
        0xA6U          /* 正常显示，位为一时点亮 */
    };

    static const uint8_t display_on[] =
    {
        0x00U,
        0xAFU          /* 开启显示 */
    };

    status = I2C_MasterTransmit(OLED_ADDR7, init_cmd, (uint16_t)sizeof(init_cmd));

    if (status != I2C_STATUS_OK)
    {
        return status;
    }

    /* 先清空显存，避免开屏时显示未知内容 */
    status = OLED_Fill(0x00U);
    if (status != I2C_STATUS_OK)
    {
        return status;
    }

    return I2C_MasterTransmit(OLED_ADDR7, display_on, (uint16_t)sizeof(display_on));
}

/**
 * @brief  将一帧页格式像素数据写入 OLED 显存
 * @param  page_buf  像素数据指针，按页优先、页内按列递增的顺序排列；
 *                   每字节表示同一列的 8 个垂直像素，最低位对应页顶像素
 * @param  len       像素数据长度，单位：字节，必须等于 OLED_FRAME_BYTES（1024）
 * @return 成功时返回 I2C_STATUS_OK；指针为空或长度不符时返回 I2C_STATUS_PARAM；
 *         通信失败时返回对应的 I2C 错误状态
 * @note   调用前需完成 OLED 初始化，输入数据不包含 I2C 控制字节。
 *         函数同步发送全部 8 页，调用期间 page_buf 指向的数据必须保持有效且不被修改。
 *         通信失败时立即返回，已写入的部分显存不会自动恢复。
 */
I2C_Status OLED_WriteFrame(const uint8_t *page_buf, uint16_t len)
{
    uint8_t page;// 页地址，从 0 到 7
    uint16_t x;// 列地址，从 0 到 127
    uint16_t offset;// 偏移量，从 0 到 1023
    I2C_Status status;// I2C 通信状态

    uint8_t packet[OLED_WIDTH + 1U];// 一字节控制信息，加一页的一百二十八字节像素数据

    uint8_t position_cmd[] =
    {
        0x00U,    /* 后续内容按命令解析 */
        0xB0U,    /* 页地址，在循环中更新 */
        0x00U,    /* 列地址低四位 */
        0x10U     /* 列地址高四位 */
    };

    if ((page_buf == NULL) || (len != OLED_FRAME_BYTES))
    {
        return I2C_STATUS_PARAM;
    }

    /* 后续字节按显存数据解析 */
    packet[0] = 0x40U;

    for (page = 0U; page < OLED_PAGE_COUNT; page++)
    {
        /* 每一页都从第零列开始写入 */
        position_cmd[1] = (uint8_t)(0xB0U | page);

        status = I2C_MasterTransmit(OLED_ADDR7, position_cmd, (uint16_t)sizeof(position_cmd));

        if (status != I2C_STATUS_OK)
        {
            return status;
        }

        /* 取出本页的一百二十八字节 */
        offset = (uint16_t)page * OLED_WIDTH;

        for (x = 0U; x < OLED_WIDTH; x++)
        {
            packet[x + 1U] = page_buf[offset + x];
        }

        status = I2C_MasterTransmit(OLED_ADDR7, packet, (uint16_t)sizeof(packet));

        if (status != I2C_STATUS_OK)
        {
            return status;
        }
    }

    return I2C_STATUS_OK;
}

I2C_Status OLED_Fill(uint8_t value)
{
    uint8_t page;
    uint16_t i;
    I2C_Status status;

    /* 控制字节加一页像素数据 */
    uint8_t data[OLED_WIDTH + 1U];

    uint8_t position_cmd[] =
    {
        0x00U,        /* 后续内容为命令 */
        0xB0U,        /* 页地址，在循环中更新 */
        0x00U,        /* 列地址低四位为零 */
        0x10U         /* 列地址高四位为零 */
    };

    /* 每个独立的数据传输包都需要控制字节 */
    data[0] = 0x40U;

    for (i = 1U; i < (uint16_t)sizeof(data); i++)
    {
        data[i] = value;
    }

    for (page = 0U; page < OLED_PAGE_COUNT; page++)
    {
        /* 选择当前页，并从第零列开始 */
        position_cmd[1] = (uint8_t)(0xB0U | page);

        status = I2C_MasterTransmit(
            OLED_ADDR7,
            position_cmd,
            (uint16_t)sizeof(position_cmd));

        if (status != I2C_STATUS_OK)
        {
            return status;
        }

        /* 连续写入当前页的全部一百二十八列 */
        status = I2C_MasterTransmit(
            OLED_ADDR7,
            data,
            (uint16_t)sizeof(data));

        if (status != I2C_STATUS_OK)
        {
            return status;
        }
    }

    return I2C_STATUS_OK;
}

/*===========================================================================
 * 私有函数实现
 *===========================================================================*/

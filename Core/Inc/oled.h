#ifndef __OLED_H
#define __OLED_H

#include "main.h"

/* ============ 驱动版本选择（选做：硬件 I²C 驱动） ============
 * 0 = 软件模拟 I²C（oled.c，SDA=PB0 / SCL=PB1，默认，已实测通过）
 * 1 = 硬件 I²C1  （oled_hw.c，SDA=PB7 / SCL=PB6，需要把两根线换到 PB6/PB7）
 *
 * 切换方法：编译时加一个宏 -DOLED_USE_HW_I2C=1，main.c 一行都不用改；
 * 两个驱动实现同一套接口（OLED_Init / OLED_Clear / OLED_ShowChar /
 * OLED_ShowString / OLED_ClearArea），上层程序完全不感知底层是软件还是硬件。
 */
#ifndef OLED_USE_HW_I2C
#define OLED_USE_HW_I2C 0
#endif

/* ============ 软件 I2C 引脚定义 ============
 * SDA -> PB0
 * SCL -> PB1
 * 接线：OLED VCC -> 3.3V，GND -> GND，SCL -> PB1，SDA -> PB0
 */
#define OLED_SCL_PORT   GPIOB
#define OLED_SCL_PIN    GPIO_PIN_1
#define OLED_SDA_PORT   GPIOB
#define OLED_SDA_PIN    GPIO_PIN_0

/* ============ 硬件 I2C1 引脚定义（选做）============
 * I2C1 在 STM32F103 上的默认映射就是 PB6=SCL、PB7=SDA，不需要 AFIO 重映射。
 * 接线：OLED VCC -> 3.3V，GND -> GND，SCL -> PB6，SDA -> PB7
 * 引脚必须配成复用开漏（GPIO_MODE_AF_OD），这是 I²C 电气特性的硬性要求：
 * 总线是"线与"结构，任何一方拉低都算低电平，推挽输出会短路。
 */
#define OLED_HW_SCL_PORT       GPIOB
#define OLED_HW_SCL_PIN        GPIO_PIN_6
#define OLED_HW_SDA_PORT       GPIOB
#define OLED_HW_SDA_PIN        GPIO_PIN_7
#define OLED_HW_I2C_INSTANCE   I2C1
#define OLED_HW_CLOCK_SPEED    400000U   /* SSD1306 支持到 400kHz 快速模式 */

/* 屏幕参数：128x64，页寻址模式，一页 8 行像素，共 8 页 */
#define OLED_WIDTH      128
#define OLED_PAGES      8
#define OLED_CHAR_W     8    /* 8x16 字体每个字符宽 8 像素，占 2 页 */

/* SSD1306 从机地址（7 位地址 + 写位）：
 * 0x78 = 0x3C<<1（A0 接地，绝大多数模块）
 * 0x7A = 0x3D<<1（A0 接 VCC 的模块）
 * 初始化时会自动探测，两个都试一遍。 */
#define OLED_ADDR_PRIMARY  0x78
#define OLED_ADDR_ALT      0x7A

/* 诊断变量：可通过 SWD 读出，用来判断模块有没有应答 */
extern volatile uint8_t  oled_addr_found;
extern volatile uint32_t oled_ack_ok;
extern volatile uint32_t oled_ack_fail;
extern volatile uint32_t oled_bus_err;   /* 硬件 I²C 传输失败次数（软件版恒为 0） */

void OLED_Init(void);
void OLED_Clear(void);
void OLED_ShowChar(uint8_t x, uint8_t page, char ch);
void OLED_ShowString(uint8_t x, uint8_t page, const char *str);
void OLED_ClearArea(uint8_t x0, uint8_t page0, uint8_t x1, uint8_t page1);

#endif /* __OLED_H */

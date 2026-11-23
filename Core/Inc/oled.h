#ifndef __OLED_H
#define __OLED_H

#include "main.h"

/* ============ 软件 I2C 引脚定义 ============
 * SDA -> PB0
 * SCL -> PB1
 * 接线：OLED VCC -> 3.3V，GND -> GND，SCL -> PB1，SDA -> PB0
 */
#define OLED_SCL_PORT   GPIOB
#define OLED_SCL_PIN    GPIO_PIN_1
#define OLED_SDA_PORT   GPIOB
#define OLED_SDA_PIN    GPIO_PIN_0

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

void OLED_Init(void);
void OLED_Clear(void);
void OLED_ShowChar(uint8_t x, uint8_t page, char ch);
void OLED_ShowString(uint8_t x, uint8_t page, const char *str);
void OLED_ClearArea(uint8_t x0, uint8_t page0, uint8_t x1, uint8_t page1);

#endif /* __OLED_H */

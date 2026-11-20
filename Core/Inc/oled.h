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

void OLED_Init(void);
void OLED_Clear(void);
void OLED_ShowChar(uint8_t x, uint8_t page, char ch);
void OLED_ShowString(uint8_t x, uint8_t page, const char *str);
void OLED_ClearArea(uint8_t x0, uint8_t page0, uint8_t x1, uint8_t page1);

#endif /* __OLED_H */

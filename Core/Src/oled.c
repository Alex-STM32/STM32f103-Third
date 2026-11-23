/**
  ******************************************************************************
  * @file    oled.c
  * @brief   SSD1306 OLED 驱动（软件模拟 I2C，无硬件 I2C 外设）
  *
  * 原理说明：
  *   1) 软件 I2C：用 GPIO 的推挽输出，按 I2C 时序手动翻转 SCL/SDA，
  *      每一位数据在 SCL 高电平期间保持稳定，SCL 为高时被从机采样。
  *   2) SSD1306 作为从机只接收不发送，因此本驱动写数据后忽略 ACK，
  *      只完成第 9 个时钟即可。
  *   3) 显存按“页”组织：8 页，每页 8 行像素，1 字节对应一列 8 个像素，
  *      bit0 = 该页最上一行，bit7 = 最下一行。
  *   4) 8x16 字体：一个字符占 8 列 × 16 行，即 2 页，每页 8 字节。
  *
  * 接线：VCC-3.3V  GND-GND  SCL-PB1  SDA-PB0
  * 字模来源：本项目 oledfont.h（由系统 8x16 点阵化生成，符号集 0x20~0x7E）
  ******************************************************************************
  */

/* 本文件是"软件模拟 I²C"版本。选做要求两份代码都要有：
   硬件 I²C 版本在 oled_hw.c 里，编译时加 -DOLED_USE_HW_I2C=1 就会切过去，
   此时本文件整体被编译掉（两个版本符号名相同，不能同时参与链接；
   字体表 oledfont.h 也是定义而不是声明，必须只被一个 .c 包含，否则重复定义）。
   所以下面的 #if 必须包在 #include 之前。 */
#if !OLED_USE_HW_I2C

#include "oled.h"
#include "oledfont.h"

/* ---------------- 底层引脚操作 ---------------- */

/* 两个引脚都配成开漏输出：写 1 = 释放总线（由上拉电阻拉高），写 0 = 拉低。
   开漏的好处是可以直接读回线上真实电平，从而判断从机是否应答（ACK）。 */
static void SDA_H(void) { HAL_GPIO_WritePin(OLED_SDA_PORT, OLED_SDA_PIN, GPIO_PIN_SET);   }
static void SDA_L(void) { HAL_GPIO_WritePin(OLED_SDA_PORT, OLED_SDA_PIN, GPIO_PIN_RESET); }
static void SCL_H(void) { HAL_GPIO_WritePin(OLED_SCL_PORT, OLED_SCL_PIN, GPIO_PIN_SET);   }
static void SCL_L(void) { HAL_GPIO_WritePin(OLED_SCL_PORT, OLED_SCL_PIN, GPIO_PIN_RESET); }
static uint8_t SDA_READ(void) { return (uint8_t)HAL_GPIO_ReadPin(OLED_SDA_PORT, OLED_SDA_PIN); }

/* 从机地址：0x78（多数模块，A0 接地）/ 0x7A（少数模块，A0 接 VCC） */
static uint8_t s_oled_addr = OLED_ADDR_PRIMARY;

/* 诊断变量（可通过 SWD 读出，判断模块到底有没有应答） */
volatile uint8_t  oled_addr_found = 0;   /* 0=两个地址都无应答 1=0x78 应答 2=0x7A 应答 */
volatile uint32_t oled_ack_ok      = 0;  /* 收到 ACK 的字节数 */
volatile uint32_t oled_ack_fail    = 0;  /* 未收到 ACK 的字节数 */
volatile uint32_t oled_bus_err     = 0;  /* 软件版没有总线错误概念，恒为 0 */

/* 位延时：72MHz 下约 2.5~3us，对应 SCL 约 150~180kHz，
   远低于 SSD1306 允许的 400kHz，避免时序过快导致模块不识别 */
static void I2C_Delay(void)
{
  for (volatile uint16_t i = 0; i < 40; i++)
  {
    __NOP();
  }
}

/* ---------------- I2C 时序 ---------------- */

static void I2C_Start(void)
{
  SDA_H();
  SCL_H();
  I2C_Delay();
  SDA_L();          /* SCL 高电平期间 SDA 由高变低 = 起始条件 */
  I2C_Delay();
  SCL_L();
  I2C_Delay();
}

static void I2C_Stop(void)
{
  SDA_L();
  I2C_Delay();
  SCL_H();
  I2C_Delay();
  SDA_H();          /* SCL 高电平期间 SDA 由低变高 = 停止条件 */
  I2C_Delay();
}

/* 发送一个字节（高位在前），返回从机应答：0 = ACK（从机拉低），1 = NACK */
static uint8_t I2C_WriteByte(uint8_t dat)
{
  uint8_t ack;

  for (uint8_t i = 0; i < 8; i++)
  {
    if (dat & 0x80) { SDA_H(); } else { SDA_L(); }
    dat <<= 1;
    I2C_Delay();
    SCL_H();        /* 从机在 SCL 高电平采样 */
    I2C_Delay();
    SCL_L();
    I2C_Delay();
  }

  /* 第 9 个时钟：主机释放 SDA（开漏写 1），从机应答时会把 SDA 拉低 */
  SDA_H();
  I2C_Delay();
  SCL_H();
  I2C_Delay();
  ack = SDA_READ();   /* 0 = 从机拉低 = ACK */
  SCL_L();
  I2C_Delay();

  if (ack == 0) oled_ack_ok++; else oled_ack_fail++;
  return ack;
}

/* ---------------- SSD1306 命令/数据 ---------------- */

static void OLED_WriteCmd(uint8_t cmd)
{
  I2C_Start();
  I2C_WriteByte(s_oled_addr);   /* 第一个字节必须是从机地址 + 写位 */
  I2C_WriteByte(0x00);          /* 控制字节：0x00 = 后续为命令 */
  I2C_WriteByte(cmd);
  I2C_Stop();
}

/* 连续写多个数据字节（整页刷新用它，比逐字节起始/停止快很多） */
static void OLED_WriteDataBuf(const uint8_t *buf, uint16_t len)
{
  I2C_Start();
  I2C_WriteByte(s_oled_addr);
  I2C_WriteByte(0x40);          /* 控制字节：0x40 = 后续为显存数据 */
  for (uint16_t i = 0; i < len; i++)
  {
    I2C_WriteByte(buf[i]);
  }
  I2C_Stop();
}

/* 设置写入起点：page(0~7)，x(0~127) */
static void OLED_SetPos(uint8_t x, uint8_t page)
{
  OLED_WriteCmd(0xB0 + (page & 0x07));          /* 页地址 */
  OLED_WriteCmd(0x00 + (x & 0x0F));             /* 列低 4 位 */
  OLED_WriteCmd(0x10 + ((x >> 4) & 0x0F));      /* 列高 4 位 */
}

/* ---------------- 对外接口 ---------------- */

void OLED_Init(void)
{
  /* PB0/PB1 作为普通开漏输出（CubeMX 未配置，这里自行初始化） */
  __HAL_RCC_GPIOB_CLK_ENABLE();

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin   = OLED_SCL_PIN | OLED_SDA_PIN;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_OD;   /* 开漏：可读回总线电平，判断 ACK */
  GPIO_InitStruct.Pull  = GPIO_PULLUP;           /* 内部弱上拉（模块上还有 4.7k 外部上拉） */
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  SCL_H();
  SDA_H();
  HAL_Delay(100);          /* 等 OLED 上电稳定 */

  /* ---- 探测从机地址：哪个地址有应答就用哪个 ---- */
  I2C_Start();
  if (I2C_WriteByte(OLED_ADDR_PRIMARY) == 0) {
    s_oled_addr     = OLED_ADDR_PRIMARY;
    oled_addr_found = 1;
    I2C_Stop();
  } else {
    I2C_Stop();
    HAL_Delay(2);
    I2C_Start();
    if (I2C_WriteByte(OLED_ADDR_ALT) == 0) {
      s_oled_addr     = OLED_ADDR_ALT;
      oled_addr_found = 2;
    } else {
      s_oled_addr     = OLED_ADDR_PRIMARY;
      oled_addr_found = 0;   /* 两个都没应答：接线或供电问题 */
    }
    I2C_Stop();
  }
  HAL_Delay(2);

  /* SSD1306 初始化序列（128x64，页寻址模式） */
  OLED_WriteCmd(0xAE);     /* 关显示 */
  OLED_WriteCmd(0xD5); OLED_WriteCmd(0x80);   /* 时钟分频 */
  OLED_WriteCmd(0xA8); OLED_WriteCmd(0x3F);   /* 复用比 1/64 */
  OLED_WriteCmd(0xD3); OLED_WriteCmd(0x00);   /* 显示偏移 0 */
  OLED_WriteCmd(0x40);                        /* 起始行 0 */
  OLED_WriteCmd(0x8D); OLED_WriteCmd(0x14);   /* 电荷泵使能（必须） */
  OLED_WriteCmd(0x20); OLED_WriteCmd(0x02);   /* 页寻址模式 */
  OLED_WriteCmd(0xA1);                        /* 段重映射：左右方向 */
  OLED_WriteCmd(0xC8);                        /* COM 扫描方向：上下方向 */
  OLED_WriteCmd(0xDA); OLED_WriteCmd(0x12);   /* COM 引脚配置 */
  OLED_WriteCmd(0x81); OLED_WriteCmd(0xCF);   /* 对比度 */
  OLED_WriteCmd(0xD9); OLED_WriteCmd(0xF1);   /* 预充电周期 */
  OLED_WriteCmd(0xDB); OLED_WriteCmd(0x40);   /* VCOMH */
  OLED_WriteCmd(0xA4);                        /* 显示跟随显存 */
  OLED_WriteCmd(0xA6);                        /* 正常显示（非反色） */
  OLED_WriteCmd(0xAF);                        /* 开显示 */
  HAL_Delay(20);

  OLED_Clear();
}

void OLED_Clear(void)
{
  uint8_t zero[OLED_WIDTH];
  for (uint16_t i = 0; i < OLED_WIDTH; i++)
  {
    zero[i] = 0x00;
  }
  for (uint8_t page = 0; page < OLED_PAGES; page++)
  {
    OLED_SetPos(0, page);
    OLED_WriteDataBuf(zero, OLED_WIDTH);
  }
}

/* 清一块区域（用于局部刷新） */
void OLED_ClearArea(uint8_t x0, uint8_t page0, uint8_t x1, uint8_t page1)
{
  uint8_t zero[OLED_WIDTH];
  for (uint16_t i = 0; i < OLED_WIDTH; i++)
  {
    zero[i] = 0x00;
  }
  for (uint8_t page = page0; page <= page1 && page < OLED_PAGES; page++)
  {
    OLED_SetPos(x0, page);
    OLED_WriteDataBuf(zero, (uint16_t)(x1 - x0));
  }
}

/* 显示一个 8x16 字符：x 为像素列，page 为起始页（字符占 page 和 page+1） */
void OLED_ShowChar(uint8_t x, uint8_t page, char ch)
{
  if (ch < 0x20 || ch > 0x7E)
  {
    ch = ' ';
  }
  const unsigned char *glyph = &F8X16[(ch - 0x20) * 16];

  OLED_SetPos(x, page);
  OLED_WriteDataBuf(glyph, 8);          /* 上半页：字符第 0~7 行 */
  OLED_SetPos(x, page + 1);
  OLED_WriteDataBuf(glyph + 8, 8);      /* 下半页：字符第 8~15 行 */
}

/* 显示字符串（8x16），超出屏幕宽度自动截断 */
void OLED_ShowString(uint8_t x, uint8_t page, const char *str)
{
  while (*str != '\0')
  {
    if (x + OLED_CHAR_W > OLED_WIDTH)
    {
      break;                            /* 一行最多 16 个字符 */
    }
    OLED_ShowChar(x, page, *str);
    x += OLED_CHAR_W;
    str++;
  }
}

#endif /* !OLED_USE_HW_I2C ---- 软件 I2C 版本结束 ---- */

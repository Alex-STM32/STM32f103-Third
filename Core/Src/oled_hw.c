/**
  ******************************************************************************
  * @file    oled_hw.c
  * @brief   SSD1306 OLED 驱动（选做：使用 STM32 硬件 I2C1 外设）
  *
  * 这是"选做"要求的第二份代码，和 oled.c（软件模拟 I2C）实现完全相同的接口，
  * 上层 main.c 一个字都不用改，只在编译时切换宏 OLED_USE_HW_I2C 即可。
  *
  * 一、软件 I²C 与硬件 I²C 的本质区别
  *   软件 I²C：把 SCL/SDA 当普通 GPIO，在 CPU 里用 for 循环加延时手动翻电平，
  *             一个字节要 CPU 亲自跑 8 遍循环，全程占着 CPU，速度由延时循环决定
  *             （本工程实测约 150kHz）。
  *   硬件 I²C：I²C1 外设内部就是一台"移位状态机"，只要把要发的字节写进数据寄存器，
  *             起始位、8 个时钟、第 9 个应答时钟、停止位全部由硬件自动产生，
  *             CPU 只在开始和结束时各参与一次（HAL_I2C_Master_Transmit 调用）。
  *             好处：不占 CPU、时序由硬件保证（不受主循环里别人拖慢影响）、
  *             可以跑标准快速模式 400kHz，刷新更快；坏处：引脚必须让给外设，
  *             而且必须用复用开漏模式。
  *
  * 二、引脚
  *   I2C1 默认映射：PB6 = SCL，PB7 = SDA（不需要 AFIO 重映射）
  *   接线：OLED VCC -> 3.3V，GND -> GND，SCL -> PB6，SDA -> PB7
  *   注意：和软件版（PB0/PB1）是两套接法，换版本时两根杜邦线必须跟着换位置。
  *
  * 三、为什么必须用"复用开漏"
  *   I²C 总线是线与结构，多个器件都能把线拉低，靠外部上拉拉高。
  *   如果用推挽输出，一旦一方输出高、另一方输出低，就是电源对地短路。
  *   所以 GPIO 必须配 GPIO_MODE_AF_OD，让外设接管引脚并且只用开漏驱动。
  *
  * 四、SSD1306 的写时序
  *   每个传输包都以"从机地址 + 写位（0x78）"开头，第二个字节是控制字节：
  *     0x00 = 后面都是命令
  *     0x40 = 后面都是显存数据
  *   硬件 I²C 一次 Master_Transmit 就能把"控制字节 + 一串数据"整包发出去，
  *   不像软件版那样每个字节都要 CPU 打一拍时钟。
  *
  * 五、健壮性
  *   HAL 的 I²C 在总线上出现异常（比如 SDA 被从机拉住不放、AF 标志置位）后
  *   可能卡在 BUSY 状态，之后每次调用都直接返回 HAL_BUSY。
  *   本驱动出错时先 HAL_I2C_DeInit 再重新 Init（相当于把外设复位一次），
  *   连续出错也会自动恢复，同时用 oled_bus_err 计数留证据。
  ******************************************************************************
  */

/* #if 必须包在 #include 之前：oledfont.h 里是字模数组的"定义"，
   两个 .c 同时包含就会在链接阶段报 F8X16 multiple definition。 */
#if OLED_USE_HW_I2C

#include "oled.h"
#include "oledfont.h"

/* ---------------- 内部变量 ---------------- */

static I2C_HandleTypeDef s_hi2c;              /* 硬件 I2C1 句柄 */
static uint8_t  s_oled_addr = OLED_ADDR_PRIMARY;

/* 诊断变量（和软件版同名，可通过 SWD 读出） */
volatile uint8_t  oled_addr_found = 0;   /* 0=两个地址都无应答 1=0x78 应答 2=0x7A 应答 */
volatile uint32_t oled_ack_ok      = 0;  /* 成功完成的传输次数 */
volatile uint32_t oled_ack_fail    = 0;  /* 失败的传输次数（NACK/超时/BUSY） */
volatile uint32_t oled_bus_err     = 0;  /* 触发"外设复位重来"的次数 */

/* 发送缓冲区：1 个控制字节 + 最多 128 列数据。用静态区，避免 129 字节压在栈上 */
static uint8_t s_txbuf[1 + OLED_WIDTH];

/* 每次传输的超时时间（ms）。400kHz 下 129 字节约 2.9ms，留足余量 */
#define OLED_I2C_TIMEOUT  20U

/* ---------------- I2C1 初始化 ---------------- */

/* 只做"外设+引脚"这一步，方便出错后重复调用 */
static void OLED_HW_I2C_Init(void)
{
  /* 1) 时钟：GPIOB 和 I2C1 都挂在 APB2/APB1 上，必须分别打开 */
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_I2C1_CLK_ENABLE();

  /* 2) 引脚：复用开漏 + 高速。外部模块自带 4.7k 上拉，这里再开内部上拉更稳 */
  GPIO_InitTypeDef gpio = {0};
  gpio.Pin   = OLED_HW_SCL_PIN | OLED_HW_SDA_PIN;
  gpio.Mode  = GPIO_MODE_AF_OD;
  gpio.Pull  = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(OLED_HW_SCL_PORT, &gpio);

  /* 3) 外设参数：400kHz 快速模式、7 位地址、单主机 */
  s_hi2c.Instance             = OLED_HW_I2C_INSTANCE;
  s_hi2c.Init.ClockSpeed      = OLED_HW_CLOCK_SPEED;
  s_hi2c.Init.DutyCycle       = I2C_DUTYCYCLE_2;          /* 快速模式占空比 2:1 */
  s_hi2c.Init.OwnAddress1     = 0;
  s_hi2c.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
  s_hi2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  s_hi2c.Init.OwnAddress2     = 0;
  s_hi2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  s_hi2c.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;     /* 允许从机拉低 SCL 拖时钟 */
  HAL_I2C_Init(&s_hi2c);
}

/* 出错恢复：把外设整个复位再初始化，清掉 BUSY/AF 这类卡死状态 */
static void OLED_HW_I2C_Recover(void)
{
  oled_bus_err++;
  HAL_I2C_DeInit(&s_hi2c);
  OLED_HW_I2C_Init();
  HAL_Delay(1);
}

/* ---------------- 底层收发 ---------------- */

/* 发一包：buf[0] 是控制字节，后面是数据。返回 0 成功，非 0 失败 */
static uint8_t OLED_HW_Transmit(const uint8_t *buf, uint16_t len)
{
  HAL_StatusTypeDef st = HAL_I2C_Master_Transmit(&s_hi2c, s_oled_addr,
                                                 (uint8_t *)buf, len, OLED_I2C_TIMEOUT);
  if (st != HAL_OK) {
    oled_ack_fail++;
    /* 只有 BUSY/超时这类才是总线卡住，才需要复位外设；
       NACK（从机没应答）多半是接线/地址问题，复位也没用，不计入 bus_err */
    if (st == HAL_BUSY || st == HAL_TIMEOUT) {
      OLED_HW_I2C_Recover();
    }
    return 1;
  }
  oled_ack_ok++;
  return 0;
}

/* 写一条命令 */
static void OLED_WriteCmd(uint8_t cmd)
{
  s_txbuf[0] = 0x00;      /* 控制字节：后面是命令 */
  s_txbuf[1] = cmd;
  OLED_HW_Transmit(s_txbuf, 2);
}

/* 连续写多个数据字节（整页刷新用） */
static void OLED_WriteDataBuf(const uint8_t *buf, uint16_t len)
{
  if (len > OLED_WIDTH) len = OLED_WIDTH;
  s_txbuf[0] = 0x40;      /* 控制字节：后面是显存数据 */
  for (uint16_t i = 0; i < len; i++) {
    s_txbuf[i + 1] = buf[i];
  }
  OLED_HW_Transmit(s_txbuf, (uint16_t)(len + 1));
}

/* 设置写入起点：page(0~7)，x(0~127) */
static void OLED_SetPos(uint8_t x, uint8_t page)
{
  OLED_WriteCmd(0xB0 + (page & 0x07));          /* 页地址 */
  OLED_WriteCmd(0x00 + (x & 0x0F));             /* 列低 4 位 */
  OLED_WriteCmd(0x10 + ((x >> 4) & 0x0F));      /* 列高 4 位 */
}

/* ---------------- 对外接口（与 oled.c 完全同名） ---------------- */

void OLED_Init(void)
{
  OLED_HW_I2C_Init();
  HAL_Delay(100);          /* 等 OLED 上电稳定 */

  /* 探测从机地址：0x78（A0 接地）/ 0x7A（A0 接 VCC）。
     HAL_I2C_IsDeviceReady 会发"地址 + 写位"并检查有没有 ACK，试 3 次。 */
  if (HAL_I2C_IsDeviceReady(&s_hi2c, OLED_ADDR_PRIMARY, 3, OLED_I2C_TIMEOUT) == HAL_OK) {
    s_oled_addr     = OLED_ADDR_PRIMARY;
    oled_addr_found = 1;
  } else if (HAL_I2C_IsDeviceReady(&s_hi2c, OLED_ADDR_ALT, 3, OLED_I2C_TIMEOUT) == HAL_OK) {
    s_oled_addr     = OLED_ADDR_ALT;
    oled_addr_found = 2;
  } else {
    s_oled_addr     = OLED_ADDR_PRIMARY;
    oled_addr_found = 0;   /* 两个都没应答：接线或供电问题 */
    OLED_HW_I2C_Recover(); /* 试探过程可能留下错误标志，复位一次再往下走 */
  }
  HAL_Delay(2);

  /* SSD1306 初始化序列（128x64，页寻址模式），和软件版逐条一致 */
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
  for (uint16_t i = 0; i < OLED_WIDTH; i++) {
    zero[i] = 0x00;
  }
  for (uint8_t page = 0; page < OLED_PAGES; page++) {
    OLED_SetPos(0, page);
    OLED_WriteDataBuf(zero, OLED_WIDTH);
  }
}

/* 清一块区域（用于局部刷新） */
void OLED_ClearArea(uint8_t x0, uint8_t page0, uint8_t x1, uint8_t page1)
{
  uint8_t zero[OLED_WIDTH];
  for (uint16_t i = 0; i < OLED_WIDTH; i++) {
    zero[i] = 0x00;
  }
  for (uint8_t page = page0; page <= page1 && page < OLED_PAGES; page++) {
    OLED_SetPos(x0, page);
    OLED_WriteDataBuf(zero, (uint16_t)(x1 - x0));
  }
}

/* 显示一个 8x16 字符：x 为像素列，page 为起始页（字符占 page 和 page+1） */
void OLED_ShowChar(uint8_t x, uint8_t page, char ch)
{
  if (ch < 0x20 || ch > 0x7E) {
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
  while (*str != '\0') {
    if (x + OLED_CHAR_W > OLED_WIDTH) {
      break;                            /* 一行最多 16 个字符 */
    }
    OLED_ShowChar(x, page, *str);
    x += OLED_CHAR_W;
    str++;
  }
}

#endif /* OLED_USE_HW_I2C ---- 硬件 I2C 版本结束 ---- */

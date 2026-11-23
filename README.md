# 电控组第三次培训作业

## 一、基本信息

- 姓名：白羽涵
- 学号：20263009177
- 工程名：Third（STM32F103C8T6 / Blue Pill，STM32CubeMX + CLion + OpenOCD）

## 二、功能实现概述

在第二次作业（3 颗 LED 流水灯）的基础上，本次增加了按键控制、串口数据包控制、串口反馈和 OLED 状态显示，四者共用同一份运行状态（`run_flag` / `dir_flag` / `interval_ms` / `led_index`），保证实际效果、屏幕显示和串口反馈一致。

### 1. 引脚分配

| 功能 | 引脚 | 配置 |
|---|---|---|
| LED0 / LED1 / LED2 | PA0 / PA1 / PA2 | 推挽输出，高电平点亮（串 220Ω 到 GND） |
| 方向切换按键 | PA11 | 上拉输入 + 下降沿外部中断（按键另一端接 GND） |
| 暂停/继续按键 | PA12 | 上拉输入 + 下降沿外部中断（按键另一端接 GND） |
| 串口 USART1 | PA9 (TX) / PA10 (RX) | 异步，115200-8-N1，接收中断 |
| OLED（软件 I²C） | PB1 = SCL，PB0 = SDA | 普通推挽输出，代码内自行初始化 |
| 调试 | PA13 / PA14 | Serial Wire（SWD） |
| 时钟 | HSE 8MHz → PLL×9 → SYSCLK 72MHz | AHB /1，APB1 /2，APB2 /1 |

### 2. 按键控制流水灯

- 按一次方向键（PA11）：正向 → 反向 / 反向 → 正向。
- 按一次暂停键（PA12）：运行 → 暂停 / 暂停 → 运行；暂停时保持当前点亮的 LED 不动，恢复后按当前方向继续。
- **消抖**：在外部中断回调里用 `HAL_GetTick()` 做 20ms 时间窗过滤（`now - last_key_tick < 20` 直接返回），一次按下只产生一个事件。中断里只置事件标志（`dir_event` / `pause_event`），真正的状态修改放在主循环。
- **暂停期间按键与串口仍然有效**：因为状态更新都在主循环里做，暂停只是不再推进 `led_index`，不影响事件处理。

## 三、串口数据包（接收）与反馈格式（发送）

### 1. 数据包格式（6 字节）

| 字节 | 字段 | 说明 |
|---|---|---|
| 1 | `AA` | 固定帧头 |
| 2 | `RUN` | 00 = 暂停，01 = 运行 |
| 3 | `DIR` | 00 = 正向，01 = 反向 |
| 4 | `STEP_L` | 切换间隔低字节 |
| 5 | `STEP_H` | 切换间隔高字节（低字节在前） |
| 6 | `CHECK` | 校验和 |

校验：`CHECK = (0xAA + RUN + DIR + STEP_L + STEP_H) % 256`。

接收流程：`HAL_UART_Receive_IT()` 一次接收 6 字节；收满后 `HAL_UART_RxCpltCallback()` 只置 `packet_ready` 标志；主循环里用结构体 `CmdPacket`（`__attribute__((packed)`，共 6 字节）解析，校验帧头、校验和、`RUN/DIR <= 1`、间隔在 100~2000ms 之间，全部通过才应用；任意一项不通过则整包丢弃、不改变状态。处理完统一重新开启接收，因此可以连续接收多包。

可用测试数据包：

| 状态 | 完整 6 字节 |
|---|---|
| 运行、正向、300ms | `AA 01 00 2C 01 D8` |
| 暂停、正向、300ms | `AA 00 00 2C 01 D7` |
| 运行、反向、300ms | `AA 01 01 2C 01 D9` |
| 暂停、反向、1000ms | `AA 00 01 E8 03 96` |

### 2. 自定义的串口反馈格式

数据包校验通过并应用设置后，以及按键改变运行状态或方向时，通过 `HAL_UART_Transmit_IT()`（串口中断发送）回传一行英文状态：

```
STATE=RUN,DIR=FWD,STEP=300,LED=1\r\n
```

字段含义：

| 字段 | 取值 | 含义 |
|---|---|---|
| `STATE` | `RUN` / `PAUSE` | 运行 / 暂停 |
| `DIR` | `FWD` / `REV` | 正向 / 反向 |
| `STEP` | 100~2000 | 当前相邻 LED 切换间隔，单位 ms |
| `LED` | 1 / 2 / 3 | 当前点亮的 LED 编号 |

以 `\r\n` 结尾，便于串口助手按行显示。发送用 `tx_busy` 标志保护，避免上一帧还没发完就覆盖缓冲区。

## 四、OLED 状态显示（软件 I²C）

- 屏幕：SSD1306 128×64，I²C 地址 0x78（写）。
- **软件 I²C**：不占用硬件 I²C 外设，直接用 GPIO 推挽输出按 I²C 时序翻转 SCL/SDA（每位数据在 SCL 高电平期间保持稳定，第 9 个时钟释放 SDA 忽略 ACK）。位延时约 0.6~0.8µs，满足 SSD1306 时序要求。
- 驱动分层：`Core/Src/oled.c` + `Core/Inc/oled.h`（时序、初始化、显存写入、字符/字符串显示），`Core/Inc/oledfont.h`（8×16 ASCII 点阵字模）。
- **硬件 I²C 版本**：见第十节（选做）。两份驱动对外接口完全一致（`OLED_Init` / `OLED_Clear` / `OLED_ShowChar` / `OLED_ShowString` / `OLED_ClearArea`），`main.c` 不需要任何改动，只靠编译宏切换底层。
- 显示内容四行，8×16 字体：

```
STATE: RUN
DIR: FWD
STEP: 300ms
LED: 1
```

- **刷新策略**：只在状态变化时刷新（`oled_dirty` 标志），刷新点包括：按键改变方向/暂停状态、串口数据包被应用、以及每次 LED 编号推进。因此暂停期间按键或串口改设置，屏幕也会立即更新；`LED` 行在刷新时取的是当次已经写入 GPIO 的 `led_index`，与实际点亮的那颗一致。每行固定 8×16 字符宽度，字符数与最长字符串（`STEP: 2000ms`，12 字符 = 96 像素）均不超过 128 像素，不需要额外补空格清尾。

## 五、参考资料与驱动来源

1. **HAL 库 / 外设初始化 / 中断机制**：ST 官方手册 UM1850（STM32F1 HAL 驱动说明）、STM32F103C8 数据手册；外设配置由 STM32CubeMX 生成。
2. **GPIO 外部中断、串口收发与数据包思路**：Keysking（B 站）STM32 教程第 4~9 集、江协科技（江科大自化协）STM32 教程串口部分；本次代码按作业要求自行组织，未直接复制教程工程。
3. **软件 I²C + SSD1306**：参考 SSD1306 数据手册的初始化序列与页寻址方式，以及常见开源 OLED 驱动的通用写法（中景园电子 OLED 教程思路）。本次的 `oled.c` 为按上述时序自行编写，未引用第三方驱动文件。
4. **字模 `oledfont.h`**：不采用网上现成字库，由本机用系统字体（Consolas 12pt）逐个字符点阵化后生成，覆盖 0x20~0x7E（95 个字符），每字符 16 字节（上/下半页各 8 字节，列优先，bit0 为页内最上一行）。
5. **`snprintf` 格式化输出**：newlib 文档；串口反馈与 OLED 文本均用它组包。

## 六、选做部分说明

本次**完成了“使用硬件 I²C 驱动 OLED 屏幕”的选做**，并且按选做要求保留了软件 I²C 版本——仓库里同时提交两份驱动代码：

| 版本 | 驱动文件 | 使用的引脚 | 编译默认 |
|---|---|---|---|
| 软件模拟 I²C | `Core/Src/oled.c` | SDA = PB0，SCL = PB1 | 是（不加宏） |
| 硬件 I²C1 | `Core/Src/oled_hw.c` | SDA = PB7，SCL = PB6 | 否（加 `-DOLED_USE_HW_I2C=1`） |

两份代码共用同一个头文件 `Core/Inc/oled.h` 和字模 `Core/Inc/oledfont.h`，接口完全相同，切换只改一个宏，详见第十节。

串口反馈仍使用基础要求的中断发送 `HAL_UART_Transmit_IT()`，没有另做 DMA 发送。

## 七、思考题回答

> 当流水灯的切换间隔较长时（如 2000ms），有没有出现按下按键和发送串口指令时不能立刻执行、会有一定延迟？如果有，问题出在哪里，可以怎样改进？

**本程序没有出现该延迟。** 原因是主循环里没有使用任何阻塞式延时：流水灯推进用 `HAL_GetTick()` 做时间戳比较（`HAL_GetTick() - last_tick >= interval_ms`），而不是 `HAL_Delay(interval_ms)`。因此无论间隔是 100ms 还是 2000ms，CPU 都不会停在那里等待，每圈循环都会立刻处理按键事件、串口数据包和 OLED 刷新，响应时间只取决于一圈循环的耗时（本工程一圈远小于 1ms）。

**如果改用 `HAL_Delay()` 会怎样**：`HAL_Delay()` 是靠 SysTick 计数忙等的阻塞函数，延时 2000ms 期间 CPU 停在这条语句上，主循环停止运转，按键事件和串口数据只能堆在标志里等着，表现就是"按下去要等灯切完才响应"，最长延迟接近一个切换间隔。

**改进方向**（本工程已经采用的即是第一种）：

1. 用时间戳 + 非阻塞状态机（本工程做法）：只在需要动作的那一刻判断时间差，其余时间主循环自由轮询。
2. 把流水灯推进放到定时器更新中断里（TIM + 中断，下一周内容），主循环只负责事件处理。
3. 用 RTOS 把流水灯、按键、串口分成不同任务，由调度器保证实时性。
4. 若必须用阻塞延时，也只能把延时切成小片（如每次 5ms）并在片间处理事件，本质仍是轮询，不如方案 1 干净。

## 八、工程结构与编译烧录

```
Third/
├── Core/
│   ├── Inc/            main.h, oled.h, oledfont.h, ...
│   └── Src/            main.c, oled.c, stm32f1xx_it.c, ...
├── Drivers/            STM32F1 HAL 库 + CMSIS
├── CMakeLists.txt      构建脚本（Core/ 下新增源文件会自动纳入）
├── STM32F103C8TX_FLASH.ld
└── README.md
```

编译：CLion 中直接 Build（或命令行 `cmake --build cmake-build-debug --target Third.elf`）。
烧录：ST-LINK V2 + OpenOCD（SWD：3.3V / GND / SWDIO / SWCLK），也可用 CLion 的 OpenOCD 运行配置。

> 说明：链接脚本 `STM32F103C8TX_FLASH.ld` 中原有的 `(READONLY)` 段属性已被移除，因为本机 GCC 10.3.1 不支持该属性，否则链接会报 `non constant or forward reference` 错误。

## 九、调试过程中修复的问题（真实排障记录）

本工程不是"一次点亮"，下面四件事都真实发生过、并定位到根因后修复。记录在此，便于复现和对照。

### 1. 烧录后 SWD 彻底连不上（调试口被代码关闭）

CubeMX 生成工程时 SYS → Debug 保持默认 `No Debug`，于是 `stm32f1xx_hal_msp.c` 里生成了：

```c
__HAL_AFIO_REMAP_SWJ_DISABLE();   /* 上电即关闭 JTAG 和 SWD */
```

结果是"刚上电一瞬间能连、程序跑起来就永远连不上"。修复：改为 `__HAL_AFIO_REMAP_SWJ_NOJTAG()`（保留 SWD、关闭 JTAG），并把 `.ioc` 的 Debug 改为 `Serial Wire`。芯片已被锁住时，把 BOOT0 跳线帽挪到 `1`、按 RESET 进入出厂 bootloader（此时不跑用户程序、SWD 活着），全片擦除后重新烧入。

### 2. 方向键一次按下被识别成两次

现象：引脚电平明确被拉低（示波级证据：GPIOA IDR bit11 由 1 变 0），但 `dir_flag` 始终是 0。根因两点：原消抖窗口只有 20ms，这颗按钮触点重复闭合超过 20ms，一次按下产生两个下降沿（`dir_flag` 翻转两次又回到原值）；另外两个按键共用同一个时间戳，几乎同时按会互相吞掉事件。

修复：每个按键各自记录上次被接受的时间，消抖窗口 100ms，抖动期内的重复边沿直接丢弃。

### 3. OLED 全黑（软件 I²C 漏发从机地址）

现象：接线正确、模块供电正常，但屏幕不亮。根因：`I2C_Start()` 之后直接发送控制字节 `0x00`，**从未发送 7 位从机地址 + 写位（`0x78`）**，模块收不到自己的地址就忽略后面所有内容。

修复：每次传输先发地址；把 SDA/SCL 改为开漏输出以便读回总线电平判断 ACK；初始化时自动探测 `0x78` 与 `0x7A` 两个常见地址；位延时放慢到 SCL 约 150kHz。修复后统计到 23381 个字节全部被模块 ACK、0 次失败。

### 4. 串口发几个包后彻底收不到数据

现象：前两包能执行，之后无论发什么都无反应。根因：串口溢出（ORE）发生后 HAL 停留在 busy 状态，`HAL_UART_Receive_IT` 一直返回 `HAL_BUSY`，接收再也挂不起来。

修复：增加 `HAL_UART_ErrorCallback`，出错时清 ORE 标志、复位 `RxState`、重新挂起接收；重挂失败时也先清错误再重试。同时增加 `uart_ok` / `uart_bad` / `uart_err` 三个计数器，用来区分"线没接（三个都不动）"和"收到了但校验不通过（`uart_bad` 增加）"。

### 验证方法说明

上述每一项都通过 SWD 直接读取芯片 RAM 中的变量核对，而不是只看现象：`interval_ms`、`run_flag`、`dir_flag`、`led_index`、`uart_ok/bad/err`、`oled_ack_ok/ack_fail`、以及 GPIO 输入寄存器 `GPIOx_IDR` 的实际电平。例如发送 `AA 00 01 C8 00 73` 后读回 `interval_ms = 0xC8 (200)`，即证明数据包被正确解析并执行。
## 十、选做：使用硬件 I²C 驱动 OLED（第二份代码）

### 1. 两种驱动怎么共存

选做要求"就算选择硬件 I²C，软件 I²C 同样需要完成，提交时提交两份代码"。本仓库的做法不是把两个工程各存一份，而是在**同一个工程里放两份驱动**，用编译宏二选一：

| 文件 | 内容 | 何时参与编译 |
|---|---|---|
| `Core/Src/oled.c` | 软件模拟 I²C（GPIO 翻转电平） | `OLED_USE_HW_I2C` 为 0（默认） |
| `Core/Src/oled_hw.c` | 硬件 I²C1（STM32 外设产生时序） | `OLED_USE_HW_I2C` 为 1 |
| `Core/Inc/oled.h` | 两个版本共用的声明、引脚、诊断变量 | 始终 |
| `Core/Inc/oledfont.h` | 8×16 点阵字模 | 始终（只在被选中的那个 .c 里包含） |

两个 `.c` 文件第一行都套了 `#if` 守卫，且 **`#if` 写在 `#include` 之前**——因为 `oledfont.h` 里是数组的"定义"而不是 `extern` 声明，两个文件同时包含就会在链接期报 `F8X16 multiple definition`。这一点在实际编译时踩到过。

两份驱动对外接口完全一致，所以 `main.c` 一行都不用改，两种底层的差异被完全封装在驱动内部。

### 2. 接线差异

| 信号 | 软件 I²C 版 | 硬件 I²C 版 |
|---|---|---|
| OLED VCC | 3.3V | 3.3V |
| OLED GND | GND | GND |
| SCL | PB1 | **PB6** |
| SDA | PB0 | **PB7** |

换版本时两根杜邦线必须跟着换位置，否则屏幕不亮（`oled_addr_found` 会读到 0，说明两个地址都没有应答）。

### 3. 原理对比

| 对比项 | 软件模拟 I²C | 硬件 I²C1 |
|---|---|---|
| 时序由谁产生 | CPU 用循环加延时逐个翻电平，每字节 CPU 跑 8 次循环 | I²C 外设的移位状态机自动产生起止位、8 个时钟和第 9 个应答时钟 |
| CPU 占用 | 每个字节全程占用 CPU | 只有调用 `HAL_I2C_Master_Transmit` 前后参与，中间由硬件完成 |
| 速率 | 由延时循环决定，本工程约 150kHz | 400kHz 快速模式（SSD1306 上限） |
| 引脚模式 | `GPIO_MODE_OUTPUT_OD`（普通开漏输出） | `GPIO_MODE_AF_OD`（**复用开漏**） |
| 抗干扰/稳定性 | 时序受主循环其他代码影响（被中断打断就会拉长电平时间） | 时序由硬件保证，不受主循环影响 |
| 出错处理 | 读回 SDA 判 ACK、计数 `oled_ack_ok/ack_fail` | HAL 返回状态码；`HAL_BUSY`/超时时 `HAL_I2C_DeInit` + 重新 `HAL_I2C_Init` 复位外设，计数 `oled_bus_err` |

**为什么硬件版必须用"复用开漏"而不是推挽**：I²C 总线是线与结构，所有器件只能"拉低"或"释放"。若用推挽输出，一方输出高、另一方输出低就是电源直接对地短路。开漏模式下高电平靠上拉电阻产生，多方同时驱动也不会冲突，这是 I²C 电气规范的硬性要求。

### 4. 硬件版的关键实现点

1. **外设初始化**：开 `GPIOB` 与 `I2C1` 时钟 → PB6/PB7 配 `AF_OD` → `HAL_I2C_Init` 设 400kHz、7 位地址、单主机。I2C1 在 F103 上默认映射就是 PB6/PB7，不需要 AFIO 重映射。
2. **控制字节**：SSD1306 的每次写传输都以"地址 + 写位（0x78）"开头，第二个字节是控制字节——`0x00` 表示后面是命令、`0x40` 表示后面是显存数据。硬件 I²C 一次 `Master_Transmit` 就能把"控制字节 + 一整页 128 列数据"整包发完，不像软件版那样每个字节都要 CPU 打一拍时钟。
3. **地址探测**：用 `HAL_I2C_IsDeviceReady()` 分别试 `0x78` 与 `0x7A`，哪个有 ACK 就用哪个，结果写进 `oled_addr_found`（1/2/0）。
4. **卡死恢复**：F1 的 I²C 在异常后容易停在 `BUSY`，之后每次调用直接返回 `HAL_BUSY`。驱动里遇到 `HAL_BUSY`/`HAL_TIMEOUT` 就 `DeInit` 再 `Init`（相当于复位外设），并累加 `oled_bus_err` 留证据；NACK 属于接线/地址问题，复位没用，不触发复位。
5. **缓冲区**：`static uint8_t s_txbuf[1 + 128]`（控制字节 + 一整页），放在静态区而不是栈上，避免 129 字节压在 1KB 栈上。

### 5. 两份固件的编译方法

软件 I²C 版（默认，也是基础要求验收用的版本）：

```powershell
cmake -S . -B cmake-build-debug -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug -j 8
```

硬件 I²C 版（只多一个宏，源码不做任何改动）：

```powershell
cmake -S . -B cmake-build-hw -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS="-DOLED_USE_HW_I2C=1"
cmake --build cmake-build-hw -j 8
```

在 CLion 里的等效做法：`Settings → CMake → CMake options` 里加 `-DCMAKE_C_FLAGS=-DOLED_USE_HW_I2C=1`，或在 `CMakeLists.txt` 里 `add_compile_definitions(OLED_USE_HW_I2C=1)`。

> **注意：工程已不再用 CubeMX 重新生成代码。** 硬件 I²C 是用代码手写的（`ioc` 里没有勾选 I2C1），所以 `stm32f1xx_hal_conf.h` 里的 `HAL_I2C_MODULE_ENABLED` 与 `Drivers/` 下的 `stm32f1xx_hal_i2c.c/.h` 是手工补进来的。如果再用 CubeMX 点「GENERATE CODE」，这三处会被按 `ioc` 现状覆盖掉，硬件版会编译不过。要重新生成就必须先在 CubeMX 里勾上 I2C1（PB6/PB7）再生成。

### 6. 两份固件的大小对比

| 版本 | RAM | FLASH | 说明 |
|---|---|---|---|
| 软件 I²C（默认） | 4344 B | 40900 B | 与基础版本完全一致，未因新增硬件版代码变大 |
| 硬件 I²C1 | 4560 B | 42812 B | 多出约 216 B RAM（I²C 句柄 80 B + 发送缓冲 129 B）与约 1.9 KB FLASH（HAL I²C 驱动） |

**代码互斥核验**（用 `arm-none-eabi-nm` 对比两个 elf）：

- 硬件版链接了 `HAL_I2C_Master_Transmit`、`HAL_I2C_IsDeviceReady`，**没有** `I2C_WriteByte`（软件位翻转函数）；
- 软件版链接了 `I2C_WriteByte`，**没有**任何 `HAL_I2C_*` 符号。

说明两份代码确实是两套独立的实现，通过编译期开关互斥地进入固件，而不是同一套代码换个名字。

### 7. 硬件版实测结论

烧入 `cmake-build-hw/Third.elf`、OLED 改接 PB6/PB7 后，用 SWD 直接读芯片 RAM 中的变量核对（不是只看现象）：

| 变量 | 读回值 | 说明 |
|---|---|---|
| `oled_addr_found` | 1 | `0x78` 有应答，地址探测成功 |
| `oled_bus_err` | 0 | 全程没有 BUSY/超时，外设一次都没被迫复位 |
| `oled_ack_fail` | 0 | 没有一次失败的传输 |
| `oled_ack_ok` | 27737 → 30297 → 32857 → 35417（1.5 秒采样一次） | 传输持续进行，说明屏幕在被持续刷新 |
| `GPIOA_ODR` | 0xB804 → 0xB802 → 0xB801 → 0xB804 | PA0~PA2 同一时刻只有一位为 1 且在轮转，流水灯正常 |
| `run_flag` / `interval_ms` | 0x01 / 0x012C（300） | 运行、300ms，串口下发的状态照常生效 |

结论：**硬件 I²C 版本与软件版功能完全一致**——四行状态显示、状态变化即时刷新、流水灯、串口数据包控制全部照常工作，差别只在"时序由 I²C1 外设产生"这一层。`oled_ack_ok` 在 6 秒内增长约 7700 次、`fail` 与 `bus_err` 始终为 0，说明 400kHz 下长时间运行没有出现 F1 硬件 I²C 常见的 BUSY 卡死。


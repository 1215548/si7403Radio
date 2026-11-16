# Si4703Radio — ESP32-C3 Supermini FM 收音机

板子：ESP32-C3 Supermini。屏：2.42 寸 OLED，SSD1309，128×64，4 线 SPI。
收音：Si4703（仓库名 `si7403Radio` 里的 `si7403` 是笔误，芯片是 Si4703，
手册在仓库根目录 `Si4703_datasheet.pdf`）。音频从 Si4703 的 LOUT/ROUT
出，接耳机/功放即可，本工程不经过 ESP32。

## 文件（模块化，一个外设一对文件）

| 文件 | 职责 |
|---|---|
| `Si4703Radio.ino` | 主程序：初始化顺序、按键含义、串口命令、每秒状态刷新 |
| `pins.h` | 全工程唯一的引脚表，改接线只改这里 |
| `led.h/.cpp` | 指示灯：常亮/常灭、按键闪一下、故障慢闪 |
| `keys.h/.cpp` | 5 按键：内部上拉、20ms 消抖、只上报“新按下” |
| `oled_display.h/.cpp` | OLED 显示：收音主界面、两行消息、测试图 |
| `radio_si4703.h/.cpp` | Si4703 驱动：3-wire 位-bang、寄存器、上电/调频/搜台/音量 |
| `DEV_Config.*`、`OLED_Driver.*`、`GUI_Paint.*`、`fonts.h`、`font*.cpp`、`ImageData.*`、`Debug.h` | 仓库原有的 Waveshare OLED 底层驱动，已按本工程引脚改过（CS 7 / RES 5 / DC 3，SPI 显式指定 SCLK 4 / MOSI 6，MODE3 + 事务制）；画布逻辑 128×64（旋转 270°） |

## 接线（`引脚对应.txt`）

| ESP32-C3 | 接到 | 说明 |
|---|---|---|
| IO7 | OLED CS | |
| IO5 | OLED RES | |
| IO3 | OLED D/C | |
| IO4 | OLED SCLK + Si4703 SCLK | 共用 |
| IO6 | OLED MOSI + Si4703 SDIO | 共用，SDIO 双向 |
| IO10 | Si4703 SEN（CS_1） | 3-wire 片选，空闲高 |
| IO2 | Si4703 RST（REST_1） | 低有效 |
| IO9 | LED | 高电平亮，见下“注意” |
| IO1 / IO0 / IO21 / IO20 / IO8 | KEY1..KEY5 | 按下低电平 |

## 编译上传

板子选 **ESP32C3 Dev Module**，`USB CDC On Boot: Enabled`：

```bash
arduino-cli compile -j 1 \
  --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc \
  --build-path ~/workspace/build/si4703radio Si4703Radio
```

上传用同一 FQBN 加 `--port <串口>`。串口监视器 115200，走 USB CDC。

## 分阶段验收

### 阶段 1：OLED、按键、LED（不依赖收音机）

1. 上电：LED 常亮，屏显示 `Si4703 FM / OLED OK`。
2. 按任意键：LED 闪一下，串口打印 `KEYx ...`，屏幕第三行显示按键动作。
3. 串口发 `t`：显示几何测试图（点/线/圆 + `Si4703 FM TEST OK`），1.5 秒后回主界面。

### 阶段 2：Si4703

上电自动：复位选 3-wire → 读 DEVICEID/CHIPID → 配寄存器 → 上电 →
调到 98.0MHz。成功时串口打印：

```
RADIO OK: DEVICEID=0x... CHIPID=0x1253 freq=98.00MHz
```

主界面显示频率 / RSSI / ST（立体声）或 MO（单声道）/ VOL，
LED 常亮表示当前是立体声信号。失败时屏幕显示 `NO RADIO`、
LED 慢闪、串口打印原因和寄存器值，按键/屏幕照常工作，串口发 `r` 重试。

## 操作

按键：KEY1 音量− / KEY2 音量+ / KEY3 频率−0.1MHz / KEY4 频率+0.1MHz /
KEY5 向上搜台。静音走串口。

串口（115200）：`f 98.5` 调频 / `s` 向上搜台 / `S` 向下搜台 /
`v 12` 音量 0–15 / `m` 静音开关 / `i` 读全部寄存器 00h–0Fh /
`t` 测试图 / `r` 重启收音机 / `?` 帮助。

## 设计要点（都是手册里的依据）

- **Si4703 用 3-wire，不用 I2C。**引脚表把 SDIO/SCLK 与 OLED 的
  MOSI/SCLK 共用、SEN 当 CS_1，这正是 3-wire 接法（手册 §4.8.1）；
  2-wire 需要 SCLK/SDIO 上拉且 SEN 复位时为高，引脚表里没有。
  3-wire 控制字 9 位：`0110 + R/W + 寄存器地址`，后跟 16 位数据；
  读时半周期转向后由芯片在 SCLK 上升沿送出、控制器在下降沿采样。
- **总线模式选择（手册 §4.9 方法 1）。**GPIO3 悬空，片内 1M 下拉
  使其为 0；RST 上升沿采样 SEN=0、SDIO=0 即选 3-wire。复位时
  SCLK 全程保持低（上升沿前 300ns 内不能有上升沿），RST 低电平
  保持 1ms（GPIO3 放电需 ≥100us）。
- **晶振（手册 §4.7、寄存器 07h）。**模块板载 32.768kHz 晶振时，
  须在 ENABLE=1 之前置 XOSCEN 位（`SI4703_USE_XTAL`，见 `pins.h`）。
  若模块既无晶振、RCLK 又没接外部时钟，调谐/搜台必然失败。
  上电前 TEST1 的 bits13:0 保持 0x0100，上电后为 0x3C04。
- **上电验证（手册 §6）。**上电前 CHIPID 应为 0x1200，上电后为
  0x1253；DEVICEID 低 12 位 MFGID=0x242。任一不对即报具体值，
  不猜原因。
- **中国 FM 参数。**波段 87.5–108MHz，步进 100kHz，
  去加重 50us（SYSCONFIG1 的 DE=1），SEEKTH=0x19。
- **seek/tune 期间少碰 SDIO（手册 4.7.1 勘误）。**内部晶振会被
  SDIO 活动干扰致误调；本驱动先静默 100–150ms 再稀疏轮询 STC。
- **OLED 与收音机共用 SPI 引脚。**OLED 用硬件 SPI，收音机用
  GPIO 位-bang；每次收音机传输前 `SPI.end()` 借走引脚，结束后
  `SPI.begin()` 还给 OLED；单线程顺序调用，不打架。
  OLED 写-only SPI 无法回读，初始化成功不等于屏亮，屏不亮先查
  供电/复位/接线。

## 注意 / 已知风险

- **IO9（LED）是 BOOT Strapping 脚。**复位瞬间 IO9 被拉低会进
  下载模式。若板子一上电就进下载模式、正常启动不了，先查 LED
  到地的通路是否把 IO9 拉低，必要时把 LED 改到其他空闲脚。
- **IO8（KEY5）和 IO2（Si4703 RST）也是 Strapping 脚。**
  上电/复位时不要按住按键；RST 由程序控制，复位后即拉高。
- **KEY3/KEY4 在 IO20/IO21**，那是 UART0 的 RX/TX；本工程串口
  走 USB CDC，不占 UART0，所以可以做按键。不要再启用 Serial0。
- **RDS 暂未接。**Si4703 的 RDS 默认关闭（SYSCONFIG1 RDS=0）；
  下一步可置 RDS=1 并读 RDSA–RDSD（0Ch–0Fh），寄存器读写接口
  已留好（`readRegister`/`readAllRegs`，串口 `i` 可直接看）。

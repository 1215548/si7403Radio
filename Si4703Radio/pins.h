// pins.h -- 全工程唯一的引脚表
// 来源:仓库根目录 引脚对应.txt(ESP32-C3 Supermini)。
// 改接线只改这里,各模块都引用这里的宏,不许在模块里写死引脚号。
#pragma once
#include <Arduino.h>

// ---------- OLED (SSD1309, 128x64, 4 线 SPI) ----------
#define PIN_OLED_CS   7   // CS
#define PIN_OLED_RES  5   // RES
#define PIN_OLED_DC   3   // D/C
#define PIN_OLED_SCLK 4   // SCLK,与 Si4703 共用
#define PIN_OLED_MOSI 6   // MOSI,与 Si4703 的 SDIO 共用

// ---------- Si4703 (FM 收音,3-wire 模式,见 radio_si4703.cpp) ----------
// 3-wire:SDIO=PIN_OLED_MOSI, SCLK=PIN_OLED_SCLK, SEN=PIN_SI4703_SEN(CS_1)
#define PIN_SI4703_SEN 10  // CS_1,3-wire 片选/使能,空闲高电平
#define PIN_SI4703_RST 2   // REST_1,复位,低有效
#define PIN_SI4703_SDIO PIN_OLED_MOSI  // SDIO,双向,写命令/读状态共用 MOSI
#define PIN_SI4703_SCLK PIN_OLED_SCLK  // SCLK

// ---------- 指示灯 ----------
// IO9 是 ESP32-C3 的 BOOT Strapping 脚:复位瞬间为低进下载模式,
// 为高才正常从 Flash 启动。LED 原接法(IO9→电阻→LED→GND)会在
// 复位时把 IO9 拉低,板子一直进下载模式(2026-11-16 实测)。
// 改法:LED 改低电平有效——LED 正极→电阻→3V3,负极→IO9;
// 复位时 IO9 经 LED 支路被拉到 3V3(无电流,LED 不亮),正常启动;
// 程序拉低 IO9 灯亮。代码用 LED_ACTIVE_LOW=1 跟随此接法。
// 若坚持高电平亮:IO9 后加 NPN 管驱动 LED(见 README),不要只加
// 上拉——LED 支路到地的分压仍可能把脚拉到 2.48V 阈值以下。
#define PIN_LED 9
#define LED_ACTIVE_LOW 1  // 1=拉低亮(改线后);0=拉高亮(须加三极管驱动)

// ---------- 按键(5 个,按下=低电平,内部上拉) ----------
// 注意:KEY5 在 IO8,也是 Strapping 脚,上电/复位时不要按住按键;
// KEY3/KEY4 在 IO20/IO21,那是 UART0 的 RX/TX,本工程串口走
// USB CDC(Serial),不占用 UART0,所以可以做按键。
#define PIN_KEY1 1   // KEY1
#define PIN_KEY2 0   // KEY2
#define PIN_KEY3 21  // KEY3
#define PIN_KEY4 20  // KEY4
#define PIN_KEY5 8   // KEY5

// ---------- 收音默认参数(中国 FM) ----------
// 波段 87.5~108MHz,步进 100kHz,去加重 50us(中国标准)。
#define RADIO_BAND_LOW_MHZ   87.5f
#define RADIO_BAND_HIGH_MHZ 108.0f
#define RADIO_SPACE_MHZ      0.1f
#define RADIO_DEFAULT_FREQ_MHZ 98.0f
#define RADIO_DEFAULT_VOLUME 10   // 0~15,0 静音
#define RADIO_SEEK_THRESHOLD 0x19 // SEEKTH,0x00 最灵敏~0x7F 最严

// Si4703 模块上的 32.768kHz 晶振:若你的模块板载了晶振(常见
// 成品模块都有),保持 1,驱动在 ENABLE 之前置 XOSCEN 位;
// 若模块既无晶振、RCLK 脚又没接外部时钟,调谐/搜台会失败,
// 此时置 0 也无济于事,须先给 RCLK 提供 32.768kHz 时钟。
#define SI4703_USE_XTAL 1

// ---------- 固件版本与网络 ----------
#define FW_VERSION "v0.5.1"   // 关于页显示;改动功能后手动 +1
#define AP_SSID "Si4703Radio" // 网页配置服务的 AP 名(开放,无密码)
#define AP_PASSWORD ""

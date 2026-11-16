// radio_si4703.h -- Si4703 FM 收音模块
// 芯片就是 Si4703(仓库名 si7403Radio 是笔误,手册 Si4702/03-C19)。
// 控制接口:3-wire 模式(手册 §4.8.1)。
//   为什么不用 2-wire(I2C):引脚表把 SDIO/SCLK 与 OLED 的 MOSI/SCLK
//   共用、SEN 当作 CS_1,这正是 3-wire 的接法(SEN=片选,SDIO=双向数据);
//   2-wire 需要 SCLK/SDIO 上拉电阻且 SEN 复位时为高,引脚表里没有,
//   而推挽 SPI 时钟与漏极开路 I2C 时钟共用同一根线也不合适。
// 总线仲裁:OLED 用硬件 SPI,本模块用 GPIO 位-bang,共用 IO4/IO6,
//   每次收音机传输前 SPI.end() 释放引脚,结束后 SPI.begin() 还给 OLED,
//   单线程顺序调用,不会打架。见 busAcquire()/busRelease()。
// 寄存器定义:手册 §5/§6。波段 87.5~108MHz,步进 100kHz,去加重 50us(中国)。
#pragma once
#include <Arduino.h>
#include "pins.h"

// 寄存器地址(手册 §5)
#define SI4703_REG_DEVICEID   0x00
#define SI4703_REG_CHIPID     0x01
#define SI4703_REG_POWERCFG   0x02
#define SI4703_REG_CHANNEL    0x03
#define SI4703_REG_SYSCONFIG1 0x04
#define SI4703_REG_SYSCONFIG2 0x05
#define SI4703_REG_SYSCONFIG3 0x06
#define SI4703_REG_TEST1      0x07
#define SI4703_REG_STATUSRSSI 0x0A
#define SI4703_REG_READCHAN   0x0B

// POWERCFG(02h) 各位
#define SI4703_PWR_DMUTE  0x4000  // 1=取消静音
#define SI4703_PWR_SEEKUP 0x0200
#define SI4703_PWR_SEEK   0x0100
#define SI4703_PWR_DISABLE 0x0040
#define SI4703_PWR_ENABLE 0x0001
// CHANNEL(03h)
#define SI4703_CH_TUNE 0x8000
// SYSCONFIG1(04h)
#define SI4703_CFG1_DE 0x0800     // 1=50us(中国),0=75us
// STATUSRSSI(0Ah)
#define SI4703_STC   0x4000
#define SI4703_SFBL  0x2000
#define SI4703_AFCRL 0x1000
#define SI4703_ST    0x0100       // 1=立体声
// TEST1(07h)
#define SI4703_TEST1_XOSCEN 0x8000
#define SI4703_TEST1_POWERDOWN 0x0100  // 上电前 bits13:0 保持值
#define SI4703_TEST1_POWERUP   0x3C04  // 上电后 bits13:0 保持值

// 期望的 ID(手册 §6):C19 版 Si4703 上电前 CHIPID=0x1200,上电后=0x1253;
// DEVICEID 低 12 位 MFGID=0x242。
// 但市售“Si4703”模块的芯片未必是 C19 版 Si4703:实测某模块上电前
// CHIPID=0x0800(DEV=0,即 Si4702,REV=2),调频功能与 Si4703 相同,
// 只是没有 RDS。因此这里不卡死 0x1200/0x1253,只认 MFGID 和
// DEV/FIRMWARE 位段,见 radio_si4703.cpp 的 begin()。
#define SI4703_CHIPID_BEFORE_POWERUP 0x1200
#define SI4703_CHIPID_AFTER_POWERUP  0x1253
#define SI4703_MFGID 0x242

class Si4703Radio {
public:
  // 复位选 3-wire、读 ID、上电、配寄存器、调到默认频率。
  // 成功返回 true;失败仍可继续用 OLED/按键,错误原因见 error()。
  bool begin();
  bool ok() const { return _ok; }
  const char* error() const { return _error; }
  uint16_t deviceId() const { return _deviceId; }
  uint16_t chipId() const { return _chipId; }
  // true=Si4703(含 RDS),false=Si4702(无 RDS,调频功能相同)
  bool isSi4703() const { return _isSi4703; }

  bool tune(float freqMHz);       // 手动调频,等 STC
  bool seek(bool up);             // 搜台,到有效台停;撞到波段边沿返回 false
  bool setVolume(uint8_t vol);    // 0~15
  bool setMute(bool mute);
  bool readStatus();              // 刷新 rssi/stereo/实际频率
  bool readAllRegs(uint16_t regs[16]); // 诊断:读 00h~0Fh

  float freqMHz() const { return _freqMHz; }
  int rssi() const { return _rssi; }          // dBuV,0~75
  bool isStereo() const { return _stereo; }
  uint8_t volume() const { return _volume; }
  bool isMuted() const { return _muted; }
  bool lastAfcRail() const { return _afcrl; }
  bool lastSeekFailed() const { return _sfbl; }

  uint16_t readRegister(uint8_t reg);
  bool writeRegister(uint8_t reg, uint16_t value);

private:
  void busAcquire();   // SPI.end() + 引脚切到 GPIO 输出
  void busRelease();   // SPI.begin() 把 SCLK/MOSI 还给 OLED
  void write3Wire(uint8_t reg, uint16_t value);
  uint16_t read3Wire(uint8_t reg);
  void clockBitOut(bool bit);  // SDIO 已是输出:置位后 SCLK 上升沿锁存
  bool waitStc(uint32_t timeoutMs, uint32_t pollMs);
  float chanToFreq(uint16_t chan) const {
    return RADIO_BAND_LOW_MHZ + chan * RADIO_SPACE_MHZ;
  }
  uint16_t freqToChan(float freqMHz) const {
    if (freqMHz < RADIO_BAND_LOW_MHZ) freqMHz = RADIO_BAND_LOW_MHZ;
    if (freqMHz > RADIO_BAND_HIGH_MHZ) freqMHz = RADIO_BAND_HIGH_MHZ;
    return (uint16_t)((freqMHz - RADIO_BAND_LOW_MHZ) / RADIO_SPACE_MHZ + 0.5f);
  }

  bool _ok = false;
  bool _isSi4703 = true;  // 由 CHIPID 的 DEV 位段判定,见 begin()
  char _error[64] = "";
  uint16_t _deviceId = 0;
  uint16_t _chipId = 0;
  uint16_t _shadow[8] = {0};  // 寄存器 02h~07h 的写缓存(03h CHANNEL 另存)
  float _freqMHz = RADIO_DEFAULT_FREQ_MHZ;
  int _rssi = 0;
  bool _stereo = false;
  bool _afcrl = false;
  bool _sfbl = false;
  uint8_t _volume = RADIO_DEFAULT_VOLUME;
  bool _muted = false;
};

// radio_si4703.cpp -- 见 radio_si4703.h
#include "radio_si4703.h"
#include <SPI.h>

// 3-wire 控制字(手册 §4.8.1):9 位,顺序 A7 A6 A5 R/W A4 A3 A2 A1 A0,
// 其中 A7:A4=0110b 为芯片地址,R/W 0=写/1=读,A3:A0=寄存器地址。
// 即控制字 = 0xC0 | (R/W<<5) | reg。
#define SI4703_CTRL_WRITE(reg) (uint16_t)(0xC0u | (reg))
#define SI4703_CTRL_READ(reg)  (uint16_t)(0xE0u | (reg))

// SCLK 最高 2.5MHz(手册表 6);位-bang 每沿 2us,约 125kHz,留足裕量。
#define SI4703_BIT_DELAY_US 2

// ---------- 总线仲裁:与 OLED 硬件 SPI 共用 IO4/IO6 ----------
void Si4703Radio::busAcquire() {
  digitalWrite(PIN_OLED_CS, HIGH);  // 先让 OLED 脱离总线,再借引脚
  SPI.end();
  pinMode(PIN_SI4703_SCLK, OUTPUT);
  pinMode(PIN_SI4703_SDIO, OUTPUT);
  digitalWrite(PIN_SI4703_SCLK, LOW);
  digitalWrite(PIN_SI4703_SDIO, LOW);
  digitalWrite(PIN_SI4703_SEN, HIGH);  // 空闲高电平
}

void Si4703Radio::busRelease() {
  digitalWrite(PIN_SI4703_SEN, HIGH);
  digitalWrite(PIN_SI4703_SCLK, LOW);
  pinMode(PIN_SI4703_SDIO, OUTPUT);
  digitalWrite(PIN_SI4703_SDIO, LOW);
  SPI.begin(PIN_OLED_SCLK, -1, PIN_OLED_MOSI, PIN_OLED_CS);
}

// ---------- 3-wire 底层 ----------
void Si4703Radio::clockBitOut(bool bit) {
  digitalWrite(PIN_SI4703_SDIO, bit ? HIGH : LOW);
  delayMicroseconds(SI4703_BIT_DELAY_US);
  digitalWrite(PIN_SI4703_SCLK, HIGH);  // 上升沿锁存
  delayMicroseconds(SI4703_BIT_DELAY_US);
  digitalWrite(PIN_SI4703_SCLK, LOW);
  delayMicroseconds(SI4703_BIT_DELAY_US);
}

void Si4703Radio::write3Wire(uint8_t reg, uint16_t value) {
  uint16_t ctrl = SI4703_CTRL_WRITE(reg);
  digitalWrite(PIN_SI4703_SEN, LOW);
  delayMicroseconds(SI4703_BIT_DELAY_US);
  for (int8_t i = 8; i >= 0; i--) clockBitOut((ctrl >> i) & 1u);
  for (int8_t i = 15; i >= 0; i--) clockBitOut((value >> i) & 1u);
  digitalWrite(PIN_SI4703_SEN, HIGH);
  // 手册:传输在 SEN 拉高后的下一个 SCLK 上升沿结束,补一个脉冲
  digitalWrite(PIN_SI4703_SCLK, HIGH);
  delayMicroseconds(SI4703_BIT_DELAY_US);
  digitalWrite(PIN_SI4703_SCLK, LOW);
}

uint16_t Si4703Radio::read3Wire(uint8_t reg) {
  uint16_t ctrl = SI4703_CTRL_READ(reg);
  uint16_t value = 0;
  digitalWrite(PIN_SI4703_SEN, LOW);
  delayMicroseconds(SI4703_BIT_DELAY_US);
  for (int8_t i = 8; i >= 0; i--) clockBitOut((ctrl >> i) & 1u);
  // 半周期总线转向:SDIO 改输入,由 Si4703 驱动
  pinMode(PIN_SI4703_SDIO, INPUT);
  delayMicroseconds(SI4703_BIT_DELAY_US * 2);
  // 数据在 SCLK 上升沿由芯片送出,控制器在下降沿采样(手册图 4)
  for (int8_t i = 15; i >= 0; i--) {
    digitalWrite(PIN_SI4703_SCLK, HIGH);
    delayMicroseconds(SI4703_BIT_DELAY_US);
    digitalWrite(PIN_SI4703_SCLK, LOW);
    delayMicroseconds(SI4703_BIT_DELAY_US);
    if (digitalRead(PIN_SI4703_SDIO) == HIGH) value |= (uint16_t)(1u << i);
  }
  digitalWrite(PIN_SI4703_SEN, HIGH);
  digitalWrite(PIN_SI4703_SCLK, HIGH);
  delayMicroseconds(SI4703_BIT_DELAY_US);
  digitalWrite(PIN_SI4703_SCLK, LOW);
  pinMode(PIN_SI4703_SDIO, OUTPUT);
  digitalWrite(PIN_SI4703_SDIO, LOW);
  return value;
}

// ---------- 对外读写(每次传输都做总线借/还) ----------
uint16_t Si4703Radio::readRegister(uint8_t reg) {
  busAcquire();
  uint16_t v = read3Wire(reg);
  busRelease();
  return v;
}

bool Si4703Radio::writeRegister(uint8_t reg, uint16_t value) {
  busAcquire();
  write3Wire(reg, value);
  busRelease();
  if (reg >= 0x02 && reg <= 0x07) _shadow[reg - 0x02] = value;
  return true;
}

// ---------- 初始化 ----------
bool Si4703Radio::begin() {
  _ok = false;
  _error[0] = '\0';

  pinMode(PIN_SI4703_RST, OUTPUT);
  pinMode(PIN_SI4703_SEN, OUTPUT);
  pinMode(PIN_SI4703_SCLK, OUTPUT);
  pinMode(PIN_SI4703_SDIO, OUTPUT);

  // 复位 + 总线模式选择(手册 §4.9 方法 1:GPIO3 悬空,片内 1M 下拉
  // 使其为 0;RST 上升沿采样 SEN=0/SDIO=0 即选 3-wire)。
  // 要求:RST 上升沿前 300ns 内 SCLK 不能有上升沿,故全程保持 SCLK 低。
  digitalWrite(PIN_SI4703_RST, LOW);
  digitalWrite(PIN_SI4703_SEN, LOW);
  digitalWrite(PIN_SI4703_SDIO, LOW);
  digitalWrite(PIN_SI4703_SCLK, LOW);
  delay(1);  // RST 低电平 ≥100us,等 GPIO3 内部下拉放电
  digitalWrite(PIN_SI4703_RST, HIGH);
  delay(1);  // SEN/SDIO/SCLK 在 RST 上升沿后保持 ≥30ns,1ms 绰绰有余
  digitalWrite(PIN_SI4703_SEN, HIGH);  // 空闲态
  delay(5);

  _deviceId = readRegister(SI4703_REG_DEVICEID);
  _chipId = readRegister(SI4703_REG_CHIPID);
  if (_deviceId == 0x0000 || _deviceId == 0xFFFF ||
      _chipId == 0x0000 || _chipId == 0xFFFF) {
    snprintf(_error, sizeof(_error),
             "no response: DEVICEID=0x%04X CHIPID=0x%04X", _deviceId, _chipId);
    return false;
  }
  if ((_deviceId & 0x0FFF) != SI4703_MFGID) {
    snprintf(_error, sizeof(_error),
             "MFGID mismatch: DEVICEID=0x%04X", _deviceId);
    return false;
  }
  if (_chipId != SI4703_CHIPID_BEFORE_POWERUP) {
    // 0x1200 是 Si4703 上电前应有的值;不是它,多半是总线模式选错
    // (如复位时 SEN/SDIO 电平不对进了 2-wire)或芯片不是 Si4703。
    snprintf(_error, sizeof(_error),
             "CHIPID before powerup=0x%04X, expect 0x1200", _chipId);
    return false;
  }

  // 晶振:须在 ENABLE=1 之前使能(手册 §4.7/寄存器 07h)。
  // 上电前 bits13:0 保持 0x0100。
#if SI4703_USE_XTAL
  writeRegister(SI4703_REG_TEST1,
                SI4703_TEST1_XOSCEN | SI4703_TEST1_POWERDOWN);
#else
  writeRegister(SI4703_REG_TEST1, SI4703_TEST1_POWERDOWN);
#endif

  // SYSCONFIG1:去加重 50us(中国),RDS 关,GPIO 高阻
  _shadow[SI4703_REG_SYSCONFIG1 - 0x02] = SI4703_CFG1_DE;
  writeRegister(SI4703_REG_SYSCONFIG1, _shadow[SI4703_REG_SYSCONFIG1 - 0x02]);
  // SYSCONFIG2:SEEKTH + 波段 87.5~108 + 步进 100kHz + 音量
  _volume = RADIO_DEFAULT_VOLUME;
  _shadow[SI4703_REG_SYSCONFIG2 - 0x02] =
      (uint16_t)((RADIO_SEEK_THRESHOLD << 8) | (0 << 6) | (1 << 4) | _volume);
  writeRegister(SI4703_REG_SYSCONFIG2, _shadow[SI4703_REG_SYSCONFIG2 - 0x02]);
  // SYSCONFIG3:默认 0
  _shadow[SI4703_REG_SYSCONFIG3 - 0x02] = 0x0000;
  writeRegister(SI4703_REG_SYSCONFIG3, 0x0000);

  // 上电:ENABLE=1 + DMUTE=1(取消静音)。上电时间最长 110ms(手册表 8)。
  _muted = false;
  _shadow[SI4703_REG_POWERCFG - 0x02] = SI4703_PWR_DMUTE | SI4703_PWR_ENABLE;
  writeRegister(SI4703_REG_POWERCFG, _shadow[SI4703_REG_POWERCFG - 0x02]);
  delay(110);

  _chipId = readRegister(SI4703_REG_CHIPID);
  if (_chipId != SI4703_CHIPID_AFTER_POWERUP) {
    snprintf(_error, sizeof(_error),
             "powerup failed: CHIPID=0x%04X, expect 0x1253", _chipId);
    return false;
  }

  _freqMHz = RADIO_DEFAULT_FREQ_MHZ;
  if (!tune(_freqMHz)) {
    snprintf(_error, sizeof(_error), "powerup ok, tune %.1fMHz no STC",
             _freqMHz);
    return false;
  }
  _ok = true;
  return true;
}

// ---------- 调谐 / 搜台 ----------
bool Si4703Radio::waitStc(uint32_t timeoutMs, uint32_t pollMs) {
  // 手册 4.7.1 勘误:seek/tune 期间 SDIO 活动会干扰内部晶振,
  // 故先静默一段时间再开始轮询,且轮询间隔放宽。
  uint32_t start = millis();
  while ((uint32_t)(millis() - start) < timeoutMs) {
    uint16_t st = readRegister(SI4703_REG_STATUSRSSI);
    if (st & SI4703_STC) return true;
    delay(pollMs);
  }
  return false;
}

bool Si4703Radio::tune(float freqMHz) {
  uint16_t chan = freqToChan(freqMHz);
  _shadow[SI4703_REG_CHANNEL - 0x02] = (uint16_t)(SI4703_CH_TUNE | chan);
  writeRegister(SI4703_REG_CHANNEL, _shadow[SI4703_REG_CHANNEL - 0x02]);
  delay(100);  //  quiet,见 waitStc 注释
  bool done = waitStc(600, 20);
  // 清 TUNE 位以清除 STC,否则下次调谐/搜台无法开始
  _shadow[SI4703_REG_CHANNEL - 0x02] = chan;
  writeRegister(SI4703_REG_CHANNEL, chan);
  readStatus();
  return done;
}

bool Si4703Radio::seek(bool up) {
  uint16_t pwr = _shadow[SI4703_REG_POWERCFG - 0x02];
  pwr = (uint16_t)((pwr & (uint16_t)~SI4703_PWR_SEEKUP) |
                   (up ? SI4703_PWR_SEEKUP : 0) | SI4703_PWR_SEEK);
  _shadow[SI4703_REG_POWERCFG - 0x02] = pwr;
  writeRegister(SI4703_REG_POWERCFG, pwr);
  delay(150);  // quiet,见 waitStc 注释
  bool done = waitStc(5000, 100);
  // 清 SEEK 位以清除 STC/SF/BL
  pwr = (uint16_t)(_shadow[SI4703_REG_POWERCFG - 0x02] &
                   (uint16_t)~SI4703_PWR_SEEK);
  _shadow[SI4703_REG_POWERCFG - 0x02] = pwr;
  writeRegister(SI4703_REG_POWERCFG, pwr);
  readStatus();
  return done && !_sfbl;
}

bool Si4703Radio::setVolume(uint8_t vol) {
  if (vol > 15) vol = 15;
  _volume = vol;
  uint16_t cfg2 = (uint16_t)((_shadow[SI4703_REG_SYSCONFIG2 - 0x02] & 0xFFF0u) | vol);
  _shadow[SI4703_REG_SYSCONFIG2 - 0x02] = cfg2;
  writeRegister(SI4703_REG_SYSCONFIG2, cfg2);
  return true;
}

bool Si4703Radio::setMute(bool mute) {
  _muted = mute;
  uint16_t pwr = _shadow[SI4703_REG_POWERCFG - 0x02];
  if (mute) pwr &= (uint16_t)~SI4703_PWR_DMUTE;  // DMUTE=0 即静音
  else pwr |= SI4703_PWR_DMUTE;
  _shadow[SI4703_REG_POWERCFG - 0x02] = pwr;
  writeRegister(SI4703_REG_POWERCFG, pwr);
  return true;
}

bool Si4703Radio::readStatus() {
  uint16_t st = readRegister(SI4703_REG_STATUSRSSI);
  uint16_t rc = readRegister(SI4703_REG_READCHAN);
  if (st == 0xFFFF || rc == 0xFFFF) return false;
  _rssi = st & 0xFF;
  _stereo = (st & SI4703_ST) != 0;
  _afcrl = (st & SI4703_AFCRL) != 0;
  _sfbl = (st & SI4703_SFBL) != 0;
  _freqMHz = chanToFreq((uint16_t)(rc & 0x03FF));
  return true;
}

bool Si4703Radio::readAllRegs(uint16_t regs[16]) {
  for (uint8_t i = 0; i < 16; i++) regs[i] = readRegister(i);
  return true;
}

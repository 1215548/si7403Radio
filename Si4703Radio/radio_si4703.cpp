// radio_si4703.cpp -- 见 radio_si4703.h
#include "radio_si4703.h"
#include "DEV_Config.h"
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
  // 分时复用:OLED CS 仍为高,空转一字节吸收 SPI 重挂后的首字节
  SPI4W_Prime();
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
  _rds.reset();  // 旧台的 RDS 内容作废

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
  // 上电前 CHIPID 只做“像不像 Si470x”的判断,不卡死 0x1200:
  // DEV 位段(9:6)bit3 置位=Si4703(8/9),清零=Si4702(0/1)。
  // 注意上电前的值不可靠:实测某模块上电前报 0x0800(DEV=0),
  // 上电后却是 0x0A50(DEV=9、FW=0x10),即老固件 Si4703,
  // 型号最终以上电后的 DEV 为准,见下面的二次判定。
  _isSi4703 = (_chipId & 0x0200) != 0;

  // 晶振:须在 ENABLE=1 之前使能(手册 §4.7/寄存器 07h)。
  // 上电前 bits13:0 保持 0x0100。
#if SI4703_USE_XTAL
  writeRegister(SI4703_REG_TEST1,
                SI4703_TEST1_XOSCEN | SI4703_TEST1_POWERDOWN);
#else
  writeRegister(SI4703_REG_TEST1, SI4703_TEST1_POWERDOWN);
#endif
#if SI4703_USE_XTAL
  // 冷启动关键:32.768kHz 晶振从零起振很慢(数百毫秒~1 秒以上),
  // 手册初始化流程(§4.11 图 9)要求先提供 RCLK 再置 ENABLE,
  // XOSCEN 置位后必须等起振稳定才能上电,否则上电/调谐失败。
  // 下载后重跑能工作,是因为模块一直带电、晶振没停过——1ms 的
  // RST 脉冲根本停不掉高 Q 晶振(衰减时间常数约 0.5 秒);重新
  // 上电则晶振从零起振,原来 XOSCEN 置位后几毫秒就上电,必然失败。
  delay(1000);
#endif

  // SYSCONFIG1:去加重 50us(中国),开 RDS,GPIO 高阻
  _shadow[SI4703_REG_SYSCONFIG1 - 0x02] =
      (uint16_t)(SI4703_CFG1_DE | SI4703_CFG1_RDS);
  writeRegister(SI4703_REG_SYSCONFIG1, _shadow[SI4703_REG_SYSCONFIG1 - 0x02]);
  // SYSCONFIG2:SEEKTH + 波段 87.5~108 + 步进 100kHz + 音量
  _volume = RADIO_DEFAULT_VOLUME;
  _shadow[SI4703_REG_SYSCONFIG2 - 0x02] =
      (uint16_t)((RADIO_SEEK_THRESHOLD << 8) | (0 << 6) | (1 << 4) | _volume);
  writeRegister(SI4703_REG_SYSCONFIG2, _shadow[SI4703_REG_SYSCONFIG2 - 0x02]);
  // SYSCONFIG3:默认 0
  _shadow[SI4703_REG_SYSCONFIG3 - 0x02] = 0x0000;
  writeRegister(SI4703_REG_SYSCONFIG3, 0x0000);

  // 上电:ENABLE=1 + DMUTE=1(取消静音)+ RDSM=1(RDS 详细模式,
  // 手册 §4.4:才有 RDSS 同步标志和各块 BLER 误码等级)。
  // 上电时间最长 110ms(手册表 8)。
  _muted = false;
  _shadow[SI4703_REG_POWERCFG - 0x02] =
      (uint16_t)(SI4703_PWR_DMUTE | SI4703_PWR_ENABLE | SI4703_PWR_RDSM);
  writeRegister(SI4703_REG_POWERCFG, _shadow[SI4703_REG_POWERCFG - 0x02]);
  delay(110);

  _chipId = readRegister(SI4703_REG_CHIPID);
  // 上电成功的标志:DEV 由 0/8 变为 1/9 且 FIRMWARE 非零。
  // 不卡死 0x1253(C19 Si4703 的值),Si4702 上电后是 DEV=1。
  {
    uint16_t dev = (uint16_t)((_chipId >> 6) & 0xF);
    uint16_t fw = (uint16_t)(_chipId & 0x3F);
    if (!((dev == 1 || dev == 9) && fw != 0)) {
      snprintf(_error, sizeof(_error),
               "powerup failed: CHIPID=0x%04X (DEV=%d FW=0x%02X)", _chipId,
               dev, fw);
      return false;
    }
    _isSi4703 = (dev == 9);
  }

  _freqMHz = RADIO_DEFAULT_FREQ_MHZ;
  bool tuned = tune(_freqMHz);
  if (!tuned) {
    // 兜底:晶振若还没完全稳,再等 500ms 重调一次
    delay(500);
    _freqMHz = RADIO_DEFAULT_FREQ_MHZ;
    tuned = tune(_freqMHz);
  }
  if (!tuned) {
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
  if (done) _rds.reset();  // 换了台,旧台的 RDS 内容作废
  return done;
}

bool Si4703Radio::seek(bool up) {
  uint16_t pwr = _shadow[SI4703_REG_POWERCFG - 0x02];
  pwr = (uint16_t)((pwr & (uint16_t)~SI4703_PWR_SEEKUP) |
                   (up ? SI4703_PWR_SEEKUP : 0) | SI4703_PWR_SEEK);
  _shadow[SI4703_REG_POWERCFG - 0x02] = pwr;
  writeRegister(SI4703_REG_POWERCFG, pwr);
  delay(150);  // quiet,见 waitStc 注释
  // 全波段 205 个频道 x 每频道最长 60ms ≈ 12.3s,超时须覆盖整圈,
  // 否则还没扫完就报失败;每按一次 KEY5 从当前频道继续扫。
  bool done = waitStc(12000, 100);
  // 清 SEEK 位以清除 STC/SF/BL
  pwr = (uint16_t)(_shadow[SI4703_REG_POWERCFG - 0x02] &
                   (uint16_t)~SI4703_PWR_SEEK);
  _shadow[SI4703_REG_POWERCFG - 0x02] = pwr;
  writeRegister(SI4703_REG_POWERCFG, pwr);
  readStatus();
  bool found = done && !_sfbl;
  if (found) _rds.reset();  // 换了台,旧台的 RDS 内容作废
  return found;
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

// ---------- RDS ----------
// 主循环每 50ms 调一次。RDS 组最快约 11 组/秒,RDSR 在详细模式
// 下同步后每组置位 ≥40ms,50ms 轮询基本不漏组;PS/RT 是循环播发
// 的,偶尔漏一组也会在 1~2 秒内补齐。
// 注意:读 0Ch~0Fh 会清除 RDSR;万一某固件不清,重复喂同一组
// 也是幂等的(同字符覆盖同位置),不会出错。
bool Si4703Radio::pollRds() {
  if (!_ok || !_isSi4703) return false;  // Si4702 无 RDS
  uint16_t st = readRegister(SI4703_REG_STATUSRSSI);
  bool synced = (st & SI4703_RDSS) != 0;
  if (!(st & SI4703_RDSR)) {
    return _rds.setSynced(synced);  // 无新组,只跟进同步状态
  }
  uint16_t rc = readRegister(SI4703_REG_READCHAN);
  uint16_t a = readRegister(SI4703_REG_RDSA);
  uint16_t b = readRegister(SI4703_REG_RDSB);
  uint16_t c = readRegister(SI4703_REG_RDSC);
  uint16_t d = readRegister(SI4703_REG_RDSD);
  return _rds.feed(a, b, c, d, (uint8_t)((st & SI4703_BLERA_MASK) >> 9),
                   (uint8_t)((rc & SI4703_BLERB_MASK) >> 14),
                   (uint8_t)((rc & SI4703_BLERC_MASK) >> 12),
                   (uint8_t)((rc & SI4703_BLERD_MASK) >> 10), synced);
}

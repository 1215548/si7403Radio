/******************************************************************************
**************************Hardware interface layer*****************************
  | file        : DEV_Config.cpp
  | version     : V1.0
  | date        : 2020-06-16
  | function    : Provide the hardware underlying interface
******************************************************************************/
#include "DEV_Config.h"

/********************************************************************************
  function: System Init and exit
  note:
  Initialize the communication method
********************************************************************************/
uint8_t System_Init(void)
{
  //set pin
  pinMode(OLED_CS, OUTPUT);
  pinMode(OLED_RST, OUTPUT);
  pinMode(OLED_DC, OUTPUT);
  OLED_CS_1; //  deselect OLED:后续 Si4703 在同一 SCLK/MOSI 上位-bang 时
             //  OLED 不会误收时钟

  //Serial 由主程序负责 begin,这里只打印(主程序先 begin 再调本函数)
  Serial.println("USE_SPI");

#if USE_SPI_4W
  //ESP32-C3 默认 SPI 引脚是 SCK 6/MOSI 7,与本工程引脚表不符,必须显式指定:
  //SCLK=IO4, MISO 不用(-1), MOSI=IO6, SS=IO7。
  //不用已废弃的 setDataMode/setClockDivider,每次传输用 SPISettings(见下)。
  SPI.begin(OLED_SCLK, -1, OLED_MOSI, OLED_CS);

#elif USE_IIC
  //set OLED I2C
  Serial.println("USE_I2C");
  OLED_DC_0;//DC = 1 => Address = 0x3d
  OLED_CS_0;
  Wire.setClock(1000000);
  Wire.begin(17,16);
#endif
  return 0;
}

/********************************************************************************
  function: Hardware interface
  note:
  SPI4W_Write_Byte(value) :
    hardware SPI
  I2C_Write_Byte(value, cmd):
    hardware I2C
********************************************************************************/
void SPI4W_Write_Byte(uint8_t DATA)
{
  //SSD1309 用 SPI MODE3,每次传输单独开事务:Si4703 驱动会在两次
  //OLED 传输之间 SPI.end()/SPI.begin() 借走同一组引脚,事务制可保证
  //每次恢复后时序/极性都重新确立。
  SPI.beginTransaction(SPISettings(OLED_SPI_CLOCK_HZ, MSBFIRST, SPI_MODE3));
  SPI.transfer(DATA);
  SPI.endTransaction();
}

void SPI4W_Write_Buffer(const uint8_t* data, size_t len) {
  if (len == 0) return;
  SPI.beginTransaction(SPISettings(OLED_SPI_CLOCK_HZ, MSBFIRST, SPI_MODE3));
  SPI.writeBytes(data, len);
  SPI.endTransaction();
}

void I2C_Write_Byte(uint8_t value, uint8_t Cmd)
{
  uint8_t Addr = 0x3c;
  Wire.beginTransmission(Addr);
  Wire.write(Cmd);
  Wire.write(value);
  Wire.endTransmission();
}

/********************************************************************************
  function: Delay function
  note:
  Driver_Delay_ms(xms) : Delay x ms
  Driver_Delay_us(xus) : Delay x us
********************************************************************************/
void Driver_Delay_ms(unsigned long xms)
{
  delay(xms);
}

void Driver_Delay_us(int xus)
{
  //原例程是空循环,编译器优化后延时不可靠,改用 delayMicroseconds。
  if (xus > 0) delayMicroseconds((unsigned int)xus);
}

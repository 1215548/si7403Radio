/******************************************************************************
**************************Hardware interface layer*****************************
* | file      	:	DEV_Config.h
* |	version		  :	V1.0
* | date		    :	2020-06-16
* | function	  :	Provide the hardware underlying interface	
******************************************************************************/
#ifndef _DEV_CONFIG_H_
#define _DEV_CONFIG_H_

#include <HardwareSerial.h>
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <pgmspace.h>

/**
 * data
**/
#define UBYTE   uint8_t
#define UWORD   uint16_t
#define UDOUBLE uint32_t

#define USE_SPI_4W  1
#define USE_IIC     0

#define IIC_CMD        0X00
#define IIC_RAM        0X40

/****************************************************************************************
    //Use the  library function definition
*****************************************************************************************/
//GPIO config -- 已按本工程引脚表(引脚对应.txt,ESP32-C3 Supermini)改写,
//原 Waveshare 例程是 ESP32-S3 引脚(CS 10/RST 3/DC 46),不可直接用。
//OLED SPI 与 Si4703 的 3-wire 共用 SCLK/MOSI,见 pins.h 与 radio_si4703.cpp
//的 busAcquire()/busRelease()。
#define OLED_CS 7
#define OLED_CS_0     digitalWrite(OLED_CS, LOW)
#define OLED_CS_1     digitalWrite(OLED_CS, HIGH)

#define OLED_RST 5
#define OLED_RST_0    digitalWrite(OLED_RST, LOW)
#define OLED_RST_1    digitalWrite(OLED_RST, HIGH)

#define OLED_DC 3
#define OLED_DC_0     digitalWrite(OLED_DC, LOW)
#define OLED_DC_1     digitalWrite(OLED_DC, HIGH)

// OLED SPI 引脚(ESP32-C3 非默认 SPI 引脚,必须显式指定,见 DEV_Config.cpp)
#define OLED_SCLK 4
#define OLED_MOSI 6
#define OLED_SPI_CLOCK_HZ 8000000UL

/*------------------------------------------------------------------------------------------------------*/
uint8_t System_Init(void);

void SPI4W_Write_Byte(uint8_t DATA);
void I2C_Write_Byte(uint8_t value, uint8_t Cmd);

void Driver_Delay_ms(unsigned long xms);
void Driver_Delay_us(int xus);

#endif


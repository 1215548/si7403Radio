/*****************************************************************************
* | File        :   OLED_Driver.cpp
* | Author      :   Waveshare team
* | Function    :   1.3inch OLED Drive function
* | Info        :
*----------------
* | This version:   V1.0
* | Date        :   2020-08-20
* | Info        :
* -----------------------------------------------------------------------------
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documnetation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to  whom the Software is
# furished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#
******************************************************************************/
#include "OLED_Driver.h"
#include <stdio.h>
#include <string.h>


/*******************************************************************************
function:
            Hardware reset
*******************************************************************************/
static void OLED_Reset(void)
{
    OLED_RST_1;
    Driver_Delay_ms(100);
    OLED_RST_0;
    Driver_Delay_ms(100);
    OLED_RST_1;
    Driver_Delay_ms(100);
}

/*******************************************************************************
function:
            Write register address and data
*******************************************************************************/
static void OLED_WriteReg(uint8_t Reg)
{

#if USE_SPI_4W
    OLED_DC_0;
    OLED_CS_0;
    SPI4W_Write_Byte(Reg);
    OLED_CS_1;
#elif USE_IIC
    I2C_Write_Byte(Reg,IIC_CMD);
#endif
}

static void OLED_WriteData(uint8_t Data)
{
    
#if USE_SPI_4W
    OLED_DC_1;
    OLED_CS_0;
    SPI4W_Write_Byte(Data);
    OLED_CS_1;
#elif USE_IIC
    I2C_Write_Byte(Data,IIC_RAM);
#endif
}

// 整页突发写:DC=1/CS 保持,一次事务发 128 字节(见 DEV_Config.h 注释)
static void OLED_WriteData_Buffer(const uint8_t* data, size_t len) {
#if USE_SPI_4W
  OLED_DC_1;
  OLED_CS_0;
  SPI4W_Write_Buffer(data, len);
  OLED_CS_1;
#elif USE_IIC
  for (size_t i = 0; i < len; i++) I2C_Write_Byte(data[i], IIC_RAM);
#endif
}

/*******************************************************************************
function:
		Common register initialization
*******************************************************************************/
static void OLED_InitReg(void)
{
    OLED_WriteReg(0xAE);//--turn off oled panel

    OLED_WriteReg(0x00);//---set low column address
    OLED_WriteReg(0x10);//---set high column address
    
    OLED_WriteReg(0x20);
    OLED_WriteReg(0x00);
        
    OLED_WriteReg(0xFF);
    
    OLED_WriteReg(0xA6);
    
    OLED_WriteReg(0xA8); 
    OLED_WriteReg(0x3F);
    
    OLED_WriteReg(0xD3);
    OLED_WriteReg(0x00);
    
    OLED_WriteReg(0xD5);
    OLED_WriteReg(0x80);
    
    OLED_WriteReg(0xD9);
    OLED_WriteReg(0x22);

    OLED_WriteReg(0xDA);
    OLED_WriteReg(0x12);
    
    OLED_WriteReg(0xDB);
    OLED_WriteReg(0x40);
    

}

/********************************************************************************
function:
            initialization
********************************************************************************/
void OLED_2IN42_Init()
{
    //Hardware reset
    OLED_Reset();

    //Set the initialization register
    OLED_InitReg();
    Driver_Delay_ms(200);

    //Turn on the OLED display
    OLED_WriteReg(0xAF);
}

/********************************************************************************
function:
			Clear screen
********************************************************************************/
void OLED_2IN42_Clear()
{
    // 显示 RAM 被直接清零,影子缓冲随之失效,下次局部刷新改全屏重发
    OLED_2IN42_InvalidateShadow();
    // UWORD Width, Height;
    UWORD i, j;
    // Width = (OLED_1IN3_WIDTH % 8 == 0)? (OLED_1IN3_WIDTH / 8 ): (OLED_1IN3_WIDTH / 8 + 1);
    // Height = OLED_1IN3_HEIGHT; 
    for (i=0; i<8; i++) {
        /* set page address */
        OLED_WriteReg(0xB0 + i);
        /* set low column address */
        OLED_WriteReg(0x00);
        /* set high column address */
        OLED_WriteReg(0x10);
        for(j=0; j<128; j++) {
            /* write data */
            OLED_WriteData(0x00);  
        }

    }
}

/********************************************************************************
function:	Update memory to OLED
********************************************************************************/
void OLED_2IN42_Display(const UBYTE *Image)
{
    UWORD page, column, temp;

    for (page=0; page<8; page++) {
        /* set page address */
        OLED_WriteReg(0xB0 + page);
        /* set low column address */
        OLED_WriteReg(0x00);
        /* set high column address */
        OLED_WriteReg(0x10);

        /* write data */
        for(column=0; column<128; column++) {
            temp = Image[(7-page) + column*8];
            OLED_WriteData(temp);
        }       
    }
}

/********************************************************************************
function:  Update array data to OLED
********************************************************************************/
void OLED_2IN42_Display_Array(const UBYTE *Image)
{
    UWORD page, column, temp;

    for (page=0; page<8; page++) {
        /* set page address */
        OLED_WriteReg(0xB0 + page);
        /* set low column address */
        OLED_WriteReg(0x00);
        /* set high column address */
        OLED_WriteReg(0x10);

        /* write data */
        for(column=0; column<128; column++) {
            temp = pgm_read_byte(&Image[(7-page) + column*8]);
            OLED_WriteData(temp);
        }       
    }
}

/********************************************************************************
局部刷新(防频闪):
  全屏重发 OLED_2IN42_Display() 每字节一个 SPI 事务,1024 次、数毫秒
  逐页滚动更新,每秒一次肉眼可见。这里留一份上次发出的影子缓冲,
  只发内容变了的页;一页 128 字节一次突发事务。配合 oled_display.cpp
  的帧哈希(内容完全没变时连驱动都不调),静止画面零 SPI 流量。
  显存布局与 Display() 相同:页内第 column 列字节在 Image[(7-page)+column*8]。
********************************************************************************/
static UBYTE s_shadow[1024];
static bool s_shadowValid = false;

void OLED_2IN42_InvalidateShadow(void) {
  s_shadowValid = false;
}

void OLED_2IN42_Display_Partial(const UBYTE *Image) {
  if (!s_shadowValid) {
    OLED_2IN42_Display(Image);
    memcpy(s_shadow, Image, sizeof(s_shadow));
    s_shadowValid = true;
    return;
  }
  UBYTE pageBuf[128];
  for (UWORD page = 0; page < 8; page++) {
    bool dirty = false;
    for (UWORD column = 0; column < 128; column++) {
      if (Image[(7 - page) + column * 8] !=
          s_shadow[(7 - page) + column * 8]) {
        dirty = true;
        break;
      }
    }
    if (!dirty) continue;
    for (UWORD column = 0; column < 128; column++) {
      pageBuf[column] = Image[(7 - page) + column * 8];
      s_shadow[(7 - page) + column * 8] = pageBuf[column];
    }
    OLED_WriteReg(0xB0 + page);
    OLED_WriteReg(0x00);
    OLED_WriteReg(0x10);
    OLED_WriteData_Buffer(pageBuf, sizeof(pageBuf));
  }
}

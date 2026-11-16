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

    // 寻址模式:0x02=页寻址(不要改回 0x00 横向寻址)。局部刷新依赖
    // 0xB0 页地址命令随机写某一页;横向模式下 0xB0/0x00/0x10 命令
    // 全部被忽略,数据按顺序流写到当前指针处——改了哪页就错写到哪页,
    // 表现为同一行内容在多个纵向位置重复出现并每秒闪动(2026-11-16
    // 实机照片:信号条在 4 个位置重复)。全屏流写在两种模式下都对,
    // 所以旧版全屏刷新看不出问题,局部刷新一上就暴露。
    OLED_WriteReg(0x20);
    OLED_WriteReg(0x02);
        
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
note: 逐页突发写:寻址/顺序与原版逐字节完全一致(先定页+列地址再发
  128 字节),只是每页一次事务而不是 128 次——全屏约 1ms。
  这样写在页寻址和横向寻址两种模式下都对(横向模式下 0xB0 命令
  被忽略、数据按顺序流写,正好与发送顺序一致),不依赖寻址模式。
  局部刷新(只发变化的页)曾因横向模式下 0xB0 被忽略而错页,已删掉。
********************************************************************************/
void OLED_2IN42_Display(const UBYTE *Image)
{
    UBYTE pageBuf[128];
    for (UWORD page=0; page<8; page++) {
        /* set page address */
        OLED_WriteReg(0xB0 + page);
        /* set low column address */
        OLED_WriteReg(0x00);
        /* set high column address */
        OLED_WriteReg(0x10);

        /* gather page data then burst it in one transaction */
        for (UWORD column=0; column<128; column++) {
            pageBuf[column] = Image[(7-page) + column*8];
        }
        OLED_WriteData_Buffer(pageBuf, sizeof(pageBuf));
    }
}

/********************************************************************************
function:	只刷新指定的显示页(局部刷新,频谱动画用)
note: 先强制页寻址模式(0x20/0x02),再逐页定页+列地址后突发写 128 字节。
  寻址/顺序与 OLED_2IN42_Display 完全一致,只是只发 page0..page1。
  局部刷新依赖 0xB0 页地址命令,横向寻址下该命令被忽略、会把页写到
  错的位置(v0.3.0 信号条多处重复即此原因),所以每次先强制页模式。
  底部频谱在 y=52..63,即第 6/7 页。
********************************************************************************/
void OLED_2IN42_RefreshPages(const UBYTE *Image, UBYTE page0, UBYTE page1)
{
    UBYTE pageBuf[128];
    OLED_WriteReg(0x20);
    OLED_WriteReg(0x02);
    for (UWORD page=page0; page<=page1 && page<8; page++) {
        OLED_WriteReg(0xB0 + page);
        OLED_WriteReg(0x00);
        OLED_WriteReg(0x10);
        for (UWORD column=0; column<128; column++) {
            pageBuf[column] = Image[(7-page) + column*8];
        }
        OLED_WriteData_Buffer(pageBuf, sizeof(pageBuf));
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

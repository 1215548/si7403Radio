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

    // 寻址模式:0x02=页寻址。SSD1309 手册表 9-3 明确:命令和它的
    // 参数都用 D/C#=0 送,只有写 GDDRAM 才用 D/C#=1。所以下面
    // 0x20/0x02 都走 OLED_WriteReg 是正确的(v0.3.7 曾把参数改成
    // DC=1,反而破坏初始化,已回退)。
    // 局部刷新失效的真正原因不是寻址模式,而是页序:画布旋转 270°,
    // 显存里逻辑页 lp 对应 GDDRAM 物理页 7-lp。旧代码刷物理页 6/7,
    // 刷的其实是顶部逻辑页 1/0,底部柱子当然一直不动。
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
function:	Set page + column start address (page addressing mode)
********************************************************************************/
static void OLED_SetPageAddress(UWORD page)
{
    OLED_WriteReg(0xB0 + page);
    OLED_WriteReg(0x00);
    OLED_WriteReg(0x10);
}

/********************************************************************************
function:	Update memory to OLED
note: 逐页突发写:先定页+列地址再发 128 字节,每页一次事务而不是
  128 次——全屏约 1ms。
  显存是旋转 270° 后的布局:物理页 page 的数据在 Image[(7-page)
  + column*8],与 GUI_Paint 的 270° 寻址一致。局部刷新必须沿用
  同一个换算,否则页号会刷错地方。
********************************************************************************/
void OLED_2IN42_Display(const UBYTE *Image)
{
    UBYTE pageBuf[128];
    for (UWORD page=0; page<8; page++) {
        OLED_SetPageAddress(page);

        /* gather page data then burst it in one transaction */
        for (UWORD column=0; column<128; column++) {
            pageBuf[column] = Image[(7-page) + column*8];
        }
        OLED_WriteData_Buffer(pageBuf, sizeof(pageBuf));
    }
}

/********************************************************************************
function:	Refresh selected GDDRAM pages only
note: page0..page1 是物理页号。先显式设页寻址(datasheet 表 9-3:
  命令参数均 DC=0),再逐页定页+列地址后突发写,与全屏逐页写法
  完全一致。底部逻辑页 6/7 => 物理页 1/0,见 OledDisplay::refreshRows。
********************************************************************************/
void OLED_2IN42_RefreshPages(const UBYTE *Image, UBYTE page0, UBYTE page1)
{
    UBYTE pageBuf[128];
    if (page0 > page1 || page1 > 7) return;
    OLED_WriteReg(0x20);
    OLED_WriteReg(0x02);
    for (UWORD page=page0; page<=page1; page++) {
        OLED_SetPageAddress(page);
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

/*****************************************************************************
* | File      	:   OLED_Driver.h
* | Author      :   Waveshare team
* | Function    :   1.54inch OLED Drive function
* | Info        :
*----------------
* |	This version:   V1.0
* | Date        :   2022-07-14
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
#ifndef __OLED_DRIVER_H
#define __OLED_DRIVER_H		

#include "DEV_Config.h"

/********************************************************************************
function:	
		Define the full screen height length of the display
********************************************************************************/
#define OLED_2IN42_WIDTH  64//OLED width
#define OLED_2IN42_HEIGHT 128 //OLED height

/********************************************************************************
function:	
			Macro definition variable name
********************************************************************************/
void OLED_2IN42_Init(void);
void OLED_2IN42_Clear(void);
void OLED_2IN42_Display(const UBYTE *Image);
void OLED_2IN42_Display_Array(const UBYTE *Image);
// 局部刷新:只重发 page0..page1 两个物理页(每页 128 字节)。
// 注意 page 是 GDDRAM 物理页号,不是逻辑画布的页号——本工程画布
// 旋转 270°,逻辑页 lp 的数据在物理页 7-lp,所以底部逻辑页 6/7
// 对应物理页 1/0。调用方请走 OledDisplay::refreshRows(),
// 按逻辑行号传参,不要直接猜页号(历史 bug 就出在这里)。
void OLED_2IN42_RefreshPages(const UBYTE *Image, UBYTE page0, UBYTE page1);

#endif  
	 

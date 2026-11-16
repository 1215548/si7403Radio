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
// 只发变化的页(与上次发出的内容比对,见 OLED_Driver.cpp):
// 全屏重发是频闪的主因——逐字节事务 1024 次、数毫秒逐页滚动更新,
// 每秒一次肉眼可见。局部刷新后 1 秒一跳只动 0~2 页,且每页一次
// 突发事务,无全屏滚动。
void OLED_2IN42_Display_Partial(const UBYTE *Image);
// 显示内容被 Clear() 等旁路改写后调用,下次局部刷新改全屏重发。
void OLED_2IN42_InvalidateShadow(void);

#endif  
	 

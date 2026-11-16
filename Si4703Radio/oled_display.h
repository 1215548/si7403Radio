// oled_display.h -- OLED 显示模块(SSD1309 128x64,单色黄)
// 底层是仓库原有的 Waveshare 驱动(DEV_Config/OLED_Driver/GUI_Paint,
// 已按本工程引脚改过,见 DEV_Config.h);本模块只负责“画什么”:
// 收音主界面、通用两行消息、测试图。逻辑画布 128x64(旋转 270°)。
#pragma once
#include <Arduino.h>

class OledDisplay {
public:
  // 初始化 SPI + 复位 + 显存。写-only SPI 无法回读,只要代码跑到
  // 这里就返回 true;屏亮不亮要看硬件(电源/复位/接线),见 README。
  bool begin();
  void clear();
  // 收音主界面:大字体频率 + RSSI/立体声/音量 + 按键提示
  void showRadio(float freqMHz, int rssi, bool stereo, int volume,
                 bool muted, bool radioOk, const char* msg);
  // 通用两行消息(启动画面、搜台中、按键反馈等)
  void showMessage(const char* line1, const char* line2);
  // 阶段一自检图:几何图形 + 字符,确认屏/字库/旋转都正常
  void showTestPattern();

private:
  void flush();  // 把显存刷到屏上
};

// idle_motion.h — Stack-Chan K151 の首をランダムに小さく動かす (StackChan-BSP 利用版)
//
// 前提: setup() で M5StackChan.begin() を呼び終わっていること(BSP が
// PY32 経由 VM EN ON / SCS バス init / Motion クラス構築を済ませる)。
//
// 使い方:
//   idle_motion::init();           // setup() 末尾で 1 回
//   idle_motion::tick(millis());   // loop() で毎回
//   idle_motion::set_enabled(false); ... set_enabled(true);  // 発話などで一時停止
//
// tick() は非ブロッキング。実際の補間は BSP の Motion クラス内 FreeRTOS task が担う。

#pragma once

#include <stdint.h>

namespace idle_motion {

void init();
void tick(uint32_t now_ms);
void set_enabled(bool enabled);

}  // namespace idle_motion

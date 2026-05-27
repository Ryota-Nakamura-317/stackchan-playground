// pet_reaction.h — 頭頂部を撫でられたら喜ぶ
//
// 前提:
//   - M5StackChan.begin() / avatar.init() / idle_motion::init() / sleep_manager::init() が終わっていること
//   - loop() の冒頭で M5StackChan.update() を呼んでおくこと (TouchSensor の更新)
//   - loop() で tick() を毎回呼ぶこと
//
// 検出: BSP の TouchSensor.wasSwipedForward/Backward() のみ (単発タップは無視)
// 反応: Expression::Happy + 全 RGB LED を暖色点灯。kHappyDurationMs (≒3s) 後に解除
// スリープ中: 検出時に sleep_manager::notify_activity() を呼んで AWAKE 復帰 → 喜びを再生

#pragma once

#include <stdint.h>

namespace m5avatar { class Avatar; }

namespace pet_reaction {

void init(m5avatar::Avatar& avatar);
void tick(uint32_t now_ms);

}  // namespace pet_reaction

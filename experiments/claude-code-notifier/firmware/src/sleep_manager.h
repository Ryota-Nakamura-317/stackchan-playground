// sleep_manager.h — アイドル時間を計測し Sleepy → Sleeping → powerOff に進める
//
// 前提:
//   - M5StackChan.begin() / avatar.init() / idle_motion::init() が終わっていること
//   - loop() で tick() を毎回呼ぶこと
//   - /speak ハンドラの冒頭 (JSON パース成功直後) で notify_activity() を呼ぶこと
//
// 状態遷移:
//   AWAKE → (30 分 idle) → SLEEPY → (+10 秒) → SLEEPING → (+30 秒) → POWERING_OFF
//   非 AWAKE で notify_activity() が来れば即 AWAKE に復帰する。

#pragma once

#include <stdint.h>

namespace m5avatar { class Avatar; }

namespace sleep_manager {

void init(m5avatar::Avatar& avatar);
void tick(uint32_t now_ms);
void notify_activity();

}  // namespace sleep_manager

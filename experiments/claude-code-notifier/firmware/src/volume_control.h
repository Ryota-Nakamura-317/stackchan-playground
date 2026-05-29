// volume_control.h — LCD 長押しで全画面の音量調整 UI を出す
//
// 前提:
//   - M5StackChan.begin() / avatar.init() が終わっていること
//   - loop() の冒頭で M5StackChan.update() を呼んでおくこと (M5.Touch の更新)
//   - loop() で tick() を毎回呼ぶこと
//   - 音量は NVS (BSP の Settings, namespace="stackchan" / key="spk_vol") に永続化。
//     init() で復元 → M5.Speaker.setVolume() を適用する。
//
// 操作:
//   - IDLE 中に LCD を長押し (wasHold) すると UI を開く。
//   - UI 表示中は Avatar の描画タスクを stop() し、M5.Display へ直接 UI を描く。
//     − / ＋ ボタンのタップで音量を増減 (即 setVolume で反映)、Close で閉じる。
//     無操作が続けば自動で閉じる。
//   - 閉じるときに NVS へ 1 回だけ書き込み、avatar.start() で顔を復帰する。
//
// is_active(): UI 表示中は true。main 側で他モジュールの tick / 発話を抑止するのに使う
// (UI 表示中は描画タスクが止まっており、setExpression() の vTaskSuspend が
//  不正ハンドルを触るのを防ぐため)。

#pragma once

#include <stdint.h>

namespace m5avatar { class Avatar; }

namespace volume_control {

void init(m5avatar::Avatar& avatar);
void tick(uint32_t now_ms);
bool is_active();
uint8_t get();

}  // namespace volume_control

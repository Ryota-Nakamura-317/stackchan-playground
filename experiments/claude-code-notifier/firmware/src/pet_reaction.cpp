// pet_reaction.cpp — 頭頂部の前後スワイプを検出して Happy 表情 + LED 点灯
//
// 設計要点:
//   - 非ブロッキング tick (millis() ベース)。idle_motion / sleep_manager と同じ流儀。
//   - 単発タッチ (wasClicked) は意図的に拾わない。「撫でた感」を出すため swipe のみ。
//   - HAPPY 中に再度スワイプを検知したらタイマを延長する (連続して撫でられたら喜び続ける)。
//   - スリープ復帰は sleep_manager::notify_activity() に一任。pet_reaction 側で
//     AWAKE state を直接書き換えない。idle_motion の停止/再開も既存 set_enabled() に従う。

#include "pet_reaction.h"

#include <Arduino.h>
#include <M5StackChan.h>
#include <Avatar.h>

#include "idle_motion.h"
#include "sleep_manager.h"

using m5avatar::Avatar;
using m5avatar::Expression;

namespace pet_reaction {

namespace {

// ─── 喜びリアクションのチューニング ─────────────────────────
constexpr uint32_t kHappyDurationMs = 3000;
// LED 暖色ピンク。WS2812C は GRB ではなく BSP が r,g,b 引数で吸収してくれる。
constexpr uint8_t kHappyR = 255;
constexpr uint8_t kHappyG = 80;
constexpr uint8_t kHappyB = 120;

enum class State : uint8_t { IDLE, HAPPY };

Avatar*  s_avatar           = nullptr;
State    s_state            = State::IDLE;
uint32_t s_state_entered_ms = 0;

bool elapsed(uint32_t now_ms, uint32_t since_ms, uint32_t threshold_ms) {
  return static_cast<int32_t>(now_ms - (since_ms + threshold_ms)) >= 0;
}

void enter_happy(uint32_t now_ms) {
  // スリープ中だった場合の復帰を先に任せる。notify_activity() は AWAKE 復帰時に
  // Neutral 表情と idle_motion の再開まで自前で行うので、Happy の上書きはこの後に行う。
  sleep_manager::notify_activity();

  idle_motion::set_enabled(false);
  if (s_avatar) {
    s_avatar->setExpression(Expression::Happy);
  }
  M5StackChan.showRgbColor(kHappyR, kHappyG, kHappyB);

  s_state = State::HAPPY;
  s_state_entered_ms = now_ms;
  Serial.println("[pet] swipe -> HAPPY");
}

void exit_happy() {
  M5StackChan.showRgbColor(0, 0, 0);
  if (s_avatar) {
    s_avatar->setExpression(Expression::Neutral);
  }
  idle_motion::set_enabled(true);
  s_state = State::IDLE;
  Serial.println("[pet] HAPPY -> IDLE");
}

}  // namespace

void init(Avatar& avatar) {
  s_avatar = &avatar;
  s_state = State::IDLE;
  Serial.println("[pet] init ok");
}

void tick(uint32_t now_ms) {
  auto& ts = M5StackChan.TouchSensor;
  const bool swiped = ts.wasSwipedForward() || ts.wasSwipedBackward();

  switch (s_state) {
    case State::IDLE:
      if (swiped) {
        enter_happy(now_ms);
      }
      break;
    case State::HAPPY:
      if (swiped) {
        // 連続して撫でられている → タイマだけ延長
        s_state_entered_ms = now_ms;
      } else if (elapsed(now_ms, s_state_entered_ms, kHappyDurationMs)) {
        exit_happy();
      }
      break;
  }
}

}  // namespace pet_reaction

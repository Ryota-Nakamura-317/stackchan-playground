// sleep_manager.cpp — アイドル自動スリープ → 電源オフの状態機械
//
// 設計要点:
//   - 非ブロッキング tick (millis() ベース)。idle_motion と同じパターン。
//   - 全ての時間比較は (int32_t)(now - target) >= 0 形式で millis() の
//     49 日ロールオーバを安全に超えられるようにする。
//   - notify_activity() は AWAKE 中なら last_activity_ms 更新のみ。非 AWAKE のとき
//     のみ Avatar/idle_motion を復帰させて handleSpeak() の続きと競合しないようにする。

#include "sleep_manager.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <M5StackChan.h>
#include <Avatar.h>

#include "idle_motion.h"

using m5avatar::Avatar;
using m5avatar::Expression;

namespace sleep_manager {

namespace {

constexpr uint32_t kIdleSleepyAfterMs    = 5UL * 60UL * 1000UL;   // 5 min
constexpr uint32_t kSleepyToSleepingMs   = 10UL * 1000UL;         // 10 s
constexpr uint32_t kSleepingToPowerOffMs = 30UL * 1000UL;         // 30 s

// BSP Motion 単位は度×10。yaw 0=正面、pitch 0..900。
constexpr int kSleepYaw   = 0;
constexpr int kSleepPitch = 100;  // 10° うつむき
constexpr int kSleepSpeed = 150;  // ゆっくり

enum class State : uint8_t { AWAKE, SLEEPY, SLEEPING, POWERING_OFF };

Avatar*  s_avatar            = nullptr;
State    s_state             = State::AWAKE;
uint32_t s_last_activity_ms  = 0;
uint32_t s_state_entered_ms  = 0;

bool elapsed(uint32_t now_ms, uint32_t since_ms, uint32_t threshold_ms) {
  return static_cast<int32_t>(now_ms - (since_ms + threshold_ms)) >= 0;
}

void enter_sleepy(uint32_t now_ms) {
  idle_motion::set_enabled(false);
  if (s_avatar) {
    s_avatar->setExpression(Expression::Sleepy);
    s_avatar->setSpeechText("");
  }
  s_state = State::SLEEPY;
  s_state_entered_ms = now_ms;
  Serial.println("[sleep] AWAKE -> SLEEPY");
}

void enter_sleeping(uint32_t now_ms) {
  if (s_avatar) {
    s_avatar->setSpeechText("Zzz...");
  }
  M5StackChan.Motion.move(kSleepYaw, kSleepPitch, kSleepSpeed);
  s_state = State::SLEEPING;
  s_state_entered_ms = now_ms;
  Serial.println("[sleep] SLEEPY -> SLEEPING");
}

void enter_powering_off() {
  Serial.println("[sleep] SLEEPING -> POWERING_OFF (USB 給電中は無視される場合あり)");
  s_state = State::POWERING_OFF;
  M5.Power.powerOff();
}

}  // namespace

void init(Avatar& avatar) {
  s_avatar = &avatar;
  s_state = State::AWAKE;
  s_last_activity_ms = millis();
  Serial.println("[sleep] init ok");
}

void tick(uint32_t now_ms) {
  switch (s_state) {
    case State::AWAKE:
      if (elapsed(now_ms, s_last_activity_ms, kIdleSleepyAfterMs)) {
        enter_sleepy(now_ms);
      }
      break;
    case State::SLEEPY:
      if (elapsed(now_ms, s_state_entered_ms, kSleepyToSleepingMs)) {
        enter_sleeping(now_ms);
      }
      break;
    case State::SLEEPING:
      if (elapsed(now_ms, s_state_entered_ms, kSleepingToPowerOffMs)) {
        enter_powering_off();
      }
      break;
    case State::POWERING_OFF:
      // powerOff が無視された場合は何もしない (再呼出ししない)。
      break;
  }
}

void notify_activity() {
  const uint32_t now_ms = millis();
  if (s_state != State::AWAKE) {
    if (s_avatar) {
      s_avatar->setExpression(Expression::Neutral);
      s_avatar->setSpeechText("");
    }
    idle_motion::set_enabled(true);
    Serial.println("[sleep] notify_activity -> AWAKE");
    s_state = State::AWAKE;
  }
  s_last_activity_ms = now_ms;
}

}  // namespace sleep_manager

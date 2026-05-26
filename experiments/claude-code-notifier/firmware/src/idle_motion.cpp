// idle_motion.cpp — 待機中にゆっくりランダムにきょろきょろする
//
// StackChan-BSP (https://github.com/m5stack/StackChan-BSP) の M5StackChan.Motion
// API を使用。サーボ電源(PY32 経由 VM EN)や SCS バス初期化は M5StackChan.begin()
// が済ませているので、ここでは Motion.move() を周期的に呼ぶだけ。
//
// 振幅・周期はこのファイル冒頭の定数で調整可能。

#include "idle_motion.h"

#include <Arduino.h>
#include <M5StackChan.h>

namespace idle_motion {

namespace {

// ─── きょろきょろチューニング ─────────────────────────────
// 単位は度×10 (BSP 仕様)。yaw: -1280〜1280 (=-128°〜128°)、pitch: 0〜900 (=0°〜90°)。
constexpr int kYawAmplitude   = 200;   // ±20°
constexpr int kPitchCenter    = 350;   // 35° (少し上向きをニュートラルに)
constexpr int kPitchAmplitude = 100;   // ±10°
constexpr int kSpeedMin       = 200;   // 0〜1000。小さいほどゆっくり
constexpr int kSpeedMax       = 400;
constexpr uint32_t kPauseMinMs    = 2000;  // 次の動きまでの待ち
constexpr uint32_t kPauseMaxMs    = 5000;
constexpr uint32_t kStartupDelayMs = 2500; // init から最初の抽選までの間

bool s_enabled = true;
uint32_t s_next_move_at_ms = 0;

int random_between(int lo, int hi_inclusive) {
  if (hi_inclusive <= lo) return lo;
  return lo + static_cast<int>(random(hi_inclusive - lo + 1));
}

}  // namespace

void init() {
  // BSP の Motion は補間アニメーションの開始点を毎回現在角度に同期するのが
  // デフォルト。idle motion は滑らかな連続動作にしたいので OFF に倒す。
  M5StackChan.Motion.setAutoAngleSyncEnabled(false);
  s_enabled = true;
  s_next_move_at_ms = millis() + kStartupDelayMs;
  Serial.println("[idle_motion] init ok (BSP)");
}

void tick(uint32_t now_ms) {
  if (!s_enabled) return;
  if (static_cast<int32_t>(now_ms - s_next_move_at_ms) < 0) return;

  int yaw   = random_between(-kYawAmplitude, kYawAmplitude);
  int pitch = kPitchCenter + random_between(-kPitchAmplitude, kPitchAmplitude);
  int speed = random_between(kSpeedMin, kSpeedMax);

  M5StackChan.Motion.move(yaw, pitch, speed);
  s_next_move_at_ms = now_ms + static_cast<uint32_t>(random_between(
                                  static_cast<int>(kPauseMinMs),
                                  static_cast<int>(kPauseMaxMs)));
}

void set_enabled(bool enabled) {
  s_enabled = enabled;
}

}  // namespace idle_motion

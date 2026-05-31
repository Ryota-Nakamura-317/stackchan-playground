// volume_control.cpp — LCD 長押しで全画面の音量調整 UI を出す
//
// 設計要点:
//   - 非ブロッキング tick (millis() ベース)。idle_motion / sleep_manager と同じ流儀。
//   - UI を開くときに avatar.stop() で描画タスクを終了させ、M5.Display を独占して
//     UI を直接描く。閉じるときに avatar.start() でタスクを作り直し顔へ復帰する。
//     (Avatar.cpp: stop() は _isDrawing=false → drawLoop/facialLoop が while を抜けて
//      自タスク終了。start() で再生成。)
//   - 音量は変更のたびに setVolume() で即時反映するが、NVS には書かない。閉じる瞬間に
//     1 回だけ Settings へ書く (フラッシュ摩耗回避)。
//   - is_active() が true の間、main 側は他モジュールの tick / 発話を止める約束。
//     描画タスクが居ない状態で setExpression() (内部で vTaskSuspend) を呼ばせないため。

#include "volume_control.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <Avatar.h>

#include "utils/settings/settings.h"

using m5avatar::Avatar;

namespace volume_control {

namespace {

// ─── 音量レンジ (M5.Speaker は 0-255。0 は無音なので下限を設ける) ───
constexpr uint8_t kVolMin     = 40;
constexpr uint8_t kVolMax     = 255;
constexpr uint8_t kVolStep    = 24;   // 約 9 段階
constexpr int32_t kVolDefault = 200;  // 旧ハードコード値と同じ

// ─── 永続化 (BSP Settings = NVS)。サーボの "servo" namespace とは分離 ───
constexpr char kNvsNamespace[] = "stackchan";
constexpr char kNvsKey[]       = "spk_vol";  // NVS キーは 15 文字以内

// ─── UI レイアウト (320x240) ─────────────────────────
constexpr int kBarX = 40, kBarY = 104, kBarW = 240, kBarH = 22;
constexpr int kBtnY = 162, kBtnH = 56, kBtnW = 70;
constexpr int kMinusX = 24;
constexpr int kCloseX = 124, kCloseW = 72;
constexpr int kPlusX  = 226;

// 右上のバッテリーアイコン (本体 + 右端の端子)。タイトル "VOLUME" (y=22) と被らない上端へ。
constexpr int kBatX = 282, kBatY = 8, kBatW = 28, kBatH = 14;
constexpr int kBatTipW = 3, kBatTipH = 6;

// 無操作が続いたら自動で閉じる
constexpr uint32_t kAutoCloseMs = 8000;
// stop() 後に残フレームが M5.Display を触り終えるのを待つ (drawLoop は 10ms 周期)
constexpr uint32_t kDrawDrainMs = 25;

enum class State : uint8_t { IDLE, SHOWING };

Avatar*  s_avatar        = nullptr;
State    s_state         = State::IDLE;
uint8_t  s_volume        = static_cast<uint8_t>(kVolDefault);
uint32_t s_last_touch_ms = 0;

bool elapsed(uint32_t now_ms, uint32_t since_ms, uint32_t threshold_ms) {
  return static_cast<int32_t>(now_ms - (since_ms + threshold_ms)) >= 0;
}

bool in_rect(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

void load_volume() {
  Settings s(kNvsNamespace, false);
  int32_t v = s.GetInt(kNvsKey, kVolDefault);
  if (v < kVolMin) v = kVolMin;
  if (v > kVolMax) v = kVolMax;
  s_volume = static_cast<uint8_t>(v);
}

void save_volume() {
  Settings s(kNvsNamespace, true);
  s.SetInt(kNvsKey, s_volume);
}  // スコープ末尾でデストラクタ → dirty 時のみ nvs_commit

void draw_button(int x, int label_pos_y, const char* label, uint16_t fill_c) {
  auto& d = M5.Display;
  const uint16_t white = d.color565(235, 240, 245);
  d.fillRoundRect(x, kBtnY, kBtnW, kBtnH, 10, fill_c);
  d.setTextColor(white, fill_c);
  d.setTextSize(2);
  d.setTextDatum(textdatum_t::middle_center);
  d.drawString(label, x + kBtnW / 2, label_pos_y);
}

// 右上に小さくバッテリー残量を描く。残量% テキスト + アイコン + 充電中の稲妻。
// draw_ui() からのみ呼ぶ (SHOWING 中は Avatar 描画タスクが停止しており Display を独占)。
void draw_battery() {
  auto& d = M5.Display;
  const uint16_t bg     = d.color565(12, 16, 28);
  const uint16_t white  = d.color565(235, 240, 245);
  const uint16_t panel  = d.color565(40, 48, 70);
  const uint16_t green  = d.color565(120, 220, 130);
  const uint16_t yellow = d.color565(240, 210, 90);
  const uint16_t red    = d.color565(235, 110, 90);
  const uint16_t bolt   = d.color565(255, 230, 120);

  const int lvl = M5.Power.getBatteryLevel();          // 0-100。不明時は負値
  const bool charging = (M5.Power.isCharging() == M5.Power.is_charging);

  // ─── アイコン枠 + 端子 ───
  d.drawRoundRect(kBatX, kBatY, kBatW, kBatH, 2, white);
  d.fillRect(kBatX + kBatW, kBatY + (kBatH - kBatTipH) / 2, kBatTipW, kBatTipH, white);

  // ─── 残量% テキスト (アイコン左に右詰め) ───
  char buf[8];
  if (lvl < 0 || lvl > 100) {
    snprintf(buf, sizeof(buf), "--");
  } else {
    snprintf(buf, sizeof(buf), "%d%%", lvl);
  }
  d.setTextColor(white, bg);
  d.setTextSize(2);
  d.setTextDatum(textdatum_t::top_right);
  d.drawString(buf, kBatX - 6, kBatY - 1);

  // ─── 枠内の残量バー (残量に応じて緑/黄/赤) ───
  if (lvl >= 0 && lvl <= 100) {
    const uint16_t fill_c = (lvl > 50) ? green : (lvl >= 20 ? yellow : red);
    const int inner_x = kBatX + 2, inner_y = kBatY + 2;
    const int inner_w = kBatW - 4, inner_h = kBatH - 4;
    d.fillRect(inner_x, inner_y, inner_w, inner_h, panel);  // 残量背景をクリア
    const int fill_w = inner_w * lvl / 100;
    if (fill_w > 0) d.fillRect(inner_x, inner_y, fill_w, inner_h, fill_c);
  }

  // ─── 充電中マーク (フォントに ⚡ が無いため fillTriangle で稲妻を描く) ───
  if (charging) {
    const int cx = kBatX + kBatW / 2;
    const int cy = kBatY + kBatH / 2;
    d.fillTriangle(cx + 2, kBatY + 2, cx - 3, cy + 1, cx + 1, cy, bolt);
    d.fillTriangle(cx - 2, kBatY + kBatH - 2, cx + 3, cy - 1, cx - 1, cy, bolt);
  }
}

void draw_ui() {
  auto& d = M5.Display;
  const uint16_t bg     = d.color565(12, 16, 28);
  const uint16_t panel  = d.color565(40, 48, 70);
  const uint16_t accent = d.color565(120, 200, 255);
  const uint16_t btn    = d.color565(60, 70, 95);
  const uint16_t white  = d.color565(235, 240, 245);

  d.fillScreen(bg);

  // タイトルは左上へ。右上は電池表示に明け渡し、上端での重なりを避ける。
  d.setTextColor(white, bg);
  d.setTextSize(2);
  d.setTextDatum(textdatum_t::top_left);
  d.drawString("VOLUME", 12, 7);

  draw_battery();  // 右上に電池残量

  // 現在値 (%) を表示
  const int pct = static_cast<int>(
      static_cast<long>(s_volume - kVolMin) * 100 / (kVolMax - kVolMin));
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", pct);
  d.setTextSize(3);
  d.setTextDatum(textdatum_t::middle_center);
  d.drawString(buf, d.width() / 2, 64);

  // 音量バー
  const int fill = static_cast<int>(
      static_cast<long>(s_volume - kVolMin) * kBarW / (kVolMax - kVolMin));
  d.fillRoundRect(kBarX, kBarY, kBarW, kBarH, 6, panel);
  if (fill > 0) d.fillRoundRect(kBarX, kBarY, fill, kBarH, 6, accent);
  d.drawRoundRect(kBarX, kBarY, kBarW, kBarH, 6, white);

  // − / Close / ＋ ボタン
  const int label_y = kBtnY + kBtnH / 2;
  draw_button(kMinusX, label_y, "-",  btn);
  draw_button(kCloseX + (kCloseW - kBtnW) / 2, label_y, "OK", btn);
  draw_button(kPlusX,  label_y, "+",  btn);
}

void apply_volume() { M5.Speaker.setVolume(s_volume); }

void open_ui(uint32_t now_ms) {
  s_avatar->stop();
  delay(kDrawDrainMs);  // 残フレームが M5.Display を触り終えるのを待つ
  draw_ui();
  s_state = State::SHOWING;
  s_last_touch_ms = now_ms;
  Serial.println("[vol] open UI");
}

void close_ui() {
  save_volume();
  M5.Display.fillScreen(0);
  s_avatar->start();  // 描画タスク再生成 → 次フレームで顔が戻る
  s_state = State::IDLE;
  Serial.printf("[vol] close UI, volume=%u\n", s_volume);
}

void handle_tap(int x, int y) {
  if (in_rect(x, y, kMinusX, kBtnY, kBtnW, kBtnH)) {
    int v = static_cast<int>(s_volume) - kVolStep;
    s_volume = static_cast<uint8_t>(v < kVolMin ? kVolMin : v);
    apply_volume();
    draw_ui();
  } else if (in_rect(x, y, kPlusX, kBtnY, kBtnW, kBtnH)) {
    int v = static_cast<int>(s_volume) + kVolStep;
    s_volume = static_cast<uint8_t>(v > kVolMax ? kVolMax : v);
    apply_volume();
    draw_ui();
  } else if (in_rect(x, y, kCloseX, kBtnY, kCloseW, kBtnH)) {
    close_ui();
  }
}

}  // namespace

void init(Avatar& avatar) {
  s_avatar = &avatar;
  s_state = State::IDLE;
  load_volume();
  apply_volume();
  Serial.printf("[vol] init ok, volume=%u\n", s_volume);
}

void tick(uint32_t now_ms) {
  switch (s_state) {
    case State::IDLE: {
      if (M5.Touch.getCount() == 0) return;
      if (M5.Touch.getDetail().wasHold()) {
        open_ui(now_ms);
      }
      break;
    }
    case State::SHOWING: {
      if (M5.Touch.getCount() > 0) {
        const auto& t = M5.Touch.getDetail();
        if (t.wasClicked()) {
          handle_tap(t.x, t.y);
          if (s_state == State::IDLE) return;  // Close でタスク復帰済み
        }
        s_last_touch_ms = now_ms;  // 触れている間は自動クローズを延長
      }
      if (elapsed(now_ms, s_last_touch_ms, kAutoCloseMs)) {
        close_ui();
      }
      break;
    }
  }
}

bool is_active() { return s_state == State::SHOWING; }

uint8_t get() { return s_volume; }

}  // namespace volume_control

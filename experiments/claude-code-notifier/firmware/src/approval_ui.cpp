// approval_ui.cpp — Claude Code の権限確認を全画面 UI で受ける
//
// 設計要点:
//   - 非ブロッキング tick (millis() ベース)。volume_control と同じ流儀。
//   - UI を開くときに avatar.stop() で描画タスクを終了させ、M5.Display を独占して
//     UI を直接描く。回答確定 (または自動クローズ) で avatar.start() で顔へ復帰。
//   - 日本語表示のため描画時に lgfxJapanGothicP_16 を設定する (M5.Display は
//     フォント未設定のままなので、閉じるときに標準フォントへ戻す)。
//   - detail の折り返しはバイト単位ではなく UTF-8 の 1 文字単位で進めて
//     textWidth() で幅を測る (マルチバイト文字を壊さない)。最大 3 行、
//     溢れたら末尾「…」で省略。
//   - 回答は ANSWERED 状態で保持し、poll() が 1 回取得するか 10 秒経過で破棄。
//     ロングポーリングはしない (ESP32 WebServer は同期・シングルスレッド)。

#include "approval_ui.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <Avatar.h>

#include <string.h>

using m5avatar::Avatar;

namespace approval_ui {

namespace {

// ─── UI レイアウト (320x240)。ボタン段は volume_control と同じ高さ ───
constexpr int kTitleY = 12;
constexpr int kDetailX = 12, kDetailY = 64, kDetailW = 296;
constexpr int kDetailLineGap  = 4;
constexpr int kDetailMaxLines = 3;
constexpr int kBtnY = 162, kBtnH = 56, kBtnW = 96;
// 誤タップ対策で承認は右端に置く (拒否と物理的に離す)
constexpr int kDenyX = 8, kPcX = 112, kAllowX = 216;
// pc_only モード (ボタン1つだけ) 用。y/h は既存の kBtnY/kBtnH を流用し、
// 幅広で画面下部中央に置く。
constexpr int kPcOnlyX = 60, kPcOnlyW = 200;

// SHOWING 中の無操作でここまで待って自動クローズ (Mac 側は PC フォールバック)
constexpr uint32_t kAutoCloseMs = 60000;
// 回答後、poll() が取りに来ないまま経過したら破棄して次の /ask を受け付ける
constexpr uint32_t kAnswerLingerMs = 10000;
// stop() 後に残フレームが M5.Display を触り終えるのを待つ (drawLoop は 10ms 周期)
constexpr uint32_t kDrawDrainMs = 25;
// タップ確定時のハイライト表示時間 (視覚フィードバック)
constexpr uint32_t kTapFeedbackMs = 150;

enum class State : uint8_t { IDLE, SHOWING, ANSWERED };

Avatar*  s_avatar         = nullptr;
State    s_state          = State::IDLE;
char     s_id[64]         = "";
char     s_title[64]      = "";
char     s_detail[256]    = "";
bool     s_pc_only        = false;  // true: ボタンは [PCで確認] 1つだけ
Answer   s_answer         = Answer::kPc;
uint32_t s_last_touch_ms  = 0;
uint32_t s_answered_at_ms = 0;

bool elapsed(uint32_t now_ms, uint32_t since_ms, uint32_t threshold_ms) {
  return static_cast<int32_t>(now_ms - (since_ms + threshold_ms)) >= 0;
}

bool in_rect(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

// UTF-8 の先頭バイトから 1 文字のバイト長を返す。継続バイトが途中で
// 切れている壊れた列は、有効だったところまでの長さを返す (最低 1)。
size_t utf8_char_len(const char* s) {
  const uint8_t c = static_cast<uint8_t>(s[0]);
  size_t n = 1;
  if ((c & 0xE0) == 0xC0)      n = 2;
  else if ((c & 0xF0) == 0xE0) n = 3;
  else if ((c & 0xF8) == 0xF0) n = 4;
  for (size_t i = 1; i < n; ++i) {
    if ((static_cast<uint8_t>(s[i]) & 0xC0) != 0x80) return i;
  }
  return n;
}

// 行バッファ末尾の UTF-8 1 文字を取り除く (継続バイトを遡って先頭バイトごと消す)
void chop_last_utf8_char(char* s) {
  size_t n = strlen(s);
  if (n == 0) return;
  --n;
  while (n > 0 && (static_cast<uint8_t>(s[n]) & 0xC0) == 0x80) --n;
  s[n] = '\0';
}

// detail を UTF-8 の 1 文字単位で kDetailW に折り返して最大 kDetailMaxLines 行
// 描く。最終行から溢れる場合は末尾を「…」にして打ち切る。呼び出し前に
// フォント・サイズ・色・datum (top_left) を設定しておくこと。
void draw_detail() {
  auto& d = M5.Display;
  const int line_h     = d.fontHeight() + kDetailLineGap;
  const int ellipsis_w = d.textWidth("…");
  char   line[128];
  size_t line_len = 0;
  int    line_no  = 0;
  const char* p = s_detail;
  while (*p != '\0' && line_no < kDetailMaxLines) {
    // 改行は強制的に行送り (コマンド文字列に \n が混ざることがある)
    if (*p == '\n') {
      line[line_len] = '\0';
      d.drawString(line, kDetailX, kDetailY + line_no * line_h);
      ++line_no;
      line_len = 0;
      ++p;
      continue;
    }
    const size_t cl = utf8_char_len(p);
    if (line_len + cl >= sizeof(line)) break;  // 念のため (通常は幅で先に折れる)
    memcpy(line + line_len, p, cl);
    line[line_len + cl] = '\0';
    if (d.textWidth(line) > kDetailW && line_len > 0) {
      line[line_len] = '\0';  // 今の 1 文字は次の行へ回す
      if (line_no == kDetailMaxLines - 1) {
        // 最終行から溢れた → 「…」が収まるまで末尾を削って省略表示
        while (line[0] != '\0' && d.textWidth(line) + ellipsis_w > kDetailW) {
          chop_last_utf8_char(line);
        }
        strlcat(line, "…", sizeof(line));
        d.drawString(line, kDetailX, kDetailY + line_no * line_h);
        return;
      }
      d.drawString(line, kDetailX, kDetailY + line_no * line_h);
      ++line_no;
      line_len = 0;
      continue;  // p は進めない
    }
    line_len += cl;
    p += cl;
  }
  if (line_len > 0 && line_no < kDetailMaxLines) {
    line[line_len] = '\0';
    d.drawString(line, kDetailX, kDetailY + line_no * line_h);
  }
}

void draw_button(int x, const char* label, uint16_t fill_c, int w = kBtnW) {
  auto& d = M5.Display;
  const uint16_t white = d.color565(235, 240, 245);
  d.fillRoundRect(x, kBtnY, w, kBtnH, 10, fill_c);
  d.setTextColor(white, fill_c);
  d.setTextSize(1);
  d.setTextDatum(textdatum_t::middle_center);
  d.drawString(label, x + w / 2, kBtnY + kBtnH / 2);
}

void draw_ui() {
  auto& d = M5.Display;
  const uint16_t bg    = d.color565(12, 16, 28);
  const uint16_t white = d.color565(235, 240, 245);
  const uint16_t red   = d.color565(170, 60, 60);    // 拒否
  const uint16_t gray  = d.color565(60, 70, 95);     // PCで確認 (中間色)
  const uint16_t green = d.color565(45, 130, 75);    // 承認

  d.fillScreen(bg);
  // 日本語表示のため描画時にフォントを設定 (M5.Display は未設定のまま)
  d.setFont(&fonts::lgfxJapanGothicP_16);

  // 上部: タイトルを大きめに
  d.setTextColor(white, bg);
  d.setTextSize(2);
  d.setTextDatum(textdatum_t::top_center);
  d.drawString(s_title, d.width() / 2, kTitleY);

  // 中央: detail を最大 3 行で折り返し表示
  d.setTextSize(1);
  d.setTextDatum(textdatum_t::top_left);
  draw_detail();

  // 下部: pc_only モードは [PCで確認] 1つだけ、通常は 3 ボタン (承認は右端、誤タップ対策)
  if (s_pc_only) {
    draw_button(kPcOnlyX, "PCで確認", gray, kPcOnlyW);
  } else {
    draw_button(kDenyX,  "拒否",     red);
    draw_button(kPcX,    "PCで確認", gray);
    draw_button(kAllowX, "承認",     green);
  }
}

// 画面を閉じて顔へ復帰する。状態遷移は呼び出し元で行う。
void close_ui() {
  auto& d = M5.Display;
  d.fillScreen(0);
  // 音量 UI など後続の直接描画が影響を受けないよう標準フォントへ戻す
  d.setFont(&fonts::Font0);
  s_avatar->start();  // 描画タスク再生成 → 次フレームで顔が戻る
}

void handle_tap(uint32_t now_ms, int x, int y) {
  int         btn_x;
  int         btn_w = kBtnW;
  const char* label;
  uint16_t    hi_c;
  Answer      ans;
  auto& d = M5.Display;
  if (s_pc_only) {
    if (!in_rect(x, y, kPcOnlyX, kBtnY, kPcOnlyW, kBtnH)) {
      return;  // ボタン外タップは無視 (自動クローズの延長のみ)
    }
    btn_x = kPcOnlyX; btn_w = kPcOnlyW; label = "PCで確認";
    hi_c  = d.color565(120, 140, 180); ans = Answer::kPc;
  } else if (in_rect(x, y, kDenyX, kBtnY, kBtnW, kBtnH)) {
    btn_x = kDenyX;  label = "拒否";     hi_c = d.color565(230, 120, 110); ans = Answer::kDeny;
  } else if (in_rect(x, y, kPcX, kBtnY, kBtnW, kBtnH)) {
    btn_x = kPcX;    label = "PCで確認"; hi_c = d.color565(120, 140, 180); ans = Answer::kPc;
  } else if (in_rect(x, y, kAllowX, kBtnY, kBtnW, kBtnH)) {
    btn_x = kAllowX; label = "承認";     hi_c = d.color565(110, 210, 140); ans = Answer::kAllow;
  } else {
    return;  // ボタン外タップは無視 (自動クローズの延長のみ)
  }
  // 押したボタンをハイライトして視覚フィードバックしてから閉じる
  draw_button(btn_x, label, hi_c, btn_w);
  delay(kTapFeedbackMs);
  s_answer         = ans;
  s_answered_at_ms = now_ms;
  s_state          = State::ANSWERED;
  close_ui();  // poll を待たずに顔へ復帰してよい (回答は保持済み)
  Serial.printf("[approval] answered id=%s answer=%d\n", s_id, static_cast<int>(ans));
}

}  // namespace

void init(Avatar& avatar) {
  s_avatar = &avatar;
  s_state  = State::IDLE;
  Serial.println("[approval] init ok");
}

void tick(uint32_t now_ms) {
  switch (s_state) {
    case State::IDLE:
      break;
    case State::SHOWING: {
      if (M5.Touch.getCount() > 0) {
        const auto& t = M5.Touch.getDetail();
        if (t.wasClicked()) {
          handle_tap(now_ms, t.x, t.y);
          if (s_state != State::SHOWING) return;  // 回答確定でタスク復帰済み
        }
        s_last_touch_ms = now_ms;  // 触れている間は自動クローズを延長
      }
      if (elapsed(now_ms, s_last_touch_ms, kAutoCloseMs)) {
        // 無操作タイムアウト。以後の poll は kUnknown → Mac 側は PC フォールバック
        Serial.printf("[approval] auto close (timeout) id=%s\n", s_id);
        close_ui();
        s_state = State::IDLE;
        s_id[0] = '\0';
      }
      break;
    }
    case State::ANSWERED: {
      if (elapsed(now_ms, s_answered_at_ms, kAnswerLingerMs)) {
        // 回答が取りに来られないまま経過 → 破棄して次の /ask を受け付ける
        Serial.printf("[approval] answer expired id=%s\n", s_id);
        s_state = State::IDLE;
        s_id[0] = '\0';
      }
      break;
    }
  }
}

bool is_active() { return s_state == State::SHOWING; }

bool show(const char* id, const char* title, const char* detail, const char* ui,
          uint32_t now_ms) {
  // v1 は 1 件のみ保持。表示中 or 未回収の回答が残っている間は受けない
  // (呼び出し元が 409 を返し、Mac 側は PC フォールバックする)。
  if (s_state != State::IDLE) return false;
  if (id == nullptr || id[0] == '\0') return false;
  snprintf(s_id,     sizeof(s_id),     "%s", id);
  snprintf(s_title,  sizeof(s_title),  "%s", (title && title[0]) ? title : "許可しますか?");
  snprintf(s_detail, sizeof(s_detail), "%s", detail ? detail : "");
  s_pc_only = (ui != nullptr) && (strcmp(ui, "pc_only") == 0);

  s_avatar->stop();
  delay(kDrawDrainMs);  // 残フレームが M5.Display を触り終えるのを待つ
  draw_ui();
  s_state         = State::SHOWING;
  s_last_touch_ms = now_ms;
  Serial.printf("[approval] show id=%s\n", s_id);
  return true;
}

Poll poll(const char* id, Answer& out) {
  if (s_state == State::IDLE) return Poll::kUnknown;
  if (id == nullptr || strcmp(id, s_id) != 0) return Poll::kUnknown;
  if (s_state == State::SHOWING) return Poll::kPending;
  // ANSWERED: 回答を渡して IDLE へ戻す (取得は 1 回きり)
  out     = s_answer;
  s_state = State::IDLE;
  s_id[0] = '\0';
  return Poll::kAnswered;
}

}  // namespace approval_ui

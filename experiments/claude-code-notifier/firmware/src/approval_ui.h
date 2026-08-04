// approval_ui.h — Claude Code の権限確認を全画面 UI で受ける
//
// 前提:
//   - M5StackChan.begin() / avatar.init() が終わっていること
//   - loop() の冒頭で M5StackChan.update() を呼んでおくこと (M5.Touch の更新)
//   - loop() で tick() を毎回呼ぶこと
//
// 流れ:
//   - /ask ハンドラが show() を呼ぶと avatar を停止し、質問文と
//     [拒否] [PCで確認] [承認] の 3 ボタンを全画面表示する (SHOWING)。
//     ui="pc_only" のときは承認/拒否の2値で表現できない質問 (AskUserQuestion
//     等) のため、ボタンを [PCで確認] 1つだけにする。
//   - ボタンタップで回答を保持し、画面を閉じて顔へ復帰する (ANSWERED)。
//     poll を待たずに閉じてよい設計 (回答は poll() が取りに来るまで保持)。
//   - Mac 側フックスクリプトが /answer 経由で poll() を叩き、回答を 1 回
//     取得すると IDLE へ戻る。取りに来ないまま 10 秒経っても破棄して IDLE へ。
//   - SHOWING のまま 60 秒無操作なら自動クローズ (以後の poll は kUnknown →
//     Mac 側は PC のプロンプトへフォールバックする)。
//
// is_active(): UI 表示中 (SHOWING) は true。volume_control と同様、main 側で
// 他モジュールの tick / 発話を抑止するのに使う (描画タスク停止中の
// setExpression() による不正タスクハンドル参照を防ぐ)。

#pragma once

#include <stdint.h>

namespace m5avatar { class Avatar; }

namespace approval_ui {

void init(m5avatar::Avatar& avatar);
void tick(uint32_t now_ms);
bool is_active();

// /ask から呼ぶ。表示中 (または未回収の回答が残っている) なら false を返し、
// 呼び出し元が 409 を返す約束。成功時は UI を開いて true。
// ui: "pc_only" のときはボタンを「PCで確認」1つだけにする (承認/拒否の2値で
// 表現できない質問向け)。それ以外・nullptr・空文字は従来の3ボタン。
bool show(const char* id, const char* title, const char* detail, const char* ui,
          uint32_t now_ms);

// /answer から呼ぶ。id 不一致・保持なしは kUnknown。
enum class Poll : uint8_t { kUnknown, kPending, kAnswered };
enum class Answer : uint8_t { kAllow, kDeny, kPc };
// kAnswered を返したら内部状態を IDLE に戻す (回答の取得は 1 回きり)。
Poll poll(const char* id, Answer& out);

}  // namespace approval_ui

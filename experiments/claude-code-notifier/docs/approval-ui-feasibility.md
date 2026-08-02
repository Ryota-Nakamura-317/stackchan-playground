# 調査: Stack-Chan 画面での Claude Code 承認 UI は技術的に可能か

Claude Code がユーザーにアクション(権限承認など)を求めたとき、

1. Stack-Chan の**画面上部に質問文**を表示し、
2. **画面下部にボタン**を並べ、
3. ボタンの 1 つは **PC の最前面に Claude デスクトップアプリを表示させる**

…という構想の技術的実現性を調査した記録。

## 結論

**3 要素すべて技術的に可能。** さらに当初構想を超えて、**Stack-Chan のボタンで承認/拒否そのものを Claude Code に返す**ことまで公式フック機構だけで実現できる見込み。

| 要素 | 実現性 | 根拠 |
| --- | --- | --- |
| 質問文の画面表示 + タッチボタン | ◎ 実証済み | 本リポジトリの volume_control が同型の全画面タッチ UI を既に実装済み |
| ボタン押下を PC へ返す | ○ 設計で解決 | フックスクリプトが Stack-Chan をショートポーリング(後述) |
| 承認/拒否を Claude Code に反映 | ○ 公式機構あり | `PermissionRequest` フックの JSON 出力 `decision.behavior: allow/deny` |
| Claude デスクトップアプリを最前面へ | ◎ 容易 | macOS の `open -a "Claude"`(追加権限不要) |

## 鍵になる発見: `PermissionRequest` フック

当初は「Notification フック(通知のみ、応答は返せない)で質問を出し、承認自体は PC で行う」しかないと想定していたが、公式ドキュメント
([hooks-guide](https://code.claude.com/docs/en/hooks-guide.md)) の調査で以下が確認できた。

- **`PermissionRequest` フックイベントが存在する**。`PreToolUse`(全ツール呼び出しで発火)と違い、**権限プロンプトが実際に表示されるときだけ**発火する。今回の用途にぴったり。
- フックは stdout に JSON を返すことで**プロンプトへの回答を代行できる**:

  ```json
  {
    "hookSpecificOutput": {
      "hookEventName": "PermissionRequest",
      "decision": { "behavior": "allow" }
    }
  }
  ```

  何も出力せず exit 0 すれば、通常どおり PC 側のプロンプトにフォールバックする。
- フック入力の JSON には `tool_name` / `tool_input` / `permission_mode` などが含まれ、**画面に出す質問文を組み立てる材料が揃っている**(例: `Bash: rm -rf build/`)。
- `command` 型フックの**タイムアウトはデフォルト 600 秒**で、フックごとの `timeout` フィールド(秒)で調整可能。「ユーザーがボタンを押すまで数十秒待つ」用途に十分。
- 類似機構として `PreToolUse` フックの `permissionDecision: allow/deny/ask` もある(こちらは全ツール呼び出しで発火するためフォールバック候補)。

> ⚠️ `PermissionRequest` は比較的新しいイベント。実装着手時に手元の Claude Code バージョンで
> `claude --version` と実フックの発火を必ず確認すること。使えない場合は
> `PreToolUse`(matcher でツールを絞り、即座に `ask` を返す分岐を入れる)で代替できる。

## 推奨アーキテクチャ

```
Claude Code (Mac)
  └─ PermissionRequest hook ─→ scripts/ask_stackchan.sh
       ├─ hook JSON から tool_name / tool_input を抽出して質問文を組み立て
       ├─ IP 解決は既存の3層機構 (lib_stackchan.sh) を再利用
       ├─ POST /ask {"id":..., "title":"許可しますか?", "detail":"Bash: git push ..."}
       ├─ GET /answer?id=... を 0.5 秒間隔でポーリング (上限 = 画面の自動クローズと同期)
       └─ 回答に応じて stdout へ JSON を返す:
            承認     → decision.behavior=allow
            拒否     → decision.behavior=deny
            PCで確認 → open -a "Claude" を実行し、何も出力せず exit 0
                        (→ 通常の PC 側プロンプトにフォールバック)
            タイムアウト/不達 → 何も出力せず exit 0 (同上)

Stack-Chan (CoreS3) — 既存 claude-code-notifier ファームの拡張
  ├─ POST /ask    → 質問を保持し approval_ui を起動 (avatar.stop() → 全画面 UI)
  │                  上部: 質問文 (lgfxJapanGothicP フォントで日本語 OK)
  │                  下部: [承認] [拒否] [PCで確認] の3ボタン
  │                  + 「kyo'ka kuda'sai.」発話 (既存 /speak 資産を流用)
  ├─ GET /answer  → {"state":"pending"} または {"state":"answered","answer":"allow"} を即返し
  └─ タッチ処理    → volume_control と同じ tick + in_rect 方式。
                     回答確定 or 自動クローズで avatar.start() で顔に復帰
```

### なぜこの形か

- **PC 側に常駐デーモンが不要。** ボタン押下を Stack-Chan → PC へ「プッシュ」するには Mac 側で HTTP を待ち受ける常駐プロセスが要るが、質問が出ている間はフックスクリプト自身が生きているので、**スクリプトがポーリングで取りに行けば常駐なしで完結**する。
- **ロングポーリングは不可、ショートポーリング一択。** ESP32 の `WebServer` は同期・シングルスレッドで、応答を保留すると `loop()` 全体(タッチ処理・サーボ・WiFi 監視)が止まる。「即返しの GET を 0.5 秒間隔で叩く」なら既存構造のまま載る。LAN 内なので負荷も問題ない。
- **失敗時は必ず PC のプロンプトに落ちる。** Stack-Chan 不達・タイムアウト・二重リクエスト(並列ツール呼び出し)はすべて「何も出力しない exit 0」に倒せば、最悪でも従来どおり PC で承認できる。安全側に壊れる。

## 各要素の詳細

### 1. 画面 UI(質問上部 + ボタン下部)

**実証済み。** `firmware/src/volume_control.cpp` が完全に同じパターンを実装している:

- `avatar.stop()` で描画タスクを止めて `M5.Display` を独占 → 全画面 UI を直接描画 → `avatar.start()` で顔に復帰
- `M5.Touch.getDetail().wasClicked()` + `in_rect()` によるボタン判定
- 無操作 8 秒で自動クローズする `millis()` ベースの非ブロッキング tick

承認 UI はこの流儀のコピーで作れる。差分は「質問文の日本語表示」だけだが、これも既に吹き出しで `fonts::lgfxJapanGothicP_16` を使用しており、`M5.Display` への日本語描画は実績あり。320×240 なので長いコマンドは折り返し + 末尾省略(例: 3 行まで)の工夫が要る。

既存モジュールとの排他(`volume_control::is_active()` と同様の `approval_ui::is_active()` で idle motion / sleep / pet reaction / `/speak` を止める)も既存パターンのまま。

### 2. ボタン押下の PC への伝達

上記アーキテクチャの通り、フックスクリプトによるショートポーリングで解決。IP 解決(キャッシュ → mDNS → ARP)と `curl --ipv4` の知見は `lib_stackchan.sh` をそのまま再利用できる。

### 3. 承認/拒否の Claude Code への反映

`PermissionRequest` フックで可能(上述)。重要な UX 上の性質として、**フックが回答を待っている間、PC のターミナルには権限プロンプトがまだ表示されない**(フックが `ask` 相当で抜けて初めて表示される)。つまり:

- タイムアウトは長くしすぎない(30〜60 秒程度 + 画面の自動クローズと同期)
- 「PCで確認」ボタンは「フックを即終了して PC プロンプトを出させる + アプリを最前面へ」という動作になり、構想と自然に噛み合う

### 4. Claude デスクトップアプリを最前面に表示

macOS では `open -a "Claude"` の 1 コマンド。フックはユーザーの GUI セッションで実行されるため追加の許可(アクセシビリティ等)は不要。AppleScript(`osascript -e 'tell application "Claude" to activate'`)でも同じことができる。

なお Claude Code を**ターミナルで**使っている場合、承認操作が行われるのはターミナル側なので、最前面に出すべきは実際には Terminal / iTerm という可能性がある。どのアプリを activate するかは `config.local.sh` の変数(例: `FOCUS_APP="Claude"`)にしておくのが良い。

## リスク・制約

| 項目 | 内容 | 対策 |
| --- | --- | --- |
| **セキュリティ** | `/answer` が無認証だと、同一 LAN の第三者が curl 一発で任意ツール実行を「承認」できてしまう | 共有トークン必須化(firmware `secrets.h` + Mac `config.local.sh` に同じトークン、`X-Stackchan-Token` ヘッダ検証)。HTTP 平文なので家庭内 LAN 前提の割り切りは README に明記 |
| `allow` の意味 | フックの `allow` は対話プロンプトをスキップする(ポリシーの deny ルールは引き続き有効) | 誤タップ対策としてボタンを十分大きく・承認は右端に配置。必要なら「承認は長押しのみ」も検討 |
| `PermissionRequest` の成熟度 | 新しめのイベントで、`decision` の正確なスキーマ(deny 時の `message` 等)はバージョン差がありうる | 実装前に手元バージョンで発火と入出力を実測。ダメなら `PreToolUse` フォールバック |
| 並列プロンプト | ツールの並列実行で承認要求が同時に 2 件来る可能性 | v1 は 1 件のみ保持し、2 件目の `/ask` には 409 を返してスクリプト側は即 PC フォールバック |
| 発話とポーリングの競合 | 既存 `/speak` は再生完了まで HTTP 応答をブロック(約 2 秒) | `/ask` 受信時の発話は再生完了を待たずに返す(または発話を tick 化)。ポーリング側のタイムアウトは 3 秒に |
| デスクトップアプリ内の Claude Code | デスクトップアプリ(Cowork)内で `~/.claude/settings.json` の hooks が同様に効くかは公式ドキュメントに明記なし | ターミナル運用では問題なし。デスクトップアプリ運用にするなら実測で確認 |
| 画面の狭さ | 320×240 に長いコマンド全文は出ない | 1 行目にツール名、以降 3 行程度に `tool_input` 要約 + 省略。全文確認したいときのための「PCで確認」ボタンが受け皿になる |

## 実装ステップ案(次フェーズ)

1. **フック実測(最小検証)**: `PermissionRequest` に `jq` でログを吐くだけのフックを付け、入力 JSON と `decision` 出力の実挙動をローカルで確認(Stack-Chan 不要、30 分程度)
2. **ファーム拡張**: `approval_ui.{h,cpp}` 追加 + `/ask` `/answer` エンドポイント + トークン検証
3. **Mac 側スクリプト**: `ask_stackchan.sh`(質問文組み立て → POST → ポーリング → decision 出力 / `open -a` 分岐)
4. **統合テスト**: 実プロンプトで承認・拒否・PCで確認・タイムアウト・電源断の 5 シナリオ

## 参考

- [Claude Code hooks ガイド](https://code.claude.com/docs/en/hooks-guide.md) — `PermissionRequest` / `PreToolUse` の入出力仕様、`timeout` 設定(command 型デフォルト 600 秒)
- 本リポジトリ `firmware/src/volume_control.cpp` — 全画面タッチ UI の実装パターン
- 本リポジトリ `scripts/lib_stackchan.sh` — IP 3層解決の再利用元

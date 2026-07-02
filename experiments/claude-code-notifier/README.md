# claude-code-notifier

Claude Code のセッション完了 (`Stop` hook) や、ユーザー操作待ち (`Notification` hook の `permission_prompt` / `elicitation_dialog`) が発生したときに、**Stack-Chan が日本語で発話して知らせる** 仕掛け。

Stack-Chan の IP は「前回成功 IP のキャッシュ → mDNS → ARP (MAC 検索)」の3層で自動解決するため、**DHCP で IP が変わっても設定変更なしで追従**します。全層で見つからないときは発話をスキップし(通知音はバナー側に一本化)、バックグラウンドで LAN を再探索して次回までに自己修復します。

## 対象ハードウェア

- **M5Stack 公式 Stack-Chan(K151系、CoreS3 ベース)**
- ESP32-S3 / 16MB Flash / AW88298 I2S スピーカー / Wi-Fi 2.4GHz

> ⚠️ スイッチサイエンスの旧 Stack-chan ボードキット 11129(M5Stack Core2 + SG90×2)とは別商品です。本実験は CoreS3 専用に書かれており、Core2 ではそのままビルドできません。

## ⚠️ 工場出荷時ファームウェアへの影響

本実験のスケッチを `pio run -t upload` で書き込むと、**CoreS3 内蔵フラッシュ上の工場出荷時ファームウェアは丸ごと上書きされます**。具体的には以下が使えなくなります。

- AI Agent
- 表情・アニメーション一式(自作 Avatar 1 枚だけになる)
- ESP-NOW 無線リモコン連携
- 公式モバイルアプリ連携(映像視聴・アバター遠隔操作・OTA)
- アプリ内ストアからのオンラインダウンロード
- 工場ファーム経由の OTA アップデート
- AI Agent 連動の表情モーションや遠隔操作機能(本スケッチでは「待機中のきょろきょろ」「頭頂部を撫でられたら喜ぶ」「画面で音量調整」と発話だけ実装。詳細は下記 **待機中の首振り (idle motion)** / **頭を撫でられたら喜ぶ (pet reaction)** / **画面で音量を調整する (volume control)** を参照)

ESP32 は同時に走るアプリが 1 つだけなので、**工場ファームと本ファームを同居させて両方動かすことはできません**。OTA パーティション(`factory` / `app0` / `app1`)を使ったブート切替によるデュアルブートは理論上可能ですが、工場ファームのパーティション構成は非公開かつ独自で、合わせて再ビルドする手段がないため非現実的です。

### 工場ファームへの戻し方(M5Burner)

1. M5Stack 公式の [M5Burner](https://docs.m5stack.com/en/download) をダウンロードして起動。
2. CoreS3 を USB-C で Mac に接続。
3. 左サイドメニューから `CORES3` カテゴリを選択。
4. `StackChan`(K151 系の出荷時ファーム)を選んで `Download` → `Burn`。
5. シリアル番号入力を求められる場合があるので、本体底面のシリアルを入力。

### 運用Tips

- 「通知ファーム ⇄ 工場ファーム」を頻繁に往復させたい場合は、ビルド済み `.bin` を保存しておき `esptool.py write_flash` で焼くと PlatformIO 経由より速い。
- 工場ファームを完全に保全したい場合は、M5Burner で焼いた直後に `esptool.py read_flash 0 0x1000000 factory_backup.bin` でフラッシュ全体を吸い出してバックアップしておくと安心。

## 仕組み

```
Claude Code (Mac)
  ├─ Stop hook         ─┐
  └─ Notification hook ─┴─→ scripts/notify_stackchan.sh <event>
                              ├─ stdin の hook JSON から message を抽出
                              ├─ event / message → 発話 kind を決定
                              ├─ IP を3層で解決 (すべて curl は --ipv4 付き)
                              │    1. ~/.cache/stackchan/last_ip (前回成功 IP)
                              │    2. mDNS: ping -c1 stackchan.local (~0.1 秒)
                              │    3. ARP テーブルから STACKCHAN_MAC を検索
                              ├─ 解決成功 → POST /speak (バックグラウンド) + IP をキャッシュ
                              └─ 全滅 → 発話スキップ + rediscover_stackchan.sh を
                                 デタッチ起動 (ping スイープ → ARP → キャッシュ自己修復)

Stack-Chan (CoreS3)
  ├─ Wi-Fi STA + mDNS (stackchan.local を公開)
  ├─ GET /healthz → 200 即返し
  ├─ POST /speak  → AquesTalk pico で PCM 合成 → M5.Speaker.playRaw() 出力
  │                 + Avatar 表情変化(発話中は idle motion 停止)
  ├─ idle motion  → StackChan-BSP の Motion API でランダムに首を振る
  │                 (2〜5 秒間隔、yaw ±20° / pitch 35°±10°)
  └─ pet reaction → 頭頂部の前後スワイプ (BSP TouchSensor) で
                    Happy 表情 + RGB LED 暖色点灯 (3 秒)。スリープ中なら起床も兼ねる
```

> 内部実装メモ: AquesTalk 同梱のラッパー `AquesTalkTTS` は M5StampS3 用 I2S ピン固定で CoreS3 では使えないため、低レベル C API (`aquestalk.h`) を直接呼んで M5Unified の `M5.Speaker.playRaw()` (AW88298 codec を正しく初期化済み) に流し込んでいます。

参考記事: [Claude Code / Codex の Stop Hook で Stack-chan を喋らせる(fujihide さん, Zenn)](https://zenn.dev/fujihide/articles/389892c9f0654a)

本実験は記事に対して以下を拡張しています:

- `Notification` hook も拾う(作業完了だけでなく権限プロンプトも喋る)
- IP の3層自動解決(キャッシュ → mDNS → ARP)+ 不達時のバックグラウンド自己修復。DHCP で IP が変わっても設定変更不要
- 発話文言は kind ごとに firmware 側に焼き込み、ASCII の AquesTalk 音素記号列で記述(評価版でナ行・マ行が「ヌ」化する制限を回避するため N/M を含まない文に統一)
- mDNS (`stackchan.local`) の名前解決は **`curl --ipv4` 必須**。macOS の `.local` 解決が 3〜5 秒かかる正体は AAAA (IPv6) クエリのタイムアウト待ちで(ESP32 は AAAA に応答しない)、A レコードだけなら数 ms で返る。`--ipv4` を付ければ mDNS 名運用で問題ない

## 必要なもの

- 上記 M5Stack 公式 Stack-Chan(K151系)
- USB-C ケーブル
- 2.4GHz Wi-Fi 環境(Mac と Stack-Chan が同一 LAN にいる必要あり)
- macOS + [Homebrew](https://brew.sh/)
- [Claude Code](https://docs.claude.com/claude-code)(インストール済み)
- `jq`(stdin の hook JSON 解析用、推奨): `brew install jq`
- [AquesTalk ESP32 Ver.2.4.2 以降](https://www.a-quest.com/products/aquestalk_esp32.html) のライブラリ ZIP
  - **評価版は無償** だがナ行・マ行が「ヌ」になる制限あり。試用には十分
  - **継続利用・商用には有償ライセンスキー** が必要
  - **Ver.2.4.2 以降が必須**(それより古いと ESP32-S3 で動かない)

## セットアップ手順

### 1. PlatformIO のインストール

```bash
brew install platformio jq
```

### 2. AquesTalk ESP32 の入手と配置

1. https://www.a-quest.com/download.html を開き、「AquesTalk ESP32」(Ver.2.4.2 以降)の ZIP を取得。
2. 展開して `firmware/lib/AquesTalkTTS/` に配置。詳細手順は [`firmware/lib/AquesTalkTTS/README.md`](firmware/lib/AquesTalkTTS/README.md) を参照。
3. 評価版でまず動作確認 → 気に入ったら製品版ライセンスを購入。

### 3. Wi-Fi 情報を `secrets.h` に記入

```bash
cd experiments/claude-code-notifier
cp firmware/include/secrets.example.h firmware/include/secrets.h
# secrets.h を編集して WIFI_SSID / WIFI_PASS を埋める
# AQUESTALK_LICENSE_KEY は評価版ならダミー値のままで OK
```

`secrets.h` は `.gitignore` で除外されているのでコミットされません。

### 4. ビルドして Stack-Chan に書き込み

1. Stack-Chan を USB-C で Mac に接続して電源 ON。
2. ポートを確認: `ls /dev/cu.*`(CoreS3 は `/dev/cu.usbmodem*` に出ることが多い)。
3. ビルド + 書き込み + モニタを一括実行:
   ```bash
   cd experiments/claude-code-notifier
   pio run -t upload -t monitor
   ```
4. **初回ビルドは依存ダウンロードとツールチェーン取得で 10〜30 分かかります**。
5. シリアルモニタに `[wifi] connected. IP=192.168.x.y` と `[mdns] stackchan.local advertised` が出れば OK。Stack-Chan の画面にも IP が出ます。

### 5. Mac 側の設定

```bash
cd experiments/claude-code-notifier/scripts
cp config.example.sh config.local.sh
# config.local.sh を編集:
#   STACKCHAN_HOST … 既定の "stackchan.local" のままで OK (IP 直書きも可)
#   STACKCHAN_MAC  … Stack-Chan の MAC アドレスを書く (ARP フォールバック用)。
#                    起動時シリアル出力かルータの DHCP クライアント一覧で確認
```

`config.local.sh` も `.gitignore` 対象です。IP は「キャッシュ → mDNS → ARP」で自動解決されるため、DHCP で変わっても書き換え不要です。

### 6. Claude Code の hook 設定

`~/.claude/settings.json` の `hooks` を以下のように書き換えます(既存の `Stop` / `Notification` hook を置き換える形)。

```json
{
  "hooks": {
    "Stop": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "/Users/<your-user>/Workspace/dev/stackchan/experiments/claude-code-notifier/scripts/notify_stackchan.sh stop"
          }
        ]
      }
    ],
    "Notification": [
      {
        "matcher": "permission_prompt|elicitation_dialog",
        "hooks": [
          {
            "type": "command",
            "command": "/Users/<your-user>/Workspace/dev/stackchan/experiments/claude-code-notifier/scripts/notify_stackchan.sh notification"
          }
        ]
      }
    ]
  }
}
```

- スクリプトパスは **絶対パスで** 指定する必要があります(Claude Code は hook を任意の cwd で実行するため)。
- リポジトリ内 `.claude/settings.local.json` にも hook を入れていると **二重発話** になります。グローバル設定に一本化したら、ローカル側は空にするか削除してください。

## 動作確認

### 手動 curl テスト

`HOST` は `stackchan.local` のままで OK(**`--ipv4` を忘れずに**。付けないと AAAA クエリ待ちで 5 秒かかる)。IP 直書きでも可。

```bash
HOST=stackchan.local
alias curl='curl --ipv4'   # このテスト中だけ。スクリプト内の curl は常に --ipv4 付き

# 疎通確認(スクリプトのフォールバック判定にも使われる)
curl -s http://$HOST/healthz

# 通知種別ごとの発話
curl -X POST http://$HOST/speak \
  -H 'Content-Type: application/json' \
  -d '{"mode":"notify","kind":"done"}'      # → 「sa'gyou shu'uryou.」(作業終了)

curl -X POST http://$HOST/speak \
  -H 'Content-Type: application/json' \
  -d '{"mode":"notify","kind":"confirm"}'   # → 「kyo'ka kuda'sai.」(許可ください)

curl -X POST http://$HOST/speak \
  -H 'Content-Type: application/json' \
  -d '{"mode":"notify","kind":"idle"}'      # → 「tsugi'wa?」(次は?)

# 自由文(AquesTalk の音素記号列を直接渡す)
curl -X POST http://$HOST/speak \
  -H 'Content-Type: application/json' \
  -d '{"mode":"free","text":"te'sutoda'yo."}'
```

### スクリプト単体テスト

```bash
# Stop hook 相当(stdin に hook JSON を渡す)
echo '{"hook_event_name":"Stop"}' | scripts/notify_stackchan.sh stop

# Notification hook 相当
echo '{"hook_event_name":"Notification","message":"Claude needs your permission to use Bash"}' \
  | scripts/notify_stackchan.sh notification

# Stack-Chan の電源を切って同じコマンドを実行 → 無音のままログに
# reason=unreachable が記録され、rediscover がバックグラウンド起動すること
```

ログは `/tmp/stackchan_notify.log` に追記されます。`tail -f /tmp/stackchan_notify.log` で発火状況を確認できます。

### Claude Code 統合確認

`~/.claude/settings.json` を更新後、新しいセッションで:

- 簡単な依頼(例: `echo hi` 実行)→ 完了時に Stack-Chan が「sa'gyou shu'uryou.」(作業終了)を発話
- 普段拒否設定のコマンドを依頼して権限プロンプトを発生させる → 「kyo'ka kuda'sai.」(許可ください)
- Stack-Chan の電源を切って同じ操作 → 無音(ログに `reason=unreachable`)。バナー通知音のみ

## 待機中の首振り (idle motion)

通知待機中も「生きている感」を出すため、yaw / pitch サーボを **2〜5 秒間隔で
ランダムに小さく動かします**。発話中 (`/speak` 受信〜再生完了) は自動で止まります。

### 仕組み

公式 Arduino 用 BSP [`m5stack/StackChan-BSP`](https://github.com/m5stack/StackChan-BSP)
(MIT) を `platformio.ini` の `lib_deps` で取り込み、`M5StackChan.begin()` 一発で:

- PY32L020 IO Expander 経由のサーボ電源 (VM EN) ON
- Feetech SCS シリアルバスサーボの UART_NUM_1 / 1 Mbps / GPIO 6,7 初期化
- 内部 FreeRTOS task による補間アニメーション

を済ませます。本ファームの `firmware/src/idle_motion.cpp` は `Motion.move(yaw, pitch, speed)`
を周期的に呼ぶだけなので非ブロッキング (補間中も `loop()` は通常通り回る)。

### 振幅・周期の調整

`firmware/src/idle_motion.cpp` 冒頭の定数を編集して再フラッシュ:

| 定数 | 既定値 | 単位 / 意味 |
|---|---|---|
| `kYawAmplitude` | 200 | 1/10 度 (±20°) |
| `kPitchCenter` | 350 | 1/10 度 (= 35°、少し上向き) |
| `kPitchAmplitude` | 100 | 1/10 度 (±10°) |
| `kSpeedMin` / `kSpeedMax` | 200 / 400 | 0〜1000 (小さいほどゆっくり) |
| `kPauseMinMs` / `kPauseMaxMs` | 2000 / 5000 | 次の動きまでの間隔 (ms) |

### 完全に無効化したい

`setup()` 末尾の `idle_motion::init();` を削除するか、その直後に
`idle_motion::set_enabled(false);` を 1 行足す。

### 首が傾いて見える

BSP の `examples/Servo/HomeCalibration/HomeCalibration.ino` を一旦焼いて画面タッチで
ホーム位置を再キャリブレーション → NVS の `servo` namespace に保存される → 本ファームに
戻すとそのホーム位置が引き継がれる。

> ⚠️ `M5StackChan.begin()` の最後でサーボが中央位置 (yaw=0, pitch=0) に動きます。
> 起動前に **必ず机に固定** してください (手で持ったまま電源を入れると無理に動かす力が
> 加わる可能性)。

> ℹ️ 工場ファームに M5Burner で戻せば AI Agent やモバイルアプリ連動も復活します
> ([⚠️ 工場出荷時ファームウェアへの影響](#-工場出荷時ファームウェアへの影響))。

## 頭を撫でられたら喜ぶ (pet reaction)

工場ファームの「頭を撫でると喜ぶ」を最小限で復活させた機能。
頭頂部 (CoreS3 LCD 上のタッチパネルではなく、ロボットボディ側の Si12T 3 ゾーン静電容量センサ) を
**前→後ろ または 後ろ→前にスワイプ**すると、Avatar が約 3 秒間 `Expression::Happy` に切り替わり、
12 個の WS2812C RGB LED が暖色ピンクで点灯します。単発タッチ (wasClicked) は意図的に拾わず、
「撫でた感」のあるスワイプのみに反応します。

### 仕組み

- 検出: `M5StackChan.TouchSensor.wasSwipedForward() / wasSwipedBackward()` (BSP がジェスチャ検出済み)
- 表情: `m5avatar::Avatar::setExpression(Expression::Happy)` → 3 秒後 `Neutral` に戻す
- LED: `M5StackChan.showRgbColor(r, g, b)` で全 12 個を一括点灯/消灯
- スリープ中の撫で: `sleep_manager::notify_activity()` を先に呼んで AWAKE 復帰させてから喜ぶ
- idle motion との競合: HAPPY 中は `idle_motion::set_enabled(false)` で停止、終了時に再開
- 連続して撫で続けると HAPPY タイマだけが延長され、喜びが持続する

### チューニング

`firmware/src/pet_reaction.cpp` 冒頭の定数:

| 定数 | 既定値 | 意味 |
|---|---|---|
| `kHappyDurationMs` | 3000 | 喜び状態の継続時間 (ms) |
| `kHappyR / G / B` | 255 / 80 / 120 | LED 暖色ピンクの RGB |

### 開発履歴メモ

最初は `m5stack/StackChan` (ESP-IDF v5 のフルファーム) から `SCSCL` / `PY32IOExpander`
ドライバを抜き出して PlatformIO + Arduino に手動移植する方針で着手したが、
`M5.In_I2C.readRegister8` がエラー時 0 を返す挙動や、PY32 アドレス 0x6F へのアクセスが
意図通り効かない問題で、サーボバスから Ping 応答を得られず詰まった。
その後の調査で **Arduino 向け公式 BSP `m5stack/StackChan-BSP`** の存在を発見し、
こちらに切り替えて一発で疎通成功した。教訓: K151 用に自前移植する前に BSP を探すこと。

## 画面で音量を調整する (volume control)

スピーカー音量を、再ビルド・再書き込みなしに **CoreS3 の LCD 画面だけ** で変えられる機能。

**LCD を長押し**すると Avatar の顔が消えて全画面の音量 UI が出ます。`−` / `+` ボタンのタップで
音量が増減し（その場で反映）、`OK` ボタンまたは無操作 8 秒で UI が閉じて顔に戻ります。
調整した音量は NVS に保存され、**電源を切っても次回起動時に復元**されます。

### 仕組み

- トリガー: `M5.Touch.getDetail().wasHold()` (LCD 静電容量タッチの長押し)
- 描画: UI 表示中は `avatar.stop()` で Avatar の描画タスクを終了させ、`M5.Display` へ直接 UI を描く。
  閉じるときに `avatar.start()` でタスクを作り直す (`suspend()/resume()` はネスト非対応で
  `setExpression()` と競合するため使わない)
- 反映: `−`/`+` タップで即 `M5.Speaker.setVolume()`。NVS への書き込みは閉じる瞬間に 1 回だけ
  (フラッシュ摩耗回避)
- 永続化: BSP の `Settings` クラス (NVS ラッパ) を再利用。namespace `"stackchan"` / key `"spk_vol"`。
  サーボ較正の `"servo"` namespace とは分離
- 他モジュールとの競合: UI 表示中は描画タスクが居ないため、`setExpression()` を呼ぶ
  `sleep_manager` / `pet_reaction` の tick と `/speak` 発話を停止 (`volume_control::is_active()` で判定)

### チューニング

`firmware/src/volume_control.cpp` 冒頭の定数:

| 定数 | 既定値 | 意味 |
|---|---|---|
| `kVolMin / kVolMax` | 40 / 255 | 音量レンジ (0 は無音なので下限を設けている) |
| `kVolStep` | 24 | 1 タップあたりの増減量 (約 9 段階) |
| `kVolDefault` | 200 | NVS 未保存時の初期値 (旧ハードコード値と同じ) |
| `kAutoCloseMs` | 8000 | 無操作で UI が自動的に閉じるまでの時間 (ms) |

## トラブルシュート

| 症状 | 確認ポイント |
| --- | --- |
| ビルドが進まない | 初回は本当に 10〜30 分かかる |
| `Error: Could not open port` | USB ケーブルがデータ通信対応か / 他アプリが占有していないか |
| `Failed uploading: timeout` | `upload_speed` を `921600` や `460800` に下げる |
| Stack-Chan の画面に IP が出ない | `WIFI_SSID` / `WIFI_PASS` のスペル、2.4GHz の AP か(5GHz だと ESP32 不可) |
| `stackchan.local` の curl が 5 秒かかる / タイムアウトする | `--ipv4` を付け忘れている。遅さの正体は AAAA (IPv6) クエリのタイムアウト待ち(ESP32 は AAAA に応答しない)。A レコード単独なら数 ms |
| 発話しない(ログが全行 `reason=unreachable`) | ① Stack-Chan の電源(アイドル 30 分で自動 powerOff する)② `tail /tmp/stackchan_notify.log` に `rediscover=ok ip=...` があれば次回から復旧済み ③ `STACKCHAN_MAC` が正しいか |
| `Operation not permitted` で curl が即時失敗 | macOS Sequoia 以降のローカルネットワーク権限。システム設定 → プライバシーとセキュリティ → ローカルネットワーク → Terminal / iTerm を ON |
| `/speak` で音が出ない | `firmware/lib/AquesTalkTTS/src/libaquestalk.a` が esp32s3 用か、CoreS3 のスピーカー音量が 0 になっていないか、AquesTalk が Ver.2.4.2 以上か。シリアルに `[tts] SetKoe err=105` が出ていたら `mode:free` で UTF-8 を渡している(ASCII の音素記号列が必要) |
| `/speak` が即 200 を返すのに無音 (`time_total` ~0.1s) | 音素記号列が AquesTalk に弾かれて `SetKoe` がエラー(シリアルに `[tts] SetKoe err=N`)→ `speakPhonemes()` が即 return。**文末 `!` が代表的な原因**。`.` / `?` / 記号なしに直す(上の **発話文言を変えたい** 参照) |
| 首が動かない | シリアルに `Servo ID: 1 get zero pos: ...` が出ていない → BSP の `M5StackChan.begin()` 内で PY32 IO Expander init がタイムアウトしている可能性。バッテリーの残量、CoreS3 とボディの結合、サーボコネクタを確認 |
| 首が傾いている | `examples/Servo/HomeCalibration` (BSP 同梱) で再キャリブレーション。詳細は **待機中の首振り (idle motion)** セクション参照 |
| 音は出るが「ヌヌヌ」 | 評価版の制限が出ている。固定文は kind ごとにナ行・マ行(N/M)を含まない設計なので発生しないはず。自由文(`mode:free`)で発生するなら製品版を購入 |
| hook が動かない | `chmod +x scripts/notify_stackchan.sh scripts/rediscover_stackchan.sh` 済みか、`~/.claude/settings.json` の `command` が絶対パスか、`scripts/config.local.sh` の `STACKCHAN_MAC` が正しいか |
| 二重に発話される | リポジトリ内 `.claude/settings.local.json` にも旧 hook が残っていないか確認 |

## 発話文言を変えたい

- 固定文(kind ごと)を変えたい: `firmware/src/main.cpp` の `MSG_DONE` / `MSG_CONFIRM` / `MSG_IDLE` を編集して焼き直し。**AquesTalk pico は ASCII の音素記号列のみ受け付ける**(`'` がアクセント核、`-` が長音、` ` が句切れ、`.` が文末下降、`?` が文末上昇)。詳細は [AquesTalk 音声記号列の仕様](https://www.a-quest.com/archive/manual/siyo_onseikigou.pdf)を参照。評価版ならナ行・マ行を避ける。
  - ⚠️ **文末に `!` は使えない**。AquesTalk は `!` を受け付けず `CAqTkPicoF_SetKoe` がエラーを返し、`speakPhonemes()` がそのまま return するため **無音のまま `/speak` は 200 を返す**(curl の `time_total` が ~0.1s と異様に速ければこれ)。テンションを出したいときは `?`(語尾上げ)や `-`(語尾伸ばし、例 `deki'tayo-`)で代用する。
- 漢字仮名混じり文を喋らせたい: AquesTalk 製品版ライセンス + 同梱辞書 `aq_dic/aqdic_m.bin` を LittleFS に焼き込み、`CAqK2R_Convert` + `CAqTkPicoF_SetKoe` に切り替える(将来の拡張)
- 通知種別ごとに別の文言を Mac 側で組み立てたい: `notify_stackchan.sh` の KIND 決定ロジックを拡張、または `mode:free` で音素記号列を直接渡す

## スコープ外 / 今後の拡張

- **サーボの高度な動作**: 待機中ランダム首振りと、頭を撫でられた時の喜び表現 (表情 + LED) は実装済み (**待機中の首振り (idle motion)** / **頭を撫でられたら喜ぶ (pet reaction)** 参照)。発話と同期したうなずきや、撫でられた時のサーボでの反応モーションは未実装。BSP の `M5StackChan.Motion.move()` をイベントに合わせて呼ぶ等で拡張可能。
- **複数 Stack-Chan への同報**: 単機運用なら HTTP 直叩きで十分。複数機なら MQTT ブローカ経由が候補。
- **出張先での通知**: 現状 Stack-Chan 不達時は無音(通知音はバナー側に一本化)。`say -v Kyoko` 等で Mac に喋らせたい場合は `notify_stackchan.sh` の unreachable 分岐に追加。

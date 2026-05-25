# claude-code-notifier

Claude Code のタスクが終わるたびに、Stack-chan が「サギョウ シュウリョウ」と日本語で発話する仕掛け。

## 仕組み

```
Claude Code (Mac)
  └─ Stop hook ─→ scripts/stop_notify.sh
                    └─ curl POST http://<Stack-chan IP>/speak
                                                  └─ AquesTalk ESP32 で発話
```

参考記事: [Claude Code / Codex の Stop Hook で Stack-chan を喋らせる(fujihide さん, Zenn)](https://zenn.dev/fujihide/articles/389892c9f0654a)
- 記事は Mac 側の Stop hook と curl ペイロードまでをカバー。
- Stack-chan 側の `/speak` HTTP サーバ実装はユーザー自作の前提なので、本実験ではそれを実装します。

## 必要なもの

- [スイッチサイエンス Stack-chan ボードキット 11129](https://www.switch-science.com/products/11129)(M5Stack Core2 + SG90×2 + 専用ボード)
- USB-C ケーブル
- 2.4GHz Wi-Fi 環境(Mac と Stack-chan は同一 LAN にいる必要あり)
- macOS + [Homebrew](https://brew.sh/)
- [Claude Code](https://docs.claude.com/claude-code)(インストール済み前提)
- [AquesTalk ESP32](https://www.a-quest.com/products/aquestalk_esp32.html) のライブラリ ZIP
  - **評価版は無償** だがナ行・マ行が「ヌ」になる制限あり。試用には十分
  - **継続利用・商用には有償ライセンスキー** が必要(個人購入可、価格は公式に問い合わせ)

## セットアップ手順

### 1. PlatformIO のインストール

```bash
brew install platformio
```

### 2. AquesTalk ESP32 の入手と配置

1. https://www.a-quest.com/download.html を開き、「AquesTalk ESP32」(または「AquesTalk ESP32 (Small辞書版)」)の Download から ZIP を取得。
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

### 4. ビルドして Stack-chan に書き込み

1. Stack-chan を USB-C で Mac に接続して電源 ON。
2. ポートを確認: `ls /dev/cu.*`(Core2 は `/dev/cu.usbserial-*` に出る)。
3. ビルド + 書き込み + モニタを一括実行:
   ```bash
   cd experiments/claude-code-notifier
   pio run -t upload -t monitor
   ```
4. **初回ビルドは依存ダウンロードとツールチェーン取得で 10〜30 分かかります**。
5. シリアルモニタに `[wifi] connected. IP=192.168.x.y` のように Stack-chan の IP が表示されるのでメモする。Stack-chan の画面にも IP が出ます。

### 5. Mac 側 hook 設定

```bash
cd experiments/claude-code-notifier/scripts
cp config.example.sh config.local.sh
# config.local.sh を編集して STACKCHAN_HOST に Stack-chan の IP を記入
```

`config.local.sh` も `.gitignore` 対象です。

リポジトリルートの `.claude/settings.local.json` に Stop hook 設定が入っています(こちらも gitignore 対象)。

## 動作確認

### 手動 curl テスト

```bash
# サーバ生存確認
curl -i http://<Stack-chan IP>/

# 任意の文(評価版だとナ行・マ行は「ヌ」化)
curl -i -X POST http://<Stack-chan IP>/speak \
  -H "Content-Type: application/json" \
  -d '{"mode":"free","text":"テスト"}'

# 固定文(評価版でも崩れない)
curl -i -X POST http://<Stack-chan IP>/speak \
  -H "Content-Type: application/json" \
  -d '{"mode":"fixed","text":"ignored"}'
```

### Stop hook 発火確認

別ターミナルで `tail -f /tmp/stackchan_stop.log` を流しておく。Claude Code でなんらかの作業を依頼し、セッションが終了するたびに 1 行追記され、Stack-chan が発話すれば成功です。

## トラブルシュート

| 症状 | 確認ポイント |
| --- | --- |
| ビルドが進まない | 初回は本当に 10〜30 分かかる |
| `Error: Could not open port` | USB ケーブルがデータ通信対応か / 他アプリが占有していないか |
| `Failed uploading: timeout` | `upload_speed` を 460800 や 115200 に下げる |
| Stack-chan の画面に IP が出ない | `WIFI_SSID` / `WIFI_PASS` のスペル、2.4GHz の AP か(5GHz だと ESP32 は不可) |
| `/speak` で音が出ない | `firmware/lib/AquesTalkTTS/` の中身が正しく配置されているか、Core2 の音量が 0 になっていないか |
| 音は出るが「ヌヌヌ」 | 評価版の制限が出ている。ナ行・マ行を含まない文を使うか、製品版を購入 |
| Stop hook が動かない | `chmod +x scripts/stop_notify.sh` 済みか、`.claude/settings.local.json` の `command` パスが絶対パスか、`scripts/config.local.sh` の `STACKCHAN_HOST` が正しいか |

## 発話文言を変えたい

- カタカナ音素列にしたい場合: `scripts/stop_notify.sh` の `MESSAGE` 変数を編集(評価版ならナ行・マ行を避ける)
- ファーム側の固定文を変えたい場合: `firmware/src/main.cpp` の `FIXED_MESSAGE` を編集して書き込み直し
- 漢字仮名混じり文を喋らせたい場合: AquesTalk ESP32 同梱の辞書 `aq_dic` を SPIFFS / LittleFS に焼き込み、`TTS.createK()` + `TTS.playK()` に切り替える(将来の拡張)

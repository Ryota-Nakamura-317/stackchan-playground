# CLAUDE.md

## プロジェクト概要

Stack-chan（M5Stack ベースの手のひらサイズコミュニケーションロボット）関連の開発プロジェクト。Macで開発を行う。

## 意識すること
- ユーザー側で作業が必要な場合は、順を追って、できる限り詳細に、最新情報をもとにした手順を示すこと。

## 購入した製品

https://docs.m5stack.com/ja/StackChan
こちらのページの製品と同製品。ページ内の資料も適宜参考にすること。

## リポジトリ運用

- GitHub 上は `stackchan-playground`（public）として公開。
- 新しい試行は `experiments/<名前>/` に独立したディレクトリを切る。各実験は独自の `README.md` / `platformio.ini` / `firmware/` / `scripts/` を持つ。
- 秘密情報（Wi-Fi SSID/PW、APIキー、ライセンスキー、Stack-chan の IP）は絶対にコミットしない。`.gitignore` で除外し、`*.example.*` だけリポジトリに残す方針。
- AquesTalk pico for ESP32 のバイナリは再配布不可。各実験の README に「ユーザー自身で a-quest.com から取得」と案内し、リポジトリには同梱しない。

## ハードウェア構成

- ボード: **M5Stack Core2**（スイッチサイエンス 11129 同梱）
- サーボ: SG90 互換 × 2（パン・チルト。ピン割り当ては各実験の `main.cpp` を参照）
- スピーカー: Core2 内蔵（M5Unified の Speaker API 経由）
- ネットワーク: Wi-Fi 2.4GHz

## 開発環境（確定）

- ホスト: macOS
- ビルド: **PlatformIO**（`brew install platformio`）
- 言語: Arduino C++（arduino-esp32 framework）
- 主要ライブラリ:
  - `m5stack/M5Unified`
  - `meganetaaan/stackchan-arduino`（Avatar、サーボ）
  - `bblanchon/ArduinoJson`
  - `AquesTalkTTS-esp32`（オフライン日本語 TTS、個人利用無償、手動配置の可能性あり）

## ビルド・書き込み・モニタコマンド

各実験ディレクトリに `cd` してから実行する。

```bash
# 依存解決 + ビルド
pio run

# 書き込み（USB-C 接続中、Core2 を ON）
pio run -t upload

# シリアルモニタ（起動時に IP アドレスが表示される）
pio device monitor

# 書き込み + そのままモニタ
pio run -t upload -t monitor
```

## シリアル設定

- ボーレート: **115200**
- macOS のポート: `/dev/cu.usbserial-*`（Core2 は CP2104 系。接続後 `ls /dev/cu.*` で確認）

## Claude Code 連携

- Stop hook: リポジトリルートの `.claude/settings.local.json`（gitignore 対象）で `experiments/claude-code-notifier/scripts/stop_notify.sh` を起動。
- Stack-chan の IP は `experiments/claude-code-notifier/scripts/config.local.sh` の `STACKCHAN_HOST` に設定（gitignore 対象）。

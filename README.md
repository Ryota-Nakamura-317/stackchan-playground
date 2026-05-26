# stackchan-playground

[Stack-chan](https://github.com/stack-chan/stack-chan)(M5Stack ベースの手のひらサイズコミュニケーションロボット)を使った試行錯誤と実験の記録。Stack-chan を初めて触る人の参考になることを目指しています。

## 対象ハードウェア

- **M5Stack 公式 Stack-Chan(K151系、CoreS3 ベース)**
  - ESP32-S3 / 16MB Flash / 8MB PSRAM
  - 2.0インチ IPS LCD + 静電容量タッチ
  - AW88298 I2S スピーカー / デュアルマイク + ES7210
  - UART フィードバックサーボ × 2(水平 360° / 垂直 90°)
  - WS2812C RGB LED × 12 / 静電容量タッチパネル × 3
  - 公式ドキュメント: https://docs.m5stack.com/ja/StackChan

> ⚠️ スイッチサイエンスの旧 Stack-chan ボードキット 11129(M5Stack Core2 + SG90×2)とは別商品です。本リポジトリの実験は CoreS3 を前提に書かれています。

## 開発環境

- macOS
- [PlatformIO](https://platformio.org/)(CLI / VS Code 拡張)
- Arduino C++(arduino-esp32 framework)

## 実験一覧

各実験は `experiments/<名前>/` に独立して配置されています。実験ごとに独自の `README.md`、`platformio.ini`、配線図、ビルド手順を持ちます。

| 実験 | 概要 |
| --- | --- |
| [claude-code-notifier](experiments/claude-code-notifier/) | [Claude Code](https://claude.com/claude-code) のセッション完了(Stop hook)と権限プロンプト(Notification hook)を Stack-Chan が日本語で発話。Stack-Chan 不在時は `afplay` にフォールバック。mDNS + HTTP + AquesTalk ESP32 Ver.2.4.2+ |

## ディレクトリ構造ポリシー

新しい試行は `experiments/<名前>/` に独立したディレクトリを切って追加します。これにより各実験を独立してビルド・実行でき、互いに干渉しません。

```
experiments/
└── claude-code-notifier/
    ├── README.md
    ├── platformio.ini
    ├── firmware/
    └── scripts/
```

## 参考リンク

- [Stack-chan 公式リポジトリ](https://github.com/stack-chan/stack-chan)
- [meganetaaan/stackchan-arduino](https://github.com/meganetaaan/stack-chan-arduino)
- [M5Unified](https://github.com/m5stack/M5Unified)
- [AquesTalk ESP32](https://www.a-quest.com/products/aquestalk_esp32.html)(Ver.2.4.2 以降が CoreS3 / ESP32-S3 に正式対応。評価版は無償だがナ行・マ行が「ヌ」になる制限あり。継続利用・商用は有償ライセンスキーが必要。本リポジトリには同梱しません)

## ライセンス

本リポジトリのソースコードは [MIT License](LICENSE) の下で公開しています。

ただし、各実験で利用しているサードパーティライブラリ(M5Unified、stackchan-arduino、AquesTalk ESP32 など)はそれぞれの提供元ライセンスに従ってください。とくに **AquesTalk ESP32 のバイナリは再配布できない** ため、本リポジトリには含まれていません。各自で公式から入手してください(手順は実験ごとの README を参照)。

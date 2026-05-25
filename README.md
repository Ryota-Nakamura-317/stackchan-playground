# stackchan-playground

[Stack-chan](https://github.com/stack-chan/stack-chan)(M5Stack ベースの手のひらサイズコミュニケーションロボット)を使った試行錯誤と実験の記録。Stack-chan を初めて触る人の参考になることを目指しています。

## 対象ハードウェア

- [スイッチサイエンス Stack-chan ボードキット(製品番号 11129)](https://www.switch-science.com/products/11129)同梱品
  - M5Stack Core2
  - SG90 互換サーボ × 2(パン・チルト)
  - 専用ボード・ケース・ねじ類

## 開発環境

- macOS
- [PlatformIO](https://platformio.org/)(CLI / VS Code 拡張)
- Arduino C++(arduino-esp32 framework)

## 実験一覧

各実験は `experiments/<名前>/` に独立して配置されています。実験ごとに独自の `README.md`、`platformio.ini`、配線図、ビルド手順を持ちます。

| 実験 | 概要 |
| --- | --- |
| [claude-code-notifier](experiments/claude-code-notifier/) | [Claude Code](https://claude.com/claude-code) のタスク完了時に Stack-chan が「作業が終わりました」と日本語で発話する。Stop hook + HTTP POST + AquesTalk pico for ESP32 |

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
- [AquesTalk pico for ESP32](https://www.a-quest.com/products/aquestalk_picoesp32.html)(個人利用無償、本リポジトリには同梱しません)

## ライセンス

本リポジトリのソースコードは [MIT License](LICENSE) の下で公開しています。

ただし、各実験で利用しているサードパーティライブラリ(M5Unified、stackchan-arduino、AquesTalk pico for ESP32 など)はそれぞれの提供元ライセンスに従ってください。とくに **AquesTalk pico for ESP32 のバイナリは再配布できない** ため、本リポジトリには含まれていません。各自で公式から入手してください(手順は実験ごとの README を参照)。

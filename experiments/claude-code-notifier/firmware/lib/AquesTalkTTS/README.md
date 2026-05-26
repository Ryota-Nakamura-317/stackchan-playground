# AquesTalk ESP32 の配置場所

このディレクトリには [AquesTalk ESP32](https://www.a-quest.com/products/aquestalk_esp32.html) を **ユーザー自身が** 配置してください。ライセンス上、本リポジトリには同梱できません。

> **ディレクトリ名について**: 互換性と歴史的経緯から `AquesTalkTTS/` というディレクトリ名を採用しています(AquesTalk 公式が examples に同梱しているラッパークラスの名前)。実体は AquesTalk ESP32(Ver.2.x)です。ただし本実験ではこのラッパー (`AquesTalkTTS.h/.cpp`) は **使いません**(M5StampS3 用 I2S ピン固定で CoreS3 では音が出ないため)。低レベル C API `aquestalk.h` を直接呼んで M5Unified の `M5.Speaker.playRaw()` に PCM を流し込む構成にしています。

> **バージョン要件**: 本実験は **M5Stack CoreS3 (ESP32-S3)** を対象にしているため、ESP32-S3 に正式対応した **Ver.2.4.2 以降**(2024/10 リリース)が必須です。それより古い AquesTalk ESP32 は ESP32-S3 で動作しません。

## ライセンスの注意

- **評価版(無償)**: ナ行・マ行の音韻がすべて「ヌ」になる制限があります。試用・評価目的のみ。
- **製品版(有償)**: ライセンスキーを購入して `secrets.h` に書き込むと制限が外れます。個人購入も可能ですが、価格・申込方法は AquesTalk 公式に問い合わせてください。

## 入手と配置の手順

1. https://www.a-quest.com/download.html を開く。
2. ページ内の「AquesTalk ESP32」(Ver.2.4.2 以降)の Download ボタンから ZIP を取得。
3. ZIP を展開すると、`aquestalk-esp32/` のようなフォルダができます。中身の例:
   ```
   aquestalk-esp32/
   ├── src/
   │   ├── aquestalk.h                  ← 本体ヘッダ(必要)
   │   ├── esp32/libaquestalk.a         ← 旧 ESP32 用スタティックライブラリ
   │   ├── esp32s2/libaquestalk.a       ← ESP32-S2 用
   │   └── esp32s3/libaquestalk.a       ← ★ CoreS3 (ESP32-S3) ではこれを使う
   ├── aq_dic/                          ← 漢字→かな変換 大辞書(本実験では未使用)
   ├── examples/                        ← 全部不要(M5StampS3 用ラッパーなどが入っている)
   └── library.properties
   ```
4. このディレクトリ(`firmware/lib/AquesTalkTTS/`)の **`src/` 直下** に以下 **2 ファイル** をコピーしてください:
   ```
   firmware/lib/AquesTalkTTS/
   ├── library.json                     ← 本リポジトリ同梱(srcDir=src, libArchive=false)
   ├── README.md                        ← 本ファイル
   └── src/
       ├── aquestalk.h                  ← aquestalk-esp32/src/aquestalk.h
       └── libaquestalk.a               ← aquestalk-esp32/src/esp32s3/libaquestalk.a (★ esp32s3 を選ぶ)
   ```
   コマンド例(`firmware/lib/AquesTalkTTS/` 直下に `aquestalk-esp32/` を展開した状態から):
   ```bash
   cd firmware/lib/AquesTalkTTS
   mkdir -p src
   cp aquestalk-esp32/src/aquestalk.h            src/
   cp aquestalk-esp32/src/esp32s3/libaquestalk.a src/
   rm -rf aquestalk-esp32   # 元 ZIP 展開フォルダはもう不要(残しても library.json が src/ だけ見るので無害)
   ```
5. `library.json` は本リポジトリに同梱済みで、`src/` を PlatformIO のビルド対象にします。`libaquestalk.a` は SCons の流儀で `.a` 拡張子付きの `-l:` 指定が通らないため、`scripts/link_aquestalk.py` (extra_scripts) で SCons の `LIBS` に絶対パスで Append する仕組みになっています。追加作業はありません。
6. 漢字仮名混じり文を喋らせたい場合は、別途 `aq_dic/aqdic_m.bin` を LittleFS に焼き込み、`CAqK2R_Convert` で音素列に変換してから `CAqTkPicoF_SetKoe` に渡します(本実験は音素記号列モードで完結するため未使用)。

## ライセンスキー

製品版を購入した場合のみ、メールで送られてくるキーを `firmware/include/secrets.h` の `AQUESTALK_LICENSE_KEY` に記入してください。評価版で試す場合は `secrets.example.h` のダミー値のままで動作します(ただしナ行・マ行は「ヌ」化)。

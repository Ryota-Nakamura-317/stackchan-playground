# AquesTalk ESP32 の配置場所

このディレクトリには [AquesTalk ESP32](https://www.a-quest.com/products/aquestalk_esp32.html) を **ユーザー自身が** 配置してください。ライセンス上、本リポジトリには同梱できません。

> **ディレクトリ名について**: 互換性と歴史的経緯から `AquesTalkTTS/` というディレクトリ名を採用しています(AquesTalk 公式が examples に同梱しているラッパークラスの名前)。実体は AquesTalk ESP32(Ver.2.x)です。

## ライセンスの注意

- **評価版(無償)**: ナ行・マ行の音韻がすべて「ヌ」になる制限があります。試用・評価目的のみ。
- **製品版(有償)**: ライセンスキーを購入して `secrets.h` に書き込むと制限が外れます。個人購入も可能ですが、価格・申込方法は AquesTalk 公式に問い合わせてください。

## 入手と配置の手順

1. https://www.a-quest.com/download.html を開く。
2. ページ内の「AquesTalk ESP32」(または「AquesTalk ESP32 (Small辞書版)」)の Download ボタンから ZIP を取得。
3. ZIP を展開すると、`AquesTalk-ESP32-Ver.X.X.X/` のようなフォルダができます。中身の例:
   ```
   AquesTalk-ESP32-Ver.2.4.4/
   ├── aquestalk.h               ← 本体ヘッダ
   ├── libaquestalk.a            ← 本体スタティックライブラリ
   ├── aq_dic/                   ← 漢字→かな変換用 大辞書(漢字対応 createK() で使用)
   ├── examples/
   │   ├── hello_aquestalk/      ← 音素列入力サンプル
   │   ├── hello_aquestalk_tts/  ← ラッパー AquesTalkTTS のサンプル(これを最も参考にする)
   │   └── ...
   └── ...
   ```
4. このディレクトリ(`firmware/lib/AquesTalkTTS/`)に、次のような構成でコピー:
   ```
   firmware/lib/AquesTalkTTS/
   ├── src/
   │   ├── aquestalk.h
   │   ├── libaquestalk.a
   │   ├── AquesTalkTTS.h        ← examples/hello_aquestalk_tts/ からコピー
   │   └── AquesTalkTTS.cpp      ← examples/hello_aquestalk_tts/ からコピー(あれば)
   ├── aq_dic/                   ← 辞書フォルダごとコピー(漢字対応に必要)
   └── library.json
   ```
5. PlatformIO に認識させるため、`library.json` を `firmware/lib/AquesTalkTTS/library.json` として作成:
   ```json
   {
     "name": "AquesTalkTTS",
     "version": "1.0.0",
     "build": {
       "flags": ["-Lsrc", "-l:libaquestalk.a"],
       "srcDir": "src",
       "srcFilter": ["+<*.h>", "+<*.cpp>"]
     }
   }
   ```

## ライセンスキー

製品版を購入した場合のみ、メールで送られてくるキーを `firmware/include/secrets.h` の `AQUESTALK_LICENSE_KEY` に記入してください。評価版で試す場合は `secrets.example.h` のダミー値のままで動作します(ただしナ行・マ行は「ヌ」化)。

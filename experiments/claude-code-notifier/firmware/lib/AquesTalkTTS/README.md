# AquesTalk pico for ESP32 の配置場所

このディレクトリには [AquesTalk pico for ESP32](https://www.a-quest.com/products/aquestalk_picoesp32.html) を **ユーザー自身が** 配置してください。ライセンス上、本リポジトリには同梱できません。

## 入手と配置の手順

1. https://www.a-quest.com/products/aquestalk_picoesp32.html にアクセスし、個人利用無償の規約に同意してダウンロード。
2. アーカイブを展開し、以下のような構造になるように本ディレクトリ配下にコピー:
   ```
   firmware/lib/AquesTalkTTS/
   ├── src/
   │   ├── AquesTalkTTS.h
   │   └── libAquesTalkTTSEsp32.a
   └── library.json   ← 必要なら下記を作成
   ```
3. PlatformIO に認識させるため、`library.json` を `firmware/lib/AquesTalkTTS/library.json` として作成:
   ```json
   {
     "name": "AquesTalkTTS",
     "version": "1.0.0",
     "build": {
       "flags": ["-Lsrc", "-l:libAquesTalkTTSEsp32.a"],
       "srcDir": "src",
       "srcFilter": ["+<*.h>"]
     }
   }
   ```

## ライセンスキー

ダウンロード時に送られてくるライセンスキー（または評価用キー）を `firmware/include/secrets.h` の `AQUESTALK_LICENSE_KEY` に記入してください。

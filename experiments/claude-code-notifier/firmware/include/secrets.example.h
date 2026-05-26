#pragma once

// このファイルを secrets.h にコピーして実値を記入してください。
//   cp secrets.example.h secrets.h
// secrets.h は .gitignore でコミットされないようになっています。

// Wi-Fi 接続情報(2.4GHz のみ。CoreS3 でも 5GHz は不可)
#define WIFI_SSID            "your-ssid"
#define WIFI_PASS            "your-password"

// AquesTalk ESP32 のライセンスキー
// 入手元: https://www.a-quest.com/products/aquestalk_esp32.html
//
// Ver.2.4.2 以降が ESP32-S3 / M5Stack CoreS3 に正式対応しています。
// 評価版で試す場合は下記ダミー値のままで OK ですが、ナ行・マ行の音韻が
// すべて「ヌ」になる制限があります。継続利用・商用利用には有償の製品版
// ライセンスキーを購入してここに記入してください(購入方法は公式に問い合わせ)。
#define AQUESTALK_LICENSE_KEY "XXX-XXX-XXX"

#pragma once

// このファイルを secrets.h にコピーして実値を記入してください。
//   cp secrets.example.h secrets.h
// secrets.h は .gitignore でコミットされないようになっています。

// Wi-Fi 接続情報
#define WIFI_SSID            "your-ssid"
#define WIFI_PASS            "your-password"

// AquesTalk ESP32 のライセンスキー
// 入手元: https://www.a-quest.com/products/aquestalk_esp32.html
//
// 評価版で試す場合は下記ダミー値のままで OK ですが、ナ行・マ行の音韻が
// すべて「ヌ」になる制限があります。継続利用や商用利用には有償の製品版
// ライセンスキーを購入し、ここに記入してください(購入方法は公式に問い合わせ)。
#define AQUESTALK_LICENSE_KEY "XXX-XXX-XXX"

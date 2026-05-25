// Claude Code Notifier — Stack-chan side firmware
//
// HTTP POST /speak を受け取り、AquesTalk ESP32 で発話するだけの最小サーバ。
// payload 例: {"mode":"fixed","text":"無視される"}
//             {"mode":"free","text":"サギョウ シュウリョウ"}
//   - mode == "fixed":     ファーム側に焼き込まれた固定文を発話(text は無視)
//   - mode == "free" 他:   payload の text フィールドをそのまま発話
//
// AquesTalk ESP32 の API メモ:
//   - TTS.createK("XXX-XXX-XXX", "/aq_dic")  漢字対応辞書つき初期化
//   - TTS.create("XXX-XXX-XXX")              音素列のみ(辞書不要)初期化
//   - TTS.playK("漢字仮名混じり文", speed)   漢字 → 音素変換 + 再生
//   - TTS.play("オンソレツ", speed)           音素列を直接再生
//   - 評価版だとナ行・マ行が「ヌ」になる制限あり。
//     固定文を「サギョウ シュウリョウ」のようにマ・ナ行を含まない言葉にすると評価版でも崩れない。
//
// 注意:
//   - ラッパー AquesTalkTTS.h は AquesTalk ESP32 同梱の examples/hello_aquestalk_tts から
//     firmware/lib/AquesTalkTTS/src/ にコピーする(詳細は firmware/lib/AquesTalkTTS/README.md)。
//   - 漢字対応で動かしたい場合は SPIFFS / LittleFS に aq_dic を書き込んでパスを渡す。
//     最小構成では音素列入力(createK ではなく create + playK の音素列モード)で十分。
//   - サーボピンは実機の配線に応じて調整すること。

#include <M5Unified.h>
#include <Avatar.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <AquesTalkTTS.h>

#include "secrets.h"

using namespace m5avatar;

namespace {

constexpr int SERVO_PIN_X = 33;  // パン(左右)
constexpr int SERVO_PIN_Y = 32;  // チルト(上下)

// AquesTalk 評価版でも崩れないようナ行・マ行を含まない固定文。
// 製品版ライセンスを購入したら好きな文に書き換えて OK。
constexpr char FIXED_MESSAGE[] = "サギョウ シュウリョウ";

Avatar avatar;
WebServer server(80);

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("[wifi] connecting");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print('.');
    if (millis() - start > 30000) {
      Serial.println("\n[wifi] timeout, restarting");
      ESP.restart();
    }
  }
  Serial.println();
  Serial.printf("[wifi] connected. IP=%s\n", WiFi.localIP().toString().c_str());
  avatar.setSpeechText(WiFi.localIP().toString().c_str());
}

void handleRoot() {
  server.send(200, "text/plain", "stackchan claude-code-notifier ready\n");
}

void handleSpeak() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "missing body\n");
    return;
  }
  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "invalid json\n");
    return;
  }

  const char* mode = doc["mode"] | "fixed";
  const char* text = doc["text"] | "";
  const char* message = (strcmp(mode, "fixed") == 0) ? FIXED_MESSAGE : text;

  Serial.printf("[speak] mode=%s message=%s\n", mode, message);
  avatar.setExpression(Expression::Happy);
  avatar.setSpeechText("(speaking)");

  // 音素列入力モードで再生(漢字対応の playK は辞書 aq_dic の配置が必要)
  TTS.play(message, 80);

  avatar.setExpression(Expression::Neutral);
  avatar.setSpeechText("");
  server.send(200, "application/json", "{\"ok\":true}\n");
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Speaker.setVolume(200);
  Serial.begin(115200);

  avatar.init();

  // AquesTalk ESP32 初期化。ライセンスキーは secrets.h で定義。
  // 評価版で試す場合は AQUESTALK_LICENSE_KEY をダミー値のままにしておく(ナ行・マ行が「ヌ」になる)。
  int tts_err = TTS.create(AQUESTALK_LICENSE_KEY);
  if (tts_err) {
    Serial.printf("[tts] init failed: %d\n", tts_err);
  }

  connectWiFi();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/speak", HTTP_POST, handleSpeak);
  server.begin();
  Serial.println("[http] server listening on :80");
}

void loop() {
  M5.update();
  server.handleClient();
  delay(1);
}

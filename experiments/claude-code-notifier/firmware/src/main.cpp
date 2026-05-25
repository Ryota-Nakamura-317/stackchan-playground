// Claude Code Notifier — Stack-chan side firmware
//
// HTTP POST /speak を受け取り、AquesTalk pico for ESP32 で発話するだけの最小サーバ。
// payload 例: {"mode":"fixed","text":"マックノ クロードノ サギョウガ オワリマシタ."}
//   - mode == "fixed" の場合: ファーム側固定の音素列を発話（text は無視）。
//   - それ以外:              text フィールドをそのまま AquesTalk の音素列として再生。
//
// 注意:
//   - AquesTalk の入力は「カタカナ + 記号」の音素表記。漢字仮名混じりは正しく発音されない。
//   - 漢字→音素変換は ESP32 上では現実的でないため、Mac 側スクリプトでカタカナ列を渡す前提。
//   - サーボピンは Stack-chan ボード（スイッチサイエンス 11129）の標準配線に合わせている。
//     実機の配線に応じて SERVO_PIN_X / SERVO_PIN_Y を調整すること。

#include <M5Unified.h>
#include <Avatar.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <AquesTalkTTS.h>

#include "secrets.h"

using namespace m5avatar;

namespace {

constexpr int SERVO_PIN_X = 33;  // パン（左右）
constexpr int SERVO_PIN_Y = 32;  // チルト（上下）

constexpr char FIXED_PHONETIC[] = "マックノ クロードノ サギョウガ オワリマシタ.";

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
  const char* phonetic = (strcmp(mode, "fixed") == 0) ? FIXED_PHONETIC : text;

  Serial.printf("[speak] mode=%s phonetic=%s\n", mode, phonetic);
  avatar.setExpression(Expression::Happy);
  avatar.setSpeechText("(speaking)");

  TTS.play(phonetic, 80);

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

  // AquesTalk pico for ESP32 初期化。ライセンスキーは secrets.h で定義。
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

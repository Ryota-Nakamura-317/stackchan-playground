// Claude Code Notifier — Stack-Chan (CoreS3 / K151) firmware
//
// HTTP で通知を受け、AquesTalk ESP32 で発話する最小サーバ。
// 出力は M5Unified の M5.Speaker(AW88298 codec)経由。AquesTalk 同梱の
// ラッパー AquesTalkTTS は M5StampS3 用 I2S ピン直叩きで CoreS3 では音が
// 出ないため使わず、低レベル C API (aquestalk.h) を直接呼び、PCM を
// M5.Speaker.playRaw() に流し込む方式に変更している。
//
// エンドポイント:
//   GET  /         → ステータステキスト(デバッグ用)
//   GET  /healthz  → {"ok":true} 即返し(Mac 側スクリプトの疎通判定用)
//   POST /speak    → JSON ペイロード
//     {"mode":"notify","kind":"done"}    → MSG_DONE を発話 (Stop hook)
//     {"mode":"notify","kind":"confirm"} → MSG_CONFIRM を発話 (Notification hook)
//     {"mode":"notify","kind":"idle"}    → MSG_IDLE を発話
//     {"mode":"fixed","kind":"done"}     → notify と同じ扱い(後方互換)
//     {"mode":"free","text":"オンソレツ"} → text を音素列として発話

#include <M5Unified.h>
#include <M5StackChan.h>
#include <Avatar.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>

extern "C" {
#include <aquestalk.h>
}

#include "secrets.h"
#include "idle_motion.h"
#include "sleep_manager.h"
#include "pet_reaction.h"
#include "volume_control.h"

using namespace m5avatar;

namespace {

constexpr char MDNS_HOSTNAME[] = "stackchan";

// AquesTalk pico は ASCII の音素記号列のみ受け付ける(漢字仮名混じり文は createK+playK+辞書が必要)。
// 評価版でナ行・マ行が「ヌ」化する制限があるため、いずれの固定文も N/M を含まないよう構成する。
//   '  : アクセント核    -  : 長音(または母音重ね)    .  : 文末下降    ?  : 文末上昇    空白: 句切れ
constexpr char MSG_DONE[]    = "deki'tayo?";            // できたよ?(語尾上げ)
constexpr char MSG_CONFIRM[] = "kyo'ka kuda'sai.";       // 許可ください
constexpr char MSG_IDLE[]    = "tsugi'wa?";              // 次は?

// AquesTalk の合成は 8kHz / 16-bit / mono。短い固定文 (~1-2 秒) に十分な領域を確保
constexpr uint16_t AQ_FRAME_SAMPLES = 32;            // 1 フレームあたりサンプル数 (30-320)
constexpr size_t   AQ_PCM_BUF_SAMPLES = 32 * 1024;   // 約 4 秒 @ 8kHz

Avatar avatar;
WebServer server(80);

// 起動時に吹き出しへ出す IP は、確認用に数秒だけ表示して自動で消す。
constexpr uint32_t kIpBalloonMs = 5000;
uint32_t s_ip_shown_at_ms    = 0;
bool     s_ip_balloon_active = false;

uint32_t aq_workbuf[AQ_SIZE_WORKBUF];
int16_t* pcm_buf = nullptr;

const char* messageForKind(const char* kind) {
  if (kind == nullptr) return MSG_DONE;
  if (strcmp(kind, "confirm") == 0) return MSG_CONFIRM;
  if (strcmp(kind, "idle") == 0)    return MSG_IDLE;
  return MSG_DONE;
}

// 吹き出し(画面表示)用の文言。AquesTalk と違い M5GFX の文字描画なので
// 「!」を含む任意の日本語を表示できる(音声側の MSG_* とは別物)。
const char* balloonForKind(const char* kind) {
  if (kind == nullptr) return "できたよ！";
  if (strcmp(kind, "confirm") == 0) return "許可ください";
  if (strcmp(kind, "idle") == 0)    return "次は？";
  return "できたよ！";
}

void speakPhonemes(const char* koe) {
  if (pcm_buf == nullptr) {
    Serial.println("[tts] pcm_buf not allocated");
    return;
  }
  uint8_t err = CAqTkPicoF_SetKoe(reinterpret_cast<const uint8_t*>(koe), 100, 0xffffU);
  if (err) {
    Serial.printf("[tts] SetKoe err=%u\n", err);
    return;
  }
  size_t total = 0;
  while (total + AQ_FRAME_SAMPLES <= AQ_PCM_BUF_SAMPLES) {
    uint16_t len = 0;
    uint8_t r = CAqTkPicoF_SyntheFrame(reinterpret_cast<short*>(&pcm_buf[total]), &len);
    total += len;
    if (r == 1) break;       // EOD
    if (r >  1) {            // error
      Serial.printf("[tts] SyntheFrame err=%u\n", r);
      break;
    }
  }
  M5.Speaker.playRaw(pcm_buf, total, 8000, false, 1, -1, true);
  while (M5.Speaker.isPlaying()) {
    delay(10);
  }
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(MDNS_HOSTNAME);
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
  s_ip_shown_at_ms    = millis();
  s_ip_balloon_active = true;
}

void startMdns() {
  if (MDNS.begin(MDNS_HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[mdns] %s.local advertised\n", MDNS_HOSTNAME);
  } else {
    Serial.println("[mdns] begin failed");
  }
}

void handleRoot() {
  String body = "stackchan claude-code-notifier ready\n";
  body += "ip=";
  body += WiFi.localIP().toString();
  body += "\nhost=";
  body += MDNS_HOSTNAME;
  body += ".local\n";
  server.send(200, "text/plain", body);
}

void handleHealthz() {
  server.send(200, "application/json", "{\"ok\":true}\n");
}

void handleSpeak() {
  // 音量調整 UI 表示中は Avatar の描画タスクを止めているため、発話で
  // setExpression() を呼ぶと不正タスクハンドルに触れる。ビジーとして弾く。
  if (volume_control::is_active()) {
    server.send(409, "text/plain", "busy (volume UI)\n");
    return;
  }
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "missing body\n");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "invalid json\n");
    return;
  }

  sleep_manager::notify_activity();

  const char* mode = doc["mode"] | "notify";
  const char* kind = doc["kind"] | "done";
  const char* text = doc["text"] | "";

  const char* message;
  if (strcmp(mode, "free") == 0) {
    message = text;
  } else {
    message = messageForKind(kind);
  }

  Serial.printf("[speak] mode=%s kind=%s message=%s\n", mode, kind, message);
  idle_motion::set_enabled(false);
  avatar.setExpression(Expression::Happy);
  // free モードは音素記号列が来るため吹き出しには出さず固定表示にする。
  const char* balloon = (strcmp(mode, "free") == 0) ? "(speaking)" : balloonForKind(kind);
  avatar.setSpeechText(balloon);

  speakPhonemes(message);

  avatar.setExpression(Expression::Neutral);
  avatar.setSpeechText("");
  idle_motion::set_enabled(true);
  server.send(200, "application/json", "{\"ok\":true}\n");
}

}  // namespace

void setup() {
  // M5StackChan.begin() の中で M5.begin() + PY32 (VM EN) + SCS バス + Motion
  // + INA226 が一括初期化される。M5.config() で何か追加設定したい場合は
  // 別途 M5.config() を呼んでから M5.begin(cfg) → M5StackChan.begin() の順に
  // することも可能だが、本ファームは標準設定で問題ないため一発で済ませる。
  M5StackChan.begin();
  Serial.begin(115200);

  avatar.init();

  // 吹き出しに日本語を表示するため日本語フォントを設定(未設定だと ASCII のみ)。
  avatar.setSpeechFont(&fonts::lgfxJapanGothicP_16);

  // AquesTalk pico 初期化。AQUESTALK_LICENSE_KEY は secrets.h で定義。
  // 評価版で試す場合は "XXX-XXX-XXX" ダミー値で OK(ナ行・マ行は「ヌ」化)。
  uint8_t aq_err = CAqTkPicoF_Init(aq_workbuf, AQ_FRAME_SAMPLES, AQUESTALK_LICENSE_KEY);
  if (aq_err) {
    Serial.printf("[tts] CAqTkPicoF_Init failed: %u\n", aq_err);
  }

  // PCM バッファ確保(64KB)。PSRAM があれば PSRAM 優先で確保
  pcm_buf = static_cast<int16_t*>(
      heap_caps_malloc(AQ_PCM_BUF_SAMPLES * sizeof(int16_t),
                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (pcm_buf == nullptr) {
    pcm_buf = static_cast<int16_t*>(
        heap_caps_malloc(AQ_PCM_BUF_SAMPLES * sizeof(int16_t), MALLOC_CAP_8BIT));
  }
  if (pcm_buf == nullptr) {
    Serial.println("[tts] pcm_buf alloc failed");
  }

  connectWiFi();
  startMdns();

  server.on("/",        HTTP_GET,  handleRoot);
  server.on("/healthz", HTTP_GET,  handleHealthz);
  server.on("/speak",   HTTP_POST, handleSpeak);
  server.begin();
  Serial.println("[http] server listening on :80");

  // BSP の Motion は M5StackChan.begin() の最後で goHome() (= 中央へ移動)
  // を呼んでいるので、idle motion は init から少し時間を置いて開始する。
  idle_motion::init();
  sleep_manager::init(avatar);
  pet_reaction::init(avatar);
  // 音量を NVS から復元して M5.Speaker.setVolume() を適用 (avatar.init() の後)
  volume_control::init(avatar);
}

void loop() {
  M5StackChan.update();
  server.handleClient();
  volume_control::tick(millis());
  // UI 表示中は Avatar 描画タスクが止まっており、setExpression() を呼ぶ他モジュールを
  // 走らせると不正タスクハンドルに触れる。UI が閉じている間だけ tick する。
  if (!volume_control::is_active()) {
    // 起動時の IP 吹き出しを数秒で自動クリア (UI 表示中は Avatar タスクが止まっているので除外)
    if (s_ip_balloon_active && millis() - s_ip_shown_at_ms > kIpBalloonMs) {
      avatar.setSpeechText("");
      s_ip_balloon_active = false;
    }
    sleep_manager::tick(millis());
    idle_motion::tick(millis());
    pet_reaction::tick(millis());
  }
  delay(1);
}

#!/usr/bin/env bash
# Claude Code の Stop / Notification hook から呼ばれるエントリスクリプト。
# 第1引数で event 種別を受ける: stop | notification
#
# 動作:
#   1. stdin の hook JSON を読み、Notification なら message から kind を推定
#   2. Stack-Chan の IP を3層で解決 (キャッシュ → mDNS → ARP)。
#      詳細は lib_stackchan.sh の resolve_ip()
#      (各層の /healthz 疎通確認は 3 秒タイムアウト。同一 IP は再 probe しない)
#   3. 解決できたら POST /speak をバックグラウンドで投げ、成功 IP をキャッシュ
#   4. 全滅なら何もしない(音は鳴らさない。通知音はバナー側の Glass に一本化)。
#      代わりに rediscover_stackchan.sh をデタッチ起動し、次回 hook までに
#      ping スイープ + ARP で IP を再探索してキャッシュを自己修復する
#   5. 標準出力には何も出さない(Stop hook で stdout を返すと Claude が
#      JSON 出力と誤解する可能性があるため)。ログは /tmp/stackchan_notify.log
#
# 注意: curl には必ず --ipv4 を付けること。macOS の .local 解決は AAAA (IPv6)
# クエリの 5 秒タイムアウトを待つため、付けないと mDNS 名がほぼ確実に
# hook の時間予算を食い潰す (ESP32 は AAAA に応答しない)。

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib_stackchan.sh
source "${SCRIPT_DIR}/lib_stackchan.sh"
load_config

EVENT="${1:-stop}"

# stdin の JSON を読む(jq があれば優先、無ければ素朴な grep でフォールバック)
STDIN_JSON="$(cat 2>/dev/null || true)"
extract_message() {
  if command -v jq >/dev/null 2>&1; then
    printf '%s' "${STDIN_JSON}" | jq -r '.message // empty' 2>/dev/null
  else
    printf '%s' "${STDIN_JSON}" | sed -n 's/.*"message"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p'
  fi
}

# event と message から発話 kind を決める
KIND="done"
case "${EVENT}" in
  notification)
    MSG="$(extract_message)"
    lower="$(printf '%s' "${MSG}" | tr '[:upper:]' '[:lower:]')"
    if printf '%s' "${lower}" | grep -q 'idle'; then
      KIND="idle"
    else
      KIND="confirm"
    fi
    ;;
  stop|*)
    KIND="done"
    ;;
esac

# IP 解決 (resolve_ip / probe / ping_resolve) は lib_stackchan.sh に集約
if resolved="$(resolve_ip)"; then
  ip="${resolved%% *}"
  via="${resolved##* }"
  mkdir -p "${CACHE_DIR}" && printf '%s\n' "${ip}" > "${CACHE_FILE}"
  # 非同期で発話投入(hook をブロックしないため)
  {
    if curl -fsS --ipv4 --max-time 5 \
        -X POST "http://${ip}/speak" \
        -H "Content-Type: application/json" \
        -d "{\"mode\":\"notify\",\"kind\":\"${KIND}\"}" \
        >/dev/null 2>&1; then
      status="ok"
    else
      status="speak-fail"
    fi
    echo "$(date -Iseconds) event=${EVENT} kind=${KIND} host=${ip} via=${via} status=${status}" >> "${LOG_FILE}"
  } &
else
  echo "$(date -Iseconds) event=${EVENT} kind=${KIND} host=${STACKCHAN_HOST} fallback=none reason=unreachable" >> "${LOG_FILE}"
  # 今回の通知は諦め、次回に備えてバックグラウンドで IP を再探索する
  if [[ -x "${SCRIPT_DIR}/rediscover_stackchan.sh" ]]; then
    nohup "${SCRIPT_DIR}/rediscover_stackchan.sh" >/dev/null 2>&1 &
    disown 2>/dev/null || true
  fi
fi

exit 0

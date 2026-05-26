#!/usr/bin/env bash
# Claude Code の Stop / Notification hook から呼ばれるエントリスクリプト。
# 第1引数で event 種別を受ける: stop | notification
#
# 動作:
#   1. stdin の hook JSON を読み、Notification なら message から kind を推定
#   2. Stack-Chan に /healthz で疎通確認 (最大 1 秒)
#   3. 応答あり → POST /speak をバックグラウンドで投げる
#   4. 応答なし → afplay /System/Library/Sounds/Ping.aiff にフォールバック
#   5. 標準出力には何も出さない(Stop hook で stdout を返すと Claude が
#      JSON 出力と誤解する可能性があるため)。ログは /tmp/stackchan_notify.log

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_FILE="/tmp/stackchan_notify.log"
FALLBACK_SOUND="/System/Library/Sounds/Ping.aiff"

# ローカル設定(gitignore対象)。STACKCHAN_HOST=stackchan.local 等を定義。
# env で渡された値を優先するため、source 前後で env の値を保存・復元する。
_ENV_STACKCHAN_HOST="${STACKCHAN_HOST:-}"
if [[ -f "${SCRIPT_DIR}/config.local.sh" ]]; then
  # shellcheck source=/dev/null
  source "${SCRIPT_DIR}/config.local.sh"
fi
if [[ -n "${_ENV_STACKCHAN_HOST}" ]]; then
  STACKCHAN_HOST="${_ENV_STACKCHAN_HOST}"
fi

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

play_fallback() {
  if [[ -f "${FALLBACK_SOUND}" ]]; then
    afplay "${FALLBACK_SOUND}" >/dev/null 2>&1 &
  fi
}

ts="$(date -Iseconds)"

# STACKCHAN_HOST 未設定なら即フォールバック
if [[ -z "${STACKCHAN_HOST:-}" ]]; then
  echo "${ts} event=${EVENT} kind=${KIND} fallback=afplay reason=no-host" >> "${LOG_FILE}"
  play_fallback
  exit 0
fi

# 疎通確認(mDNS 名前解決 + TCP コネクションを含むため 3 秒)
if curl -fsS --max-time 3 "http://${STACKCHAN_HOST}/healthz" >/dev/null 2>&1; then
  # 非同期で発話投入(hook をブロックしないため)
  {
    if curl -fsS --max-time 5 \
        -X POST "http://${STACKCHAN_HOST}/speak" \
        -H "Content-Type: application/json" \
        -d "{\"mode\":\"notify\",\"kind\":\"${KIND}\"}" \
        >/dev/null 2>&1; then
      status="ok"
    else
      status="speak-fail"
    fi
    echo "${ts} event=${EVENT} kind=${KIND} host=${STACKCHAN_HOST} status=${status}" >> "${LOG_FILE}"
  } &
else
  echo "${ts} event=${EVENT} kind=${KIND} host=${STACKCHAN_HOST} fallback=afplay reason=unreachable" >> "${LOG_FILE}"
  play_fallback
fi

exit 0

#!/usr/bin/env bash
# Claude Code の PermissionRequest hook から呼ばれる承認スクリプト。
# Stack-Chan の画面に「許可しますか?」を表示し、タッチボタンの回答を
# Claude Code に返す。
#
# ⚠️ notify_stackchan.sh の「stdout に何も出さない」規約と違い、このスクリプトは
# **意図的に stdout へ JSON を出す**(それが PermissionRequest フックへの回答に
# なる)。ログ・デバッグ出力は絶対に stdout に混ぜないこと。ログは LOG_FILE への
# リダイレクトのみ(stderr にも出さない)。
#
# 動作:
#   1. stdin の hook JSON から tool_name / tool_input / tool_use_id を抽出し
#      質問文を組み立てる
#   2. IP を3層で解決 (lib_stackchan.sh の resolve_ip) → POST /ask
#   3. GET /answer?id=... を 0.5 秒間隔でポーリング (上限 ASK_TIMEOUT_SEC 秒)
#   4. 回答に応じて stdout へ JSON を返す:
#        allow / deny → decision.behavior JSON
#        pc           → open -a "${FOCUS_APP}" して何も出力せず exit 0
#        タイムアウト・不達・409 等 → 何も出力せず exit 0
#      (→ 通常の PC 側プロンプトにフォールバック。安全側に壊れる)
#
# 注意: curl には必ず --ipv4 を付けること (.local の AAAA 5 秒問題。詳細は
# notify_stackchan.sh 冒頭コメント参照)。

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib_stackchan.sh
source "${SCRIPT_DIR}/lib_stackchan.sh"
load_config

ASK_ID="-"

# 全経路で1行ログを残す。$1=result (allow|deny|pc|fallback), $2=reason (省略可)
# 書き込み失敗時のエラーも握りつぶす (stderr にすら何も漏らさない)
log() {
  { echo "$(date -Iseconds) event=ask id=${ASK_ID} result=$1${2:+ reason=$2}" >> "${LOG_FILE}"; } 2>/dev/null
}

# このスクリプトは jq 必須 (sed フォールバックは作らない)
if ! command -v jq >/dev/null 2>&1; then
  log fallback no_jq
  exit 0
fi

# トークン未設定なら Stack-Chan 側も 503 を返すだけなので即フォールバック
if [[ -z "${STACKCHAN_TOKEN:-}" ]]; then
  log fallback no_token
  exit 0
fi

STDIN_JSON="$(cat 2>/dev/null || true)"
TOOL_NAME="$(printf '%s' "${STDIN_JSON}" | jq -r '.tool_name // empty' 2>/dev/null)"
TOOL_USE_ID="$(printf '%s' "${STDIN_JSON}" | jq -r '.tool_use_id // empty' 2>/dev/null)"

ASK_ID="${TOOL_USE_ID:-$$-$(date +%s)}"

# 質問文の組み立て。title は固定、detail はツールごとに要点だけ抜く
TITLE="許可しますか?"
case "${TOOL_NAME}" in
  Bash)
    DETAIL="Bash: $(printf '%s' "${STDIN_JSON}" | jq -r '.tool_input.command // empty' 2>/dev/null)"
    ;;
  Edit|Write|NotebookEdit)
    DETAIL="${TOOL_NAME}: $(printf '%s' "${STDIN_JSON}" | jq -r '.tool_input.file_path // empty' 2>/dev/null)"
    ;;
  *)
    compact="$(printf '%s' "${STDIN_JSON}" | jq -c '.tool_input // {}' 2>/dev/null)"
    DETAIL="${TOOL_NAME:-unknown}: ${compact:0:120}"
    ;;
esac
# 画面側でも省略されるが、転送量を抑えるため 200 文字で切り詰める
DETAIL="${DETAIL:0:200}"

# IP 解決。失敗時は自己修復をデタッチ起動して PC フォールバック
if ! resolved="$(resolve_ip)"; then
  log fallback unreachable
  if [[ -x "${SCRIPT_DIR}/rediscover_stackchan.sh" ]]; then
    nohup "${SCRIPT_DIR}/rediscover_stackchan.sh" >/dev/null 2>&1 &
    disown 2>/dev/null || true
  fi
  exit 0
fi
ip="${resolved%% *}"
mkdir -p "${CACHE_DIR}" && printf '%s\n' "${ip}" > "${CACHE_FILE}"

# POST /ask で質問を表示させる (jq で JSON エスケープを任せる)
BODY="$(jq -cn --arg id "${ASK_ID}" --arg title "${TITLE}" --arg detail "${DETAIL}" \
  '{id: $id, title: $title, detail: $detail}')"
http_code="$(curl -sS --ipv4 --max-time 5 -o /dev/null -w '%{http_code}' \
  -X POST "http://${ip}/ask" \
  -H "Content-Type: application/json" \
  -H "X-Stackchan-Token: ${STACKCHAN_TOKEN}" \
  -d "${BODY}" 2>/dev/null)" || http_code="000"
if [[ "${http_code}" != "200" ]]; then
  # 409=別の質問/音量UI表示中, 401=トークン不一致, 503=トークン未設定, 000=不達
  log fallback "ask_http_${http_code}"
  exit 0
fi

# 0.5 秒間隔のショートポーリングで回答を待つ。
# 上限はフック timeout (推奨 75 秒) と画面の自動クローズ (60 秒) より短くする。
TIMEOUT_SEC="${ASK_TIMEOUT_SEC:-55}"
FAIL_STREAK=0
SECONDS=0
while (( SECONDS < TIMEOUT_SEC )); do
  answer_json="$(curl -fsS --ipv4 --max-time 3 \
    -H "X-Stackchan-Token: ${STACKCHAN_TOKEN}" \
    "http://${ip}/answer?id=${ASK_ID}" 2>/dev/null)" || answer_json=""
  state="$(printf '%s' "${answer_json}" | jq -r '.state // empty' 2>/dev/null)"
  case "${state}" in
    pending)
      FAIL_STREAK=0
      ;;
    answered)
      answer="$(printf '%s' "${answer_json}" | jq -r '.answer // empty' 2>/dev/null)"
      case "${answer}" in
        allow)
          echo '{"hookSpecificOutput":{"hookEventName":"PermissionRequest","decision":{"behavior":"allow"}}}'
          log allow
          exit 0
          ;;
        deny)
          echo '{"hookSpecificOutput":{"hookEventName":"PermissionRequest","decision":{"behavior":"deny"}}}'
          log deny
          exit 0
          ;;
        pc)
          # PC 側プロンプトへ誘導: アプリを最前面に出し、何も出力せず抜ける
          open -a "${FOCUS_APP:-Claude}" >/dev/null 2>&1 || true
          log pc
          exit 0
          ;;
        *)
          log fallback "unknown_answer_${answer:-empty}"
          exit 0
          ;;
      esac
      ;;
    *)
      # 404 (タイムアウトで画面が閉じた等) / curl 失敗 / パース不能。
      # 一時的な取りこぼしはあり得るので 3 回連続で初めて諦める
      FAIL_STREAK=$((FAIL_STREAK + 1))
      if (( FAIL_STREAK >= 3 )); then
        log fallback poll_lost
        exit 0
      fi
      ;;
  esac
  sleep 0.5
done

log fallback timeout
exit 0

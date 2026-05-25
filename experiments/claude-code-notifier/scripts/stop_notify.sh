#!/usr/bin/env bash
# Claude Code Stop hook の実体。
# 標準入力に session 情報 JSON が渡されるが、今回は固定文言を喋らせるだけなので捨てる。

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ローカル設定（gitignore対象）。STACKCHAN_HOST=192.168.x.y 等を定義しておく。
if [[ -f "${SCRIPT_DIR}/config.local.sh" ]]; then
  # shellcheck source=/dev/null
  source "${SCRIPT_DIR}/config.local.sh"
fi

# STACKCHAN_HOST が未設定なら何もしないで終わる（Claude Code をブロックしない）。
if [[ -z "${STACKCHAN_HOST:-}" ]]; then
  echo "$(date -Iseconds) stop_notify: STACKCHAN_HOST not set" >> /tmp/stackchan_stop.log
  exit 0
fi

# AquesTalk の発話文。
# 評価版だとナ行・マ行が「ヌ」になる制限があるため、それらを含まない固定文にしている。
# 製品版ライセンスを購入したら好きな文に差し替えて OK。
MESSAGE="サギョウ シュウリョウ"

# stdin の JSON を捨てる
cat >/dev/null 2>&1 || true

# 非同期で投げる（hook が長引かないように）。失敗してもログに残すだけ。
{
  curl -fsS --max-time 5 \
    -X POST "http://${STACKCHAN_HOST}/speak" \
    -H "Content-Type: application/json" \
    -d "{\"mode\":\"fixed\",\"text\":\"${MESSAGE}\"}" \
    >/dev/null 2>&1 \
    && status="ok" || status="fail"
  echo "$(date -Iseconds) stop_notify: ${status} host=${STACKCHAN_HOST}" >> /tmp/stackchan_stop.log
} &

exit 0

#!/usr/bin/env bash
# Claude Code の Stop / Notification hook から呼ばれるエントリスクリプト。
# 第1引数で event 種別を受ける: stop | notification
#
# 動作:
#   1. stdin の hook JSON を読み、Notification なら message から kind を推定
#   2. Stack-Chan の IP を3層で解決 (キャッシュ → mDNS → ARP)。詳細は resolve_ip()
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
LOG_FILE="/tmp/stackchan_notify.log"
CACHE_DIR="${HOME}/.cache/stackchan"
CACHE_FILE="${CACHE_DIR}/last_ip"

# ローカル設定(gitignore対象)。STACKCHAN_HOST / STACKCHAN_MAC を定義。
# env で渡された値を優先するため、source 前後で env の値を保存・復元する。
_ENV_STACKCHAN_HOST="${STACKCHAN_HOST:-}"
_ENV_STACKCHAN_MAC="${STACKCHAN_MAC:-}"
if [[ -f "${SCRIPT_DIR}/config.local.sh" ]]; then
  # shellcheck source=/dev/null
  source "${SCRIPT_DIR}/config.local.sh"
fi
if [[ -n "${_ENV_STACKCHAN_HOST}" ]]; then
  STACKCHAN_HOST="${_ENV_STACKCHAN_HOST}"
fi
if [[ -n "${_ENV_STACKCHAN_MAC}" ]]; then
  STACKCHAN_MAC="${_ENV_STACKCHAN_MAC}"
fi
STACKCHAN_HOST="${STACKCHAN_HOST:-stackchan.local}"
STACKCHAN_MAC="${STACKCHAN_MAC:-}"

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

# healthz が返れば生きている機体。発話中はファーム側の loop が約 2 秒
# ブロックして応答が遅れるため、タイムアウトは 2 秒とる。
probe() {
  curl -fsS --ipv4 --max-time 2 "http://$1/healthz" >/dev/null 2>&1
}

# ホスト名/IP を ping 1発で IPv4 に解決する (実測 ~0.1 秒)。
# mDNS 名でも AAAA を引かないので速い。副作用で ARP エントリも温まる。
ping_resolve() {
  ping -c1 -W 700 "$1" 2>/dev/null |
    sed -n 's/^PING [^ ]* (\([0-9.][0-9.]*\)).*/\1/p' | head -1
}

# ARP テーブルから Stack-Chan の MAC を探して IP を得る (実測 ~10ms)。
# エントリが expire していると空振りするが、その場合は rediscover 側の
# ping スイープが次回までに埋める。
arp_resolve() {
  [[ -n "${STACKCHAN_MAC}" ]] || return 0
  arp -an 2>/dev/null | grep -i "${STACKCHAN_MAC}" |
    sed -n 's/.*(\([0-9.][0-9.]*\)).*/\1/p' | head -1
}

# 3層で IP を解決する。stdout に「IP 経路名」を出す (例: "192.168.0.15 cache")。
resolve_ip() {
  local ip

  # 1) 前回成功した IP (名前解決コストゼロ)
  if [[ -r "${CACHE_FILE}" ]]; then
    ip="$(<"${CACHE_FILE}")"
    if [[ -n "${ip}" ]] && probe "${ip}"; then
      echo "${ip} cache"
      return 0
    fi
  fi

  # 2) mDNS (STACKCHAN_HOST が IP 直書きでも ping はそのまま通る)
  ip="$(ping_resolve "${STACKCHAN_HOST}")"
  if [[ -n "${ip}" ]] && probe "${ip}"; then
    echo "${ip} mdns"
    return 0
  fi

  # 3) ARP テーブルの MAC 検索 (mDNS が死んでいても LAN に居れば拾える)
  ip="$(arp_resolve)"
  if [[ -n "${ip}" ]] && probe "${ip}"; then
    echo "${ip} arp"
    return 0
  fi

  return 1
}

ts="$(date -Iseconds)"

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
    echo "${ts} event=${EVENT} kind=${KIND} host=${ip} via=${via} status=${status}" >> "${LOG_FILE}"
  } &
else
  echo "${ts} event=${EVENT} kind=${KIND} host=${STACKCHAN_HOST} fallback=none reason=unreachable" >> "${LOG_FILE}"
  # 今回の通知は諦め、次回に備えてバックグラウンドで IP を再探索する
  if [[ -x "${SCRIPT_DIR}/rediscover_stackchan.sh" ]]; then
    nohup "${SCRIPT_DIR}/rediscover_stackchan.sh" >/dev/null 2>&1 &
    disown 2>/dev/null || true
  fi
fi

exit 0

#!/usr/bin/env bash
# notify_stackchan.sh / rediscover_stackchan.sh / ask_stackchan.sh が source する
# 共通定義。パス定数・config 読み込み・IP 3層解決 (キャッシュ → mDNS → ARP) を
# ここに集約し、スクリプト間の実装乖離 (片方だけ直して壊れる) を防ぐ。
# 単体では実行しない。
#
# 前提: source 元が SCRIPT_DIR を定義していること。

LOG_FILE="/tmp/stackchan_notify.log"
CACHE_DIR="${HOME}/.cache/stackchan"
CACHE_FILE="${CACHE_DIR}/last_ip"

# config.local.sh (gitignore 対象) を読む。env で渡された値は config より優先。
load_config() {
  local env_host="${STACKCHAN_HOST:-}"
  local env_mac="${STACKCHAN_MAC:-}"
  local env_token="${STACKCHAN_TOKEN:-}"
  local env_focus="${FOCUS_APP:-}"
  local env_ask_timeout="${ASK_TIMEOUT_SEC:-}"
  if [[ -f "${SCRIPT_DIR}/config.local.sh" ]]; then
    # shellcheck source=/dev/null
    source "${SCRIPT_DIR}/config.local.sh"
  fi
  if [[ -n "${env_host}" ]]; then
    STACKCHAN_HOST="${env_host}"
  fi
  if [[ -n "${env_mac}" ]]; then
    STACKCHAN_MAC="${env_mac}"
  fi
  if [[ -n "${env_token}" ]]; then
    STACKCHAN_TOKEN="${env_token}"
  fi
  if [[ -n "${env_focus}" ]]; then
    FOCUS_APP="${env_focus}"
  fi
  if [[ -n "${env_ask_timeout}" ]]; then
    ASK_TIMEOUT_SEC="${env_ask_timeout}"
  fi
  STACKCHAN_HOST="${STACKCHAN_HOST:-stackchan.local}"
  STACKCHAN_MAC="${STACKCHAN_MAC:-}"
  STACKCHAN_TOKEN="${STACKCHAN_TOKEN:-}"
  FOCUS_APP="${FOCUS_APP:-Claude}"
  ASK_TIMEOUT_SEC="${ASK_TIMEOUT_SEC:-55}"
}

# macOS の arp -an は octet の先頭ゼロを削った表記 (0a → a) を返すので、
# ゼロ埋め形式 (シリアルログや WiFi.macAddress() の %02x) で設定されていても
# 一致するよう、比較前に同じ形式へ正規化する。
normalize_mac() {
  printf '%s' "$1" | tr '[:upper:]' '[:lower:]' | sed -E 's/(^|:)0([0-9a-f])/\1\2/g'
}

# ARP テーブルから STACKCHAN_MAC の IP を得る (実測 ~10ms)。
# " at <mac> " を固定文字列でアンカーし、部分一致の誤マッチ
# (例: 2:c8:… が a2:c8:… に当たる) を防ぐ。
arp_resolve() {
  [[ -n "${STACKCHAN_MAC}" ]] || return 0
  local mac
  mac="$(normalize_mac "${STACKCHAN_MAC}")"
  arp -an 2>/dev/null | grep -F " at ${mac} " |
    sed -n 's/.*(\([0-9.][0-9.]*\)).*/\1/p' | head -1
}

# healthz が返れば生きている機体。発話中はファーム側の loop が約 2 秒
# ブロックして応答が遅れるため、1 秒のマージンを持たせてタイムアウトは 3 秒とる。
# (/healthz は無認証エンドポイント。トークンは不要)
probe() {
  curl -fsS --ipv4 --max-time 3 "http://$1/healthz" >/dev/null 2>&1
}

# ホスト名/IP を ping 1発で IPv4 に解決する (実測 ~0.1 秒)。
# mDNS 名でも AAAA を引かないので速い。副作用で ARP エントリも温まる。
ping_resolve() {
  ping -c1 -W 700 "$1" 2>/dev/null |
    sed -n 's/^PING [^ ]* (\([0-9.][0-9.]*\)).*/\1/p' | head -1
}

# 3層で IP を解決する。stdout に「IP 経路名」を出す (例: "192.168.x.y cache")。
# 一度 probe に失敗した IP は後段レイヤで再 probe しない (各層が同じ IP に
# 解決されると probe タイムアウトが積み重なるため)。
resolve_ip() {
  local tried=""
  # $1=IP $2=経路名。未 probe の IP なら probe し、成功なら "IP 経路名" を出力
  try_ip() {
    [[ -n "$1" ]] || return 1
    case " ${tried} " in *" $1 "*) return 1 ;; esac
    tried="${tried} $1"
    probe "$1" && { echo "$1 $2"; return 0; }
    return 1
  }

  # 1) 前回成功した IP (名前解決コストゼロ)
  if [[ -r "${CACHE_FILE}" ]]; then
    try_ip "$(<"${CACHE_FILE}")" cache && return 0
  fi
  # 2) mDNS (STACKCHAN_HOST が IP 直書きでも ping はそのまま通る)
  try_ip "$(ping_resolve "${STACKCHAN_HOST}")" mdns && return 0
  # 3) ARP テーブルの MAC 検索 (mDNS が死んでいても LAN に居れば拾える)
  try_ip "$(arp_resolve)" arp && return 0
  return 1
}

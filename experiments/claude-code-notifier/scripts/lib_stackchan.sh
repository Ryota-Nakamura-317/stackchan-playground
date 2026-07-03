#!/usr/bin/env bash
# notify_stackchan.sh / rediscover_stackchan.sh が source する共通定義。
# パス定数・config 読み込み・ARP 検索をここに集約し、2 スクリプト間の
# 実装乖離 (片方だけ直して壊れる) を防ぐ。単体では実行しない。
#
# 前提: source 元が SCRIPT_DIR を定義していること。

LOG_FILE="/tmp/stackchan_notify.log"
CACHE_DIR="${HOME}/.cache/stackchan"
CACHE_FILE="${CACHE_DIR}/last_ip"

# config.local.sh (gitignore 対象) を読む。env で渡された値は config より優先。
load_config() {
  local env_host="${STACKCHAN_HOST:-}"
  local env_mac="${STACKCHAN_MAC:-}"
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
  STACKCHAN_HOST="${STACKCHAN_HOST:-stackchan.local}"
  STACKCHAN_MAC="${STACKCHAN_MAC:-}"
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

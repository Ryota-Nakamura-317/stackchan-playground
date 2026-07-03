#!/usr/bin/env bash
# Stack-Chan の IP をサブネット全体から再探索してキャッシュを自己修復する。
# notify_stackchan.sh が unreachable のときにデタッチ起動される (手動実行も可)。
#
# 動作:
#   1. /24 を並列 ping スイープして ARP テーブルを温める (~2 秒)
#   2. ARP から STACKCHAN_MAC を検索して IP を得る
#   3. /healthz が返れば ~/.cache/stackchan/last_ip を更新
#
# hook 本体はブロックしない前提なので時間には余裕がある。多重起動は
# mkdir ロックで防ぐ (macOS に flock(1) は無い)。

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib_stackchan.sh
source "${SCRIPT_DIR}/lib_stackchan.sh"
load_config
LOCK_DIR="${CACHE_DIR}/rediscover.lock"

if [[ -z "${STACKCHAN_MAC}" ]]; then
  exit 0
fi

mkdir -p "${CACHE_DIR}"
if ! mkdir "${LOCK_DIR}" 2>/dev/null; then
  # 正常な rediscover は数秒で終わる。10 分より古いロックは異常終了
  # (SIGKILL やシャットダウンで EXIT trap が走らなかった) の残骸とみなす。
  if find "${LOCK_DIR}" -maxdepth 0 -mmin +10 2>/dev/null | grep -q .; then
    rmdir "${LOCK_DIR}" 2>/dev/null || true
    mkdir "${LOCK_DIR}" 2>/dev/null || exit 0
  else
    exit 0  # 別の rediscover が走行中
  fi
fi
trap 'rmdir "${LOCK_DIR}" 2>/dev/null' EXIT

# デフォルトルートのインターフェースから自分のサブネット (/24 前提) を得る
iface="$(route -n get default 2>/dev/null | awk '/interface:/{print $2}')"
iface="${iface:-en0}"
my_ip="$(ipconfig getifaddr "${iface}" 2>/dev/null)"
if [[ -z "${my_ip}" ]]; then
  exit 0  # ネットワーク未接続
fi
subnet="${my_ip%.*}"

# 並列 ping スイープで ARP テーブルを温める
for i in $(seq 1 254); do
  ping -c1 -W 300 "${subnet}.${i}" >/dev/null 2>&1 &
done
wait

ip="$(arp_resolve)"

ts="$(date -Iseconds)"
if [[ -n "${ip}" ]] && curl -fsS --ipv4 --max-time 2 "http://${ip}/healthz" >/dev/null 2>&1; then
  printf '%s\n' "${ip}" > "${CACHE_FILE}"
  echo "${ts} rediscover=ok ip=${ip}" >> "${LOG_FILE}"
else
  echo "${ts} rediscover=fail subnet=${subnet}.0/24" >> "${LOG_FILE}"
fi

exit 0

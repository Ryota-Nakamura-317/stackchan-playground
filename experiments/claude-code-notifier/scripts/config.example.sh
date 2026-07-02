#!/usr/bin/env bash
# このファイルを config.local.sh にコピーして使ってください。
#   cp config.example.sh config.local.sh
# config.local.sh は .gitignore でコミット対象から除外されています。

# Stack-Chan のホスト名 or IP アドレス。
#
# notify_stackchan.sh は「前回成功 IP のキャッシュ → mDNS → ARP (MAC 検索)」
# の3層で IP を解決するので、mDNS 名のままで OK。IP 直書きも可 (その場合も
# キャッシュ・ARP フォールバックは働く)。
#
# 補足: macOS で .local 名が遅いのは AAAA (IPv6) クエリの 5 秒タイムアウトが
# 原因。スクリプト内の curl は --ipv4 を付けているので mDNS 名でも速い。
export STACKCHAN_HOST="stackchan.local"

# Stack-Chan の MAC アドレス (小文字コロン区切り)。
# DHCP で IP が変わっても ARP テーブル / ping スイープから機体を特定する
# 最後の砦として使う。空だと ARP フォールバックと自己修復が無効になる。
# 確認方法: 起動時シリアル出力、またはルータの DHCP クライアント一覧。
#   export STACKCHAN_MAC="aa:bb:cc:dd:ee:ff"
export STACKCHAN_MAC=""

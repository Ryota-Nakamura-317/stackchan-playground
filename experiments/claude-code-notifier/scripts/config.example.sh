#!/usr/bin/env bash
# このファイルを config.local.sh にコピーして使ってください。
#   cp config.example.sh config.local.sh
# config.local.sh は .gitignore でコミット対象から除外されています。

# Stack-Chan のホスト名 or IP アドレス。
#
# firmware は mDNS で "stackchan.local" を公開しているが、macOS の curl は
# .local の名前解決に 3〜5 秒かかることが多く、Stop hook の疎通確認(3 秒)
# を頻繁にタイムアウトさせる。実用上は **IP 直書きが推奨**。
#
# Stack-Chan の IP は起動時のシリアル出力(`[wifi] connected. IP=...`)か
# LCD 画面右下に表示される。家庭用ルータなら通常 DHCP リース期間中は固定。
#
#   export STACKCHAN_HOST="192.168.1.123"
#
# mDNS で問題なく解決できる環境ならデフォルトのままでも動く。
export STACKCHAN_HOST="stackchan.local"

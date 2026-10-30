# PCなしのUSB-C接続: Raspberry Pi 4ブリッジ案

## BSHM2U105C1PBKについて

バッファローのBSHM2U105C1PBKは、Type-Cオスをホスト（スマホ・タブレット・PC）につなぎ、Type-Cメスの1つをPD給電入力、もう1つをデータ機器接続に使う製品です。PD入力はデータ用の2つ目の下流ポートではなく、この製品自体もUSBホストやAzarashiの復号処理を提供しません。したがって、StopWatchとGR-M10-C/Sをこの製品だけで接続しても通信できません。このPi 4案の「給電付きUSBハブ」をBSHM2U105C1PBKに置き換えることもできません。公式仕様: https://www.buffalo.jp/product/detail/bshm2u105c1pbk.html

## 構成と成立条件

```text
GR-M10-C/S ── USB-C↔USB-A ─┐
                            ├─ 給電付きUSBハブ ─ USB-A ─ Raspberry Pi 4
StopWatch ─── USB-C↔USB-A ─┘                 │
                                      5V/3A以上の電源
```

Raspberry Pi 4 は2つのUSB 2.0端子と2つのUSB 3.0端子を備えるUSBホストです。PiのUSB-C端子は電源入力です。GNSSとStopWatchはどちらもPi側のUSBホスト配下に接続し、StopWatch側は指定どおりUSB-Cを使います。PiではRaspberry Pi OS LiteとPythonのAzarashiを動かします。起動後はPC、Wi-Fi、キーボード、画面を使わずに受信・復号・表示できます。初期設定と診断には別の端末が必要です。

電源設計は未確定です。Pi 4 のUSB端子は合計最大1200mAという公式上限があり、StopWatchの充電中電流とGNSSの電流を実測していません。最初の実機試験は、Pi本体の適切な5V/3A電源に加え、独立給電のUSBハブから2台に給電してください。ハブの電源・ポートごとの電流能力、逆流防止、ケーブルのデータ対応を製品仕様で確認してください。電池駆動時間は未測定です。

これはPCを使わない独立した装置ですが、小型LinuxボードがUSBホストと復号を担当します。StopWatchとGNSSのUSB-C端子だけをケーブル1本で直結する方式ではありません。

## ソフトウェア

`qzss_watch.standalone` はGR-M10-C/Sで実測したCH340 (VID:PID 1A86:7523) と、StopWatchで実測したESP32-S3 USB Serial/JTAG (303A:1001) を識別します。同じ識別子の機器が複数ある場合は誤接続を避けるため停止して再試行します。GNSSのSFRBXをRAMで有効にしてからAzarashiで復号し、内容のある通報のみStopWatchへ送ります。USB切断や15秒間のGNSS無通信後はポートを再検出します。通報はすべて `source=live`、`notification_eligible=false` です。StopWatch側の鮮度判定と警報音・振動はまだありません。

ログは既定で保存しません。必要な場合のみ `--log-dir` を付けます。受信した原文には位置や周辺状況に関わる情報が入り得るため、保存場所のアクセス権を管理してください。

## 機材を入手した後のセットアップ

1. Raspberry Pi OS LiteをSDカードに入れ、SSHと利用者を設定します。Pi 4には公式推奨の5V/3A電源を使います。
2. このリポジトリのWindows側で `python scripts/package_pi.py` を実行します。`outputs/qzss-watch-pi.tar.gz` にはPythonソース、依存定義、systemdサービスだけを入れます。Wi-Fi設定、`secrets.h`、トークン、受信ログは含みません。
3. `scp outputs/qzss-watch-pi.tar.gz <Pi利用者>@<PiのIP>:~/` と `scp work/watch-token.txt <Pi利用者>@<PiのIP>:~/` で転送します。以下のコマンドはPi上で実行します。

```sh
sudo useradd --system --home-dir /opt/qzss-watch --shell /usr/sbin/nologin qzsswatch
sudo apt update
sudo apt install python3-venv
sudo install -d -m 755 /opt/qzss-watch
sudo tar -xzf ~/qzss-watch-pi.tar.gz -C /opt/qzss-watch
sudo python3 -m venv /opt/qzss-watch/.venv
sudo /opt/qzss-watch/.venv/bin/python -m pip install /opt/qzss-watch
sudo install -d -m 700 /etc/qzss-watch
sudo python3 -c 'import sys; from pathlib import Path; Path("/etc/qzss-watch/bridge.env").write_text("QZSS_WATCH_TOKEN=" + Path(sys.argv[1]).read_text().strip() + "\n")' "$HOME/watch-token.txt"
sudo chmod 600 /etc/qzss-watch/bridge.env
rm -- ~/watch-token.txt
sudo install -m 644 /opt/qzss-watch/deploy/raspberry-pi/qzss-watch.service /etc/systemd/system/qzss-watch.service
sudo systemctl daemon-reload
sudo systemctl enable --now qzss-watch.service
```

4. `systemctl status qzss-watch.service` と `journalctl -u qzss-watch.service -f` で `bridge active`、`stored`、`session summary` を確認します。GNSSとStopWatchのUSBポート名は固定しません。`lsusb` と `python3 -m serial.tools.list_ports -v` で識別子を照合してください。
5. 給電を抜き差しして自動復旧を確認します。画面に「衛星受信」と日本語本文が出ること、通報の再送で `duplicate` になること、USB切断時に古い通報を新規通知しないことを確認します。ユーザーによるこの構成の実機試験はまだありません。

## 参考資料

- [Raspberry Pi 4 Model B製品概要](https://datasheets.raspberrypi.com/rpi4/raspberry-pi-4-product-brief.pdf)
- [Raspberry Piの電源・USB出力仕様](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html)
- [Raspberry Piの初期セットアップ](https://www.raspberrypi.com/documentation/computers/getting-started.html)

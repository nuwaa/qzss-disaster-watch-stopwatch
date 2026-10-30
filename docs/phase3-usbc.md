# Phase 3: USB-C 接続の検証

## 接続構成

StopWatch と GR-M10-C/S の USB-C 端子はいずれも USB 機器側です。StopWatch の公式回路図では USB-C の CC1/CC2 にプルダウン抵抗があり、VBUS は入力です。GR-M10-C/S は CH340 を介して USB シリアル機器として動作します。したがって USB-C ケーブルによる2台の直結や、給電だけを追加する受動アダプタでは通信できません。

最初の実機検証では、PC を一時的な USB ホストとして使います。

```text
GR-M10-C/S --USB-C--> PC（Azarashi 復号）--USB-C--> StopWatch
```

PC を外す最終構成には、2台の USB 機器に給電し、それぞれをホストとして扱う能動的なブリッジが必要です。小型 Linux ボードと給電可能な USB ハブなどが候補ですが、現時点では製品・消費電力・電池駆動・相性は未選定・未検証です。この構成は StopWatch に USB-C で接続できますが、GNSS 基板とのケーブル1本による直結ではありません。StopWatch 単体で USB ホストになる案は、USB 端子の給電回路と現行 Arduino USB CDC 設定の変更を伴うため、別途ハードウェア検討が必要です。

StopWatch v1.0.1 背面の BAT は電池端子です。v1.0 の電源端子と取り違えて 5V を加えないでください。USB-C 以外で給電する前には、実機の版数と公式回路図を再確認してください。

## USB シリアル表示プロトコル

StopWatch の USB CDC に 115200 bps で、UTF-8 の1行を送信します。

```text
WATCH_REPORT <QZSS_WATCH_TOKEN> <表示用JSON>\n
```

応答は `WATCH_ACK stored`、`WATCH_ACK duplicate`、またはエラーです。既存 HTTP と共通の JSON 検証・履歴・重複判定を使用します。空の DCX は送信しません。画面の出所表示は StopWatch 側で再計算します。通知音・振動は引き続き無効です。

## PC をホストにした検証手順

1. ファームウェアを `scripts/build_watch.ps1` でビルドし、`scripts/upload_watch.ps1 -Port COM10` で書き込みます。実際の COM 番号に置き換えてください。USB受信キューを8192バイトに拡張した版は再ビルド・再書き込みが必要です。
2. StopWatch と GNSS をそれぞれ PC に USB-C で接続し、`python -m serial.tools.list_ports -v` で2つのポートを確認します。
3. PowerShell で以下を実行します。既存の `work/watch-token.txt` を使い、`setup_watch.ps1` を再実行してトークンを変更しないでください。スクリプトはトークンを画面に表示せず、処理後に環境変数から削除します。

```powershell
& .\scripts\run_live_usb.ps1 -WatchPort COM10 -SampleOnly
& .\scripts\run_live_usb.ps1 -GnssPort COM11 -WatchPort COM10
```

4. サンプルでは `watch USB: stored` と画面の「デモ・訓練」を確認します。ライブ受信では画面の「衛星受信」、本文、`USB-C 転送あり` を確認します。実際の受信は日時・衛星可視性で変わります。画面に表示された通報を現行の警報とは断定しません。

2026-10-31、PCをホストとする実機検証でサンプルの `stored` と、5分間の衛星受信からStopWatchへのUSB-C表示を確認しました。3014 UBXフレームから143件を復号し、空DCXを除く固有メッセージは30件でした。StopWatch画面の「衛星受信」と日本語本文はユーザーが確認しました。転送の個別ACK件数は記録していないため、全通報が画面に登録されたことまでは確認していません。

## 参考資料

- [M5Stack StopWatch 公式資料](https://docs.m5stack.com/ja/core/StopWatch)
- [M5Stack StopWatch 公式回路図](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1242/C152-SCH_Stopwatch_PRJ_Main_VA_20251201_2026_04_24_17_46_22.pdf)
- [PRIORIS GR-M10-C/S 製品一覧](https://prioris.jp/gnss/lineup/)
- [Espressif Arduino ESP32 USB Host](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/usb_host.html)

# QZSS Disaster Watch — Phase 1

みちびき L1S の DCR/DCX を受信する研究・実験用ソフトウェアです。認証済みの防災製品ではありません。避難判断は公的な情報源で行ってください。

## 構成

- `src/qzss_watch/core.py`: UBX フレーム抽出と Azarashi JSON 変換
- `src/qzss_watch/cli.py`: USB シリアル受信、JSONL ログ、再生、公開訓練サンプル
- `tests/`: フレーム処理の単体テスト
- `firmware/stopwatch/`: Phase 2 表示用ファームウェア（PlatformIO）
- `docs/phase3-usbc.md`: USB-C 接続の成立条件と検証手順
- `src/qzss_watch/standalone.py`: PCなしのUSBホストブリッジ用常駐処理
- `docs/phase3-pi-bridge.md`: Raspberry Pi 4への導入、給電、実機検証手順
- `firmware/stopwatch-host-probe/`: StopWatch単体USBホスト化の列挙試験（通常版とは別）
- `firmware/stopwatch-gnss-probe/`: CH340経由でGNSSのNMEA/UBX受信数を画面で確認する試験版
- `firmware/stopwatch-standalone/`: USBホスト受信とDCR/DCX日本語表示のPCなし試作版（受信・本文表示・通知待機を実機確認済み）
- `docs/phase3-stopwatch-host.md`: BSHM2U105C1PBKを使う列挙試験と復旧手順

## セットアップ

Python 3.11 以上を使用します。

```powershell
py -3.11 -m venv .venv
.venv\Scripts\python -m pip install -e .
.venv\Scripts\python -m unittest discover -s tests
```

Windows では実行前に `$env:PYTHONUTF8='1'` を設定してください。Azarashi 0.17.1 の日本語コード表を正しく読み込むために必要です。

Linux では `python3 -m venv .venv`、`.venv/bin/python` に読み替えてください。

## 実行

```powershell
$env:PYTHONUTF8='1'
.venv\Scripts\qzss-watch sample
.venv\Scripts\qzss-watch receive COM3 --baud 115200 --raw-log logs/raw.jsonl --output logs/reports.jsonl
.venv\Scripts\qzss-watch receive COM9 --baud 9600 --enable-sfrbx --raw-log logs/raw.jsonl --output logs/reports.jsonl
.venv\Scripts\qzss-watch replay logs/raw.jsonl --output logs/replayed.jsonl
```

Linux のポート例: `/dev/ttyUSB0`。保存先の `logs/` は先に作成してください。GR-M10-C/S の実際のボーレートは受信機設定で確認してください。Prioris の資料によると同機種は出荷時に SFRBX 出力が BBR に設定されていますが、バックアップ電池の状態によって再設定が必要です。
`--enable-sfrbx` を指定した場合に限り、UART1 の SFRBX 出力を受信機の RAM 上で有効にします。電源を切るとこの設定は失われます。

標準出力と `--output` は Azarashi JSON v2 に `watch.source` と `watch.notification_eligible` を付加した JSONL です。`source` は `live`、`replay`、`sample`。`is_test` は Azarashi が判定します。Phase 1 では鮮度・永続的な重複判定がないため、**すべての出力で `notification_eligible=false`** とします。Phase 2 で判定を実装するまでは振動・音を出さないでください。`qzss.dcx.null` は内容のない空メッセージです。DCX の日本語本文は Azarashi の `texts.ja` に常に入るわけではありません。

`sample` は Azarashi README の公開訓練/試験 QZQSM 例です。生の UBX ログの代わりに単独の訓練サンプルの JSON 化を確認できます。

## Phase 2 前の実機確認

1. Windows/Linux でポート名・ボーレート・SFRBX 出力・QZSS L1S 受信を確認する。
2. 実受信の生ログを保存し、再生結果と時刻・DCR/DCX 判定を照合する。
3. StopWatch v1.0.1 の公式回路図で UART GPIO、電圧、電源端子、消費電力を確認する。v1.0 の `BAT` 表示ピンは実際には 5V IN で、v1.0.1 では BAT に修正されている。
4. M5Unified/M5GFX の StopWatch 対応、画面、日本語フォント、振動、音、電池動作を実機で確認する。
5. Wi-Fi 通信は最初に PC からの HTTP POST を候補とし、接続断・古い通報・重複・訓練表示を検証する。

## Phase 2 表示プロトタイプ

StopWatch の公式 Arduino/PlatformIO 構成に合わせ、M5Unified・M5GFX・ArduinoJson を使用します。日本語フォントで通報種別、本文、受信時刻、Wi-Fi 状態を表示し、A/B ボタンで最新8件を移動します。通報本文は日本語の文字境界で折り返し、各行を中央に配置します。HTTP POST は `http://<StopWatch の IP>:8080/report`。PC は Azarashi JSON から表示用の短い JSON を作ります。振動・音は無効です。画面・Wi-Fi・公開訓練サンプルと中央揃えの表示は実機で確認済みです。

1. 通常の PowerShell で `scripts/setup_watch.ps1` を実行し、Wi-Fi 名・パスワードを入力します。スクリプトの実行が無効な場合は、その PowerShell で `Set-ExecutionPolicy -Scope Process -ExecutionPolicy RemoteSigned -Force` を先に実行します。この設定は現在のセッションだけに適用されます。ランダムなトークンが自動生成されます。`secrets.h` と `work/watch-token.txt` は Git の対象外です。スクリプトを使わない場合は `firmware/stopwatch/include/secrets.example.h` を `secrets.h` にコピーして編集してください。
2. `scripts/build_watch.ps1` でビルドします。Windows のコンパイラパスを短くするため、空いている Z:/Y:/X:/W: ドライブをビルド中だけ割り当て、終了時に解除します。ビルド結果は `work/watch-build.log` に保存されます。成功後に `scripts/upload_watch.ps1 -Port COM10` で StopWatch に書き込みます。ポート番号は接続時に確認してください。書き込みモードに入らない場合は M5Stack 公式手順のリセット操作を行ってください。

3. シリアルモニタ（115200 bps）に表示される IP アドレスを確認します。PC と StopWatch を同じ Wi-Fi に接続します。
4. PC の PowerShell で `$env:PYTHONUTF8='1'`、`$env:QZSS_WATCH_TOKEN=(Get-Content work/watch-token.txt -Raw)` を設定します。まず `.venv\Scripts\qzss-watch sample --device-url http://<IP>:8080/report` で「デモ・訓練」と表示されるか確認します。
5. 実衛星の受信は `& .\scripts\run_live_watch.ps1 -Port COM11 -WatchIp <IP>`。5分で自動停止し、30秒ごとに進捗を表示します。受信ログは Git 管理対象外の `logs/` に保存されます。停止後、`python .\scripts\summarize_live.py` で通報種別・固有通報数・訓練フラグを確認できます。COM 番号と IP は実際の値を指定してください。

ビルド・書き込みスクリプトは一時ドライブの解除時に `TEMP`、`TMP`、`PYTHONPATH`、`PLATFORMIO_CORE_DIR` を元に戻します。旧版を実行したPowerShellでGit Bashが「/tmpが見つからない」または解除済みの `Z:\work\tmp` を参照する場合は、`$env:TEMP = Join-Path $env:LOCALAPPDATA 'Temp'; $env:TMP = $env:TEMP` を実行するか、新しいPowerShellを開いてください。

HTTP は同一 LAN 上の試験用です。トークンは平文で送られるため、信頼できるネットワークだけで使ってください。`watch.notification_eligible` は全件 `false` で、画面に表示されても警報音・振動は出しません。空の DCX はログに記録しますが StopWatch へは送りません。実受信でも、配信時刻と現在時刻の照合が済むまでは「現在有効な警報」と扱わないでください。

## 検証記録

PCを外すPhase 3用にRaspberry Pi 4のUSBホスト上で動く自動再接続ブリッジとsystemdサービスを実装。Pythonテスト17件成功。機材未接続のためRaspberry Pi上での実行、電源容量、StopWatch表示は未検証です。`python scripts/package_pi.py` で秘密情報を含まない配布アーカイブを作れます。導入手順は `docs/phase3-pi-bridge.md` を参照してください。
ユーザー希望のBSHM2U105C1PBKとStopWatch自身のUSBホスト化を調査。StopWatchのUSB-Cは機器側のCC/5V配線だが、列挙試験の実機画面に「GNSS CH340 を認識」「1A86:7523」が表示され、GNSSまでのUSBデータ経路を確認した。CH340のシリアル受信を調べる独立ファームウェアを用意した。こちらのビルド・書き込み・実機受信は未確認。手順は `docs/phase3-stopwatch-host.md` を参照。
2026-10-31: ユーザーがStopWatch実機画面で、GNSS受信byte、UBX、SFRBX、QZSS L1Sの件数が継続して増加することを確認。その後、単体表示版で通報本文の表示も実機確認。PCなしのUSB-C受信・本文表示まで成立。本文のAzarashiとの一致、時刻と訓練区分、再接続、音・振動通知は未検証。
2026-10-31: ユーザーが単体表示版の発表時刻表示を実機確認。表示値とAzarashiの一致、訓練区分、再接続、音・振動通知は引き続き未検証。
2026-10-31: A/B同時長押しの手動通知テストで振動・音・画面表示をユーザーが確認。次版に新着DCRだけの自動通知とNVS重複抑制を追加。自動通知の実機動作は未検証。条件は `docs/phase3-stopwatch-host.md` を参照。
2026-10-31: 更新版で「自動通知待機」への移行をユーザーが実機確認。GNSS側USBの切断・再接続では状態表示と受信再開、通知待機への復帰が正常で、意図しない音・振動はなかった。実際の新着DCRによる自動通知と再起動後の重複抑制は未検証。
2026-10-31: USB切断時にGNSS時刻の有効判定を即時解除する修正版をユーザーがビルド・書き込み、再接続後の「自動通知待機」への復帰を実機確認。再接続後は時刻の再確認と60秒の準備時間を設ける。
2026-10-31: 準備の残り秒数が減り「自動通知待機」に変わる表示をユーザーが実機確認。自動通知した通報を示す「衛星受信・通知済」は、新着DCRをまだ受信していないため未確認。

Phase 3 の USB-C シリアル表示口を実装しました。接続構成と試験コマンドは [Phase 3 手順](docs/phase3-usbc.md) を参照してください。今回のファームウェア変更はまだビルド・実機表示を確認していません。

2026-10-30: Python 3.13.15、Azarashi 0.17.1、pySerial 3.5 で公開訓練サンプルを復号。DCR 緊急地震速報、`is_test=true`、日本語本文あり、通知対象外として JSON 化できました。
2026-10-30: GR-M10-C/S の COM9 をユーザー側 PowerShell から確認。通信速度は 9600 bps。RAM 上で SFRBX 出力を有効化すると ACK と SFRBX 35件を確認。内訳は GPS/Galileo/BeiDou で、QZSS L1S は0件。途中で測位有効になりましたが、GQGSV は衛星0機です。DCR/DCX の実衛星受信は未確認です。
2026-10-30: その後の5分間の実機ログで UBX-RXM-SFRBX 2812件、うち QZSS L1S 636件を確認。Azarashi で310件を復号（空 DCX 153件、降灰 126件、台風 20件、海上 11件）。29種類の `message_id` を確認。訓練/試験判定はいずれも `false` ですが、この値だけで現行の警報とは断定できません。5分ログはフレームごとの受信時刻を保存していないため、復号時刻と配信時刻の厳密な照合には使えません。
2026-10-30: 実受信 QZSS フレーム1件を位置情報を含まないテスト素材として保存し、Azarashi 結合テストを追加。Python テスト計10件成功。StopWatch ファームウェアは PlatformIO の依存解決まで確認しましたが、この Codex Windows 実行環境の Xtensa コンパイラが「システム DLL の無効な再配置」で起動できず、コンパイルと実機表示は未検証です。
2026-10-31: StopWatch の `server.begin()` を Wi-Fi 接続後に移動し、起動時の `Invalid mbox` 再起動ループを解消。ユーザー側で再ビルド・書き込み後、Wi-Fi 接続・IP 取得・公開訓練サンプルの日本語表示を確認。
2026-10-31: COM11 の5分間ライブ受信で SFRBX 3061件、Azarashi 復号228件を確認。空 DCX 113件を除く固有メッセージは28件。StopWatch へ送信した直近の通報の再送は HTTP 200 `duplicate` となり、端末側の履歴登録と重複判定を確認。ユーザーが画面の「衛星受信」と日本語本文の表示を確認。これらの件数は繰り返し配信を含み、現行の新規警報件数ではありません。
2026-10-31: 通報本文を日本語の文字境界で折り返し、各行を中央揃えに変更。ユーザーが修正版を書き込み、公開訓練サンプルの「デモ・訓練」が読みやすく表示されることを確認。
2026-10-31: 中央揃え版で再度5分間ライブ受信。SFRBX 3072件、Azarashi 復号139件（空 DCX 65件、固有の非空通報23件）。StopWatch 画面の「衛星受信」と本文をユーザーが確認。Wi-Fi 転送で1回タイムアウトしたが、受信終了後の直近通報の再送は HTTP 200 `duplicate` となり履歴保存を確認。
2026-10-31: Phase 3 のUSB-Cシリアル表示を実機確認。COM10のStopWatchに公開訓練サンプルを送り、`watch USB: stored` を確認。COM11のGNSSから5分間受信し、UBX-RXM-SFRBX 3014件、Azarashi復号143件（空DCX 70件、非空の固有メッセージ30件）。USB転送エラーは出ず、ユーザーがStopWatch画面の「衛星受信」と日本語本文を確認。これでPCをUSBホストとする2機器のUSB-C接続を確認したが、PCを外した能動的USBホストブリッジは未構築・未検証。

## 参考資料

- [Azarashi API/JSON](https://github.com/nbtk/azarashi/blob/main/docs/json.md)
- [Prioris UBX-RXM-SFRBX 説明](https://prioris.jp/gnss/docs/processing/qzqsm/)
- [M5Stack StopWatch](https://docs.m5stack.com/ja/core/StopWatch)

# QZSS Disaster Watch — Phase 1

みちびき L1S の DCR/DCX を受信する研究・実験用ソフトウェアです。認証済みの防災製品ではありません。避難判断は公的な情報源で行ってください。

## 構成

- `src/qzss_watch/core.py`: UBX フレーム抽出と Azarashi JSON 変換
- `src/qzss_watch/cli.py`: USB シリアル受信、JSONL ログ、再生、公開訓練サンプル
- `tests/`: フレーム処理の単体テスト
- `firmware/stopwatch/`: Phase 2 表示用ファームウェア（PlatformIO）
- `docs/phase3-usbc.md`: USB-C 接続の成立条件と検証手順
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

以下は作業順です。受信件数には衛星からの繰り返し配信が含まれ、現在有効な警報の件数ではありません。

### 2026-10-30：Phase 1、受信と復号

- **公開訓練データを復号。** Python 3.13.15、Azarashi 0.17.1、pySerial 3.5で緊急地震速報の公開サンプルを処理し、`is_test=true`と日本語本文を確認しました。通知対象外のJSONとして保存できます。

- **GR-M10-C/Sの通信条件を特定。** COM9、9600 bpsでNMEAを受信しました。受信機RAMでSFRBX出力を有効にするとACKとSFRBX 35件を確認しました。この時点ではQZSS L1Sは0件でした。

- **QZSS L1Sを実受信。** 続く5分間でSFRBX 2812件、そのうちQZSS L1S 636件を確認しました。Azarashiの復号は310件（空DCX 153件、降灰126件、台風20件、海上11件）、固有`message_id`は29種類でした。このログにはフレームごとの受信時刻がないため、発表時刻との厳密な照合には使いません。

- **再現用の実受信素材を追加。** 位置情報を含まないQZSSフレーム1件をテスト素材として保存し、Azarashi結合テストに使用しました。

### 2026-10-31：Phase 2、PCからStopWatchへの表示

- **起動不具合を修正。** Wi-Fi接続前の`server.begin()`が原因だった`Invalid mbox`再起動ループを解消しました。ユーザーが再ビルド・書き込み後、IP取得と公開訓練サンプルの日本語表示を確認しました。

- **Wi-Fiで実受信を転送。** COM11からの5分間でSFRBX 3061件、Azarashi復号228件を確認しました。StopWatchに「衛星受信」と日本語本文が表示され、同一通報の再送はHTTP 200 `duplicate`となりました。

- **円形画面の本文を調整。** 日本語の文字境界で折り返し、各行を中央に配置しました。ユーザーが公開訓練サンプルの読みやすさを確認しました。

- **中央揃え版で再受信。** 5分間でSFRBX 3072件、Azarashi復号139件（空DCX 65件、固有の非空通報23件）を確認しました。StopWatchの本文表示を確認。Wi-Fi転送が1回タイムアウトしましたが、終了後の再送は`duplicate`となりました。

### 2026-10-31：Phase 3、USB-C接続とPCなし動作

- **まずPCをUSBホストとして接続。** COM10のStopWatchへ公開訓練サンプルを送り、`watch USB: stored`を確認しました。COM11のGNSSからの5分間でSFRBX 3014件、Azarashi復号143件を得て、StopWatchに「衛星受信」と日本語本文が表示されました。

- **接続方式をStopWatchのUSBホストに決定。** 途中でRaspberry Piブリッジ案も作成しましたが、採用していません。以降の実機検証はStopWatch自身のUSBホスト方式で進めました。

- **StopWatch自身のUSBホスト化を確認。** BSHM2U105C1PBK経由でGNSSのCH340（`1A86:7523`）を列挙しました。続くGNSS試験版で受信byte、UBX、SFRBX、QZSS L1Sの各件数が増加することを画面で確認しました。

- **PCなしの本文表示を確認。** 単体表示版でQZSS通報の日本語本文と発表時刻をユーザーが確認しました。構成と書き込み手順は[StopWatch USBホスト版](docs/phase3-stopwatch-host.md)にあります。

- **通知用の振動・音を確認。** A/Bボタン同時長押しの手動テストで、短い振動・音と「手動通知テスト」の表示を確認しました。その後、新着DCR向けの時刻判定とNVSによる通知済みID保存を実装しました。

- **通知待機と再接続を確認。** GNSS時刻の確認と60秒の準備後、「自動通知待機」になりました。GNSS側USBの切断・再接続では接続待ち、受信再開、通知待機への復帰を確認し、意図しない音・振動はありませんでした。準備の残り秒数表示も実機で確認しました。

- **保存ログで通知条件とヘッダーを照合。** 143件のAzarashi復号結果には、発表後3分以内の自動通知候補がありませんでした。別の監査では3014 UBXフレームから354件のQZSS L1Sを取り出し、DCR 73件の発表時刻・通報区分・発表種別・訓練判定をAzarashiと比較して不一致0件でした。Pythonテスト21件に成功しました。

- **概要画面のUIを調整。** 従来の文字画面を残したまま、大きな文字の概要画面を追加しました。復号本文の空行を詰め、「災危通報」と「発表時刻」を上へ移動して1行ずつ表示しました。ユーザーが実機で文字位置と文字切れがないことを確認しました。

- **図形アイコンと背景色を追加。** 通報待ち、訓練・試験、古い通報、通報種別を図形と文字で区別しました。実機での見た目をユーザーが確認しました。配色の違いによる判定精度や長時間表示は未評価です。

- **自動通知の試験用ビルドを追加。** GNSS受信を止めた「通知試験モード」で、訓練・古い通報・新着相当・重複の条件を人工的に確認できます。通常版とは通知済みIDの保存領域を分けています。手順は[StopWatch USBホスト版](docs/phase3-stopwatch-host.md#自動通知の試験用ビルド)を参照してください。

- **通知条件を実機試験。** ユーザーが試験用ビルドを書き込み、訓練・古い通報・重複では無音、新着DCR模擬だけ音と振動が1回出ることを確認しました。音量設定を64から160へ上げてもほぼ聞こえなかったため、通知音を4kHz・300ms、設定値220に変更。ユーザーが聞き取りやすい音になったことと、再起動後の試験用IDの重複抑制を実機確認しました。実衛星の新着通報での自動通知は未確認です。

- **通常版に復帰。** 試験用ビルドの確認後、ユーザーが通常版を書き込み、GNSS受信画面への復帰を確認しました。

- **保存ログの降灰DCRを本文照合。** 実受信した1件をStopWatch内で復号し、ユーザーが発表時刻10月30日20時24分、桜島、少量の降灰、鹿児島県東串良町など、Azarashiと照合する項目を実機で読めることを確認しました。本文全体の一致や他の通報種別は未確認です。手順は[StopWatch USBホスト版](docs/phase3-stopwatch-host.md#保存ログによる本文照合ビルド)を参照してください。

- **比較試験後に通常版へ復帰。** ユーザーが通常版を書き込み、GNSS受信画面に戻ったことを確認しました。

- **別種のDCRを実機照合。** 実受信ログから台風・海上・気象の各1件を抽出し、保存ログ比較用ビルドで降灰と合わせて選べるようにしました。ユーザーが台風31号・非常に強い、海上濃霧警報・サハリン東方海上、竜巻注意情報・新潟県を実機で読めることを確認しました。保存ログに内容のあるDCXはなく、DCX本文は引き続き未確認です。

- **追加の比較試験後に通常版へ復帰。** ユーザーが通常版を書き込み、GNSS受信画面に戻ったことを確認しました。

- **PCなしの実受信で自動通知と1時間の連続動作を確認。** 11:37、実受信した海上警報（画面上の発表時刻11:35）に対して「新規通報通知」「衛星受信・通知済」が表示され、ユーザーが音と振動を確認しました。受信カウンターは11:37に515,226 B・L1S 1,270件、12:07に2,267,226 B・L1S 3,552件、12:37に3,991,994 B・L1S 5,528件でした。1時間で3,476,768 B・L1S 4,258件増加しました。12:07と12:37は「衛星データ受信中」「自動通知待機」の表示で、11:37～12:37に受信停止・再起動・追加の音や振動は報告されていません。両時点の画面では、11:35発表の海上警報が「古い通報」と表示されました。これはユーザーによる実機観察であり、衛星通報の内容や通知条件を独立に照合した結果ではありません。

- **通常版を起動し直した後に受信と通知待機への復帰を確認。** 電源ボタンの長押し後、受信カウンターのリセット、受信B・L1S件数の増加、「通知: 準備中 あと50秒」から「自動通知待機」への移行をユーザーが確認しました。この操作がCPUのリセットか電源オフ→オンかは、当時の表示だけでは区別できません。初回の操作では、音・振動の有無を確認できませんでした。

- **起動し直した際の意図しない通知を再点検。** ユーザーが通常版を再度起動し、音・振動がなかったことを確認しました。この時点では11:35発表の通報はすでに古く、NVSに保存された通知済みIDによる重複抑制を単独で証明する試験ではありません。起動操作の種類も未判別です。

- **電源操作を区別して確認。** 起動理由表示を追加した通常版をユーザーが書き込み、「CPU電源投入」を確認しました。電源ボタンを2回押すと画面と緑LEDが消え、短押しで「CPU電源投入 0分」として起動しました。電源オン中の短押しは変化なし。約2秒の長押しでは画面が消えて緑LEDだけ点灯し、続く短押しで「CPU電源投入 0分」として起動しました。緑LEDだけの状態の内部動作は未判定です。

- **直近の自動通知を調べる診断記録を実装。** 実衛星DCRで音・振動を開始した時だけ、L1S 32バイトと通知判定の項目を通常版NVSに保存し、A/B同時短押しで記録画面を開けるようにしました。模擬通知の保存先は別領域です。保存済み実受信フレーム4種で、画面のL1Sから再構成したUBXをAzarashiで復号した内容が元のUBXと一致するPythonテストに成功しました。ユーザーが試験用ビルドを書き込み、模擬通知時の「診断記録あり（試験用）」と電源オフ→オン後の保持を確認しました。通常版での診断記録保存・記録画面は未確認です。

### 現在の未確認項目

- 通常版での再起動後の通知済みIDによる重複抑制（試験用ビルドでは確認済み）
- 実衛星通報での自動通知について、GNSS生データとAzarashi復号結果による独立照合
- StopWatchに表示する日本語本文・DCX本文とAzarashiの復号結果の一致
- 画面上部に追加したCPU起動理由の各区分の実機表示（「CPU電源投入」は確認済み）
- 通常版での自動通知診断記録の保存・再読出し・記録画面表示（試験用ビルドの保存・再読出しは確認済み）

防災用の認証済み製品ではなく、研究・実験用です。


## 参考資料

- [Azarashi API/JSON](https://github.com/nbtk/azarashi/blob/main/docs/json.md)
- [Prioris UBX-RXM-SFRBX 説明](https://prioris.jp/gnss/docs/processing/qzqsm/)
- [M5Stack StopWatch](https://docs.m5stack.com/ja/core/StopWatch)

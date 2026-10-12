# uiapduino-neko-keyer

UIAPduino（CH32V003）で動く、ネコ型の CW（モールス）キーヤーです。
ブラウザで文字を入力して送信すると、ネコの腕（サーボ）がパドルを打ちます。PC との通信には WebHID を使うので、ドライバのインストールは不要です。

A cat-shaped CW keyer for UIAPduino (CH32V003). Type text in a Chrome/Edge page and the cat taps the paddle with two servos, over WebHID.

## できること

- ブラウザの入力欄に好きな文字を入れて「打たせる」を押すと、ネコがモールスで打ちます
- 速さを 5〜30 WPM で変えられます（打っている途中でも変えられます）
- 「くりかえす」で繰り返し打たせられます
- いま打っている文字が画面でハイライトされます
- 使える文字: 英数字と `. , ? / - ( ) + @ : ; =`（小文字は大文字に変換されます。200 文字まで）

## 必要なもの

- UIAPduino（HID ProMicro CH32V003）
- ホビーサーボ 2 個（SG90 など）。DIT 用と DAH 用のレバーを動かします
- Chrome または Edge（WebHID が使えるブラウザ。Firefox / Safari は不可）
- Arduino IDE 2.x と、UIAPduino 用ボードパッケージ

## 配線

| 役割 | ピン |
|---|---|
| DIT（短点）サーボ | PD2（TIM1_CH1） |
| DAH（長点）サーボ | PA1（TIM1_CH2） |

サーボの押す／離すの角度は、`NekoKeyer/NekoKeyer.ino` の先頭で変えられます。

```cpp
#define DIT_REST    15
#define DIT_ACTIVE  0
#define DAH_REST    0
#define DAH_ACTIVE  15
```

サーボの信号は 50 Hz で、角度 0〜180° がパルス幅 1〜2 ms に対応します。

## 使い方

### 1. 基板にスケッチを書き込む

1. Arduino IDE の「ファイル → 環境設定」で、「追加のボードマネージャの URL」に次を追加します。

   ```
   https://raw.githubusercontent.com/tarosay/board_manager_files/main/package_uiap_hid_index.json
   ```

2. 「ツール → ボード → ボードマネージャ」で `UIAPduino` を検索し、**UIAPduino HID** をインストールします。
3. `NekoKeyer/NekoKeyer.ino` を開きます（フォルダ名と `.ino` の名前は同じにしておきます）。
4. 「ツール」で次を選びます。
   - ボード: **HID ProMicro CH32V003**
   - USB: **WebHID Only**
5. USB ケーブルを一度抜きます。
6. **先に** Arduino IDE の書き込みボタンを押し、すぐに **基板のボタンを押したまま USB を挿して**、挿したらボタンを離します。書き込みが終わると、基板は自動で再起動します。
   - 「Could not initialize any supported programmers」と出たときは、基板が書き込みモードになる前に IDE が探し終えています。もう一度、タイミングを早めて試してください。
   - Xcratch など、基板に接続しているタブやアプリがあれば閉じてください。

### 2. ブラウザから動かす

1. `index.html` を Chrome か Edge で開きます（GitHub Pages で公開している場合はその URL）。
   - ファイルを直接開いて接続できないときは、フォルダで `python -m http.server` を実行して、`http://localhost:8000/` で開いてください。
2. 「ネコを選んで接続」を押して、一覧から UIAPduino を選びます。
3. 文字と速さを決めて「▶ 打たせる」を押します。

## ファイル

| ファイル | 内容 |
|---|---|
| `NekoKeyer/NekoKeyer.ino` | 基板のスケッチ（サーボ制御・モールス変換・WebHID 受信） |
| `index.html` | PC で開く操作ページ（単体の HTML です） |

## 通信の仕様

PC から基板へは Feature Report（レポート ID 0、32 バイト）、基板から PC へは Input Report（8 バイト）で送ります。先頭の 1 バイトがコマンドです。

PC → 基板

| コマンド | バイト列 | 内容 |
|---|---|---|
| `0x10` CLEAR | `[0x10, unit_ms, loop]` | 文字バッファを空にして、短点の長さ（ms）と繰り返しを設定 |
| `0x11` DATA | `[0x11, n, c0, c1, …]` | 文字を n 個追加（最大 200 文字） |
| `0x12` PLAY | `[0x12]` | 先頭から打ち始める |
| `0x13` STOP | `[0x13]` | 止める |
| `0x14` SPEED | `[0x14, unit_ms]` | 打っている途中でも速さを変える |

基板 → PC

| コマンド | バイト列 | 内容 |
|---|---|---|
| `0x61` | `[0x61, pos, total]` | `pos` 番目の文字を打ち始めた |
| `0x62` | `[0x62]` | 最後まで打ち終えた |
| `0x63` | `[0x63, total]` | PLAY を受け付けた |
| `0x64` | `[0x64]` | STOP で止まった |

短点の長さ（`unit_ms`）と WPM の関係は `WPM = 1200 / unit_ms` です。長点は 3 倍、符号の間は 1 倍、文字の間は 3 倍、単語の間は 7 倍で打ちます。

## ライセンス

[MIT License](LICENSE)

## 謝辞

WebHID での通信の仕組みは、[tarosay/uiap-hid-web](https://github.com/tarosay/uiap-hid-web)（MIT License）の公式サンプルを参考にしています。基板側のライブラリは [tarosay/arduino_core_ch32](https://github.com/tarosay/arduino_core_ch32) を使っています。

UIAPduino は tarosay さんの製品です。このリポジトリは公式のものではありません。

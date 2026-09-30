# Chromecast への送信（計画）

> 2026-09-25 夜: ライブラリは **sdlcastg** に改名し、フォルダも `sdlcastg/` にした（公開の準備。
> memo/sdlcast-publish.md）。この文書の `sdlcast` はその前の名前。公開向けの解説は `sdlcastg/docs/design.md`。

演奏の画面と音声を Chromecast に送る。**mxv2 に直接作り込まず、先に
「SDL2 の映像と音声を Chromecast に送るライブラリ」を作り、mxv2 はそれを
使う**（ユーザーの指示、2026-09-25）。**進め方の 1（下調べ）・2（制御部分）・3（ライブ配信）・4（Android でのビルド）まで済み。**
4 は端末のネットワークの都合で、Android から TV へ流すところだけ確かめられていない（下の「手順 4 の結果」）。

## 前提として確かめたいこと

- **送り先は TV の Chromecast built-in**（ユーザー提供、2026-09-25）:
  東芝 REGZA 43Z570K、名前 `mytv`、`192.168.2.36`、有線 LAN。
  - `http://192.168.2.36:8008/setup/eureka_info` →
    `cast_build_revision 3.72.446070`、`name mytv`、`locale en-US`。
  - TCP 8008 / 8009 / 8443 が開いている。
  - mDNS の TXT: `md=SmartTV FFM`（TV 内蔵の汎用名）、`fn=mytv`、`ve=05`、
    `ca=264709`（映像出力・音声出力を含む）、`rs=Screen Mirroring`
    （問い合わせたとき TV 側で動いていたアプリの名前）。
  - **マルチキャストで問い合わせても応答が取れなかったが、TV へ直に
    （ユニキャストで）問い合わせると答えた。** 最初の問い合わせは
    QU ビット（ユニキャストで返してほしい印）を立てておらず、TV は
    224.0.0.251:5353 へ返していて、こちらはそこで待っていなかった。
    **探索は QU ビットを立てる＋ 5353 番でも待つ**の両方にすること。
  - 手元の ffmpeg（`D:/O/EXEDLLS/ffmpeg`）は libvpx（VP8/VP9）・libopus・
    libx264・aac を持っている。pychromecast は入っていない
    （入れずに、Cast V2 の最小のクライアントを Python で手書きすれば
    C++ 版の試作にもなる）。
- Android の OS には「画面のキャスト」（画面と音をまるごとミラー）が
  すでにあるので、mxv2 独自に送る価値は主に **(1) Windows 版** と
  **(2) ダイアログやステータスバー抜きの、プレーヤーの画面だけを TV の
  大きさで出すこと** にある。この前提で進めてよいか。
- **自前で送る利点（ユーザーの指摘、2026-09-25）**: 端末の OS のキャスト
  （画面のミラー）は、**アプリと無関係に来る通知まで TV に映してしまう**。
  自前なら mxv2 の画面（キャンバスの範囲）だけを送るので、通知は写らない。
  （ほかに、手順 4 で分かった「WebM で送るので途切れにくい」もある。）

## 仕組みの概要

Chromecast へ映像を送る方法は、ざっくり 2 通りある。

| 方法 | 受信側 | 遅れ | 手間 |
|---|---|---|---|
| **A. 標準の受信アプリに URL を渡す** | Default Media Receiver（アプリ ID `CC1AD845`、登録不要） | 数秒（受信側のバッファ） | 中 |
| B. 自前の受信アプリ + WebRTC | Google Cast の開発者登録（有料 5 ドル）と、HTTPS で公開する HTML の受信アプリ | 0.2 秒程度 | 大（WebRTC の実装が重い） |

**A を採る。** 送るのは「演奏の画面と音」で、両方が同じストリームに入る
ので、画面と音はずれない。遅れるのは手元の操作が TV に届くまでだけ
（曲を替えてから TV が替わるまで数秒）で、プレーヤーとしては許容できる。

A の流れ:

1. **探す** — mDNS で `_googlecast._tcp.local` を問い合わせ、名前（`fn=`）・
   機種（`md=`）・アドレス・ポート（ふつう 8009）を得る。
2. **つなぐ** — Chromecast の 8009 番へ TLS で接続（証明書は自己署名なので
   検証しない）。やりとりは **Cast V2 プロトコル**: 4 バイトの長さ +
   protobuf の `CastMessage`（中身は名前空間ごとの JSON）。公式の SDK は
   Android / iOS / Chrome 用だけで、デスクトップの C/C++ 用は無い。
   ただしプロトコルは VLC（`modules/stream_out/chromecast`）、pychromecast、
   go-chromecast などが実装済みで、送る側に認証は要らない。
   - `urn:x-cast:com.google.cast.tp.connection` … CONNECT / CLOSE
   - `urn:x-cast:com.google.cast.tp.heartbeat` … 5 秒ごとの PING / PONG
   - `urn:x-cast:com.google.cast.receiver` … LAUNCH（アプリ ID）、状態、音量
   - `urn:x-cast:com.google.cast.media` … LOAD（URL・型・`streamType: LIVE`）、状態
3. **流す** — 送る側が小さな HTTP サーバーを立て、エンコードした
   ライブストリームを返し続ける。Chromecast はその URL を取りに来る。

## ライブラリ（仮称 sdlcast）

置き場所は `mxv2/sdlcast/`（`skineditor/` と同じ扱い。独立した CMake
プロジェクトで、mxv2 の CMake から `add_subdirectory` する）。ライセンスは
mxv2 と同じ Apache 2.0。

### 層の分け方

- **core**（SDL に依存しない）: 探す・つなぐ・エンコード・多重化・HTTP 配信。
  映像は RGBA の画素、音声は int16 の PCM を受け取る。
- **SDL 層**（薄い）: `SDL_Renderer` の描画結果を取り込む（描画先の
  テクスチャから `SDL_RenderReadPixels`）、`SDL_AudioSpec` の形式を
  変換して渡す、スレッドや時計を SDL のもので済ませる。

### API の形（案。C の関数で出す）

```c
int  SDLCast_Init(void);
void SDLCast_Quit(void);

/* 探す。結果は SDLCast_GetDevices で読む（別スレッドで更新される） */
int  SDLCast_StartDiscovery(void);
int  SDLCast_GetDevices(SDLCast_Device *out, int max);  /* 名前・機種・id・アドレス */

/* つなぐ。映像の大きさ・fps・ビットレート、音声のレート・チャンネル数 */
int  SDLCast_Connect(const char *deviceId, const SDLCast_Config *cfg);
void SDLCast_Disconnect(void);
SDLCast_State SDLCast_GetState(void);   /* 探索中 / 接続中 / 読み込み中 / 再生中 / エラー */
const char *SDLCast_GetError(void);

/* 流す。pts は音声のサンプル位置（音を時計にして映像を合わせる） */
int  SDLCast_SubmitAudio(const int16_t *pcm, int frames, uint64_t ptsSamples);
int  SDLCast_SubmitVideoRGBA(const void *pixels, int pitch, uint64_t ptsSamples);
int  SDLCast_SubmitVideoFromRenderer(SDL_Renderer *r, const SDL_Rect *src,
                                     uint64_t ptsSamples);

/* 受信側の音量（TV のリモコンと同じもの） */
int  SDLCast_SetVolume(float level);
```

スレッドは 3 本: エンコード、HTTP 配信、Cast の制御（心拍を含む）。
呼ぶ側のスレッドを待たせない（Submit は待ち行列に積むだけ。溢れたら古い
映像フレームから捨てる。音声は捨てない）。

### 何でエンコードするか

**VP8 + Opus を WebM に入れて、HTTP で流しっぱなしにする**のを第一案にする。

- libvpx（VP8。BSD）、libopus（BSD）、libwebm（多重化。BSD）はどれも
  Windows と Android（NDK）でビルドでき、Apache 2.0 と組み合わせやすい。
- VP8・Opus・WebM は Chromecast の全世代が再生できる形式。VLC が
  Chromecast へ変換して送るときも WebM（VP8 + Vorbis）を流しっぱなしにする
  作りで、実績がある。
- 映像はほとんど静止した絵なので、640x480〜1280x720・30fps・1〜2Mbps で足りる
  見込み（libvpx の実時間モード）。
- 代わりの案: FFmpeg（LGPL。重いがコーデックと容れ物を選び放題）、
  端末のハードウェアエンコーダー（Android の MediaCodec / Windows の Media
  Foundation で H.264。速いが実装が 2 通りになる）。第一案で CPU が
  足りなければ（特に非力な Android 機）考える。

### そのほかの部品

- **TLS**: mbedTLS（Apache 2.0）。mxv2 は通信を OS に任せる方針
  （`httpget` は WinHTTP / Java）だが、Cast V2 は素の TCP の上の TLS で
  HTTP ではないので、OS の HTTP 部品は使えない。Windows の Schannel と
  Android の Java `SSLSocket` に分けるより、1 本で書ける mbedTLS にする。
- **protobuf**: 使うのは `CastMessage` の 7 つの項目だけなので、手で
  符号化・復号する（protobuf のライブラリは入れない）。
- **JSON**: 読むのは受信側の状態（`transportId` / `sessionId` /
  `mediaSessionId` / 音量）だけ。小さな自前の読み取りで足りる。
- **mDNS**: mjansson/mdns（パブリックドメインの単一ヘッダー）か自前。
  Android では `WifiManager.MulticastLock` を取らないとマルチキャストを
  受け取れない端末があるので、そこだけ Java の手助けが要る
  （`CHANGE_WIFI_MULTICAST_STATE` の許可）。
- **HTTP サーバー**: 1 本の URL（`/stream.webm`）を返すだけの最小のもの。
  自分の IP は「Chromecast へつないだソケットの手元側のアドレス」を使う
  （複数の NIC があっても正しい口が選べる）。

第三者のソースは mxv2 と同じく同梱せず、BUILD.md で用意させる。NOTICE に
libvpx / libopus / libwebm / mbedTLS /（使えば）mdns を足す。

## mxv2 からの使い方

- **映像**: プレーヤーの画面だけを送る（設定ダイアログやメニュー、
  チュートリアルは送らない）。キャンバスと文字レイヤーを、送る大きさの
  描画先テクスチャへ合成して取り込む。TV は横長なので、縦のスキン
  （Phone）のときは左右に帯が出る。送っている間は横のスキンを使う設定を
  用意するかは、動かしてから決める。
- **音声**: 出力装置へ渡しているのと同じ PCM（マスター音量を掛けた後）を
  `SDLCast_SubmitAudio` へ。時刻はサンプル位置で、mxv2 がもともと持っている
  「サンプル位置打刻」（`visualFrame`）で映像のフレームにも同じ時計を付ける。
  **送っている間は手元の音を消す**（TV と数秒ずれて二重に聞こえるため）。
  既定は消す、設定で残せる。
- **操作**: 手元の mxv2 がそのままリモコンになる（曲送り・一時停止・
  シーク）。ストリームは途切れず流れ続け、一時停止中は止まった画面と
  無音が流れる（受信側の一時停止は使わない。ライブなので）。
- **UI**: メニューに [キャスト…]。探索中の一覧から選んで接続、接続中は
  [キャストを終了]。状態（接続中・読み込み中・エラー）はダイアログと
  ログに出す。文言は message.ini。
- **Android**: バックグラウンドでも送り続ける（前面サービスはすでにある）。
  ただし画面を描いていないので、バックグラウンドの間の映像は
  (a) 描画を続けて送る、(b) 最後の絵のまま、のどちらか。電池を考えて
  まず (b)。小窓（PiP）との関係もここで決める。

## 進め方

1. **下調べ（コードを書く前に、手元の道具で確かめる）**
   - LAN の Chromecast の機種を確かめる。
   - ffmpeg でテストパターン + 正弦波を VP8 + Opus の WebM にして HTTP で
     流しっぱなしにし、pychromecast（または catt）で Default Media Receiver
     に LOAD させる。**再生が始まるか、遅れは何秒か、何分流しても
     止まらないか**を見る。駄目なら fMP4（H.264 + AAC）や HLS を同じ方法で
     試す。**ここで形式を決める。**
2. **sdlcast の制御部分**: mDNS・TLS・Cast V2・心拍・LAUNCH・LOAD。
   まずは手元の HTTP サーバーの静止ファイル（1 の出力）を再生させる。
   Windows の見本プログラム（コマンドラインで機種を選んで流す）を作る。
3. **sdlcast のライブ配信**: エンコード・多重化・HTTP 配信・A/V の時刻合わせ。
   SDL2 の見本プログラム（動く絵 + 音）で 30 分流して、止まらないこと・
   ずれないことを確かめる。
4. **Android でのビルド**: NDK で libvpx / libopus / mbedTLS をビルド、
   マルチキャストのロック、CPU の重さを Pixel 7a と非力な機で測る。
5. **mxv2 への組み込み**: 上の「mxv2 からの使い方」。
6. **仕上げ**: Windows のファイアウォール（HTTP サーバーの待ち受けで
   許可を求められる）、接続が切れたときの後始末、BUILD.md・NOTICE・README。

## 気をつけること

- **Windows のファイアウォール**: 待ち受け（HTTP サーバー）と mDNS の受信で
  許可のダイアログが出る。拒否されると Chromecast が取りに来られない。
  状態とエラーの文言で分かるようにする。
- **Android の新しい権限**: 新しい版の Android には「ローカル
  ネットワークへのアクセス」の権限が入ってきている。今の targetSdk (34)
  では効かない見込みだが、上げるときに要確認。
- **受信側のバッファ**: Default Media Receiver はライブでも数秒ためる。
  送る側が遅れると再生が止まり、ため直しになる。エンコードが間に合わない
  ときは映像のフレームを落として、音は落とさない。
- **遅れを縮めたくなったら** B（自前の受信アプリ + WebRTC）しかない。
  そのときは開発者登録と受信アプリの置き場所（HTTPS）が要る。

## 下調べの結果（2026-09-25、進め方の 1）

道具は `memo/cast_spike/casttest.py`（Python の標準ライブラリ + ffmpeg。
Cast V2 を手書き）。ユーザーの許可を得て TV（mytv）へ流した。

    python casttest.py 192.168.2.36 60 webm   # VP8 + Opus の WebM
    python casttest.py 192.168.2.36 45 mp4    # H.264 + AAC の fMP4

**どちらも Default Media Receiver で再生できた。形式は WebM（VP8 + Opus）に決める。**

| | WebM（VP8 + Opus） | fMP4（H.264 + AAC） |
|---|---|---|
| LAUNCH → アプリが起動 | 6.4 秒 | 6.1 秒 |
| 取りに来てから PLAYING まで | 約 4 秒 | 約 4.4 秒 |
| 遅れ（流した長さ − currentTime） | **3.84 秒で一定**（60 秒間） | 4.36〜4.84 秒で揺れる |
| 受信側の状態の知らせ | 問い合わせたときだけ | 1 秒に何十回も来る。currentTime が何度か足踏み |

分かったこと:

- 受信側は **Android TV の上の Cast 受信部**（User-Agent が
  `Chrome/92.0.4515.0 ... CrKey/1.56.500000 DeviceType/AndroidTV`）。
- 受信側は `Range: bytes=0-` 付きの GET を 1 回だけ送ってきて、あとは
  流しっぱなしを読み続ける。**Range には応えず 200 で流しっぱなしで
  よかった**（Content-Length なし、`Connection: close`）。HEAD は来なかった。
- `streamType: LIVE`、`contentType: video/webm`。ffmpeg の webm 出力は
  `-live 1 -cluster_time_limit 1000`、VP8 は `-deadline realtime
  -cpu-used 8 -b:v 2M -g 60`、Opus は 128k。1280x720・30fps で
  60 秒 7.8MB（約 1Mbps）。
- STOP（receiver 名前空間、`sessionId`）で受信アプリが終わり、TV は
  アプリの無い状態に戻る。HTTP の接続は受信側から切られる。
- 心拍は受信側からも PING が来るので PONG を返す。こちらからも 5 秒ごとに
  PING を送った。これで 60 秒間切れなかった。
- **TV での見え方（ユーザーが目で確認）**: WebM は 60 秒間良好に受信できた。
  **H.264（fMP4）は途中で引っかかりがあり、その後中断した**（受信側の
  状態の知らせでは、currentTime の足踏みとして見えていたもの）。
  → WebM に決めたことの裏付け。

まだ試していないこと: 30 分以上流したとき、VP9、遅れを縮める工夫
（受信側のバッファの長さは送る側からは変えられない見込み）。

## ユーザーからの指摘（2026-09-25、手順 2 の途中）

1. **Android の OS のキャスト（画面のミラー）でも、H.264 の試験と同じく途中で
   引っかかったり中断したりする。** OS のキャストはエンコーダーを選べない。
   → このライブラリで WebM（VP8 + Opus）を HTTP で送れば、Android でも
   改善するかもしれない。**Android で送る価値が「Windows 版のついで」より
   大きくなった。** 手順 4（Android でのビルド）の優先度を上げる。
   参考: OS の画面ミラーは受信側の再生バッファを浅くして遅れを縮める作り
   （公開されている仕組みでは UDP で送る Cast Streaming）なので、揺れに弱い。
   こちらの作りは TCP で約 4 秒ぶん溜めるので、遅れる代わりに途切れにくい。
2. **ImGui のダイアログやメニューも送られるのか。** ImGui は SDL の同じ描画先に
   描かれるので、「画面をまるごと読み取る」なら送られる。ただし、どこで
   読み取るかで選べる:
   - mxv2 の描く順は「キャンバス → 文字レイヤー → ImGui（ダイアログ・メニュー・
     チュートリアル）→ 表示」。**ImGui を描く前に読み取れば、プレーヤーの
     画面だけ**になる（送る大きさの描画先へキャンバスと文字レイヤーを
     合成して読む）。**ImGui を描いたあとに読めば、ダイアログも送られる**。
   - ライブラリはどちらにも縛らない（`SDLCast_SubmitVideoFromRenderer` を
     呼ぶ時点を呼ぶ側が決める）。**mxv2 では ImGui も含めて送る**（ユーザーの判断、2026-09-25）。
     つまり ImGui を描いたあと、表示の直前に読み取る。

## 手順 2 の結果（2026-09-25）

`mxv2/sdlcast/`（CMake の単独プロジェクト。`BUILD.md` の mbedTLS の節）:

| ファイル | 中身 |
|---|---|
| `include/sdlcast.h` | 公開 API（C）。探す・つなぐ・URL を再生させる・ファイルを配る・受信アプリを止める・音量・状態 |
| `src/net.*` | ソケットの包み（Winsock / POSIX）、口の一覧、手元の住所 |
| `src/json.*` | 木に読むだけの小さな JSON |
| `src/castmsg.*` | CastMessage の手書きの符号化・復号 |
| `src/channel.*` | mbedTLS の TLS。**入出力は 1 本のスレッド**（mbedTLS は読みと書きを別スレッドから同時にできない）。送信は待ち行列、読み取りは 50ms で区切って合間に書く。心拍（5 秒ごとの PING、受信側の PING に PONG、15 秒無音で切断） |
| `src/session.*` | CONNECT / GET_STATUS / LAUNCH / LOAD / STOP / SET_VOLUME、RECEIVER_STATUS・MEDIA_STATUS から状態を作る |
| `src/discovery.*` | mDNS（QU ビット付き、口ごとに問い合わせ、5353 番でも待つ）、1 台に直に尋ねる Probe |
| `src/httpserver.*` | GET / HEAD、ファイルは Range（206）、ライブは 200 で流しっぱなし |
| `tools/castplay.cpp` | 見本: `--list` / `--status <IP|名前>` / `<IP|名前> <ファイル> [秒]` / `--url` |

確かめたこと（Windows、Debug、ライブラリ部分の警告ゼロ）:

- `castplay --status 192.168.2.36` → Probe で名前と機種、TLS でつないで
  RECEIVER_STATUS（アプリなし・音量 0.45）。TV の表示は変わらない。
- `castplay --list 6` → 3 台見つかった: `mytv`（SmartTV FFM、192.168.2.36）、
  `ダイニング ルーム`（Google Home Mini、.27）、`ベッドルーム`（Google Nest Hub、.34）。
  **Home Mini は画面が無い**ので、映像を送る先の一覧からは TXT の `ca=`
  （能力のビット。映像出力は 1）で外すこと（手順 3 以降）。
- `castplay mytv test20.webm 40`（20 秒の VP8 + Opus の WebM）→
  LAUNCH から 6.8 秒で受信アプリ、BUFFERING → PLAYING、20 秒で
  `IDLE/FINISHED`、STOP で受信アプリが消えた。
- ファイアウォールのダイアログは出なかった（ユーザーの許可済みの環境）。

残したこと: HTTP サーバーの作業スレッドは Stop まで溜まる（手順 3 で片付ける）。

## 手順 3 の結果（2026-09-25）

足したもの（`mxv2/sdlcast/`）:

| ファイル | 中身 |
|---|---|
| `src/webm.*` | ライブ向けの WebM の書き出し（自前。Segment の大きさ不明・Cues なし・Cluster は 500ms かキーフレームで区切る・Void で詰める） |
| `src/livesource.*` | HTTP で配る中身。受信側ごとに「つないだあとのキーフレーム」から、時刻を 0 に振り直して渡す |
| `src/encoder.*` | VP8（libvpx 実時間・CBR・2 秒ごとのキーフレーム）と Opus（48kHz ステレオ、20ms）。音声が時計。途切れたら無音、映像が来なければ最後の絵を 1 秒ごとに出し直す。RGBA → I420 は縦横比を保って収める（libyuv の面積の平均・BT.601。手順 4 で自前の双線形から替えた） |
| `include/sdlcast_sdl.h` / `src/sdlcast_sdl.cpp` | SDL の層: 描画先を `SDL_RenderReadPixels`（fps で間引く）、`SDL_AudioStream` で 48kHz ステレオ int16 へ |
| `tools/sdlcastdemo.cpp` | 見本。隠したウィンドウの 1280x720 の描画先に描き、毎秒の頭にビープと白い四角 |
| `tools/encodetest.cpp` | 受信側なしで WebM を書き出して ffprobe / ffmpeg で確かめる |
| `vcpkg.json` | libvpx / opus を vcpkg で `third_party/vcpkg_installed` へ（BUILD.md）。**手順 4 でやめた**（クラシックモードへ） |

公開 API に足したもの: `SDLCast_StartStream / StopStream / SubmitAudio /
SubmitVideoRGBA / WantVideoFrame / StreamTimeMs / GetStreamStats`、設定
`SDLCast_StreamConfig`（大きさ・fps・映像と音声のビットレート・**minKbps**・題名）。

### 遅れを 7.6 秒から 4.4 秒に縮めた経緯（TV = mytv、REGZA の Cast）

最初は遅れ（流れの時刻 − 受信側の再生位置）が 7.6 秒で、下調べ（3.84 秒）より
ずっと大きかった。順に確かめた:

1. **溜めてある直近のキーフレームから渡していた** → 受信側がつないだのは
   流し始めて 8 秒後、直近のキーフレームは 6 秒 → 最初から 2 秒古い。
   → つないだらキーフレームを頼み、つないだあとのものから渡すようにした。
   **遅れは変わらなかった**（ほかに原因がある）。
2. **時刻が 0 から始まっていなかった** → 受信側は 0 から再生を始めて最初の
   Cluster の時刻へ飛ぶ。→ 受信側ごとに Cluster の Timecode を振り直した
   （ブロックの時刻は Cluster からの相対なのでそのまま）。**これも変わらず。**
   ただし正しい作りなので残した。
3. **流す量が少なすぎた。** 見本の絵は動きが少なく、VP8 にすると約 0.2Mbps
   （下調べのテストパターンは約 1Mbps）。受信側は一定のバイト数ずつ読む作りと
   みられ、少ないと 1 回読むのに 1 秒以上かかって、そのたびに再生が詰まる
   （PLAYING → BUFFERING を 2 回繰り返してから落ち着いていた）。
   → Cluster の末尾に WebM の **Void（読み飛ばされる詰め物）** を足して、
   最低 1Mbps 流すようにした（`minKbps`、既定 1000）。
   **溜め直しは始めの 1 回だけになり、遅れは（測り方を直したあとの値で）
   3.7 秒。** 2Mbps・4Mbps に上げても変わらない（受信側がもともと約 4 秒
   溜める）ので、既定は 1Mbps。**mxv2 の画面も動きが少ないので、この詰め物は要る。**

（ここまでの遅れの値には、次の節の測り方の誤りで 0〜2 秒が乗っている。
「受信側が少し速く再生して詰めている」と一度書いたが、それは誤り。）

### 30 分の試験と、遅れの測り方の誤り（2026-09-25）

`sdlcastdemo mytv 1800`（Release）: **30 分間止まらなかった。** 溜め直しは
始めの 1 回だけ、捨てたフレーム 0、無音の足し 0、エンコード 1 枚 6〜7ms、
流した量 214MB（約 1Mbps。Void の詰め物込み）。

ただし遅れの値が**のこぎり形**だった: 約 130 秒かけて 5.7 秒 → 3.7 秒へ縮み、
2 秒跳ねて戻る、の繰り返し。溜め直しの知らせは出ていない。
**原因は測り方**: 受信側の再生位置（currentTime）は 2 秒ごとの問い合わせで
しか更新されず、表示（約 2.02 秒ごと）との周期のずれで「受け取った値の古さ」
が 0〜2 秒の間をゆっくり回っていた（ずれの周期 2 / 0.02 × 2.02 ≈ 135 秒と
一致）。→ `Session::status()` で、再生中なら受け取ってからの経過を足すように
した。**直したあと 5 分流して、遅れは 3.69〜3.73 秒で平ら。**
下調べの ffmpeg（3.84 秒）と同じか少し短い。

### そのほか確かめたこと

- `sdlcast_encodetest`（640x360・800x600 の絵を縦横比を保って収める、音を 2 秒・
  絵を 3 秒止める）→ ffprobe で vp8 / opus / webm、ffmpeg で最後まで復号できる。
  無音の足しは止めた長さと一致（2001ms）、止めた間は絵を 1 秒ごとに出し直し、
  捨てたフレームは 0。最初の 2 枚が同じ時刻だった不具合と、終わりの
  Cluster を読み切らずに終えていた不具合はここで見つけて直した。
- エンコードは Release で 720p・1 枚 5〜7ms（変換込み）。Debug だと 720p は
  実時間に追いつかないことがあるので、受信側へ流すときは Release。
- ウィンドウは隠したまま（描画先のテクスチャから読み出す）で、デスクトップに何も出ない。

### 踏んだ罠

- **Bash の heredoc 経由で Python に置換させると、バックスラッシュが消える**
  （メモリにもある既知の罠）。この回も 4 回踏んだ（16 進の 80 のエスケープが U+0080 の 2 バイトに、ヌル文字のエスケープが本物の NUL に、改行のエスケープを含む置換元が一致せず置換されない、行継続の記号が消えて 1 行につながる）。
  バックスラッシュを含む置換は Edit か、バックスラッシュを使わない書き方で。

## 手順 4 の結果（2026-09-25）

### 作り方（BUILD.md の「libvpx / opus / libyuv」）

- **vcpkg はクラシックモードにした。** マニフェスト（`sdlcast/vcpkg.json`）で
  `--x-install-root` へ入れると、入れ先に triplet が 1 つしか置けず、
  Android 向けを入れた時点で Windows 向けが消された。`vcpkg.json` は捨てた。
- **Android 向けの triplet を自前にした**（`sdlcast/triplets/`）。この版の vcpkg
  （2025-06-20）は標準の `arm64-android` で Android のツールチェーンを
  読み込まず、`ANDROID_NDK_HOME` を渡しても「NDK が見つからない」で止まった。
  `VCPKG_CHAINLOAD_TOOLCHAIN_FILE` を明示。vcpkg の場所は、読み込み元
  （`<vcpkg>/scripts/buildsystems/vcpkg.cmake`）の `CMAKE_PARENT_LIST_FILE` から
  求める（環境変数 `VCPKG_ROOT` も `Z_VCPKG_ROOT_DIR` も triplet の評価や
  try_compile の中では見えなかった）。API は 21、STL は `c++_static`。
- **libvpx の移植を写して直した**（`sdlcast/ports/libvpx/`）。標準のものは
  Android を `generic-gnu` で作り、**SIMD なしの C だけ**になる（simpleperf で
  `*_c` の関数ばかり出た）。直したのは 3 つ:
  1. ターゲットを `arm64-android-gcc` / `armv7-android-gcc` に。
  2. 環境変数 `CFLAGS` / `CXXFLAGS` / `ASFLAGS` にも `--target=...` を入れる。
     移植が渡す `--extra-cflags` は NEON の有無を調べた**あと**で足されるので、
     それだけだと `-march=armv8-a` の試しが x86 向けの clang で失敗し、
     **NEON が黙って外れる**（`vpx_config.h` の `HAVE_NEON 0` で分かる）。
     armv7 のアセンブリも同じ理由で x86 扱いになって止まる。
  3. `AS` の `.exe` を取る（configure は `*clang` のときだけ `-c` を足す）。
     armv7 は `--enable-thumb` を外す（GNU as 用の `-mimplicit-it=always` が付き、
     clang が受け付けない）。
- **libyuv を足した**（vcpkg。縮小と RGBA→I420）。自前の双線形の縮小と
  変換は Pixel 7a でエンコード全体の 1/3 を食っていた。いまは縮小は
  `ARGBScale`（`kFilterBox`＝面積の平均。大きく縮めても字がちらつきにくい）、
  変換は `ABGRToI420`（libyuv の "ABGR" はメモリ上で R, G, B, A）。
  vcpkg の libyuv は libjpeg-turbo を連れてくるが、リンクはしていない。
- sdlcast のコードは NDK で**警告ゼロ**でそのまま通った（POSIX の分岐は
  初めてのビルドだった）。直したのは、クラスの中で値を与えた `static const`
  の定数を参照で渡していたところ（`std::chrono::milliseconds(kJoinWaitMs)`）。
  最適化しない Debug では定義が無くてリンクで落ちる（C++11）。`.cpp` に定義を足した。

### CPU の重さ（Pixel 7a、`sdlcast_encodetest --bench`、アプリの外のシェルで）

**時間の割合ではなくサイクル数で比べること。** スマートフォンは周期的な
仕事に合わせてクロックを下げるので（schedutil）、「1 コアの何 %」は
何を変えても 60〜70% に張り付いた（クロックが 0.5〜0.95GHz まで下がって
帳尻が合う）。`simpleperf stat -e cpu-cycles:u` で測る
（`task-clock` や `:k` はシェルからは使えなかった）。

| 送る大きさ | fps | サイクル/秒 |
|---|---|---|
| 1280x720 | 30 | 0.63〜0.69G |
| 1280x720 | 20 | 0.41G |
| 854x480 | 30 | 0.41G |
| 854x480 | 20 | 0.28G |
| 640x360 | 30 | 0.27G |

（絵は一部だけが動く `--static`、libyuv と NEON を使ったあと、VP8 は 1 スレッド。
絵を作る分は含まない。）画素数 × fps にほぼ比例する。720p30 は X1 コア
（2.85GHz）の 1/4 弱、A55（1.8GHz）なら 1/3 強。

分かったこと:
- **VP8 は 1 スレッドにした**（既定を「コア数の半分・最大 4」から 1 へ）。
  複数スレッドだと行のそろい待ちを回りながら待つ（`thread_encoding_proc` が
  サイクルの 4 割）。2 本でサイクル 1.46 倍、4 本で 2 倍以上、1 枚の時間は
  縮まない。パソコンでも 1 本と 4 本で CPU の時間は同じだった。
  `StreamConfig::threads`（内部）で変えられる。
- 変わらなかったもの: 速さ（`VP8E_SET_CPUUSED`）16 は 1 割減るだけで画質が
  落ちるので 8 のまま。`VP8E_SET_STATIC_THRESHOLD` と
  `VP8E_SET_SCREEN_CONTENT_MODE` は軽くならなかった。映像のビットレート
  （250〜2000kbps）でも変わらない。
- 1 枚のエンコードの時間はどの大きさでも 17〜20ms（クロックが下がるため）。
  30fps の 33ms には収まっており、捨てたフレームは 0。
- 縦の画面（1080x2400）を 1280x720 へ収めると、縮小のぶん重い。mxv2 では
  **画面をまるごと読み出すと、読み出し（GPU→CPU）も縦長の画面の大きさになる**。
  手順 5 で、送る大きさの描画先へ GPU で縮めてから読むか、読む間隔を
  落とすかを決める（アプリの中での測定は手順 5 で）。

### マルチキャストのロック

`SDLCast_StartDiscoverySDL` / `SDLCast_StopDiscoverySDL`（sdlcast_sdl.h）。
Android では JNI で `WifiManager.MulticastLock` を取ってから探す（Java の
クラスは足していない。FindClass を使わず、手元のオブジェクトからクラスを取る）。
権限 `CHANGE_WIFI_MULTICAST_STATE` をマニフェストに足した（無ければロックなしで探す）。

確かめ方: mxv2 を `-Pmxv2.cmakeArgs="-DMXV2_CAST=ON -DMXV2_CAST_SELFTEST=ON"`
でビルドすると、起動直後に 6 秒探して logcat（タグ `mxv2`）へ出す。
`WifiService: acquireMulticastLock uid=... lockTag=sdlcast` と release が出た。
**ロックは効いている。**

### 端末から TV へ届かない（未解決。端末かネットワークの問題）

Pixel 7a（Android 17）から、探しても `ベッドルーム`（Nest Hub）しか
見つからない（パソコンからは 3 台）。調べると、**Pixel から TV（.36）と
Home Mini（.27）へは ping も通らない時間帯がある**（20 回中 0 回）。
同じときにルーターと Nest Hub へは 0% の損失。ARP の MAC は正しい。
通るときもあり、そのときは TCP の 8009 へもつながり `castplay --status` も
成功した（最初の数回は SYN の応答が無く時間切れ）。パソコン（有線）からは
いつでも届く。**sdlcast の作りではなく、端末と無線（メッシュの中継機など）の
問題とみている。** この状態では Android から TV へ流す試験（30 分・CPU）は
できない。

- Android 17 の「ローカルネットワークへのアクセス」の制限は、パソコンの
  TCP（5357）へは届くので、少なくとも一律には効いていない。
- シェル（uid 2000）でも、mxv2 のデバッグ版の UID（`run-as`）でも同じ。

### Xperia Ace III（A203SO、Android 14、Snapdragon 480）

小さいコア 6 つ（A55 1.8GHz）と大きいコア 2 つ（A76 2.0GHz）。軽い仕事は
小さいコアへ回されるので、**サイクル数もコアの種類をそろえないと比べられない**
（そろえずに測ると、大きさを変えても 0.8〜0.9G/秒で変わらなく見えた）。
`taskset` で固定して測った（`--bench --static`、10 秒、サイクル/秒）:

| | 小さいコアだけ (3f) | 大きいコアだけ (c0) |
|---|---|---|
| arm64 1280x720 30fps | 1.85G、**捨てたフレーム 32/300**（1 枚 35ms） | 0.71G |
| arm64 854x480 30fps | 1.37G（A55 の 3/4） | 0.59G |
| arm64 854x480 20fps | 1.00G | 0.39G |
| armv7 1280x720 30fps | 2.12G、**捨てた 109/300**（1 枚 47ms） | 0.91G |
| armv7 854x480 30fps | 1.85G | 0.77G |
| armv7 854x480 20fps | 1.31G | 0.54G |

- **小さいコアだけだと 720p30 は間に合わない**（映像を捨てる。音は途切れない）。
  480p30 はぎりぎり、480p20 なら余裕。大きいコアなら 720p30 でも 1/3 強。
- **armv7 は arm64 より 3〜4 割重い**（同じ NEON でも）。両方入る端末では
  arm64 が選ばれるので問題は 32bit 専用機（AQUOS 603SH など）。
- → 手順 5 では、Android の既定は **854x480**（または端末の重さで選ぶ）にする。
  バックグラウンドでは小さいコアに寄せられるが、そのときは最後の絵の出し直し
  （1 秒に 1 枚）だけなので軽い。

ネットワーク: Xperia からは TV・Home Mini・Nest Hub とも ping の損失 0%、
**シェルから（ロックなしで）探しても 3 台見つかり**、`castplay --status mytv` も
成功した。**Pixel 7a で届かないのは Pixel 側（Android 17 か、その端末の無線）の
問題**と分かった。

Android の端末のシェルで流す試験のために `castplay <IP|名前> --live [秒] [WxH]` を
足した（SDL の要らない sdlcastdemo。ほとんど止まった絵 + 動く棒 + 毎秒のビープと
白い四角）。**TV へ流すのはユーザーの了解を得てから**（確認したとき TV では
「Screen Mirroring」が動いていた）。

### Xperia から TV へ流した（2026-09-25）

`castplay mytv --live 1800 854x480`（Claude Code の許可に、このコマンドだけを足してもらった）。
流し始めて 9 秒で PLAYING、遅れは 3.77〜3.81 秒で平ら（パソコンからと同じ）、
捨てたフレーム 0、無音の足し 0。

見つけて直したこと:

1. **SIGPIPE でプロセスごと落ちていた。** 1 回目は 330 秒で
   「child process was terminated by signal Broken pipe」。調べるためにパソコンから
   同じ流れを読んでいた curl を切った瞬間だった。POSIX では、相手が切った
   ソケットに書くと SIGPIPE が出てプロセスが終わる（Windows には無いので
   手順 3 では出なかった）。**mxv2 に入れたら、TV が再生をやめたり
   ネットワークが切れたりしただけでアプリが落ちる。** 直したのは 2 か所:
   `net::Send`（`MSG_NOSIGNAL`。HTTP の配信）と、TLS の送信（mbedTLS の
   `mbedtls_net_send` は `write()` なので、自前の送信関数 `TlsSendNoSignal` に
   差し替え）。直したあと、流している途中で別の読み手をつないで切っても続いた。
2. **映像が音から少しずつ遅れては、10〜15 秒ごとに一度に合わせ直す**
   （ユーザーが TV を見て気づいた。フレームが飛んだように見える）。
   流れそのものを curl で取って調べると、白い四角はビープの 7〜44ms 後で
   溜まっていない＝**流れは正しく、受信側（TV のプレーヤー）の動き**。
   ただし映像の時刻の間隔が 20〜47ms とばらばらだった（呼ぶ側が絵を
   渡した時刻がそのまま入る。見本は 8ms ごとに回っている。しかも間引きの間隔が
   整数の 33ms で、実際は 30.3fps）。→ **映像の時刻を fps の格子
   （枠 n の時刻 = n × 1000 / fps）に載せ、受け取る頃合いも流れの時計が次の枠に
   入ったとき**にした（`StreamEncoder::SlotOf / SlotMs`）。間隔は 33 / 34ms の
   繰り返しになり、**ユーザーが TV で見て改善を確認した**。mxv2 の描画の
   周期も揺れるので、この直しはそのまま効くはず。
3. 状態の表示の遅れ（`clientBaseMs`）が、最後につないだ読み手の値になっていた。
   curl をつなぐと -1.31 秒のような値になる。つないでいる中で一番古い読み手
   （ふつうは受信側）の値にした。

**直したあとの 30 分（1805 秒）**: 最後まで止まらず、溜め直しは始めの 1 回だけ、
無音の足し 0。遅れの表示は -1.29〜-1.35 秒でほぼ平ら（途中で curl をつないだ
ため、上の 3 の直し前の版では基準がずれて負になっている。値の揺れの幅だけが
意味を持つ）。**映像を捨てたのは 53811 枚中 137 枚（0.25%）**で、8 回ほどの
かたまり。そのときだけ 1 枚のエンコードが 35〜41ms に延びている（普段は
19〜22ms）。小さいコアに回されたか、ほかの仕事とぶつかったとみられる。
音は一度も途切れていない（**ユーザーが TV で聞いて、30 分間音声に乱れが無かったことを確認**。通奏音で途切れが分かる状態で）。CPU は平均 0.80G サイクル/秒（絵を作る分を含む）。
→ 手順 5 では、エンコードのスレッドの優先度を上げる（デコードのスレッドと
同じ `setpriority`）ことも考える。

見本の音に、ビープとは別の小さい通奏音（220Hz）を足した（ユーザーの提案。
途切れたら耳で分かる）。`castplay --live` と `sdlcastdemo`。

### 残り

- Pixel 7a から TV と Home Mini へ届かない件（端末側）。
- 32bit 専用機（AQUOS 603SH）と最小の XS17 での計測。
- アプリの中での CPU（読み出しを含む）は手順 5 で。

## 手順 5（mxv2 への組み込み）— 2026-09-25、実装してビルドまで（動作確認はユーザーに依頼）

| ファイル | 中身 |
|---|---|
| `src/cast.h` / `src/cast.cpp` | mxv2 からの使い方。`MXV2_CAST` が無ければ空の実装（`Available()` が false）。つなぐ・やめるは別スレッド（`SDLCast_Connect` は TLS の握手まで待つので、メインループを止めない） |
| `src/settingsui_cast.cpp` | ダイアログ。送り先の一覧（映像を出せるものだけ）→ [送る]、送っている間は状態と [キャストを終了]。[送っている間は手元の音を消す] |
| `src/player.*` | `SetAudioTap`。オーディオのコールバックの後に、装置へ渡す PCM（音量を掛けたあと）をそのまま渡す。取りこぼしの無音も渡す |
| `src/main.cpp` | `SetAudioTap` → `cast::Init`。表示の直前に `cast::CaptureFrame`（キャンバスの範囲だけ。ImGui も写る）、毎フレーム `cast::Poll`（背面でも）。終了時に `cast::Shutdown`（受信アプリも止める） |
| `src/settings.*` | `[Cast] MuteLocal`（既定 1） |
| `CMakeLists.txt` | `MXV2_CAST` = AUTO（既定。mbedTLS と vcpkg の成果物があれば組み込む）/ ON / OFF。手順 4 の `MXV2_CAST_SELFTEST` は外した |
| メニュー | [表示] に [キャスト…]（[小窓で表示] の下。送っている間はチェック） |

決めたこと（仮定）:
- **送る大きさ**: Android 854x480、パソコン 1280x720、30fps（手順 4 の計測から）。設定は作っていない。
- **絵の時刻**: オーディオのコールバックで「この頭の音（playedFrames）が流れのどの時刻に
  入ったか」を控え、`visualFrame`（いま聞こえている音の位置）から絵の時刻を出す。
  TV でも画面と音がそろう。音が 150ms 来ていなければ（一時停止）「いま」。
- **受信側が終わったら**（TV で別のアプリにした・受信アプリを閉じた・切れた）後始末して、
  理由をダイアログに出す。受信アプリが消えると状態は CONNECTED に戻るので、
  再生を見たあとの CONNECTED も「終わった」とみなす。
- **背面（Android）**: 描かないので絵は最後のまま、音は送り続ける。
- 送り先の一覧は `ca=` で画面の無いもの（Home Mini）を外す（`SDLCast_Device.capabilities`
  を足した。`castplay --list` にも出る）。
- 題名（受信側に出る）は `MXV2_APP_NAME`。

ユーザーの確かめで直したこと:
- **Windows で、送っていて鳴っている最中にアプリを閉じると、手元に音が戻って鳴った。**
  `cast::Shutdown` が送るのをやめた（＝消音を外した）あと、受信アプリを止め終わるまで
  最大 1.5 秒ほど待つ間も演奏が続いていた。→ 終了時は、送っていれば先に
  `player.Stop()` してから `cast::Shutdown`。

- **受信側から切られたら一時停止する**（ヘッドホンが抜けたときと同じ扱い。
  ユーザーの指示）。`cast::Poll()` は終わりを見つけると true を返すだけで、まだ
  送るのをやめない（手元は消音のまま）。main の `PollCast` が一時停止してから
  `cast::Stop()` で畳む（先に畳むと消音が外れて手元で鳴る。終了時と同じ理屈）。
  自分で [キャストを終了] を押したときは止めない。

- **鍵盤（映像）が音より遅れて見える**（ユーザーが TV で見て、60f 換算で Android 7〜8f・
  PC もいくらか。sdlcastdemo はほぼ 0）。原因は 3 つ:
  1. 映像の時刻を読み出すときの `visualFrame()` で取っていた。描いてから読むまでに
     オーディオのコールバックが挟まると 43ms 遅い時刻が付く → 描くのに使った `frame` を渡す。
  2. 受け取る頃合いを「渡された音声の長さ」の枠だけで決めていたので、音声が 43ms ずつ
     まとめて来る Android では最大 23fps（実測 20fps）→ 最後に音声が来てからの経過も
     足した見積もり（`StreamNowMsLocked`）で決める。
  3. **ウィンドウの大きさのまま読み出すのが重い**（Xperia で 720x1352 を 1 回 22ms。描画 48fps の
     うち 21〜23fps しか読めない）。絵は「次に読み出した絵」で初めて出るので、間隔が
     開くほど遅れて見える → 送っている間はウィンドウと同じ大きさのテクスチャへ描かせ、
     **GPU で送る大きさへ縮めてから読む**（`SDLCast_BeginRendererFrame / End`。2 倍より
     大きく縮めるときは 2 倍の大きさを経由して 2x2 平均に）。mxv2 は `Screen::Draw` の
     中で描画先を NULL に戻していたのを「元の描画先」に戻すよう直した。
  1 と 2 のあとで Android 3〜4f・PC 1〜2f に縮んだ（ユーザーの確認）。3 は確認待ち。
  3 のあと読み出しは 22ms → 9.6ms、30fps で読めるようになったが、**TV で映像の遅れが
  どんどん溜まった**。原因は私の作った枠の処理: mxv2 の絵の時刻（visualFrame）は
  音声のコールバックごと（43ms）にしか進まないので、30fps で読むと同じ時刻の絵が
  続く。エンコーダーは「枠が埋まっていたら次の枠へ」送っていたので、後ろの絵が
  押し出され続け、映像の時刻が実際より先へずれ続けた。→ **枠が埋まっていたら
  その絵は捨てる**（キーフレームを頼まれているときと出し直しだけ次の枠へ）。
  捨てた数は `sameTimeFrames`（ダイアログの `same`）。mxv2 側でも、演奏中に
  visualFrame が前回から進んでいなければ読み出さない（止まっているときは読む）。
  直したあと 3〜4f に戻った（溜まらなくなった）。残りは表示の粒: 再生位置が
  コールバックごと（Android 43ms）にしか進まず、鍵盤は次の段まで出ない（平均 21ms、
  最大 43ms）。→ `Player::visualFrame()` がコールバックからの経過（1 回ぶんまで）を
  足すようにした（演奏中だけ。手元の画面も細かくなる）。StatusWatch の 50Hz の見回り
  （平均 10ms、最大 20ms）は残る。
  それでも 3f ほど残り、**「映像を早める」補正（[Cast] VideoAdvanceMs、ダイアログの
  スライダー）を 200ms にしても効かなかった**（PC でユーザーが確認）。**本当の原因は
  WebM の書き出し**: `WebmWriter::AddBlock` が「どのトラックでも前の塊より時刻を
  戻さない」と揃えていた。mxv2 の絵は「いま聞こえている音」の時刻で、渡し終えた音より
  装置のバッファぶん古いので、書くときにはその時刻の音がもう書いてあり、**絵の時刻が
  音の時刻まで押し出されていた**（Android 43ms・PC 10ms ほど遅れる＝Android と PC の
  差の正体。補正もここで消えていた）。sdlcastdemo は音を渡すのと同じ時点の絵なので
  起きない。→ エンコーダーで音を kReorderMs（300ms）溜め、音と映像を時刻順に混ぜて
  書く（`WriteOrdered`）。受信側の遅れはそのぶん延びる（約 4.0 → 4.3 秒）。
  `sdlcast_encodetest --video-lag 100`（絵を 100ms 古い時刻で渡す）で、点滅がビープの
  7ms 後のまま（押し出されていれば 100ms 後）になることを確かめた。
  直したあと、ユーザーが TV で見て [映像を早める] を合わせた結果、**パソコン 16ms、
  Pixel 7a 48ms** がちょうどよい → これを既定値にした（`Settings::kCastAdvanceMsDefault`。
  Android かどうかで切り替え）。スライダーは送っている間もすぐ効くが、TV に見えるのは
  数秒後（TV の溜め方による）なので、その注記を出す。
  ログ: 送っている間 5 秒ごとに `cast     : draw .. fps, capture .. fps, read .. ms`。

- **TV のリモコンの PAUSE / PLAY**（ユーザーの判断で案 1）: 受信側の PAUSED を見たら
  手元を一時停止（`kEventRemotePause`）、PAUSED のあと BUFFERING / PLAYING を見たら
  手元を再開して `SDLCast_ReloadStream`（新しい URL で LOAD し直す。止めていた間も
  流れは進むので、そのままだと止めた長さだけ TV が遅れる）。読み込み直した直後に来る
  前の再生の IDLE（INTERRUPTED）は「終わった」と取らない（`reloading`）。TV で止めた
  まま手元で再開したときも読み込み直させる。手元で止めたときは TV は再生のまま
  （止まった絵と無音が流れる）。
  **読み込み直しで切れた件**（ユーザーの報告が 2 回）: 1 回目は `LOAD_FAILED`（受信側の
  自前の読み込み直しを遮った返事）→ こちらの LOAD の requestId 以外の失敗は無視。
  2 回目は `IDLE / INTERRUPTED`（こちらの LOAD に遮られた古い再生の知らせ）。
  `reloading` で無視するはずが、LOAD が効く前に古い再生の PLAYING が来て `reloading`
  が外れていた → **LOAD を送る前の mediaSessionId を控え、その古い再生の知らせは
  Session で捨てる**（`staleMediaSessionId_`）。
  直したあと PAUSE → PLAY は何度でも続いた。
- **TV のリモコンの左右キー**（飛ばす）で切れた: 目次（Cues）の無いライブの流れを飛ばそう
  として、受信側のプレーヤーが `ERROR`（detailedErrorCode 104）や `CANCELLED` で止まる。
  送る側からキーは止められない → 受信側の再生が IDLE の ERROR / CANCELLED で終わったら
  **読み込み直して続ける**（手元の演奏は止めない）。1 分に 3 回を超えたら直らないとみて
  終える。TV によっては STOP キーが CANCELLED になるかもしれない（そのときは止まらず
  読み込み直してしまう。受信アプリを閉じたとき＝CONNECTED に戻るのは従来どおり終える）。
- **受信側が受け付ける操作**（`supportedMediaCommands`、mytv で 274447）: PAUSE、SEEK、
  STREAM_VOLUME、STREAM_MUTE、EDIT_TRACKS、PLAYBACK_RATE、STREAM_TRANSFER。
  QUEUE_NEXT / PREV・SKIP は無い（今の作りでは TV の「次へ / 前へ」は効かない）。
  送る側にキーは届かず、受信側の状態の変化から推し量るだけ。値が変わるとログに出す
  （`sdlcast  : supportedMediaCommands ...`）。
- **前の接続の再生が残っていると、開始直後に「INTERRUPTED で終わった」と判断した**
  （アプリを入れ直して STOP せずに終わった再生を、こちらの LOAD が遮った知らせ）。
  staleMediaSessionId_ は同じ接続の中の読み込み直しにしか効かなかった → **LOAD への
  返事（同じ requestId の MEDIA_STATUS）で自分の mediaSessionId を知り、それまでと、
  それより古い id の知らせは捨てる**（`ourMediaSessionId_`。返事が 10 秒来なければ諦めて
  来たものを見る）。
  直したあと、開始直後に切れなくなり、LOAD の返事は 0.1 秒以内に来た（requestId は付く）。
- 左右キーからの立て直しの上限を「1 分に 3 回」にしていたら、左右キーを続けて押しただけで
  終わった → **読み込み直しても再生（PLAYING）に戻らないまま 3 回続いたら終える**に変えた
  （`recoverStreak`。PLAYING を見たら 0 に戻す）。
- **TV のリモコンの停止ボタン**も IDLE / CANCELLED になり、読み込み直すだけになっていた。
  受信側の状態の移り変わりをログに出して確かめた（`sdlcast  : media PLAYING -> ...`）:
  左右キーは PLAYING → BUFFERING →（PLAYING に戻るか、CANCELLED / ERROR）、停止ボタンは
  **PLAYING → IDLE CANCELLED と直に**。→ 直前の状態を `SDLCast_Status.prevPlayerState` で
  渡し、CANCELLED は BUFFERING からのときだけ読み込み直す。PLAYING / PAUSED からの
  CANCELLED は受信側で終えたものとして、手元を一時停止して終える。ERROR は読み込み直す。
  停止ボタンで終わることはユーザーが確認した。
- 左右キーで**くるくるが出たまま戻らない**ことがあった（PLAYING → BUFFERING のまま）。
  ライブの流れは飛べない: 先へ飛ぶと中身が来るまで待ち（8 秒で戻った例）、前へ飛ぶと
  二度と来ない中身を待ち続ける → **再生のあと BUFFERING が 2 秒を超えたら読み込み直す**
  （`bufferingSince`。3 回続けて戻らなければそれ以上はしない）。その間の表示は「送っています」のまま。
  左右キーの変な動きは無くなった（ユーザーの確認）が、**受信側の「再生中」の札（題名・時刻・
  赤丸と LIVE -0:01）が出たまま消えない**ようになった。ログでは読み込み直した再生は
  PLAYING に戻っている（流れは止まっていない）。→ 試しに、読み込み直した再生が PLAYING に
  なったら PLAY を 1 回送る（`SDLCast_PlayMedia`）→ **効かなかった**。ユーザーの観察:
  くるくるが出た飛ばしを読み込み直すと札が消えなくなり、PAUSE / RESUME でも消えず、
  次に「くるくるが出ない飛ばし」が済むまで残る＝**受信側の飛ばしが終わらないまま
  読み込み直したので、受信側の UI が飛ばしの途中のまま**。→ PLAY の後押しは外し、
  溜め直しが 2 秒を超えたら、まず **SEEK で受信側に届いているところ（流れの今の位置から
  4 秒手前）へ飛ばし直して、受信側の飛ばしを終わらせる**（`SDLCast_SeekToLive`）。
  6 秒を超えても戻らなければ読み込み直す。
  **これで正常になった**（ユーザーの確認、2026-09-25。くるくるも札も消える）。
- **保留（ユーザーの判断、2026-09-25）: 手元が演奏していないときの TV の PLAY キーを
  mxv2 の PLAY に割り当てる**。受信側は止まった絵と無音のライブを「再生中」なので、
  PLAY を押しても状態が変わらず何も届かない（ログで確認）。拾うには、手元が演奏して
  いない間は受信側を一時停止させておき、PAUSED → PLAYING を拾う形になる（副作用: その間
  TV の絵が止まる、一時停止の表示が残るかもしれない、演奏を始めると読み込み直しで数秒
  かかる）。リモコンまわりはここで一旦保留。

- **ダイアログの作り**（ユーザーの指摘で直した）: 一覧は 4 行、並びは送っていても
  いなくても同じ（大きさ・ボタンの位置を変えない）、[映像を早める] は名札を上の行に。
  **幅を SetNextWindowSizeConstraints で決めると、Android で中身が収まっているのに縦の
  スクロールバーが出た**（Pixel 7a の画面を adb で撮りながら試した。スクロールバーを
  隠しても何も切れない＝誤判定）。`SetNextWindowContentSize`（幅だけ）＋
  `AlwaysAutoResize` にしたら出なくなった。幅は画面の縁の余白（DisplaySafeAreaPadding）の
  内側に収める。F9 で開く（メニューにも F9、操作方法にも追加）。
  **横画面**（Pixel 7a）では 1 列だと高さが足りない → 1 列の高さを字と部品の行数から
  見積もり、画面の高さに収まらない横長の画面では 2 列（左: 一覧と状態、右: ボタンと
  調整項目。表 `##castcols`）にし、一覧・状態を 3 行に詰める。パソコンのウィンドウはふつう
  1 列のまま。横画面の adb 操作: バナー (300,860) → [表示] (360,488) → [キャスト…] (400,538)。
  横画面で開いてから縦画面にすると、ダイアログが下へ潜った（置き直した次のフレームで
  2 列→1 列に替わって背が伸びるため）→ ダイアログの大きさが変わったら次のフレームで
  中央へ置き直す（`castRecenter_`）。
- **未解決（2026-09-25）**: Pixel 7a で [キャスト] を開いたまま縦横を素早く何度も替えたら、
  画面が上下逆・赤青入れ替わり・横と縦の古い絵が混ざった状態で固まった（描画のループは
  動いている）。ログには回転のたびに `eglMakeCurrentImpl ... EGL_BAD_ACCESS`（SDL の
  surfaceChanged が UI スレッドから文脈を取ろうとして失敗）。ホームへ出て戻すと直る。
  送ってはいなかった（送っている間だけの描き方は動いていない）。今日の変更のせいか
  前からかは未確認（`settings put system user_rotation` ではアプリが回らず再現できない）。
  **追記（同日）**: `adb shell cmd window user-rotation lock 1 / lock 0`（戻すのは `free`）で
  アプリを回せる。ダイアログを開いたまま 40 回ほど素早く回すと再現する
  （scratchpad の rotstress.sh）。**送っていない普通のビルドでも起き、今日の描画の変更が
  入る前のデバッグ版（15:31）でも起きた**（こちらは真っ黒のまま）→ キャストや今日の
  変更のせいではなく、前からある（SDL / GL と回転の組み合わせ）。ダイアログを開いて
  いないと 20 回では起きなかった。ホームへ出て戻すと直る（デバッグ版は戻らなかった）。
  確かめ用に、送っていなくても Begin/End の描き方を 0.5 秒ごとに入り切りさせる
  `MXV2_CAST_FRAMETEST`（CMake。sdlcast 側は `SDLCast_TestForceRendererFrame`）を
  仮に足してあった（TEMPTEST の印）。→ 2026-09-25 夜に原因を直して取り除いた
  （screen_orientation.md「直した不具合」）。
  adb で開く手順: バナー（MXV のロゴ）を `input swipe 190 960 190 960 120` → [表示] →
  [キャスト…]（`input tap` だと効かないことがあった）。

まだ確かめていないこと（ユーザーに依頼）: 実際にメニューから送る流れ（パソコン・Android）、
パソコンでの初回のファイアウォールの許可、Android での CPU（読み出しを含む）、
長時間。Windows で試すなら Release（Debug は 720p のエンコードが追いつかない）。

## 手順 6（仕上げ）— 2026-09-25

- **NOTICE**: キャストのときに静的リンクするもの（Mbed TLS / libvpx / Opus / libyuv）を足した。
  `memo/THIRDPARTY.md` の表にも。libyuv に付いてくる libjpeg-turbo はリンクしない。
- **README**: 「キャストについて」の節（使い方、手元の音を消す、映像を早める、背面、
  受信側のリモコン、Windows のファイアウォール）。
- **ファイアウォール**: 受信側が流れを取りに来ないと、受信側はくるくるのまま何も
  言ってこない → 流し始めて 20 秒たっても誰も取りに来なければ、「送り先が映像を取りに
  来ませんでした。ファイアウォールで mxv2 の通信が許可されているか確かめてください。」
  （`Cast.NoFetch`、`cast::LastErrorKey`）を出して終える。まだ送り始めていないので手元の
  演奏は止めない。**Windows で実際にファイアウォールの確認が出るところ・拒否したときは
  未確認**（mxv2.exe の起動はユーザーの確認が要る）。
- **切れたときの後始末**: 見直した。ネットワークが切れた・TV の電源が落ちた → 心拍が
  15 秒来ない → Channel が閉じる → DISCONNECTED → 手元を一時停止して終える（受信側で
  終わったときと同じ）。受信側が消えたあとの書き込みは SIGPIPE を出さない（手順 4）。
  変更なし。
- 残り: 32bit 専用機・XS17 での確認、回転の不具合（`screen_orientation.md`）と仮の試験
  フラグ（TEMPTEST）、リモコンの保留分。→ 回転の不具合と TEMPTEST は 2026-09-25 夜に片付けた。

### AQUOS 603SH（32bit 専用機、Android 8.0、MSM8952 1.5GHz）で確認（2026-09-25）

リリース版（armeabi-v7a）で mytv へ送った。音声は OpenSL ES（AAudio が無い）。
描画 15〜21fps（Pixel 60）、TV へ送れた絵 11〜15fps、読み出し 27〜33ms（最大 83ms）。
CPU はエンコードのスレッドがほぼ 1 コア使い切り、描画 79%、デコード 45%。
**ユーザーが TV で見て、フレームレートは低いが動作に問題なし**。drop 9 / same 10 は
始めだけで、あとは増えなかった。→ 画質を落とす仕組み（自動・手動）は今は入れない。
起動直後の PiP の警告（setPictureInPictureParams: Can't find activity）はキャストと無関係。

### XS17（MT6737M、Linux 3.18、Android 8.1）でつながるまで 2 分かかった件（2026-09-25）

一覧に mytv は出るが、[送る] から 2 分以上つながらない（待てばつながる）。段階ごとの
時間をログに出すと（`cast     : connect N ms, receiver status N ms, stream start N ms`、
遅いときだけ sdlcast の `slow connect … (psa, tcp, seed, handshake)`）、connect が
135 秒、その内訳は **psa_crypto_init 39 秒・ctr_drbg_seed 86 秒**で、TCP 5ms・握手
0.2 秒。CPU はほぼ使っていない＝**mbedTLS が乱数の種に使う getrandom(2) が止まって
いた**（同じ端末で /dev/urandom は一瞬で読める）。

対処: Android だけ `MBEDTLS_USER_CONFIG_FILE`（`sdlcast/src/mbedtls_user_config.h`）で
`MBEDTLS_NO_PLATFORM_ENTROPY` + `MBEDTLS_ENTROPY_HARDWARE_ALT` にし、種は /dev/urandom を
読む `mbedtls_hardware_poll`（channel.cpp）から取る。構造体の形が変わるので定義は
mbedcrypto の PUBLIC に付ける（sdlcast 側にも効く）。third_party は無改変のまま。
castplay --status で 125 秒 → 0.23 秒。
直したあと mxv2 から: connect 116ms、TV で再生まで約 2.7 秒、描画 55fps・送出 30fps・
読み出し 9.5ms。**ユーザーが XS17 の確認を完了とした**（2026-09-25）。

## キャスト品質の設定（2026-09-26、ユーザーの指定）

解像度を PC / Android で決め打ちにしていたのを、[キャスト品質]（ダイアログの
[送っている間は手元の音を消す] の上。名札は上の行、選ぶ欄は幅いっぱい）で選べるようにした。
mxv2.ini の `[Cast] Quality`（0〜4）。表は `cast::GetQualityPreset`（src/cast.cpp）。

| 段 | 大きさ | fps | 映像 | 音声 |
|---|---|---|---|---|
| 0 最低 | 640x360 | 15 | 1Mbps | 128kbps |
| 1 低（Android の既定） | 854x480 | 30 | 2Mbps | 128kbps |
| 2 中（パソコンの既定） | 1280x720 | 30 | 2Mbps | 192kbps |
| 3 高 | 1920x1080 | 30 | 4Mbps | 192kbps |
| 4 最高 | 1920x1080 | 60 | 6Mbps | 192kbps |

（音声 192kbps は「中」以上。2026-09-26、ユーザーの指定で足した。）

- 送り始めるときに決まるので、送っている間は選ぶ欄を触れなくした（一覧と同じ）。
- 流す量の下限（minKbps 1000）はそのまま。
- パソコン（開発機）の `sdlcastg_encodetest --bench --static`（Release、10 秒）:
  720p30 は 1 枚 8ms・1 コアの 30%、1080p30 は 19ms・68%、1080p60 は 11ms・79%。
  どれも捨てたフレーム 0。1080p60 は余裕が少ないので、遅いパソコンでは捨てるかもしれない。
  スマートフォンの 1080p は重い（CPU は画素数 × fps にほぼ比例）が、選ぶのは使う人。
- 1 列の高さの見積もりに、名札 1 行と部品 1 行を足した。Pixel 7a の縦横で収まることを確認。

## 60fps（最高）で絵が全部届かない件（2026-09-26）

Pixel 7a で「最高」（1920x1080・60fps）を送ると、タイトルのスクロールが滑らかでない。
drop は 0。ログ（5 秒ごとの `cast     : draw … capture … encode … same …` に、エンコードした
絵の数と same を足した）で段階ごとに見た:

1. **受け取る頃合いの判定**（sdlcastg `WantVideoFrame`）で落ちていた: 描画 60fps に対し
   読み出し 41fps（演奏していないと 28fps）。「流れの時計が次の枠に入ったら受け取る」作りで、
   描画も同じ 60Hz なので揺れで 1 つの枠に 2 回来て 2 回目を断り、次の枠は空く。流れの時計は
   音がまとめて届くたびに跳ね、演奏していないと無音の足しでしか進まない。
   → **実時間で「次に受け取る予定」を持ち、予定の半枠手前から受け取り、予定を 1 枠ずつ進める**
   （平均はちょうど fps）。`StreamNowMsLocked` と `SlotOf` は不要になり消した。
   直したあと読み出し 56〜60fps。
2. 残り: エンコード 44〜45fps、same が毎秒 12〜14。絵の時刻（visualFrame から出す）が、
   音声のコールバック（Android 43ms）の揺れで止まったり跳ねたりし、続けて読んだ絵が同じ
   16.7ms の枠に入る。**ユーザーの判断で、数値で観測できているのでこれを限界として受け入れた。**
   直すなら: 絵の時刻を実時間でなめらかに進め、音の位置へは少しずつ寄せる（PLL のような形。
   EndFrame で「前の時刻 + 実時間の経過」を予測し、音から出した時刻との差の数 % だけ寄せ、
   差が大きければ合わせ直す）。あわせて「visualFrame が進んでいなければ読まない」は外せる。
3. PC（Release、mytv）で同じログを見た: 再生中は読み出し 59〜60fps・エンコード 58〜59fps・same は
   5 秒で 1〜7（PC の音声のコールバックは 96kHz・512 フレーム＝5.3ms と細かく、時刻が揺れにくい）。
   same が目立つのは曲を鳴らしていないとき（5 秒で 77）: 絵の時刻が「流れの今」＝エンコーダーが
   無音を足す刻みでしか進まないため。直すなら、鳴っていないときの時刻を流れを始めてからの実時間に
   する（encoder の SubmitVideoRGBA の ptsMs < 0 の扱い）。**ユーザーの判断で、この件はこれで OK
   （2026-09-26）。**

## 手動で [キャストを終了] したときも一時停止する（2026-09-26、ユーザーの指示）

以前は「自分で [キャストを終了] を押したときは止めない」としていたが、受信側で終わったときと
同じく、手元を一時停止してから終える。ダイアログは要求（SettingsUi::kRequestStopCast）を
出すだけで、main のループが Pause → cast::Stop の順に行う（先に止めないと、後始末で消音が
外れてその場で鳴る）。

# 演奏時間の測定と、任意時刻からの演奏

mxv2 は MDX の**総演奏時間**を曲を読み込んだ時点で知っていて、シークバーで
**任意の時刻へ飛ぶ**こともできる。どちらも「MDX を実際に最後まで（あるいは
目的の時刻まで）走らせてみる」ことで実現している。ここではその仕組みを、
下から順に説明する。

対象は mxv2 本体 (`src/player.cpp` / `src/statuswatch.cpp`) と、その下で
動いている portable_mdx (`third_party/portable_mdx/`) の MXDRV / x68sound。

---

## 0. 要約

| やりたいこと | 方法 |
|---|---|
| 総演奏時間を知る | 音を出さずに曲を最後まで**空回し**し、内部の時間カウンタを読む |
| 任意時刻から鳴らす | 曲の頭から目的時刻まで**空回し**してから、そこで音を出し始める |
| 今どこを鳴らしているか | OPM 割り込みのたびに時間カウンタを写し取る |

いずれも「MDX は再生しないと長さが分からない」という前提から来ている。MDX は
テンポ変更・ループ・PDX の有無で長さが変わるデータで、ファイルを眺めただけでは
演奏時間が決まらない。そこで**エミュレータを実時間から切り離して全速力で回す**。

---

## 1. 時間の物差し — OPM タイマー B と PLAYTIME

### 1.1 OPM の時計

X68000 の FM 音源 YM2151 (OPM) は 4MHz で動く。x68sound のエミュレーションは
これを 64 分周した **62500Hz** を内部の刻みにしている
(`x68sound_opm.cpp` の `Opm::pcmset62`)。出力サンプリングレート（48000Hz など）
とは別の時計で、PCM を 1 サンプル作るあいだに内部は 62500/48000 ≒ 1.3 回進む。

OPM にはタイマー A / B があり、MXDRV は**タイマー B を曲のテンポに使う**。
タイマー B の周期はレジスタ `$12` に書いた値 CLKB で決まる。

```
タイマー B 周期 = (256 - CLKB) * 1024 / 4,000,000 秒
                = (256 - CLKB) * 0.256 ミリ秒
```

エミュレーション側では `TimerB = (256 - CLKB) << 4` と持ち、62500Hz の刻みで
数えて溢れたら OPM 割り込みを起こす (`Opm::timer`)。MXDRV の演奏処理
（音符を読んで OPM に書く処理）は、すべてこの割り込みの中で動く。

### 1.2 PLAYTIME

MXDRV は割り込みが起きるたびに、その割り込みの重み（＝タイマー B の設定値）を
足し込む。`mxdrv.cpp` の `OPMINTFUNC`:

```c
OPMINT_FUNC( context );                      // 本来の演奏処理
if ( !G.STOPMUSICTIMER ) {
    G.PLAYTIME += 256 - G.MUSICTIMER;        // MUSICTIMER = OPM の $12 に書いた値
}
if ( MXCALLBACK_OPMINT ) MXCALLBACK_OPMINT( context );   // 差し替え可能なフック
```

`G.MUSICTIMER` は MXDRV がテンポ設定のときに控えている CLKB。したがって
**`PLAYTIME` の 1 は 0.256 ミリ秒**であり、ミリ秒への変換はどこでも同じ式になる。

```c
ms = PLAYTIME * 1024 / 4000
```

`PLAYTIME` はテンポが変わっても正しく積み上がる。速いテンポ（CLKB が大きい）では
割り込みの回数が増え、1 回あたりの重みが小さくなるからである。

**この `PLAYTIME` が、以下すべての土台になる。**

### 1.3 実時間から切り離せること

ここが肝心な性質で、OPM 割り込みは「実時間で 1 秒に何回」ではなく
「PCM を何サンプル作ったか」で起きる。音を出さずに割り込み関数を直接呼べば、
曲は CPU の速度で好きなだけ速く進む。演奏時間の測定もシークも、これを使う。

---

## 2. 演奏時間の測定 — MXDRV_MeasurePlayTime2

`src/player.cpp` の `Player::PlaySong` が、曲を読み込んだ直後に 1 回だけ呼ぶ。

```cpp
const int loops = maxLoops();
const bool fade = autoFadeout();
playTimeMs_ = MXDRV_MeasurePlayTime2(&context_, fade ? loops : loops + 1, fade ? 1 : 0);
```

### 2.1 手順

`MXDRV_MeasurePlayTime2` は次の順で動く。

1. **OPM 割り込みの配線を外す**（`X68Sound_OpmInt(..., NULL, NULL)`）。
   以後、割り込みはエミュレータからは呼ばれない。
2. `MeasurePlayTime = TRUE` を立てる。この間、MXDRV の OPM 書き込みルーチンと
   PCM 出力ルーチンは**先頭で return する**（`if ( MeasurePlayTime ) return;`）。
   つまり**音源には何も書かれず、音も出ない**。
3. OPM 割り込みのフック `MXCALLBACK_OPMINT` を、測定用の
   `MXDRV_MeasurePlayTime_OPMINT` に差し替える。
4. 演奏を開始する。ここで渡すコマンドは `d0 = 0x0f, d1 = -1`。
   `d1` はチャンネルマスクで、**-1 は全 16 チャンネルをミュート**の意味。
   同時に `PLAYTIME` が 0 に戻る。
5. **`while ( !TerminatePlay ) OPMINTFUNC( context );`**
   割り込み関数を自分で呼び続ける。1 回の呼び出しで曲がタイマー B 1 周期ぶん
   進むので、実時間とは無関係に最後まで走り切る。
6. 停止して、割り込みの配線とフックを元に戻す。

### 2.2 終わりの判定

毎回のフックで、3 つの終了条件を見る。

```c
if ( G.PLAYTIME >= G.MEASURETIMELIMIT ) TerminatePlay = TRUE;  // 時間切れ
if ( G.L001e13 != 0 )                   TerminatePlay = TRUE;  // 曲が終わった
if ( G.L002246 == 65535 )               TerminatePlay = TRUE;  // ループ数が異常
```

- `G.L001e13` は MXDRV の「演奏終了」フラグ。ループしない曲はここで止まる。
- `G.MEASURETIMELIMIT` は `MXDRV_Start` で **20 分 - 2 秒**に設定される。
  無限に続く曲（ループの検出に失敗するデータなど）で測定が終わらなくなるのを
  防ぐ止め弁。
- `G.L002246` はループ回数のカウンタ。

### 2.3 ループとフェードアウト

ループする曲は放っておくと終わらないので、**指定ループ数に達したところで
打ち切る**。フェードアウトする設定なら、そこでフェードを始めて、フェードが
終わって曲が止まるまで測り続ける。

```c
LoopCount = G.L002246;
if ( !FadeoutStart ) {
    if ( LoopCount >= LoopLimit ) {
        if ( ReqFadeout ) { FadeoutStart = TRUE; MXDRV_Fadeout(context); }
        else              { TerminatePlay = TRUE; }
    }
}
```

mxv2 が渡すループ数が設定と 1 ずれているのはこのため。

| 設定 | 渡す値 | 意味 |
|---|---|---|
| フェードアウトする | `loops`, `fadeout=1` | loops 回鳴ったらフェード開始。消えきるまでが演奏時間 |
| フェードアウトしない | `loops + 1`, `fadeout=0` | loops + 1 回目に入ったところで終わり |

フェードアウトしない場合に +1 するのは、「n 回ループする」を「n + 1 回目の頭で
止める」と数えるためで、これは旧 mxv と同じ勘定になっている。

### 2.4 返り値

```c
uint32_t ret = (DWORD)(G.PLAYTIME*(LONGLONG)1024/4000+(1-DBL_EPSILON))+2000;
G.PLAYTIME = 0;
return ret;
```

- `PLAYTIME * 1024 / 4000` でミリ秒へ。
- **`+2000` は 2 秒の余韻**。曲が止まった瞬間に演奏を切ると、残響や最後の
  音の減衰が途切れるため、後ろに 2 秒足してある。
- `+(1-DBL_EPSILON)` は切り上げのつもりの項だが、直前の除算がすでに整数除算
  なので結果は変わらない（原典から引き継いでいる無害な残骸）。
- 最後に `PLAYTIME` を 0 に戻す。これが無いと、このあと本番の演奏を始めたとき
  現在位置が測定ぶんだけ進んだところから始まってしまう。

### 2.5 費用

空回しは曲の長さに比例して時間が掛かる。5 分の曲・2 ループなら、測定だけで
10 分ぶんの OPM 割り込み（タイマー B の周期が 65ms なら約 1 万回、テンポが
速ければ数万回）を回すことになる。音を作らないぶん実時間よりずっと速いが、
**曲を切り替えるたびに必ず発生する処理**なので、mxv2 はこれを
**曲の読み込みスレッドではなくメインスレッドの `PlaySong` の中**で、
オーディオ装置とデコードスレッドを止めた状態で行っている。

---

## 3. 演奏中の現在位置

測定と同じ `PLAYTIME` を、本番の演奏中は**読むだけ**で使う。

### 3.1 OPM 割り込みのフックで写し取る

mxv2 は `MXCALLBACK_OPMINT` に自前のフックを掛けていて、その中で
`StatusWatch::OnOpmInt` が呼ばれる。

```cpp
nowTimeMs_ = (uint32_t)((uint64_t)g_->PLAYTIME * 1024 / 4000);
```

変換式は測定と同じ。演奏中の `PLAYTIME` は、デコードスレッドが
`MXDRV_GetPCM` で PCM を作るたびに、その中の OPM 割り込みで進む。つまり
**「今の時刻」は実時間ではなく、生成済みのサンプル数に紐づいている**。

### 3.2 50Hz のポーリングと表示

割り込みは毎回拾うが、画面へ渡すのはデコードスレッドが
**サンプル位置 1/50 秒ごと**に呼ぶ `Player::PollStep` のタイミングにまとめてある
(`StatusWatch::kPollHz = 50`)。旧 mxv の 50Hz ポーリングを、実時間ではなく
再生位置基準で再現したもの。

```cpp
uint32_t now = watch_.nowTimeMs();
if (watch_.terminated() && playTimeMs_ != 0 && now > playTimeMs_) now = playTimeMs_;
nowTimeMs_.store(now, std::memory_order_relaxed);
```

曲が終わったあとは、測定した総演奏時間で頭打ちにする。表示が
「総時間より先」へ行かないようにするため。

なお、ここで得た時刻は**デコード済みの位置**であって、スピーカーから出ている
位置ではない。両者はオーディオ装置のバッファぶんずれるので、画面に出す時刻は
`Player::visualFrame()` が遅らせて補正している（この補正の仕組みは本題から
外れるので割愛）。

---

## 4. 任意時刻からの演奏 — MXDRV_PlayAt

シークバーを離したときと、`,` / `.` キー（3 秒送り / 戻し）から呼ばれる。

### 4.1 考え方

MDX には「この時刻のデータはここ」という索引が無い。音色・音量・LFO・
ループ位置といった状態はすべて、先頭から順に処理して初めて決まる。
したがって**目的の時刻へ飛ぶ唯一の方法は、頭から空回しすること**になる。

### 4.2 手順

`MXDRV_PlayAt(context, playat_ms, loop, fadeout)` の中身。

1. OPM 割り込みの配線を外す。
2. `L_PLAY()` で演奏を頭から掛け直す（`PLAYTIME` が 0 に戻る）。
3. 目的時刻をミリ秒から `PLAYTIME` の単位へ直す。
   `playat = playat * 4000 / 1024`
4. 割り込みフックを、測定と同じ `MXDRV_MeasurePlayTime_OPMINT` に差し替える。
   ループとフェードアウトの扱いも測定と同じなので、**飛び先が曲の終わりより
   後ろでも暴走しない**。
5. チャンネルマスクを `-1`（全ミュート）にして演奏開始。
6. **`while ( G.PLAYTIME < playat ) { if (TerminatePlay) break; OPMINTFUNC(context); }`**
   目的時刻に届くまで割り込みを直接回す。
7. 割り込みの配線とフックを元に戻す。

測定との違いは 2 つ。

| | 測定 (`MeasurePlayTime2`) | シーク (`PlayAt`) |
|---|---|---|
| 止める条件 | 曲が終わるまで | 目的時刻に届くまで |
| `MeasurePlayTime` フラグ | TRUE（**OPM に何も書かない**） | FALSE（**OPM に書く**） |

シークでフラグを立てないのは、**飛び先で音を正しく鳴らすため**。空回しの
途中で設定された音色・音量・LFO などがそのまま音源に反映される。ただし
**空回しの間は PCM を作らないので、書き込みを消費する側が動いていない**。
書き込みは x68sound のコマンドバッファ（65535 段）に積まれる一方で、
`X68Sound_OpmWait(1)` を掛けても消費は起きない（消費は PCM 生成の中の
`Opm::ExecuteCmnd` でしか進まない）。

**このバッファは長いシークであふれる。** 溜め込みは

```c
if (NumCmnd < CMNDBUFSIZE) { ...積む...; ++NumCmnd; }
```

なので、いっぱいになったあとの書き込みは**黙って捨てられる**。捨てられるのは
**あとから来たほう**、つまり飛び先に近いほうの書き込みである。実測は下の
「7. 実測値」を見ること。

### 4.3 mxv2 側の段取り — Player::SeekMs

`MXDRV_PlayAt` は重く、しかも内部で OPM 割り込みハンドラを差し替える。
デコードスレッドと同時に走らせると壊れるので、曲を差し替えるときと同じ手順で
囲っている。

```cpp
bool Player::SeekMs(uint32_t ms) {
    if (playTimeMs_ != 0 && ms > playTimeMs_) ms = playTimeMs_;   // 総時間で頭打ち
    const bool wasPaused = paused_;

    SDL_PauseAudioDevice(audioDevice_, 1);   // 装置を止める
    StopDecodeThread();                      // デコードスレッドを止める

    const uint16_t mask = channelMask();     // ★ ミュート状態を退避
    MXDRV_PlayAt(&context_, ms, fade ? loops : loops + 1, fade ? 1 : 0);
    SetChannelMask(mask);                    // ★ 戻す
    MXDRV_FlushX68SoundCommandBuffer(&context_);  // ★ 溜まった書き込みを吐き出す

    displayReset_.store(true, ...);          // 画面の鍵盤を消させる
    watch_.Reset();
    dispQueue_.Clear();
    ResetClocks();                           // リングバッファごと捨てる

    nowTimeMs_.store(ms, ...);
    playTerminate_.store(false, ...);
    fadeoutStarted_ = false;
    paused_ = false;                         // PlayAt は演奏を掛け直すので解ける
    if (wasPaused) Pause();                  // 止まっていたなら止め直す

    StartDecodeThread();
    WaitForPrefill();                        // 溜まるまで待ってから
    ResumeAudioDevice();                     // 装置を動かす
}
```

要点を 4 つ。

- **チャンネルマスクは呼ぶ側で退避する。** `MXDRV_PlayAt` も内部で退避して
  いるように見えるが、控えを取るのが `L_PLAY()`（マスクをクリアする）の
  **後ろ**なので、実際には常に 0 を控えて 0 を書き戻している。mxv2 で
  ミュートしたチャンネルを保つには、呼ぶ側で挟むしかない。
- **一時停止は引き継ぐ。** `MXDRV_PlayAt` は演奏を掛け直すので一時停止が
  解ける。止まった状態でシークしたら、飛び先でも止まったままにする。
  掛け直すのは**デコードを始める前**でなければならない（あとからだと飛び先の
  音が一瞬だけ鳴る）。
- **画面の状態を捨てる。** 鍵盤の表示は「前は鳴っていた」という記憶と
  差分で動いているので、`watch_.Reset()` だけでは消す指示が出ない。
  `displayReset_` を立てて画面側に全消しを頼み、まだ消費していない表示
  イベントも `dispQueue_.Clear()` で捨てる。
- **オーディオのリングバッファも捨てる。** `ResetClocks()` で読み書き位置と
  セマフォを初期状態へ戻す。捨てないと、飛ぶ前の音がバッファのぶん鳴り続ける。

### 4.4 費用

空回しの量は**飛び先の時刻に比例する**（飛ぶ距離ではない）。曲の終わり近くへ
飛ぶほど時間が掛かり、3 秒送りを繰り返しても毎回頭から数え直す。
`SeekMs` はメインスレッドを塞いで走るので、この間は画面も止まる。

とはいえ実測（Release）では、いちばん遠い 6:00 への
シークでも空回し 9.1 ms ＋ 吐き出し 2.2 ms ＝ **11.3 ms** で、
1 フレーム（60Hz なら 16.7 ms）に収まる。数字は「7. 実測値」。

---

## 5. 終了判定と曲送り

演奏の終わりは 2 系統で判定している。どちらか一方でも立てば終わり。

```cpp
// 1. MXDRV が「終わった」と言っている（ループしない曲）
if (watch_.terminated()) playTerminate_.store(true, ...);   // G.L001e13 != 0

// 2. 最初に測った総演奏時間に達した（ループする曲）
if (playTimeMs_ != 0 && now >= playTimeMs_) playTerminate_.store(true, ...);
```

**2 が無いとループする曲は永遠に終わらない。** MXDRV は指定ループ数を知らない
（測定のときだけフックで見ていた）ので、本番の演奏では止まらないからである。

自動フェードアウトも `PollStep` が同じ場所で見ている。

```cpp
if (autoFadeout() && g->L002246 >= maxLoops() && !fadeoutStarted_) {
    fadeoutStarted_ = true;
    Fadeout();          // MXDRV_Fadeout
}
```

フェードアウトする設定なら、測定した総演奏時間にフェードのぶんも入っているので、
「消えきったところで 2 の条件に掛かる」という形で辻褄が合う。

終わったあと、`main.cpp` はさらに **1 秒の余韻**（`kLingerFrames`）を鳴らしてから
次の曲へ送る。前節の `+2000` と合わせて、曲間は最大 3 秒の余裕がある。

---

## 6. 既知の癖・制約

- **総演奏時間は「その時のループ数・フェードアウト設定」での値。** 設定を
  変えても、鳴っている曲の `playTimeMs_` は測り直さない。次の曲から効く
  （設定ウィンドウにもその旨の注記がある）。
- **20 分 - 2 秒で測定を打ち切る。** それより長い曲は、総演奏時間が
  20 分ちょうど（打ち切り + 2 秒の余韻）として扱われ、そこで演奏も切れる。
- **測定・シークの空回し中は OPM の書き込み通知が飛ばない。** mxv2 の
  レジスタ一覧オーバーレイは `MXDRV_SetOpmWriteCallback` で書き込みを
  拾っているが、測定中 (`MeasurePlayTime`) は MXDRV が先頭で return するため
  届かない。飛び先の値は、演奏が再開して次に書かれたときに入る。
- **シークは飛び先の時刻に比例して重い。** 索引を持たない MDX の構造上の制約。
- **2 分 37 秒より先へシークすると、OPM への書き込みがコマンドバッファから
  あふれて捨てられる**（7.3）。捨てられるのは飛び先に近いほうなので、
  飛んだ直後の音源の状態が正しくなかった。**2026-09-17 に直した**——バッファを
  4MB に広げ（7.5）、シークの直後に吐き出すようにした（7.6）。
  残る穴は「密度の高い曲の 15 分より先へ飛んだとき」だけ。
- **`PLAYTIME` は 32bit。** 0.256ms 刻みなので理論上 305 時間ぶんあり、
  20 分の上限のほうが先に効く。

---

## 7. 実測値

**すべて Windows の Release ビルドで測った値**（2026-09-17、最終版のコードで測り直し）。
Debug は最適化が効かないぶんおよそ 2 倍遅いので、参考として並べてある。
曲は `am_field.mdx`（総演奏時間 6:46.455）、ループ 2 回・フェードアウトあり
＝ mxv2 の既定。機械が違えば絶対値は変わるので、見るべきは比と桁。

計測は **`tools/benchmark.cpp`（`mxv2_benchmark`）**。`MXDRV_MeasurePlayTime2` と
`MXDRV_PlayAt` を直に呼び、OPM への書き込みは `MXDRV_SetOpmWriteCallback` で
数え、コマンドバッファの大きさと水位は `MXDRV_GetX68SoundCommandBuffer*` で読む。

```
make run-benchmark BUILD=release                          # 既定（am_field.mdx / 6:00）
make run-benchmark BUILD=release SEEKMS="60000 180000 360000"
make run-benchmark BUILD=release MDXFILE=testdata/bos01.mdx SEEKMS=90000
make run-benchmark BUILD=release BUFSIZE=65535 SEEKMS="360000 -noflush"  # 直す前と比べる
```

`MDXFILE` はカレントからの相対か絶対パス。そこに無ければ実行ファイルの隣
（＝ 同梱の `assets/`）からも探すので、既定値 `assets/mdx/ArctanX/am_field.mdx`
がそのまま通る。`SEEKMS` は空白区切りで複数書ける。

### 7.1 演奏時間の測定

曲を読み込むたびに 1 回通る（`Player::PlaySong`）。

| | **Release** | Debug |
|---|---|---|
| 測定に掛かった時間 | **約 7.0 ms** | 約 14.4 ms |
| 実時間に対する速さ | **約 58,000 倍** | 約 28,000 倍 |
| 得られた総演奏時間 | 406,455 ms (6:46.455) | 同じ |
| 空回し中の OPM 書き込み | **0 本**（`MeasurePlayTime` が止めている） | 同じ |

6 分 46 秒の曲を 7 ミリ秒で走り切る。曲を切り替えるたびに必ず通るが、
体感できる待ちにはならない。

### 7.2 シークの負荷

mxv2 の `SeekMs` は「空回し」と「吐き出し」（7.6）の 2 つを通る。

| 飛び先 | 空回し | 吐き出し | **合計 (Release)** | 合計 (Debug) | OPM 書き込み |
|---|---|---|---|---|---|
| 1:00 | 1.5 ms | 0.4 ms | **1.9 ms** | 3.8 ms | 25,080 本 |
| 3:00 | 4.6 ms | 1.2 ms | **5.8 ms** | 11.2 ms | 75,303 本 |
| 6:00 | 9.1 ms | 2.2 ms | **11.3 ms** | 21.8 ms | 150,639 本 |

**飛び先の時刻にきれいに比例する**（空回しはおよそ 1.5 ms / 分）。飛ぶ距離では
なく飛び先で決まるので、3 秒送りを繰り返しても 1 回あたりの費用は減らない。
`SeekMs` はメインスレッドを塞いで走るが、いちばん重い 6:00 でも 11 ミリ秒で、
1 フレーム（60Hz なら 16.7 ミリ秒）に収まる。

### 7.3 コマンドバッファ

ここから先はビルド構成によらない（積む本数の話なので）。

空回し中の書き込みは毎秒およそ 418 本（全チャンネルがミュートされた状態）。
通常の演奏では毎秒およそ 475 本。**portable_mdx の既定は 65,535 本なので、
約 157 秒 ＝ 2 分 37 秒ぶんでいっぱいになる。**

既定の 65,535 本のままだと、こうなっていた。

| 飛び先 | 書き込み | 水位 | 捨てられた分 | 掃けるまで |
|---|---|---|---|---|
| 1:00 | 25,080 | 25,080 / 65,535 (38%) | 0 | 約 0.6 秒 |
| 3:00 | 75,303 | 65,535 / 65,535 (**満杯**) | 9,768 (13%) | 約 1.6 秒 |
| 6:00 | 150,639 | 65,535 / 65,535 (**満杯**) | **85,104 (56%)** | 約 1.7 秒 |

掃け出しの速さは実測で毎秒およそ 41,000 本。これは理屈とも合う
（既定の `OpmWait = 240` なら `CmndRate / 4096 = 0.6665` 本を 62500 Hz で
消費するので毎秒 41,656 本）。

**2 分 37 秒より先へ飛ぶと、飛び先に近いほうの書き込みが捨てられる。**
さらに、演奏を再開してから **1.7 秒ほどは、曲の頭のほうの古い書き込みを
音源へ流し続けている**ことになる。6:00 へのシークでは、捨てられた 56% が
まさに飛び先付近の設定である。

### 7.4 バッファを大きくしてみた

コマンドバッファの大きさは実行時に変えられるようにした（7.5）。6:00 への
シークを、**大きさだけ**変えて測り直すとこうなる（吐き出しはしない）。

| バッファ | 水位 | 捨てられた分 | 掃けるまで |
|---|---|---|---|
| 65,535（portable_mdx の既定） | 満杯 | 85,104 (56%) | 約 1.7 秒 |
| 1,048,575（2MB） | 150,639 (14%) | **0** | **約 3.8 秒** |
| 2,097,151（4MB。今の mxv2） | 150,639 (7%) | **0** | **約 3.8 秒** |

**捨てられなくなる代わりに、掃けるまでの時間が倍以上に延びる。** 溜まって
いるのは「曲の頭から飛び先まで」の書き込みなので、鳴らし直したあとその
ぶんだけ古い設定を音源へ流し続けることになる。**大きくするだけでは音は
直らず、むしろ変な音の続く時間が延びる。**

そこで、大きくしたうえで**空回しのあとにバッファを吐き出す**ことにした（7.6）。

### 7.5 バッファの大きさを変える API

2026-09-17 に portable_mdx へ足した（gorry 版の拡張。本家には無い）。

| 層 | もの |
|---|---|
| x68sound | `X68Sound_SetCommandBufferSize` / `...GetCommandBufferSize` / `...GetCommandBufferUsed`、マクロ `X68SOUND_SUPPORT_ADJUST_COMMAND_BUFFER_SIZE` |
| MXDRV | `MXDRV_SetX68SoundCommandBufferSize` / `...Get...Size` / `...Get...Used` / **`MXDRV_FlushX68SoundCommandBuffer`**、マクロ **`MXDRV_SUPPORT_ADJUST_X68SOUND_COMMAND_BUFFER_SIZE`** |
| mxv2 | `Player::Config::opmCommandBufferEntries`（0 なら既定のまま）、`Player::kOpmCommandBufferEntries`、`Player::kCanAdjustOpmCommandBuffer` |

`CmndBuf` は固定長配列をやめて `malloc` で取るようにし、添字のマスク
（`CmndBufMask`）を実行時の値にした。指定した本数以上の「2 のべき乗」を
確保する。**取り直すと積んである内容は捨てる**ので、鳴らし始める前に呼ぶこと。

mxv2 は `opmCommandBufferEntries` を **2^21 - 1 本（4MB）**にしてある
（`Player::kOpmCommandBufferEntries`）。空回し中の書き込みは実測で毎秒
400〜2600 本（曲の密度による。`testdata/` で実測）なので、密度の高い曲でも
15 分ぶん、ふつうの曲なら 1 時間以上まかなえる。

### 7.6 シーク直後の吐き出し

7.4 のとおり、バッファを大きくするだけでは「古い設定を流し続ける時間」が
延びるだけで直らない。**シークの直後に、音を捨てながらバッファを吐き出す**
ところまでやって初めて直る。

`MXDRV_FlushX68SoundCommandBuffer(context)` を足した。中身は単純で、

1. `X68Sound_OpmWait(1)` で書き込み待ちを最短にする（1 回の `ExecuteCmnd` で
   160 本消費するようになる。既定の 240 では 0.67 本）
2. バッファが空になるまで `X68Sound_GetPcm` を回し、**出力は捨てる**
3. 待ちを元に戻す

`Player::SeekMs` が `MXDRV_PlayAt` の直後に呼ぶ。待ちを最短にしてあるので、
吐き出しのために進む曲の時間はごくわずかで済む。

**Release、`am_field.mdx` の 6:00 へシーク:**

| | 直す前 | 今 |
|---|---|---|
| バッファ | 65,535 本 | 2,097,151 本（4MB） |
| 水位 | 満杯・**56% を捨てる** | 150,639（7%）・捨てない |
| 空回し | 9.5 ms | 9.1 ms |
| 吐き出し | — | **2.2 ms**（PCM 768 フレーム ＝ 16 ms ぶんを作って捨てる） |
| 曲が進む量 | — | 14 ms |
| 鳴らし直してから正しくなるまで | **約 1.7 秒** | **0 秒**（鳴らす前に済んでいる） |

曲が 14 ms 進むが、シーク自体の着地誤差（6:00.005）と同じ桁なので問題にならない。
**2026-09-17、ユーザーが実機でシークが正確になったことを確認。**

残る穴は「密度の高い曲（毎秒 2600 本）の 15 分より先へ飛んだとき」だけで、
そのときは飛び先付近の書き込みが捨てられる。起きたらバッファをもう一段
大きくすればよい（`Player::kOpmCommandBufferEntries`）。

## 8. 関連ファイル

| ファイル | 役割 |
|---|---|
| `src/player.cpp` | `PlaySong`（測定の呼び出し）、`SeekMs`、`PollStep`（現在位置と終了判定） |
| `src/statuswatch.cpp` | `OnOpmInt`（`PLAYTIME` の写し取り）、`terminated()` |
| `src/mouse.cpp` | シークバーのドラッグ → `UpdateSeekDrag` → `SeekMs` |
| `src/keybind.cpp` | `,` `.` `<` `>` の 3 秒 / 30 秒送り |
| `third_party/portable_mdx/src/mxdrv/mxdrv.cpp` | `MXDRV_MeasurePlayTime2`、`MXDRV_PlayAt`、`OPMINTFUNC`、`MXDRV_GetTerminated` |
| `third_party/portable_mdx/src/x68sound/x68sound_opm.cpp` | `Opm::timer`（タイマー B）、`Opm::pcmset62`（62500Hz の刻み） |
| `third_party/portable_mdx/include/mxdrv.h` | `MXWORK_GLOBAL` の `PLAYTIME` / `MUSICTIMER` / `L001e13` / `L002246` |
| `tools/benchmark.cpp` | 7 章の数字を測るツール（`make run-benchmark`） |
| `third_party/portable_mdx/src/x68sound/x68sound_opm.cpp` | `Opm::SetCommandBufferSize`（7.5） |

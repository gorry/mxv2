// mxv2 - 演奏時間の測定とシークの負荷を測る
//
// MDX は再生してみるまで長さが分からず、途中の時刻を指す索引も持たない。
// mxv2 はどちらも「音を出さずに曲を全速力で走らせる」——空回し——で解いて
// いる（仕組みは memo/playtime.md）。このツールはその費用を測る。
//
//   [1] MXDRV_MeasurePlayTime2 … 総演奏時間の測定。曲を最後まで空回しする
//   [2] MXDRV_PlayAt           … 任意時刻へのシーク。頭からそこまで空回しする
//
// あわせて、空回しの間に x68sound のコマンドバッファへ積まれる OPM 書き込みの
// 本数も見る。**空回し中は PCM を作らないので誰も消費せず、積むだけになる。**
// バッファは 65535 段しか無いので、長いシークではあふれて捨てられる。
//
// 書き込みの本数は gorry 版 portable_mdx の拡張 MXDRV_SetOpmWriteCallback で
// 数え、バッファの大きさと水位は同じく拡張の
// MXDRV_GetX68SoundCommandBufferSize / ...Used で読む。
//
// 使い方:
//   mxv2_benchmark <mdxfile> [-buf <本数>] [-noflush] [シーク先ms ...]
//
// <mdxfile> はカレントディレクトリからの相対、または絶対パス。そこに無ければ
// **実行ファイルの隣**からも探す（同梱の assets/ を指せるように）。
// シーク先を省いたときは 360000 ms（6 分 0 秒）。
// -buf は OPM コマンドバッファの本数（MXDRV_SetX68SoundCommandBufferSize）。
// 省くと mxv2 と同じ本数で測る。
// -noflush はシーク直後の吐き出し（MXDRV_FlushX68SoundCommandBuffer）を
// 省く。mxv2 は必ず吐き出すので、省くのは「直す前」と比べるときだけ。
//
// Makefile からは `make run-benchmark MDXFILE=... SEEKMS=... BUFSIZE=...`。

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

#include <mxdrv.h>
#include <mxdrv_context.h>

#include "fileutil.h"
#include "mdxsong.h"
#include "vfs.h"

namespace {

const int kSampleRate = 48000;
// mxv2 の既定（settings.cpp）。Player は fadeout のとき loops、
// しないとき loops + 1 を MXDRV へ渡す。
const int kLoops = 2;
const int kFadeout = 1;
const uint32_t kDefaultSeekMs = 360000;  // 6 分 0 秒
// mxv2 の既定（src/player.h の Player::kOpmCommandBufferEntries）。
const int kMxv2BufEntries = (1 << 21) - 1;

long long g_opmWrites = 0;

void CountOpmWrite(MxdrvContext *, uint8_t, uint8_t) { g_opmWrites++; }

// x68sound のコマンドバッファ: 積める本数と、今いくつ積まれているか。
int CmndSize(MxdrvContext *ctx) { return MXDRV_GetX68SoundCommandBufferSize(ctx); }
int CmndDepth(MxdrvContext *ctx) { return MXDRV_GetX68SoundCommandBufferUsed(ctx); }

double Now() {
	using namespace std::chrono;
	return duration<double>(steady_clock::now().time_since_epoch()).count();
}

const char *BuildName() {
#ifdef NDEBUG
	return "Release";
#else
	return "Debug";
#endif
}

// 指定のまま読めなければ、実行ファイルの隣からも探す。
bool ResolveMdx(const mxv2::Vfs &vfs, const char *given, std::string *ref) {
	if (vfs.Resolve(given, std::string(), ref) && vfs.Exists(*ref)) return true;
	const std::string beside = mxv2::JoinPath(mxv2::ExecutableDir(), given);
	if (vfs.Resolve(beside, std::string(), ref) && vfs.Exists(*ref)) return true;
	return false;
}

void PrintMs(const char *label, uint32_t ms) {
	printf("%s%u ms (%u:%02u.%03u)", label, ms, ms / 60000, (ms / 1000) % 60, ms % 1000);
}

}  // namespace

int main(int argc, char **argv) {
	if (argc < 2) {
		printf("usage: %s <mdxfile> [-buf <entries>] [-noflush] [seekMs ...]\n", argv[0]);
		return EXIT_FAILURE;
	}
	std::vector<uint32_t> seeks;
	// 0 なら mxv2 と同じ本数（player.h の kOpmCommandBufferEntries）。
	int wantBufEntries = 0;
	bool doFlush = true;
	for (int i = 2; i < argc; i++) {
		if (strcmp(argv[i], "-buf") == 0 && i + 1 < argc) {
			wantBufEntries = atoi(argv[++i]);
			continue;
		}
		if (strcmp(argv[i], "-noflush") == 0) {
			doFlush = false;
			continue;
		}
		seeks.push_back((uint32_t)strtoul(argv[i], 0, 10));
	}
	if (seeks.empty()) seeks.push_back(kDefaultSeekMs);

	// ---- 曲を読む ------------------------------------------------------
	mxv2::Vfs vfs;
	vfs.Configure(std::string(), std::string());  // ローカル FS だけあれば足りる
	std::string ref;
	if (!ResolveMdx(vfs, argv[1], &ref)) {
		printf("ERROR: cannot find %s\n", argv[1]);
		printf("       (tried the path as given, and next to %s)\n",
		       mxv2::ExecutableDir().c_str());
		return EXIT_FAILURE;
	}
	mxv2::MdxSong song;
	std::string err;
	if (!mxv2::LoadMdxSong(vfs, ref, std::vector<std::string>(), &song, &err)) {
		printf("ERROR: %s\n", err.c_str());
		return EXIT_FAILURE;
	}
	printf("build  : %s / %d Hz\n", BuildName(), kSampleRate);
	printf("mdx    : %s\n", song.path.c_str());
	printf("title  : %s\n", song.title.c_str());
	printf("pdx    : %s", song.hasPdx ? song.pdxPath.c_str() : "(none)");
	if (song.hasPdx) printf(" (%llu bytes)", (unsigned long long)song.pdxBuffer.size());
	printf("\n");
	printf("loops  : %d / fadeout %d  (mxv2 の既定)\n", kLoops, kFadeout);

	// ---- MXDRV を起こす ------------------------------------------------
	MxdrvContext context;
	if (!MxdrvContext_Initialize(&context, 8 * 1024 * 1024)) {
		printf("ERROR: MxdrvContext_Initialize failed\n");
		return EXIT_FAILURE;
	}
	int ret = MXDRV_Start(&context, kSampleRate, 0, 0, 0, 1024 * 1024, 2 * 1024 * 1024, 0);
	if (ret != 0) {
		printf("ERROR: MXDRV_Start failed (%d)\n", ret);
		return EXIT_FAILURE;
	}
	MXDRV_PCM8Enable(&context, 1);
	MXDRV_TotalVolume(&context, 256);
	MXDRV_SetOpmWriteCallback(&context, CountOpmWrite);

	// OPM コマンドバッファ。-buf があれば取り直す（MXDRV_Start の直後、
	// 鳴らし始める前でなければならない）。
	{
		const int want = (wantBufEntries > 0) ? wantBufEntries : kMxv2BufEntries;
		const int r = MXDRV_SetX68SoundCommandBufferSize(&context, want);
		if (r != 0) {
			printf("ERROR: MXDRV_SetX68SoundCommandBufferSize failed (%d)\n", r);
			return EXIT_FAILURE;
		}
	}
	printf("cmndbuf: %d 本 (%.1f MB)%s\n", CmndSize(&context),
	       (double)(CmndSize(&context) + 1) * 2.0 / (1024.0 * 1024.0),
	       (wantBufEntries > 0) ? "  ← -buf で指定" : "  ← mxv2 と同じ");
	printf("flush  : %s\n\n", doFlush ? "する（mxv2 と同じ）" : "しない（-noflush）");

	void *mdx = (void *)(song.mdxBuffer.empty() ? NULL : &song.mdxBuffer[0]);
	void *pdx = (void *)(song.pdxBuffer.empty() ? NULL : &song.pdxBuffer[0]);
	ret = MXDRV_SetData2(&context, mdx, (uint32_t)song.mdxBuffer.size(), pdx,
	                     (uint32_t)song.pdxBuffer.size());
	if (ret != 0) {
		printf("ERROR: MXDRV_SetData2 failed (%d)\n", ret);
		return EXIT_FAILURE;
	}

	// ---- [1] 演奏時間の測定 --------------------------------------------
	// mxv2 は曲を読み込むたびに 1 回ここを通る（Player::PlaySong）。
	printf("[1] MXDRV_MeasurePlayTime2 — 総演奏時間の測定\n");
	uint32_t playTimeMs = 0;
	double bestMs = 0.0;
	for (int pass = 0; pass < 3; pass++) {
		g_opmWrites = 0;
		const double t0 = Now();
		const uint32_t ms = MXDRV_MeasurePlayTime2(&context, kLoops, kFadeout);
		const double elapsed = (Now() - t0) * 1000.0;
		playTimeMs = ms;
		if (pass == 0 || elapsed < bestMs) bestMs = elapsed;
		printf("  pass%d : %8.2f ms  →  ", pass + 1, elapsed);
		PrintMs("", ms);
		printf("\n");
		if (pass == 0) {
			printf("          OPM 書き込み %lld 本（空回し中は通知が止まる）\n",
			       g_opmWrites);
		}
	}
	if (bestMs > 0.0) {
		printf("  実時間に対する速さ: 約 %.0f 倍\n\n", (playTimeMs / 1000.0) / (bestMs / 1000.0));
	}

	// ---- [1.5] 通常の演奏で 1 秒あたり何本 OPM に書くか ----------------
	// 空回し中は全チャンネルがミュート (mask = -1) なので本数が違う。
	// バッファの水位を読むときの物差しにする。
	{
		MXDRV_Play2(&context);
		std::vector<int16_t> buf(1024 * 2);
		g_opmWrites = 0;
		const int secs = 10;
		for (int n = 0; n < kSampleRate * secs / 1024; n++) {
			MXDRV_GetPCM(&context, &buf[0], 1024);
		}
		printf("[1.5] 通常の演奏（全チャンネル発音）: %lld 本 / %d 秒 = %.0f 本/秒\n\n",
		       g_opmWrites, secs, (double)g_opmWrites / secs);
	}

	// ---- [2] 任意時刻へのシーク ----------------------------------------
	printf("[2] MXDRV_PlayAt — 任意時刻へのシーク\n");
	for (size_t i = 0; i < seeks.size(); i++) {
		const uint32_t want = seeks[i];

		// 鳴っている途中から飛ぶ状況を作る（Player::SeekMs と同じ）。
		// 1 秒ぶん鳴らして、溜まったコマンドを掃けさせておく。
		MXDRV_Play2(&context);
		{
			std::vector<int16_t> buf(1024 * 2);
			for (int n = 0; n < kSampleRate / 1024; n++) {
				MXDRV_GetPCM(&context, &buf[0], 1024);
			}
		}

		g_opmWrites = 0;
		const double t0 = Now();
		MXDRV_PlayAt(&context, want, kLoops, kFadeout);
		const double elapsed = (Now() - t0) * 1000.0;
		const uint32_t landed = MXDRV_GetPlayAt(&context);
		const int depth = CmndDepth(&context);
		const long long dropped = g_opmWrites - depth;

		PrintMs("  ", want);
		printf(" へシーク: %8.2f ms\n", elapsed);
		PrintMs("    着地          : ", landed);
		printf("%s\n", (landed + 500 < want) ? "  ※ 曲の終わりで打ち切られた" : "");
		printf("    OPM 書き込み  : %lld 本\n", g_opmWrites);
		const int size = CmndSize(&context);
		printf("    バッファ水位  : %d / %d (%.1f%%)%s\n", depth, size,
		       100.0 * (double)depth / (double)size,
		       dropped > 0 ? "  ★ あふれて捨てている" : "");
		if (dropped > 0) {
			printf("    捨てられた分  : %lld 本 (%.0f%%)  ← 飛び先に近いほうから捨てられる\n",
			       dropped, 100.0 * (double)dropped / (double)g_opmWrites);
		}

		// シーク直後の吐き出し（mxv2 の Player::SeekMs と同じ）。
		if (doFlush) {
			const uint32_t before = MXDRV_GetPlayAt(&context);
			const double f0 = Now();
			const int frames = MXDRV_FlushX68SoundCommandBuffer(&context);
			const double f1 = Now();
			printf("    吐き出し      : %.2f ms / PCM %d フレーム (%.1f ms ぶん) / "
			       "曲が %u ms 進む / 残り %d 本\n",
			       (f1 - f0) * 1000.0, frames, 1000.0 * frames / kSampleRate,
			       MXDRV_GetPlayAt(&context) - before, CmndDepth(&context));
		}

		// 鳴らし直したとき、溜まったぶんが掃けるまでどれだけ PCM を作るか。
		{
			std::vector<int16_t> buf(256 * 2);
			const int step = kSampleRate / 8;  // 0.125 秒ずつ（256 で割り切れる）
			long long rendered = 0;
			printf("    掃け出し      :");
			for (int q = 1; q <= 40; q++) {
				for (int n = 0; n < step / 256; n++) {
					MXDRV_GetPCM(&context, &buf[0], 256);
					rendered += 256;
				}
				const int d = CmndDepth(&context);
				if (d == 0) {
					printf("  %.3fs で空", (double)rendered / kSampleRate);
					break;
				}
				printf("  %.3fs:%d", (double)rendered / kSampleRate, d);
			}
			printf("\n");
		}
	}

	MXDRV_End(&context);
	MxdrvContext_Terminate(&context);
	return EXIT_SUCCESS;
}

// mxv2 - デコード分割の等価性テスト
//
// mxv2 のデコードスレッドは、StatusWatch のポーリング境界 (48000/50 = 960
// サンプル) に合わせて MXDRV_GetPCM の呼び出しを細切れにしている。
// これが出力波形を変えないことを確認する。
//
//   passA: 固定 512 サンプルずつデコード (portable_mdx のサンプルと同じ)
//   passB: mxv2 のデコードスレッドと同じ、ポーリング境界で切った分割
//
// 両者がバイト単位で一致すれば、分割は無害。
//
// 使い方: mxv2_chunktest <mdxfile> [秒数]

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <mxdrv.h>
#include <mxdrv_context.h>

#include "fileutil.h"
#include "mdxsong.h"

namespace {

const int kSampleRate = 48000;
const int kPollHz = 50;
const int kBlockFrames = 512;

bool Render(const mxv2::MdxSong &song, int totalFrames, bool pollAligned,
            std::vector<int16_t> *out) {
	MxdrvContext context;
	if (!MxdrvContext_Initialize(&context, 8 * 1024 * 1024)) {
		printf("MxdrvContext_Initialize failed\n");
		return false;
	}
	int ret = MXDRV_Start(&context, kSampleRate, 0, 0, 0, 1024 * 1024, 2 * 1024 * 1024, 0);
	if (ret != 0) {
		printf("MXDRV_Start failed (%d)\n", ret);
		MxdrvContext_Terminate(&context);
		return false;
	}
	MXDRV_PCM8Enable(&context, 1);
	MXDRV_TotalVolume(&context, 256);

	void *mdx = (void *)(song.mdxBuffer.empty() ? NULL : &song.mdxBuffer[0]);
	void *pdx = (void *)(song.pdxBuffer.empty() ? NULL : &song.pdxBuffer[0]);
	ret = MXDRV_SetData2(&context, mdx, (uint32_t)song.mdxBuffer.size(), pdx,
	                     (uint32_t)song.pdxBuffer.size());
	if (ret != 0) {
		printf("MXDRV_SetData2 failed (%d)\n", ret);
		MXDRV_End(&context);
		MxdrvContext_Terminate(&context);
		return false;
	}
	// Player と同じ手順を踏む。MXDRV_MeasurePlayTime2 は曲を一度走らせるため、
	// これを呼ぶかどうかで以降の波形がわずかに変わる。
	MXDRV_MeasurePlayTime2(&context, 1, 0);

	MXDRV_Play2(&context);

	out->assign((size_t)totalFrames * 2, 0);

	const int framesPerPoll = kSampleRate / kPollHz;
	uint64_t cursor = 0;
	uint64_t nextPoll = 0;
	int written = 0;
	while (written < totalFrames) {
		int blockFrames = kBlockFrames;
		if (written + blockFrames > totalFrames) blockFrames = totalFrames - written;

		int done = 0;
		while (done < blockFrames) {
			int n = blockFrames - done;
			if (pollAligned) {
				if (cursor >= nextPoll) {
					nextPoll += (uint64_t)framesPerPoll;
					continue;
				}
				uint64_t untilPoll = nextPoll - cursor;
				if ((uint64_t)n > untilPoll) n = (int)untilPoll;
			}
			MXDRV_GetPCM(&context, &(*out)[((size_t)written + done) * 2], n);
			cursor += (uint64_t)n;
			done += n;
		}
		written += blockFrames;
	}

	MXDRV_End(&context);
	MxdrvContext_Terminate(&context);
	return true;
}

}  // namespace

int main(int argc, char **argv) {
	if (argc < 2) {
		printf("usage: %s <mdxfile> [seconds]\n", argv[0]);
		return EXIT_FAILURE;
	}
	const int seconds = (argc >= 3) ? atoi(argv[2]) : 20;

	mxv2::MdxSong song;
	std::string err;
	if (!mxv2::LoadMdxSong(argv[1], std::vector<std::string>(), &song, &err)) {
		printf("ERROR: %s\n", err.c_str());
		return EXIT_FAILURE;
	}
	printf("mdx   : %s\n", song.path.c_str());
	printf("title : %s\n", song.title.c_str());
	printf("pdx   : %s\n", song.hasPdx ? song.pdxPath.c_str() : "(none)");

	const int totalFrames = kSampleRate * seconds;
	std::vector<int16_t> a, b;
	if (!Render(song, totalFrames, false, &a)) return EXIT_FAILURE;
	if (!Render(song, totalFrames, true, &b)) return EXIT_FAILURE;

	if (a.size() != b.size()) {
		printf("NG: size mismatch\n");
		return EXIT_FAILURE;
	}
	if (memcmp(&a[0], &b[0], a.size() * sizeof(int16_t)) != 0) {
		size_t at = 0;
		while (at < a.size() && a[at] == b[at]) at++;
		printf("NG: differs at sample %llu (%d vs %d)\n", (unsigned long long)at,
		       (int)a[at], (int)b[at]);
		return EXIT_FAILURE;
	}

	// 参考値。原典 MXDRVg の WAV との A/B 比較に使う。
	int peak = 0;
	double sum = 0.0;
	for (size_t i = 0; i < a.size(); i++) {
		int v = a[i] < 0 ? -a[i] : a[i];
		if (v > peak) peak = v;
		sum += (double)a[i] * a[i];
	}
	printf("OK: %d sec / %llu samples identical (peak=%d rms=%.0f)\n", seconds,
	       (unsigned long long)a.size(), peak, sqrt(sum / a.size()));
	return EXIT_SUCCESS;
}

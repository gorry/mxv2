// mxv2 - ビジュアライズ用イベントキュー
//
// 旧 mxv の DISPWORKBUF に相当する。ただし打刻を実時間 (timeGetTime) から
// 「累積サンプル位置」へ変更している。
//
//   旧 mxv : MXDRV.DLL が waveOut で自走 → OPM 割り込み ≒ 実時間だった。
//            そのため実時間で打刻し、固定遅延 DISP_LATE 経過後に取り出していた。
//   mxv2   : portable_mdx は pull 型 (MXDRV_GetPCM) なので、OPM 割り込みは
//            「デコード時刻」に呼ばれ、実際の発音よりリングバッファ深さ分だけ
//            先行する。実時間で打刻するとズレるため、デコード済みサンプル位置で
//            打刻し、SDL オーディオコールバックが数えた再生済みサンプル位置に
//            追いついたものだけを描画する。
//
// 生産者は OPMINT コールバック（デコードスレッド）、消費者は描画（メイン
// スレッド）の 1 対 1 なので SPSC リングとして扱う。

#ifndef MXV2_DISPQUEUE_H
#define MXV2_DISPQUEUE_H

#include <atomic>
#include <cstdint>
#include <vector>

namespace mxv2 {

enum DispCmd {
	DISP_NONE = 0,
	DISP_KEYOFF,
	DISP_KEYON,
	DISP_KEYBEND,
	DISP_VOLUME,
	DISP_PANPOT,
	DISP_DETUNE,
	DISP_VOICE,
	DISP_Q,
	DISP_PTR,
	DISP_LFOPITCH,
	DISP_LFOPITCH1,
	DISP_LFOPITCH2,
	DISP_LFOPITCH3,
	DISP_LFOPITCH4,
	DISP_LFOVOLUME,
	DISP_LFOVOLUME1,
	DISP_LFOVOLUME2,
	DISP_LFOVOLUME3,
	DISP_LFOVOLUME4,
	DISP_LEVELMETER,
	DISP_PCMVOLUME,
	DISP_PCMPTR,
};

struct DispWork {
	uint64_t frame;  // このイベントが発生したデコード済みサンプル位置
	uint8_t cmd;
	uint8_t param1;
	uint8_t param2;
	uint8_t param3;
};

class DispQueue {
public:
	// 旧 mxv と同じ 64K エントリ。2 のべき乗であること（& マスクで回すため）。
	static const uint32_t kCapacity = 1024 * 64;

	// バッファは 1MB になるのでヒープに置く（スタックには載らない）。
	DispQueue() : buf_(kCapacity), head_(0), tail_(0), dropped_(0) {}

	// 消費側が停止している状態でのみ呼ぶこと（曲の切り替え時など）。
	void Clear() {
		head_.store(0, std::memory_order_relaxed);
		tail_.store(0, std::memory_order_relaxed);
		dropped_.store(0, std::memory_order_relaxed);
	}

	// 生産者専用。満杯なら捨てて false を返す。
	bool Push(const DispWork &w) {
		uint32_t head = head_.load(std::memory_order_relaxed);
		uint32_t next = (head + 1) & (kCapacity - 1);
		if (next == tail_.load(std::memory_order_acquire)) {
			dropped_.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		buf_[head] = w;
		head_.store(next, std::memory_order_release);
		return true;
	}

	void Push(uint64_t frame, uint8_t cmd, uint8_t p1, uint8_t p2, uint8_t p3) {
		DispWork w;
		w.frame = frame;
		w.cmd = cmd;
		w.param1 = p1;
		w.param2 = p2;
		w.param3 = p3;
		Push(w);
	}

	// 消費者専用。先頭を覗く。空なら false。
	bool Peek(DispWork *out) const {
		uint32_t tail = tail_.load(std::memory_order_relaxed);
		if (tail == head_.load(std::memory_order_acquire)) return false;
		*out = buf_[tail];
		return true;
	}

	// 消費者専用。先頭を捨てる。
	void Pop() {
		uint32_t tail = tail_.load(std::memory_order_relaxed);
		if (tail == head_.load(std::memory_order_acquire)) return;
		tail_.store((tail + 1) & (kCapacity - 1), std::memory_order_release);
	}

	uint32_t dropped() const { return dropped_.load(std::memory_order_relaxed); }

private:
	std::vector<DispWork> buf_;
	std::atomic<uint32_t> head_;  // 生産者が書く
	std::atomic<uint32_t> tail_;  // 消費者が書く
	std::atomic<uint32_t> dropped_;

	DispQueue(const DispQueue &);
	DispQueue &operator=(const DispQueue &);
};

}  // namespace mxv2

#endif  // MXV2_DISPQUEUE_H

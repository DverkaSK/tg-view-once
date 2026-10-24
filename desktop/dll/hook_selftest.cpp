// In-process proof that the hook engine + core work together: hook a local
// function that stands in for the send seam, call it with a real uploaded-doc
// blob, and verify the detour set round_message in place AND the original
// function still ran (trampoline intact). No injection, no Telegram.
#include "../core/round_rewrite.h"
#include "../core/tl.h"
#include "vendor/minhook/include/MinHook.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using SeamFn = void (*)(uint8_t *body, size_t size);

volatile bool g_originalRan = false;
uint32_t g_flagsSeenByOriginal = 0; // video flags as the original saw them

// Stand-in for the pre-encryption send function. noinline + an exported-ish
// address use keep it present and hookable in the optimized build.
__attribute__((noinline)) void realSeam(uint8_t *body, size_t size) {
	g_originalRan = true;
	for (size_t i = 0; i + 8 <= size; i += 4) {
		uint32_t id;
		std::memcpy(&id, body + i, 4);
		if (id == tl::kDocAttrVideo) {
			std::memcpy(&g_flagsSeenByOriginal, body + i + 4, 4);
			break;
		}
	}
}

SeamFn g_original = nullptr;

void detour(uint8_t *body, size_t size) {
	mod::MakeUploadedVideoRound(body, size); // edit in place before sending
	if (g_original) g_original(body, size);  // then the real send
}

std::vector<uint8_t> buildVideoRequest() {
	tl::Builder a;
	a.putU32(tl::kDocAttrFilename);
	a.putString("v.mp4");
	a.putU32(tl::kDocAttrVideo);
	a.putU32(0); // flags: not round yet
	a.putDouble(5.0);
	a.putI32(640);
	a.putI32(480);

	tl::Builder b;
	b.putU32(tl::kInputMediaUploadedDocument);
	b.putU32(0);
	b.putU32(tl::kInputFile);
	b.putI64(1);
	b.putI32(3);
	b.putString("v.mp4");
	b.putString("md5");
	b.putString("video/mp4");
	b.putU32(tl::kVector);
	b.putU32(2);
	b.putRaw(a.bytes());
	return b.take();
}

int g_failures = 0;
void check(bool ok, const char *name) {
	std::printf("%s %s\n", ok ? "[ ok ]" : "[FAIL]", name);
	if (!ok) ++g_failures;
}

} // namespace

int main() {
	volatile SeamFn keep = &realSeam; // prevent elision
	(void)keep;

	if (MH_Initialize() != MH_OK) {
		std::printf("[FAIL] MH_Initialize\n");
		return 1;
	}
	if (MH_CreateHook(
			reinterpret_cast<void *>(&realSeam),
			reinterpret_cast<void *>(&detour),
			reinterpret_cast<void **>(&g_original)) != MH_OK) {
		std::printf("[FAIL] MH_CreateHook\n");
		return 1;
	}
	if (MH_EnableHook(reinterpret_cast<void *>(&realSeam)) != MH_OK) {
		std::printf("[FAIL] MH_EnableHook\n");
		return 1;
	}

	auto req = buildVideoRequest();
	realSeam(req.data(), req.size()); // goes through the detour

	// Find the video flags in the (possibly modified) buffer.
	uint32_t finalFlags = 0;
	for (size_t i = 0; i + 8 <= req.size(); i += 4) {
		uint32_t id;
		std::memcpy(&id, req.data() + i, 4);
		if (id == tl::kDocAttrVideo) {
			std::memcpy(&finalFlags, req.data() + i + 4, 4);
			break;
		}
	}

	check(g_originalRan, "original ran (trampoline intact)");
	check((finalFlags & tl::kVideoFlagRoundMessage) != 0,
		"round_message set in buffer after hooked call");
	check((g_flagsSeenByOriginal & tl::kVideoFlagRoundMessage) != 0,
		"original saw the round_message edit (in-place, before send)");

	MH_DisableHook(reinterpret_cast<void *>(&realSeam));
	MH_Uninitialize();

	std::printf("\n%s\n", g_failures ? "FAILED" : "ALL PASSED");
	return g_failures ? 1 : 0;
}

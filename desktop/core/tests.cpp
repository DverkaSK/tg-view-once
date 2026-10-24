// Offline tests for the round-message transform. No injection, no account:
// hand-built TL blobs that mirror what Telegram Desktop serializes for a sent
// video, then we assert the transform flips round_message and nothing else.
#include "round_rewrite.h"
#include "tl.h"

#include <cstdint>
#include <cstdio>
#include <optional>
#include <random>

using tl::Builder;

namespace {

int g_failures = 0;

void check(bool ok, const char *name) {
	std::printf("%s %s\n", ok ? "[ ok ]" : "[FAIL]", name);
	if (!ok) ++g_failures;
}

// --- blob builders -------------------------------------------------------

void putFilename(Builder &b, const std::string &name) {
	b.putU32(tl::kDocAttrFilename);
	b.putString(name);
}

void putAnimated(Builder &b) {
	b.putU32(tl::kDocAttrAnimated);
}

void putAudio(Builder &b, int duration) {
	b.putU32(tl::kDocAttrAudio);
	b.putU32(0);            // flags: no voice/title/performer/waveform
	b.putI32(duration);     // duration:int
}

void putVideo(Builder &b, uint32_t flags, double duration, int w, int h) {
	b.putU32(tl::kDocAttrVideo);
	b.putU32(flags);        // flags (round_message is bit 0)
	b.putDouble(duration);  // duration:double
	b.putI32(w);            // w:int
	b.putI32(h);            // h:int
}

void putInputFile(Builder &b, bool big) {
	if (big) {
		b.putU32(tl::kInputFileBig);
		b.putI64(123456789);     // id:long
		b.putI32(84);            // parts:int
		b.putString("video.mp4");// name
	} else {
		b.putU32(tl::kInputFile);
		b.putI64(123456789);     // id:long
		b.putI32(3);             // parts:int
		b.putString("video.mp4");// name
		b.putString("deadbeef"); // md5_checksum
	}
}

// Builds an inputMediaUploadedDocument blob. `attrs` is appended as the
// attribute vector contents; `attrCount` is its element count.
std::vector<uint8_t> buildUploaded(
		bool fileBig,
		bool hasThumb,
		const std::vector<uint8_t> &attrs,
		uint32_t attrCount) {
	Builder b;
	b.putU32(tl::kInputMediaUploadedDocument);
	const uint32_t flags = hasThumb ? (1u << 2) : 0u;
	b.putU32(flags);
	putInputFile(b, fileBig);          // file
	if (hasThumb) putInputFile(b, false); // thumb
	b.putString("video/mp4");          // mime_type
	b.putU32(tl::kVector);
	b.putU32(attrCount);
	b.putRaw(attrs);
	return b.take();
}

// Reads the documentAttributeVideo flags out of a finished blob, for checking.
std::optional<uint32_t> readVideoFlags(const std::vector<uint8_t> &buf) {
	for (size_t i = 0; i + 8 <= buf.size(); i += 4) {
		uint32_t id;
		std::memcpy(&id, buf.data() + i, 4);
		if (id == tl::kDocAttrVideo) {
			uint32_t flags;
			std::memcpy(&flags, buf.data() + i + 4, 4);
			return flags;
		}
	}
	return std::nullopt;
}


} // namespace

int main() {
	using mod::MakeUploadedVideoRound;
	using mod::RoundResult;

	// 1. Typical video: [filename, video(flags=0)] -> round bit set.
	{
		Builder a;
		putFilename(a, "video.mp4");
		putVideo(a, 0, 12.5, 640, 480);
		auto buf = buildUploaded(false, false, a.bytes(), 2);
		const auto r = MakeUploadedVideoRound(buf);
		check(r == RoundResult::Modified, "sets round on plain video");
		check(readVideoFlags(buf).value_or(0) == tl::kVideoFlagRoundMessage,
			"video flags == round_message only");
	}

	// 2. Preserves other flags (supports_streaming) while adding round.
	{
		Builder a;
		putFilename(a, "clip.mp4");
		putVideo(a, tl::kVideoFlagSupportsStreaming, 3.0, 400, 400);
		auto buf = buildUploaded(false, false, a.bytes(), 2);
		const auto r = MakeUploadedVideoRound(buf);
		const auto flags = readVideoFlags(buf).value_or(0);
		check(r == RoundResult::Modified, "sets round alongside streaming");
		check((flags & tl::kVideoFlagRoundMessage)
				&& (flags & tl::kVideoFlagSupportsStreaming),
			"streaming flag preserved");
	}

	// 3. Big uploaded file + thumb present (flags.2) -> still found.
	{
		Builder a;
		putFilename(a, "big.mp4");
		putVideo(a, 0, 30.0, 720, 720);
		auto buf = buildUploaded(true, true, a.bytes(), 2);
		const auto r = MakeUploadedVideoRound(buf);
		check(r == RoundResult::Modified, "handles big file + thumb");
	}

	// 4. Idempotent: already round -> AlreadyRound, unchanged.
	{
		Builder a;
		putVideo(a, tl::kVideoFlagRoundMessage, 5.0, 300, 300);
		auto buf = buildUploaded(false, false, a.bytes(), 1);
		const auto before = buf;
		const auto r = MakeUploadedVideoRound(buf);
		check(r == RoundResult::AlreadyRound, "detects already-round");
		check(buf == before, "already-round leaves buffer untouched");
	}

	// 5. No video attribute (document, not a video) -> NoVideoAttr, unchanged.
	{
		Builder a;
		putFilename(a, "notes.pdf");
		auto buf = buildUploaded(false, false, a.bytes(), 1);
		const auto before = buf;
		const auto r = MakeUploadedVideoRound(buf);
		check(r == RoundResult::NoVideoAttr, "no video attr -> NoVideoAttr");
		check(buf == before, "non-video leaves buffer untouched");
	}

	// 6. Audio attribute before video is skipped correctly.
	{
		Builder a;
		putAudio(a, 7);
		putAnimated(a);
		putVideo(a, 0, 9.0, 480, 480);
		auto buf = buildUploaded(false, false, a.bytes(), 3);
		const auto r = MakeUploadedVideoRound(buf);
		check(r == RoundResult::Modified, "skips audio/animated before video");
	}

	// 7. Embedded in a larger messages.sendMedia-like body -> located by id.
	{
		Builder a;
		putFilename(a, "video.mp4");
		putVideo(a, 0, 1.0, 200, 200);
		auto inner = buildUploaded(false, false, a.bytes(), 2);

		Builder outer;
		outer.putU32(0x0330e77f); // messages.sendMedia id
		outer.putU32(0x00000001); // flags
		outer.putU32(0xdde8a54c); // inputPeerUser id (arbitrary header noise)
		outer.putI64(42);         // user_id
		outer.putI64(99);         // access_hash
		outer.putRaw(inner);      // media:InputMedia
		auto buf = outer.take();

		const auto r = MakeUploadedVideoRound(buf);
		check(r == RoundResult::Modified, "finds uploaded doc inside sendMedia");
		check(readVideoFlags(buf).value_or(0) & tl::kVideoFlagRoundMessage,
			"round set in embedded body");
	}

	// 8. Buffer without an uploaded document -> NoUploadedDoc.
	{
		std::vector<uint8_t> buf = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
		const auto r = MakeUploadedVideoRound(buf);
		check(r == RoundResult::NoUploadedDoc, "no uploaded doc -> NoUploadedDoc");
	}

	// 9. Fuzz: random and id-salted buffers must never crash and must return
	// a defined result. The core promises no out-of-bounds access on garbage.
	{
		std::mt19937 rng(0xC0FFEE);
		std::uniform_int_distribution<int> byte(0, 255);
		std::uniform_int_distribution<int> len(0, 512);
		bool ok = true;
		for (int iter = 0; iter < 200000; ++iter) {
			std::vector<uint8_t> buf(len(rng));
			for (auto &b : buf) b = uint8_t(byte(rng));
			// Salt some buffers with the uploaded-doc id to exercise parsing.
			if ((iter % 3) == 0 && buf.size() >= 4) {
				const size_t at = (size_t(byte(rng)) % buf.size()) & ~size_t(3);
				if (at + 4 <= buf.size()) {
					const uint32_t id = tl::kInputMediaUploadedDocument;
					std::memcpy(buf.data() + at, &id, 4);
				}
			}
			const auto r = MakeUploadedVideoRound(buf);
			(void)r; // any enum value is fine; the point is "no crash".
		}
		check(ok, "fuzz: 200k random buffers, no crash");
	}

	std::printf("\n%s\n", g_failures ? "FAILED" : "ALL PASSED");
	return g_failures ? 1 : 0;
}

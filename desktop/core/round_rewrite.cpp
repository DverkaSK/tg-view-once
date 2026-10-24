#include "round_rewrite.h"

#include "tl.h"

namespace mod {
namespace {

// Finds the start offset of an inputMediaUploadedDocument constructor id.
// The uploaded-document InputMedia carries no media bytes (the file content is
// uploaded separately via upload.saveFilePart), so the blob is small and
// structured and a scan for the id does not collide with file payload.
std::optional<size_t> findUploadedDocument(const uint8_t *data, size_t size) {
	if (size < 4) return std::nullopt;
	for (size_t i = 0; i + 4 <= size; i += 4) {
		uint32_t v;
		std::memcpy(&v, data + i, 4);
		if (v == tl::kInputMediaUploadedDocument) return i;
	}
	return std::nullopt;
}

// Skips an InputFile (inputFile / inputFileBig). Returns false on anything else.
bool skipInputFile(tl::Reader &r) {
	const uint32_t id = r.readU32();
	if (id == tl::kInputFile) {
		r.skip(8); // id:long
		r.skip(4); // parts:int
		r.skipString(); // name
		r.skipString(); // md5_checksum
		return true;
	}
	if (id == tl::kInputFileBig) {
		r.skip(8); // id:long
		r.skip(4); // parts:int
		r.skipString(); // name
		return true;
	}
	return false;
}

// Skips a single DocumentAttribute that is NOT the video one. Only the
// attributes that can realistically precede documentAttributeVideo for a sent
// video are handled; anything else makes us bail out safely.
bool skipNonVideoAttribute(tl::Reader &r, uint32_t id) {
	switch (id) {
	case tl::kDocAttrFilename:
		r.skipString(); // file_name
		return true;
	case tl::kDocAttrAnimated:
	case tl::kDocAttrHasStickers:
		return true;
	case tl::kDocAttrImageSize:
		r.skip(8); // w:int h:int
		return true;
	case tl::kDocAttrAudio: {
		const uint32_t flags = r.readU32();
		r.skip(4); // duration:int
		if (flags & (1u << 0)) r.skipString(); // title
		if (flags & (1u << 1)) r.skipString(); // performer
		if (flags & (1u << 2)) r.skipString(); // waveform:bytes
		return true;
	}
	default:
		// kDocAttrSticker and unknown types carry nested boxed types we don't
		// model; refuse rather than risk corrupting the request.
		return false;
	}
}

} // namespace

RoundResult MakeUploadedVideoRound(uint8_t *data, size_t size) {
	if (!data) return RoundResult::NoUploadedDoc;
	const auto start = findUploadedDocument(data, size);
	if (!start) return RoundResult::NoUploadedDoc;

	try {
		tl::Reader r(data, size);
		r.seek(*start);

		if (r.readU32() != tl::kInputMediaUploadedDocument) {
			return RoundResult::ParseError;
		}
		const uint32_t flags = r.readU32();
		// nosound_video (f.3), force_file (f.4), spoiler (f.5): bare true flags.
		if (!skipInputFile(r)) return RoundResult::ParseError; // file
		if (flags & (1u << 2)) {                               // thumb
			if (!skipInputFile(r)) return RoundResult::ParseError;
		}
		r.skipString(); // mime_type

		if (r.readU32() != tl::kVector) return RoundResult::ParseError;
		const uint32_t count = r.readU32();
		for (uint32_t i = 0; i < count; ++i) {
			const uint32_t attrId = r.readU32();
			if (attrId == tl::kDocAttrVideo) {
				const size_t flagsOffset = r.pos();
				uint32_t videoFlags;
				std::memcpy(&videoFlags, data + flagsOffset, 4);
				if (videoFlags & tl::kVideoFlagRoundMessage) {
					return RoundResult::AlreadyRound;
				}
				videoFlags |= tl::kVideoFlagRoundMessage;
				std::memcpy(data + flagsOffset, &videoFlags, 4);
				return RoundResult::Modified;
			}
			if (!skipNonVideoAttribute(r, attrId)) {
				return RoundResult::ParseError;
			}
		}
		return RoundResult::NoVideoAttr;
	} catch (const std::out_of_range &) {
		return RoundResult::ParseError;
	}
}

RoundResult MakeUploadedVideoRound(std::vector<uint8_t> &buffer) {
	return MakeUploadedVideoRound(buffer.data(), buffer.size());
}

} // namespace mod

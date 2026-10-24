// Core transform for the desktop mod: given the serialized body of an outgoing
// messages.sendMedia request (or just the InputMedia blob inside it), find the
// uploaded document's documentAttributeVideo and set its round_message flag.
//
// This is the whole "send video as a video message" feature at the wire level.
// It keys on TL constructor ids, which are stable across Telegram Desktop
// rebuilds, not on code addresses.
#pragma once

#include <cstdint>
#include <vector>

namespace mod {

enum class RoundResult {
	Modified,        // round_message bit was set now
	AlreadyRound,    // the attribute already had round_message
	NoUploadedDoc,   // no inputMediaUploadedDocument found in the buffer
	NoVideoAttr,     // uploaded doc had no documentAttributeVideo
	ParseError,      // structure didn't match the schema we expect
};

// Scans `buffer` for an inputMediaUploadedDocument, walks to its attribute
// vector and flips round_message on the video attribute in place.
// `buffer` may be the exact InputMedia blob or a larger request body that
// contains one. Returns what happened; the buffer is mutated only on Modified.
RoundResult MakeUploadedVideoRound(std::vector<uint8_t> &buffer);

} // namespace mod

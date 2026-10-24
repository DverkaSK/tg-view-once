// roundtool: apply the round-message transform to a raw request body on disk.
//
// Use it to validate the core against a real capture: dump the plaintext body
// of an outgoing messages.sendMedia (before MTProto encryption) to a file, run
//   roundtool in.bin out.bin
// and diff the result, or inspect the reported outcome.
#include "../core/round_rewrite.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

const char *describe(mod::RoundResult r) {
	switch (r) {
	case mod::RoundResult::Modified: return "Modified (round_message set)";
	case mod::RoundResult::AlreadyRound: return "AlreadyRound (unchanged)";
	case mod::RoundResult::NoUploadedDoc: return "NoUploadedDoc";
	case mod::RoundResult::NoVideoAttr: return "NoVideoAttr";
	case mod::RoundResult::ParseError: return "ParseError";
	}
	return "?";
}

bool readFile(const std::string &path, std::vector<uint8_t> &out) {
	std::ifstream f(path, std::ios::binary);
	if (!f) return false;
	out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	return true;
}

bool writeFile(const std::string &path, const std::vector<uint8_t> &data) {
	std::ofstream f(path, std::ios::binary);
	if (!f) return false;
	f.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
	return bool(f);
}

} // namespace

int main(int argc, char **argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: %s <in.bin> [out.bin]\n", argv[0]);
		return 2;
	}
	std::vector<uint8_t> buf;
	if (!readFile(argv[1], buf)) {
		std::fprintf(stderr, "cannot read %s\n", argv[1]);
		return 2;
	}
	const auto result = mod::MakeUploadedVideoRound(buf);
	std::printf("%zu bytes -> %s\n", buf.size(), describe(result));

	if (argc >= 3) {
		if (!writeFile(argv[2], buf)) {
			std::fprintf(stderr, "cannot write %s\n", argv[2]);
			return 2;
		}
		std::printf("wrote %s\n", argv[2]);
	}
	return (result == mod::RoundResult::Modified
		|| result == mod::RoundResult::AlreadyRound) ? 0 : 1;
}

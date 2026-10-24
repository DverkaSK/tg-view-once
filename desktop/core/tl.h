// Minimal TL (Type Language) wire helpers for reading and building the small,
// self-contained blobs we need. Everything is little-endian, same as MTProto.
#pragma once

#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace tl {

// Constructor ids from Telegram Desktop's api.tl (name#id), on the wire LE.
constexpr uint32_t kVector = 0x1cb5c415;
constexpr uint32_t kInputMediaUploadedDocument = 0x037c9330;
constexpr uint32_t kInputFile = 0xf52ff27f;
constexpr uint32_t kInputFileBig = 0xfa4f0bb5;
constexpr uint32_t kDocAttrImageSize = 0x6c37c15c;
constexpr uint32_t kDocAttrAnimated = 0x11b58939;
constexpr uint32_t kDocAttrSticker = 0x6319d612;
constexpr uint32_t kDocAttrVideo = 0x43c57c48;
constexpr uint32_t kDocAttrAudio = 0x9852f9c6;
constexpr uint32_t kDocAttrFilename = 0x15590068;
constexpr uint32_t kDocAttrHasStickers = 0x9801d2f7;

// documentAttributeVideo#43c57c48 flags: round_message is bit 0.
constexpr uint32_t kVideoFlagRoundMessage = 1u << 0;
constexpr uint32_t kVideoFlagSupportsStreaming = 1u << 1;

// A bounds-checked cursor over an existing buffer. It never writes; callers that
// need to mutate use the returned offsets.
class Reader {
public:
	Reader(const uint8_t *data, size_t size) : _data(data), _size(size) {}

	size_t pos() const { return _pos; }
	void seek(size_t pos) { _pos = pos; }
	bool eof() const { return _pos >= _size; }
	size_t remaining() const { return _size - _pos; }

	uint32_t readU32() {
		need(4);
		uint32_t v;
		std::memcpy(&v, _data + _pos, 4);
		_pos += 4;
		return v;
	}

	uint32_t peekU32() const {
		if (_pos + 4 > _size) throw std::out_of_range("peekU32");
		uint32_t v;
		std::memcpy(&v, _data + _pos, 4);
		return v;
	}

	void skip(size_t n) {
		need(n);
		_pos += n;
	}

	// TL string/bytes: 1 or 4 byte length header + data, padded to 4 bytes.
	void skipString() {
		need(1);
		const uint8_t first = _data[_pos];
		size_t header, length;
		if (first < 254) {
			header = 1;
			length = first;
		} else {
			need(4);
			length = size_t(_data[_pos + 1])
				| (size_t(_data[_pos + 2]) << 8)
				| (size_t(_data[_pos + 3]) << 16);
			header = 4;
		}
		const size_t total = header + length;
		const size_t padded = (total + 3) & ~size_t(3);
		need(padded);
		_pos += padded;
	}

private:
	void need(size_t n) const {
		if (_pos + n > _size) throw std::out_of_range("tl::Reader overrun");
	}

	const uint8_t *_data;
	size_t _size;
	size_t _pos = 0;
};

// Small helper to build the test blobs (and, later, any request we re-emit).
class Builder {
public:
	const std::vector<uint8_t> &bytes() const { return _buf; }
	size_t size() const { return _buf.size(); }

	void putU32(uint32_t v) {
		const size_t at = _buf.size();
		_buf.resize(at + 4);
		std::memcpy(_buf.data() + at, &v, 4);
	}

	void putI32(int32_t v) { putU32(uint32_t(v)); }

	void putI64(int64_t v) {
		const size_t at = _buf.size();
		_buf.resize(at + 8);
		std::memcpy(_buf.data() + at, &v, 8);
	}

	void putDouble(double v) {
		uint64_t bits;
		std::memcpy(&bits, &v, 8);
		const size_t at = _buf.size();
		_buf.resize(at + 8);
		std::memcpy(_buf.data() + at, &bits, 8);
	}

	void putString(const std::string &s) {
		const size_t length = s.size();
		if (length < 254) {
			_buf.push_back(uint8_t(length));
		} else {
			_buf.push_back(254);
			_buf.push_back(uint8_t(length & 0xff));
			_buf.push_back(uint8_t((length >> 8) & 0xff));
			_buf.push_back(uint8_t((length >> 16) & 0xff));
		}
		_buf.insert(_buf.end(), s.begin(), s.end());
		while (_buf.size() % 4) _buf.push_back(0);
	}

	void putRaw(const std::vector<uint8_t> &raw) {
		_buf.insert(_buf.end(), raw.begin(), raw.end());
	}

	std::vector<uint8_t> take() { return std::move(_buf); }

private:
	std::vector<uint8_t> _buf;
};

} // namespace tl

// round_dll: injectable scaffold that arms "send the next video as a video
// message" and rewrites the outgoing request with the verified core.
//
// Status: this is a scaffold. It compiles as an x64 DLL and does the parts that
// are the same on every machine — locate the send seam by byte signature, log
// it, and toggle the armed state by hotkey. The one machine-specific step,
// installing the detour at the located address, is marked INTEGRATION POINT
// below: it needs the signature of your exact Telegram.exe and a trampoline
// (MinHook is recommended). Nothing here touches the network until that detour
// is wired, so loading the DLL as-is is inert besides logging.
#include "../core/round_rewrite.h"

#include <windows.h>
#include <psapi.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

// --- logging ------------------------------------------------------------

std::wstring moduleDir() {
	wchar_t path[MAX_PATH] = {};
	HMODULE self = nullptr;
	GetModuleHandleExW(
		GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
			| GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(&moduleDir),
		&self);
	GetModuleFileNameW(self, path, MAX_PATH);
	std::wstring full(path);
	const auto slash = full.find_last_of(L"\\/");
	return (slash == std::wstring::npos) ? L"." : full.substr(0, slash);
}

void logLine(const std::string &line) {
	static const std::wstring dir = moduleDir();
	std::ofstream f((dir + L"\\tgvo.log").c_str(), std::ios::app);
	if (f) f << line << "\n";
}

// --- byte-signature scan over the main module ---------------------------
// Pattern is IDA-style text, e.g. "48 8B 05 ?? ?? ?? ?? 48 85 C0", where ??
// matches any byte. Keeping the seam as a config string means a new Telegram
// build is handled by editing tgvo_pattern.txt, not rebuilding the DLL.

struct PatternByte {
	uint8_t value = 0;
	bool wildcard = false;
};

std::vector<PatternByte> parsePattern(const std::string &text) {
	std::vector<PatternByte> out;
	std::istringstream in(text);
	std::string tok;
	while (in >> tok) {
		if (tok == "??" || tok == "?") {
			out.push_back({0, true});
		} else {
			out.push_back({uint8_t(std::stoul(tok, nullptr, 16)), false});
		}
	}
	return out;
}

bool mainModuleRange(uint8_t *&base, size_t &size) {
	HMODULE mod = GetModuleHandleW(nullptr);
	if (!mod) return false;
	MODULEINFO info = {};
	if (!GetModuleInformation(GetCurrentProcess(), mod, &info, sizeof(info))) {
		return false;
	}
	base = static_cast<uint8_t *>(info.lpBaseOfDll);
	size = info.SizeOfImage;
	return true;
}

std::optional<uint8_t *> scan(const std::vector<PatternByte> &pat) {
	uint8_t *base = nullptr;
	size_t size = 0;
	if (!mainModuleRange(base, size) || pat.empty() || pat.size() > size) {
		return std::nullopt;
	}
	for (size_t i = 0; i + pat.size() <= size; ++i) {
		bool hit = true;
		for (size_t j = 0; j < pat.size(); ++j) {
			if (!pat[j].wildcard && base[i + j] != pat[j].value) {
				hit = false;
				break;
			}
		}
		if (hit) return base + i;
	}
	return std::nullopt;
}

std::string readPatternFile() {
	const std::wstring path = moduleDir() + L"\\tgvo_pattern.txt";
	std::ifstream f(path.c_str());
	if (!f) return {};
	std::stringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

// --- armed state --------------------------------------------------------

volatile bool g_armed = false;

// Called by the installed detour for every outgoing request body (plaintext TL,
// before MTProto encryption). Wired up at the INTEGRATION POINT below.
void OnOutgoingRequest(uint8_t *data, size_t size) {
	if (!g_armed || !data || !size) return;
	std::vector<uint8_t> buf(data, data + size);
	const auto result = mod::MakeUploadedVideoRound(buf);
	if (result == mod::RoundResult::Modified) {
		std::memcpy(data, buf.data(), size); // length is unchanged for round
		g_armed = false;
		logLine("armed send rewritten to round_message");
	}
}

DWORD WINAPI worker(LPVOID) {
	logLine("round_dll loaded");

	const std::string patternText = readPatternFile();
	if (!patternText.empty()) {
		const auto pat = parsePattern(patternText);
		if (const auto at = scan(pat)) {
			char buf[64];
			std::snprintf(buf, sizeof(buf), "seam found at %p", (void *)*at);
			logLine(buf);
			// --- INTEGRATION POINT ---------------------------------------
			// Install a detour at *at (e.g. MinHook: MH_CreateHook(*at, ...))
			// whose callback hands the request body to OnOutgoingRequest()
			// before calling the original. This is the only machine-specific
			// step and needs the signature for your exact Telegram.exe.
			(void)&OnOutgoingRequest;
		} else {
			logLine("seam pattern not found - update tgvo_pattern.txt");
		}
	} else {
		logLine("no tgvo_pattern.txt next to the dll - nothing to hook yet");
	}

	// Hotkey: Ctrl+Alt+R arms the next send. Polling keeps the scaffold free
	// of a message loop; a real build would use RegisterHotKey.
	bool wasDown = false;
	for (;;) {
		const bool down = (GetAsyncKeyState(VK_CONTROL) & 0x8000)
			&& (GetAsyncKeyState(VK_MENU) & 0x8000)
			&& (GetAsyncKeyState('R') & 0x8000);
		if (down && !wasDown) {
			g_armed = !g_armed;
			logLine(g_armed ? "armed: next video -> round" : "disarmed");
		}
		wasDown = down;
		Sleep(30);
	}
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
	if (reason == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(module);
		CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
	}
	return TRUE;
}

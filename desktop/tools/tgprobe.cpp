// tgprobe: a tiny, careful debugger to locate the send path in a running
// Telegram.exe. It attaches, optionally sets a hardware execute breakpoint at
// <module base + rva>, and when it fires (e.g. while you send a video to Saved
// Messages) it logs the registers, stack return addresses and a peek at the
// pointed-to memory, then detaches cleanly.
//
//   tgprobe <pid> [rva_hex] [seconds]
//
// rva omitted or 0 => safe mode: just attach, run, detach (no breakpoint).
// DebugSetProcessKillOnExit(FALSE) is set so Telegram survives even if this
// tool exits unexpectedly.
//
// Consent/risk: debugging the official client is against Telegram's terms and
// may be flagged. Use on your own client with your own consent.
#include <windows.h>
#include <tlhelp32.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>

namespace {

FILE *g_log = nullptr;
void logf(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	if (g_log) {
		va_start(ap, fmt);
		vfprintf(g_log, fmt, ap);
		va_end(ap);
		fflush(g_log);
	}
}

uint64_t g_base = 0;
std::vector<uint64_t> g_targets; // up to 4 absolute addresses (Dr0..Dr3)
int g_hits = 0;

bool isTarget(uint64_t addr) {
	for (auto t : g_targets) if (t == addr) return true;
	return false;
}

void setHwBreakpoint(HANDLE thread, const std::vector<uint64_t> &addrs) {
	SuspendThread(thread);
	CONTEXT ctx = {};
	ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
	if (GetThreadContext(thread, &ctx)) {
		uint64_t dr7 = ctx.Dr7;
		for (size_t i = 0; i < addrs.size() && i < 4; ++i) {
			(&ctx.Dr0)[i] = addrs[i];
			dr7 |= (1ULL << (i * 2));             // local enable for Dri
			dr7 &= ~(0xFULL << (16 + i * 4));      // exec, length 1
		}
		ctx.Dr7 = dr7;
		ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
		if (!SetThreadContext(thread, &ctx)) {
			logf("    SetThreadContext failed: %lu\n", GetLastError());
		}
	}
	ResumeThread(thread);
}

void setBpOnAllThreads(DWORD pid, const std::vector<uint64_t> &addrs) {
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snap == INVALID_HANDLE_VALUE) return;
	THREADENTRY32 te = {};
	te.dwSize = sizeof(te);
	if (Thread32First(snap, &te)) {
		do {
			if (te.th32OwnerProcessID == pid) {
				HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT
					| THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
				if (t) {
					setHwBreakpoint(t, addrs);
					CloseHandle(t);
				}
			}
		} while (Thread32Next(snap, &te));
	}
	CloseHandle(snap);
}

void peek(HANDLE proc, const char *label, uint64_t addr) {
	if (!addr) return;
	uint8_t buf[32] = {};
	SIZE_T got = 0;
	if (ReadProcessMemory(proc, (void *)addr, buf, sizeof(buf), &got) && got) {
		char hex[3 * sizeof(buf) + 1] = {};
		for (SIZE_T i = 0; i < got; ++i) std::snprintf(hex + i * 3, 4, "%02x ", buf[i]);
		logf("    %-4s %016llx: %s\n", label, (unsigned long long)addr, hex);
	}
}

} // namespace

int main(int argc, char **argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: %s <pid> [rva_hex] [seconds]\n", argv[0]);
		return 2;
	}
	const DWORD pid = DWORD(std::strtoul(argv[1], nullptr, 10));
	// argv[2]: up to 4 comma-separated rva (or absolute with TGPROBE_ABS) in hex.
	std::vector<uint64_t> rvas;
	if (argc >= 3) {
		const char *s = argv[2];
		while (*s && rvas.size() < 4) {
			rvas.push_back(std::strtoull(s, nullptr, 16));
			const char *comma = strchr(s, ',');
			if (!comma) break;
			s = comma + 1;
		}
	}
	const int seconds = (argc >= 4) ? std::atoi(argv[3]) : 60;

	g_log = fopen("tgprobe.log", "a");
	logf("\n=== tgprobe pid=%lu bps=%zu seconds=%d ===\n",
		pid, rvas.size(), seconds);

	if (!DebugActiveProcess(pid)) {
		logf("DebugActiveProcess failed: %lu\n", GetLastError());
		return 1;
	}
	DebugSetProcessKillOnExit(FALSE); // Telegram survives if we die/detach

	HANDLE proc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION,
		FALSE, pid);

	const DWORD deadline = GetTickCount() + DWORD(seconds) * 1000;
	bool running = true;
	while (running) {
		DEBUG_EVENT ev = {};
		if (!WaitForDebugEvent(&ev, 200)) {
			if (GetTickCount() > deadline) break;
			continue;
		}
		DWORD cont = DBG_CONTINUE;

		switch (ev.dwDebugEventCode) {
		case CREATE_PROCESS_DEBUG_EVENT: {
			g_base = (uint64_t)ev.u.CreateProcessInfo.lpBaseOfImage;
			logf("module base = %016llx\n", (unsigned long long)g_base);
			if (!rvas.empty()) {
				const bool abs = getenv("TGPROBE_ABS") != nullptr;
				for (auto r : rvas) g_targets.push_back(abs ? r : g_base + r);
				setBpOnAllThreads(pid, g_targets);
				for (auto t : g_targets)
					logf("hw breakpoint armed at %016llx (rva %llx)\n",
						(unsigned long long)t, (unsigned long long)(t - g_base));
			}
			if (ev.u.CreateProcessInfo.hFile) CloseHandle(ev.u.CreateProcessInfo.hFile);
			break;
		}
		case LOAD_DLL_DEBUG_EVENT:
			if (ev.u.LoadDll.hFile) CloseHandle(ev.u.LoadDll.hFile);
			break;
		case CREATE_THREAD_DEBUG_EVENT:
			if (!g_targets.empty()) {
				HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT
					| THREAD_SUSPEND_RESUME, FALSE, ev.dwThreadId);
				if (t) { setHwBreakpoint(t, g_targets); CloseHandle(t); }
			}
			break;
		case EXCEPTION_DEBUG_EVENT: {
			const auto &er = ev.u.Exception.ExceptionRecord;
			const bool ours = !g_targets.empty()
				&& er.ExceptionCode == EXCEPTION_SINGLE_STEP
				&& isTarget((uint64_t)er.ExceptionAddress);
			if (ours) {
				HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT
					| THREAD_SUSPEND_RESUME, FALSE, ev.dwThreadId);
				CONTEXT ctx = {};
				ctx.ContextFlags = CONTEXT_FULL | CONTEXT_DEBUG_REGISTERS;
				if (t && GetThreadContext(t, &ctx)) {
					++g_hits;
					logf("[hit %d] rip=%llx (rva %llx) tid=%lu\n", g_hits,
						(unsigned long long)ctx.Rip,
						(unsigned long long)(ctx.Rip - g_base), ev.dwThreadId);
					logf("    rcx=%llx rdx=%llx r8=%llx r9=%llx rax=%llx rsp=%llx\n",
						(unsigned long long)ctx.Rcx, (unsigned long long)ctx.Rdx,
						(unsigned long long)ctx.R8, (unsigned long long)ctx.R9,
						(unsigned long long)ctx.Rax, (unsigned long long)ctx.Rsp);
					peek(proc, "rcx", ctx.Rcx);
					peek(proc, "rdx", ctx.Rdx);
					peek(proc, "r8", ctx.R8);
					// When probing a serialization site, dump the buffer the id
					// is being written into. TGPROBE_DUMP="rbp+1f84:160".
					if (const char *spec = getenv("TGPROBE_DUMP")) {
						char reg[8] = {}; unsigned long long off = 0, len = 64;
						if (sscanf(spec, "%7[a-z]+%llx:%llu", reg, &off, &len) >= 2) {
							uint64_t basev = 0;
							if (!strcmp(reg, "rbp")) basev = ctx.Rbp;
							else if (!strcmp(reg, "rsp")) basev = ctx.Rsp;
							else if (!strcmp(reg, "rdx")) basev = ctx.Rdx;
							else if (!strcmp(reg, "rcx")) basev = ctx.Rcx;
							else if (!strcmp(reg, "rax")) basev = ctx.Rax;
							std::vector<uint8_t> dump(len);
							SIZE_T got = 0;
							if (ReadProcessMemory(proc, (void *)(basev + off),
									dump.data(), len, &got) && got) {
								logf("    dump %s+%llx (%016llx), %llu bytes:\n      ",
									reg, off, (unsigned long long)(basev + off),
									(unsigned long long)got);
								for (SIZE_T i = 0; i < got; ++i) {
									logf("%02x ", dump[i]);
									if ((i & 31) == 31) logf("\n      ");
								}
								logf("\n");
							}
						}
					}
					// stack return addresses (callers), as module-relative RVAs
					uint64_t stack[8] = {};
					SIZE_T got = 0;
					if (ReadProcessMemory(proc, (void *)ctx.Rsp, stack,
							sizeof(stack), &got)) {
						for (size_t i = 0; i < got / 8; ++i) {
							if (stack[i] > g_base && stack[i] < g_base + 0x8000000ULL) {
								logf("    ret[%zu] rva=%llx\n", i,
									(unsigned long long)(stack[i] - g_base));
							}
						}
					}
					ctx.EFlags |= 0x10000; // Resume Flag: don't retrigger instantly
					ctx.ContextFlags = CONTEXT_FULL | CONTEXT_DEBUG_REGISTERS;
					SetThreadContext(t, &ctx);
				}
				if (t) CloseHandle(t);
			} else {
				cont = DBG_EXCEPTION_NOT_HANDLED; // not ours, let Telegram handle
			}
			break;
		}
		case EXIT_PROCESS_DEBUG_EVENT:
			running = false;
			break;
		}

		ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, cont);
		if (GetTickCount() > deadline) break;
	}

	DebugActiveProcessStop(pid);
	logf("detached, hits=%d\n", g_hits);
	if (proc) CloseHandle(proc);
	if (g_log) fclose(g_log);
	return 0;
}

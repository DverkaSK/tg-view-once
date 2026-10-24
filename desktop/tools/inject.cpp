// Minimal DLL injector: loads a DLL into a target process by pid or image name
// via CreateRemoteThread(LoadLibraryA). x64; build the DLL and target as x64.
//
//   inject <pid | name.exe> <path-to-dll>
//
// Note: injecting into Telegram modifies the official client in memory, which
// is against Telegram's terms and may be flagged by antivirus. Use on your own
// client at your own risk.
#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

namespace {

DWORD findPidByName(const char *name) {
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE) return 0;
	PROCESSENTRY32 e = {};
	e.dwSize = sizeof(e);
	DWORD pid = 0;
	if (Process32First(snap, &e)) {
		do {
			if (_stricmp(e.szExeFile, name) == 0) {
				pid = e.th32ProcessID;
				break;
			}
		} while (Process32Next(snap, &e));
	}
	CloseHandle(snap);
	return pid;
}

bool fileExists(const char *path) {
	const DWORD a = GetFileAttributesA(path);
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

} // namespace

int main(int argc, char **argv) {
	if (argc != 3) {
		std::fprintf(stderr, "usage: %s <pid | name.exe> <path-to-dll>\n", argv[0]);
		return 2;
	}

	char dllPath[MAX_PATH];
	if (!GetFullPathNameA(argv[2], MAX_PATH, dllPath, nullptr)
			|| !fileExists(dllPath)) {
		std::fprintf(stderr, "dll not found: %s\n", argv[2]);
		return 2;
	}

	DWORD pid = DWORD(std::strtoul(argv[1], nullptr, 10));
	if (pid == 0) pid = findPidByName(argv[1]);
	if (pid == 0) {
		std::fprintf(stderr, "target process not found: %s\n", argv[1]);
		return 2;
	}

	HANDLE proc = OpenProcess(
		PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION
			| PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
		FALSE, pid);
	if (!proc) {
		std::fprintf(stderr, "OpenProcess failed: %lu\n", GetLastError());
		return 1;
	}

	const size_t bytes = std::strlen(dllPath) + 1;
	void *remote = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE,
		PAGE_READWRITE);
	if (!remote) {
		std::fprintf(stderr, "VirtualAllocEx failed: %lu\n", GetLastError());
		CloseHandle(proc);
		return 1;
	}

	if (!WriteProcessMemory(proc, remote, dllPath, bytes, nullptr)) {
		std::fprintf(stderr, "WriteProcessMemory failed: %lu\n", GetLastError());
		VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
		CloseHandle(proc);
		return 1;
	}

	const auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
		GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA"));
	HANDLE thread = CreateRemoteThread(proc, nullptr, 0, loadLibrary, remote,
		0, nullptr);
	if (!thread) {
		std::fprintf(stderr, "CreateRemoteThread failed: %lu\n", GetLastError());
		VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
		CloseHandle(proc);
		return 1;
	}

	WaitForSingleObject(thread, INFINITE);
	DWORD loadResult = 0;
	GetExitCodeThread(thread, &loadResult);
	std::printf("injected into pid %lu, LoadLibraryA returned 0x%lx\n",
		pid, loadResult);

	CloseHandle(thread);
	VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
	CloseHandle(proc);
	return loadResult ? 0 : 1;
}

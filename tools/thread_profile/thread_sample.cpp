// thread_sample (Windows): where the busiest threads of a running process spend their time, from
// outside the process. Linux: tools/profile_guest.py (GDB).
// Usage: thread_sample <process.exe> [seconds=5] [threads=6]
// Measures CPU per thread over 1 s, then for the busiest threads samples the instruction pointer
// and up to 5 callers (SuspendThread + GetThreadContext + StackWalk64, every ~2 ms; each thread
// stops for microseconds) for `seconds`, and prints the most frequent call chains per thread
// (dbghelp, with the PDBs next to the modules), plus each thread's start address.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// windows.h first: the other system headers use its types.
#include <dbghelp.h>
#include <tlhelp32.h>
#include <winternl.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

static ULONGLONG Ticks(const FILETIME& t) {
  return (ULONGLONG(t.dwHighDateTime) << 32) | t.dwLowDateTime;
}

static DWORD FindProcess(const wchar_t* exe) {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  PROCESSENTRY32W e = {};
  e.dwSize = sizeof(e);
  DWORD pid = 0;
  for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e))
    if (_wcsicmp(e.szExeFile, exe) == 0) pid = e.th32ProcessID;
  CloseHandle(snap);
  return pid;
}

static std::vector<DWORD> Threads(DWORD pid) {
  std::vector<DWORD> ids;
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  THREADENTRY32 e = {};
  e.dwSize = sizeof(e);
  for (BOOL ok = Thread32First(snap, &e); ok; ok = Thread32Next(snap, &e))
    if (e.th32OwnerProcessID == pid) ids.push_back(e.th32ThreadID);
  CloseHandle(snap);
  return ids;
}

static std::string Symbol(HANDLE process, DWORD64 address) {
  char buffer[sizeof(SYMBOL_INFO) + 512] = {};
  auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
  symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
  symbol->MaxNameLen = 511;
  DWORD64 displacement = 0;
  IMAGEHLP_MODULE64 module = {};
  module.SizeOfStruct = sizeof(module);
  std::string mod = SymGetModuleInfo64(process, address, &module) ? module.ModuleName : "?";
  if (SymFromAddr(process, address, &displacement, symbol)) return mod + "!" + symbol->Name;
  char hex[32];
  std::snprintf(hex, sizeof(hex), "+0x%llx", address - module.BaseOfImage);
  return mod + hex;
}

int wmain(int argc, wchar_t** argv) {
  const double seconds = argc > 2 ? _wtof(argv[2]) : 5.0;
  const size_t top_threads = argc > 3 ? size_t(_wtoi(argv[3])) : 6;
  const DWORD pid = FindProcess(argv[1]);
  if (!pid) return 1;
  HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
  SymInitialize(process, nullptr, TRUE);

  // CPU per thread over 1 s.
  std::map<DWORD, ULONGLONG> before;
  for (DWORD id : Threads(pid)) {
    HANDLE t = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, id);
    FILETIME c, x, k, u;
    if (t && GetThreadTimes(t, &c, &x, &k, &u)) before[id] = Ticks(k) + Ticks(u);
    if (t) CloseHandle(t);
  }
  Sleep(1000);
  struct Busy {
    DWORD id;
    double cpu;
  };
  std::vector<Busy> busy;
  for (auto& [id, t0] : before) {
    HANDLE t = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, id);
    FILETIME c, x, k, u;
    if (t && GetThreadTimes(t, &c, &x, &k, &u))
      busy.push_back({id, (Ticks(k) + Ticks(u) - t0) * 1e-5});
    if (t) CloseHandle(t);
  }
  std::sort(busy.begin(), busy.end(), [](const Busy& a, const Busy& b) { return a.cpu > b.cpu; });
  if (busy.size() > top_threads) busy.resize(top_threads);

  using NtQit = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
  auto query = reinterpret_cast<NtQit>(
      GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread"));
  struct Target {
    DWORD id = 0;
    double cpu = 0;
    HANDLE handle = nullptr;
    std::wstring name;
    DWORD64 start = 0;
    std::map<std::string, int> hits;
    int samples = 0;
  };
  std::vector<Target> targets;
  for (const Busy& b : busy) {
    HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                          FALSE, b.id);
    if (!t) continue;
    Target target;
    target.id = b.id;
    target.cpu = b.cpu;
    target.handle = t;
    PWSTR desc = nullptr;
    if (SUCCEEDED(GetThreadDescription(t, &desc)) && desc) {
      target.name = desc;
      LocalFree(desc);
    }
    PVOID start = nullptr;
    if (query) query(t, 9 /* ThreadQuerySetWin32StartAddress */, &start, sizeof(start), nullptr);
    target.start = DWORD64(start);
    targets.push_back(std::move(target));
  }

  // Instruction pointer samples.
  LARGE_INTEGER f, t0, now;
  QueryPerformanceFrequency(&f);
  QueryPerformanceCounter(&t0);
  do {
    for (Target& t : targets) {
      if (SuspendThread(t.handle) == DWORD(-1)) continue;
      CONTEXT context{};
      context.ContextFlags = CONTEXT_CONTROL;
      context.ContextFlags = CONTEXT_FULL;
      if (GetThreadContext(t.handle, &context)) {
        // The instruction pointer and up to 5 callers.
        std::string chain = Symbol(process, context.Rip);
        STACKFRAME64 frame{};
        frame.AddrPC.Offset = context.Rip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = context.Rbp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = context.Rsp;
        frame.AddrStack.Mode = AddrModeFlat;
        CONTEXT walk = context;
        if (StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, t.handle, &frame, &walk, nullptr,
                        SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
          for (int depth = 0;
               depth < 5 &&
               StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, t.handle, &frame, &walk, nullptr,
                           SymFunctionTableAccess64, SymGetModuleBase64, nullptr) &&
               frame.AddrPC.Offset;
               ++depth)
            chain += " <- " + Symbol(process, frame.AddrPC.Offset);
        }
        ++t.hits[chain];
        ++t.samples;
      }
      ResumeThread(t.handle);
    }
    Sleep(2);
    QueryPerformanceCounter(&now);
  } while (double(now.QuadPart - t0.QuadPart) / f.QuadPart < seconds);

  for (Target& t : targets) {
    std::printf("\n== thread %lu, %.1f%% of a core, %ls, starts at %s (%d samples)\n", t.id, t.cpu,
                t.name.empty() ? L"(unnamed)" : t.name.c_str(), Symbol(process, t.start).c_str(),
                t.samples);
    std::vector<std::pair<int, std::string>> rows;
    for (auto& [name, n] : t.hits) rows.push_back({n, name});
    std::sort(rows.rbegin(), rows.rend());
    for (size_t i = 0; i < rows.size() && i < 8; ++i)
      std::printf("  %5.1f%%  %s\n", 100.0 * rows[i].first / std::max(1, t.samples),
                  rows[i].second.c_str());
    CloseHandle(t.handle);
  }
  SymCleanup(process);
  return 0;
}

// thread_cpu (Windows): CPU use per thread of a running process, by thread name, from outside the
// process (nothing in the game changes). Linux: tools/profile_utilization.py.
// Usage: thread_cpu <process.exe> [seconds=10]
// Takes GetThreadTimes of every thread twice, `seconds` apart, and prints each thread's user and
// kernel CPU over the interval as a percentage of one core, with its description
// (SetThreadDescription; the SDK names its threads), sorted by total.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// windows.h first: the other system headers use its types.
#include <tlhelp32.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

struct Sample {
  ULONGLONG user = 0, kernel = 0;
  std::wstring name;
};

static ULONGLONG Ticks(const FILETIME& t) {
  return (ULONGLONG(t.dwHighDateTime) << 32) | t.dwLowDateTime;
}

static DWORD FindProcess(const wchar_t* exe) {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  PROCESSENTRY32W e = {};
  e.dwSize = sizeof(e);
  DWORD pid = 0;
  for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e)) {
    if (_wcsicmp(e.szExeFile, exe) == 0) pid = e.th32ProcessID;
  }
  CloseHandle(snap);
  return pid;
}

static std::map<DWORD, Sample> Snapshot(DWORD pid) {
  std::map<DWORD, Sample> out;
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  THREADENTRY32 e = {};
  e.dwSize = sizeof(e);
  for (BOOL ok = Thread32First(snap, &e); ok; ok = Thread32Next(snap, &e)) {
    if (e.th32OwnerProcessID != pid) continue;
    HANDLE t = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, e.th32ThreadID);
    if (!t) continue;
    FILETIME c, x, k, u;
    Sample s;
    if (GetThreadTimes(t, &c, &x, &k, &u)) {
      s.user = Ticks(u);
      s.kernel = Ticks(k);
    }
    PWSTR desc = nullptr;
    if (SUCCEEDED(GetThreadDescription(t, &desc)) && desc) {
      s.name = desc;
      LocalFree(desc);
    }
    CloseHandle(t);
    out[e.th32ThreadID] = s;
  }
  CloseHandle(snap);
  return out;
}

int wmain(int argc, wchar_t** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: thread_cpu <process.exe> [seconds]\n");
    return 2;
  }
  const double seconds = argc > 2 ? _wtof(argv[2]) : 10.0;
  const DWORD pid = FindProcess(argv[1]);
  if (!pid) {
    std::fprintf(stderr, "process not found\n");
    return 1;
  }
  auto before = Snapshot(pid);
  LARGE_INTEGER f, t0, t1;
  QueryPerformanceFrequency(&f);
  QueryPerformanceCounter(&t0);
  Sleep(DWORD(seconds * 1000));
  QueryPerformanceCounter(&t1);
  auto after = Snapshot(pid);
  const double wall = double(t1.QuadPart - t0.QuadPart) / double(f.QuadPart);
  struct Row {
    DWORD id;
    std::wstring name;
    double user, kernel;
  };
  std::vector<Row> rows;
  double total = 0;
  for (auto& [id, a] : after) {
    auto b = before.find(id);
    if (b == before.end()) continue;
    Row r{id, a.name, (a.user - b->second.user) * 1e-7 / wall * 100,
          (a.kernel - b->second.kernel) * 1e-7 / wall * 100};
    total += r.user + r.kernel;
    rows.push_back(r);
  }
  std::sort(rows.begin(), rows.end(),
            [](const Row& x, const Row& y) { return x.user + x.kernel > y.user + y.kernel; });
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  std::printf(
      "pid %lu, %.1f s, %zu threads, total %.1f%% of one core (%.1f%% of %lu logical CPUs)\n", pid,
      wall, rows.size(), total, total / si.dwNumberOfProcessors, si.dwNumberOfProcessors);
  for (const Row& r : rows) {
    if (r.user + r.kernel < 0.5) continue;
    std::printf("%6lu  %6.1f%%  (user %5.1f, kernel %5.1f)  %ls\n", r.id, r.user + r.kernel, r.user,
                r.kernel, r.name.empty() ? L"(unnamed)" : r.name.c_str());
  }
  return 0;
}

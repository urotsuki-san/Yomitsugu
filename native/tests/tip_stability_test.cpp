#include "tip.h"
#include "fake_thread_manager.h"
#include "fake_context.h"
#include <psapi.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

HINSTANCE g_hInstance = nullptr;
LONG g_moduleRefCount = 0;
void ModuleAddRef() { InterlockedIncrement(&g_moduleRefCount); }
void ModuleRelease() { InterlockedDecrement(&g_moduleRefCount); }
int TestDictionaryEditor();

namespace {
int failures = 0;
void Check(bool value, const char* name) {
  std::printf("%s %s\n", value ? "PASS" : "FAIL", name);
  if (!value) ++failures;
}
struct Resources { DWORD gdi, user, handles; SIZE_T memory; };
Resources Measure() {
  PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
  GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
  DWORD handles = 0; GetProcessHandleCount(GetCurrentProcess(), &handles);
  return {GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS),
          GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS), handles, memory.PrivateUsage};
}
void Pump() {
  MSG msg{};
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
}
HWND Owner() { return CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 100, 100, nullptr, nullptr, g_hInstance, nullptr); }
int PipeHost(int argc, wchar_t** argv) {
  auto input = reinterpret_cast<HANDLE>(_wcstoui64(argv[2], nullptr, 10));
  auto output = reinterpret_cast<HANDLE>(_wcstoui64(argv[3], nullptr, 10));
  const std::wstring mode = argc > 5 ? argv[5] : L"";
  for (;;) {
    uint32_t size = 0; DWORD got = 0;
    if (!ReadFile(input, &size, sizeof(size), &got, nullptr) || got != sizeof(size) || size > 16384) return 0;
    std::string data(size, '\0');
    if (!ReadFile(input, data.data(), size, &got, nullptr) || got != size) return 0;
    std::string response = "[{\"text\":\"変換\",\"reading\":\"へんかん\",\"raw\":false}]";
    if (mode == L"empty") response = "[{\"text\":\"\",\"reading\":\"a\"}]";
    if (mode == L"nul") response = "[{\"text\":\"a\\u0000b\",\"reading\":\"a\"}]";
    if (mode == L"reading") response = "[{\"text\":\"a\",\"reading\":\"" + std::string(65537, 'a') + "\"}]";
    size = mode == L"bad" ? 2 * 1024 * 1024 : static_cast<uint32_t>(response.size());
    if (!WriteFile(output, &size, sizeof(size), &got, nullptr)) return 0;
    if (mode == L"bad") { Sleep(2000); return 0; }
    if (!WriteFile(output, response.data(), size, &got, nullptr)) return 0;
  }
}
}

struct TipStabilityTest {
  static void Run() {
    {
      TextService service;
      wchar_t exe[32768]{}; GetModuleFileNameW(nullptr, exe, 32768);
      Check(service.engine_channel_.Start(exe), "start engine before nonblocking Enter test");
      service.session_.Type("nihongo");
      const auto started = GetTickCount64();
      const bool queued = service.BeginPendingCommit();
      Check(queued && service.pending_commit_ && GetTickCount64() - started < 100 && service.session_.composing(),
            "Enter schedules unresolved conversion without waiting on UI thread");
    }
    std::vector<ime::Candidate> candidates(3);
    candidates[0].output_text = "日本語"; candidates[1].output_text = "にほんご";
    candidates[2].output_text = "nihongo"; candidates[2].is_raw = true;
    CandidateWindow window;
    HWND owner = Owner();
    Check(window.Create(g_hInstance, owner), "create candidate and polling windows");
    window.Show(candidates, 0, {20, 20}); Pump();
    Check(SendMessageW(window.list_, LB_GETCOUNT, 0, 0) == 3, "candidate list populated");
    HDC dc = GetDC(window.hwnd());
    Check(!PtVisible(dc, 4, 4), "parent paint excludes candidate list"); ReleaseDC(window.hwnd(), dc);
    Check(!GetUpdateRect(window.list_, nullptr, FALSE), "candidate paint completed before Show returns");
    HFONT font = window.font_;
    for (int i = 0; i < 100; ++i) window.Show(candidates, i % 3, {20, 20});
    Check(window.font_ == font, "font reused at unchanged DPI");
    HWND poll = window.poll_window_;
    DestroyWindow(owner);
    Check(!window.hwnd() && !window.list_ && !window.font_, "owner destruction clears popup resources");
    Check(IsWindow(poll), "polling window survives owner destruction");
    bool timer = false; const auto until = GetTickCount64() + 200;
    while (GetTickCount64() < until && !timer) {
      MSG msg{};
      timer = PeekMessageW(&msg, poll, WM_TIMER, WM_TIMER, PM_REMOVE) != FALSE;
      if (!timer) Sleep(1);
    }
    Check(timer, "engine polling timer survives owner destruction");
    owner = Owner(); window.SetOwner(owner); window.Show(candidates, 1, {20, 20});
    Check(IsWindow(window.hwnd()) && GetWindow(window.hwnd(), GW_OWNER) == owner &&
          SendMessageW(window.list_, LB_GETCURSEL, 0, 0) == 1, "popup recovers for next input window");
    window.Hide(); Check(!IsWindowVisible(window.hwnd()), "candidate hides");
    window.Destroy(); DestroyWindow(owner); Pump();

    auto batch = [&](int count) {
      for (int i = 0; i < count; ++i) {
        auto parent = Owner(); CandidateWindow c;
        if (!c.Create(g_hInstance, parent)) { ++failures; DestroyWindow(parent); return; }
        for (int j = 0; j < 10; ++j) c.Show(candidates, j % 3, {20, 20});
        DestroyWindow(parent);
        c.Show(candidates, 0, {20, 20}); c.Hide(); c.Destroy(); Pump();
      }
    };
    batch(30); auto before = Measure(); batch(500); auto after = Measure();
    std::printf("popup stress: 500 owner closes / 5500 updates; GDI %lu->%lu USER %lu->%lu handles %lu->%lu private %zu->%zu\n",
                before.gdi, after.gdi, before.user, after.user, before.handles, after.handles, before.memory, after.memory);
    Check(after.gdi <= before.gdi && after.user <= before.user && after.handles <= before.handles,
          "popup stress leaves no additional GDI USER or kernel handles");
    Check(after.memory <= before.memory + 8 * 1024 * 1024, "popup private memory remains bounded (8 MiB tolerance)");

    FakeThreadManager manager; ITfThreadMgr* mgr = &manager; TfClientId client = 1;
    {
      TextService service;
      Check(SUCCEEDED(service.Activate(mgr, client)), "activate service");
      service.session_.Type("nihongo"); service.caret_valid_ = true;
      service.cand_window_.Show(candidates, 0, {20, 20});
      service.OnSetFocus(static_cast<ITfDocumentMgr*>(nullptr), nullptr);
      service.SyncCandidateWindow(nullptr);
      Check(!IsWindowVisible(service.cand_window_.hwnd()), "document focus loss cannot reshow candidates");
      service.OnSetFocus(FALSE); service.Deactivate();
      Check(SUCCEEDED(service.Activate(mgr, client)) && service.foreground_ && service.document_focused_,
            "reactivation resets stale background state");
      service.Deactivate();
      manager.fail_sink = true;
      Check(FAILED(service.Activate(mgr, client)) && !manager.keys && !manager.events && manager.refs == 1,
            "failed focus subscription rolls back activation references");
      manager.fail_sink = false;
      bool cycles = true;
      for (int i = 0; i < 100; ++i) {
        if (FAILED(service.ActivateEx(mgr, client, TF_TMAE_SECUREMODE))) { cycles = false; break; }
        service.OnSetFocus(FALSE); service.Deactivate();
        if (FAILED(service.Activate(mgr, client)) || service.restricted_ || !service.foreground_) { cycles = false; break; }
        service.Deactivate();
      }
      Check(cycles && manager.refs == 1 && !manager.keys && !manager.events,
            "100 secure to normal activation cycles release all sink references");
      FakeContext old_context;
      FakeComposition old_composition(&service);
      service.context_ = &old_context; old_context.AddRef();
      service.composition_ = &old_composition; old_composition.AddRef(); service.has_composition_ = true;
      service.OnPopContext(&old_context);
      Check(old_context.queued && !service.composition_ && !service.context_, "popped context schedules composition cleanup outside synchronous focus callback");
      service.session_.Type("atarashii");
      Check(SUCCEEDED(old_context.Flush()) && old_composition.ended && service.session_.raw_text() == "atarashii",
            "delayed termination of previous composition preserves new input");
      Check(old_context.refs == 1 && old_composition.refs == 1, "delayed composition cleanup releases retained references");
    }
  }
};

int wmain(int argc, wchar_t** argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  if (argc >= 4 && wcscmp(argv[1], L"--pipe") == 0) return PipeHost(argc, argv);
  // 切替・入力注入は行わず、表示されていない専用デスクトップ上で試験する。
  auto original = GetThreadDesktop(GetCurrentThreadId());
  auto name = L"YomitsuguStability-" + std::to_wstring(GetCurrentProcessId());
  auto desktop = CreateDesktopW(name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
  if (!desktop || !SetThreadDesktop(desktop)) { std::puts("isolated desktop unavailable"); return 2; }
  g_hInstance = GetModuleHandleW(nullptr);
  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(com)) return 3;
  TipStabilityTest::Run();
  failures += TestDictionaryEditor();
  CoUninitialize();
  SetThreadDesktop(original); CloseDesktop(desktop);

  wchar_t exe[32768]{}; GetModuleFileNameW(nullptr, exe, 32768);
  ime::EngineChannel channel;
  auto exchange = [&](int count) {
    for (int i = 0; i < count; ++i) {
      if (!channel.Start(exe)) return false;
      ime::DecodeInput input; input.raw_text = "henkan"; input.revision = i;
      channel.Submit(input); bool received = false;
      auto deadline = GetTickCount64() + 2000;
      while (channel.running() && GetTickCount64() < deadline && !received) {
        ime::DecodeInput request; std::vector<ime::Candidate> result;
        if (channel.Poll(&request, &result)) received = request.revision == i && result.size() == 1 && result[0].output_text == "変換";
        if (!received) Sleep(1);
      }
      channel.Stop(); if (!received) return false;
    }
    return true;
  };
  Check(exchange(5), "engine channel warmup"); auto before = Measure();
  Check(exchange(100), "100 engine process restart and decode cycles"); auto after = Measure();
  std::printf("channel stress: handles %lu->%lu private %zu->%zu\n", before.handles, after.handles, before.memory, after.memory);
  Check(after.handles <= before.handles, "engine restart closes pipe process and job handles");
  for (auto mode : {L"bad", L"empty", L"nul", L"reading"}) {
    Check(channel.Start(exe, mode), "start malformed reply host");
    ime::DecodeInput input; input.raw_text = "a"; channel.Submit(input);
    auto deadline = GetTickCount64() + 2000;
    while (channel.running() && GetTickCount64() < deadline) {
      ime::DecodeInput request; std::vector<ime::Candidate> result;
      channel.Poll(&request, &result); Sleep(1);
    }
    Check(!channel.running(), "oversized empty or NUL-containing candidate is rejected");
    Check(exchange(1), "decode recovers after failed engine");
  }
  Check(g_moduleRefCount == 0, "service COM objects released");
  std::printf("stability failures=%d\n", failures);
  return failures ? 1 : 0;
}

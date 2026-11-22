#include "tip.h"
#include <ime.h>
#include "input_mode_menu.h"
#include <ctffunc.h>
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
  static void TestInputMode() {
    ITfThreadMgrEx* manager = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&manager));
    TfClientId client = TF_CLIENTID_NULL;
    if (SUCCEEDED(hr))
      hr = manager->ActivateEx(&client, TF_TMAE_NOACTIVATETIP | TF_TMAE_NOACTIVATEKEYBOARDLAYOUT);
    Check(hr == S_OK, "create isolated Windows text-services thread");
    if (FAILED(hr)) { if (manager) manager->Release(); return; }

    TextService service;
    const auto profile = std::filesystem::temp_directory_path() /
        (L"YomitsuguKeyTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    if (!std::filesystem::create_directory(profile)) {
      Check(false, "create isolated shortcut profile");
      manager->Deactivate(); manager->Release(); return;
    }
    service.preferences_directory_ = profile;
    // The isolated desktop has no interactive shell to host a language bar.
    // Use Windows' actual thread compartment, with a local keystroke manager.
    FakeThreadManager key_manager;
    key_manager.compartment_provider = manager;
    hr = service.Activate(&key_manager, client);
    Check(hr == S_OK && service.keyboard_open_, "attach input service to the Windows compartment");
    if (SUCCEEDED(hr)) service.input_mode_menu_ = new InputModeMenu(service);
    if (SUCCEEDED(hr) && service.keyboard_open_ && service.input_mode_menu_) {
      auto* button = service.input_mode_menu_;
      auto shows = [&](const wchar_t* expected) {
        BSTR text = nullptr;
        HRESULT result = button->GetText(&text);
        bool matches = result == S_OK && text && wcscmp(text, expected) == 0;
        SysFreeString(text);
        return matches;
      };
      TF_LANGBARITEMINFO info{};
      Check(button->GetInfo(&info) == S_OK && (info.dwStyle & TF_LBI_STYLE_BTN_BUTTON) &&
            (info.dwStyle & TF_LBI_STYLE_BTN_MENU), "shell can dispatch both a click and the menu");
      VARIANT original{};
      service.keyboard_open_->GetValue(&original);
      VARIANT off{}; off.vt = VT_I4; off.lVal = 0;
      Check(service.keyboard_open_->SetValue(client, &off) == S_OK && shows(L"A"),
            "externally closed IME shows A");
      FakeContext context;
      BOOL eaten = FALSE;
      service.OnTestKeyDown(&context, 'K', 0, &eaten);
      Check(!eaten, "closed IME passes ordinary letters");
      Check(button->OnClick(TF_LBI_CLK_LEFT, {}, nullptr) == S_OK && shows(L"あ"),
            "one click reopens Japanese input");
      service.OnTestKeyDown(&context, 'K', 0, &eaten);
      Check(eaten, "reopened IME accepts romaji");
      Check(button->OnClick(TF_LBI_CLK_LEFT, {}, nullptr) == S_OK && shows(L"A"),
            "second click restores direct input");
      service.OnTestKeyDown(&context, VK_DBE_DBCSCHAR, 0, &eaten);
      Check(eaten && shows(L"A"), "testing a half/full-width key has no side effect");
      service.OnKeyDown(&context, VK_DBE_DBCSCHAR, 0, &eaten);
      Check(eaten && shows(L"あ"), "half/full-width key opens a closed IME");
      service.OnKeyDown(&context, VK_DBE_DBCSCHAR, 1L << 30, &eaten);
      Check(eaten && shows(L"あ"), "holding the key does not toggle repeatedly");
      service.OnKeyDown(&context, VK_DBE_SBCSCHAR, 0, &eaten);
      Check(eaten && shows(L"A"), "half/full-width key switches back to direct input");
      service.OnKeyDown(&context, VK_IME_ON, 0, &eaten);
      Check(eaten && shows(L"あ"), "dedicated IME on key restores Japanese");
      service.OnKeyDown(&context, VK_IME_OFF, 0, &eaten);
      Check(eaten && shows(L"A"), "dedicated IME off key restores direct input");
      BYTE previous[256]{};
      if (GetKeyboardState(previous)) {
        BYTE keys[256]{};
        keys[VK_SHIFT] = keys[VK_LSHIFT] = 0x80;
        SetKeyboardState(keys); // scoped to this test thread's input queue
        service.OnKeyDown(&context, VK_CAPITAL, 0, &eaten);
        Check(eaten && shows(L"あ"), "Shift plus Caps Lock opens Japanese input");
        service.OnKeyDown(&context, VK_IME_OFF, 0, &eaten);
        Check(eaten && shows(L"A"), "Shift plus dedicated alphanumeric key toggles input");
        service.OnKeyDown(&context, 'A', 0, &eaten);
        Check(!eaten && shows(L"A"), "capital A stays an ordinary character in direct input");
        service.OnKeyDown(&context, VK_DBE_ALPHANUMERIC, 0, &eaten);
        Check(eaten && shows(L"あ"), "Shift plus translated alphanumeric key reopens Japanese");
        key_manager.accept_shortcuts = true;
        service.RegisterModeShortcuts();
        Check(key_manager.shortcuts > 0, "register Windows mode shortcuts");
        service.OnKeyDown(&context, VK_CAPITAL, 0, &eaten);
        Check(eaten && shows(L"あ"), "registered chord waits for key-up without toggling twice");
        service.OnPreservedKey(&context, key_manager.last_shortcut, &eaten);
        Check(eaten && shows(L"A"), "Windows shortcut notification switches mode once");
        service.OnPreservedKey(&context, key_manager.last_shortcut, &eaten);
        Check(eaten && shows(L"あ"), "next Windows shortcut reopens Japanese input");
        SetKeyboardState(previous);
      }
      service.OnTestKeyDown(&context, VK_TAB, 0, &eaten);
      Check(!eaten, "Tab passes to the application when no text is being composed");
      service.context_ = &context; context.AddRef();
      service.session_.Type("kana");
      service.OnTestKeyDown(&context, VK_TAB, 0, &eaten);
      Check(eaten, "Tab stays in the field while composing");
      auto& edit = service.session_;
      edit.PressTab(false);
      Check(edit.is_converting() && !edit.candidates_ready(), "first Tab requests candidates");
      const auto request = edit.decode_input();
      edit.PressTab(false);
      const std::vector<ime::Candidate> choices{{u8"かな", u8"かな", false}, {u8"仮名", u8"かな", false}};
      auto reversed = edit;
      reversed.PressTab(true);
      reversed.PressTab(true);
      Check(reversed.ApplyCandidates(request, choices) && reversed.selected_index() == 1,
            "queued reverse Tab wraps from the first candidate");
      Check(edit.ApplyCandidates(request, choices), "repeated Tab before reply still accepts full candidates");
      Check(edit.visible_text() == u8"仮名", "second Tab selects the next candidate when the worker replies");
      edit.PressTab(true);
      Check(edit.visible_text() == u8"かな", "Shift Tab returns to the previous candidate");
      edit.PressTab(false);
      Check(edit.visible_text() == u8"仮名", "Tab selects the next available candidate");
      edit.PressEnter();
      Check(!edit.composing(), "Enter commits the selected Tab candidate");
      service.context_->Release(); service.context_ = nullptr;
      ime::InputPreferences disabled{};
      disabled.mode_shortcut.mode = ime::ShortcutMode::Disabled;
      disabled.tab_candidates = false;
      const UINT changed = RegisterWindowMessageW(ime::kInputPreferencesChanged);
      Check(ime::SaveInputPreferences(profile, {}, disabled), "save disabled shortcut and native Tab");
      SendMessageW(service.cand_window_.poll_hwnd(), changed, 0, 0);
      Check(service.mode_key_count_ == 0 && !service.input_preferences_.tab_candidates && !key_manager.shortcuts,
            "settings notification removes previous shortcuts without restarting");
      service.context_ = &context; context.AddRef();
      service.session_.Type("kana");
      service.OnTestKeyDown(&context, VK_TAB, 0, &eaten);
      Check(!eaten, "native Tab preference passes navigation to the app while composing");
      service.ResetSessionState();
      service.context_->Release(); service.context_ = nullptr;
      Check(button->OnClick(TF_LBI_CLK_LEFT, {}, nullptr) == S_OK && shows(L"A"),
            "mouse switch remains available with keyboard shortcuts disabled");
      const ime::InputPreferences custom{{ime::ShortcutMode::Custom, VK_F9, MOD_CONTROL}, true};
      Check(ime::SaveInputPreferences(profile, disabled, custom), "save Ctrl+F9 as the only shortcut");
      SendMessageW(service.cand_window_.poll_hwnd(), changed, 0, 0);
      Check(key_manager.shortcuts == 1 && service.mode_keys_[0].uVKey == VK_F9 &&
            (service.mode_keys_[0].uModifiers & TF_MOD_CONTROL), "only the saved custom chord is registered");
      service.OnPreservedKey(&context, key_manager.last_shortcut, &eaten);
      Check(eaten && shows(L"あ"), "custom shortcut reopens Japanese input");
      Check(ime::SaveInputPreferences(profile, custom, {}), "restore factory input preferences");
      SendMessageW(service.cand_window_.poll_hwnd(), changed, 0, 0);
      Check(key_manager.shortcuts > 1 && service.input_preferences_.tab_candidates, "restore default shortcuts and Tab");
      if (original.vt == VT_I4) service.keyboard_open_->SetValue(client, &original);
      VariantClear(&original);
      button->AddRef();
      service.Deactivate();
      Check(!key_manager.shortcuts, "all Windows shortcuts released when switching IMEs");
      Check(button->OnClick(TF_LBI_CLK_LEFT, {}, nullptr) == TF_E_DISCONNECTED,
            "old shell button is harmless after profile switch");
      button->Release();
    } else if (SUCCEEDED(hr)) service.Deactivate();
    manager->Deactivate();
    manager->Release();
    std::filesystem::remove(profile / L"settings.json");
    std::filesystem::remove(profile);
  }
  static void Run() {
    TestInputMode();
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
    Check(window.poll_interval_ == 0, "idle window has no polling timer");
    window.SetPollInterval(25);
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
      FakeContext context; TextService service;
      Check(SUCCEEDED(service.Activate(mgr,client)),"activate for completed result awaiting its edit");
      service.context_=&context; context.AddRef();
      service.session_.Type("hashi"); service.session_.RefineCandidates();
      const auto request=service.session_.decode_input();
      ime::Candidate provisional,confirmed;
      provisional.output_text=u8"端"; confirmed.output_text=u8"橋";
      auto early=service.session_, ready=service.session_;
      Check(early.ApplyPendingPreview(request,{provisional},1) && ready.ApplyCandidates(request,{confirmed}) &&
            service.RequestState(&context,ready,ready.visible_text(),false,false,true)==TF_S_ASYNC &&
            service.HasQueuedFinal(),"remember completed worker reply while document lock is deferred");
      Check(service.ApplyState(1,&context,request,early,early.visible_text(),false,false)==S_FALSE,
            "older provisional edit cannot paint after final result was received");
      service.session_.set_context(u8"橋を", "");
      Check(!service.HasQueuedFinal() && context.Flush()==S_FALSE,
            "changed context discards queued commit result and pending document edit");
      service.Deactivate();
    }
    {
      FakeContext context;TextService service;
      Check(SUCCEEDED(service.Activate(mgr,client)),"activate for queued preview ordering");
      service.context_=&context;context.AddRef();
      service.session_.Type("hashi");service.session_.RefineCandidates();
      const auto request=service.session_.decode_input();
      ime::Candidate older,newer;older.output_text=u8"端";newer.output_text=u8"橋";
      auto early=service.session_;
      Check(early.ApplyPendingPreview(request,{older},1) &&
            service.RequestState(&context,early,early.visible_text(),false,false,true)==TF_S_ASYNC,
            "queue earlier neural display");
      Check(service.session_.ApplyPendingPreview(request,{newer},2) && context.Flush()==S_FALSE &&
            service.session_.visible_text()==u8"橋","delayed TSF edit cannot replace newer intermediate");
      Check(service.RequestState(&context,early,early.visible_text(),false,false,true)==TF_S_ASYNC &&
            service.session_.ApplyCandidates(request,{newer}) && context.Flush()==S_FALSE &&
            service.session_.candidates_ready(),"delayed TSF edit cannot undo final candidate readiness");
      service.Deactivate();
    }
    {
      TextService service;
      Check(SUCCEEDED(service.Activate(mgr, client)), "activate service");
      Check(!service.engine_channel_.running(), "activation does not launch inference");
      {
        FakeContext context;
        service.context_ = &context; context.AddRef();
        service.session_.Type("ka");
        service.last_key_at_ = GetTickCount64();
        service.preview_delay_ms_ = 125;
        service.PollEngine();
        Check(!service.engine_channel_.running(), "rapid keys keep local preview without launching a worker");
        wchar_t exe[MAX_PATH]{}; GetModuleFileNameW(nullptr, exe, MAX_PATH);
        Check(service.engine_channel_.Start(exe), "start isolated worker for scheduling tests");
        service.last_key_at_ -= 300;
        service.PollEngine();
        Check(service.session_.decode_input().phase == "end_of_phrase" && service.submitted_revision_ == service.session_.revision(),
              "quiet interval submits final-quality conversion");
        service.session_.Reset();
        service.last_key_at_ = GetTickCount64() - 61000;
        const auto deadline = GetTickCount64() + 2000;
        while (service.engine_channel_.running() && GetTickCount64() < deadline) { service.PollEngine(); Sleep(1); }
        Check(!service.engine_channel_.running() && service.cand_window_.poll_interval_ == 0,
              "idle service releases worker and stops timer");
        service.context_->Release(); service.context_ = nullptr;
      }
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

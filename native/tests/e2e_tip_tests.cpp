#include <windows.h>
#include <msctf.h>
#include <ctfutb.h>
#include <ctffunc.h>
#include <oleauto.h>
#include <commctrl.h>
#include <richedit.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "tip_diagnostics.h"

#pragma comment(lib, "ole32.lib")

// tip.hの登録GUIDと一致させる。
static const GUID kTextServiceClsid = {
    0x8f3a1c2e, 0x4b5d, 0x4e6f, {0x8a, 0x9b, 0x0c, 0x1d, 0x2e, 0x3f, 0x4a, 0x5b}};
static const GUID kProfileGuid = {
    0x8f3a1c2e, 0x4b5d, 0x4e6f, {0x8a, 0x9b, 0x0c, 0x1d, 0x2e, 0x3f, 0x4a, 0x5c}};

static HWND g_hMain = nullptr;
static HWND g_hEdit = nullptr;
static bool g_activate_profile = true;
static int g_fail = 0;
static int g_pass = 0;

static std::string Utf8(const wchar_t* text) {
  if (!text || !*text) return {};
  const int size=WideCharToMultiByte(CP_UTF8,0,text,-1,nullptr,0,nullptr,nullptr);
  std::string value(size, '\0');
  WideCharToMultiByte(CP_UTF8,0,text,-1,value.data(),size,nullptr,nullptr);
  value.resize(size-1); return value;
}

static void Expect(bool cond, const char* name, const std::string& detail = "") {
  if (cond) {
    std::printf("[PASS] %s\n", name);
    ++g_pass;
  } else {
    std::printf("[FAIL] %s %s\n", name, detail.c_str());
    ++g_fail;
  }
  std::fflush(stdout);
}

static void PrintTipDiagnostics(const char* phase) {
  HMODULE module = GetModuleHandleW(L"ime_mixed_tip_v14.dll");
  auto get = module ? reinterpret_cast<ImeTipGetDiagnosticsFn>(
                          GetProcAddress(module, "ImeTipGetDiagnostics")) : nullptr;
  TipDiagnostics d{};
  d.size = sizeof(d);
  if (!get || !get(&d)) {
    std::printf("TIPDIAG phase=%s available=0\n", phase);
    return;
  }
  std::printf("TIPDIAG phase=%s act=%u ok=%u flags=0x%x advise=0x%08x test=%u key=%u "
              "test_eaten=%u noctx=%u restricted=%u bg=%u compartment=%u readonly=%u "
              "nochar=%u state_ok=%u state_fail=%u langbar_mgr=0x%08x langbar_add=0x%08x\n",
              phase, d.activate_calls, d.activate_success, d.activate_flags,
              static_cast<unsigned>(d.advise_result), d.test_key_down, d.key_down,
              d.test_eaten, d.gated_no_context, d.gated_restricted, d.gated_background,
              d.gated_compartment, d.gated_readonly, d.gated_no_character,
              d.state_success, d.state_failure,
              static_cast<unsigned>(d.langbar_manager_result), static_cast<unsigned>(d.langbar_add_result));
}

static void Pump(int ms = 30) {
  const ULONGLONG end = GetTickCount64() + static_cast<ULONGLONG>(ms);
  MSG msg;
  while (GetTickCount64() < end) {
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    Sleep(5);
  }
}

static std::wstring GetEditText() {
  Pump(50);
  int len = GetWindowTextLengthW(g_hEdit);
  if (len <= 0) return L"";
  std::wstring s(static_cast<size_t>(len) + 1, L'\0');
  GetWindowTextW(g_hEdit, s.data(), len + 1);
  s.resize(wcslen(s.c_str()));
  return s;
}

static void ClearEdit() {
  SendMessageW(g_hEdit, EM_SETSEL, 0, -1);
  SendMessageW(g_hEdit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L""));
  Pump(50);
}

static void SendVk(WORD vk, bool shift = false) {
  INPUT inp[4] = {};
  int n = 0;
  if (shift) {
    inp[n].type = INPUT_KEYBOARD;
    inp[n].ki.wVk = VK_SHIFT;
    ++n;
  }
  inp[n].type = INPUT_KEYBOARD;
  inp[n].ki.wVk = vk;
  ++n;
  inp[n].type = INPUT_KEYBOARD;
  inp[n].ki.wVk = vk;
  inp[n].ki.dwFlags = KEYEVENTF_KEYUP;
  ++n;
  if (shift) {
    inp[n].type = INPUT_KEYBOARD;
    inp[n].ki.wVk = VK_SHIFT;
    inp[n].ki.dwFlags = KEYEVENTF_KEYUP;
    ++n;
  }
  SendInput(n, inp, sizeof(INPUT));
  Pump(15);
}

static void SendAscii(const char* s) {
  for (const char* p = s; *p; ++p) {
    char c = *p;
    if (c >= 'A' && c <= 'Z') {
      SendVk(static_cast<WORD>(c), true);
    } else if (c >= 'a' && c <= 'z') {
      SendVk(static_cast<WORD>(c - 'a' + 'A'), false);
    } else if (c >= '0' && c <= '9') {
      SendVk(static_cast<WORD>(c), false);
    } else if (c == '.') {
      SendVk(VK_OEM_PERIOD, false);
    } else if (c == ',') {
      SendVk(VK_OEM_COMMA, false);
    } else if (c == '/') {
      SendVk(VK_OEM_2, false);
    } else if (c == ' ') {
      SendVk(VK_SPACE, false);
    } else if (c == '-') {
      SendVk(VK_OEM_MINUS, false);
    }
  }
}

static void FocusEdit() {
  SetForegroundWindow(g_hMain);
  SetFocus(g_hEdit);
  Pump(30);
  // フォーカス移動後に、試験スレッドの入力プロファイルを選び直す。
  if (!g_activate_profile) return;
  ITfInputProcessorProfiles* profiles = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_ITfInputProcessorProfiles,
                                 reinterpret_cast<void**>(&profiles))) &&
      profiles) {
    profiles->ActivateLanguageProfile(kTextServiceClsid, 0x0411, kProfileGuid);
    LANGID lang = 0;
    GUID prof = {};
    if (SUCCEEDED(profiles->GetActiveLanguageProfile(kTextServiceClsid, &lang, &prof))) {
      bool ours = IsEqualGUID(prof, kProfileGuid);
      if (!ours) {
        std::printf("active profile NOT ours\n");
        profiles->ActivateLanguageProfile(kTextServiceClsid, 0x0411, kProfileGuid);
      }
    }
    profiles->Release();
  }
  Pump(20);
}

static bool ActivateOurTtip() {
  ITfInputProcessorProfiles* profiles = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_ITfInputProcessorProfiles,
                                 reinterpret_cast<void**>(&profiles));
  if (FAILED(hr) || !profiles) {
    std::printf("CoCreate ITfInputProcessorProfiles failed hr=0x%08lx\n", hr);
    return false;
  }

  profiles->EnableLanguageProfile(kTextServiceClsid, 0x0411, kProfileGuid, TRUE);

  // 英語環境のCIでも、日本語を選んでからIMEを有効にする。
  LANGID current_language = 0;
  profiles->GetCurrentLanguage(&current_language);
  std::printf("Current input language=0x%04x\n", current_language);
  if (current_language != 0x0411) {
    hr = profiles->ChangeCurrentLanguage(0x0411);
    std::printf("ChangeCurrentLanguage hr=0x%08lx\n", hr);
    if (FAILED(hr)) {
      const auto keyboard = LoadKeyboardLayoutW(L"00000411", KLF_ACTIVATE);
      std::printf("Load Japanese keyboard available=%d\n", keyboard ? 1 : 0);
      hr = profiles->ChangeCurrentLanguage(0x0411);
      std::printf("ChangeCurrentLanguage after keyboard hr=0x%08lx\n", hr);
    }
    Pump(150);
  }

  BSTR desc = nullptr;
  hr = profiles->GetLanguageProfileDescription(kTextServiceClsid, 0x0411, kProfileGuid, &desc);
  std::printf("GetLanguageProfileDescription hr=0x%08lx\n", hr);
  if (desc) {
    char buf[128] = {};
    int n = WideCharToMultiByte(CP_UTF8, 0, desc, -1, buf, sizeof(buf) - 1, nullptr, nullptr);
    std::printf("profile desc=[%s]\n", n > 0 ? buf : "?");
    SysFreeString(desc);
  }

  bool ok = false;
  for (int i = 0; i < 8; ++i) {
    hr = profiles->ActivateLanguageProfile(kTextServiceClsid, 0x0411, kProfileGuid);
    std::printf("ActivateLanguageProfile try=%d hr=0x%08lx\n", i, hr);
    if (SUCCEEDED(hr)) {
      ok = true;
      break;
    }
    Pump(150);
  }
  profiles->Release();
  return ok;
}

static LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(hwnd, msg, wp, lp);
  }
}

static bool CreateUi() {
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = MainProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = L"ImeE2EHost";
  RegisterClassExW(&wc);

  g_hMain = CreateWindowExW(0, L"ImeE2EHost", L"IME E2E Host", WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, CW_USEDEFAULT, 640, 360, nullptr, nullptr,
                            wc.hInstance, nullptr);
  if (!g_hMain) return false;

  // Rich EditでTSFを有効にする。SES_USECTFは既定でオフ。
  static HMODULE hre = LoadLibraryW(L"Msftedit.dll");
  if (!hre) return false;
  g_hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"RICHEDIT50W", L"",
                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL |
                                ES_WANTRETURN,
                            8, 8, 600, 300, g_hMain, nullptr, wc.hInstance, nullptr);
  if (!g_hEdit) return false;
  const LRESULT edit_style = SendMessageW(g_hEdit, EM_SETEDITSTYLE, SES_USECTF, SES_USECTF);
  if ((edit_style & SES_USECTF) == 0) return false;

  ShowWindow(g_hMain, SW_SHOWNORMAL);
  // 起動元がwShowWindowを指定すると初回のnCmdShowが無視されるため、
  // 2回目のShowWindowで試験ホストを表示する。
  ShowWindow(g_hMain, SW_SHOWNORMAL);
  UpdateWindow(g_hMain);
  if (!IsWindowVisible(g_hMain)) return false;
  FocusEdit();
  if (!SendMessageW(g_hEdit, EM_SETCTFOPENSTATUS, TRUE, 0) ||
      !SendMessageW(g_hEdit, EM_GETCTFOPENSTATUS, 0, 0)) return false;
  std::puts("E2E RichEdit TSF=enabled keyboard=open");
  return true;
}

struct Case {
  const char* name;
  const char* keys;       // ascii sequence, special tokens handled separately
  int enter;              // 0 none, 1 enter, 2 escape, 3 space+enter
  bool expect_nonempty;
  bool expect_empty;
  const wchar_t* expect_contains;
  bool expect_contains_any;  // if true, expect_contains is searched; else exact when non-null
  bool exact;
};

static void RunKeys(const char* keys) { SendAscii(keys); }

static void PrintDemoFrame(int case_id, const char* typed, const char* phase) {
  const std::wstring display = GetEditText();
  const int needed = WideCharToMultiByte(CP_UTF8, 0, display.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (needed <= 0) return;
  std::string utf8(static_cast<size_t>(needed), '\0');
  WideCharToMultiByte(CP_UTF8, 0, display.c_str(), -1, utf8.data(), needed, nullptr, nullptr);
  std::printf("DEMO_FRAME\t%d\t%s\t%s\t%s\n", case_id, phase, typed, utf8.c_str());
}

static void RunKeysForDemo(int case_id, const char* keys) {
  std::string typed;
  for (const char* p = keys; *p; ++p) {
    const char one[] = {*p, '\0'};
    SendAscii(one);
    Pump(95);
    typed.push_back(*p);
    PrintDemoFrame(case_id, typed.c_str(), "typing");
  }
}

static bool RunDemoCase(int case_id, const char* keys, const wchar_t* expected) {
  ClearEdit();
  FocusEdit();
  Pump(350);
  PrintDemoFrame(case_id, "", "start");
  RunKeysForDemo(case_id, keys);
  const auto deadline = GetTickCount64() + 10000;
  while (GetTickCount64() < deadline && GetEditText() != expected) Pump(50);
  PrintDemoFrame(case_id, keys, "candidate");
  SendVk(VK_RETURN);
  Pump(1400);
  const bool matched = GetEditText() == expected;
  PrintDemoFrame(case_id, keys, "committed");
  std::printf("DEMO %s\n", matched ? "PASS" : "FAIL");
  return matched;
}

static void ClearBetween() {
  FocusEdit();
  ClearEdit();
}

static bool CandidateVisible() {
  bool visible = false;
  EnumThreadWindows(GetCurrentThreadId(), [](HWND hwnd, LPARAM value) -> BOOL {
    wchar_t name[80]{}; GetClassNameW(hwnd, name, 80);
    if (wcscmp(name, L"ImeMixedCandidateWindow") == 0 && IsWindowVisible(hwnd))
      *reinterpret_cast<bool*>(value) = true;
    return TRUE;
  }, reinterpret_cast<LPARAM>(&visible));
  return visible;
}

static void WaitForCommit(const wchar_t* expected = nullptr) {
  const auto deadline = GetTickCount64() + 4000;
  do { Pump(30); } while ((CandidateVisible() || (expected && GetEditText()!=expected)) && GetTickCount64() < deadline);
}

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(hrCo)) {
    std::printf("CoInitialize failed 0x%08lx\n", hrCo);
    return 2;
  }

  // 試験ホスト自身でTSFを有効化し、プロファイルの選択を入力サービスへ反映する。
  // 終了時には対応するDeactivateを呼ぶ。
  ITfThreadMgr* tsf_thread_mgr = nullptr;
  HRESULT hrTsf = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_ITfThreadMgr,
                                   reinterpret_cast<void**>(&tsf_thread_mgr));
  TfClientId tsf_client_id = TF_CLIENTID_NULL;
  if (SUCCEEDED(hrTsf) && tsf_thread_mgr) hrTsf = tsf_thread_mgr->Activate(&tsf_client_id);
  std::printf("TSF thread activation hr=0x%08lx\n", hrTsf);
  if (FAILED(hrTsf)) {
    if (tsf_thread_mgr) tsf_thread_mgr->Release();
    CoUninitialize();
    return 2;
  }
  if (argc == 2 && std::strcmp(argv[1], "--tsf-smoke") == 0) {
    tsf_thread_mgr->Deactivate();
    tsf_thread_mgr->Release();
    CoUninitialize();
    std::puts("TSF thread activation smoke=ok");
    return 0;
  }
  if (argc == 2 && std::strcmp(argv[1], "--richedit-smoke") == 0) {
    g_activate_profile = false;
    const bool ok = CreateUi();
    if (g_hMain) DestroyWindow(g_hMain);
    tsf_thread_mgr->Deactivate();
    tsf_thread_mgr->Release();
    CoUninitialize();
    std::printf("RichEdit TSF smoke=%s\n", ok ? "ok" : "failed");
    return ok ? 0 : 4;
  }
  std::printf("=== IME E2E start ===\n");

  if (!CreateUi()) {
    std::printf("CreateUi failed\n CoUninitialize\n");
    tsf_thread_mgr->Deactivate();
    tsf_thread_mgr->Release();
    CoUninitialize();
    return 2;
  }
  Pump(100);

  if (!ActivateOurTtip()) {
    std::printf("[FAIL] activate ttip\n");
    DestroyWindow(g_hMain);
    tsf_thread_mgr->Deactivate();
    tsf_thread_mgr->Release();
    CoUninitialize();
    return 3;
  }
  Pump(100);
  FocusEdit();
  // 言語を切り替えると開閉状態も切り替わるので、選択後に入力を開く。
  SendMessageW(g_hEdit, EM_SETCTFOPENSTATUS, TRUE, 0);
  ITfCompartmentMgr* compartments = nullptr;
  ITfCompartment* keyboard_open = nullptr;
  HRESULT open_hr = tsf_thread_mgr->QueryInterface(IID_ITfCompartmentMgr,
      reinterpret_cast<void**>(&compartments));
  if (SUCCEEDED(open_hr)) open_hr = compartments->GetCompartment(
      GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &keyboard_open);
  if (SUCCEEDED(open_hr)) {
    VARIANT enabled{}; enabled.vt = VT_I4; enabled.lVal = 1;
    open_hr = keyboard_open->SetValue(tsf_client_id, &enabled);
  }
  if (keyboard_open) keyboard_open->Release();
  if (compartments) compartments->Release();
  std::printf("Open selected IME hr=0x%08lx edit_open=%d\n", open_hr,
              SendMessageW(g_hEdit, EM_GETCTFOPENSTATUS, 0, 0) ? 1 : 0);
  if (FAILED(open_hr)) {
    DestroyWindow(g_hMain); tsf_thread_mgr->Deactivate();
    tsf_thread_mgr->Release(); CoUninitialize(); return 2;
  }
  Pump(100);
  if (argc == 2 && std::strcmp(argv[1], "--stability-inputs") == 0) {
    ClearBetween(); RunKeys("a"); SendVk(VK_RETURN); RunKeys("i"); SendVk(VK_RETURN); Pump(1500);
    Expect(GetEditText() == L"あい", "S00 typing after pending Enter preserves input order");
    for (int i = 0; i < 12; ++i) {
      ClearBetween(); RunKeys("nihongo");
      auto deadline = GetTickCount64() + 4000;
      while (GetTickCount64() < deadline && GetEditText() != L"日本語") Pump(30);
      Expect(GetEditText() == L"日本語", "S01 conversion before closing input window");
      // 未確定文字列と候補を残したまま入力先を閉じ、同じスレッドで作り直す。
      DestroyWindow(g_hMain); Pump(100);
      Expect(CreateUi(), "S02 recreate input window on same TSF thread");
      if (!IsWindow(g_hEdit)) break;
      RunKeys("sannkai");
      deadline = GetTickCount64() + 4000;
      while (GetTickCount64() < deadline && GetEditText() != L"散開") Pump(30);
      SendVk(VK_RETURN);
      WaitForCommit();
      Expect(GetEditText() == L"散開", "S03 engine delivers candidates after owner destruction");
      Expect(!CandidateVisible(), "S04 committed composition leaves no candidate popup");
    }
    PrintTipDiagnostics("stability_inputs");
    DestroyWindow(g_hMain); tsf_thread_mgr->Deactivate(); tsf_thread_mgr->Release(); CoUninitialize();
    std::printf("E2E done pass=%d fail=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
  }
  if (argc == 2 && std::strcmp(argv[1], "--learning-inputs") == 0) {
    ClearBetween(); RunKeys("sannkai"); Pump(1500);
    const auto original=GetEditText();
    SendVk(VK_SPACE); SendVk(VK_DOWN); Pump(100);
    const auto selected=GetEditText();
    Expect(!selected.empty() && selected!=original, "L01 select an alternative candidate");
    SendVk(VK_RETURN); Pump(700);
    Expect(GetEditText()==selected,"L02 commit selected candidate");
    ClearBetween(); RunKeys("sannkai"); Pump(1500); SendVk(VK_RETURN);
    WaitForCommit();
    const auto learned=GetEditText();
    std::printf("LEARNING original=[%s] selected=[%s] next=[%s]\n",
                Utf8(original.c_str()).c_str(), Utf8(selected.c_str()).c_str(), Utf8(learned.c_str()).c_str());
    Expect(learned==selected,"L03 learned choice leads the next conversion");
    DestroyWindow(g_hMain);
    tsf_thread_mgr->Deactivate(); tsf_thread_mgr->Release(); CoUninitialize();
    std::printf("E2E done pass=%d fail=%d\n",g_pass,g_fail);
    return g_fail?1:0;
  }
  if (argc == 2 && std::strcmp(argv[1], "--reported-inputs") == 0) {
    const std::pair<const char*, const wchar_t*> reported[] = {
      {"insuto-ru", L"インストール"}, {"innsuto-ru", L"インストール"},
      {"aninsuto-ru", L"アンインストール"}, {"anninsuto-ru", L"アンインストール"},
      {"de-taeb-su", L"データベース"},
      {"softwarewokoushinsuru", L"softwareを更新する"}
    };
    for (const auto& item : reported) {
      ClearBetween();
      RunKeys(item.first);
      const auto deadline = GetTickCount64() + 4000;
      while (GetTickCount64() < deadline && GetEditText() != item.second) Pump(30);
      SendVk(VK_RETURN);
      WaitForCommit(item.second);
      auto text = GetEditText();
      std::printf("REPORTED %s => [%s]\n", item.first, Utf8(text.c_str()).c_str());
      Expect(text == item.second && !CandidateVisible(), item.first);
    }
    for (const auto& item : reported) {
      ClearBetween();
      RunKeys(item.first);
      SendVk(VK_RETURN);
      Pump(1000);
      auto text = GetEditText();
      std::printf("QUICK_ENTER %s => [%s]\n", item.first, Utf8(text.c_str()).c_str());
      Expect(text == item.second, "Enter immediately after typing");
    }
    ClearBetween(); RunKeys("nakagakara"); SendVk(VK_RETURN); WaitForCommit(L"中が空");
    Expect(GetEditText() == L"中が空", "R11 refine ambiguous adjective reading on Enter");
    Expect(!CandidateVisible(), "R12 refined commit closes candidates");
    ClearBetween(); RunKeys("karanoyouki"); Pump(1000); SendVk(VK_SPACE); Pump(1000);
    for (int i=0; i<16 && GetEditText()!=L"空の容器"; ++i) { SendVk(VK_DOWN); Pump(50); }
    Expect(GetEditText() == L"空の容器", "R13 empty container is selectable");
    SendVk(VK_RETURN); WaitForCommit();
    Expect(GetEditText() == L"空の容器" && !CandidateVisible(), "R14 selected container is committed");
    PrintTipDiagnostics("reported_inputs");
    DestroyWindow(g_hMain);
    tsf_thread_mgr->Deactivate(); tsf_thread_mgr->Release(); CoUninitialize();
    std::printf("E2E done pass=%d fail=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
  }
  if (argc == 2 && std::strcmp(argv[1], "--demo") == 0) {
    SetWindowTextW(g_hMain, L"Yomitsugu input demo (Rich Edit)");
    SetWindowPos(g_hMain, HWND_TOP, 120, 100, 700, 360, SWP_SHOWWINDOW);
    FocusEdit();
    Pump(2200);
    const bool first = RunDemoCase(1, "samukunaltutekimasitane", L"寒くなってきましたね");
    const bool second = RunDemoCase(2, "ri-domi-wokousinnsitekudasaiGithubde",
                                    L"READMEを更新してくださいGithubで");
    const bool third = RunDemoCase(3, "sannkai", L"散開");
    DestroyWindow(g_hMain);
    tsf_thread_mgr->Deactivate();
    tsf_thread_mgr->Release();
    CoUninitialize();
    return first && second && third ? 0 : 1;
  }
  std::printf("E2E host visible=%d foreground=%d focused=%d keyboard_lang=0x%04x\n",
              IsWindowVisible(g_hMain) ? 1 : 0,
              GetForegroundWindow() == g_hMain ? 1 : 0,
              GetFocus() == g_hEdit ? 1 : 0,
              LOWORD(reinterpret_cast<ULONG_PTR>(GetKeyboardLayout(0))));
  std::printf("E2E TIP module_loaded=%d\n", GetModuleHandleW(L"ime_mixed_tip_v14.dll") ? 1 : 0);

  // 入力インジケーターに、このTIPの「あ」メニューが登録されているか確認する。
  {
    ITfLangBarItemMgr* manager = nullptr;
    ITfLangBarItem* item = nullptr;
    ITfLangBarItemButton* button = nullptr;
    BSTR label = nullptr;
    HRESULT manager_hr = tsf_thread_mgr->QueryInterface(IID_ITfLangBarItemMgr,
        reinterpret_cast<void**>(&manager));
    HRESULT item_hr = manager ? manager->GetItem(GUID_LBI_INPUTMODE, &item) : E_NOINTERFACE;
    HRESULT button_hr = item ? item->QueryInterface(IID_ITfLangBarItemButton,
        reinterpret_cast<void**>(&button)) : E_NOINTERFACE;
    HRESULT text_hr = button ? button->GetText(&label) : E_NOINTERFACE;
    std::printf("LANGBAR mgr=0x%08lx item=0x%08lx button=0x%08lx text=0x%08lx label=%s\n",
                manager_hr, item_hr, button_hr, text_hr, Utf8(label ? label : L"").c_str());
    const bool available = SUCCEEDED(manager_hr) && SUCCEEDED(item_hr) && SUCCEEDED(button_hr) &&
                           SUCCEEDED(text_hr) && label && wcscmp(label, L"あ") == 0;
    Expect(available, "T00 input indicator exposes hiragana menu item");
    if (label) SysFreeString(label);
    if (button) button->Release();
    if (item) item->Release();
    if (manager) manager->Release();
  }

  // プロファイル選択だけで成功とせず、実際にキーが変換されることを確認する。
  // 後続の空文字チェックだけでは、英字のまま通る不具合を見逃す。
  {
    ClearBetween();
    RunKeys("ka");
    Pump(120);
    SendVk(VK_RETURN);
    Pump(120);
    Expect(GetEditText() == L"か", "T00 active TIP converts romaji to kana");
    PrintTipDiagnostics("after_T00");
  }

  // T01: 入力をEnterで確定し、未確定状態を終了する。
  {
    ClearBetween();
    FocusEdit();
    RunKeys("konnitiwa");
    Pump(80);
    SendVk(VK_RETURN);
    Pump(120);
    std::wstring t = GetEditText();
    std::printf("T01 text=[%s] len=%zu\n", Utf8(t.c_str()).c_str(), t.size());
    Expect(!t.empty(), "T01 enter commits non-empty");
    // 確定後に続けて入力できることを確認する。
    RunKeys("a");
    Pump(50);
    SendVk(VK_RETURN);
    Pump(100);
    std::wstring t2 = GetEditText();
    std::printf("T01b text=[%s]\n", Utf8(t2.c_str()).c_str());
    Expect(t2.size() > t.size(), "T01 second cycle appends (no stuck composition)");
  }

  // T02: Escで取り消し、確定文字を残さない。
  {
    ClearBetween();
    FocusEdit();
    RunKeys("konnitiwa");
    Pump(80);
    SendVk(VK_ESCAPE);
    Pump(150);
    std::wstring t = GetEditText();
    std::printf("T02 text=[%s] len=%zu\n", Utf8(t.c_str()).c_str(), t.size());
    Expect(t.empty(), "T02 escape cancels", std::to_string(t.size()));
    // 取り消し後にフォーカスを戻して入力する。
    FocusEdit();
    RunKeys("hi");
    Pump(80);
    SendVk(VK_RETURN);
    Pump(150);
    std::wstring t2 = GetEditText();
    std::printf("T02b text=[%s]\n", Utf8(t2.c_str()).c_str());
    Expect(!t2.empty(), "T02 type after escape works");
  }

  // T03: Spaceで変換してEnterで確定する。
  {
    ClearBetween();
    FocusEdit();
    RunKeys("konnitiwa");
    Pump(100);
    FocusEdit();
    SendVk(VK_SPACE);
    Pump(100);
    SendVk(VK_RETURN);
    Pump(150);
    std::wstring t = GetEditText();
    std::printf("T03 text=[%s] len=%zu\n", Utf8(t.c_str()).c_str(), t.size());
    Expect(!t.empty(), "T03 space+enter commits");
    // 確定後も英字を入力できることを確認する。
    ClearBetween();
    RunKeys("test");
    Pump(50);
    SendVk(VK_RETURN);
    Pump(100);
    std::wstring t2 = GetEditText();
    std::printf("T03b text=[%s]\n", Utf8(t2.c_str()).c_str());
    Expect(!t2.empty(), "T03 subsequent typing works");
  }

  // T04: 未確定入力がなければSpaceをアプリへ渡す。
  {
    ClearBetween();
    FocusEdit();
    // Escで未確定入力を取り消す。
    SendVk(VK_ESCAPE);
    Pump(50);
    // 文字を入力せずSpaceを押す。
    SendVk(VK_SPACE);
    Pump(80);
    std::wstring t = GetEditText();
    std::printf("T04 text=[%s] len=%zu\n", Utf8(t.c_str()).c_str(), t.size());
    // Spaceの後も文字入力と確定ができることを確認する。
    RunKeys("abc");
    Pump(50);
    SendVk(VK_RETURN);
    Pump(100);
    std::wstring t2 = GetEditText();
    std::printf("T04b text=[%s]\n", Utf8(t2.c_str()).c_str());
    Expect(!t2.empty(), "T04 input after idle space works");
  }

  // T05: 入力と確定を繰り返し、未確定状態が固まらないことを確認する。
  {
    ClearBetween();
    bool all_ok = true;
    for (int i = 0; i < 5; ++i) {
      ClearEdit();
      FocusEdit();
      Pump(50);
      RunKeys("no");
      Pump(60);
      SendVk(VK_RETURN);
      Pump(150);
      std::wstring t = GetEditText();
      if (t.empty()) {
        std::printf("T05 cycle %d empty\n", i);
        all_ok = false;
      }
    }
    Expect(all_ok, "T05 five commit cycles all succeed");
  }

  // T06: 入力中にBackspaceで削除してから確定する。
  {
    ClearBetween();
    FocusEdit();
    RunKeys("konnitiwa");
    Pump(60);
    SendVk(VK_BACK);
    Pump(40);
    SendVk(VK_RETURN);
    Pump(100);
    std::wstring t = GetEditText();
    std::printf("T06 text=[%s]\n", Utf8(t.c_str()).c_str());
    Expect(!t.empty(), "T06 backspace+enter commits");
    // 削除後も入力できることを確認する。
    ClearEdit();
    RunKeys("x");
    Pump(40);
    SendVk(VK_RETURN);
    Pump(80);
    Expect(!GetEditText().empty(), "T06 still alive after backspace cycle");
  }

  // T07: 英単語の後に空白を入力する。
  {
    ClearBetween();
    FocusEdit();
    RunKeys("hello");
    Pump(60);
    SendVk(VK_SPACE);
    Pump(60);
    SendVk(VK_RETURN);
    Pump(100);
    std::wstring t = GetEditText();
    std::printf("T07 text=[%s]\n", Utf8(t.c_str()).c_str());
    Expect(!t.empty(), "T07 english+space+enter commits");
  }

  // T08: 入力を中断し、待機後に再び入力する。
  {
    ClearBetween();
    Pump(500);
    FocusEdit();
    RunKeys("ok");
    Pump(50);
    SendVk(VK_RETURN);
    Pump(100);
    Expect(!GetEditText().empty(), "T08 type after idle works");
  }

  // T09: F7/F6で文字種を変換する（KEY-09）。
  {
    ClearBetween();
    FocusEdit();
    RunKeys("konnitiwa");
    Pump(80);
    SendVk(VK_F7, false);  // to katakana if composing shows hiragana
    Pump(60);
    SendVk(VK_RETURN);
    Pump(120);
    std::wstring t = GetEditText();
    std::printf("T09 text=[%s]\n", Utf8(t.c_str()).c_str());
    Expect(!t.empty(), "T09 F7+enter commits");
    // 文字種の変換後も入力できることを確認する。
    ClearEdit();
    RunKeys("a");
    Pump(40);
    SendVk(VK_RETURN);
    Pump(80);
    Expect(!GetEditText().empty(), "T09 alive after F7");
  }

  // T10: Spaceの後に番号で候補を選ぶ（KEY-11）。
  {
    ClearBetween();
    FocusEdit();
    RunKeys("konnitiwa");
    Pump(80);
    SendVk(VK_SPACE);
    Pump(80);
    SendVk('2');  // select 2nd candidate if present
    Pump(60);
    SendVk(VK_RETURN);
    Pump(120);
    std::wstring t = GetEditText();
    std::printf("T10 text=[%s]\n", Utf8(t.c_str()).c_str());
    Expect(!t.empty(), "T10 digit-select+enter commits");
  }

  // T11: 上下キーで候補を選んで確定する。
  {
    ClearBetween();
    FocusEdit();
    RunKeys("konnitiwa");
    Pump(80);
    SendVk(VK_SPACE);
    Pump(80);
    SendVk(VK_DOWN);
    Pump(50);
    SendVk(VK_UP);
    Pump(50);
    SendVk(VK_RETURN);
    Pump(120);
    std::wstring t = GetEditText();
    std::printf("T11 text=[%s]\n", Utf8(t.c_str()).c_str());
    Expect(!t.empty(), "T11 down/up+enter commits");
  }

  // T12: 変換中の左右キーで処理が停止したり、不正な文字が入ったりしないか確認する。
  {
    ClearBetween();
    FocusEdit();
    RunKeys("READMEwoyondekudasai.");
    Pump(100);
    SendVk(VK_SPACE);
    Pump(80);
    SendVk(VK_RIGHT);
    Pump(40);
    SendVk(VK_LEFT);
    Pump(40);
    SendVk(VK_RETURN);
    Pump(120);
    std::wstring t = GetEditText();
    std::printf("T12 text=[%s]\n", Utf8(t.c_str()).c_str());
    Expect(!t.empty(), "T12 segment arrows+enter commits");
  }

  // 配布前の回帰試験。確定文字列の全文一致を確認する。
  {
    ClearBetween(); RunKeys("hello"); SendVk(VK_F9); SendVk(VK_RETURN); Pump(100);
    Expect(GetEditText() == L"ｈｅｌｌｏ", "R01 F9 exact fullwidth and commit caret");
    RunKeys("abc"); SendVk(VK_F10); SendVk(VK_RETURN); Pump(100);
    Expect(GetEditText() == L"ｈｅｌｌｏabc", "R02 F10 exact and appends after commit");
  }
  {
    ClearBetween(); RunKeys("gakkou"); SendVk(VK_SPACE); SendVk(VK_ESCAPE);
    SendVk(VK_F6); SendVk(VK_RETURN); Pump(100);
    Expect(GetEditText() == L"がっこう", "R03 conversion cancel preserves original reading");
  }
  {
    ClearBetween(); RunKeys("toshokan");
    auto deadline = GetTickCount64() + 10000;
    while (GetTickCount64() < deadline && GetEditText() != L"図書館") Pump(50);
    SendVk(VK_RETURN); Pump(100);
    Expect(GetEditText() == L"図書館", "R04 real dictionary through isolated worker");
  }
  // 報告された入力例をTSF経由で打鍵し、確定文字列を確認する。
  const struct { const char* keys; const wchar_t* expected; const char* label; } user_cases[] = {
      {"ltu", L"っ", "R05 ltu small tsu"},
      {"samukunaltutekimasitane", L"寒くなってきましたね", "R06 sentence with ltu"},
      {"saikilyou", L"最強", "R07 small yo chooses intended kanji"},
      {"yajirushi", L"→", "R08 common arrow symbol"},
      {".", L"。", "R09 Japanese period stays fullwidth"},
      {"ri-domi-", L"README", "R10 README from public dictionary"},
      {"a", L"あ", "R11 single vowel commits hiragana"},
      {"ri-domi-wokousinnsitekudasaiGithubde", L"READMEを更新してくださいGithubで", "R12 long mixed public dictionary sentence"},
      {"sannkai", L"散開", "R13 single-word lexical rank"},
  };
  for (const auto& c : user_cases) {
    ClearBetween(); FocusEdit(); RunKeys(c.keys);
    const auto deadline = GetTickCount64() + 10000;
    while (GetTickCount64() < deadline && GetEditText() != c.expected) Pump(50);
    SendVk(VK_RETURN); Pump(100);
    Expect(GetEditText() == c.expected, c.label);
  }
  std::printf("=== E2E done pass=%d fail=%d ===\n", g_pass, g_fail);
  PrintTipDiagnostics("final");

  DestroyWindow(g_hMain);
  Pump(50);
  tsf_thread_mgr->Deactivate();
  tsf_thread_mgr->Release();
  CoUninitialize();
  return g_fail == 0 ? 0 : 1;
}

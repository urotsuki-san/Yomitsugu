#include "learning_store.h"
#include "mode_shortcut.h"
#include "shortcut_dialog.h"
#include "../src/shortcut_dialog_ids.h"
#include <ime.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
unsigned scenario, stage, polls, notices;
int failure;
UINT message;
std::filesystem::path profile;
HWND owner;
const wchar_t* const label = L"入力キーの設定";
void Check(bool condition, int line) {
  if (!condition && !failure) { failure = line; std::cerr << "FAIL line " << line << "\n"; }
}
#define CHECK(value) Check((value), __LINE__)
void Key(HWND edit, UINT key, UINT scan, UINT modifier = 0) {
  BYTE before[256]{}, keys[256]{};
  GetKeyboardState(before);
  if (modifier) keys[modifier]=0x80;
  SetKeyboardState(keys);
  SendMessageW(edit, WM_KEYDOWN, key, 1 | (scan << 16));
  SetKeyboardState(before);
}
std::wstring Text(HWND control) {
  wchar_t value[256]{};
  GetWindowTextW(control,value,256);
  return value;
}
LRESULT CALLBACK OwnerProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
  if (message && msg == message) { ++notices; return 0; }
  return DefWindowProcW(hwnd,msg,w,l);
}
void CALLBACK Drive(HWND, UINT, UINT_PTR timer, DWORD) {
  const HWND dialog = FindWindowW(L"#32770",label);
  if (!dialog || GetWindow(dialog,GW_OWNER) != owner) {
    if (++polls > 250) { CHECK(false); KillTimer(nullptr,timer); PostQuitMessage(42); }
    return;
  }
  const auto keys = GetDlgItem(dialog,IDC_SHORTCUT_KEYS);
  const auto tab = GetDlgItem(dialog,IDC_SHORTCUT_TAB);
  const auto save = GetDlgItem(dialog,IDOK);
  auto click = [&](int id) { SendDlgItemMessageW(dialog,id,BM_CLICK,0,0); };
  if (scenario == 0 && stage == 0) {
    CHECK(IsDlgButtonChecked(dialog,IDC_SHORTCUT_DEFAULT)==BST_CHECKED);
    CHECK(!IsWindowEnabled(keys) && IsDlgButtonChecked(dialog,IDC_SHORTCUT_TAB)==BST_CHECKED);
    click(IDC_SHORTCUT_CUSTOM);
    CHECK(IsWindowEnabled(keys) && GetFocus()==keys && GetPropW(keys,L"Yomitsugu.ModeShortcutCapture.1"));
    Key(keys,'A',0x1e,VK_SHIFT);
    CHECK(!IsWindowEnabled(save));
    Key(keys,VK_DBE_ALPHANUMERIC,0x3a,VK_SHIFT);
    CHECK(IsWindowEnabled(save) && Text(keys)==L"Shift + 英数");
    CHECK(GetNextDlgTabItem(dialog,keys,FALSE)==tab);
    CHECK(ime::ReadInputPreferences(profile).mode_shortcut.mode==ime::ShortcutMode::Default);
    PostMessageW(keys,WM_KEYDOWN,VK_TAB,1 | (0x0f<<16));
    ++stage; return;
  }
  if (scenario == 0) {
    CHECK(GetFocus()==tab);
    Key(keys,'J',0x24,VK_CONTROL);
    CHECK(Text(keys)==L"Shift + 英数");
    click(IDC_SHORTCUT_TAB); click(IDOK);
  } else if (scenario == 1) {
    CHECK(IsDlgButtonChecked(dialog,IDC_SHORTCUT_CUSTOM)==BST_CHECKED && Text(keys)==L"Shift + 英数");
    click(IDC_SHORTCUT_RESET);
    CHECK(IsDlgButtonChecked(dialog,IDC_SHORTCUT_DEFAULT)==BST_CHECKED && IsDlgButtonChecked(dialog,IDC_SHORTCUT_TAB)==BST_CHECKED);
    click(IDCANCEL);
  } else if (scenario == 2) {
    click(IDC_SHORTCUT_OFF); click(IDOK);
  } else if (scenario == 3) {
    CHECK(IsDlgButtonChecked(dialog,IDC_SHORTCUT_OFF)==BST_CHECKED);
    click(IDC_SHORTCUT_RESET); click(IDOK);
  }
}
bool Open(unsigned item) {
  scenario=item; stage=polls=0;
  const auto timer=SetTimer(nullptr,0,15,Drive);
  const bool result=ShowInputPreferences(owner,profile);
  KillTimer(nullptr,timer);
  return result;
}
}
int main() {
  namespace fs=std::filesystem;
  profile=fs::temp_directory_path()/(L"YomitsuguKeyTest_"+std::to_wstring(GetCurrentProcessId()));
  if (!fs::create_directory(profile)) return 1;
  const auto original=GetThreadDesktop(GetCurrentThreadId());
  const auto desktop=CreateDesktopW((L"YomitsuguKeyTest_"+std::to_wstring(GetCurrentProcessId())).c_str(),
                                    nullptr,nullptr,0,GENERIC_ALL,nullptr);
  if (!desktop || !SetThreadDesktop(desktop)) return 2;
  const auto path=profile/L"settings.json";
  { std::ofstream file(path); file << R"({"conversion_model":"compact","learning_enabled":false,"extension":{"keep":17}})"; }
  message=RegisterWindowMessageW(ime::kInputPreferencesChanged);
  WNDCLASSW wc{}; wc.hInstance=GetModuleHandleW(nullptr); wc.lpfnWndProc=OwnerProc; wc.lpszClassName=L"YomitsuguKeyTest";
  RegisterClassW(&wc);
  owner=CreateWindowW(wc.lpszClassName,L"Test",WS_OVERLAPPEDWINDOW,0,0,100,100,nullptr,nullptr,wc.hInstance,nullptr);
  ShowWindow(owner,SW_SHOW);
  CHECK(Open(0));
  auto found=ime::ReadInputPreferences(profile);
  CHECK(found.mode_shortcut.mode==ime::ShortcutMode::Custom && found.mode_shortcut.virtual_key==VK_DBE_ALPHANUMERIC &&
        found.mode_shortcut.modifiers==MOD_SHIFT && !found.tab_candidates && notices==1);
  CHECK(!Open(1));
  found=ime::ReadInputPreferences(profile);
  CHECK(found.mode_shortcut.mode==ime::ShortcutMode::Custom && !found.tab_candidates && notices==1);
  ime::LearningStore store(profile);
  CHECK(!store.enabled() && store.model()==ime::ConversionModel::Compact);
  CHECK(store.SetModel(ime::ConversionModel::HybridFull) && store.SetEnabled(true));
  CHECK(ime::ReadInputPreferences(profile).mode_shortcut.virtual_key==VK_DBE_ALPHANUMERIC);
  CHECK(Open(2));
  found=ime::ReadInputPreferences(profile);
  CHECK(found.mode_shortcut.mode==ime::ShortcutMode::Disabled && !found.tab_candidates);
  CHECK(Open(3));
  found=ime::ReadInputPreferences(profile);
  CHECK(found.mode_shortcut.mode==ime::ShortcutMode::Default && found.tab_candidates && notices==3);
  auto hotkey=found, tab=found;
  hotkey.mode_shortcut={ime::ShortcutMode::Custom,'J',MOD_CONTROL}; tab.tab_candidates=false;
  CHECK(ime::SaveInputPreferences(profile,found,hotkey) && ime::SaveInputPreferences(profile,found,tab));
  const auto merged=ime::ReadInputPreferences(profile);
  CHECK(merged.mode_shortcut.virtual_key=='J' && !merged.tab_candidates && store.enabled() &&
        store.model()==ime::ConversionModel::HybridFull);
  CHECK(!ime::IsAllowedModeShortcut('A',0) && !ime::IsAllowedModeShortcut('A',MOD_SHIFT) &&
        !ime::IsAllowedModeShortcut(VK_TAB,MOD_ALT) && !ime::IsAllowedModeShortcut(VK_F4,MOD_ALT) &&
        !ime::IsAllowedModeShortcut('L',MOD_WIN) && !ime::IsAllowedModeShortcut(VK_DELETE,MOD_CONTROL|MOD_ALT) &&
        ime::IsAllowedModeShortcut(VK_NONCONVERT,0) && ime::IsAllowedModeShortcut(VK_SPACE,MOD_CONTROL));
  { std::ifstream file(path); std::string contents((std::istreambuf_iterator<char>(file)),{});
    CHECK(contents.find("\"extension\":{\"keep\":17}")!=std::string::npos); }
  DestroyWindow(owner); SetThreadDesktop(original); CloseDesktop(desktop);
  fs::remove(path); fs::remove(profile);
  if (failure) return 1;
  std::cout << "PASS native shortcut dialog, focus/Tab, observed Shift+F0/3A, cancel/reset/save, notification, settings merge\n";
  return 0;
}

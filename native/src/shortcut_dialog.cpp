#include "shortcut_dialog.h"
#include "shortcut_dialog_ids.h"
#include "mode_shortcut.h"
#include <commctrl.h>
#include <ime.h>
#include <string>
#include <vector>

namespace {
struct DialogState {
  std::filesystem::path profile;
  ime::InputPreferences before;
  ime::ModeShortcut custom{ime::ShortcutMode::Custom};
  bool raw_registered = false;
};
UINT Modifiers() {
  UINT result = 0;
  if (GetKeyState(VK_SHIFT) < 0) result |= MOD_SHIFT;
  if (GetKeyState(VK_CONTROL) < 0) result |= MOD_CONTROL;
  if (GetKeyState(VK_MENU) < 0) result |= MOD_ALT;
  return result;
}
bool ModifierKey(UINT key) {
  return key == VK_SHIFT || key == VK_CONTROL || key == VK_MENU ||
         (key >= VK_LSHIFT && key <= VK_RMENU) || key == VK_LWIN || key == VK_RWIN;
}
std::wstring KeyName(UINT key, UINT mods, UINT scan, bool extended) {
  std::wstring name;
  if (mods & MOD_CONTROL) name += L"Ctrl + ";
  if (mods & MOD_ALT) name += L"Alt + ";
  if (mods & MOD_SHIFT) name += L"Shift + ";
  switch (key) {
    case VK_DBE_ALPHANUMERIC: return name + L"英数";
    case VK_DBE_HIRAGANA: return name + L"ひらがな";
    case VK_DBE_KATAKANA: return name + L"カタカナ";
    case VK_IME_ON: return name + L"IME オン";
    case VK_IME_OFF: return name + L"IME オフ";
    case VK_KANJI: return name + L"半角／全角・漢字";
    case VK_KANA: return name + L"かな";
    case VK_CONVERT: return name + L"変換";
    case VK_NONCONVERT: return name + L"無変換";
  }
  if (!scan) {
    scan = MapVirtualKeyExW(key, MAPVK_VK_TO_VSC_EX, GetKeyboardLayout(0));
    extended = (scan >> 8) == 0xe0;
  }
  wchar_t display[80]{};
  const LONG code = static_cast<LONG>((scan & 0xff) << 16) | (extended ? 1 << 24 : 0);
  if (scan && GetKeyNameTextW(code, display, 80) > 0) return name + display;
  swprintf_s(display, L"VK %02X", key);
  return name + display;
}
bool ChooseCustom(HWND dialog) {
  return IsDlgButtonChecked(dialog, IDC_SHORTCUT_CUSTOM) == BST_CHECKED;
}
void Refresh(HWND dialog, const DialogState& state) {
  const bool custom = ChooseCustom(dialog);
  const auto& key = state.custom;
  EnableWindow(GetDlgItem(dialog, IDC_SHORTCUT_KEYS), custom);
  const auto name = custom && key.virtual_key ? KeyName(key.virtual_key, key.modifiers, 0, false) : L"";
  SetDlgItemTextW(dialog, IDC_SHORTCUT_KEYS, name.c_str());
  SetDlgItemTextW(dialog, IDC_SHORTCUT_HINT, custom ?
      L"上の欄で使いたいキーを押してください。英数・変換キーや、Ctrlなどを組み合わせたキーを登録できます。" :
      (IsDlgButtonChecked(dialog, IDC_SHORTCUT_OFF) == BST_CHECKED ?
       L"言語バーの入力モードから切り替えられます。" :
       L"Shift＋英数（Caps Lock）、半角／全角、Alt＋` で切り替えます。"));
  EnableWindow(GetDlgItem(dialog, IDOK), !custom || key.virtual_key != 0);
}
void Capture(HWND dialog, UINT key, UINT scan, bool extended) {
  if (!ChooseCustom(dialog) || GetFocus() != GetDlgItem(dialog, IDC_SHORTCUT_KEYS) ||
      ModifierKey(key) || key == VK_ESCAPE || key == VK_TAB || key == VK_RETURN ||
      !key || key == 0xff || key == VK_PROCESSKEY) return;
  auto& state = *reinterpret_cast<DialogState*>(GetWindowLongPtrW(dialog, DWLP_USER));
  const auto mods = Modifiers();
  auto name = KeyName(key, mods, scan, extended);
  const bool altgr = GetKeyState(VK_RMENU) < 0 && (mods & MOD_CONTROL);
  const bool valid = !altgr && GetKeyState(VK_LWIN) >= 0 && GetKeyState(VK_RWIN) >= 0 &&
                     ime::IsAllowedModeShortcut(key, mods);
  state.custom = {ime::ShortcutMode::Custom, valid ? key : 0, valid ? mods : 0};
  SetDlgItemTextW(dialog, IDC_SHORTCUT_KEYS, name.c_str());
  SetDlgItemTextW(dialog, IDC_SHORTCUT_HINT, valid ? L"「保存」でこのキーを使います。" :
      L"このキーは登録できません。文字キーにはCtrlかAltを組み合わせてください。");
  EnableWindow(GetDlgItem(dialog, IDOK), valid);
}
LRESULT CALLBACK CaptureProc(HWND edit, UINT msg, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR) {
  if (msg == WM_GETDLGCODE && l) {
    const auto* key = reinterpret_cast<const MSG*>(l);
    if ((key->message == WM_KEYDOWN || key->message == WM_SYSKEYDOWN) &&
        key->wParam != VK_TAB && key->wParam != VK_ESCAPE && key->wParam != VK_RETURN)
      return DLGC_WANTMESSAGE;
  }
  if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && w != VK_TAB && w != VK_ESCAPE && w != VK_RETURN) {
    if (!(l & (1LL << 30))) Capture(GetParent(edit), static_cast<UINT>(w), (l >> 16) & 0xff, (l & (1 << 24)) != 0);
    return 0;
  }
  if (msg == WM_CHAR || msg == WM_SYSCHAR) return 0;
  if (msg == WM_NCDESTROY) {
    RemovePropW(edit, L"Yomitsugu.ModeShortcutCapture.1");
    RemoveWindowSubclass(edit, CaptureProc, id);
  }
  return DefSubclassProc(edit, msg, w, l);
}
bool RegisterLocalKeyboard(HWND dialog) {
  UINT count = 0;
  if (GetRegisteredRawInputDevices(nullptr, &count, sizeof(RAWINPUTDEVICE)) == UINT(-1)) return false;
  if (count) {
    std::vector<RAWINPUTDEVICE> current(count);
    if (GetRegisteredRawInputDevices(current.data(), &count, sizeof(RAWINPUTDEVICE)) == UINT(-1)) return false;
    for (const auto& device : current)
      if (device.usUsagePage == 1 && device.usUsage == 6) return false;
  }
  RAWINPUTDEVICE device{1, 6, 0, dialog}; // Foreground only. Never a system-wide hook or input sink.
  return RegisterRawInputDevices(&device, 1, sizeof(device)) != FALSE;
}
INT_PTR CALLBACK DialogProc(HWND dialog, UINT msg, WPARAM w, LPARAM l) {
  auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(dialog, DWLP_USER));
  if (msg == WM_INITDIALOG) {
    state = reinterpret_cast<DialogState*>(l);
    SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
    if (state->before.mode_shortcut.mode == ime::ShortcutMode::Custom) state->custom = state->before.mode_shortcut;
    const auto mode = state->before.mode_shortcut.mode;
    CheckRadioButton(dialog, IDC_SHORTCUT_DEFAULT, IDC_SHORTCUT_CUSTOM, mode == ime::ShortcutMode::Default ?
        IDC_SHORTCUT_DEFAULT : mode == ime::ShortcutMode::Disabled ? IDC_SHORTCUT_OFF : IDC_SHORTCUT_CUSTOM);
    CheckDlgButton(dialog, IDC_SHORTCUT_TAB, state->before.tab_candidates ? BST_CHECKED : BST_UNCHECKED);
    const auto edit = GetDlgItem(dialog, IDC_SHORTCUT_KEYS);
    SetPropW(edit, L"Yomitsugu.ModeShortcutCapture.1", reinterpret_cast<HANDLE>(1));
    SetWindowSubclass(edit, CaptureProc, 1, 0);
    state->raw_registered = RegisterLocalKeyboard(dialog);
    Refresh(dialog, *state);
    return TRUE;
  }
  if (msg == WM_INPUT && GET_RAWINPUT_CODE_WPARAM(w) == RIM_INPUT && state &&
      GetFocus() == GetDlgItem(dialog, IDC_SHORTCUT_KEYS)) {
    RAWINPUT input{}; UINT bytes = sizeof(input);
    const auto read = GetRawInputData(reinterpret_cast<HRAWINPUT>(l), RID_INPUT, &input, &bytes, sizeof(RAWINPUTHEADER));
    if (read != UINT(-1) && read >= offsetof(RAWINPUT, data) + sizeof(RAWKEYBOARD) &&
        input.header.dwType == RIM_TYPEKEYBOARD && !(input.data.keyboard.Flags & RI_KEY_BREAK)) {
      const auto& key = input.data.keyboard;
      Capture(dialog, key.VKey, key.MakeCode, (key.Flags & RI_KEY_E0) != 0);
    }
    return FALSE; // The default window procedure releases foreground raw-input data.
  }
  if (msg == WM_DESTROY && state && state->raw_registered) {
    RAWINPUTDEVICE remove{1, 6, RIDEV_REMOVE, nullptr};
    RegisterRawInputDevices(&remove, 1, sizeof(remove));
  }
  if (msg == WM_CLOSE || (msg == WM_COMMAND && LOWORD(w) == IDCANCEL)) {
    EndDialog(dialog, IDCANCEL); return TRUE;
  }
  if (msg != WM_COMMAND || !state) return FALSE;
  switch (LOWORD(w)) {
    case IDC_SHORTCUT_DEFAULT: case IDC_SHORTCUT_OFF: case IDC_SHORTCUT_CUSTOM:
      if (HIWORD(w) != BN_CLICKED) return FALSE;
      Refresh(dialog, *state);
      if (LOWORD(w) == IDC_SHORTCUT_CUSTOM) SetFocus(GetDlgItem(dialog, IDC_SHORTCUT_KEYS));
      return TRUE;
    case IDC_SHORTCUT_RESET:
      CheckRadioButton(dialog, IDC_SHORTCUT_DEFAULT, IDC_SHORTCUT_CUSTOM, IDC_SHORTCUT_DEFAULT);
      CheckDlgButton(dialog, IDC_SHORTCUT_TAB, BST_CHECKED);
      state->custom = {ime::ShortcutMode::Custom};
      Refresh(dialog, *state); return TRUE;
    case IDOK: {
      ime::InputPreferences selected;
      selected.tab_candidates = IsDlgButtonChecked(dialog, IDC_SHORTCUT_TAB) == BST_CHECKED;
      if (ChooseCustom(dialog)) selected.mode_shortcut = state->custom;
      else if (IsDlgButtonChecked(dialog, IDC_SHORTCUT_OFF) == BST_CHECKED)
        selected.mode_shortcut.mode = ime::ShortcutMode::Disabled;
      if (!IsWindowEnabled(GetDlgItem(dialog, IDOK))) return TRUE;
      if (!ime::SaveInputPreferences(state->profile, state->before, selected)) {
        SetDlgItemTextW(dialog, IDC_SHORTCUT_HINT, L"保存できませんでした。設定ファイルを確認してから、もう一度保存してください。");
        return TRUE;
      }
      const UINT changed = RegisterWindowMessageW(ime::kInputPreferencesChanged);
      if (changed) SendNotifyMessageW(HWND_BROADCAST, changed, 0, 0);
      EndDialog(dialog, IDOK); return TRUE;
    }
  }
  return FALSE;
}
}
bool ShowInputPreferences(HWND owner, const std::filesystem::path& profile) {
  DialogState state{profile, ime::ReadInputPreferences(profile)};
  return DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_INPUT_PREFERENCES),
                         owner, DialogProc, reinterpret_cast<LPARAM>(&state)) == IDOK;
}

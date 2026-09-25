#include <windows.h>
#include <shellapi.h>
#include <filesystem>
#include <string>

namespace {
constexpr UINT kTrayId = 1;
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kOpenSettings = 101;
constexpr UINT kOpenDictionary = 102;
constexpr UINT kImportDictionary = 103;
constexpr UINT kUpdateDictionary = 104;
constexpr UINT kAbout = 105;
constexpr UINT kExit = 106;
UINT taskbar_created = 0;
HICON icon = nullptr;

std::wstring SettingsPath() {
  wchar_t path[32768]{};
  if (!GetModuleFileNameW(nullptr, path, 32768)) return {};
  return (std::filesystem::path(path).parent_path() / L"ime_settings.exe").wstring();
}
HICON MakeIcon() {
  return static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101),
                                     IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
}
void AddIcon(HWND hwnd) {
  NOTIFYICONDATAW data{}; data.cbSize = sizeof(data); data.hWnd = hwnd; data.uID = kTrayId;
  data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  data.uCallbackMessage = kTrayMessage; data.hIcon = icon;
  wcscpy_s(data.szTip, L"Yomitsugu - 設定と辞書");
  if (Shell_NotifyIconW(NIM_ADD, &data)) {
    data.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data);
  }
}
void RemoveIcon(HWND hwnd) {
  NOTIFYICONDATAW data{}; data.cbSize = sizeof(data); data.hWnd = hwnd; data.uID = kTrayId;
  Shell_NotifyIconW(NIM_DELETE, &data);
}
void Launch(const wchar_t* arguments) {
  const auto path = SettingsPath();
  ShellExecuteW(nullptr, L"open", path.c_str(), arguments, nullptr, SW_SHOWNORMAL);
}
void ShowMenu(HWND hwnd) {
  HMENU menu = CreatePopupMenu();
  if (!menu) return;
  AppendMenuW(menu, MF_STRING, kOpenSettings, L"設定と辞書の管理...");
  AppendMenuW(menu, MF_STRING, kOpenDictionary, L"ユーザー辞書を編集...");
  AppendMenuW(menu, MF_STRING, kImportDictionary, L"TSV辞書を取り込む...");
  AppendMenuW(menu, MF_STRING, kUpdateDictionary, L"公開辞書を更新...");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kAbout, L"バージョンと説明...");
  AppendMenuW(menu, MF_STRING, kExit, L"このアイコンを終了");
  POINT point{}; GetCursorPos(&point);
  SetForegroundWindow(hwnd);
  const UINT selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                        point.x, point.y, 0, hwnd, nullptr);
  DestroyMenu(menu);
  PostMessageW(hwnd, WM_NULL, 0, 0);
  switch (selected) {
    case kOpenSettings: Launch(L""); break;
    case kOpenDictionary: Launch(L"--dictionary"); break;
    case kImportDictionary: Launch(L"--import"); break;
    case kUpdateDictionary: Launch(L"--update"); break;
    case kAbout: Launch(L"--about"); break;
    case kExit: DestroyWindow(hwnd); break;
  }
}
LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == taskbar_created) { AddIcon(hwnd); return 0; }
  switch (message) {
    case WM_CREATE: return 0;
    case kTrayMessage:
      if (LOWORD(lparam) == WM_CONTEXTMENU || LOWORD(lparam) == WM_RBUTTONUP) ShowMenu(hwnd);
      else if (LOWORD(lparam) == NIN_SELECT || LOWORD(lparam) == WM_LBUTTONDBLCLK) Launch(L"");
      return 0;
    case WM_DESTROY: RemoveIcon(hwnd); PostQuitMessage(0); return 0;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}
}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  HANDLE singleton = CreateMutexW(nullptr, TRUE, L"Local\\ImeMixedTraySingleton");
  if (!singleton || GetLastError() == ERROR_ALREADY_EXISTS) {
    if (singleton) CloseHandle(singleton);
    return 0;
  }
  taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
  icon = MakeIcon();
  if (!icon) { CloseHandle(singleton); return 1; }
  WNDCLASSW cls{}; cls.lpfnWndProc = WndProc; cls.hInstance = instance;
  cls.lpszClassName = L"ImeMixedTrayWindow";
  if (!RegisterClassW(&cls)) { DestroyIcon(icon); CloseHandle(singleton); return 2; }
  HWND window = CreateWindowExW(0, cls.lpszClassName, L"Yomitsugu tray",
                               WS_OVERLAPPED, 0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
  if (!window) { DestroyIcon(icon); CloseHandle(singleton); return 3; }
  AddIcon(window);
  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  DestroyIcon(icon);
  CloseHandle(singleton);
  return static_cast<int>(msg.wParam);
}

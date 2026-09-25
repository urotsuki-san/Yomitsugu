#include "dictionary_editor.h"
#include <windows.h>
#include <shellapi.h>
#include <filesystem>

namespace {
LRESULT CALLBACK OwnerProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  int count = 0;
  auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
  if (!arguments || count != 2) { if (arguments) LocalFree(arguments); return 2; }
  const std::filesystem::path directory(arguments[1]);
  LocalFree(arguments);
  WNDCLASSW cls{}; cls.lpfnWndProc = OwnerProc; cls.hInstance = instance;
  cls.lpszClassName = L"YomitsuguDictionarySmokeOwner";
  if (!RegisterClassW(&cls)) return 3;
  HWND owner = CreateWindowW(cls.lpszClassName, L"Dictionary smoke owner", WS_OVERLAPPED,
                             0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
  if (!owner) return 4;
  ShowDictionaryEditor(owner, directory, nullptr);
  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  return 0;
}

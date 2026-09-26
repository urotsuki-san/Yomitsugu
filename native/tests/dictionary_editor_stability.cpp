#include "../src/dictionary_editor.cpp"
#include <cstdio>

namespace {
LRESULT CALLBACK FailedEditorProc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
  if (message == WM_CREATE) return -1;
  return EditorProc(hwnd, message, w, l);
}
}
int TestDictionaryEditor() {
  INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
  if (!InitCommonControlsEx(&controls)) return 1;
  WNDCLASSW cls{}; cls.hInstance = GetModuleHandleW(nullptr);
  cls.lpfnWndProc = FailedEditorProc; cls.lpszClassName = L"YomitsuguFailedEditorTest";
  if (!RegisterClassW(&cls)) return 1;
  for (int i = 0; i < 100; ++i) {
    auto state = std::make_unique<State>();
    HWND hwnd = CreateWindowW(cls.lpszClassName, L"", WS_POPUP, 0, 0, 708, 581,
                               nullptr, nullptr, cls.hInstance, &state);
    if (hwnd || state) return 1;
  }
  UnregisterClassW(cls.lpszClassName, cls.hInstance);
  std::puts("PASS 100 failed editor creations without double deletion");

  cls.lpfnWndProc = EditorProc; cls.lpszClassName = L"YomitsuguEditorResourceTest";
  if (!RegisterClassW(&cls)) return 1;
  auto run = [&]() {
    auto state = std::make_unique<State>();
    HWND hwnd = CreateWindowW(cls.lpszClassName, L"", WS_POPUP, 0, 0, 708, 581,
                               nullptr, nullptr, cls.hInstance, &state);
    if (!hwnd) return false;
    return DestroyWindow(hwnd) != FALSE;
  };
  if (!run()) return 1;
  const auto gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
  const auto user = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
  for (int i = 0; i < 200; ++i) if (!run()) return 1;
  const bool stable = gdi == GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) &&
      user == GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
  UnregisterClassW(cls.lpszClassName, cls.hInstance);
  std::printf("%s 200 editor create/destroy cycles release GDI and USER resources\n", stable ? "PASS" : "FAIL");
  return stable ? 0 : 1;
}

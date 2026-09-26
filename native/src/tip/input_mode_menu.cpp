#include "input_mode_menu.h"

#include <oleauto.h>
#include <ctffunc.h>
#include <shellapi.h>
#include <strsafe.h>

namespace {
constexpr UINT kSettings = 1;
constexpr UINT kDictionary = 2;
constexpr UINT kUpdate = 3;
constexpr UINT kAbout = 4;
constexpr UINT kAppUpdate = 5;

HRESULT Add(ITfMenu* menu, UINT id, const wchar_t* label) {
  return menu->AddMenuItem(id, 0, nullptr, nullptr, label,
                           static_cast<ULONG>(wcslen(label)), nullptr);
}

void LaunchSettings(const wchar_t* action) {
  wchar_t module[MAX_PATH]{};
  if (!GetModuleFileNameW(g_hInstance, module, MAX_PATH)) return;
  wchar_t* slash = wcsrchr(module, L'\\');
  if (!slash) return;
  StringCchCopyW(slash + 1, MAX_PATH - (slash + 1 - module), L"ime_settings.exe");
  ShellExecuteW(nullptr, L"open", module, action, nullptr, SW_SHOWNORMAL);
}
}  // namespace

InputModeMenu::InputModeMenu() { ModuleAddRef(); }
InputModeMenu::~InputModeMenu() {
  if (sink_) sink_->Release();
  ModuleRelease();
}
STDMETHODIMP InputModeMenu::QueryInterface(REFIID iid, void** out) {
  if (!out) return E_POINTER;
  *out = nullptr;
  if (iid == IID_ITfSource) *out = static_cast<ITfSource*>(this);
  else if (iid == IID_IUnknown || iid == IID_ITfLangBarItem || iid == IID_ITfLangBarItemButton)
    *out = static_cast<ITfLangBarItemButton*>(this);
  else return E_NOINTERFACE;
  AddRef();
  return S_OK;
}
STDMETHODIMP_(ULONG) InputModeMenu::AddRef() { return InterlockedIncrement(&refs_); }
STDMETHODIMP_(ULONG) InputModeMenu::Release() {
  const auto count = InterlockedDecrement(&refs_);
  if (!count) delete this;
  return count;
}
STDMETHODIMP InputModeMenu::GetInfo(TF_LANGBARITEMINFO* info) {
  if (!info) return E_POINTER;
  *info = {};
  info->clsidService = kTextServiceClsid;
  info->guidItem = GUID_LBI_INPUTMODE;
  info->dwStyle = TF_LBI_STYLE_BTN_MENU;
  StringCchCopyW(info->szDescription, TF_LBI_DESC_MAXLEN, L"Yomitsugu Preview");
  return S_OK;
}
STDMETHODIMP InputModeMenu::GetStatus(DWORD* status) {
  if (!status) return E_POINTER;
  *status = 0;
  return S_OK;
}
STDMETHODIMP InputModeMenu::Show(BOOL) { return S_OK; }
STDMETHODIMP InputModeMenu::GetTooltipString(BSTR* tooltip) {
  if (!tooltip) return E_POINTER;
  *tooltip = SysAllocString(L"Yomitsugu Preview — 設定と辞書");
  return *tooltip ? S_OK : E_OUTOFMEMORY;
}
STDMETHODIMP InputModeMenu::OnClick(TfLBIClick, POINT, const RECT*) { return S_OK; }
STDMETHODIMP InputModeMenu::InitMenu(ITfMenu* menu) {
  if (!menu) return E_POINTER;
  HRESULT hr = Add(menu, kSettings, L"設定と辞書の管理...");
  if (SUCCEEDED(hr)) hr = Add(menu, kDictionary, L"ユーザー辞書を開く...");
  if (SUCCEEDED(hr)) hr = Add(menu, kUpdate, L"公開辞書を更新...");
  if (SUCCEEDED(hr)) hr = Add(menu, kAppUpdate, L"アプリを更新...");
  if (SUCCEEDED(hr)) hr = Add(menu, kAbout, L"バージョンと説明...");
  return hr;
}
STDMETHODIMP InputModeMenu::OnMenuSelect(UINT id) {
  switch (id) {
    case kSettings: LaunchSettings(L""); break;
    case kDictionary: LaunchSettings(L"--dictionary"); break;
    case kUpdate: LaunchSettings(L"--update"); break;
    case kAppUpdate: LaunchSettings(L"--app-update"); break;
    case kAbout: LaunchSettings(L"--about"); break;
    default: return E_INVALIDARG;
  }
  return S_OK;
}
STDMETHODIMP InputModeMenu::GetIcon(HICON* icon) {
  if (!icon) return E_POINTER;
  *icon = nullptr;
  HDC screen = GetDC(nullptr);
  if (!screen) return E_FAIL;
  HDC memory = CreateCompatibleDC(screen);
  HBITMAP color = CreateCompatibleBitmap(screen, 24, 24);
  HBITMAP mask = CreateBitmap(24, 24, 1, 1, nullptr);
  if (memory && color && mask) {
    auto previous = SelectObject(memory, color);
    RECT area{0, 0, 24, 24};
    FillRect(memory, &area, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    auto font = CreateFontW(-21, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Yu Gothic UI");
    auto old_font = font ? SelectObject(memory, font) : nullptr;
    SetBkMode(memory, TRANSPARENT);
    SetTextColor(memory, RGB(20, 20, 20));
    DrawTextW(memory, L"あ", 1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (old_font) SelectObject(memory, old_font);
    if (font) DeleteObject(font);
    SelectObject(memory, previous);
    ICONINFO info{}; info.fIcon = TRUE; info.hbmColor = color; info.hbmMask = mask;
    *icon = CreateIconIndirect(&info);
  }
  if (mask) DeleteObject(mask);
  if (color) DeleteObject(color);
  if (memory) DeleteDC(memory);
  ReleaseDC(nullptr, screen);
  return *icon ? S_OK : E_FAIL;
}
STDMETHODIMP InputModeMenu::GetText(BSTR* text) {
  if (!text) return E_POINTER;
  *text = SysAllocString(L"あ");
  return *text ? S_OK : E_OUTOFMEMORY;
}
STDMETHODIMP InputModeMenu::AdviseSink(REFIID iid, IUnknown* unknown, DWORD* cookie) {
  if (!unknown || !cookie) return E_INVALIDARG;
  *cookie = TF_INVALID_COOKIE;
  if (iid != IID_ITfLangBarItemSink) return E_NOINTERFACE;
  if (sink_) return E_FAIL;
  HRESULT hr = unknown->QueryInterface(IID_ITfLangBarItemSink,
                                       reinterpret_cast<void**>(&sink_));
  if (SUCCEEDED(hr)) *cookie = 1;
  return hr;
}
STDMETHODIMP InputModeMenu::UnadviseSink(DWORD cookie) {
  if (cookie != 1 || !sink_) return E_INVALIDARG;
  sink_->Release(); sink_ = nullptr;
  return S_OK;
}

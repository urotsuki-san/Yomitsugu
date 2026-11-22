#include "tip.h"
#include <algorithm>
#include <mutex>

namespace {
constexpr wchar_t kClass[] = L"ImeMixedCandidateWindow";
std::mutex class_mutex;
UINT PreferencesMessage() {
  static const UINT message = RegisterWindowMessageW(ime::kInputPreferencesChanged);
  return message;
}
std::wstring Widen(const std::string& text) {
  int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (count <= 0) return {};
  std::wstring result(count, L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), count);
  return result;
}
}
CandidateWindow::CandidateWindow() = default;
CandidateWindow::~CandidateWindow() { Destroy(); }
bool CandidateWindow::Create(HINSTANCE instance, HWND parent) {
  std::lock_guard<std::mutex> lock(class_mutex);
  if (hwnd_) return true;
  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = WndProc; wc.hInstance = instance; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1); wc.lpszClassName = kClass;
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
  // 入力先が閉じられても、変換結果の受信は止めない。
  if (!poll_window_) {
    poll_window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClass, L"", WS_POPUP, 0, 0, 0, 0,
                                  nullptr, nullptr, instance, this);
    if (!poll_window_) return false;

  }
  hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClass, L"変換候補",
                         WS_POPUP | WS_BORDER | WS_CLIPCHILDREN, 0, 0, 0, 0, parent, nullptr, instance, this);
  if (!hwnd_) return false;
  // 標準リストを使い、候補の文字列と選択状態を支援技術へ伝える。
  list_ = CreateWindowExW(0, L"LISTBOX", L"変換候補", WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                          0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(1), instance, nullptr);
  if (!list_) { DestroyWindow(hwnd_); return false; }
  return true;
}
void CandidateWindow::SetPollInterval(UINT milliseconds) {
  if (!poll_window_ || milliseconds == poll_interval_) return;
  if (!milliseconds) KillTimer(poll_window_, 1);
  else if (!SetTimer(poll_window_, 1, milliseconds, nullptr)) return;
  poll_interval_ = milliseconds;
}
void CandidateWindow::SetOwner(HWND owner) {
  if (!hwnd_) Create(g_hInstance, owner);
  if (hwnd_) SetWindowLongPtrW(hwnd_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
}
void CandidateWindow::Destroy() {
  std::lock_guard<std::mutex> lock(class_mutex);
  if (poll_window_) { KillTimer(poll_window_, 1); DestroyWindow(poll_window_); poll_window_ = nullptr; }
  if (hwnd_) DestroyWindow(hwnd_);
  UnregisterClassW(kClass, g_hInstance); // Succeeds after the final window is gone.
  list_ = nullptr;
  if (font_) { DeleteObject(font_); font_ = nullptr; }
  font_dpi_ = 0;
  poll_interval_ = 0;
}
void CandidateWindow::Hide() {
  displayed_.clear();
  measured_width_ = 0;
  if (hwnd_ && IsWindowVisible(hwnd_)) {
    NotifyWinEvent(EVENT_OBJECT_IME_HIDE, hwnd_, OBJID_CLIENT, CHILDID_SELF);
    ShowWindow(hwnd_, SW_HIDE);
  }
}
void CandidateWindow::Show(const std::vector<ime::Candidate>& candidates, int selected, POINT pt) {
  if (candidates.empty()) { Hide(); return; }
  if (!hwnd_ && !Create(g_hInstance, nullptr)) return;
  if (!list_) { Hide(); return; }
  UINT dpi = GetDpiForWindow(GetWindow(hwnd_, GW_OWNER)); if (!dpi) dpi = 96;
  const bool new_font = !font_ || font_dpi_ != dpi;
  if (new_font) {
    NONCLIENTMETRICSW metrics{sizeof(metrics)};
    if (SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi)) {
      HFONT next = CreateFontIndirectW(&metrics.lfMessageFont);
      if (next) { SendMessageW(list_, WM_SETFONT, reinterpret_cast<WPARAM>(next), FALSE); if (font_) DeleteObject(font_); font_ = next; font_dpi_ = dpi; }
    }
  }
  const bool same = !new_font && displayed_.size()==candidates.size() &&
      std::equal(displayed_.begin(),displayed_.end(),candidates.begin(),
        [](const auto& cached,const auto& c){return cached.first==c.output_text && cached.second==c.is_raw;});
  int width = measured_width_;
  if (!same) {
  std::vector<std::wstring> labels;
  size_t bytes = 0;
  for (size_t i = 0; i < candidates.size(); ++i) {
    bytes += candidates[i].output_text.size();
    auto label = std::to_wstring(i + 1) + L"  " + Widen(candidates[i].output_text);
    if (candidates[i].is_raw) label += L"  ［入力どおり］";
    labels.push_back(std::move(label));
  }
  SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
  SendMessageW(list_, LB_RESETCONTENT, 0, 0);
  HDC dc = GetDC(list_); auto old = SelectObject(dc, font_); width = MulDiv(316, dpi, 96);
  for (const auto& label : labels) {
    SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    SIZE extent{}; GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()), &extent);
    width = (std::max)(width, static_cast<int>(extent.cx) + MulDiv(32, dpi, 96));
  }
  SelectObject(dc, old); ReleaseDC(list_, dc);
  SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
  measured_width_=width;
  displayed_.clear();
  if (bytes<=8192 && candidates.size()<=32)
    for (const auto& c:candidates) displayed_.emplace_back(c.output_text,c.is_raw);
  }
  int row = MulDiv(28, dpi, 96), footer = MulDiv(26, dpi, 96);
  SendMessageW(list_, LB_SETITEMHEIGHT, 0, row);
  MONITORINFO monitor{sizeof(monitor)};
  GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &monitor);
  auto bounds = monitor.rcWork;
  width = (std::min)(width, (std::min)(MulDiv(640, dpi, 96), static_cast<int>(bounds.right - bounds.left)));
  int list_height = row * static_cast<int>((std::min)(candidates.size(), size_t(8)));
  int height = list_height + footer + 2;
  int x = (std::clamp)(pt.x, bounds.left, (std::max)(bounds.left, bounds.right - width));
  int y = pt.y + height > bounds.bottom ? pt.y - height - MulDiv(24, dpi, 96) : pt.y;
  y = (std::max)(y, static_cast<int>(bounds.top));
  bool visible = IsWindowVisible(hwnd_) != FALSE;
  MoveWindow(list_, 0, 0, width - 2, list_height, FALSE);
  const int index = std::clamp(selected,0,static_cast<int>(candidates.size())-1);
  SendMessageW(list_, LB_SETCURSEL, index, 0);
  SetWindowPos(hwnd_, HWND_TOP, x, y, width, height, SWP_SHOWWINDOW | SWP_NOACTIVATE);
  RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
  NotifyWinEvent(visible ? EVENT_OBJECT_IME_CHANGE : EVENT_OBJECT_IME_SHOW, hwnd_, OBJID_CLIENT, CHILDID_SELF);
}
LRESULT CALLBACK CandidateWindow::WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
  auto self = reinterpret_cast<CandidateWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE) {
    self = reinterpret_cast<CandidateWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  if (self) {
    try {
      if (msg >= 0xc000 && msg == PreferencesMessage() && hwnd == self->poll_window_ && self->service_) {
        self->service_->ReloadInputPreferences();
        return 0;
      }
      if (msg == ime::kEngineReplyMessage && hwnd == self->poll_window_ && self->service_) {
        self->service_->OnEngineReply(static_cast<DWORD>(w),static_cast<DWORD>(l));
        return 0;
      }
      if (msg == WM_TIMER && hwnd == self->poll_window_ && self->service_) { self->service_->PollEngine(); return 0; }
      if (msg == WM_COMMAND && HIWORD(w) == LBN_SELCHANGE && self->service_) {
        auto index = static_cast<int>(SendMessageW(self->list_, LB_GETCURSEL, 0, 0));
        if (index >= 0) self->service_->ClickCandidate(index); return 0;
      }
      if (msg == WM_PAINT) {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps); RECT rect{}; GetClientRect(hwnd, &rect);
        FillRect(dc, &rect, GetSysColorBrush(COLOR_WINDOW));
        RECT list{}; GetWindowRect(self->list_, &list); rect.top = list.bottom - list.top;
        auto old = SelectObject(dc, self->font_); SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        const auto n=SendMessageW(self->list_,LB_GETCOUNT,0,0), idx=SendMessageW(self->list_,LB_GETCURSEL,0,0);
        const auto next_key = self->service_ && !self->service_->TabSelectsCandidates() ? L"Space" : L"Tab";
        const auto help = std::to_wstring(idx + 1) + L" / " + std::to_wstring(n) +
                          L"     " + next_key + L" 次候補    Enter 確定    Esc 戻る";
        DrawTextW(dc,help.c_str(),-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        SelectObject(dc, old); EndPaint(hwnd, &ps); return 0;
      }
      if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
      if (msg == WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        if (hwnd == self->hwnd_) {
          self->hwnd_ = nullptr; self->list_ = nullptr;
          self->displayed_.clear(); self->measured_width_=0;
          if (self->font_) { DeleteObject(self->font_); self->font_ = nullptr; }
          self->font_dpi_ = 0;
        }
        if (hwnd == self->poll_window_) self->poll_window_ = nullptr;
      }
    } catch (...) { return 0; }
  }
  return DefWindowProcW(hwnd, msg, w, l);
}

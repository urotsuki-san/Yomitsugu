#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <dwmapi.h>
#include "dictionary_editor.h"
#include "user_dictionary.h"
#include "learning_store.h"
#include "app_update.h"
#include "dictionary_status.h"
#include <memory>

namespace {
constexpr int kOpen = 101, kImport = 102, kUpdate = 103, kAbout = 104;
constexpr int kLearning = 105, kClearLearning = 106;
constexpr int kAppUpdate = 107;
constexpr UINT kUpdateDone = WM_APP + 1;
constexpr UINT kAppUpdateDone = WM_APP + 2;
struct UpdateResult {
  std::optional<ime::AppRelease> release;
  std::filesystem::path installer;
  bool failed = false, downloaded = false;
};
HWND window = nullptr, update_button = nullptr;
HWND app_update_button = nullptr;
HWND learning_checkbox = nullptr;
bool updating = false;
bool app_updating = false;
std::filesystem::path base, user_dir;
std::wstring public_status, user_status, status_note;
std::wstring learning_status;
std::wstring dictionary_updated, dictionary_checked;
ime::DictionaryStatus dictionary_status;
HFONT title_font = nullptr, section_font = nullptr, body_font = nullptr, small_font = nullptr;

COLORREF Ink() { return RGB(26, 43, 65); }
COLORREF Muted() { return RGB(105, 122, 143); }
COLORREF Accent() { return RGB(31, 110, 204); }
void Text(HDC dc, HFONT font, COLORREF color, const wchar_t* value, RECT area, UINT format) {
  auto previous = SelectObject(dc, font);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, color);
  DrawTextW(dc, value, -1, &area, format | DT_NOPREFIX);
  SelectObject(dc, previous);
}
void Panel(HDC dc, RECT area, COLORREF fill, COLORREF border, int radius) {
  auto brush = CreateSolidBrush(fill);
  auto pen = CreatePen(PS_SOLID, 1, border);
  auto old_brush = SelectObject(dc, brush);
  auto old_pen = SelectObject(dc, pen);
  RoundRect(dc, area.left, area.top, area.right, area.bottom, radius, radius);
  SelectObject(dc, old_brush);
  SelectObject(dc, old_pen);
  DeleteObject(brush);
  DeleteObject(pen);
}

std::filesystem::path OwnDirectory() {
  wchar_t path[32768]{};
  GetModuleFileNameW(nullptr, path, 32768);
  return std::filesystem::path(path).parent_path();
}
void RepaintStatus() {
  if (window) { RECT area{28, 164, 710, 294}; InvalidateRect(window, &area, FALSE); }
}
void RefreshStatus() {
  std::error_code ec;
  auto cached_public_file = user_dir / L"public_dictionary.tsv";
  auto user_file = user_dir / L"user_dictionary.tsv";
  dictionary_status=ime::ReadDictionaryStatus(base/L"engine"/L"public_dictionary.tsv",cached_public_file);
  public_status=dictionary_status.bundled?L"同梱版・最新状況は未確認":L"更新版・最新状況は未確認";
  if(dictionary_status.result=="updated" || dictionary_status.result=="unchanged") public_status=L"確認時点で最新版";
  else if(dictionary_status.result=="failed") public_status=L"最新状況を確認できません";
  dictionary_updated=L"最終更新: "+(dictionary_status.updated_at.empty()?(dictionary_status.bundled?L"同梱版":L"記録なし"):ime::DictionaryTimeLabel(dictionary_status.updated_at));
  dictionary_checked=L"最終確認: "+ime::DictionaryTimeLabel(dictionary_status.checked_at);
  user_status = std::filesystem::exists(user_file, ec) ? L"登録済み" : L"未作成";
  ime::LearningStore learning(user_dir);
  learning_status = L"学習した候補: " + std::to_wstring(learning.size()) + L"件";
  status_note = L"入力文とユーザー辞書は外部に送信しません";
  RepaintStatus();
}
bool StartHidden(const std::wstring& executable, const std::wstring& arguments, DWORD* exit_code) {
  std::wstring command = L"\"" + executable + L"\" " + arguments;
  STARTUPINFOW start{}; start.cb = sizeof(start);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, base.c_str(), &start, &process)) return false;
  CloseHandle(process.hThread);
  WaitForSingleObject(process.hProcess, INFINITE);
  const bool result = GetExitCodeProcess(process.hProcess, exit_code) != FALSE;
  CloseHandle(process.hProcess);
  return result;
}
std::wstring PowerShellPath() {
  wchar_t system[32768]{};
  const auto length = GetSystemDirectoryW(system, 32768);
  if (!length || length >= 32768) return {};
  return (std::filesystem::path(system) / L"WindowsPowerShell" / L"v1.0" / L"powershell.exe").wstring();
}
void OpenDictionary() {
  ShowDictionaryEditor(window, user_dir, RefreshStatus);
}
void ImportDictionary() {
  wchar_t path[32768]{};
  OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = window;
  dialog.lpstrFile = path; dialog.nMaxFile = 32768;
  dialog.lpstrFilter = L"UTF-8 TSV (*.tsv)\0*.tsv\0すべてのファイル (*.*)\0*.*\0";
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&dialog)) return;
  auto tool = base / L"ime_dictionary_tool.exe";
  DWORD code = 0;
  if (!StartHidden(tool.wstring(), L"--import \"" + std::wstring(path) + L"\"", &code) || code) {
    MessageBoxW(window, L"取り込みに失敗しました。UTF-8のTSV形式（読み、単語、品詞）を確認してください。元の辞書は維持されます。",
                L"Yomitsugu", MB_ICONERROR); return;
  }
  RefreshStatus();
  MessageBoxW(window, L"取り込みました。次の変換から有効です。元の辞書は .bak に保存しました。",
              L"Yomitsugu", MB_OK);
}
void UpdateDictionary() {
  if (updating) return;
  const auto launcher = PowerShellPath();
  const auto script = base / L"update_public_dictionary.ps1";
  if (launcher.empty() || !std::filesystem::exists(launcher) || !std::filesystem::exists(script)) {
    MessageBoxW(window, L"辞書更新機能が見つかりません。アプリを再インストールしてください。",
                L"Yomitsugu", MB_ICONERROR); return;
  }
  updating = true;
  EnableWindow(update_button, FALSE);
  public_status = L"公開辞書を更新中...";
  status_note = L"辞書をダウンロードしています";
  RepaintStatus();
  const std::wstring args = L"-NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + script.wstring() + L"\"";
  std::thread([launcher, args] {
    DWORD code = 0;
    if (!StartHidden(launcher, args, &code)) code = 0xffffffff;
    PostMessageW(window, kUpdateDone, static_cast<WPARAM>(code), 0);
  }).detach();
}
void CheckAppUpdate() {
  if (app_updating) return;
  app_updating = true;
  EnableWindow(app_update_button,FALSE);
  SetWindowTextW(window,L"Yomitsugu - 最新版を確認中...");
  std::thread([] {
    auto result=std::make_unique<UpdateResult>();
    try { result->release=ime::CheckAppUpdate(); } catch (...) { result->failed=true; }
    if (PostMessageW(window,kAppUpdateDone,0,reinterpret_cast<LPARAM>(result.get()))) result.release();
  }).detach();
}
void HandleAppUpdate(UpdateResult& result) {
  app_updating=false;
  EnableWindow(app_update_button,TRUE);
  SetWindowTextW(window,L"Yomitsugu - 設定と辞書");
  if (result.failed) {
    MessageBoxW(window,result.downloaded ? L"更新ファイルを取得できませんでした。接続を確認して、もう一度お試しください。" :
        L"最新版を確認できませんでした。接続を確認して、しばらくしてからお試しください。",L"Yomitsugu",MB_ICONERROR);
    return;
  }
  if (result.downloaded) {
    if (ime::LaunchAppUpdate(result.installer,*result.release)) DestroyWindow(window);
    else MessageBoxW(window,L"インストーラーを開けませんでした。もう一度更新をお試しください。",L"Yomitsugu",MB_ICONERROR);
    return;
  }
  if (!result.release) { MessageBoxW(window,L"お使いのYomitsuguは最新版です。",L"Yomitsugu",MB_OK); return; }
  const auto& tag=result.release->tag;
  const std::wstring prompt=std::wstring(tag.begin(),tag.end())+L" が公開されています。\r\nダウンロードして更新しますか？";
  if (MessageBoxW(window,prompt.c_str(),L"Yomitsugu",MB_YESNO|MB_ICONQUESTION)!=IDYES) return;
  app_updating=true;
  EnableWindow(app_update_button,FALSE);
  SetWindowTextW(window,L"Yomitsugu - 更新ファイルをダウンロード中...");
  const auto release=*result.release;
  std::thread([release] {
    auto result=std::make_unique<UpdateResult>(); result->release=release; result->downloaded=true;
    try { result->installer=ime::DownloadAppUpdate(release,user_dir/L"updates"); } catch (...) { result->failed=true; }
    if (PostMessageW(window,kAppUpdateDone,0,reinterpret_cast<LPARAM>(result.get()))) result.release();
  }).detach();
}
void About() {
  MessageBoxW(window,
    L"Yomitsugu 0.2.10-preview\r\nローマ字を打つそばから、日本語に変えるIMEです。\r\n\r\n変換はPC内で処理します。辞書はMozcとEDRDG、アプリの更新はGitHubから取得します。\r\n\r\n使い方はスタートメニューの「Yomitsugu → 使い方」、出典はインストール先の THIRD_PARTY.md を参照してください。",
    L"Yomitsugu", MB_OK);
}
void DrawAction(const DRAWITEMSTRUCT* item) {
  HDC dc = item->hDC;
  RECT area = item->rcItem;
  const bool primary = item->CtlID == kUpdate;
  const bool disabled = (item->itemState & ODS_DISABLED) != 0;
  const bool pressed = (item->itemState & ODS_SELECTED) != 0;
  const COLORREF fill = primary ? (pressed ? RGB(19, 81, 165) : Accent()) :
                                  (pressed ? RGB(234, 243, 255) : RGB(255, 255, 255));
  const COLORREF border = primary ? fill : RGB(214, 224, 236);
  Panel(dc, area, fill, border, 15);
  const wchar_t* heading = L"";
  const wchar_t* description = L"";
  const wchar_t* mark = L"";
  switch (item->CtlID) {
    case kOpen: heading=L"ユーザー辞書を編集"; description=L"登録した単語を確認・修正"; mark=L"✎"; break;
    case kImport: heading=L"TSV辞書を取り込む"; description=L"今の辞書をTSVの内容で置換"; mark=L"＋"; break;
    case kUpdate: heading=L"公開辞書を確認・更新"; description=L"最新データを取得して比較"; mark=L"↻"; break;
    case kAppUpdate: heading=L"アプリを更新"; description=L"最新版を確認してインストール"; mark=L"↓"; break;
  }
  RECT icon_area{area.left+17, area.top+19, area.left+62, area.top+64};
  Panel(dc, icon_area, primary ? RGB(55, 139, 228) : RGB(236, 245, 255),
        primary ? RGB(55, 139, 228) : RGB(236, 245, 255), 13);
  Text(dc, section_font, primary ? RGB(255,255,255) : Accent(), mark, icon_area,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  RECT title_area{area.left+75,area.top+18,area.right-20,area.top+47};
  RECT desc_area{area.left+75,area.top+48,area.right-12,area.bottom-11};
  Text(dc, section_font, disabled ? RGB(181,190,200) : (primary ? RGB(255,255,255) : Ink()),
       heading,title_area,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
  Text(dc, small_font, primary ? RGB(218,238,255) : Muted(), description,desc_area,
       DT_LEFT|DT_VCENTER|DT_SINGLELINE);
  if (item->itemState & ODS_FOCUS) {
    RECT focus = area; InflateRect(&focus,-4,-4); DrawFocusRect(dc,&focus);
  }
}
void DrawDashboard(HWND hwnd, HDC target) {
  RECT client{}; GetClientRect(hwnd,&client);
  HDC dc = CreateCompatibleDC(target);
  HBITMAP image = CreateCompatibleBitmap(target,client.right,client.bottom);
  HGDIOBJ previous = SelectObject(dc,image);
  auto page_brush = CreateSolidBrush(RGB(246,248,252));
  FillRect(dc,&client,page_brush); DeleteObject(page_brush);
  RECT header{0,0,client.right,143};
  auto header_brush = CreateSolidBrush(RGB(22,43,76));
  FillRect(dc,&header,header_brush); DeleteObject(header_brush);
  HICON logo = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101),
                                            IMAGE_ICON, 58, 58, LR_DEFAULTCOLOR));
  if (logo) { DrawIconEx(dc, 35, 30, logo, 58, 58, 0, nullptr, DI_NORMAL); DestroyIcon(logo); }
  RECT title{107,29,530,72};
  Text(dc,title_font,RGB(255,255,255),L"Yomitsugu",title,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
  RECT subtitle{108,78,610,110};
  Text(dc,body_font,RGB(201,220,238),L"ローマ字を続けて、日本語へ",subtitle,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
  RECT version{594,35,690,66};
  Text(dc,small_font,RGB(178,206,235),L"PREVIEW 0.2.10",version,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
  RECT status_card{34,169,690,288};
  Panel(dc,status_card,RGB(255,255,255),RGB(222,230,240),18);
  RECT public_label{54,178,330,198}, public_value{54,198,330,229};
  RECT user_label{365,185,660,207}, user_value{365,210,660,243};
  Text(dc,small_font,Muted(),L"公開辞書",public_label,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
  Text(dc,section_font,Ink(),public_status.c_str(),public_value,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
  Text(dc,small_font,Muted(),L"ユーザー辞書",user_label,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
  Text(dc,section_font,Ink(),user_status.c_str(),user_value,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
  RECT divider{346,187,347,242}; auto line = CreateSolidBrush(RGB(229,235,242));
  FillRect(dc,&divider,line); DeleteObject(line);
  RECT update_date{54,232,330,254}, check_date{54,256,330,278};
  Text(dc,small_font,Muted(),dictionary_updated.c_str(),update_date,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
  Text(dc,small_font,Muted(),dictionary_checked.c_str(),check_date,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
  RECT note{365,246,665,282};
  Text(dc,small_font,RGB(58,126,124),status_note.c_str(),note,DT_LEFT|DT_WORDBREAK);
  RECT section{35,302,685,335};
  Text(dc,section_font,Ink(),L"操作",section,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
  RECT learning_panel{34,544,690,650};
  Panel(dc,learning_panel,RGB(255,255,255),RGB(222,230,240),18);
  RECT learning_note{54,592,665,615}, learning_count{54,618,665,642};
  Text(dc,small_font,Muted(),L"確定した候補を、次の変換で優先します。",learning_note,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
  Text(dc,small_font,Muted(),learning_status.c_str(),learning_count,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
  RECT footer{35,662,690,689};
  Text(dc,small_font,Muted(),L"辞書と学習履歴はこのPCに保存されます",footer,
       DT_LEFT|DT_VCENTER|DT_SINGLELINE);
  BitBlt(target,0,0,client.right,client.bottom,dc,0,0,SRCCOPY);
  SelectObject(dc,previous); DeleteObject(image); DeleteDC(dc);
}
LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_CREATE: {
      title_font = CreateFontW(-31,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,
                               CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
      section_font = CreateFontW(-17,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,
                                 CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Yu Gothic UI");
      body_font = CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,
                              CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Yu Gothic UI");
      small_font = CreateFontW(-13,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,
                               CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Yu Gothic UI");
      const struct { int id; const wchar_t* text; int x,y; } buttons[] = {
        {kOpen, L"ユーザー辞書を編集", 34,342}, {kImport, L"TSV辞書を取り込む", 370,342},
        {kUpdate, L"公開辞書を確認・更新", 34,440}, {kAppUpdate, L"アプリを更新", 370,440}};
      for (const auto& button : buttons) {
        auto control = CreateWindowW(L"BUTTON", button.text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                    button.x, button.y, 320, 87, hwnd,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(button.id)), nullptr, nullptr);
        if (button.id == kUpdate) update_button = control;
        if (button.id == kAppUpdate) app_update_button = control;
      }
      learning_checkbox = CreateWindowW(L"BUTTON", L"候補の選択を学習する", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,
          54,558,340,30,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kLearning)),nullptr,nullptr);
      auto clear_learning = CreateWindowW(L"BUTTON", L"学習履歴を削除", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
          502,558,166,34,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kClearLearning)),nullptr,nullptr);
      SendMessageW(learning_checkbox,WM_SETFONT,reinterpret_cast<WPARAM>(body_font),TRUE);
      SendMessageW(clear_learning,WM_SETFONT,reinterpret_cast<WPARAM>(body_font),TRUE);
      ime::LearningStore learning(user_dir);
      SendMessageW(learning_checkbox,BM_SETCHECK,learning.enabled()?BST_CHECKED:BST_UNCHECKED,0);
      RefreshStatus();
      return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
      PAINTSTRUCT paint{}; HDC dc = BeginPaint(hwnd,&paint);
      DrawDashboard(hwnd,dc);
      EndPaint(hwnd,&paint); return 0;
    }
    case WM_DRAWITEM:
      DrawAction(reinterpret_cast<const DRAWITEMSTRUCT*>(lparam)); return TRUE;
    case WM_COMMAND:
      switch (LOWORD(wparam)) {
        case kOpen: OpenDictionary(); break;
        case kImport: ImportDictionary(); break;
        case kUpdate: UpdateDictionary(); break;
        case kAbout: About(); break;
        case kAppUpdate: CheckAppUpdate(); break;
        case kLearning: {
          ime::LearningStore learning(user_dir);
          const bool enabled = SendMessageW(learning_checkbox,BM_GETCHECK,0,0)==BST_CHECKED;
          if (!learning.SetEnabled(enabled)) {
            SendMessageW(learning_checkbox,BM_SETCHECK,learning.enabled()?BST_CHECKED:BST_UNCHECKED,0);
            MessageBoxW(hwnd,L"設定を保存できませんでした。",L"Yomitsugu",MB_ICONERROR);
          }
          break;
        }
        case kClearLearning: {
          if (MessageBoxW(hwnd,L"学習履歴を削除しますか？",L"Yomitsugu",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES) break;
          ime::LearningStore learning(user_dir);
          if (!learning.Clear()) MessageBoxW(hwnd,L"学習履歴を削除できませんでした。",L"Yomitsugu",MB_ICONERROR);
          RefreshStatus(); InvalidateRect(hwnd,nullptr,FALSE); break;
        }
      }
      return 0;
    case kAppUpdateDone: {
      std::unique_ptr<UpdateResult> result(reinterpret_cast<UpdateResult*>(lparam));
      HandleAppUpdate(*result); return 0;
    }
    case kUpdateDone:
      updating = false; EnableWindow(update_button, TRUE);
      if (wparam == 0) {
        RefreshStatus();
        MessageBoxW(hwnd, dictionary_status.result=="unchanged"?L"公開辞書に変更はありません。最新の内容を確認しました。":
                    L"公開辞書を更新しました。次の変換から有効です。", L"Yomitsugu", MB_OK);
      } else {
        RefreshStatus();
        MessageBoxW(hwnd, L"更新できませんでした。ネットワーク接続を確認してください。辞書は更新前の状態を維持します。",
                    L"Yomitsugu", MB_ICONERROR);
      }
      return 0;
    case WM_CLOSE:
      if (updating || app_updating) { MessageBoxW(hwnd, L"更新の完了までお待ちください。", L"Yomitsugu", MB_OK); return 0; }
      DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
      for (auto font : {title_font,section_font,body_font,small_font}) if (font) DeleteObject(font);
      PostQuitMessage(0); return 0;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}
}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command, int show) {
  base = OwnDirectory();
  PWSTR local = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) return 1;
  user_dir = std::filesystem::path(local) / L"ImeMixed";
  CoTaskMemFree(local);
  WNDCLASSW cls{}; cls.lpfnWndProc = WndProc; cls.hInstance = instance;
  cls.lpszClassName = L"ImeMixedSettingsWindow"; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
  cls.hbrBackground = nullptr;
  if (!RegisterClassW(&cls)) return 2;
  window = CreateWindowW(cls.lpszClassName, L"Yomitsugu - 設定と辞書", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                         CW_USEDEFAULT, CW_USEDEFAULT, 740, 745, nullptr, nullptr, instance, nullptr);
  if (!window) return 3;
  ShowWindow(window, show); UpdateWindow(window);
  if (wcscmp(command, L"--dictionary") == 0) OpenDictionary();
  else if (wcscmp(command, L"--import") == 0) ImportDictionary();
  else if (wcscmp(command, L"--update") == 0) UpdateDictionary();
  else if (wcscmp(command, L"--about") == 0) About();
  else if (wcscmp(command, L"--app-update") == 0) CheckAppUpdate();
  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  return static_cast<int>(msg.wParam);
}

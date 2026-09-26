#include "dictionary_editor.h"
#include "user_dictionary.h"
#include <commctrl.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <memory>

namespace {
constexpr int kList = 201, kReading = 202, kWord = 203, kPos = 204;
constexpr int kNew = 205, kAdd = 206, kDelete = 207, kSave = 208, kClose = 209;
struct Entry { std::wstring reading, word, pos; };
struct State {
  HWND owner{}, hwnd{}, list{}, reading{}, word{}, pos{};
  std::filesystem::path path;
  void (*on_saved)(){};
  std::vector<Entry> entries;
  std::wstring notice;
  HFONT heading_font{}, body_font{};
  bool dirty = false;
};
HWND current_editor = nullptr;

std::wstring FromUtf8(const std::string& source) {
  if (source.empty()) return {};
  int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source.data(),
                                 static_cast<int>(source.size()), nullptr, 0);
  if (!size) return {};
  std::wstring result(size, L'\0');
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source.data(),
                           static_cast<int>(source.size()), result.data(), size)) return {};
  return result;
}
std::string ToUtf8(const std::wstring& source) {
  if (source.empty()) return {};
  int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source.data(),
                                 static_cast<int>(source.size()), nullptr, 0, nullptr, nullptr);
  if (!size) return {};
  std::string result(size, '\0');
  if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source.data(),
                           static_cast<int>(source.size()), result.data(), size, nullptr, nullptr)) return {};
  return result;
}
std::wstring ControlText(HWND control) {
  const int count = GetWindowTextLengthW(control);
  std::wstring result(count + 1, L'\0');
  GetWindowTextW(control, result.data(), count + 1);
  result.resize(count);
  return result;
}
void Notice(State* state, std::wstring text) {
  state->notice = std::move(text);
  RECT area{22, 550, 690, 580};
  InvalidateRect(state->hwnd, &area, TRUE);
}
bool LoadEntries(State* state) {
  if (!std::filesystem::exists(state->path)) return true;
  ime::UserDictionary validated;
  std::string error;
  if (!validated.Load(state->path, &error)) return false;
  std::ifstream file(state->path, std::ios::binary);
  if (!file) return false;
  std::string line;
  bool first = true;
  while (std::getline(file, line)) {
    if (first) { first = false; if (line.compare(0, 3, "\xef\xbb\xbf") == 0) line.erase(0, 3); }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#' || line[0] == '!') continue;
    const auto first_tab = line.find('\t');
    const auto second_tab = line.find('\t', first_tab + 1);
    Entry entry{FromUtf8(line.substr(0, first_tab)),
                FromUtf8(line.substr(first_tab + 1, second_tab - first_tab - 1)),
                second_tab == std::string::npos ? L"名詞" :
                    FromUtf8(line.substr(second_tab + 1, line.find('\t', second_tab + 1) - second_tab - 1))};
    state->entries.push_back(std::move(entry));
  }
  return file.eof();
}
void RefreshList(State* state, int select = -1) {
  ListView_SetItemCountEx(state->list, static_cast<int>(state->entries.size()), LVSICF_NOINVALIDATEALL);
  InvalidateRect(state->list, nullptr, TRUE);
  if (select >= 0) {
    ListView_SetItemState(state->list, select, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(state->list, select, FALSE);
  }
}
void NewEntry(State* state) {
  ListView_SetItemState(state->list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
  SetWindowTextW(state->reading, L"");
  SetWindowTextW(state->word, L"");
  SetWindowTextW(state->pos, L"名詞");
  SetFocus(state->reading);
}
bool ValidField(const std::wstring& field, size_t maximum_bytes) {
  if (field.empty() || field.find_first_of(L"\t\r\n") != std::wstring::npos) return false;
  const auto bytes = ToUtf8(field);
  return !bytes.empty() && bytes.size() <= maximum_bytes;
}
void AddEntry(State* state) {
  Entry entry{ControlText(state->reading), ControlText(state->word), ControlText(state->pos)};
  if (!ValidField(entry.reading, 512) || !ValidField(entry.word, 1024) ||
      !ValidField(entry.pos, 256)) {
    Notice(state, L"読み・単語・品詞を入力してください。改行とタブは使用できません。"); return;
  }
  int selected = ListView_GetNextItem(state->list, -1, LVNI_SELECTED);
  int same_reading = 0;
  for (size_t index = 0; index < state->entries.size(); ++index) {
    if (state->entries[index].reading == entry.reading) ++same_reading;
    if (static_cast<int>(index) != selected && state->entries[index].reading == entry.reading &&
        state->entries[index].word == entry.word) {
      Notice(state, L"同じ読みと単語は登録済みです。"); return;
    }
  }
  if (same_reading >= 32 && (selected < 0 || state->entries[selected].reading != entry.reading)) {
    Notice(state, L"同じ読みに登録できる候補は32件までです。"); return;
  }
  if (selected < 0 && state->entries.size() >= 100000) {
    Notice(state, L"登録件数の上限に達しました。"); return;
  }
  if (selected >= 0) state->entries[selected] = std::move(entry);
  else { state->entries.push_back(std::move(entry)); selected = static_cast<int>(state->entries.size() - 1); }
  state->dirty = true;
  RefreshList(state, selected);
  Notice(state, L"変更があります。「保存」を押すと次の変換から有効です。");
}
void DeleteEntry(State* state) {
  const int selected = ListView_GetNextItem(state->list, -1, LVNI_SELECTED);
  if (selected < 0) { Notice(state, L"削除する単語を一覧から選択してください。"); return; }
  state->entries.erase(state->entries.begin() + selected);
  state->dirty = true;
  RefreshList(state);
  NewEntry(state);
  Notice(state, L"選択した単語を削除しました。保存すると反映されます。");
}
bool SaveEntries(State* state) {
  std::error_code ec;
  std::filesystem::create_directories(state->path.parent_path(), ec);
  if (ec) { Notice(state, L"辞書フォルダーを作成できません。"); return false; }
  auto temp = state->path;
  temp += L"." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64()) + L".tmp";
  std::ofstream file(temp, std::ios::binary);
  if (!file) { Notice(state, L"辞書ファイルを書き込めません。"); return false; }
  file << u8"# Yomitsugu user dictionary: reading<TAB>word<TAB>part-of-speech\n";
  for (const auto& entry : state->entries) {
    file << ToUtf8(entry.reading) << '\t' << ToUtf8(entry.word) << '\t' << ToUtf8(entry.pos) << '\n';
  }
  file.close();
  ime::UserDictionary validated;
  std::string error;
  if (!file || !validated.Load(temp, &error)) {
    std::filesystem::remove(temp, ec);
    Notice(state, L"辞書の形式またはサイズが上限を超えています。変更は保存されていません。");
    return false;
  }
  if (std::filesystem::exists(state->path)) {
    auto backup = state->path; backup += L".bak";
    std::filesystem::copy_file(state->path, backup, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) { std::filesystem::remove(temp, ec); Notice(state, L"バックアップを作成できません。"); return false; }
  }
  if (!MoveFileExW(temp.c_str(), state->path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    std::filesystem::remove(temp, ec);
    Notice(state, L"辞書を保存できません。既存の内容は維持されました。"); return false;
  }
  state->dirty = false;
  Notice(state, L"保存しました。次の変換から有効です。");
  if (state->on_saved) state->on_saved();
  return true;
}
void DrawEditor(State* state, HDC dc) {
  RECT client{}; GetClientRect(state->hwnd, &client);
  auto background = CreateSolidBrush(RGB(248, 250, 253));
  FillRect(dc, &client, background); DeleteObject(background);
  RECT header{0, 0, client.right, 86};
  auto navy = CreateSolidBrush(RGB(38, 41, 68));
  FillRect(dc, &header, navy); DeleteObject(navy);
  auto prior = SelectObject(dc, state->heading_font);
  SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255));
  RECT title{24, 14, 680, 49};
  DrawTextW(dc, L"ユーザー辞書", -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  SelectObject(dc, state->body_font);
  SetTextColor(dc, RGB(199, 215, 237));
  RECT description{24, 48, 680, 73};
  DrawTextW(dc, L"よみと単語を登録すると、次の変換から候補に表示します", -1,
            &description, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  SetTextColor(dc, RGB(52, 71, 99));
  RECT reading{24, 419, 208, 446}, word{224, 419, 473, 446}, pos{488, 419, 680, 446};
  DrawTextW(dc, L"読み", -1, &reading, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  DrawTextW(dc, L"単語", -1, &word, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  DrawTextW(dc, L"品詞", -1, &pos, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  SetTextColor(dc, RGB(38, 111, 164));
  RECT notice{24, 550, 680, 577};
  DrawTextW(dc, state->notice.c_str(), -1, &notice, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  SelectObject(dc, prior);
}
LRESULT CALLBACK EditorProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  auto state = reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    auto create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    state = static_cast<std::unique_ptr<State>*>(create->lpCreateParams)->release();
    state->hwnd = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    return DefWindowProcW(hwnd, message, wparam, lparam);
  }
  if (!state) return DefWindowProcW(hwnd, message, wparam, lparam);
  switch (message) {
    case WM_CREATE: {
      state->heading_font = CreateFontW(-24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Yu Gothic UI");
      state->body_font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Yu Gothic UI");
      state->list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_OWNERDATA | LVS_SHOWSELALWAYS,
          24, 101, 660, 305, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kList)), nullptr, nullptr);
      ListView_SetExtendedListViewStyle(state->list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);
      for (int index = 0; index < 3; ++index) {
        LVCOLUMNW column{}; column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.pszText = const_cast<LPWSTR>(index == 0 ? L"読み" : index == 1 ? L"単語" : L"品詞");
        column.cx = index == 0 ? 185 : index == 1 ? 290 : 160;
        ListView_InsertColumn(state->list, index, &column);
      }
      state->reading = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                       24, 449, 184, 32, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kReading)), nullptr, nullptr);
      state->word = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                    224, 449, 249, 32, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kWord)), nullptr, nullptr);
      state->pos = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWN | CBS_AUTOHSCROLL,
                                   488, 449, 196, 200, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPos)), nullptr, nullptr);
      for (auto value : {L"名詞", L"固有名詞", L"人名", L"地名", L"組織", L"サ変名詞", L"形容詞", L"副詞", L"記号"})
        SendMessageW(state->pos, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
      SetWindowTextW(state->pos, L"名詞");
      const struct { int id; const wchar_t* text; int x, width; } buttons[] = {
        {kNew, L"新規", 24, 78}, {kAdd, L"登録・更新", 110, 132},
        {kDelete, L"選択を削除", 250, 126}, {kSave, L"保存", 485, 95}, {kClose, L"閉じる", 588, 96}};
      for (const auto& button : buttons) {
        HWND control = CreateWindowW(L"BUTTON", button.text, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                     button.x, 497, button.width, 36, hwnd,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(button.id)), nullptr, nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state->body_font), TRUE);
      }
      for (HWND control : {state->list, state->reading, state->word, state->pos})
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state->body_font), TRUE);
      SendMessageW(state->reading, EM_LIMITTEXT, 256, 0);
      SendMessageW(state->word, EM_LIMITTEXT, 512, 0);
      RefreshList(state);
      Notice(state, state->entries.empty() ? L"読みと単語を入力して「登録・更新」を押してください。" :
                                           L"一覧から選ぶと登録内容を編集できます。");
      return 0;
    }
    case WM_NOTIFY: {
      auto notice = reinterpret_cast<NMHDR*>(lparam);
      if (notice->idFrom != kList) break;
      if (notice->code == LVN_GETDISPINFOW) {
        auto info = reinterpret_cast<NMLVDISPINFOW*>(lparam);
        if (info->item.iItem < 0 || static_cast<size_t>(info->item.iItem) >= state->entries.size()) return 0;
        auto& entry = state->entries[info->item.iItem];
        const auto& value = info->item.iSubItem == 0 ? entry.reading : info->item.iSubItem == 1 ? entry.word : entry.pos;
        info->item.pszText = const_cast<LPWSTR>(value.c_str());
        return 0;
      }
      if (notice->code == LVN_ITEMCHANGED) {
        auto info = reinterpret_cast<NMLISTVIEW*>(lparam);
        if ((info->uNewState & LVIS_SELECTED) && !(info->uOldState & LVIS_SELECTED) &&
            info->iItem >= 0 && static_cast<size_t>(info->iItem) < state->entries.size()) {
          const auto& entry = state->entries[info->iItem];
          SetWindowTextW(state->reading, entry.reading.c_str());
          SetWindowTextW(state->word, entry.word.c_str());
          SetWindowTextW(state->pos, entry.pos.c_str());
        }
      }
      return 0;
    }
    case WM_COMMAND:
      switch (LOWORD(wparam)) {
        case kNew: NewEntry(state); break;
        case kAdd: AddEntry(state); break;
        case kDelete: DeleteEntry(state); break;
        case kSave: SaveEntries(state); break;
        case kClose: SendMessageW(hwnd, WM_CLOSE, 0, 0); break;
      }
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT paint{};
      HDC dc = BeginPaint(hwnd, &paint); DrawEditor(state, dc); EndPaint(hwnd, &paint); return 0;
    }
    case WM_CLOSE:
      if (state->dirty) {
        const auto answer = MessageBoxW(hwnd, L"変更を保存しますか？", L"Yomitsugu",
                                        MB_YESNOCANCEL | MB_ICONQUESTION);
        if (answer == IDCANCEL) return 0;
        if (answer == IDYES && !SaveEntries(state)) return 0;
      }
      DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
      EnableWindow(state->owner, TRUE); SetForegroundWindow(state->owner);
      current_editor = nullptr;
      if (state->heading_font) DeleteObject(state->heading_font);
      if (state->body_font) DeleteObject(state->body_font);
      return 0;
    case WM_NCDESTROY:
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
      delete state; return 0;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}
}  // namespace

void ShowDictionaryEditor(HWND owner, const std::filesystem::path& user_directory,
                          void (*on_saved)()) {
  if (current_editor && IsWindow(current_editor)) { SetForegroundWindow(current_editor); return; }
  INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
  InitCommonControlsEx(&controls);
  auto state = std::make_unique<State>();
  state->owner = owner;
  state->path = user_directory / L"user_dictionary.tsv";
  state->on_saved = on_saved;
  if (!LoadEntries(state.get())) {
    MessageBoxW(owner, L"既存の辞書を読み込めません。ファイルの形式を確認してください。内容は変更していません。",
                L"Yomitsugu", MB_ICONERROR);
    return;
  }
  HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSW cls{}; cls.lpfnWndProc = EditorProc; cls.hInstance = instance;
  cls.lpszClassName = L"YomitsuguDictionaryEditor";
  cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
  RegisterClassW(&cls);
  HWND editor = CreateWindowExW(0, cls.lpszClassName, L"Yomitsugu - ユーザー辞書",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, 720, 620, owner, nullptr, instance, &state);
  if (!editor) return;
  current_editor = editor;
  EnableWindow(owner, FALSE);
  ShowWindow(editor, SW_SHOW); UpdateWindow(editor);
}

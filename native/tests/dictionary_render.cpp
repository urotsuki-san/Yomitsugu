// 記事用の登録例を、画面に表示せず実際の辞書編集UIで描く。
#include "../src/dictionary_editor.cpp"
#include <iostream>

int wmain(int argc, wchar_t** argv) {
  if (argc != 2) return 2;
  INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
  InitCommonControlsEx(&controls);
  WNDCLASSW cls{};
  cls.lpfnWndProc = EditorProc; cls.hInstance = GetModuleHandleW(nullptr);
  cls.lpszClassName = L"YomitsuguHiddenDictionaryRender";
  if (!RegisterClassW(&cls)) return 3;
  auto owned_state = std::make_unique<State>();
  auto state = owned_state.get();
  state->entries = {{L"よみつぐ", L"Yomitsugu", L"固有名詞"},
                    {L"かいはつめも", L"開発メモ", L"名詞"},
                    {L"れびゅー", L"コードレビュー", L"名詞"}};
  // 非表示の独立ウィンドウを使い、ユーザーの辞書と入力先には触れない。
  HWND hwnd = CreateWindowW(cls.lpszClassName,L"",WS_POPUP,0,0,708,581,
                            nullptr,nullptr,cls.hInstance,&owned_state);
  if (!hwnd) return 4;
  RefreshList(state, 0);
  SetWindowTextW(state->reading,L"よみつぐ");
  SetWindowTextW(state->word,L"Yomitsugu");
  SetWindowTextW(state->pos,L"固有名詞");
  HDC dc = CreateCompatibleDC(nullptr);
  BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = 708; info.bmiHeader.biHeight = -581;
  info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
  void* pixels = nullptr;
  HBITMAP bitmap = CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
  if (!bitmap) { DeleteDC(dc); DestroyWindow(hwnd); return 5; }
  auto previous = SelectObject(dc,bitmap);
  DrawEditor(state,dc);
  for (int id : {kList,kReading,kWord,kPos,kNew,kAdd,kDelete,kSave,kClose}) {
    HWND child = GetDlgItem(hwnd,id); RECT rect{};
    GetWindowRect(child,&rect);
    MapWindowPoints(nullptr,hwnd,reinterpret_cast<POINT*>(&rect),2);
    SetViewportOrgEx(dc,rect.left,rect.top,nullptr);
    SendMessageW(child,WM_PRINT,reinterpret_cast<WPARAM>(dc),
                 PRF_CLIENT|PRF_NONCLIENT|PRF_ERASEBKGND|PRF_CHILDREN);
    if (id == kReading || id == kWord || id == kPos) {
      // 非表示のEDITはWM_PRINTで描画しないため、同じ値・フォントで補う。
      RECT field{0,0,rect.right-rect.left,32};
      if (id == kPos) { field.left=2; field.top=2; field.right-=20; field.bottom-=2; }
      FillRect(dc,&field,reinterpret_cast<HBRUSH>(COLOR_WINDOW+1));
      if (id != kPos) DrawEdge(dc,&field,EDGE_SUNKEN,BF_RECT);
      field.left+=4;
      SelectObject(dc,state->body_font);
      SetTextColor(dc,GetSysColor(COLOR_WINDOWTEXT)); SetBkMode(dc,TRANSPARENT);
      const auto text=ControlText(child);
      DrawTextW(dc,text.c_str(),-1,&field,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    }
  }
  GdiFlush();
  BITMAPFILEHEADER header{}; header.bfType = 0x4d42;
  header.bfOffBits = sizeof(header)+sizeof(BITMAPINFOHEADER);
  header.bfSize = header.bfOffBits+708*581*4;
  std::ofstream file(std::filesystem::path(argv[1]),std::ios::binary);
  file.write(reinterpret_cast<char*>(&header),sizeof(header));
  file.write(reinterpret_cast<char*>(&info.bmiHeader),sizeof(BITMAPINFOHEADER));
  file.write(static_cast<char*>(pixels),708*581*4); file.close();
  SelectObject(dc,previous); DeleteObject(bitmap); DeleteDC(dc);
  DestroyWindow(hwnd);
  std::cout << "Rendered dictionary editor without showing a window\n";
  return file.fail()?6:0;
}

// 設定画面を表示せず、実際の描画処理を画像へ保存する。
#include "../src/settings.cpp"
#include <iostream>

int wmain(int argc, wchar_t** argv) {
  if (argc != 3) return 2;
  const std::filesystem::path output = argv[1];
  const std::wstring mode = argv[2];
  if (mode != L"updated" && mode != L"unknown" && mode != L"failed") return 2;
  auto fixture = std::filesystem::temp_directory_path() / (L"yomitsugu_render_" + std::to_wstring(GetCurrentProcessId()));
  base = fixture; user_dir = fixture / L"profile";
  std::filesystem::create_directories(base / L"engine");
  std::filesystem::create_directory(user_dir);
  { std::ofstream file(user_dir / L"user_dictionary.tsv");
    file << u8"よみつぐ\tYomitsugu\t固有名詞\nかいはつめも\t開発メモ\t名詞\nれびゅー\tコードレビュー\t名詞\n"; }
  { std::ofstream file(base / L"engine/public_dictionary.tsv"); file << "fixture"; }
  { std::ofstream file(base / L"engine/public_dictionary.sources.json"); file << R"({"format_version":2,"entries":299521})"; }
  if (mode != L"unknown") {
    { std::ofstream file(user_dir / L"public_dictionary.tsv"); file << "fixture"; }
    { std::ofstream file(user_dir / L"public_dictionary.sources.json"); file << R"({"format_version":2,"entries":299522,"update_result":"unchanged","updated_at_utc":"2026-09-25T02:00:00Z","checked_at_utc":"2026-09-26T01:29:59Z"})"; }
  }
  if (mode == L"failed") {
    std::ofstream file(user_dir / L"public_dictionary.status.json");
    file << R"({"format_version":1,"result":"failed","checked_at_utc":"2026-09-26T02:00:00Z"})";
  }
  ime::LearningStore fixture_settings(user_dir);
  if (fixture_settings.model()!=ime::ConversionModel::Automatic) return 6;
  const auto originalDesktop = GetThreadDesktop(GetCurrentThreadId());
  const auto desktop = CreateDesktopW((L"YomitsuguSettingsTest-" + std::to_wstring(GetCurrentProcessId())).c_str(),
                                      nullptr, nullptr, 0, GENERIC_ALL, nullptr);
  if (!desktop || !SetThreadDesktop(desktop)) return 3;
  WNDCLASSW cls{}; cls.lpfnWndProc = WndProc; cls.hInstance = GetModuleHandleW(nullptr);
  cls.lpszClassName = L"YomitsuguHiddenRender";
  if (!RegisterClassW(&cls)) return 3;
  // Render native controls on a separate desktop without switching the input desktop.
  window = CreateWindowW(cls.lpszClassName,L"",WS_POPUP|WS_VISIBLE,0,0,656,560,nullptr,nullptr,cls.hInstance,nullptr);
  if (!window || SendMessageW(model_combo,CB_GETCURSEL,0,0)!=0) return 4;
  RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
  if (GetNextDlgTabItem(window,model_combo,FALSE)!=GetDlgItem(window,kInputKeys) ||
      GetNextDlgTabItem(window,GetDlgItem(window,kInputKeys),FALSE)!=GetDlgItem(window,kOpen) ||
      GetNextDlgTabItem(window,GetDlgItem(window,kInputKeys),TRUE)!=model_combo) return 7;
  for (int i=0;i<static_cast<int>(std::size(models));++i) {
    SendMessageW(model_combo,CB_SETCURSEL,i,0);
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(kModel,CBN_SELCHANGE),reinterpret_cast<LPARAM>(model_combo));
    wchar_t detail[256]{}; GetWindowTextW(model_detail,detail,256);
    if (fixture_settings.model()!=models[i].id || std::wstring(detail)!=models[i].detail) return 8;
    RECT bounds{}; GetClientRect(model_detail,&bounds);
    RECT measured=bounds;
    HDC label=GetDC(model_detail);
    const auto previous=SelectObject(label,body_font);
    DrawTextW(label,detail,-1,&measured,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
    SelectObject(label,previous); ReleaseDC(model_detail,label);
    if (measured.bottom>bounds.bottom || measured.right>bounds.right) return 9;
  }
  SendMessageW(model_combo,CB_SETCURSEL,0,0);
  SendMessageW(window,WM_COMMAND,MAKEWPARAM(kModel,CBN_SELCHANGE),reinterpret_cast<LPARAM>(model_combo));
  HDC dc = CreateCompatibleDC(nullptr);
  BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth=656; info.bmiHeader.biHeight=-560;
  info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
  void* pixels = nullptr;
  HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
  auto previous=SelectObject(dc,bitmap);
  DrawDashboard(window,dc);
  for (int id : {kOpen,kImport,kUpdate,kAppUpdate}) {
    HWND child=GetDlgItem(window,id); RECT rect{}; GetWindowRect(child,&rect);
    MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&rect),2);
    SetViewportOrgEx(dc,rect.left,rect.top,nullptr);
    DRAWITEMSTRUCT item{}; item.CtlID=id; item.hwndItem=child; item.hDC=dc;
    item.rcItem={0,0,rect.right-rect.left,rect.bottom-rect.top}; DrawAction(&item);
  }
  SetViewportOrgEx(dc,0,0,nullptr);
  for (HWND child : {model_label, model_combo, model_detail, GetDlgItem(window,kInputKeys), learning_checkbox, GetDlgItem(window,kClearLearning)}) {
    RECT rect{}; GetWindowRect(child,&rect);
    MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&rect),2);
    SetViewportOrgEx(dc,rect.left,rect.top,nullptr);
    SendMessageW(child,WM_PRINT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_CHILDREN|PRF_NONCLIENT|PRF_ERASEBKGND);
  }
  GdiFlush();
  BITMAPFILEHEADER header{}; header.bfType=0x4d42;
  header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);
  header.bfSize=header.bfOffBits+656*560*4;
  std::ofstream file(output,std::ios::binary);
  file.write(reinterpret_cast<char*>(&header),sizeof(header));
  file.write(reinterpret_cast<char*>(&info.bmiHeader),sizeof(BITMAPINFOHEADER));
  file.write(static_cast<char*>(pixels),656*560*4); file.close();
  SelectObject(dc,previous); DeleteObject(bitmap); DeleteDC(dc); DestroyWindow(window);
  SetThreadDesktop(originalDesktop); CloseDesktop(desktop);
  for(const auto& entry:std::filesystem::recursive_directory_iterator(fixture))
    if(entry.is_regular_file()) std::filesystem::remove(entry.path());
  std::filesystem::remove(fixture/L"profile"); std::filesystem::remove(fixture/L"engine"); std::filesystem::remove(fixture);
  std::cout << "Rendered settings without showing a window\n";
  return file.fail()?5:0;
}

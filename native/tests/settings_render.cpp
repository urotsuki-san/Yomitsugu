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
  WNDCLASSW cls{}; cls.lpfnWndProc = WndProc; cls.hInstance = GetModuleHandleW(nullptr);
  cls.lpszClassName = L"YomitsuguHiddenRender";
  if (!RegisterClassW(&cls)) return 3;
  // WS_VISIBLEとShowWindowを使わず、デスクトップの入力先を変えない。
  window = CreateWindowW(cls.lpszClassName,L"",WS_POPUP,0,0,724,706,nullptr,nullptr,cls.hInstance,nullptr);
  if (!window) return 4;
  HDC dc = CreateCompatibleDC(nullptr);
  BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth=724; info.bmiHeader.biHeight=-706;
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
  for (int id : {kLearning,kClearLearning}) {
    HWND child=GetDlgItem(window,id); RECT rect{}; GetWindowRect(child,&rect);
    MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&rect),2);
    SetViewportOrgEx(dc,rect.left,rect.top,nullptr);
    SendMessageW(child,WM_PRINT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_ERASEBKGND);
  }
  GdiFlush();
  BITMAPFILEHEADER header{}; header.bfType=0x4d42;
  header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);
  header.bfSize=header.bfOffBits+724*706*4;
  std::ofstream file(output,std::ios::binary);
  file.write(reinterpret_cast<char*>(&header),sizeof(header));
  file.write(reinterpret_cast<char*>(&info.bmiHeader),sizeof(BITMAPINFOHEADER));
  file.write(static_cast<char*>(pixels),724*706*4); file.close();
  SelectObject(dc,previous); DeleteObject(bitmap); DeleteDC(dc); DestroyWindow(window);
  for(const auto& entry:std::filesystem::recursive_directory_iterator(fixture))
    if(entry.is_regular_file()) std::filesystem::remove(entry.path());
  std::filesystem::remove(fixture/L"profile"); std::filesystem::remove(fixture/L"engine"); std::filesystem::remove(fixture);
  std::cout << "Rendered settings without showing a window\n";
  return file.fail()?5:0;
}

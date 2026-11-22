#include "tip.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "nlohmann/json.hpp"

HINSTANCE g_hInstance;
void TextService::ClickCandidate(int) {}
void TextService::PollEngine() {}
void TextService::ReloadInputPreferences() {}
using Clock = std::chrono::steady_clock;
using Json = nlohmann::json;
double Milliseconds(Clock::time_point start) {
  return std::chrono::duration<double,std::milli>(Clock::now()-start).count();
}
std::wstring Wide(const std::string& utf8) {
  auto n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.data(),int(utf8.size()),nullptr,0);
  std::wstring w(n,L' ');
  if(n) MultiByteToWideChar(CP_UTF8,0,utf8.data(),int(utf8.size()),w.data(),n);
  return w;
}
void Paint(HWND edit,const std::string& value) {
  const auto text=Wide(value);
  SetWindowTextW(edit,text.c_str());
  RedrawWindow(edit,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW);
  GdiFlush();
}
void SaveBitmap(HWND hwnd,const wchar_t* path) {
  RECT r{};GetWindowRect(hwnd,&r);
  const int w=r.right-r.left,h=r.bottom-r.top;
  BITMAPINFO info{};auto& bi=info.bmiHeader;
  bi.biSize=sizeof(bi);bi.biWidth=w;bi.biHeight=-h;bi.biPlanes=1;bi.biBitCount=32;
  void* pixels=nullptr;
  auto dc=CreateCompatibleDC(nullptr);
  auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
  auto old=SelectObject(dc,bitmap);
  PrintWindow(hwnd,dc,2);GdiFlush();
  BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(bi);header.bfSize=header.bfOffBits+w*h*4;
  std::ofstream file(path,std::ios::binary);
  file.write(reinterpret_cast<char*>(&header),sizeof(header));file.write(reinterpret_cast<char*>(&bi),sizeof(bi));
  file.write(static_cast<char*>(pixels),w*h*4);
  SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
}
bool Reply(ime::EngineChannel& engine,ime::Session& session) {
  auto req=session.decode_input();
  req.dictionary_preview=req.phase=="typing";
  engine.Submit(req);
  ime::DecodeInput received;
  std::vector<ime::Candidate> rows;
  const auto start=GetTickCount64();
  while(GetTickCount64()-start<30000) {
    if(engine.Poll(&received,&rows))return session.ApplyCandidates(received,std::move(rows));
    Sleep(1);
  }
  return false;
}
int wmain(int argc,wchar_t** argv) {
  if(argc!=5)return 2;
  g_hInstance=GetModuleHandleW(nullptr);
  auto desktop=CreateDesktopW((L"YomitsuguPaint-"+std::to_wstring(GetCurrentProcessId())).c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);
  if(!desktop||!SetThreadDesktop(desktop))return 3;
  auto owner=CreateWindowW(L"STATIC",L"Yomitsugu test",WS_POPUP|WS_VISIBLE,0,0,700,100,nullptr,nullptr,g_hInstance,nullptr);
  auto edit=CreateWindowW(L"EDIT",L"",WS_VISIBLE|WS_CHILD|WS_BORDER,8,8,660,60,owner,nullptr,g_hInstance,nullptr);
  if(!owner||!edit)return 4;
  CandidateWindow popup;
  if(!popup.Create(g_hInstance,owner))return 5;
  std::vector<ime::Candidate> demo;
  for(auto s:{u8"会いました",u8"合いました",u8"遭いました",u8"逢いました",u8"あいました",u8"あいマシタ",u8"アイマシタ",u8"相ました",u8"遇いました","aimasita"}) {
    ime::Candidate c; c.output_text=s;c.is_raw=demo.size()==9;demo.push_back(c);
  }
  popup.Show(demo,0,{40,70});popup.Show(demo,8,{40,70});
  SaveBitmap(popup.hwnd(),argv[4]);
  auto list=FindWindowExW(popup.hwnd(),nullptr,L"LISTBOX",nullptr);
  auto idx=SendMessageW(list,LB_GETCURSEL,0,0), top=SendMessageW(list,LB_GETTOPINDEX,0,0);
  if(idx!=8||idx<top||idx>=top+8)return 6;
  popup.Hide();
  ime::EngineChannel engine;
  if(!engine.Start(argv[1],argv[2]))return 7;
  std::vector<std::string> phrases={"nihongowobenkyousuru","kyouhaiitenkidesune","hasiwowataru","toshokandehonwoyomu",
    "kinoutakahasisantoaimasita","kikannnitsuite","watasiwanihonjinndesu","sakuragasaiteiru","toukyouniikimasu"};
  Json result=Json::array();
  for (const auto& keys:phrases) {
    ime::Session session; session.set_deferred_decoding(true);
    std::string last;
    for(char key:keys) {
      auto start=Clock::now();
      session.Type(std::string(1,key));
      const auto immediate=session.visible_text();Paint(edit,immediate);
      const auto first=Milliseconds(start);
      if(!Reply(engine,session))return 8;
      const auto settled=session.visible_text();Paint(edit,settled);
      result.push_back({{"key_ms",first},{"settled_ms",Milliseconds(start)},
         {"replaced_prefix",immediate.compare(0,last.size(),last)!=0},{"settled_replaced_prefix",settled.compare(0,last.size(),last)!=0},
         {"phrase",keys},{"raw",session.raw_text()},{"visible",immediate},{"settled",settled}});
      last=settled;
    }
    session.RefineCandidates();
    if(!Reply(engine,session))return 9;
    result.push_back({{"phrase",keys},{"commit",session.visible_text()}});
  }
  std::ofstream(argv[3])<<result.dump();
  engine.Stop();popup.Destroy();DestroyWindow(owner);return 0;
}

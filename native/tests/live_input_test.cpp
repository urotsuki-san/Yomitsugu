#include <algorithm>
#include "tip.h"
#include "fake_context.h"
#include "fake_thread_manager.h"
#include "nlohmann/json.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <psapi.h>
#include <tlhelp32.h>

HINSTANCE g_hInstance=nullptr;
LONG g_moduleRefCount=0;
void ModuleAddRef() { InterlockedIncrement(&g_moduleRefCount); }
void ModuleRelease() { InterlockedDecrement(&g_moduleRefCount); }
using Clock=std::chrono::steady_clock;
using Json=nlohmann::json;
static double Ms(Clock::time_point start) { return std::chrono::duration<double,std::milli>(Clock::now()-start).count(); }

static HANDLE OpenWorker(const wchar_t* executable) {
  const auto name=std::filesystem::path(executable).filename().wstring();
  HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
  if(snapshot==INVALID_HANDLE_VALUE)return nullptr;
  PROCESSENTRY32W entry{};
  entry.dwSize=sizeof(entry);
  HANDLE worker=nullptr;
  if(Process32FirstW(snapshot,&entry))do {
    if(entry.th32ParentProcessID==GetCurrentProcessId() && _wcsicmp(entry.szExeFile,name.c_str())==0) {
      worker=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,entry.th32ProcessID);
      if(worker)break;
    }
  } while(Process32NextW(snapshot,&entry));
  CloseHandle(snapshot);
  return worker;
}

static Json Resources(HANDLE worker) {
  FILETIME created{},exited{},kernel{},user{};
  PROCESS_MEMORY_COUNTERS mem{};
  mem.cb=sizeof(mem);
  if(!GetProcessTimes(worker,&created,&exited,&kernel,&user) ||
      !GetProcessMemoryInfo(worker,&mem,sizeof(mem)))return nullptr;
  const auto ticks=[](FILETIME t){return (static_cast<uint64_t>(t.dwHighDateTime)<<32) | t.dwLowDateTime;};
  return {{"cpu_ms",(ticks(kernel)+ticks(user))/10000.0},{"peak_ws",mem.PeakWorkingSetSize},
          {"peak_private",mem.PeakPagefileUsage}};
}

// A small TSF document adapter that writes the composition into a Win32 EDIT.
// The TIP, its WM_TIMER scheduling and the separate conversion worker are real.
struct EditRange final : ITfRange {
  HWND edit=nullptr;
  HWND keys=nullptr;
  std::filesystem::path frames;
  ULONG refs=1;
  Clock::time_point start;
  std::string raw;
  Json events=Json::array();
  void Capture() {
    HWND owner=GetParent(edit);
    RECT r{};GetClientRect(owner,&r);
    HDC screen=GetDC(owner), dc=CreateCompatibleDC(screen);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=r.right;info.bmiHeader.biHeight=-r.bottom;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* pixels=nullptr;
    auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(bitmap) {
      auto previous=SelectObject(dc,bitmap);
      PrintWindow(owner,dc,2);
      GdiFlush();
      const DWORD length=r.right*r.bottom*4;
      BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(info.bmiHeader);
      file.bfSize=file.bfOffBits+length;
      std::ofstream stream(frames/(std::to_wstring(events.size())+L".bmp"),std::ios::binary);
      stream.write(reinterpret_cast<const char*>(&file),sizeof(file));
      stream.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(info.bmiHeader));
      stream.write(static_cast<const char*>(pixels),length);
      SelectObject(dc,previous);DeleteObject(bitmap);
    }
    DeleteDC(dc);ReleaseDC(owner,screen);
  }
  STDMETHODIMP QueryInterface(REFIID id,void** out) override {
    *out=nullptr;
    if(id!=IID_IUnknown && id!=IID_ITfRange)return E_NOINTERFACE;
    *out=static_cast<ITfRange*>(this);AddRef();return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++refs; }
  STDMETHODIMP_(ULONG) Release() override { return --refs; }
  STDMETHODIMP SetText(TfEditCookie,DWORD,const WCHAR* text,LONG length) override {
    std::wstring wide(text,length);
    if(!SetWindowTextW(edit,wide.c_str()))return E_FAIL;
    RedrawWindow(edit,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);
    wchar_t shown[512]{};
    const int count=GetWindowTextW(edit,shown,512);
    if(wide!=std::wstring(shown,count))return E_FAIL;
    char bytes[2048];
    const int size=WideCharToMultiByte(CP_UTF8,0,shown,count,bytes,sizeof(bytes),nullptr,nullptr);
    events.push_back({{"ms",Ms(start)},{"raw",raw},{"text",std::string(bytes,size)}});
    if(!frames.empty()) {
      SetWindowTextW(keys,std::wstring(raw.begin(),raw.end()).c_str());
      Capture();
    }
    return S_OK;
  }
  STDMETHODIMP GetText(TfEditCookie,DWORD flags,WCHAR* text,ULONG capacity,ULONG* copied) override {
    if (!copied || (!text && capacity)) return E_INVALIDARG;
    if (flags) return E_NOTIMPL;
    std::wstring current(GetWindowTextLengthW(edit)+1, L'\0');
    const int count = GetWindowTextW(edit,current.data(),static_cast<int>(current.size()));
    *copied = std::min(capacity,static_cast<ULONG>(count));
    std::copy_n(current.begin(),*copied,text);
    return S_OK;
  }
  STDMETHODIMP GetFormattedText(TfEditCookie,IDataObject**) override {return E_NOTIMPL;}
  STDMETHODIMP GetEmbedded(TfEditCookie,REFGUID,REFIID,IUnknown**) override {return E_NOTIMPL;}
  STDMETHODIMP InsertEmbedded(TfEditCookie,DWORD,IDataObject*) override {return E_NOTIMPL;}
  STDMETHODIMP ShiftStart(TfEditCookie,LONG,LONG*,const TF_HALTCOND*) override {return E_NOTIMPL;}
  STDMETHODIMP ShiftEnd(TfEditCookie,LONG,LONG*,const TF_HALTCOND*) override {return E_NOTIMPL;}
  STDMETHODIMP ShiftStartToRange(TfEditCookie,ITfRange*,TfAnchor) override {return E_NOTIMPL;}
  STDMETHODIMP ShiftEndToRange(TfEditCookie,ITfRange*,TfAnchor) override {return E_NOTIMPL;}
  STDMETHODIMP ShiftStartRegion(TfEditCookie,TfShiftDir,BOOL*) override {return E_NOTIMPL;}
  STDMETHODIMP ShiftEndRegion(TfEditCookie,TfShiftDir,BOOL*) override {return E_NOTIMPL;}
  STDMETHODIMP IsEmpty(TfEditCookie,BOOL*) override {return E_NOTIMPL;}
  STDMETHODIMP Collapse(TfEditCookie,TfAnchor) override {return E_NOTIMPL;}
  STDMETHODIMP IsEqualStart(TfEditCookie,ITfRange*,TfAnchor,BOOL*) override {return E_NOTIMPL;}
  STDMETHODIMP IsEqualEnd(TfEditCookie,ITfRange*,TfAnchor,BOOL*) override {return E_NOTIMPL;}
  STDMETHODIMP CompareStart(TfEditCookie,ITfRange*,TfAnchor,LONG*) override {return E_NOTIMPL;}
  STDMETHODIMP CompareEnd(TfEditCookie,ITfRange*,TfAnchor,LONG*) override {return E_NOTIMPL;}
  STDMETHODIMP AdjustForInsert(TfEditCookie,ULONG,BOOL*) override {return E_NOTIMPL;}
  STDMETHODIMP GetGravity(TfGravity*,TfGravity*) override {return E_NOTIMPL;}
  STDMETHODIMP SetGravity(TfEditCookie,TfGravity,TfGravity) override {return E_NOTIMPL;}
  STDMETHODIMP Clone(ITfRange**) override {return E_NOTIMPL;}
  STDMETHODIMP GetContext(ITfContext**) override {return E_NOTIMPL;}
};
struct Composition final : FakeComposition {
  EditRange& range;
  Composition(TextService& service,EditRange& r):FakeComposition(&service),range(r){}
  STDMETHODIMP GetRange(ITfRange** out) override { *out=&range;range.AddRef();return S_OK; }
};
struct Document final : FakeContext {
  STDMETHODIMP RequestEditSession(TfClientId id,ITfEditSession* s,DWORD flags,HRESULT* result) override {
    if(flags&TF_ES_SYNC){*result=s->DoEditSession(1);return S_OK;}
    return FakeContext::RequestEditSession(id,s,flags,result);
  }
};
struct TipStabilityTest {
  static bool TestRefusedFinal(TextService& service, Document& doc, Composition& composition, EditRange& range) {
    auto attach=[&] {
      service.session_.Reset();
      if (!service.composition_) {
        service.composition_=&composition; composition.AddRef(); service.has_composition_=true;
      }
    };
    auto collide=[&] {
      attach();
      service.session_.Type("kana");
      auto shown=service.session_.visible_text();
      if (FAILED(service.EditSetText(1,&doc,shown,shown))) return false;
      service.session_.RefineCandidates();
      auto id=service.session_.decode_input();
      auto preview=service.session_, final=service.session_;
      ime::Candidate one,two;
      one.output_text=u8"仮名"; one.reading_text=u8"かな";
      two.output_text=u8"かな"; two.reading_text=u8"かな";
      return preview.ApplyPendingPreview(id,{one},++service.received_preview_sequence_) &&
          final.ApplyCandidates(id,{one,two}) &&
          service.RequestState(&doc,preview,preview.visible_text(),false,false,true)==TF_S_ASYNC &&
          service.RequestState(&doc,final,final.visible_text(),false,false,true)==E_FAIL &&
          service.HasQueuedFinal() && service.queued_final_->retry_candidates.size()==2;
    };
    if (!collide()) return false;
    // Keep the lock held across retries. Only the timer deadline may advance
    // the backoff. An unapplied final must never restart or call the worker.
    for (int i=0;i<8;++i) {
      const auto delay=service.queued_final_->retry_delay;
      service.queued_final_->retry_at=0;
      service.PollEngine();
      if (service.queued_final_->retry_delay!=(std::min<DWORD>)(2*delay,1000)) return false;
      const auto deadline=service.queued_final_->retry_at;
      service.PollEngine();
      if (service.queued_final_->retry_at!=deadline || service.engine_channel_.running()) return false;
    }
    auto grant=[&] {
      if (doc.Flush()!=S_FALSE || !service.HasQueuedFinal() || service.queued_final_->retry_at) return false;
      service.PollEngine();
      return doc.queued && !service.engine_channel_.running();
    };
    if (!grant() || doc.Flush()!=S_OK || !service.session_.candidates_ready() ||
        service.session_.candidates().size()!=2 || service.session_.visible_text()!=u8"仮名") return false;
    // A later key supersedes a retry already accepted by the app.
    if (!collide() || !grant()) return false;
    auto typed=service.session_; typed.Type("g");
    if (service.RequestState(&doc,typed,typed.visible_text(),false,false,false)!=S_OK ||
        doc.Flush()!=S_FALSE || service.session_.raw_text()!="kanag" || service.HasQueuedFinal()) return false;
    if (!collide() || !grant()) return false;
    service.OnSetFocus(FALSE);
    service.OnSetFocus(TRUE);
    if (doc.Flush()!=S_FALSE || service.HasQueuedFinal() || service.session_.candidates_ready()) return false;
    // A paste can replace the actual range without generating an IME key.
    if (!collide() || !grant()) return false;
    SendMessageW(range.edit,EM_SETSEL,0,-1);
    SendMessageW(range.edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"貼り付けた文字"));
    if (doc.Flush()!=S_FALSE || service.session_.composing() || service.HasQueuedFinal()) return false;
    wchar_t actual[32]{};
    GetWindowTextW(range.edit,actual,32);
    if (std::wstring_view(actual)!=L"貼り付けた文字") return false;
    attach();
    return true;
  }
  static int Run(const wchar_t* worker,const wchar_t* profile,const wchar_t* output,const wchar_t* captures,bool phrases) {
    FakeThreadManager manager;
    Document doc; EditRange range;
    TextService service; Composition composition(service,range);
    auto owner=CreateWindowW(L"STATIC",L"",WS_POPUP|WS_VISIBLE,0,0,440,100,nullptr,nullptr,g_hInstance,nullptr);
    range.edit=CreateWindowW(L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_BORDER,8,8,424,44,owner,nullptr,g_hInstance,nullptr);
    auto font=CreateFontW(-26,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Yu Gothic UI");
    SendMessageW(range.edit,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    if(captures) {
      range.frames=captures;std::filesystem::create_directories(range.frames);
      CreateWindowW(L"STATIC",L"入力キー",WS_CHILD|WS_VISIBLE,10,64,72,28,owner,nullptr,g_hInstance,nullptr);
      range.keys=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,86,64,330,28,owner,nullptr,g_hInstance,nullptr);
    }
    if(!range.edit || FAILED(service.Activate(&manager,1)))return 3;
    service.context_=&doc; doc.AddRef();
    service.composition_=&composition; composition.AddRef(); service.has_composition_=true;
    // Repeatedly accepting the same candidate must leave the document alone.
    // The application may change its own range, including adding text after a matching prefix.
    service.session_.Type("kana");
    const auto displayed=service.session_.visible_text();
    if (FAILED(service.EditSetText(1,&doc,displayed,displayed)) ||
        FAILED(service.EditSetText(1,&doc,displayed,displayed)) || range.events.size()!=1) return 23;
    SetWindowTextW(range.edit,L"かな別");
    if (FAILED(service.EditSetText(1,&doc,displayed,displayed)) || range.events.size()!=2) return 24;
    service.session_.RefineCandidates();
    const auto request=service.session_.decode_input();
    auto ready=service.session_;
    ime::Candidate first; first.output_text=u8"仮名"; first.reading_text=u8"かな";
    if (!ready.ApplyCandidates(request,{first}) ||
        service.RequestState(&doc,ready,ready.visible_text(),false,false,true)!=TF_S_ASYNC) return 25;
    // Press Enter before TSF grants the queued edit. It must use the already
    // completed candidate, even if no worker is available for another decode.
    service.retry_engine_at_=~ULONGLONG{0};
    if (!service.BeginPendingCommit() || service.engine_channel_.running() ||
        service.FinishPendingCommit(false)!=S_OK || doc.Flush()!=S_FALSE ||
        range.events.back().at("text")!=u8"仮名") return 26;
    service.retry_engine_at_=0;
    service.session_.Reset();
    if (!service.composition_) {
      service.composition_=&composition; composition.AddRef(); service.has_composition_=true;
    }
    if (!TestRefusedFinal(service,doc,composition,range)) return 27;
    service.test_worker_=worker;service.test_profile_=profile;
    if(!service.StartEngine())return 4;
    HANDLE counters=OpenWorker(worker);
    if(!counters)return 8;
    unsigned restarts=0;
    Json runs=Json::array();
    auto pump=[&] {
      MSG msg{};
      while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
      if(doc.queued)doc.Flush();
      if (WaitForSingleObject(counters,0)==WAIT_OBJECT_0 && service.engine_channel_.running()) {
        auto next=OpenWorker(worker);
        if (next && WaitForSingleObject(next,0)==WAIT_TIMEOUT) {
          CloseHandle(counters); counters=next; ++restarts;
        } else if(next) CloseHandle(next);
      }
      Sleep(1);
    };
    struct Trial { int gap; std::string raw, expected; };
    std::vector<Trial> trials;
    const wchar_t* casesPath = _wgetenv(L"YOMITSUGU_BENCHMARK_CASES");
    const wchar_t* configuredWindow = _wgetenv(L"YOMITSUGU_BENCHMARK_SETTLE_MS");
    const int observeMs = configuredWindow ? std::clamp(_wtoi(configuredWindow), 2000, 15000) : 2000;
    if (casesPath) {
      std::ifstream corpus(std::filesystem::path(casesPath), std::ios::binary);
      for (const auto& row : Json::parse(corpus))
        trials.push_back({40, row.at("raw").get<std::string>(), row.at("expected").at(0).get<std::string>()});
    } else if(phrases) {
      trials={{40,"kyouhaiitenkidesune",u8"今日はいい天気ですね"},
              {40,"hasiwowataru",u8"橋を渡る"},
              {40,"toshokandehonwoyomu",u8"図書館で本を読む"},
              {40,"sakuragasaiteiru",u8"桜が咲いている"},
              {40,"toukyouniikimasu",u8"東京に行きます"}};
    } else {
      for(int gap : {80,40,20,80,40,20})
        trials.push_back({gap,"nihongowobenkyousuru",u8"日本語を勉強する"});
    }
    for(const auto& trial : trials) {
      if (casesPath) {
        if (!service.composition_) {
          service.composition_=&composition; composition.AddRef(); service.has_composition_=true;
        }
        std::cout<<"typing"<<std::endl;
      }
      range.raw.clear();range.events=Json::array();range.start=Clock::now();
      service.session_.Reset();service.submitted_revision_=-1; service.last_submit_at_=0;
      std::vector<double> keyMs, keyStartMs;
      for(char key:trial.raw) {
        const auto at=range.raw.size()*trial.gap;
        while(Ms(range.start)<at)pump();
        range.raw+=key;
        BOOL eaten=FALSE;
        // These synthetic letter events have no modifiers. Set the test
        // thread's keyboard state for the layout used by the real key handler.
        BYTE keyboard[256]{};
        const auto vk = VkKeyScanExW(static_cast<unsigned char>(key), GetKeyboardLayout(0));
        if (vk == -1) return 9;
        if (HIBYTE(vk) & 1) keyboard[VK_SHIFT]=0x80;
        if(!SetKeyboardState(keyboard))return 9;
        if (_wgetenv(L"YOMITSUGU_BENCHMARK_RESTART") && &trial==&trials.back() && range.raw.size()==2) {
          // Terminate only the previously verified child process of this fixture.
          HANDLE dead=OpenProcess(PROCESS_TERMINATE|SYNCHRONIZE,FALSE,GetProcessId(counters));
          if (!dead) return 21;
          const bool stopped=TerminateProcess(dead,99) && WaitForSingleObject(dead,2000)==WAIT_OBJECT_0;
          CloseHandle(dead);
          if (!stopped) return 22;
        }
        const auto sent=Clock::now();
        const auto hr=service.OnKeyDown(&doc,static_cast<WPARAM>(LOBYTE(vk)),0,&eaten);
        if(FAILED(hr) || !eaten || service.session_.raw_text()!=range.raw) {
          std::cerr<<"key failed: "<<range.raw<<" -> "<<service.session_.raw_text()
                   <<" hr="<<hr<<" eaten="<<eaten<<" gap="<<trial.gap<<"\n";
          return 5;
        }
        keyMs.push_back(Ms(sent));
        keyStartMs.push_back(std::chrono::duration<double,std::milli>(sent-range.start).count());
      }
      const double last=Ms(range.start);
      double candidatesReadyMs = -1;
      if (casesPath) {
        std::cout<<"settling"<<std::endl;
        while (Ms(range.start)<last+observeMs) {
          if (candidatesReadyMs < 0 && !service.session_.typing() &&
              service.session_.candidates_ready() && !doc.queued)
            candidatesReadyMs = Ms(range.start) - last;
          pump();
        }
      } else while(service.session_.typing() || !service.session_.candidates_ready() || doc.queued) {
        if(Ms(range.start)>last+10000)return 6;
        pump();
      }
      const auto finalText=service.session_.visible_text();
      auto resources=Resources(counters);
      if(resources.is_null())return 8;
      runs.push_back({{"gap",trial.gap},{"last_key",last},{"events",range.events},{"key_ms",keyMs},{"key_start_ms",keyStartMs},{"window_ms",observeMs},{"final",finalText},
                      {"expected",trial.expected},{"worker",resources}});
      if (casesPath) {
        runs.back()["candidates_ready_ms"] = candidatesReadyMs < 0 ? Json(nullptr) : Json(candidatesReadyMs);
        const bool hold = _wgetenv(L"YOMITSUGU_BENCHMARK_HOLD") && &trial == &trials.back();
        if (hold) {
          const auto wait=Clock::now();
          while ((service.session_.typing() || !service.session_.candidates_ready() || doc.queued) && Ms(wait)<15000) pump();
          if (!service.session_.candidates_ready() || range.events.empty()) return 19;
          const auto held=range.events.back().at("text");
          std::cout<<"holding"<<std::endl;
          const auto paused=Clock::now();
          while (Ms(paused)<65000) pump();
          if (range.events.back().at("text")!=held || !service.session_.candidates_ready()) return 19;
          runs.back()["held_worker"]=service.engine_channel_.running();
        }
        BOOL eaten=FALSE;
        auto pressed=Clock::now();
        if (FAILED(service.OnKeyDown(&doc,VK_RETURN,0,&eaten)) || !eaten) return 13;
        while (service.session_.composing() && Ms(pressed)<4000) pump();
        if (service.session_.composing() || range.events.empty()) return 14;
        runs.back()["answer"]=range.events.back().at("text");
        runs.back()["commit_ms"]=Ms(pressed);
        runs.back()["worker_restarts"]=restarts;
      }
      if(!casesPath && !phrases && finalText!=trial.expected)return 7;
      if(captures)break;
    }
    if (casesPath) {
      const auto setting=_wgetenv(L"YOMITSUGU_BENCHMARK_IDLE");
      const auto duration=setting ? _wtoi(setting)*1000 : 0;
      std::cout<<"idle"<<std::endl;
      const auto start=Clock::now();
      while (Ms(start)<duration) pump();
    } else if (phrases) {
      if (service.low_memory_event_) CloseHandle(service.low_memory_event_);
      service.low_memory_event_ = CreateEventW(nullptr, TRUE, TRUE, nullptr);
      if (!service.low_memory_event_) return 9;
      service.PollEngine();
      if (!service.engine_channel_.running()) return 10; // Recent keystroke.
      if (service.retry_engine_at_) return 20; // A worker already returned a valid response.
      auto visible = service.session_.visible_text();
      auto selected = service.session_.selected_index();
      service.last_key_at_ = GetTickCount64() - 5100;
      service.PollEngine();
      if (service.engine_channel_.running() || !service.session_.candidates_ready() ||
          !service.session_.composing() || service.session_.visible_text() != visible ||
          service.session_.selected_index() != selected) return 15;
      service.PollEngine();
      if (service.engine_channel_.running()) return 16; // No restart on timer.
      if (!service.engine_channel_.Start(worker,profile)) return 17;
      CloseHandle(counters); counters=OpenWorker(worker);
      if (!counters) return 18;
      ResetEvent(service.low_memory_event_);
      service.session_.Reset();
      service.last_key_at_ = GetTickCount64();
      service.PollEngine();
      if (!service.engine_channel_.running()) return 11;
      SetEvent(service.low_memory_event_);
      service.PollEngine();
      if (service.engine_channel_.running() || WaitForSingleObject(counters, 2000) != WAIT_OBJECT_0) return 12;
      // A cold worker may restart once before the usual cooldown. A second
      // consecutive exit must leave it down instead of launching in a loop.
      service.retry_engine_at_=0;
      if (!service.StartEngine()) return 24;
      service.engine_channel_.Stop();
      if (!service.StartEngine()) return 25;
      service.engine_channel_.Stop();
      if (service.StartEngine()) return 26;
    }
    service.cand_window_.SetPollInterval(0);
    CloseHandle(counters);
    service.ReleaseCompositionRef();
    service.Deactivate();DestroyWindow(owner);DeleteObject(font);
    std::ofstream(std::filesystem::path(output),std::ios::binary)<<runs.dump();
    return 0;
  }
};
int wmain(int argc,wchar_t** argv) {
  if(argc!=4 && argc!=5)return 2;
  auto desktop=CreateDesktopW((L"YomitsuguLive-"+std::to_wstring(GetCurrentProcessId())).c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);
  if(!desktop||!SetThreadDesktop(desktop))return 2;
  g_hInstance=GetModuleHandleW(nullptr);
  if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return 2;
  const bool phrases=argc==5 && std::wstring_view(argv[4])==L"--phrases";
  const int code=TipStabilityTest::Run(argv[1],argv[2],argv[3],argc==5 && !phrases ? argv[4] : nullptr,phrases);
  CoUninitialize();
  return code;
}

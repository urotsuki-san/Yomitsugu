#pragma once

#include <windows.h>
#include <msctf.h>
#include <ctfutb.h>
#include <vector>
#include <string>
#include <memory>

#include "ime_engine.h"
#include "engine_channel.h"

// {8F3A1C2E-4B5D-4E6F-8A9B-0C1D2E3F4A5B}
static const GUID kTextServiceClsid = {
    0x8f3a1c2e, 0x4b5d, 0x4e6f, {0x8a, 0x9b, 0x0c, 0x1d, 0x2e, 0x3f, 0x4a, 0x5b}};
// {8F3A1C2E-4B5D-4E6F-8A9B-0C1D2E3F4A5C}
static const GUID kProfileGuid = {
    0x8f3a1c2e, 0x4b5d, 0x4e6f, {0x8a, 0x9b, 0x0c, 0x1d, 0x2e, 0x3f, 0x4a, 0x5c}};
// {8F3A1C2E-4B5D-4E6F-8A9B-0C1D2E3F4A5D}
static const GUID kDisplayAttributeGuid = {
    0x8f3a1c2e, 0x4b5d, 0x4e6f, {0x8a, 0x9b, 0x0c, 0x1d, 0x2e, 0x3f, 0x4a, 0x5d}};

extern HINSTANCE g_hInstance;
extern LONG g_moduleRefCount;

void ModuleAddRef();
void ModuleRelease();

class TextService;
class InputModeMenu;

class CandidateWindow {
 public:
  CandidateWindow();
  ~CandidateWindow();
  bool Create(HINSTANCE hinst, HWND parent);
  void Destroy();
  void Show(const std::vector<ime::Candidate>& cands, int selected, POINT pt);
  void Hide();
  void SetService(TextService* svc) { service_ = svc; }
  void SetOwner(HWND owner);
  HWND hwnd() const { return hwnd_; }

 private:
  friend struct TipStabilityTest;
  static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
  HWND poll_window_ = nullptr;
  HWND hwnd_ = nullptr;
  HWND list_ = nullptr;
  HFONT font_ = nullptr;
  TextService* service_ = nullptr;
  UINT font_dpi_ = 0;
};

class TextService : public ITfTextInputProcessorEx,
                    public ITfKeyEventSink,
                    public ITfThreadMgrEventSink,
                    public ITfCompositionSink,
                    public ITfDisplayAttributeProvider {
 public:
  TextService();
  ~TextService();

  // IUnknown
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;

  // ITfTextInputProcessor
  STDMETHODIMP Activate(ITfThreadMgr* ptim, TfClientId tid) override;
  STDMETHODIMP Deactivate() override;

  // ITfTextInputProcessorEx
  STDMETHODIMP ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD dwFlags) override;

  // ITfKeyEventSink
  STDMETHODIMP OnSetFocus(BOOL fForeground) override;
  STDMETHODIMP OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
  STDMETHODIMP OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
  STDMETHODIMP OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
  STDMETHODIMP OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
  STDMETHODIMP OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten) override;

  // ITfThreadMgrEventSink
  STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr* pdim) override;
  STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr* pdim) override;
  STDMETHODIMP OnSetFocus(ITfDocumentMgr* pdimFocus, ITfDocumentMgr* pdimPrevFocus) override;
  STDMETHODIMP OnPushContext(ITfContext* pic) override;
  STDMETHODIMP OnPopContext(ITfContext* pic) override;

  // ITfCompositionSink
  STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite, ITfComposition* pComposition) override;

  // ITfDisplayAttributeProvider
  STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) override;
  STDMETHODIMP GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) override;

  // UIスレッドから呼ぶ編集セッション処理。
  HRESULT EditStart(TfEditCookie ec, ITfContext* pic);
  HRESULT EditSetText(TfEditCookie ec, ITfContext* pic, const std::string& utf8, const std::string& caret);
  HRESULT EditEnd(TfEditCookie ec, ITfContext* pic, bool commit);
  HRESULT ApplyState(TfEditCookie ec, ITfContext* pic, const ime::DecodeInput& expected,
                    const ime::Session& next, const std::string& text, bool finish, bool cancel);
  void PollEngine();
  bool StartEngine();
  bool BeginPendingCommit();
  void FinishEditCompleted(const ime::DecodeInput& expected);
  void ClickCandidate(int index);
  TfClientId client_id() const { return client_id_; }
  TfGuidAtom display_attr_atom() const { return display_attr_atom_; }

 private:
  friend struct TipStabilityTest;
  HRESULT ActivateInternal(ITfThreadMgr* ptim, TfClientId tid);
  HRESULT InitThreadMgrSink();
  HRESULT UninitThreadMgrSink();
  HRESULT HandleKey(ITfContext* pic, WPARAM wParam, BOOL* pfEaten, bool test_only);
  HRESULT EndComposition(ITfContext* pic, bool commit);
  HRESULT RequestState(ITfContext* pic, const ime::Session& next, const std::string& text,
                       bool finish = false, bool cancel = false, bool async = false);
  void SyncCandidateWindow(ITfContext* pic);
  void ReleaseCompositionRef();
  void DetachComposition();
  HRESULT FinishPendingCommit(bool async);
  void ResetSessionState();
  std::string Utf8FromW(LPWSTR ws, int len) const;
  std::wstring WFromUtf8(const std::string& s) const;

  LONG ref_ = 1;
  ITfThreadMgr* thread_mgr_ = nullptr;
  ITfLangBarItemMgr* lang_bar_mgr_ = nullptr;
  InputModeMenu* input_mode_menu_ = nullptr;
  TfClientId client_id_ = TF_CLIENTID_NULL;
  DWORD thread_key_sink_cookie_ = TF_INVALID_COOKIE;
  DWORD thread_mgr_sink_cookie_ = TF_INVALID_COOKIE;
  ITfContext* context_ = nullptr;
  ITfComposition* composition_ = nullptr;
  bool has_composition_ = false;
  bool activated_ = false;
  bool foreground_ = true;
  bool document_focused_ = true;
  bool ending_ = false;
  bool restricted_ = false;
  POINT caret_point_{0, 0};
  bool caret_valid_ = false;
  ime::EngineChannel engine_channel_;
  int submitted_revision_ = -1;
  int submitted_interaction_ = -1;
  int submitted_context_ = -1;
  ULONGLONG retry_engine_at_ = 0;
  bool pending_commit_ = false;
  ULONGLONG commit_deadline_ = 0;
  int commit_edit_revision_ = -1;
  ime::Session session_;
  CandidateWindow cand_window_;
  TfGuidAtom display_attr_atom_ = TF_INVALID_GUIDATOM;
};

class DisplayAttributeInfo : public ITfDisplayAttributeInfo {
 public:
  DisplayAttributeInfo();
  ~DisplayAttributeInfo();

  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;
  STDMETHODIMP GetGUID(GUID* pguid) override;
  STDMETHODIMP GetDescription(BSTR* pbstrDesc) override;
  STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE* pda) override;
  STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE* pda) override;
  STDMETHODIMP Reset() override;

 private:
  LONG ref_ = 1;
};

class DisplayAttributeEnum : public IEnumTfDisplayAttributeInfo {
 public:
  explicit DisplayAttributeEnum(ULONG index = 0);
  ~DisplayAttributeEnum();

  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;
  STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo** ppEnum) override;
  STDMETHODIMP Reset() override;
  STDMETHODIMP Next(ULONG ulCount, ITfDisplayAttributeInfo** ppInfo, ULONG* pcFetched) override;
  STDMETHODIMP Skip(ULONG ulCount) override;

 private:
  LONG ref_ = 1;
  ULONG index_ = 0;
};

class ClassFactory : public IClassFactory {
 public:
  ClassFactory();
  ~ClassFactory();

  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;
  STDMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override;
  STDMETHODIMP LockServer(BOOL fLock) override;

 private:
  LONG ref_ = 1;
};

HRESULT RegisterTextService();
HRESULT UnregisterTextService();

#include "tip.h"
#include "tip_diagnostics.h"
#include "input_mode_menu.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>
#include <filesystem>
#include <algorithm>
#include <textstor.h>
#include <initguid.h>
#include <inputscope.h>

namespace {
void TipLog(const char*, ...) {}  // Release builds never record key codes or input.
struct DiagnosticCounters {
  std::atomic<std::uint32_t> activate_calls{0};
  std::atomic<std::uint32_t> activate_success{0};
  std::atomic<std::uint32_t> activate_flags{0};
  std::atomic<std::int32_t> advise_result{0};
  std::atomic<std::uint32_t> test_key_down{0};
  std::atomic<std::uint32_t> key_down{0};
  std::atomic<std::uint32_t> test_eaten{0};
  std::atomic<std::uint32_t> gated_no_context{0};
  std::atomic<std::uint32_t> gated_restricted{0};
  std::atomic<std::uint32_t> gated_background{0};
  std::atomic<std::uint32_t> gated_compartment{0};
  std::atomic<std::uint32_t> gated_readonly{0};
  std::atomic<std::uint32_t> gated_no_character{0};
  std::atomic<std::uint32_t> state_success{0};
  std::atomic<std::uint32_t> state_failure{0};
  std::atomic<std::int32_t> langbar_manager_result{0};
  std::atomic<std::int32_t> langbar_add_result{0};
};
DiagnosticCounters g_diagnostics;
class StateEditSession : public ITfEditSession {
 public:
  StateEditSession(TextService* svc, ITfContext* pic, ime::DecodeInput expected,
                   ime::Session next, std::string text, bool finish, bool cancel)
      : svc_(svc), pic_(pic), expected_(std::move(expected)), next_(std::move(next)),
        text_(std::move(text)), finish_(finish), cancel_(cancel) { svc_->AddRef(); pic_->AddRef(); }
  ~StateEditSession() { pic_->Release(); svc_->Release(); }
  STDMETHODIMP QueryInterface(REFIID id, void** out) override {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (id != IID_IUnknown && id != IID_ITfEditSession) return E_NOINTERFACE;
    *out = static_cast<ITfEditSession*>(this); AddRef(); return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
  STDMETHODIMP_(ULONG) Release() override { auto r = InterlockedDecrement(&refs_); if (!r) delete this; return r; }
  STDMETHODIMP DoEditSession(TfEditCookie ec) override {
    try { return svc_->ApplyState(ec, pic_, expected_, next_, text_, finish_, cancel_); }
    catch (...) { return E_FAIL; }
  }
 private:
  LONG refs_ = 1; TextService* svc_; ITfContext* pic_;
  ime::DecodeInput expected_; ime::Session next_; std::string text_; bool finish_, cancel_;
};
bool CompartmentEnabled(IUnknown* object, REFGUID id, bool default_value) {
  if (!object) return default_value;
  ITfCompartmentMgr* mgr = nullptr;
  if (FAILED(object->QueryInterface(IID_ITfCompartmentMgr, reinterpret_cast<void**>(&mgr)))) return default_value;
  ITfCompartment* compartment = nullptr; VARIANT v; VariantInit(&v);
  bool value = default_value;
  if (SUCCEEDED(mgr->GetCompartment(id, &compartment)) && compartment) {
    if (SUCCEEDED(compartment->GetValue(&v)) && v.vt == VT_I4) value = v.lVal != 0;
    compartment->Release();
  }
  VariantClear(&v); mgr->Release(); return value;
}
}

extern "C" BOOL WINAPI ImeTipGetDiagnostics(TipDiagnostics* out) {
  if (!out || out->size != sizeof(TipDiagnostics)) return FALSE;
  out->activate_calls = g_diagnostics.activate_calls.load();
  out->activate_success = g_diagnostics.activate_success.load();
  out->activate_flags = g_diagnostics.activate_flags.load();
  out->advise_result = g_diagnostics.advise_result.load();
  out->test_key_down = g_diagnostics.test_key_down.load();
  out->key_down = g_diagnostics.key_down.load();
  out->test_eaten = g_diagnostics.test_eaten.load();
  out->gated_no_context = g_diagnostics.gated_no_context.load();
  out->gated_restricted = g_diagnostics.gated_restricted.load();
  out->gated_background = g_diagnostics.gated_background.load();
  out->gated_compartment = g_diagnostics.gated_compartment.load();
  out->gated_readonly = g_diagnostics.gated_readonly.load();
  out->gated_no_character = g_diagnostics.gated_no_character.load();
  out->state_success = g_diagnostics.state_success.load();
  out->state_failure = g_diagnostics.state_failure.load();
  out->langbar_manager_result = g_diagnostics.langbar_manager_result.load();
  out->langbar_add_result = g_diagnostics.langbar_add_result.load();
  return TRUE;
}

HRESULT TextService::EditStart(TfEditCookie ec, ITfContext* pic) {
  if (has_composition_) return S_OK;
  ITfContextComposition* comp = nullptr;
  HRESULT hr = pic->QueryInterface(IID_ITfContextComposition, reinterpret_cast<void**>(&comp));
  if (FAILED(hr)) return hr;
  TF_SELECTION selection{}; ULONG fetched = 0;
  hr = pic->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &selection, &fetched);
  if (SUCCEEDED(hr) && fetched && selection.range)
    hr = comp->StartComposition(ec, selection.range, this, &composition_);
  else hr = E_FAIL;
  if (selection.range) selection.range->Release();
  comp->Release(); has_composition_ = SUCCEEDED(hr) && composition_;
  return hr;
}

HRESULT TextService::EditSetText(TfEditCookie ec, ITfContext* pic, const std::string& text,
                               const std::string& caret) {
  HRESULT hr = EditStart(ec, pic);
  if (FAILED(hr)) return hr;
  ITfRange* range = nullptr;
  hr = composition_->GetRange(&range);
  if (FAILED(hr) || !range) return E_FAIL;
  auto wide = WFromUtf8(text);
  hr = range->SetText(ec, 0, wide.c_str(), static_cast<LONG>(wide.size()));
  if (SUCCEEDED(hr)) {
    ITfProperty* prop = nullptr;
    if (SUCCEEDED(pic->GetProperty(GUID_PROP_ATTRIBUTE, &prop)) && prop) {
      VARIANT v; VariantInit(&v); v.vt = VT_I4; v.lVal = display_attr_atom_;
      prop->SetValue(ec, range, &v); prop->Release();
    }
    ITfRange* selection = nullptr;
    if (SUCCEEDED(range->Clone(&selection)) && selection) {
      selection->Collapse(ec, TF_ANCHOR_START); LONG moved = 0;
      auto prefix = WFromUtf8(caret);
      selection->ShiftEnd(ec, static_cast<LONG>((std::min)(prefix.size(), wide.size())), &moved, nullptr);
      selection->Collapse(ec, TF_ANCHOR_END);
      TF_SELECTION s{}; s.range = selection; s.style.ase = TF_AE_NONE;
      pic->SetSelection(ec, 1, &s);
      ITfContextView* view = nullptr;
      if (SUCCEEDED(pic->GetActiveView(&view)) && view) {
        RECT rect{}; BOOL clipped = FALSE; HWND owner = nullptr;
        if (SUCCEEDED(view->GetTextExt(ec, selection, &rect, &clipped))) {
          caret_point_ = {rect.left, rect.bottom + 2}; caret_valid_ = true;
        }
        if (SUCCEEDED(view->GetWnd(&owner)) && owner) cand_window_.SetOwner(owner);
        view->Release();
      }
      selection->Release();
    }
  }
  range->Release(); return hr;
}

HRESULT TextService::EditEnd(TfEditCookie ec, ITfContext*, bool commit) {
  if (!composition_) return S_OK;
  ITfComposition* comp = composition_; comp->AddRef();
  HRESULT hr = S_OK;
  if (!commit) {
    ITfRange* range = nullptr;
    hr = comp->GetRange(&range);
    if (SUCCEEDED(hr) && range) { hr = range->SetText(ec, 0, L"", 0); range->Release(); }
  }
  if (SUCCEEDED(hr)) {
    ending_ = true; hr = comp->EndComposition(ec); ending_ = false;
    if (SUCCEEDED(hr)) ReleaseCompositionRef();
  }
  comp->Release(); return hr;
}

HRESULT TextService::ApplyState(TfEditCookie ec, ITfContext* pic, const ime::DecodeInput& expected,
                               const ime::Session& next, const std::string& text, bool finish, bool cancel) {
  const auto now = session_.decode_input();
  if (!activated_ || pic != context_ || now.revision != expected.revision ||
      now.context_generation != expected.context_generation ||
      now.interaction_generation != expected.interaction_generation) return S_FALSE;
  if (!foreground_ && !finish && !cancel) return S_FALSE;
  TF_SELECTION selection{}; ULONG fetched = 0;
  std::string field = "prose";
  if (SUCCEEDED(pic->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &selection, &fetched)) && selection.range) {
    ITfReadOnlyProperty* property = nullptr;
    bool sensitive = false;
    if (SUCCEEDED(pic->GetAppProperty(GUID_PROP_INPUTSCOPE, &property)) && property) {
      VARIANT value; VariantInit(&value);
      if (SUCCEEDED(property->GetValue(ec, selection.range, &value)) && value.vt == VT_UNKNOWN && value.punkVal) {
        ITfInputScope* scope = nullptr;
        if (SUCCEEDED(value.punkVal->QueryInterface(IID_ITfInputScope, reinterpret_cast<void**>(&scope)))) {
          InputScope* scopes = nullptr; UINT count = 0;
          if (SUCCEEDED(scope->GetInputScopes(&scopes, &count))) {
            for (UINT i = 0; i < count; ++i) {
              if (scopes[i] == IS_PASSWORD || scopes[i] == IS_NUMERIC_PASSWORD ||
                  scopes[i] == IS_NUMERIC_PIN || scopes[i] == IS_ALPHANUMERIC_PIN ||
                  scopes[i] == IS_ALPHANUMERIC_PIN_SET) sensitive = true;
              if (scopes[i] == IS_URL || scopes[i] == IS_EMAIL_SMTPEMAILADDRESS) field = "identifier";
            }
            CoTaskMemFree(scopes);
          }
          scope->Release();
        }
      }
      VariantClear(&value); property->Release();
    }
    selection.range->Release();
    if (sensitive) return E_ACCESSDENIED;
  }
  HRESULT hr = S_OK;
  if (!cancel) hr = EditSetText(ec, pic, text, finish ? text : next.caret_prefix());
  if (SUCCEEDED(hr) && (finish || cancel)) hr = EditEnd(ec, pic, !cancel);
  if (FAILED(hr)) return hr;
  const bool selected_conversion = session_.candidates_ready() && session_.selected_index() >= 0 &&
      session_.selected_index() < static_cast<int>(session_.candidates().size()) && !session_.candidates()[session_.selected_index()].is_raw;
  if (finish && !cancel && session_.composing() && (next.last_commit_learnable() || selected_conversion)) {
    auto learned = now; learned.field = field;
    engine_channel_.Learn(learned, text);
  }
  session_ = next;
  if (!finish && !cancel && composition_) {
    if (session_.decode_input().field != field) session_.set_field(field);
    ITfRange* range = nullptr;
    if (SUCCEEDED(composition_->GetRange(&range)) && range) {
      auto surrounding = [&](bool left) {
        ITfRange* part = nullptr; std::string value;
        if (SUCCEEDED(range->Clone(&part)) && part) {
          LONG moved = 0;
          part->Collapse(ec, left ? TF_ANCHOR_START : TF_ANCHOR_END);
          if (left) part->ShiftStart(ec, -128, &moved, nullptr);
          else part->ShiftEnd(ec, 128, &moved, nullptr);
          wchar_t buffer[128]{}; ULONG count = 0;
          if (SUCCEEDED(part->GetText(ec, 0, buffer, 128, &count))) value = Utf8FromW(buffer, count);
          part->Release();
        }
        return value;
      };
      session_.set_context(surrounding(true), surrounding(false)); range->Release();
    }
  }
  SyncCandidateWindow(pic);
  return S_OK;
}

HRESULT TextService::RequestState(ITfContext* pic, const ime::Session& next, const std::string& text,
                                 bool finish, bool cancel, bool async) {
  if (!pic) return E_INVALIDARG;
  auto edit = new (std::nothrow) StateEditSession(this, pic, session_.decode_input(), next, text, finish, cancel);
  if (!edit) return E_OUTOFMEMORY;
  HRESULT result = E_FAIL;
  HRESULT hr = pic->RequestEditSession(client_id_, edit,
      TF_ES_READWRITE | (async ? TF_ES_ASYNC : TF_ES_SYNC), &result);
  edit->Release();
  return FAILED(hr) ? hr : result;
}
HRESULT TextService::EndComposition(ITfContext* pic, bool commit) {
  if (!composition_) return S_OK;
  auto next = session_; auto text = next.visible_text(); next.Reset();
  return RequestState(pic, next, text, commit, !commit);
}
void TextService::ReleaseCompositionRef() {
  if (composition_) { composition_->Release(); composition_ = nullptr; }
  has_composition_ = false;
}
void TextService::ResetSessionState() { session_.Reset(); caret_valid_ = false; cand_window_.Hide(); }

bool TextService::StartEngine() {
  if (!engine_channel_.running() && GetTickCount64() >= retry_engine_at_) {
    wchar_t path[32768]{}; GetModuleFileNameW(g_hInstance, path, 32768);
    auto exe = std::filesystem::path(path).parent_path() / L"engine" / L"ime_engine_host.exe";
    engine_channel_.Start(exe.wstring());
    retry_engine_at_ = GetTickCount64() + 10000;
    submitted_revision_ = -1;
  }
  return engine_channel_.running();
}
void TextService::ResolveForCommit(ime::Session* next) {
  if (next->candidates_ready() || !next->composing() || !StartEngine()) return;
  engine_channel_.Submit(next->decode_input());
  const auto deadline = GetTickCount64() + 3000;
  while (engine_channel_.running() && GetTickCount64() < deadline && !next->candidates_ready()) {
    ime::DecodeInput request; std::vector<ime::Candidate> candidates;
    if (engine_channel_.Poll(&request, &candidates)) next->ApplyCandidates(request, std::move(candidates));
    if (!next->candidates_ready()) Sleep(1);
  }
}
void TextService::PollEngine() {
  if (!activated_ || !foreground_ || restricted_ || !context_) return;
  if (!session_.composing()) {
    ime::DecodeInput ignored; std::vector<ime::Candidate> candidates;
    engine_channel_.Poll(&ignored, &candidates);
    return;
  }
  StartEngine();
  if (session_.revision() != submitted_revision_ || session_.interaction_generation() != submitted_interaction_ ||
      session_.context_generation() != submitted_context_) {
    engine_channel_.Submit(session_.decode_input());
    submitted_revision_ = session_.revision(); submitted_interaction_ = session_.interaction_generation();
    submitted_context_ = session_.context_generation();
  }
  ime::DecodeInput request; std::vector<ime::Candidate> result;
  if (engine_channel_.Poll(&request, &result)) {
    auto next = session_;
    if (next.ApplyCandidates(request, std::move(result)))
      RequestState(context_, next, next.visible_text(), false, false, true);
  }
}
void TextService::ClickCandidate(int index) {
  if (!context_ || !foreground_) return;
  auto next = session_;
  if (!next.SelectCandidate(index)) return;
  auto text = next.visible_text(); next.PressEnter();
  RequestState(context_, next, text, true, false, true);
}

std::string TextService::Utf8FromW(LPWSTR ws, int len) const {
  if (!ws || len <= 0) return "";
  int bytes = WideCharToMultiByte(CP_UTF8, 0, ws, len, nullptr, 0, nullptr, nullptr);
  if (bytes <= 0) return "";
  std::string out(static_cast<size_t>(bytes), '\0');
  WideCharToMultiByte(CP_UTF8, 0, ws, len, out.data(), bytes, nullptr, nullptr);
  return out;
}

std::wstring TextService::WFromUtf8(const std::string& s) const {
  if (s.empty()) return L"";
  int chars = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  if (chars <= 0) return L"";
  std::wstring out(static_cast<size_t>(chars), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), chars);
  return out;
}

// --- IUnknown ---
STDMETHODIMP TextService::QueryInterface(REFIID riid, void** ppv) {
  if (!ppv) return E_POINTER;
  *ppv = nullptr;
  if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfTextInputProcessor)) {
    *ppv = static_cast<ITfTextInputProcessor*>(this);
  } else if (IsEqualIID(riid, IID_ITfTextInputProcessorEx)) {
    *ppv = static_cast<ITfTextInputProcessorEx*>(this);
  } else if (IsEqualIID(riid, IID_ITfKeyEventSink)) {
    *ppv = static_cast<ITfKeyEventSink*>(this);
  } else if (IsEqualIID(riid, IID_ITfThreadMgrEventSink)) {
    *ppv = static_cast<ITfThreadMgrEventSink*>(this);
  } else if (IsEqualIID(riid, IID_ITfCompositionSink)) {
    *ppv = static_cast<ITfCompositionSink*>(this);
  } else if (IsEqualIID(riid, IID_ITfDisplayAttributeProvider)) {
    *ppv = static_cast<ITfDisplayAttributeProvider*>(this);
  } else {
    return E_NOINTERFACE;
  }
  AddRef();
  return S_OK;
}

STDMETHODIMP_(ULONG) TextService::AddRef() { return static_cast<ULONG>(InterlockedIncrement(&ref_)); }
STDMETHODIMP_(ULONG) TextService::Release() {
  LONG r = InterlockedDecrement(&ref_);
  if (r == 0) delete this;
  return static_cast<ULONG>(r);
}

TextService::TextService() { ModuleAddRef(); session_.set_deferred_decoding(true); session_.set_learning_allowed(false); }
TextService::~TextService() {
  if (lang_bar_mgr_ && input_mode_menu_) lang_bar_mgr_->RemoveItem(input_mode_menu_);
  if (input_mode_menu_) { input_mode_menu_->Release(); input_mode_menu_ = nullptr; }
  if (lang_bar_mgr_) { lang_bar_mgr_->Release(); lang_bar_mgr_ = nullptr; }
  engine_channel_.FinishLearning(250);
  engine_channel_.Stop();
  ResetSessionState();
  ReleaseCompositionRef();
  if (context_) {
    context_->Release();
    context_ = nullptr;
  }
  cand_window_.Destroy();
  if (thread_mgr_) {
    UninitThreadMgrSink();
    ITfKeystrokeMgr* keys = nullptr;
    if (SUCCEEDED(thread_mgr_->QueryInterface(IID_ITfKeystrokeMgr, reinterpret_cast<void**>(&keys))) &&
        keys) {
      keys->UnadviseKeyEventSink(client_id_);
      keys->Release();
    }
    thread_mgr_->Release();
    thread_mgr_ = nullptr;
  }
  ModuleRelease();
}

HRESULT TextService::Activate(ITfThreadMgr* ptim, TfClientId tid) {
  g_diagnostics.activate_flags.store(0);
  return ActivateInternal(ptim, tid);
}

HRESULT TextService::ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD flags) {
  g_diagnostics.activate_flags.store(flags);
  restricted_ = (flags & (TF_TMAE_SECUREMODE | TF_TMAE_COMLESS)) != 0;
  return ActivateInternal(ptim, tid);
}

HRESULT TextService::ActivateInternal(ITfThreadMgr* ptim, TfClientId tid) {
  g_diagnostics.activate_calls.fetch_add(1);
  TipLog("ActivateInternal enter tid=%ld already=%d", static_cast<long>(tid), activated_ ? 1 : 0);
  if (activated_) return S_OK;
  if (!ptim) return E_INVALIDARG;
  if (thread_mgr_) {
    thread_mgr_->Release();
    thread_mgr_ = nullptr;
  }
  thread_mgr_ = ptim;
  thread_mgr_->AddRef();
  client_id_ = tid;

  ITfKeystrokeMgr* keys = nullptr;
  HRESULT hr = thread_mgr_->QueryInterface(IID_ITfKeystrokeMgr, reinterpret_cast<void**>(&keys));
  if (FAILED(hr)) {
    TipLog("QI KeystrokeMgr failed 0x%08lx", hr);
    thread_mgr_->Release();
    thread_mgr_ = nullptr;
    client_id_ = TF_CLIENTID_NULL;
    return hr;
  }
  hr = keys->AdviseKeyEventSink(client_id_, static_cast<ITfKeyEventSink*>(this), TRUE);
  g_diagnostics.advise_result.store(hr);
  keys->Release();
  TipLog("AdviseKeyEventSink(fg) hr=0x%08lx", hr);
  if (FAILED(hr)) {
    thread_mgr_->Release();
    thread_mgr_ = nullptr;
    client_id_ = TF_CLIENTID_NULL;
    return hr;
  }
  thread_key_sink_cookie_ = 1;  // keystroke sink keyed by client_id; mark advised

  InitThreadMgrSink();

  ITfCategoryMgr* catmgr = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_ITfCategoryMgr, reinterpret_cast<void**>(&catmgr))) &&
      catmgr) {
    TfGuidAtom atom = TF_INVALID_GUIDATOM;
    if (SUCCEEDED(catmgr->RegisterGUID(kDisplayAttributeGuid, &atom))) display_attr_atom_ = atom;
    catmgr->Release();
  }

  cand_window_.SetService(this);
  cand_window_.Create(g_hInstance, nullptr);
  if (cand_window_.hwnd()) SetTimer(cand_window_.hwnd(), 1, 25, nullptr);
  HRESULT langbar_hr = thread_mgr_->QueryInterface(IID_ITfLangBarItemMgr,
      reinterpret_cast<void**>(&lang_bar_mgr_));
  g_diagnostics.langbar_manager_result.store(langbar_hr);
  if (SUCCEEDED(langbar_hr) && lang_bar_mgr_) {
    input_mode_menu_ = new (std::nothrow) InputModeMenu();
    if (input_mode_menu_) {
      HRESULT add_hr = lang_bar_mgr_->AddItem(input_mode_menu_);
      g_diagnostics.langbar_add_result.store(add_hr);
      if (FAILED(add_hr)) {
        input_mode_menu_->Release(); input_mode_menu_ = nullptr;
      }
    }
  }
  activated_ = true;
  if (!restricted_) StartEngine();
  g_diagnostics.activate_success.fetch_add(1);
  TipLog("ActivateInternal done client=%ld", static_cast<long>(client_id_));
  return S_OK;
}

HRESULT TextService::Deactivate() {
  TipLog("Deactivate enter activated=%d", activated_ ? 1 : 0);
  if (!activated_) return S_OK;
  engine_channel_.FinishLearning(250);
  engine_channel_.Stop();
  if (cand_window_.hwnd()) KillTimer(cand_window_.hwnd(), 1);
  if (composition_) {
    if (context_) {
      EndComposition(context_, false);
    }
    if (composition_) {
      // 編集セッションを実行できなかった場合も通知先の参照を解放する。
      TipLog("Deactivate force-release composition");
      ReleaseCompositionRef();
    }
    has_composition_ = false;
  }
  if (thread_mgr_) {
    if (lang_bar_mgr_ && input_mode_menu_) lang_bar_mgr_->RemoveItem(input_mode_menu_);
    if (input_mode_menu_) { input_mode_menu_->Release(); input_mode_menu_ = nullptr; }
    if (lang_bar_mgr_) { lang_bar_mgr_->Release(); lang_bar_mgr_ = nullptr; }
    ITfKeystrokeMgr* keys = nullptr;
    if (SUCCEEDED(thread_mgr_->QueryInterface(IID_ITfKeystrokeMgr, reinterpret_cast<void**>(&keys))) &&
        keys) {
      keys->UnadviseKeyEventSink(client_id_);
      keys->Release();
    }
    thread_key_sink_cookie_ = TF_INVALID_COOKIE;
    UninitThreadMgrSink();
    thread_mgr_->Release();
    thread_mgr_ = nullptr;
    client_id_ = TF_CLIENTID_NULL;
  }
  if (context_) {
    context_->Release();
    context_ = nullptr;
  }
  ResetSessionState();
  cand_window_.Destroy();
  activated_ = false;
  TipLog("Deactivate done");
  return S_OK;
}

HRESULT TextService::InitThreadMgrSink() {
  if (!thread_mgr_ || thread_mgr_sink_cookie_ != TF_INVALID_COOKIE) return S_OK;
  ITfSource* src = nullptr;
  HRESULT hr = thread_mgr_->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(&src));
  if (FAILED(hr)) return hr;
  hr = src->AdviseSink(IID_ITfThreadMgrEventSink, static_cast<ITfThreadMgrEventSink*>(this),
                       &thread_mgr_sink_cookie_);
  src->Release();
  if (FAILED(hr)) thread_mgr_sink_cookie_ = TF_INVALID_COOKIE;
  return hr;
}

HRESULT TextService::UninitThreadMgrSink() {
  if (!thread_mgr_ || thread_mgr_sink_cookie_ == TF_INVALID_COOKIE) return S_OK;
  ITfSource* src = nullptr;
  if (SUCCEEDED(thread_mgr_->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(&src))) && src) {
    src->UnadviseSink(thread_mgr_sink_cookie_);
    src->Release();
  }
  thread_mgr_sink_cookie_ = TF_INVALID_COOKIE;
  return S_OK;
}

// キー入力。
STDMETHODIMP TextService::OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM, BOOL* pfEaten) {
  g_diagnostics.test_key_down.fetch_add(1);
  if (!pfEaten) return E_POINTER;
  *pfEaten = FALSE;
  try {
    BOOL eaten = FALSE;
    HRESULT hr = HandleKey(pic, wParam, &eaten, true);
    if (SUCCEEDED(hr)) *pfEaten = eaten;
    return hr;
  } catch (...) {
    *pfEaten = FALSE;
    return E_FAIL;
  }
}

STDMETHODIMP TextService::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM, BOOL* pfEaten) {
  g_diagnostics.key_down.fetch_add(1);
  if (!pfEaten) return E_POINTER;
  *pfEaten = FALSE;
  try {
    BOOL eaten = FALSE;
    HRESULT hr = HandleKey(pic, wParam, &eaten, false);
    if (SUCCEEDED(hr)) *pfEaten = eaten;
    TipLog("OnKeyDown vk=0x%lx hr=0x%08lx eaten=%d", static_cast<unsigned long>(wParam), hr,
           eaten ? 1 : 0);
    return hr;
  } catch (...) {
    *pfEaten = FALSE;
    return E_FAIL;
  }
}

STDMETHODIMP TextService::OnTestKeyUp(ITfContext*, WPARAM, LPARAM, BOOL* pfEaten) {
  if (pfEaten) *pfEaten = FALSE;
  return S_OK;
}

STDMETHODIMP TextService::OnKeyUp(ITfContext*, WPARAM, LPARAM, BOOL* pfEaten) {
  if (pfEaten) *pfEaten = FALSE;
  return S_OK;
}

STDMETHODIMP TextService::OnPreservedKey(ITfContext*, REFGUID, BOOL* pfEaten) {
  if (pfEaten) *pfEaten = FALSE;
  return S_OK;
}

STDMETHODIMP TextService::OnSetFocus(BOOL fForeground) {
  foreground_ = fForeground != FALSE;
  if (foreground_) retry_engine_at_ = 0;
  if (!foreground_) { cand_window_.Hide(); engine_channel_.FinishLearning(250); engine_channel_.Stop(); submitted_revision_ = -1; }
  else if (activated_ && !restricted_) StartEngine();
  return S_OK;
}

HRESULT TextService::HandleKey(ITfContext* pic, WPARAM key, BOOL* eaten, bool test) {
  if (!eaten) return E_POINTER;
  *eaten = FALSE;
  if (!pic) { g_diagnostics.gated_no_context.fetch_add(1); return S_OK; }
  if (restricted_) { g_diagnostics.gated_restricted.fetch_add(1); return S_OK; }
  if (!foreground_) { g_diagnostics.gated_background.fetch_add(1); return S_OK; }
  if ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000) ||
      (GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000)) return S_OK;
  if (!CompartmentEnabled(thread_mgr_, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, true) ||
      CompartmentEnabled(pic, GUID_COMPARTMENT_KEYBOARD_DISABLED, false) ||
      CompartmentEnabled(pic, GUID_COMPARTMENT_EMPTYCONTEXT, false)) {
    g_diagnostics.gated_compartment.fetch_add(1);
    return S_OK;
  }
  TF_STATUS status{};
  if (SUCCEEDED(pic->GetStatus(&status)) && (status.dwDynamicFlags & TS_SD_READONLY)) {
    g_diagnostics.gated_readonly.fetch_add(1);
    return S_OK;
  }
  bool composing = session_.composing(), converting = session_.is_converting();
  bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  bool operation = key == VK_BACK || key == VK_DELETE || key == VK_ESCAPE || key == VK_RETURN ||
      key == VK_SPACE || key == VK_LEFT || key == VK_RIGHT || key == VK_HOME || key == VK_END ||
      (key >= VK_F6 && key <= VK_F10) || key == VK_CONVERT;
  bool candidate_key = converting && (key == VK_UP || key == VK_DOWN || key == VK_TAB ||
      key == VK_PRIOR || key == VK_NEXT || (!shift && key >= '1' && key <= '9'));
  std::string append;
  if (!operation && !candidate_key) {
    BYTE state[256]{}; wchar_t chars[8]{};
    if (!GetKeyboardState(state)) { g_diagnostics.gated_no_character.fetch_add(1); return S_OK; }
    // フラグ4でデッドキーの状態を保ったまま文字を取得する。
    auto layout = GetKeyboardLayout(0);
    int count = ToUnicodeEx(static_cast<UINT>(key), MapVirtualKeyExW(static_cast<UINT>(key), MAPVK_VK_TO_VSC, layout),
                            state, chars, 8, 4, layout);
    if (count <= 0 || chars[0] < L' ') {
      g_diagnostics.gated_no_character.fetch_add(1);
      return S_OK;
    }
    append = Utf8FromW(chars, count);
  }
  if ((operation && !composing) || (!operation && !candidate_key && append.empty())) return S_OK;
  if (test) { *eaten = TRUE; g_diagnostics.test_eaten.fetch_add(1); return S_OK; }
  if (context_ != pic) {
    if (composition_) return S_OK;  // Never apply an old composition to another document.
    if (context_) context_->Release(); context_ = pic; context_->AddRef();
    ResetSessionState();
  }
  auto next = session_; bool finish = false, cancel = false;
  if (!append.empty()) {
    if (converting) {
      auto committed = session_; auto text = committed.visible_text(); committed.PressEnter();
      HRESULT hr = RequestState(pic, committed, text, true);
      if (hr != S_OK) { *eaten = TRUE; return S_OK; }
      next = session_;
    }
    next.Type(append);
  } else if (key >= VK_F6 && key <= VK_F10) next.PressCharacterClass(static_cast<int>(key));
  else if (key == VK_BACK) next.Backspace();
  else if (key == VK_DELETE) next.DeleteForward();
  else if (key == VK_ESCAPE) next.PressEscape();
  else if (key == VK_RETURN) { ResolveForCommit(&next); finish = true; }
  else if (key == VK_SPACE || key == VK_CONVERT) { if (shift) next.PressShiftSpace(); else next.PressSpace(); }
  else if (key == VK_LEFT) next.PressLeft();
  else if (key == VK_RIGHT) next.PressRight();
  else if (key == VK_HOME) next.PressHome();
  else if (key == VK_END) next.PressEnd();
  else if (key == VK_UP || key == VK_PRIOR || (key == VK_TAB && shift)) next.PressUp();
  else if (key == VK_DOWN || key == VK_NEXT || key == VK_TAB) next.PressDown();
  else if (key >= '1' && key <= '9') next.SelectCandidate(static_cast<int>(key - '1'));
  auto text = next.visible_text();
  if (finish) next.PressEnter();
  else cancel = !next.composing();
  HRESULT hr = RequestState(pic, next, text, finish, cancel);
  if (hr == S_OK) g_diagnostics.state_success.fetch_add(1);
  else g_diagnostics.state_failure.fetch_add(1);
  // 編集失敗時は元の状態を維持し、処理したキーをアプリへ重ねて渡さない。
  *eaten = (hr == S_OK || composing) ? TRUE : FALSE;
  return S_OK;
}

void TextService::SyncCandidateWindow(ITfContext*) {
  if (!foreground_ || !session_.composing() || !caret_valid_) { cand_window_.Hide(); return; }
  cand_window_.Show(session_.candidates(), session_.selected_index(), caret_point_);
}

// TSFスレッドと未確定文字列。
STDMETHODIMP TextService::OnInitDocumentMgr(ITfDocumentMgr*) { return S_OK; }
STDMETHODIMP TextService::OnUninitDocumentMgr(ITfDocumentMgr*) { return S_OK; }
STDMETHODIMP TextService::OnSetFocus(ITfDocumentMgr* pdimFocus, ITfDocumentMgr*) {
  try {
    if (!pdimFocus) {
      cand_window_.Hide();
      return S_OK;
    }
    ITfContext* ctx = nullptr;
    if (SUCCEEDED(pdimFocus->GetTop(&ctx)) && ctx) {
      if (context_ && context_ != ctx) {
        if (composition_) EndComposition(context_, true);
        ReleaseCompositionRef(); ResetSessionState();
        context_->Release();
        context_ = nullptr;
      }
      if (!context_) {
        context_ = ctx;
        context_->AddRef();
      }
      ctx->Release();
    }
    return S_OK;
  } catch (...) {
    return E_FAIL;
  }
}
STDMETHODIMP TextService::OnPushContext(ITfContext*) { return S_OK; }
STDMETHODIMP TextService::OnPopContext(ITfContext* pic) {
  if (context_ == pic) {
    if (composition_) EndComposition(pic, true);
    ReleaseCompositionRef(); ResetSessionState();
    context_->Release();
    context_ = nullptr;
  }
  return S_OK;
}

STDMETHODIMP TextService::OnCompositionTerminated(TfEditCookie, ITfComposition* pComp) {
  if (ending_) return S_OK;
  TipLog("OnCompositionTerminated comp=%p ours=%p", static_cast<void*>(pComp),
         static_cast<void*>(composition_));
  if (composition_ && pComp && composition_ != pComp) {
    // 他のIMEの未確定文字列なら、このセッションは変更しない。
    return S_OK;
  }
  ReleaseCompositionRef();
  has_composition_ = false;
  if (session_.composing()) {
    session_.Reset();
    TipLog("OnCompositionTerminated session cancelled");
  }
  cand_window_.Hide();
  return S_OK;
}

STDMETHODIMP TextService::EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) {
  if (!ppEnum) return E_POINTER;
  *ppEnum = new (std::nothrow) DisplayAttributeEnum(0);
  return *ppEnum ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP TextService::GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) {
  if (!ppInfo) return E_POINTER;
  *ppInfo = nullptr;
  if (IsEqualGUID(guid, kDisplayAttributeGuid)) {
    *ppInfo = new (std::nothrow) DisplayAttributeInfo();
    return *ppInfo ? S_OK : E_OUTOFMEMORY;
  }
  return E_NOINTERFACE;
}

// --- ClassFactory ---
ClassFactory::ClassFactory() { ModuleAddRef(); }
ClassFactory::~ClassFactory() { ModuleRelease(); }

STDMETHODIMP ClassFactory::QueryInterface(REFIID riid, void** ppv) {
  if (!ppv) return E_POINTER;
  *ppv = nullptr;
  if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
    *ppv = static_cast<IClassFactory*>(this);
    AddRef();
    return S_OK;
  }
  return E_NOINTERFACE;
}
STDMETHODIMP_(ULONG) ClassFactory::AddRef() { return static_cast<ULONG>(InterlockedIncrement(&ref_)); }
STDMETHODIMP_(ULONG) ClassFactory::Release() {
  LONG r = InterlockedDecrement(&ref_);
  if (r == 0) delete this;
  return static_cast<ULONG>(r);
}
STDMETHODIMP ClassFactory::CreateInstance(IUnknown* outer, REFIID riid, void** ppv) {
  if (outer) return CLASS_E_NOAGGREGATION;
  if (!ppv) return E_POINTER;
  *ppv = nullptr;
  auto* obj = new (std::nothrow) TextService();
  if (!obj) return E_OUTOFMEMORY;
  HRESULT hr = obj->QueryInterface(riid, ppv);
  obj->Release();
  return hr;
}
STDMETHODIMP ClassFactory::LockServer(BOOL fLock) {
  if (fLock) ModuleAddRef();
  else ModuleRelease();
  return S_OK;
}

// --- DisplayAttributeInfo ---
DisplayAttributeInfo::DisplayAttributeInfo() { ModuleAddRef(); }
DisplayAttributeInfo::~DisplayAttributeInfo() { ModuleRelease(); }

STDMETHODIMP DisplayAttributeInfo::QueryInterface(REFIID riid, void** ppv) {
  if (!ppv) return E_POINTER;
  *ppv = nullptr;
  if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfDisplayAttributeInfo)) {
    *ppv = static_cast<ITfDisplayAttributeInfo*>(this);
    AddRef();
    return S_OK;
  }
  return E_NOINTERFACE;
}
STDMETHODIMP_(ULONG) DisplayAttributeInfo::AddRef() { return static_cast<ULONG>(InterlockedIncrement(&ref_)); }
STDMETHODIMP_(ULONG) DisplayAttributeInfo::Release() {
  LONG r = InterlockedDecrement(&ref_);
  if (r == 0) delete this;
  return static_cast<ULONG>(r);
}
STDMETHODIMP DisplayAttributeInfo::GetGUID(GUID* pguid) {
  if (!pguid) return E_POINTER;
  *pguid = kDisplayAttributeGuid;
  return S_OK;
}
STDMETHODIMP DisplayAttributeInfo::GetDescription(BSTR* pbstrDesc) {
  if (!pbstrDesc) return E_POINTER;
  *pbstrDesc = SysAllocString(L"IME Mixed composition");
  return *pbstrDesc ? S_OK : E_OUTOFMEMORY;
}
STDMETHODIMP DisplayAttributeInfo::GetAttributeInfo(TF_DISPLAYATTRIBUTE* pda) {
  if (!pda) return E_POINTER;
  ZeroMemory(pda, sizeof(*pda));
  pda->crText.type = TF_CT_NONE;
  pda->crBk.type = TF_CT_NONE;
  pda->lsStyle = TF_LS_SOLID;
  pda->fBoldLine = FALSE;
  pda->crLine.type = TF_CT_NONE;
  pda->bAttr = TF_ATTR_TARGET_CONVERTED;
  return S_OK;
}
STDMETHODIMP DisplayAttributeInfo::SetAttributeInfo(const TF_DISPLAYATTRIBUTE*) {
  return E_NOTIMPL;
}
STDMETHODIMP DisplayAttributeInfo::Reset() {
  return S_OK;
}

// --- Enum ---
DisplayAttributeEnum::DisplayAttributeEnum(ULONG index) : index_(index) { ModuleAddRef(); }
DisplayAttributeEnum::~DisplayAttributeEnum() { ModuleRelease(); }

STDMETHODIMP DisplayAttributeEnum::QueryInterface(REFIID riid, void** ppv) {
  if (!ppv) return E_POINTER;
  *ppv = nullptr;
  if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IEnumTfDisplayAttributeInfo)) {
    *ppv = static_cast<IEnumTfDisplayAttributeInfo*>(this);
    AddRef();
    return S_OK;
  }
  return E_NOINTERFACE;
}
STDMETHODIMP_(ULONG) DisplayAttributeEnum::AddRef() { return static_cast<ULONG>(InterlockedIncrement(&ref_)); }
STDMETHODIMP_(ULONG) DisplayAttributeEnum::Release() {
  LONG r = InterlockedDecrement(&ref_);
  if (r == 0) delete this;
  return static_cast<ULONG>(r);
}
STDMETHODIMP DisplayAttributeEnum::Clone(IEnumTfDisplayAttributeInfo** ppEnum) {
  if (!ppEnum) return E_POINTER;
  *ppEnum = new (std::nothrow) DisplayAttributeEnum(index_);
  return *ppEnum ? S_OK : E_OUTOFMEMORY;
}
STDMETHODIMP DisplayAttributeEnum::Reset() {
  index_ = 0;
  return S_OK;
}
STDMETHODIMP DisplayAttributeEnum::Next(ULONG ulCount, ITfDisplayAttributeInfo** ppInfo, ULONG* pcFetched) {
  if (!ppInfo) return E_POINTER;
  ULONG fetched = 0;
  while (fetched < ulCount && index_ == 0) {
    ppInfo[fetched] = new (std::nothrow) DisplayAttributeInfo();
    if (!ppInfo[fetched]) {
      if (pcFetched) *pcFetched = fetched;
      return E_OUTOFMEMORY;
    }
    ++fetched;
    index_ = 1;
  }
  if (pcFetched) *pcFetched = fetched;
  return fetched == ulCount ? S_OK : S_FALSE;
}
STDMETHODIMP DisplayAttributeEnum::Skip(ULONG ulCount) {
  index_ += ulCount;
  return S_OK;
}

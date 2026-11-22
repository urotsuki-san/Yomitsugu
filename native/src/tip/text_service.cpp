#include "tip.h"
#include <ime.h>
#include <shlobj.h>
#include "alternate_n.h"
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
constexpr GUID kToggleKeyboardMode = {0xbb0997b5,0xbc52,0x442c,{0xa2,0xbb,0x6d,0xbb,0xbb,0xdd,0x38,0x0d}};
constexpr TF_PRESERVEDKEY kDefaultModeKeys[] = {
  {VK_CAPITAL, TF_MOD_SHIFT | TF_MOD_ON_KEYUP},
  {VK_DBE_ALPHANUMERIC, TF_MOD_SHIFT | TF_MOD_ON_KEYUP},
  {VK_IME_OFF, TF_MOD_SHIFT | TF_MOD_ON_KEYUP},
  {VK_DBE_DBCSCHAR, TF_MOD_ON_KEYUP},
  {VK_DBE_SBCSCHAR, TF_MOD_ON_KEYUP},
  {VK_KANJI, TF_MOD_ON_KEYUP},
  {VK_OEM_3, TF_MOD_ALT | TF_MOD_ON_KEYUP},
};
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
// フォーカス通知中に同期編集できなくても、以前の入力先の未確定状態を終了する。
class CompositionEndSession : public ITfEditSession {
 public:
  explicit CompositionEndSession(ITfComposition* composition) : composition_(composition) {
    composition_->AddRef(); ModuleAddRef();
  }
  ~CompositionEndSession() { composition_->Release(); ModuleRelease(); }
  STDMETHODIMP QueryInterface(REFIID id, void** out) override {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (id != IID_IUnknown && id != IID_ITfEditSession) return E_NOINTERFACE;
    *out = static_cast<ITfEditSession*>(this); AddRef(); return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
  STDMETHODIMP_(ULONG) Release() override { auto r = InterlockedDecrement(&refs_); if (!r) delete this; return r; }
  STDMETHODIMP DoEditSession(TfEditCookie ec) override { return composition_->EndComposition(ec); }
 private:
  LONG refs_ = 1;
  ITfComposition* composition_;
};
class StateEditSession : public ITfEditSession {
 public:
  StateEditSession(TextService* svc, ITfContext* pic, ime::DecodeInput expected,
                   ime::Session next, std::string text, bool finish, bool cancel, uint64_t token)
      : svc_(svc), pic_(pic), expected_(std::move(expected)), next_(std::move(next)),
        text_(std::move(text)), finish_(finish), cancel_(cancel), retry_token_(token) { svc_->AddRef(); pic_->AddRef(); }
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
    HRESULT result = E_FAIL;
    try { result = svc_->ApplyState(ec, pic_, expected_, next_, text_, finish_, cancel_, retry_token_); }
    catch (...) {}
    svc_->EditCompleted(expected_,finish_);
    return result;
  }
 private:
  LONG refs_ = 1; TextService* svc_; ITfContext* pic_;
  ime::DecodeInput expected_; ime::Session next_; std::string text_; bool finish_, cancel_; uint64_t retry_token_;
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
  return has_composition_ ? S_OK : (FAILED(hr) ? hr : E_FAIL);
}

HRESULT TextService::EditSetText(TfEditCookie ec, ITfContext* pic, const std::string& text,
                               const std::string& caret) {
  const bool same_surface = composition_ && session_.visible_text() == text;
  HRESULT hr = EditStart(ec, pic);
  if (FAILED(hr)) return hr;
  ITfRange* range = nullptr;
  hr = composition_->GetRange(&range);
  if (FAILED(hr) || !range) return E_FAIL;
  auto wide = WFromUtf8(text);
  // A streamed result or Enter can confirm the text already in the document.
  // Read the active range under the same edit lock in case the app changed it.
  wchar_t current[256];
  ULONG copied = 0;
  const bool matches = same_surface && !wide.empty() && wide.size() < std::size(current) &&
      SUCCEEDED(range->GetText(ec, 0, current, static_cast<ULONG>(wide.size() + 1), &copied)) &&
      copied == wide.size() && std::equal(wide.begin(), wide.end(), current);
  if (!matches) hr = range->SetText(ec, 0, wide.c_str(), static_cast<LONG>(wide.size()));
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
      caret_valid_ = false;
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
                               ime::Session& next, const std::string& text, bool finish, bool cancel, uint64_t retry_token) {
  const auto now = session_.decode_input();
  if (!activated_ || pic != context_ || now.revision != expected.revision ||
      now.context_generation != expected.context_generation ||
      now.interaction_generation != expected.interaction_generation) return S_FALSE;
  if (!finish && !cancel && next.revision()==now.revision &&
      next.interaction_generation()==now.interaction_generation &&
      next.context_generation()==now.context_generation &&
      !next.candidates_ready() && (session_.candidates_ready() || HasQueuedFinal() ||
          session_.refinement_preview_sequence()>next.refinement_preview_sequence())) return S_FALSE;
  if ((!foreground_ || !document_focused_) && !finish && !cancel) return S_FALSE;
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
  if (retry_token) {
    if (!HasQueuedFinal() || queued_final_->retry_token!=retry_token) return S_FALSE;
    // The application can replace its composition range (for example, on
    // paste) while TSF withholds the lock. Compare under this edit's lock.
    ITfRange* active=nullptr;
    const auto before=WFromUtf8(session_.visible_text());
    std::wstring actual(before.size()+1,L'\0');
    ULONG count=0;
    const bool current=composition_ && SUCCEEDED(composition_->GetRange(&active)) && active &&
        SUCCEEDED(active->GetText(ec,0,actual.data(),static_cast<ULONG>(actual.size()),&count)) &&
        count==before.size() && std::equal(before.begin(),before.end(),actual.begin());
    if (active) active->Release();
    if (!current) {
      EditEnd(ec,pic,true); // Leave the application's replacement text intact.
      ResetSessionState();
      return S_FALSE;
    }
  }
  HRESULT hr = S_OK;
  if (!cancel) {
    // Tab can arrive after the worker reply but before Chrome grants the edit.
    const bool moved = !finish && session_.is_converting() && !session_.candidates_ready() &&
        next.candidates_ready() && next.interaction_generation() == now.interaction_generation &&
        next.SelectPendingTab(session_.pending_tab_steps());
    if (moved) hr = EditSetText(ec, pic, next.visible_text(), next.caret_prefix());
    else hr = EditSetText(ec, pic, text, finish ? text : next.caret_prefix());
  }
  if (SUCCEEDED(hr) && (finish || cancel)) hr = EditEnd(ec, pic, !cancel);
  if (FAILED(hr)) return hr;
  const bool selected_conversion = session_.candidates_ready() && session_.selected_index() >= 0 &&
      session_.selected_index() < static_cast<int>(session_.candidates().size()) && !session_.candidates()[session_.selected_index()].is_raw;
  if (finish && !cancel && session_.composing() && (next.last_commit_f10() || next.last_commit_learnable() || selected_conversion)) {
    auto learned = now; learned.field = field;
    const auto& shown = session_.candidates();
    const bool chose_alternative = next.last_commit_explicit() && session_.candidates_ready() && !shown.empty() &&
        shown.front().output_text != text && std::any_of(shown.begin()+1,shown.end(),
          [&](const auto& candidate){return !candidate.is_raw && candidate.output_text == text;});
    const auto previous = chose_alternative ? shown.front().output_text : std::string{};
    std::uint8_t alternate_n = 0;
    if (chose_alternative && learned.raw_text.find("nn") != std::string::npos) {
      const auto selected = std::find_if(shown.begin(), shown.end(), [&](const auto& c) {
        return !c.is_raw && c.output_text == text;
      });
      std::string standard;
      if (selected != shown.end() && ime::ConvertRomajiChecked(learned.raw_text, &standard))
        alternate_n = ime::SyllabicNPosition(standard, selected->reading_text);
    }
    if (StartEngine()) engine_channel_.Learn(learned, text, next.last_commit_explicit(), next.last_commit_f10(), previous, alternate_n);
  }
  session_ = next;
  if (session_.candidates_ready() || !HasQueuedFinal()) queued_final_.reset();
  if (finish || cancel) pending_commit_ = false;
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
  if (engine_channel_.may_cancel()) engine_channel_.CancelObsolete(session_.decode_input());
  SyncCandidateWindow(pic);
  return S_OK;
}

HRESULT TextService::RequestState(ITfContext* pic, const ime::Session& next, const std::string& text,
                                 bool finish, bool cancel, bool async) {
  if (!pic) return E_INVALIDARG;
  const bool applying_final=async && !finish && !cancel && !next.typing() &&
      next.candidates_ready() && next.revision()==session_.revision() &&
      next.context_generation()==session_.context_generation() &&
      next.interaction_generation()==session_.interaction_generation();
  const auto token=((applying_final || (finish && pending_commit_)) && HasQueuedFinal())
      ? queued_final_->retry_token : 0;
  auto edit = new (std::nothrow) StateEditSession(this, pic, session_.decode_input(), next, text, finish, cancel, token);
  if (!edit) return E_OUTOFMEMORY;
  HRESULT result = E_FAIL;
  HRESULT hr = pic->RequestEditSession(client_id_, edit,
      TF_ES_READWRITE | (async ? TF_ES_ASYNC : TF_ES_SYNC), &result);
  edit->Release();
  // TSF can hold the document lock after the worker has finished. Keep the
  // confirmed first candidate available to Enter until this edit is applied.
  const auto status=FAILED(hr) ? hr : result;
  if (applying_final && !next.candidates().empty()) {
    const auto make_final=[&] {
      return QueuedFinal{next.revision(),next.context_generation(),next.interaction_generation(),
                         next.candidates().front()};
    };
    if (status==TF_S_ASYNC) {
      queued_final_.emplace(make_final());
      queued_final_->retry_token=token;
      if (next.is_converting()) {
        queued_final_->retry_candidates=next.candidates();
        queued_final_->retry_at=~ULONGLONG{0};
      }
    } else if ((status==E_FAIL || status==TF_E_LOCKED || status==TF_E_SYNCHRONOUS) &&
               pic==context_ && session_.revision()==next.revision() &&
               session_.context_generation()==next.context_generation() &&
               session_.interaction_generation()==next.interaction_generation() &&
               !session_.candidates_ready() && !HasQueuedFinal()) {
      // The host already supplied the complete alternatives. Retain them when
      // the document temporarily refuses a second edit while granting a lock.
      queued_final_.emplace(make_final());
      queued_final_->retry_token=++received_preview_sequence_;
      queued_final_->retry_candidates=next.candidates();
      queued_final_->retry_at=GetTickCount64()+queued_final_->retry_delay;
      cand_window_.SetPollInterval(queued_final_->retry_delay);
    }
  }
  return status;
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
void TextService::DetachComposition() {
  auto edit = composition_ ? new (std::nothrow) CompositionEndSession(composition_) : nullptr;
  ReleaseCompositionRef();
  if (!edit) return;
  if (context_) {
    HRESULT result = E_FAIL;
    context_->RequestEditSession(client_id_, edit, TF_ES_READWRITE | TF_ES_ASYNCDONTCARE, &result);
  }
  edit->Release();
}
void TextService::ResetSessionState() {
  queued_final_.reset();
  session_.Reset();
  if (engine_channel_.may_cancel()) engine_channel_.CancelObsolete(session_.decode_input());
  caret_valid_ = false; cand_window_.Hide();
  first_preview_at_ = last_submit_at_ = 0;
  pending_commit_ = false; commit_edit_revision_ = -1;
}

bool TextService::StartEngine() {
  const bool early=GetTickCount64()<retry_engine_at_;
  if (!engine_channel_.running() && (!early || retry_after_early_exit_)) {
    wchar_t path[32768]{}; GetModuleFileNameW(g_hInstance, path, 32768);
    auto exe = std::filesystem::path(path).parent_path() / L"engine" / L"ime_engine_host.exe";
#ifdef YOMITSUGU_LIVE_TEST
    if (!test_worker_.empty()) engine_channel_.Start(test_worker_,test_profile_,cand_window_.poll_hwnd());
    else
#endif
    engine_channel_.Start(exe.wstring(),{},cand_window_.poll_hwnd());
    // A launched worker can exit before answering its first conversion.
    // Allow one early restart; broken installations retain the normal backoff.
    retry_after_early_exit_ = engine_channel_.running() && !early;
    retry_engine_at_ = GetTickCount64() + 10000;
    submitted_revision_ = -1;
  }
  return engine_channel_.running();
}
bool TextService::HasQueuedFinal() const {
  return queued_final_ && queued_final_->revision==session_.revision() &&
      queued_final_->context==session_.context_generation() &&
      queued_final_->interaction==session_.interaction_generation();
}

bool TextService::BeginPendingCommit() {
  if (!session_.composing()) return false;
  session_.RefineCandidates();
  if (session_.candidates_ready()) return false;
  const bool received = HasQueuedFinal();
  if (!received && !StartEngine()) return false;
  pending_commit_ = true; commit_deadline_ = GetTickCount64() + 3000;
  if (!received) engine_channel_.Submit(session_.decode_input());
  return true;
}
void TextService::ForgetDeferredFinal() {
  if (queued_final_ && queued_final_->retry_token) {
    queued_final_.reset();
    submitted_revision_=-1;
  }
}
void TextService::EditCompleted(const ime::DecodeInput& expected,bool finish) {
  if (finish && commit_edit_revision_==expected.revision) commit_edit_revision_=-1;
  if (HasQueuedFinal() && !queued_final_->retry_candidates.empty() &&
      expected.revision==session_.revision() && expected.context_generation==session_.context_generation() &&
      expected.interaction_generation==session_.interaction_generation()) {
    // The pending edit has been granted. Retry on the UI thread after this
    // callback releases its lock, without waiting for a long backoff timer.
    queued_final_->retry_at=0;
    queued_final_->retry_delay=25;
    cand_window_.SetPollInterval(25);
  }
}
HRESULT TextService::FinishPendingCommit(bool async) {
  if (!pending_commit_ || !context_) return S_FALSE;
  if (async && commit_edit_revision_ == session_.revision()) return TF_S_ASYNC;
  auto next = session_;
  if (!next.candidates_ready() && HasQueuedFinal()) {
    if (queued_final_->retry_candidates.empty())
      next.ApplyCandidates(next.decode_input(), {queued_final_->first});
    else next.ApplyCandidates(next.decode_input(), queued_final_->retry_candidates);
  }
  auto text = next.visible_text(); next.PressEnter();
  commit_edit_revision_ = session_.revision();
  auto hr = RequestState(context_, next, text, true, false, async);
  if (hr != TF_S_ASYNC) commit_edit_revision_ = -1;
  return hr;
}
void TextService::PollEngine() {
  if (!activated_ || !foreground_ || restricted_) return;
  const auto now = GetTickCount64();
  const auto quiet_ms = now - last_key_at_;
  // The document owns the displayed text and candidates. A settled edit can
  // release its worker without changing what Enter or a candidate click selects.
  auto release_idle_worker = [&](bool composing) {
    if (!engine_channel_.running() || engine_channel_.busy() || pending_commit_ || commit_edit_revision_ >= 0)
      return;
    if (!low_memory_event_)
      low_memory_event_ = CreateMemoryResourceNotification(LowMemoryResourceNotification);
    const bool pressure = low_memory_event_ && WaitForSingleObject(low_memory_event_, 0) == WAIT_OBJECT_0;
    if (quiet_ms < 60000 && (!pressure || (composing && quiet_ms < 5000))) return;
    engine_channel_.Stop();
    submitted_revision_ = -1;
    retry_engine_at_ = 0;
  };
  if (!document_focused_ || !context_ || !session_.composing()) {
    ime::DecodeInput ignored;
    std::vector<ime::Candidate> candidates;
    engine_channel_.Poll(&ignored, &candidates);
    if (!engine_channel_.busy()) {
      if (!session_.composing() || (session_.candidates_ready() && !session_.typing()))
        release_idle_worker(session_.composing());
      cand_window_.SetPollInterval(engine_channel_.running() ? 1000 : 0);
    }
    return;
  }
  if (!KeyboardOpen() ||
      CompartmentEnabled(context_, GUID_COMPARTMENT_KEYBOARD_DISABLED, false) ||
      CompartmentEnabled(context_, GUID_COMPARTMENT_EMPTYCONTEXT, false)) {
    cand_window_.Hide();
    cand_window_.SetPollInterval(1000);
    return;
  }
  if (HasQueuedFinal() && !queued_final_->retry_candidates.empty() && now>=queued_final_->retry_at) {
    auto next=session_;
    if (next.ApplyCandidates(session_.decode_input(),queued_final_->retry_candidates)) {
      queued_final_->retry_delay=(std::min<DWORD>)(2*queued_final_->retry_delay,1000);
      queued_final_->retry_at=now+queued_final_->retry_delay;
      cand_window_.SetPollInterval(queued_final_->retry_delay);
      RequestState(context_,next,next.visible_text(),false,false,true);
    } else {
      queued_final_.reset();
    }
  }
  // One late key can still arrive. Give a regular typist another half interval
  // of slack; irregular or newly started input keeps a longer grace period.
  const DWORD refine_after = measured_intervals_ >= 3
      ? std::clamp<DWORD>((3*key_interval_ms_)/2+10,70,230) : 180;
  if (session_.typing() && quiet_ms >= refine_after) session_.RefineCandidates();
  if (!first_preview_at_) first_preview_at_=now;
  const auto interval=std::clamp<DWORD>(2*preview_delay_ms_,75,180);
  const bool due = !session_.typing() || quiet_ms >= preview_delay_ms_ ||
      now-(last_submit_at_ ? last_submit_at_ : first_preview_at_) >= interval;
  if (!engine_channel_.running() && !session_.candidates_ready()) submitted_revision_ = -1;
  if (due && !HasQueuedFinal() && session_.needs_typing_lookup() && (!engine_channel_.busy() || !session_.typing()) && (session_.typing() || !session_.candidates_ready()) && !(session_.is_converting() && session_.candidates_ready()) &&
      session_.raw_text().size() <= 512 &&
      (session_.revision() != submitted_revision_ || session_.interaction_generation() != submitted_interaction_ ||
       session_.context_generation() != submitted_context_) && StartEngine()) {
    last_submit_at_ = now;
    auto request = session_.preview_request();
    request.dictionary_preview = session_.typing() &&
        (!engine_channel_.last_decode_ms() || preview_delay_ms_ >= 55);
    engine_channel_.Submit(request);
    submitted_revision_ = session_.revision(); submitted_interaction_ = session_.interaction_generation();
    submitted_context_ = session_.context_generation();
  }
  ime::DecodeInput request; std::vector<ime::Candidate> result;
  bool provisional = false;
  if (engine_channel_.Poll(&request, &result, &provisional)) {
    // A responsive worker earned an immediate restart if it later exits.
    retry_engine_at_ = 0;
    if (request.phase=="typing") {
      const auto target = std::clamp<DWORD>(engine_channel_.last_decode_ms() / 2, 25, 125);
      preview_delay_ms_ = (preview_delay_ms_ + target) / 2;
    }
    auto next = session_;
    bool changed = false;
    if (provisional) {
      changed = next.ApplyPendingPreview(request,std::move(result),++received_preview_sequence_);
    } else if (request.raw_text!=next.raw_text()) {
      changed = !result.empty() && next.ApplyTypingPrefix(request,result.front());
    } else if (request.phase=="typing" && !next.typing()) {
      changed = next.ApplyPendingPreview(request,std::move(result));
    } else {
      changed = next.ApplyCandidates(request,std::move(result));
    }
    if (changed)
      RequestState(context_, next, next.visible_text(), false, false, true);
  }
  if (pending_commit_ && (session_.candidates_ready() || HasQueuedFinal() || !engine_channel_.running() || GetTickCount64() >= commit_deadline_))
    FinishPendingCommit(true);
  if (!pending_commit_ && HasQueuedFinal()) release_idle_worker(true);
  // Full conversions wake this thread when their replies are ready. Retain a
  // slower watchdog while there is no typing deadline or queued TSF edit.
  if (engine_channel_.awaits_notified_reply() && !session_.typing() && !HasQueuedFinal() && !pending_commit_)
    cand_window_.SetPollInterval(125);
  if (!pending_commit_ && !engine_channel_.busy() && session_.candidates_ready() && !session_.typing()) {
    release_idle_worker(true);
    cand_window_.SetPollInterval(engine_channel_.running() ? 1000 : 0);
  }
}
void TextService::ClickCandidate(int index) {
  if (!context_ || !foreground_) return;
  last_key_at_ = GetTickCount64();
  cand_window_.SetPollInterval(25);
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
  } else if (IsEqualIID(riid, IID_ITfCompartmentEventSink)) {
    *ppv = static_cast<ITfCompartmentEventSink*>(this);
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
  UnregisterModeShortcuts();
  UnwatchKeyboardMode();
  if (input_mode_menu_) input_mode_menu_->Detach();
  if (low_memory_event_) CloseHandle(low_memory_event_);
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
  restricted_ = false;
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
  foreground_ = true;
  document_focused_ = true;
  retry_engine_at_ = 0;
  submitted_revision_ = submitted_interaction_ = submitted_context_ = -1;
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

  hr = InitThreadMgrSink();
  if (FAILED(hr)) {
    if (SUCCEEDED(thread_mgr_->QueryInterface(IID_ITfKeystrokeMgr, reinterpret_cast<void**>(&keys)))) {
      keys->UnadviseKeyEventSink(client_id_); keys->Release();
    }
    thread_key_sink_cookie_ = TF_INVALID_COOKIE;
    thread_mgr_->Release(); thread_mgr_ = nullptr; client_id_ = TF_CLIENTID_NULL;
    return hr;
  }

  ITfCategoryMgr* catmgr = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_ITfCategoryMgr, reinterpret_cast<void**>(&catmgr))) &&
      catmgr) {
    TfGuidAtom atom = TF_INVALID_GUIDATOM;
    if (SUCCEEDED(catmgr->RegisterGUID(kDisplayAttributeGuid, &atom))) display_attr_atom_ = atom;
    catmgr->Release();
  }

  cand_window_.SetService(this);
  if (!cand_window_.Create(g_hInstance, nullptr)) {
    activated_ = true; Deactivate(); return E_FAIL;
  }
  WatchKeyboardMode();
  ReloadInputPreferences();
  HRESULT langbar_hr = thread_mgr_->QueryInterface(IID_ITfLangBarItemMgr,
      reinterpret_cast<void**>(&lang_bar_mgr_));
  g_diagnostics.langbar_manager_result.store(langbar_hr);
  if (SUCCEEDED(langbar_hr) && lang_bar_mgr_) {
    input_mode_menu_ = new (std::nothrow) InputModeMenu(*this);
    if (input_mode_menu_) {
      HRESULT add_hr = lang_bar_mgr_->AddItem(input_mode_menu_);
      g_diagnostics.langbar_add_result.store(add_hr);
      if (FAILED(add_hr)) {
        input_mode_menu_->Release(); input_mode_menu_ = nullptr;
      }
    }
  }
  activated_ = true;
  // The first composition starts the worker on demand.
  g_diagnostics.activate_success.fetch_add(1);
  TipLog("ActivateInternal done client=%ld", static_cast<long>(client_id_));
  return S_OK;
}

HRESULT TextService::Deactivate() {
  TipLog("Deactivate enter activated=%d", activated_ ? 1 : 0);
  if (!activated_) return S_OK;
  engine_channel_.FinishLearning(250);
  engine_channel_.Stop();
  DetachComposition();
  if (thread_mgr_) {
    UnregisterModeShortcuts();
    UnwatchKeyboardMode();
    if (input_mode_menu_) input_mode_menu_->Detach();
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

void TextService::ReloadInputPreferences() {
  if (restricted_ || !thread_mgr_) return;
  if (preferences_directory_.empty()) {
    PWSTR folder = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder))) {
      preferences_directory_ = std::filesystem::path(folder) / L"ImeMixed";
      CoTaskMemFree(folder);
    }
  }
  const auto next = preferences_directory_.empty() ? ime::InputPreferences{} :
                    ime::ReadInputPreferences(preferences_directory_);
  const auto old = input_preferences_.mode_shortcut;
  const bool tab_changed = next.tab_candidates != input_preferences_.tab_candidates;
  input_preferences_ = next;
  if (tab_changed && IsWindowVisible(cand_window_.hwnd()))
    InvalidateRect(cand_window_.hwnd(), nullptr, FALSE);
  const auto& key = next.mode_shortcut;
  if (activated_ && old.mode == key.mode && old.virtual_key == key.virtual_key &&
      old.modifiers == key.modifiers) return;
  UnregisterModeShortcuts();
  RegisterModeShortcuts();
}

void TextService::RegisterModeShortcuts() {
  mode_key_count_ = 0;
  const auto& shortcut = input_preferences_.mode_shortcut;
  if (shortcut.mode == ime::ShortcutMode::Default) {
    static_assert(std::size(kDefaultModeKeys) == std::tuple_size_v<decltype(mode_keys_)>);
    std::copy(std::begin(kDefaultModeKeys), std::end(kDefaultModeKeys), mode_keys_.begin());
    mode_key_count_ = static_cast<unsigned>(mode_keys_.size());
  } else if (shortcut.mode == ime::ShortcutMode::Custom) {
    UINT modifiers = TF_MOD_ON_KEYUP;
    if (shortcut.modifiers & MOD_ALT) modifiers |= TF_MOD_ALT;
    if (shortcut.modifiers & MOD_SHIFT) modifiers |= TF_MOD_SHIFT;
    if (shortcut.modifiers & MOD_CONTROL) modifiers |= TF_MOD_CONTROL;
    mode_keys_[mode_key_count_++] = {shortcut.virtual_key, modifiers};
  }

  ITfKeystrokeMgr* keys = nullptr;
  if (!keyboard_open_ || restricted_ || FAILED(thread_mgr_->QueryInterface(IID_PPV_ARGS(&keys)))) return;
  constexpr wchar_t label[] = L"よみつぐ 日本語入力の切り替え";
  for (unsigned i = 0; i < mode_key_count_; ++i) {
    if (keys->PreserveKey(client_id_, kToggleKeyboardMode, &mode_keys_[i],
                          label, static_cast<ULONG>(std::size(label) - 1)) == S_OK)
      registered_shortcuts_ |= 1u << i;
  }
  keys->Release();
}

void TextService::UnregisterModeShortcuts() {
  ITfKeystrokeMgr* keys = nullptr;
  if (registered_shortcuts_ && thread_mgr_ && SUCCEEDED(thread_mgr_->QueryInterface(IID_PPV_ARGS(&keys)))) {
    for (unsigned i = 0; i < mode_key_count_; ++i)
      if (registered_shortcuts_ & (1u << i)) keys->UnpreserveKey(kToggleKeyboardMode, &mode_keys_[i]);
    keys->Release();
  }
  registered_shortcuts_ = 0;
  mode_key_count_ = 0;
}

void TextService::WatchKeyboardMode() {
  ITfCompartmentMgr* manager = nullptr;
  if (FAILED(thread_mgr_->QueryInterface(IID_PPV_ARGS(&manager)))) return;
  manager->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &keyboard_open_);
  manager->Release();
  ITfSource* source = nullptr;
  if (keyboard_open_ && SUCCEEDED(keyboard_open_->QueryInterface(IID_PPV_ARGS(&source)))) {
    source->AdviseSink(IID_ITfCompartmentEventSink, static_cast<ITfCompartmentEventSink*>(this), &mode_cookie_);
    source->Release();
  }
}

void TextService::UnwatchKeyboardMode() {
  if (!keyboard_open_) return;
  ITfSource* source = nullptr;
  if (mode_cookie_ != TF_INVALID_COOKIE && SUCCEEDED(keyboard_open_->QueryInterface(IID_PPV_ARGS(&source)))) {
    source->UnadviseSink(mode_cookie_);
    source->Release();
  }
  mode_cookie_ = TF_INVALID_COOKIE;
  keyboard_open_->Release();
  keyboard_open_ = nullptr;
}

bool TextService::KeyboardOpen() const {
  if (!keyboard_open_) return CompartmentEnabled(thread_mgr_, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, true);
  VARIANT value{};
  const bool open = keyboard_open_->GetValue(&value) != S_OK || value.vt != VT_I4 || value.lVal != 0;
  VariantClear(&value);
  return open;
}

HRESULT TextService::SetKeyboardOpen(bool open) {
  if (!activated_ || !keyboard_open_) return E_UNEXPECTED;
  if (restricted_) return E_ACCESSDENIED;
  VARIANT value{};
  value.vt = VT_I4;
  value.lVal = open ? 1 : 0;
  HRESULT hr = keyboard_open_->SetValue(client_id_, &value);
  if (hr == S_OK && mode_cookie_ == TF_INVALID_COOKIE) OnChange(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
  return hr;
}

STDMETHODIMP TextService::OnChange(REFGUID id) {
  if (id != GUID_COMPARTMENT_KEYBOARD_OPENCLOSE || !activated_) return S_OK;
  if (!KeyboardOpen() && (session_.composing() || has_composition_)) {
    DetachComposition();
    ResetSessionState();
  }
  if (input_mode_menu_) input_mode_menu_->Refresh();
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
STDMETHODIMP TextService::OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM flags, BOOL* pfEaten) {
  g_diagnostics.test_key_down.fetch_add(1);
  if (!pfEaten) return E_POINTER;
  *pfEaten = FALSE;
  try {
    BOOL eaten = FALSE;
    HRESULT hr = HandleKey(pic, wParam, flags, &eaten, true);
    if (SUCCEEDED(hr)) *pfEaten = eaten;
    return hr;
  } catch (...) {
    *pfEaten = FALSE;
    return E_FAIL;
  }
}

STDMETHODIMP TextService::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM flags, BOOL* pfEaten) {
  g_diagnostics.key_down.fetch_add(1);
  if (!pfEaten) return E_POINTER;
  *pfEaten = FALSE;
  try {
    BOOL eaten = FALSE;
    HRESULT hr = HandleKey(pic, wParam, flags, &eaten, false);
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

STDMETHODIMP TextService::OnPreservedKey(ITfContext* context, REFGUID id, BOOL* eaten) {
  if (!eaten) return E_POINTER;
  *eaten = FALSE;
  if (id != kToggleKeyboardMode || !registered_shortcuts_ || !foreground_ || restricted_ || !context ||
      GetPropW(GetFocus(), L"Yomitsugu.ModeShortcutCapture.1"))
    return S_OK;
  TF_STATUS status{};
  if (CompartmentEnabled(context, GUID_COMPARTMENT_KEYBOARD_DISABLED, false) ||
      CompartmentEnabled(context, GUID_COMPARTMENT_EMPTYCONTEXT, false) ||
      (SUCCEEDED(context->GetStatus(&status)) && (status.dwDynamicFlags & TS_SD_READONLY))) return S_OK;
  if ((GetKeyState(VK_RMENU) & 0x8000) && (GetKeyState(VK_CONTROL) & 0x8000)) return S_OK;
  *eaten = SetKeyboardOpen(!KeyboardOpen()) == S_OK;
  return S_OK;
}

STDMETHODIMP TextService::OnSetFocus(BOOL fForeground) {
  foreground_ = fForeground != FALSE;
  if (foreground_) retry_engine_at_ = 0;
  if (!foreground_) {
    ForgetDeferredFinal();
    cand_window_.Hide(); engine_channel_.FinishLearning(250); engine_channel_.Stop();
    submitted_revision_ = -1; last_submit_at_ = 0; cand_window_.SetPollInterval(0);
  }
  else if (activated_ && session_.composing() && !restricted_) cand_window_.SetPollInterval(25);
  return S_OK;
}

HRESULT TextService::HandleKey(ITfContext* pic, WPARAM key, LPARAM flags, BOOL* eaten, bool test) {
  if (!eaten) return E_POINTER;
  *eaten = FALSE;
  if (!pic) { g_diagnostics.gated_no_context.fetch_add(1); return S_OK; }
  if (restricted_) { g_diagnostics.gated_restricted.fetch_add(1); return S_OK; }
  if (!foreground_) { g_diagnostics.gated_background.fetch_add(1); return S_OK; }
  if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000)) return S_OK;
  const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  const UINT modifiers = (shift ? TF_MOD_SHIFT : 0) |
      ((GetKeyState(VK_CONTROL) & 0x8000) ? TF_MOD_CONTROL : 0) |
      ((GetKeyState(VK_MENU) & 0x8000) ? TF_MOD_ALT : 0);
  unsigned binding = 0;
  while (binding < mode_key_count_ &&
         (mode_keys_[binding].uVKey != key ||
          (mode_keys_[binding].uModifiers & ~TF_MOD_ON_KEYUP) != modifiers)) ++binding;
  // On layouts with AltGr, right Alt plus Ctrl generates ordinary text.
  if ((modifiers & (TF_MOD_ALT | TF_MOD_CONTROL)) == (TF_MOD_ALT | TF_MOD_CONTROL) &&
      (GetKeyState(VK_RMENU) & 0x8000)) return S_OK;
  const bool toggle = binding < mode_key_count_;
  const bool dedicated = input_preferences_.mode_shortcut.mode == ime::ShortcutMode::Default &&
      !modifiers && (key == VK_IME_ON || key == VK_IME_OFF);
  const bool mode_key = keyboard_open_ && (toggle || dedicated);
  if (!mode_key && (modifiers & (TF_MOD_CONTROL | TF_MOD_ALT))) return S_OK;
  if (!mode_key && key == VK_TAB && !input_preferences_.tab_candidates) return S_OK;
  if (!mode_key && !KeyboardOpen()) {
    g_diagnostics.gated_compartment.fetch_add(1);
    if (!test) cand_window_.Hide();
    return S_OK;
  }
  if (CompartmentEnabled(pic, GUID_COMPARTMENT_KEYBOARD_DISABLED, false) ||
      CompartmentEnabled(pic, GUID_COMPARTMENT_EMPTYCONTEXT, false)) {
    g_diagnostics.gated_compartment.fetch_add(1);
    if (!test) cand_window_.Hide();
    return S_OK;
  }
  TF_STATUS status{};
  if (SUCCEEDED(pic->GetStatus(&status)) && (status.dwDynamicFlags & TS_SD_READONLY)) {
    g_diagnostics.gated_readonly.fetch_add(1);
    if (!test) cand_window_.Hide();
    return S_OK;
  }
  if (mode_key) {
    if (GetPropW(GetFocus(), L"Yomitsugu.ModeShortcutCapture.1")) return S_OK;
    // Windows dispatches a registered chord on key-up. Consume its down
    // without toggling twice. Some hosts send only ordinary key callbacks.
    if (toggle && (registered_shortcuts_ & (1u << binding))) {
      *eaten = TRUE;
      return S_OK;
    }
    // Bit 30 indicates autorepeat. Holding 半角/全角 must switch only once.
    if (test || (flags & (1L << 30))) *eaten = TRUE;
    else *eaten = SetKeyboardOpen(toggle ? !KeyboardOpen() : key == VK_IME_ON) == S_OK;
    return S_OK;
  }
  bool composing = context_ == pic && session_.composing();
  bool converting = composing && session_.is_converting();
  bool operation = key == VK_BACK || key == VK_DELETE || key == VK_ESCAPE || key == VK_RETURN ||
      key == VK_SPACE || key == VK_TAB || key == VK_LEFT || key == VK_RIGHT || key == VK_HOME || key == VK_END ||
      (key >= VK_F6 && key <= VK_F10) || key == VK_CONVERT;
  bool candidate_key = converting && (key == VK_UP || key == VK_DOWN ||
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
  if (!composing) first_preview_at_ = last_submit_at_ = 0;
  const auto now=GetTickCount64();
  const auto gap=now-last_key_at_;
  if (composing && !converting && !append.empty() && gap>0 && gap<=200) {
    const auto sample=static_cast<DWORD>(gap);
    key_interval_ms_=measured_intervals_ ? (3*key_interval_ms_+sample+2)/4 : sample;
    measured_intervals_=std::min(3u,measured_intervals_+1);
  } else measured_intervals_=0;
  last_key_at_ = now;
  cand_window_.SetPollInterval(25);
  document_focused_ = true;
  if (context_ != pic) {
    DetachComposition();
    if (context_) context_->Release(); context_ = pic; context_->AddRef();
    ResetSessionState();
    composing = false; converting = false;
  }
  if (pending_commit_) {
    // 次のキーが来たら現在の表示を確定し、入力の順序を保つ。UIスレッドでは変換を待たない。
    if (FinishPendingCommit(false) != S_OK) { *eaten = TRUE; return S_OK; }
    composing = false; converting = false;
    if (operation || candidate_key) return S_OK;
  }
  if (key == VK_TAB && converting && !session_.candidates_ready()) {
    session_.PressTab(shift);
    *eaten = TRUE;
    return S_OK;
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
  else if (key == VK_RETURN) {
    if (BeginPendingCommit()) { *eaten = TRUE; return S_OK; }
    finish = true;
  }
  else if (key == VK_SPACE || key == VK_CONVERT) { if (shift) next.PressShiftSpace(); else next.PressSpace(); }
  else if (key == VK_TAB) next.PressTab(shift);
  else if (key == VK_LEFT) next.PressLeft();
  else if (key == VK_RIGHT) next.PressRight();
  else if (key == VK_HOME) next.PressHome();
  else if (key == VK_END) next.PressEnd();
  else if (key == VK_UP || key == VK_PRIOR) next.PressUp();
  else if (key == VK_DOWN || key == VK_NEXT) next.PressDown();
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
  if (!foreground_ || !document_focused_ || !session_.composing() || !caret_valid_) { cand_window_.Hide(); return; }
  cand_window_.Show(session_.candidates(), session_.selected_index(), caret_point_);
}

// TSFスレッドと未確定文字列。
STDMETHODIMP TextService::OnInitDocumentMgr(ITfDocumentMgr*) { return S_OK; }
STDMETHODIMP TextService::OnUninitDocumentMgr(ITfDocumentMgr* document) {
  ITfDocumentMgr* current = nullptr;
  if (context_ && SUCCEEDED(context_->GetDocumentMgr(&current)) && current) {
    const bool removed = current == document; current->Release();
    if (removed) {
      DetachComposition(); ResetSessionState();
      context_->Release(); context_ = nullptr; document_focused_ = false;
    }
  }
  return S_OK;
}
STDMETHODIMP TextService::OnSetFocus(ITfDocumentMgr* pdimFocus, ITfDocumentMgr*) {
  try {
    document_focused_ = false;
    cand_window_.Hide();
    if (!pdimFocus) {
      ForgetDeferredFinal();
      return S_OK;
    }
    ITfContext* ctx = nullptr;
    if (SUCCEEDED(pdimFocus->GetTop(&ctx)) && ctx) {
      document_focused_ = true;
      if (context_ && context_ != ctx) {
        DetachComposition(); ResetSessionState();
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
STDMETHODIMP TextService::OnPushContext(ITfContext*) {
  ITfDocumentMgr* document = nullptr;
  if (thread_mgr_ && SUCCEEDED(thread_mgr_->GetFocus(&document)) && document) {
    auto hr = OnSetFocus(document, nullptr); document->Release(); return hr;
  }
  return S_OK;
}
STDMETHODIMP TextService::OnPopContext(ITfContext* pic) {
  if (context_ == pic) {
    DetachComposition(); ResetSessionState();
    context_->Release();
    context_ = nullptr;
  }
  return S_OK;
}

STDMETHODIMP TextService::OnCompositionTerminated(TfEditCookie, ITfComposition* pComp) {
  if (ending_) return S_OK;
  TipLog("OnCompositionTerminated comp=%p ours=%p", static_cast<void*>(pComp),
         static_cast<void*>(composition_));
  if (pComp != composition_) {
    // 他のIMEの未確定文字列なら、このセッションは変更しない。
    return S_OK;
  }
  ReleaseCompositionRef();
  has_composition_ = false;
  ResetSessionState();
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

#pragma once
#include <msctf.h>

class FakeContext : public ITfContext {
 public:
  ULONG refs = 1;
  ITfEditSession* queued = nullptr;
  DWORD requested_flags = 0;
  ~FakeContext() { if (queued) queued->Release(); }
  STDMETHODIMP QueryInterface(REFIID id, void** out) override {
    *out = nullptr;
    if (id != IID_IUnknown && id != IID_ITfContext) return E_NOINTERFACE;
    *out = static_cast<ITfContext*>(this); AddRef(); return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++refs; }
  STDMETHODIMP_(ULONG) Release() override { return --refs; }
  STDMETHODIMP RequestEditSession(TfClientId, ITfEditSession* session, DWORD flags, HRESULT* result) override {
    requested_flags = flags;
    if (flags & TF_ES_SYNC) { *result = TF_E_SYNCHRONOUS; return S_OK; }
    if (queued) return E_FAIL;
    queued = session; queued->AddRef(); *result = TF_S_ASYNC; return S_OK;
  }
  HRESULT Flush() {
    if (!queued) return E_FAIL;
    auto session = queued; queued = nullptr;
    auto hr = session->DoEditSession(1); session->Release(); return hr;
  }
  STDMETHODIMP InWriteSession(TfClientId, BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP GetSelection(TfEditCookie, ULONG, ULONG, TF_SELECTION*, ULONG*) override { return E_NOTIMPL; }
  STDMETHODIMP SetSelection(TfEditCookie, ULONG, const TF_SELECTION*) override { return E_NOTIMPL; }
  STDMETHODIMP GetStart(TfEditCookie, ITfRange**) override { return E_NOTIMPL; }
  STDMETHODIMP GetEnd(TfEditCookie, ITfRange**) override { return E_NOTIMPL; }
  STDMETHODIMP GetActiveView(ITfContextView**) override { return E_NOTIMPL; }
  STDMETHODIMP EnumViews(IEnumTfContextViews**) override { return E_NOTIMPL; }
  STDMETHODIMP GetStatus(TF_STATUS* status) override { *status = {}; return S_OK; }
  STDMETHODIMP GetProperty(REFGUID, ITfProperty**) override { return E_NOTIMPL; }
  STDMETHODIMP GetAppProperty(REFGUID, ITfReadOnlyProperty**) override { return E_NOTIMPL; }
  STDMETHODIMP TrackProperties(const GUID**, ULONG, const GUID**, ULONG, ITfReadOnlyProperty**) override { return E_NOTIMPL; }
  STDMETHODIMP EnumProperties(IEnumTfProperties**) override { return E_NOTIMPL; }
  STDMETHODIMP GetDocumentMgr(ITfDocumentMgr**) override { return E_NOTIMPL; }
  STDMETHODIMP CreateRangeBackup(TfEditCookie, ITfRange*, ITfRangeBackup**) override { return E_NOTIMPL; }
};

class FakeComposition : public ITfComposition {
 public:
  ULONG refs = 1;
  ITfCompositionSink* sink;
  bool ended = false;
  explicit FakeComposition(ITfCompositionSink* target) : sink(target) {}
  STDMETHODIMP QueryInterface(REFIID id, void** out) override {
    *out = nullptr;
    if (id != IID_IUnknown && id != IID_ITfComposition) return E_NOINTERFACE;
    *out = static_cast<ITfComposition*>(this); AddRef(); return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++refs; }
  STDMETHODIMP_(ULONG) Release() override { return --refs; }
  STDMETHODIMP GetRange(ITfRange**) override { return E_NOTIMPL; }
  STDMETHODIMP ShiftStart(TfEditCookie, ITfRange*) override { return E_NOTIMPL; }
  STDMETHODIMP ShiftEnd(TfEditCookie, ITfRange*) override { return E_NOTIMPL; }
  STDMETHODIMP EndComposition(TfEditCookie cookie) override {
    ended = true; return sink->OnCompositionTerminated(cookie, this);
  }
};

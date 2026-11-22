#pragma once
#include <msctf.h>

// 登録や現在のIMEを変更せず、サービスの有効化・解除を試す。
class FakeThreadManager : public ITfThreadMgr, public ITfKeystrokeMgr, public ITfSource {
 public:
  ULONG refs = 1;
  bool fail_sink = false;
  bool accept_shortcuts = false;
  UINT shortcuts = 0;
  GUID last_shortcut{};
  IUnknown* compartment_provider = nullptr;
  ITfKeyEventSink* keys = nullptr;
  IUnknown* events = nullptr;
  STDMETHODIMP QueryInterface(REFIID id, void** out) override {
    *out = nullptr;
    if (id == IID_IUnknown || id == IID_ITfThreadMgr) *out = static_cast<ITfThreadMgr*>(this);
    else if (id == IID_ITfKeystrokeMgr) *out = static_cast<ITfKeystrokeMgr*>(this);
    else if (id == IID_ITfSource) *out = static_cast<ITfSource*>(this);
    else if (id == IID_ITfCompartmentMgr && compartment_provider) return compartment_provider->QueryInterface(id, out);
    else return E_NOINTERFACE;
    AddRef(); return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++refs; }
  STDMETHODIMP_(ULONG) Release() override { return --refs; }
  STDMETHODIMP Activate(TfClientId* id) override { *id = 1; return S_OK; }
  STDMETHODIMP Deactivate() override { return S_OK; }
  STDMETHODIMP CreateDocumentMgr(ITfDocumentMgr**) override { return E_NOTIMPL; }
  STDMETHODIMP EnumDocumentMgrs(IEnumTfDocumentMgrs**) override { return E_NOTIMPL; }
  STDMETHODIMP GetFocus(ITfDocumentMgr** doc) override { *doc = nullptr; return S_OK; }
  STDMETHODIMP SetFocus(ITfDocumentMgr*) override { return E_NOTIMPL; }
  STDMETHODIMP AssociateFocus(HWND, ITfDocumentMgr*, ITfDocumentMgr**) override { return E_NOTIMPL; }
  STDMETHODIMP IsThreadFocus(BOOL* value) override { *value = TRUE; return S_OK; }
  STDMETHODIMP GetFunctionProvider(REFCLSID, ITfFunctionProvider**) override { return E_NOTIMPL; }
  STDMETHODIMP EnumFunctionProviders(IEnumTfFunctionProviders**) override { return E_NOTIMPL; }
  STDMETHODIMP GetGlobalCompartment(ITfCompartmentMgr**) override { return E_NOTIMPL; }
  STDMETHODIMP AdviseKeyEventSink(TfClientId, ITfKeyEventSink* sink, BOOL) override {
    if (keys) return E_FAIL;
    keys = sink; keys->AddRef(); return S_OK;
  }
  STDMETHODIMP UnadviseKeyEventSink(TfClientId) override {
    if (keys) { keys->Release(); keys = nullptr; } return S_OK;
  }
  STDMETHODIMP GetForeground(CLSID*) override { return E_NOTIMPL; }
  STDMETHODIMP TestKeyDown(WPARAM, LPARAM, BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP TestKeyUp(WPARAM, LPARAM, BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP KeyDown(WPARAM, LPARAM, BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP KeyUp(WPARAM, LPARAM, BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP GetPreservedKey(ITfContext*, const TF_PRESERVEDKEY*, GUID*) override { return E_NOTIMPL; }
  STDMETHODIMP IsPreservedKey(REFGUID, const TF_PRESERVEDKEY*, BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP PreserveKey(TfClientId, REFGUID id, const TF_PRESERVEDKEY*, const WCHAR*, ULONG) override {
    if (!accept_shortcuts) return E_NOTIMPL;
    last_shortcut = id; ++shortcuts; return S_OK;
  }
  STDMETHODIMP UnpreserveKey(REFGUID, const TF_PRESERVEDKEY*) override {
    if (!shortcuts) return E_FAIL;
    --shortcuts; return S_OK;
  }
  STDMETHODIMP SetPreservedKeyDescription(REFGUID, const WCHAR*, ULONG) override { return E_NOTIMPL; }
  STDMETHODIMP GetPreservedKeyDescription(REFGUID, BSTR*) override { return E_NOTIMPL; }
  STDMETHODIMP SimulatePreservedKey(ITfContext*, REFGUID, BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP AdviseSink(REFIID, IUnknown* sink, DWORD* cookie) override {
    if (fail_sink || events) return E_FAIL;
    events = sink; events->AddRef(); *cookie = 1; return S_OK;
  }
  STDMETHODIMP UnadviseSink(DWORD) override {
    if (events) { events->Release(); events = nullptr; } return S_OK;
  }
};

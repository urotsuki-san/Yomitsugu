#pragma once

#include "tip.h"

// The standard Windows input indicator owns the tray surface. This item only
// contributes an IME-specific button and menu while the text service is active.
class InputModeMenu final : public ITfLangBarItemButton, public ITfSource {
 public:
  InputModeMenu();
  STDMETHODIMP QueryInterface(REFIID iid, void** out) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;
  STDMETHODIMP GetInfo(TF_LANGBARITEMINFO* info) override;
  STDMETHODIMP GetStatus(DWORD* status) override;
  STDMETHODIMP Show(BOOL show) override;
  STDMETHODIMP GetTooltipString(BSTR* tooltip) override;
  STDMETHODIMP OnClick(TfLBIClick click, POINT pt, const RECT* area) override;
  STDMETHODIMP InitMenu(ITfMenu* menu) override;
  STDMETHODIMP OnMenuSelect(UINT id) override;
  STDMETHODIMP GetIcon(HICON* icon) override;
  STDMETHODIMP GetText(BSTR* text) override;
  STDMETHODIMP AdviseSink(REFIID iid, IUnknown* sink, DWORD* cookie) override;
  STDMETHODIMP UnadviseSink(DWORD cookie) override;

 private:
  ~InputModeMenu();
  LONG refs_ = 1;
  ITfLangBarItemSink* sink_ = nullptr;
};

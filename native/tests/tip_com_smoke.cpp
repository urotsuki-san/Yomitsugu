#include <windows.h>
#include <msctf.h>
#include <cstdio>
#include <filesystem>
#include "tip_diagnostics.h"

namespace {
const GUID kTextServiceClsid = {
    0x8f3a1c2e, 0x4b5d, 0x4e6f, {0x8a, 0x9b, 0x0c, 0x1d, 0x2e, 0x3f, 0x4a, 0x5b}};
using DllGetClassObjectFn = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, void**);
}

int wmain() {
  wchar_t own_path[MAX_PATH]{};
  if (!GetModuleFileNameW(nullptr, own_path, MAX_PATH)) return 2;
  const auto dll_path = std::filesystem::path(own_path).parent_path() / L"ime_mixed_tip_v14.dll";
  HMODULE module = LoadLibraryW(dll_path.c_str());
  if (!module) {
    std::printf("TIP COM smoke load=failed code=%lu\n", GetLastError());
    return 3;
  }
  auto get_diagnostics = reinterpret_cast<ImeTipGetDiagnosticsFn>(
      GetProcAddress(module, "ImeTipGetDiagnostics"));
  TipDiagnostics diagnostics{};
  diagnostics.size = sizeof(diagnostics);
  if (!get_diagnostics || !get_diagnostics(&diagnostics)) {
    std::puts("TIP COM smoke diagnostics=failed");
    FreeLibrary(module);
    return 8;
  }
  auto get_class = reinterpret_cast<DllGetClassObjectFn>(GetProcAddress(module, "DllGetClassObject"));
  if (!get_class) {
    std::puts("TIP COM smoke factory_export=missing");
    FreeLibrary(module);
    return 4;
  }
  IClassFactory* factory = nullptr;
  HRESULT hr = get_class(kTextServiceClsid, IID_IClassFactory,
                         reinterpret_cast<void**>(&factory));
  if (FAILED(hr) || !factory) {
    std::printf("TIP COM smoke factory=failed hr=0x%08lx\n", hr);
    FreeLibrary(module);
    return 5;
  }
  ITfTextInputProcessor* processor = nullptr;
  hr = factory->CreateInstance(nullptr, IID_ITfTextInputProcessor,
                               reinterpret_cast<void**>(&processor));
  factory->Release();
  if (FAILED(hr) || !processor) {
    std::printf("TIP COM smoke instance=failed hr=0x%08lx\n", hr);
    FreeLibrary(module);
    return 6;
  }
  ITfKeyEventSink* key_sink = nullptr;
  hr = processor->QueryInterface(IID_ITfKeyEventSink,
                                 reinterpret_cast<void**>(&key_sink));
  if (key_sink) key_sink->Release();
  processor->Release();
  FreeLibrary(module);
  if (FAILED(hr)) {
    std::printf("TIP COM smoke key_sink=failed hr=0x%08lx\n", hr);
    return 7;
  }
  std::puts("TIP COM smoke load=factory=instance=key_sink=ok");
  return 0;
}

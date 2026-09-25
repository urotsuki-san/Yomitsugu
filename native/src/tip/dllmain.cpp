#include "tip.h"

#include <combaseapi.h>
#include <new>

HINSTANCE g_hInstance = nullptr;
LONG g_moduleRefCount = 0;

void ModuleAddRef() { InterlockedIncrement(&g_moduleRefCount); }
void ModuleRelease() { InterlockedDecrement(&g_moduleRefCount); }

BOOL APIENTRY DllMain(HINSTANCE hModule, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_hInstance = hModule;
    DisableThreadLibraryCalls(hModule);
  }
  return TRUE;
}

STDAPI DllCanUnloadNow() {
  return InterlockedCompareExchange(&g_moduleRefCount, 0, 0) == 0 ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
  if (ppv == nullptr) return E_POINTER;
  *ppv = nullptr;
  if (!IsEqualCLSID(rclsid, kTextServiceClsid)) return CLASS_E_CLASSNOTAVAILABLE;
  auto* f = new (std::nothrow) ClassFactory();
  if (!f) return E_OUTOFMEMORY;
  HRESULT hr = f->QueryInterface(riid, ppv);
  f->Release();
  return hr;
}

// Registration helpers implemented in register.cpp
STDAPI DllRegisterServer() { return RegisterTextService(); }
STDAPI DllUnregisterServer() { return UnregisterTextService(); }

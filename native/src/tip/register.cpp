#include "tip.h"

#include <msctf.h>
#include <string>
#include <vector>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "ole32.lib")

namespace {

std::wstring ModulePath() {
  wchar_t buf[32768] = {};
  if (!GetModuleFileNameW(g_hInstance, buf, 32768)) return {};
  return buf;
}

HRESULT SetRegString(HKEY root, const std::wstring& subkey, const wchar_t* name,
                     const std::wstring& value) {
  HKEY key = nullptr;
  LONG err = RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
  if (err != ERROR_SUCCESS) return HRESULT_FROM_WIN32(err);
  err = RegSetValueExW(key, name, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(value.c_str()),
                       static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
  RegCloseKey(key);
  return err == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(err);
}

std::wstring GuidString(const GUID& g) {
  wchar_t buf[64] = {};
  StringFromGUID2(g, buf, 64);
  return buf;
}

}  // namespace

HRESULT RegisterTextService() {
  const std::wstring clsid = GuidString(kTextServiceClsid);
  const std::wstring dll = ModulePath();
  if (dll.empty()) return E_FAIL;

  // COMクラスの登録。
  std::wstring base = L"CLSID\\" + clsid;
  HRESULT hr = SetRegString(HKEY_CLASSES_ROOT, base, nullptr, L"Yomitsugu Preview");
  if (FAILED(hr)) return hr;
  hr = SetRegString(HKEY_CLASSES_ROOT, base + L"\\InProcServer32", nullptr, dll);
  if (FAILED(hr)) return hr;
  hr = SetRegString(HKEY_CLASSES_ROOT, base + L"\\InProcServer32", L"ThreadingModel", L"Apartment");
  if (FAILED(hr)) return hr;

  HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool com_inited = SUCCEEDED(hrInit);
  HRESULT hrCo = S_OK;

  ITfInputProcessorProfiles* profiles = nullptr;
  hrCo = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                          IID_ITfInputProcessorProfiles, reinterpret_cast<void**>(&profiles));
  if (SUCCEEDED(hrCo) && profiles) {
    // 日本語の入力プロファイルを登録する。
    LANGID lang = 0x0411;  // ja-JP
    hr = profiles->Register(kTextServiceClsid);
    if (SUCCEEDED(hr)) hr = profiles->AddLanguageProfile(kTextServiceClsid, lang, kProfileGuid, const_cast<LPWSTR>(L"Yomitsugu Preview"),
                                static_cast<ULONG>(wcslen(L"Yomitsugu Preview")), dll.data(),
                                static_cast<ULONG>(dll.size()), 0);
    if (SUCCEEDED(hr)) hr = profiles->EnableLanguageProfile(kTextServiceClsid, lang, kProfileGuid, TRUE);
    profiles->Release();
  } else {
    hr = FAILED(hrCo) ? hrCo : E_FAIL;
  }

  ITfCategoryMgr* catmgr = nullptr;
  HRESULT hrCat = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_ITfCategoryMgr, reinterpret_cast<void**>(&catmgr));
  if (SUCCEEDED(hrCat) && catmgr) {
    if (SUCCEEDED(hr)) hr = catmgr->RegisterCategory(kTextServiceClsid, GUID_TFCAT_TIP_KEYBOARD, kTextServiceClsid);
    if (SUCCEEDED(hr)) hr = catmgr->RegisterCategory(kTextServiceClsid, GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER, kTextServiceClsid);
    // 対応しているTSFカテゴリだけを登録する。
    catmgr->Release();
  } else if (FAILED(hrCat)) {
    hr = hrCat;
  }

  if (com_inited) CoUninitialize();
  if (FAILED(hr)) UnregisterTextService();
  return hr;
}

HRESULT UnregisterTextService() {
  const std::wstring clsid = GuidString(kTextServiceClsid);
  HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool com_inited = SUCCEEDED(hrInit);

  ITfInputProcessorProfiles* profiles = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_ITfInputProcessorProfiles,
                                 reinterpret_cast<void**>(&profiles))) &&
      profiles) {
    LANGID lang = 0x0411;
    profiles->EnableLanguageProfile(kTextServiceClsid, lang, kProfileGuid, FALSE);
    profiles->RemoveLanguageProfile(kTextServiceClsid, lang, kProfileGuid);
    profiles->Unregister(kTextServiceClsid);
    profiles->Release();
  }

  ITfCategoryMgr* catmgr = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_ITfCategoryMgr, reinterpret_cast<void**>(&catmgr))) &&
      catmgr) {
    catmgr->UnregisterCategory(kTextServiceClsid, GUID_TFCAT_TIP_KEYBOARD, kTextServiceClsid);
    catmgr->UnregisterCategory(kTextServiceClsid, GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER, kTextServiceClsid);
    catmgr->UnregisterCategory(kTextServiceClsid, GUID_TFCAT_TIPCAP_UIELEMENTENABLED, kTextServiceClsid);
    catmgr->UnregisterCategory(kTextServiceClsid, GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT, kTextServiceClsid);
    catmgr->Release();
  }

  std::wstring base = L"CLSID\\" + clsid;
  RegDeleteTreeW(HKEY_CLASSES_ROOT, base.c_str());
  if (com_inited) CoUninitialize();
  return S_OK;
}

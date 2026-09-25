#include <windows.h>
#include <msctf.h>
#include <iostream>

int wmain(int argc, wchar_t** argv) {
  if (argc != 2) return 2;
  const bool restore = wcscmp(argv[1], L"--restore-google") == 0;
  const bool check = wcscmp(argv[1], L"--check-google") == 0;
  if (!restore && !check) return 2;

  HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(init)) return 3;
  CLSID google{};
  GUID google_profile{};
  CLSIDFromString(L"{D5A86FD5-5308-47EA-AD16-9C4EB160EC3C}", &google);
  CLSIDFromString(L"{773EB24E-CA1D-4B1B-B420-FA985BB0B80D}", &google_profile);

  ITfInputProcessorProfiles* profiles = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                IID_ITfInputProcessorProfiles,
                                reinterpret_cast<void**>(&profiles));
  if (SUCCEEDED(hr) && profiles) {
    if (restore) {
      hr = profiles->SetDefaultLanguageProfile(0x0411, google, google_profile);
      if (SUCCEEDED(hr)) hr = profiles->ActivateLanguageProfile(google, 0x0411, google_profile);
    } else {
      CLSID current{};
      GUID current_profile{};
      hr = profiles->GetDefaultLanguageProfile(0x0411, GUID_TFCAT_TIP_KEYBOARD,
                                               &current, &current_profile);
      if (hr == S_OK && (!IsEqualGUID(current, google) ||
                         !IsEqualGUID(current_profile, google_profile))) hr = S_FALSE;
    }
    profiles->Release();
  }
  CoUninitialize();
  std::cout << (check ? "default_ja_google=" : "restore_google=") << (hr == S_OK ? "yes" : "no")
            << " HRESULT=" << std::hex << hr << '\n';
  return hr == S_OK ? 0 : 4;
}

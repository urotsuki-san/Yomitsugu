// Select the registered Japanese profile on the disposable CI desktop.
#include <windows.h>
#include <msctf.h>
#include <cstdio>
#include <cstdlib>

int wmain(int argc, wchar_t** argv) {
  wchar_t runner[32]{};
  if (argc != 2 || !GetEnvironmentVariableW(L"RUNNER_ENVIRONMENT",runner,32) ||
      wcscmp(runner,L"github-hosted")) return 2;
  const auto target = wcstoul(argv[1],nullptr,10);
  HWND window = GetForegroundWindow();
  DWORD pid = 0;
  GetWindowThreadProcessId(window,&pid);
  if (!target || target != pid) return 3;
  if (FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED))) return 4;
  CLSID service{}, profile{};
  CLSIDFromString(L"{8F3A1C2E-4B5D-4E6F-8A9B-0C1D2E3F4A5B}",&service);
  CLSIDFromString(L"{8F3A1C2E-4B5D-4E6F-8A9B-0C1D2E3F4A5C}",&profile);
  ITfInputProcessorProfiles* profiles = nullptr;
  auto hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&profiles));
  if (FAILED(hr)) return 5;
  const auto enable = profiles->EnableLanguageProfile(service,0x0411,profile,TRUE);
  const auto hkl = LoadKeyboardLayoutW(L"00000411",KLF_ACTIVATE);
  if (hkl) {
    DWORD_PTR response = 0;
    SendMessageTimeoutW(window,WM_INPUTLANGCHANGEREQUEST,0,reinterpret_cast<LPARAM>(hkl),SMTO_ABORTIFHUNG,1500,&response);
  }
  const auto language = profiles->ChangeCurrentLanguage(0x0411);
  ITfInputProcessorProfileMgr* manager = nullptr;
  hr = profiles->QueryInterface(IID_PPV_ARGS(&manager));
  if (FAILED(hr)) return 6;
  hr = manager->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR,0x0411,service,profile,nullptr,
                                TF_IPPMF_ENABLEPROFILE | TF_IPPMF_FORSESSION | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE);
  std::printf("Enable=%08lx language=%08lx layout=%p activate=%08lx\n",enable,language,hkl,hr);
  TF_INPUTPROCESSORPROFILE active{};
  const auto current = manager->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD,&active);
  std::printf("GetActiveProfile=%08lx Japanese=%d service=%d profile=%d\n",current,active.langid==0x0411,
              IsEqualGUID(active.clsid,service),IsEqualGUID(active.guidProfile,profile));
  manager->Release(); profiles->Release(); CoUninitialize();
  return hr == S_OK ? 0 : 7;
}

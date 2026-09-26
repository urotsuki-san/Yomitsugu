#include "app_update.h"
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <fstream>
#include <functional>
#include <regex>
#include <stdexcept>
#include <vector>

namespace ime {
namespace {
constexpr std::uint64_t kMaxInstaller = 512ull * 1024 * 1024;
constexpr char kDownloadPrefix[] = "https://github.com/urotsuki-san/Yomitsugu/releases/download/";
struct InternetHandle {
  HINTERNET value;
  ~InternetHandle() { if (value) WinHttpCloseHandle(value); }
  operator HINTERNET() const { return value; }
};
std::wstring Wide(const std::string& text) { return {text.begin(), text.end()}; }
std::optional<std::array<unsigned long, 5>> Version(const std::string& tag) {
  static const std::regex pattern(R"(^v?([0-9]{1,6})\.([0-9]{1,6})\.([0-9]{1,6})(?:-preview(?:\.([0-9]{1,6}))?)?$)");
  std::smatch match;
  if (!std::regex_match(tag, match, pattern)) return {};
  return std::array<unsigned long,5>{std::stoul(match[1]),std::stoul(match[2]),std::stoul(match[3]),
      tag.find("preview") == std::string::npos ? 1ul : 0ul, match[4].matched ? std::stoul(match[4]) : 0ul};
}
bool DigestValid(const std::string& hash) {
  return hash.size()==64 && hash.find_first_not_of("0123456789abcdef")==std::string::npos;
}
void Fetch(std::string url, std::uint64_t limit, const std::function<void(const char*,std::size_t)>& consume) {
  InternetHandle session{WinHttpOpen(L"Yomitsugu-updater/0.2.10", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
      WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0)};
  if (!session.value) throw std::runtime_error("network initialization");
  WinHttpSetTimeouts(session,10000,10000,30000,30000);
  const auto started = GetTickCount64();
  for (int redirects=0; redirects<6; ++redirects) {
    if (!TrustedUpdateUrl(url,true)) throw std::runtime_error("untrusted update URL");
    const auto wide = Wide(url);
    URL_COMPONENTS parts{}; parts.dwStructSize=sizeof(parts);
    parts.dwHostNameLength=parts.dwUrlPathLength=parts.dwExtraInfoLength=static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide.c_str(),0,0,&parts)) throw std::runtime_error("invalid URL");
    const std::wstring host(parts.lpszHostName,parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath,parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength) path.append(parts.lpszExtraInfo,parts.dwExtraInfoLength);
    InternetHandle connection{WinHttpConnect(session,host.c_str(),INTERNET_DEFAULT_HTTPS_PORT,0)};
    InternetHandle request{WinHttpOpenRequest(connection,L"GET",path.c_str(),nullptr,
        WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE)};
    DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    DWORD disable=WINHTTP_DISABLE_COOKIES;
    if (!request.value || !WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy)) ||
        !WinHttpSetOption(request,WINHTTP_OPTION_DISABLE_FEATURE,&disable,sizeof(disable)) ||
        !WinHttpSendRequest(request,L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n",static_cast<DWORD>(-1),
            WINHTTP_NO_REQUEST_DATA,0,0,0) || !WinHttpReceiveResponse(request,nullptr)) throw std::runtime_error("update connection failed");
    DWORD status=0, bytes=sizeof(status);
    if (!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,&status,&bytes,WINHTTP_NO_HEADER_INDEX)) throw std::runtime_error("missing HTTP status");
    if (status==301 || status==302 || status==303 || status==307 || status==308) {
      std::vector<wchar_t> location(16384); bytes=static_cast<DWORD>(location.size()*sizeof(wchar_t));
      if (!WinHttpQueryHeaders(request,WINHTTP_QUERY_LOCATION,WINHTTP_HEADER_NAME_BY_INDEX,
          location.data(),&bytes,WINHTTP_NO_HEADER_INDEX)) throw std::runtime_error("invalid redirect");
      std::wstring next(location.data());
      if (std::any_of(next.begin(),next.end(),[](wchar_t c){return c<33 || c>126;}))
        throw std::runtime_error("invalid redirect characters");
      url.clear();
      for (auto ch:next) url.push_back(static_cast<char>(ch));
      continue;
    }
    if (status!=200) throw std::runtime_error("update HTTP status " + std::to_string(status));
    char buffer[65536]; std::uint64_t total=0;
    for (;;) {
      DWORD read=0;
      if (GetTickCount64()-started>300000 || !WinHttpReadData(request,buffer,sizeof(buffer),&read)) throw std::runtime_error("update download interrupted");
      if (!read) return;
      total+=read;
      if (total>limit) throw std::runtime_error("update exceeds size limit");
      consume(buffer,read);
    }
  }
  throw std::runtime_error("too many update redirects");
}
}
bool NewerRelease(const std::string& candidate, const std::string& current) {
  const auto a=Version(candidate),b=Version(current);
  return a && b && *a>*b;
}
bool TrustedUpdateUrl(const std::string& url, bool redirect) {
  if (url.find_first_of("\\\r\n\t #")!=std::string::npos ||
      std::any_of(url.begin(),url.end(),[](unsigned char c){return c<33 || c>126;})) return false;
  if (url.rfind(kDownloadPrefix,0)==0) return true;
  if (!redirect) return false;
  return url=="https://api.github.com/repos/urotsuki-san/Yomitsugu/releases?per_page=100" ||
      url.rfind("https://release-assets.githubusercontent.com/",0)==0 ||
      url.rfind("https://objects.githubusercontent.com/",0)==0;
}
std::optional<AppRelease> ParseAppReleases(const std::string& text, const std::string& current) {
  const auto json=nlohmann::json::parse(text);
  if (!json.is_array() || !Version(current)) throw std::runtime_error("invalid release list");
  std::optional<AppRelease> newest;
  for (const auto& item:json) {
    try {
      const std::string tag=item.at("tag_name").get<std::string>();
      if (item.at("draft").get<bool>() || !NewerRelease(tag,newest?newest->tag:current)) continue;
      const auto version=Version(tag);
      if (!version) continue;
      const auto& v=*version;
      const auto name="Yomitsugu-"+std::to_string(v[0])+"."+std::to_string(v[1])+"."+std::to_string(v[2])+
          (v[3]?"":"-preview")+"-x64-setup.exe";
      for (const auto& asset:item.at("assets")) {
        if (asset.at("name")!=name || asset.value("state",std::string())!="uploaded") continue;
        AppRelease release{tag,name,asset.at("browser_download_url").get<std::string>(),"",asset.at("size").get<std::uint64_t>()};
        const auto digest=asset.at("digest").get<std::string>();
        if (digest.rfind("sha256:",0)!=0) continue;
        release.sha256=digest.substr(7);
        if (release.size==0 || release.size>kMaxInstaller || !DigestValid(release.sha256) ||
            release.url!=std::string(kDownloadPrefix)+tag+"/"+name || !TrustedUpdateUrl(release.url)) continue;
        newest=release; break;
      }
    } catch (const nlohmann::json::exception&) { continue; }
  }
  return newest;
}
std::optional<AppRelease> CheckAppUpdate(const std::string& current) {
  std::string response;
  Fetch("https://api.github.com/repos/urotsuki-san/Yomitsugu/releases?per_page=100",4*1024*1024,
      [&](const char* data,std::size_t size){response.append(data,size);});
  return ParseAppReleases(response,current);
}
bool VerifyInstaller(const std::filesystem::path& file, const AppRelease& release) {
  std::error_code ec;
  if (!DigestValid(release.sha256) || release.size==0 || release.size>kMaxInstaller ||
      std::filesystem::file_size(file,ec)!=release.size || ec) return false;
  BCRYPT_ALG_HANDLE algorithm=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
  if (BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return false;
  bool ok=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
  std::ifstream stream(file,std::ios::binary); char buffer[65536];
  while (ok && stream) { stream.read(buffer,sizeof(buffer)); const auto size=stream.gcount();
    if (size) ok=BCryptHashData(hash,reinterpret_cast<PUCHAR>(buffer),static_cast<ULONG>(size),0)>=0;
  }
  unsigned char digest[32]{};
  ok=ok && stream.eof() && BCryptFinishHash(hash,digest,sizeof(digest),0)>=0;
  if (hash) BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(algorithm,0);
  std::string hex;
  for (auto byte:digest) { hex+="0123456789abcdef"[byte>>4]; hex+="0123456789abcdef"[byte&15]; }
  return ok && hex==release.sha256;
}
std::filesystem::path DownloadAppUpdate(const AppRelease& release, const std::filesystem::path& directory) {
  if (!Version(release.tag) || !TrustedUpdateUrl(release.url) || !DigestValid(release.sha256) ||
      release.size==0 || release.size>kMaxInstaller || release.name.find_first_of("/\\:")!=std::string::npos ||
      release.name.size()<10 || release.url!=std::string(kDownloadPrefix)+release.tag+"/"+release.name)
    throw std::runtime_error("invalid update metadata");
  std::filesystem::create_directories(directory);
  // 別の設定画面で更新していても、途中のファイルを共有しない。
  const auto staging=directory/(std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
  if (!std::filesystem::create_directory(staging)) throw std::runtime_error("update directory exists");
  const auto file=staging/release.name;
  try {
    std::ofstream stream(file,std::ios::binary|std::ios::trunc);
    if (!stream) throw std::runtime_error("cannot save update");
    Fetch(release.url,release.size,[&](const char* data,std::size_t size){
      stream.write(data,static_cast<std::streamsize>(size));
      if (!stream) throw std::runtime_error("update write failed");
    });
    stream.close();
    if (!VerifyInstaller(file,release)) throw std::runtime_error("update checksum mismatch");
    return file;
  } catch (...) { std::error_code ec; std::filesystem::remove(file,ec); std::filesystem::remove(staging,ec); throw; }
}
bool LaunchAppUpdate(const std::filesystem::path& file, const AppRelease& release, bool quiet) {
  if (!VerifyInstaller(file,release)) return false;
  return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",file.c_str(),
      quiet?L"/VERYSILENT /SUPPRESSMSGBOXES /NORESTART":L"/NORESTART",
      file.parent_path().c_str(),quiet?SW_HIDE:SW_SHOWNORMAL))>32;
}
}

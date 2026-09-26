#include "app_update.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <chrono>
namespace {
int checks=0;
void Check(bool value,const char* label) { ++checks; if (!value) throw std::runtime_error(label); }
nlohmann::json Release(const std::string& tag="v0.2.9-preview.1") {
  const std::string name="Yomitsugu-0.2.9-preview-x64-setup.exe";
  return {{"tag_name",tag},{"draft",false},{"assets",nlohmann::json::array({{
    {"name",name},{"state","uploaded"},{"size",3},
    {"browser_download_url","https://github.com/urotsuki-san/Yomitsugu/releases/download/"+tag+"/"+name},
    {"digest","sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"}}})}};
}
}
int main(int argc,char** argv) {
  try {
    if (argc==4 && std::string(argv[1])=="--launch-verified") {
      const std::filesystem::path file=argv[2];
      ime::AppRelease release;
      release.sha256=argv[3]; release.size=std::filesystem::file_size(file);
      Check(ime::LaunchAppUpdate(file,release,true),"verified installer launch");
      std::cout<<"Verified installer opened\n"; return 0;
    }
    Check(ime::NewerRelease("v0.2.10-preview.1","v0.2.9-preview.1"),"numeric version");
    Check(ime::NewerRelease("v0.2.9-preview.2","v0.2.9-preview.1"),"preview revision");
    Check(ime::NewerRelease("v0.2.9","v0.2.9-preview.1"),"stable ordering");
    Check(!ime::NewerRelease("v0.2.8","v0.2.9-preview.1"),"no downgrade");
    Check(!ime::NewerRelease("garbage","v0.2.9"),"invalid version");
    Check(!ime::NewerRelease("v999999999999.0.0","v0.2.9"),"overflow version");
    Check(!ime::NewerRelease("v0.2.9-preview.1",ime::kReleaseTag),"same version");
    const auto fixture=Release();
    auto list=nlohmann::json::array({fixture});
    const auto release=ime::ParseAppReleases(list.dump(),"v0.2.8-preview.1");
    Check(release.has_value(),"release accepted");
    Check(!ime::ParseAppReleases(list.dump(),ime::kReleaseTag),"already current");
    for (int mode=0;mode<7;++mode) {
      auto bad=fixture;
      if (mode==0) bad["draft"]=true;
      if (mode==1) bad["assets"][0]["digest"]=nullptr;
      if (mode==2) bad["assets"][0]["browser_download_url"]="https://example.com/setup.exe";
      if (mode==3) bad["assets"][0]["size"]=600ull*1024*1024;
      if (mode==4) bad["assets"][0]["name"]="Yomitsugu-arm64-setup.exe";
      if (mode==5) bad["assets"][0]["state"]="new";
      if (mode==6) bad["assets"][0]["size"]=0;
      Check(!ime::ParseAppReleases(nlohmann::json::array({bad}).dump(),"v0.2.8"),"invalid asset rejected");
    }
    list.push_back(Release("v0.2.9-preview.2"));
    Check(ime::ParseAppReleases(list.dump(),"v0.2.8")->tag=="v0.2.9-preview.2","newest asset selected");
    for (auto url:{"http://github.com/urotsuki-san/Yomitsugu/releases/download/x",
        "https://github.com.evil.test/urotsuki-san/Yomitsugu/releases/download/x",
        "https://evil@github.com/urotsuki-san/Yomitsugu/releases/download/x",
        "https://release-assets.githubusercontent.com.evil.test/x",
        "https://github.com/urotsuki-san/Yomitsugu/releases/download/\\evil/x"})
      Check(!ime::TrustedUpdateUrl(url,true),"untrusted URL rejected");
    Check(ime::TrustedUpdateUrl("https://release-assets.githubusercontent.com/a?token=b",true),"asset redirect");
    const auto directory=std::filesystem::temp_directory_path()/("yomitsugu-update-test-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    const auto file=directory/"sample.bin";
    {std::ofstream out(file,std::ios::binary);out<<"abc";}
    Check(ime::VerifyInstaller(file,*release),"SHA256 matches");
    {std::ofstream out(file,std::ios::binary);out<<"abd";}
    Check(!ime::VerifyInstaller(file,*release),"tampered bytes rejected");
    {std::ofstream out(file,std::ios::binary);out<<"ab";}
    Check(!ime::VerifyInstaller(file,*release),"truncated file rejected");
    std::filesystem::remove(file);std::filesystem::remove(directory);
    std::cout<<checks<<" update checks passed\n";
    if (argc>1 && std::string(argv[1])=="--live") {
      const auto available=ime::CheckAppUpdate("v0.2.7-preview.1");
      Check(available.has_value(),"live release lookup");
      std::cout<<"Live release: "<<available->tag<<"\n";
      if (argc>2) {
        const auto downloaded=ime::DownloadAppUpdate(*available,argv[2]);
        Check(ime::VerifyInstaller(downloaded,*available),"live downloaded installer");
        std::cout<<"Verified: "<<downloaded.u8string()<<"\n";
      }
    }
    return 0;
  } catch (const std::exception& e) {std::cerr<<e.what()<<"\n";return 1;}
}

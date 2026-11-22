#include "engine_channel.h"
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <future>
#include <iostream>
#include <string_view>

namespace fs = std::filesystem;
struct Mode { const wchar_t* id; const wchar_t* title; const char* base; const wchar_t* weight; const wchar_t* policy; };
constexpr Mode modes[] = {
    {L"auto",L"自動最適化（既定）","automatic",nullptr,nullptr},
    {L"hybrid",L"ハイブリッド・省メモリ", "compact_extended",L"jinen-v2-xsmall-Q5_K_M.gguf",L"hybrid"},
    {L"full",L"ハイブリッド・毎回三方式を比較", "compact_extended",L"jinen-v2-xsmall-Q5_K_M.gguf",L"full"},
    {L"compact",L"軽量・詳細探索", "compact_extended",nullptr,L"baseline"},
    {L"diverse",L"軽量・詳細探索＋短い読みの候補補完", "compact_extended",nullptr,L"diverse"},
    {L"jinen",L"Jinen v2 xsmall＋辞書", "dictionary",L"jinen-v2-xsmall-Q5_K_M.gguf",L"direct"},
    {L"iroha",L"iroha-kkc-v1＋辞書", "dictionary",L"iroha-kkc-v1-Q5_K_M.gguf",L"direct"},
    {L"compact-iroha",L"軽量・詳細探索＋iroha", "compact_extended",L"iroha-kkc-v1-Q5_K_M.gguf",L"direct"},
    {L"verify",L"ハイブリッド・辞書照合", "compact_extended",L"jinen-v2-xsmall-Q5_K_M.gguf",L"verify"}
};
std::string Utf8(const std::wstring& s) {
    if (s.empty()) return {};
    const int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    std::string out(n,'\0');
    WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr);
    return out;
}
std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    const int n=MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0);
    std::wstring out(n,L'\0');
    MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n);
    return out;
}
bool Convert(ime::EngineChannel& channel,const std::wstring& raw,const std::wstring& left,const std::wstring& right) {
    ime::DecodeInput q;
    q.raw_text=Utf8(raw); q.left_context=Utf8(left); q.right_context=Utf8(right); q.candidate_limit=16;
    if (q.raw_text.empty() || q.raw_text.size()>512) {std::wcerr<<L"1〜512バイトの入力を指定してください。\n";return false;}
    channel.Submit(q);
    const auto start=GetTickCount64();
    while (channel.running() && GetTickCount64()-start<30000) {
        ime::DecodeInput reply;
        std::vector<ime::Candidate> candidates;
        if (channel.Poll(&reply,&candidates)) {
            for(size_t n=0;n<candidates.size();++n) std::wcout<<n+1<<L"  "<<Wide(candidates[n].output_text)<<L'\n';
            std::wcout<<GetTickCount64()-start<<L" ms\n"<<std::flush;
            return true;
        }
        Sleep(1);
    }
    std::wcerr<<L"変換ホストから応答がありませんでした。\n";return false;
}
int wmain(int argc,wchar_t** argv) {
    _setmode(_fileno(stdout),_O_U8TEXT); _setmode(_fileno(stderr),_O_U8TEXT);
    if (_isatty(_fileno(stdin))) _setmode(_fileno(stdin),_O_U16TEXT);
    const Mode* mode=nullptr;
    if (argc>=2 && argc<=5) {
        for (const auto& m:modes) if (m.id==std::wstring_view(argv[1])) mode=&m;
    } else if (argc==1) {
        std::wcout<<L"Yomitsugu 変換比較\n";
        for(size_t i=0;i<std::size(modes);++i) std::wcout<<i+1<<L"  "<<modes[i].title<<L'\n';
        std::wcout<<L"方式を番号で選択（Enterで自動最適化）> "<<std::flush;
        std::wstring s; std::getline(std::wcin,s);
        if(s.empty() && std::wcin) mode=&modes[0];
        if(s.size()==1 && s[0]>=L'1' && static_cast<size_t>(s[0]-L'1')<std::size(modes)) mode=&modes[s[0]-L'1'];
    }
    if (!mode) {std::wcerr<<L"Compare.exe auto|hybrid|verify|full|compact|diverse|jinen|iroha|compact-iroha [ローマ字 [左文脈 [右文脈]]]\n"; return 2;}
    wchar_t module[32768]{};
    if (!GetModuleFileNameW(nullptr,module,32768)) return 3;
    const auto dir=fs::path(module).parent_path()/L"engine";
    const auto path=mode->weight ? dir/L"models"/mode->weight : fs::path{};
    if (mode->weight && !fs::is_regular_file(path)) {std::wcerr<<L"モデルがありません。\n"; return 4;}
    SetEnvironmentVariableW(L"YOMITSUGU_PROBE_GGUF",mode->weight?path.c_str():nullptr);
    SetEnvironmentVariableW(L"YOMITSUGU_PROBE_POLICY",mode->policy);
    const bool jinen=mode->weight && std::wstring_view(mode->weight).find(L"jinen")==0;
    SetEnvironmentVariableW(L"YOMITSUGU_PROBE_NFKC",jinen?L"1":nullptr);
    SetEnvironmentVariableW(L"YOMITSUGU_PROBE_UBATCH",jinen?L"32":nullptr);
    SetEnvironmentVariableW(L"IROHA_GPU_LAYERS",L"0");SetEnvironmentVariableW(L"IROHA_THREADS",L"4");
    SetEnvironmentVariableW(L"IROHA_CROSS_KV",L"1");
    const auto profile=fs::temp_directory_path()/(L"YomitsuguCompare-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    if (!fs::create_directory(profile)) return 5;
    struct Cleanup {fs::path path; ~Cleanup(){std::error_code ec;fs::remove(path/L"settings.json",ec);fs::remove(path,ec);}} cleanup{profile};
    {std::ofstream f(profile/L"settings.json");f<<"{\"learning_enabled\":false,\"conversion_model\":\""<<mode->base<<"\"}"; if(!f) return 6;}
    ime::EngineChannel channel;
    auto start_worker=[&] {
        if(channel.running() || channel.Start((dir/L"compare_host.exe").wstring(),profile.wstring())) return true;
        std::wcerr<<L"変換ホストを起動できませんでした。\n";
        return false;
    };
    std::wcout<<mode->title<<L'\n';
    if(argc>=3) {
        if(!start_worker()) return 7;
        return Convert(channel,argv[2],argc>=4?argv[3]:L"",argc>=5?argv[4]:L"") ? 0:8;
    }
    while(true) {
        std::wcout<<L"ローマ字を入力（空行で終了）> "<<std::flush;
        auto input=std::async(std::launch::async,[] {
            std::wstring raw;
            std::getline(std::wcin,raw);
            return raw;
        });
        if(channel.running() && input.wait_for(std::chrono::seconds(15))==std::future_status::timeout)
            channel.Stop();
        const auto raw=input.get();
        if(raw.empty()) break;
        if(!start_worker()) return 7;
        if (!Convert(channel,raw,L"",L"") && !channel.running()) return 8;
    }
    return 0;
}

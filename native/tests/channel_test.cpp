#include "engine_channel.h"
#include "nlohmann/json.hpp"
#include <iostream>
#include <filesystem>

using nlohmann::json;

bool Transfer(HANDLE h, void* data, DWORD n, bool output) {
    auto* bytes=static_cast<char*>(data);
    while(n) {
        DWORD count=0;
        if (!(output?WriteFile(h,bytes,n,&count,nullptr):ReadFile(h,bytes,n,&count,nullptr)) || !count) return false;
        bytes+=count;
        n-=count;
    }
    return true;
}

int Serve(wchar_t** argv) {
    HANDLE in=reinterpret_cast<HANDLE>(_wcstoui64(argv[2],nullptr,10));
    HANDLE out=reinterpret_cast<HANDLE>(_wcstoui64(argv[3],nullptr,10));
    int serial=0, learns=0;
    bool interrupted_once=false;
    for(;;) {
        DWORD size=0;
        if(!Transfer(in,&size,sizeof(size),false)) return 0;
        if(size>16384)return 2;
        std::string frame(size,'\0');
        if(!Transfer(in,frame.data(),size,false))return 2;
        auto query=json::parse(frame);
        Sleep(25);
        json result;
        if(query.value("operation","")=="learn") {
            if (query.contains("cancel_event")) return 4;
            ++learns;
            result={{"learned",true}};
        } else {
            auto text = std::to_string(++serial) + "/" + std::to_string(learns);
            if (query.at("raw")=="cancel" && query.value("allow_preview",false)) {
                auto event = reinterpret_cast<HANDLE>(query.at("cancel_event").get<uintptr_t>());
                if (WaitForSingleObject(event,0)!=WAIT_TIMEOUT) return 5;
                if (!interrupted_once) {
                    auto ready=json{{"preview",json::array({{{"text","armed"},{"reading","kana"}}})}}.dump();
                    auto length=static_cast<DWORD>(ready.size());
                    if (!Transfer(out,&length,sizeof(length),true) || !Transfer(out,ready.data(),length,true) ||
                        WaitForSingleObject(event,2000)!=WAIT_OBJECT_0) return 6;
                    if (!Transfer(out,&length,sizeof(length),true) || !Transfer(out,ready.data(),length,true)) return 7;
                    interrupted_once=true;
                    text="stale";
                } else text="retried/"+std::to_string(learns);
            }
            if (query.at("raw") == "flags") {
                text = std::string(query.value("punctuation_completion",false) ? "1" : "0") +
                    (query.value("shallow_search",false) ? "1" : "0") +
                    (query.value("dictionary_preview",false) ? "1" : "0");
            }
            if (query.at("raw")=="stream" && query.value("allow_preview",false)) {
                if (query.value("max_previews",0) != 3) return 3;
                for (const char* partial : {"one","two","three"}) {
                    auto packet=json{{"preview",json::array({{{"text",partial},{"reading","kana"}}})}}.dump();
                    auto length=static_cast<DWORD>(packet.size());
                    if (!Transfer(out,&length,sizeof(length),true) || !Transfer(out,packet.data(),length,true)) return 2;
                    Sleep(15);
                }
                text="done";
            }
            result=json::array({{{"text",text},
                                 {"reading","kana"},{"raw",false}}});
        }
        frame=result.dump();
        size=static_cast<DWORD>(frame.size());
        if(!Transfer(out,&size,sizeof(size),true)||!Transfer(out,frame.data(),size,true))return 2;
        if (query.contains("reply_event"))
            SetEvent(reinterpret_cast<HANDLE>(query["reply_event"].get<uintptr_t>()));
    }
}

int wmain(int argc,wchar_t** argv) {
    if(argc>=4 && std::wstring_view(argv[1])==L"--pipe") return Serve(argv);
    ime::EngineChannel channel;
    wchar_t exe[32768]{};
    if(!GetModuleFileNameW(nullptr,exe,32768)||!channel.Start(exe))return 1;
    ime::DecodeInput a,reply;
    a.raw_text="nihongo";
    std::vector<ime::Candidate> values;
    auto start=[&] {channel.Submit(a); return !channel.Poll(&reply,&values) && channel.busy();};
    auto drain=[&] {
        std::vector<std::pair<std::string,ime::DecodeInput>> seen;
        auto deadline=GetTickCount64()+5000;
        while(channel.busy() && GetTickCount64()<deadline) {
            if(channel.Poll(&reply,&values))seen.emplace_back(values.at(0).output_text,reply);
            Sleep(1);
        }
        return seen;
    };
    if(!start())return 2;
    for(int i=0;i<20;++i)channel.Submit(a);
    auto other=a;
    other.left_context="A";
    channel.Submit(other); channel.Submit(a);
    auto dedup=drain();
    if(dedup.size()!=1 || dedup.front().first!="1/0")return 3;
    if(!start())return 4;
    channel.Submit(other);
    auto context=drain();
    if(context.size()!=2 || context.back().second.left_context!="A")return 5;
    if(!start())return 6;
    other=a; ++other.interaction_generation;
    channel.Submit(other);
    auto gen=drain();
    if(gen.size()!=2 || gen.back().second.interaction_generation!=other.interaction_generation)return 7;
    if(!start())return 8;
    channel.Learn(a,"fixture");channel.Submit(a);
    auto learned=drain();
    if(learned.size()!=2 || learned.front().first!="6/0" || learned.back().first!="7/1")return 9;
    a.raw_text = "flags";
    for (int bits = 0; bits < 8; ++bits) {
        a.punctuation_completion = (bits & 4) != 0;
        a.shallow_search = (bits & 2) != 0;
        a.dictionary_preview = (bits & 1) != 0;
        channel.Submit(a);
        const auto replies = drain();
        const std::string expected = std::string(a.punctuation_completion ? "1" : "0") +
            (a.shallow_search ? "1" : "0") + (a.dictionary_preview ? "1" : "0");
        if (replies.size() != 1 || replies.front().first != expected) return 10;
    }
    a.raw_text="stream";a.punctuation_completion=false;a.dictionary_preview=false;a.shallow_search=false;
    channel.Submit(a);
    std::vector<std::string> stream;
    const auto until=GetTickCount64()+5000;
    while(channel.busy() && GetTickCount64()<until) {
        bool interim=false;
        if (channel.Poll(&reply,&values,&interim)) {
            if (interim!=channel.busy() || reply.raw_text!=a.raw_text) return 11;
            stream.push_back(values.at(0).output_text);
        }
        Sleep(1);
    }
    // The channel may coalesce unread previews. The final reply must always win.
    if (stream.empty() || stream.back()!="done" || stream.size()>4) return 12;
    channel.Submit(a);
    const auto oldClient=drain();
    if (oldClient.size()!=1 || oldClient.front().first=="done") return 13;
    // A canceled edit can immediately become current again. Older hosts can
    // still send a full response. Drain it and learn before retrying.
    a.raw_text="cancel";
    channel.Submit(a);
    auto wait_frame=[&](bool want_preview, const std::string& text) {
        const auto deadline=GetTickCount64()+3000;
        while (channel.busy() && GetTickCount64()<deadline) {
            bool preview=false;
            if (channel.Poll(&reply,&values,&preview))
                return preview==want_preview && values.size()==1 && values.front().output_text==text;
            Sleep(1);
        }
        return false;
    };
    if (!wait_frame(true,"armed")) return 14;
    auto edited=a;
    ++edited.revision;
    channel.CancelObsolete(edited);
    channel.Learn(a,"kept");
    channel.Submit(a);
    if (!wait_frame(false,"retried/2") || channel.busy()) return 15;
    // Hold the second conversion behind the first. After consuming the first
    // reply, the next result must notify us without an intervening timer poll.
    HWND window=CreateWindowExW(0,L"STATIC",L"",0,0,0,0,0,HWND_MESSAGE,nullptr,nullptr,nullptr);
    if (!window || !channel.Start(exe,{},window)) return 16;
    const auto wait_for_reply=[&] {
        const auto deadline=GetTickCount64()+3000;
        while (GetTickCount64()<deadline) {
            MSG message{};
            if (PeekMessageW(&message,window,ime::kEngineReplyMessage,ime::kEngineReplyMessage,PM_REMOVE))
                return !channel.AcceptReplyWake(static_cast<DWORD>(message.wParam),static_cast<DWORD>(message.lParam)+1) &&
                    channel.AcceptReplyWake(static_cast<DWORD>(message.wParam),static_cast<DWORD>(message.lParam));
            MsgWaitForMultipleObjects(0,nullptr,FALSE,25,QS_POSTMESSAGE);
        }
        return false;
    };
    a.raw_text="first";
    a.phase="end_of_phrase";
    channel.Submit(a);
    if (channel.Poll(&reply,&values) || channel.AcceptReplyWake(0,0)) return 17;
    a.raw_text="second"; ++a.revision;
    channel.Submit(a);
    if (!wait_for_reply() || !channel.Poll(&reply,&values) || reply.raw_text!="first") return 18;
    if (!wait_for_reply() || !channel.Poll(&reply,&values) || reply.raw_text!="second" || channel.busy()) return 19;
    channel.Stop(); DestroyWindow(window);
    std::cout<<"PASS duplicates coalesced; latest input, generation and learning preserved.\n";
}

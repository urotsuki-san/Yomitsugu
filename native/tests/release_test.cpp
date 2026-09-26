#include <windows.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "ime_engine.h"
#include "engine_channel.h"
#include "user_dictionary.h"
#include "learning_store.h"
#include "dictionary_status.h"
namespace {
int failures = 0, checks = 0;
void Check(bool pass, const char* name) { ++checks; if (!pass) ++failures; std::cout << (pass ? "PASS " : "FAIL ") << name << '\n'; }
ime::Session Preview() { ime::Session s; s.set_deferred_decoding(true); s.set_learning_allowed(false); return s; }
bool Contains(const std::vector<ime::Candidate>& list, const std::string& word) {
  return std::any_of(list.begin(),list.end(),[&](const auto& c){return c.output_text == word;});
}
}
int wmain(int argc, wchar_t** argv) {
  using namespace ime;
  {
    const auto folder=std::filesystem::temp_directory_path()/(L"yomitsugu_status_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(GetTickCount64()));
    std::filesystem::create_directory(folder);
    const auto bundled=folder/L"bundled.tsv",cached=folder/L"public_dictionary.tsv";
    {std::ofstream file(bundled);file<<"test";}
    {std::ofstream file(folder/L"bundled.sources.json");file<<R"({"format_version":2,"entries":299521,"output_sha256":"original"})";}
    auto status=ReadDictionaryStatus(bundled,cached);
    Check(status.bundled&&status.result=="unknown"&&status.entries==299521,"bundled dictionary is not presented as recently checked");
    {std::ofstream file(cached);file<<"test";}
    {std::ofstream file(folder/L"public_dictionary.sources.json");file<<R"({"format_version":2,"entries":299521,"output_sha256":"original","checked_at_utc":"2026-09-26T01:00:00Z","update_result":"unchanged"})";}
    status=ReadDictionaryStatus(bundled,cached);
    Check(status.bundled&&status.result=="unchanged"&&!status.checked_at.empty()&&status.updated_at.empty(),"unchanged bundled data records check separately from update");
    {std::ofstream file(folder/L"public_dictionary.sources.json");file<<R"({"format_version":2,"entries":299522,"output_sha256":"new","updated_at_utc":"2026-09-26T01:00:00Z","checked_at_utc":"2026-09-26T01:02:00Z","update_result":"unchanged"})";}
    status=ReadDictionaryStatus(bundled,cached);
    Check(!status.bundled&&status.updated_at!=status.checked_at&&status.result=="unchanged","update date survives a later unchanged check");
    {std::ofstream file(folder/L"public_dictionary.status.json");file<<R"({"format_version":1,"checked_at_utc":"2026-09-26T01:03:00Z","result":"failed"})";}
    status=ReadDictionaryStatus(bundled,cached);
    Check(status.result=="failed"&&status.updated_at=="2026-09-26T01:00:00Z","failed check preserves installed dictionary update date");
    Check(DictionaryTimeLabel("bad")==L"記録なし"&&DictionaryTimeLabel("2026-02-31T00:00:00Z")==L"記録なし","invalid dictionary dates rejected");
    Check(DictionaryTimeLabel("2026-09-26T01:00:00Z")!=L"記録なし","UTC check date rendered in local time");
    {std::ofstream file(folder/L"public_dictionary.status.json");file<<"broken";}
    Check(ReadDictionaryStatus(bundled,cached).result=="unchanged","damaged attempt status falls back to valid provenance");
    for(const auto& file:std::filesystem::directory_iterator(folder)) std::filesystem::remove(file.path());
    std::filesystem::remove(folder);
  }
  wchar_t own_module[32768]{}; GetModuleFileNameW(nullptr, own_module, 32768);
  auto public_host = std::filesystem::path(own_module).parent_path()/L"ime_engine_host.exe";
  if (argc == 2) public_host = argv[1];
  UserDictionary public_dictionary; std::string public_error;
  Check(public_dictionary.Load(public_host.parent_path()/L"public_dictionary.tsv", &public_error, true) && public_dictionary.size() >= 3000,
        "public dictionary loaded from engine directory");
  auto DecodeWithPublic = [&](DecodeInput input) {
    auto list = Decode(input); public_dictionary.Apply(input, &list, true); return list;
  };
  auto parsed = ParseAzookeyCandidates(R"([{"correspondingCount":2,"text":"A"},{"text":"\u3042","correspondingCount":9}])");
  Check(parsed.size()==2 && parsed[0].corresponding_count==2 && parsed[1].text==u8"あ", "JSON field order and Unicode escapes");
  Check(ParseAzookeyCandidates(R"([{"text":"x","correspondingCount":-1},{"text":"x","correspondingCount":18446744073709551615}])").empty(), "reject invalid coverage counts");
  Check(ParseAzookeyCandidates("not JSON").empty(), "reject malformed JSON");
  { auto s=Preview(); s.Type(std::string(511,'x')); s.Type(u8"😀"); Check(s.raw_text()==std::string(511,'x')+u8"😀", "UTF8 input crosses old cap intact");
    s.Type(std::string(1000,'a')); Check(s.raw_text().size()==1515,"long input preserved"); s.Backspace(); Check(s.raw_text().size()==1514,"long input remains editable"); }
  { auto s=Preview(); s.Type("abc"); s.DeleteForward(); Check(s.raw_text()=="abc","Delete at end is no-op");
    s.PressLeft(); s.DeleteForward(); Check(s.raw_text()=="ab","Delete acts at caret");
    s.PressHome(); s.Type("x"); Check(s.raw_text()=="xab","insertion acts at caret");
    s.PressEnd(); s.Backspace(); Check(s.raw_text()=="xa","Backspace acts before caret"); }
  { auto s=Preview(); s.Type("gakkou"); s.PressSpace(); s.PressEscape(); Check(s.raw_text()=="gakkou"&&!s.is_converting(),"Esc restores reading");
    s.PressEscape(); Check(!s.composing(),"second Esc clears composition"); }
  { auto s=Preview(); s.Type("gakkou"); s.PressSpace(); s.Backspace(); Check(s.raw_text()=="gakkou"&&!s.is_converting(),"Backspace cancels conversion before deleting"); }
  { auto s=Preview(); s.Type("arigatougozaimasu"); s.Backspace(); Check(s.raw_text()=="arigatougozaimas" && s.visible_text()!=u8"ありがとうございます","deletion never silently completes removed syllable"); }
  { bool ok=true; Check(ConvertRomaji("k",&ok)=="k"&&!ok,"incomplete consonant remains literal"); }
  { bool ok=false; Check(ConvertRomaji("ltu",&ok)==u8"っ"&&ok,"ltu small tsu");
    Check(ConvertRomaji("xtu",&ok)==u8"っ"&&ok,"xtu small tsu");
    Check(ConvertRomaji("ltsu",&ok)==u8"っ"&&ok,"ltsu small tsu");
    Check(ConvertRomaji("xtsu",&ok)==u8"っ"&&ok,"xtsu small tsu");
    Check(ConvertRomaji("samukunaltutekimasitane",&ok)==u8"さむくなってきましたね"&&ok,"user sentence romaji reading");
    Check(ConvertRomaji("saikilyou",&ok)==u8"さいきょう"&&ok,"saikilyou reading");
    Check(ConvertRomaji(".",&ok)==u8"。"&&ok,"period reading is Japanese punctuation"); }
  { auto s=Preview(); s.Type("."); Check(s.visible_text()==u8"。","period previews as Japanese punctuation"); }
  { const std::pair<const char*,const char*> vowels[]={{"a",u8"あ"},{"i",u8"い"},{"u",u8"う"},{"e",u8"え"},{"o",u8"お"}};
    for (auto [raw,kana]:vowels) {
      auto s=Preview(); s.Type(raw); Check(s.visible_text()==kana,"single vowel previews as kana");
      DecodeInput in; in.raw_text=raw; auto list=Decode(in);
      Check(!list.empty()&&list.front().output_text==kana&&Contains(list,raw),"single vowel worker keeps kana first and raw fallback");
    }
    DecodeInput in; in.raw_text="a"; in.field="identifier";
    Check(!Decode(in).empty()&&Decode(in).front().output_text=="a","identifier vowel stays literal"); }
  { DecodeInput in; in.raw_text="ltu"; auto candidates=Decode(in);
    Check(!candidates.empty()&&candidates[0].output_text==u8"っ","ltu first candidate after worker update"); }
  { auto s=Preview(); s.Type("gakkou"); s.ReplaceVisible(u8"学校"); s.PressCharacterClass(0x75);
    Check(s.visible_text()==u8"がっこう"&&s.raw_text()=="gakkou","F6 uses original reading and preserves raw"); }
  Check(ConvertCharacterClass("ABC",0x78)==u8"ＡＢＣ","F9 fullwidth");
  Check(ConvertCharacterClass(u8"ＡＢＣ",0x79)=="ABC","F10 halfwidth");
  Check(ConvertCharacterClass(u8"ｶﾞ",0x75)==u8"が","halfwidth voiced kana normalized");
  { auto s=Preview(); s.Type("toshokan"); s.PressSpace(); auto request=s.decode_input();
    Candidate c; c.output_text=u8"図書館";
    Check(s.ApplyCandidates(request,{c})&&s.is_converting()&&s.visible_text()==u8"図書館", "first Space can receive pending dictionary result");
    s.PressDown(); request=s.decode_input(); Check(!s.ApplyCandidates(request,{c}),"explicit candidate navigation freezes results"); }
  { auto s=Preview(); s.Type("gakkou"); s.ReplaceVisible(u8"学校"); s.Type("a");
    Check(s.raw_text()=="a" && !s.output_log().empty() && s.output_log().back()==u8"学校","typing after selection preserves selected commit"); }
  { auto s=Preview(); s.Type("ka"); auto request=s.decode_input(); Candidate c; c.output_text=u8"蚊";
    s.Type("n"); Check(!s.ApplyCandidates(request,{c}),"stale revision result discarded");
    request=s.decode_input(); s.set_context("new",""); Check(!s.ApplyCandidates(request,{c}),"stale context result discarded");
    request=s.decode_input(); s.PressSpace(); Check(!s.ApplyCandidates(request,{c}),"manual selection never overwritten"); }
  { DecodeInput in; in.raw_text="konnitiwa"; in.candidate_limit=2; Check(Contains(Decode(in),in.raw_text),"raw retained in bounded candidates");
    in.candidate_limit=0; Check(Decode(in).empty(),"zero candidate limit"); }
  { auto path=std::filesystem::temp_directory_path()/(L"ime_dictionary_test_"+std::to_wstring(GetCurrentProcessId())+L".tsv");
    {std::ofstream out(path,std::ios::binary); out << u8"こでっくす\tCodex\t名詞\nこでっくす\tCodex\t名詞\n";}
    UserDictionary dict; std::string error; Check(dict.Load(path,&error)&&dict.size()==1,"user dictionary validation and deduplication");
    DecodeInput in; in.raw_text="kodekkusu"; in.candidate_limit=2; std::vector<Candidate> list; dict.Apply(in,&list);
    Check(!list.empty()&&list[0].output_text=="Codex"&&Contains(list,in.raw_text),"user dictionary priority plus literal escape");
    {std::ofstream out(path,std::ios::binary); out << "invalid row\n";}
    Check(!dict.Load(path,&error)&&dict.size()==1,"invalid import preserves previous dictionary");
    std::filesystem::remove(path); }
  Check(AzookeyEnsureReady(),"real dictionary required (no soft skip)");
  {
    auto directory = std::filesystem::temp_directory_path() / (L"yomitsugu_learning_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64()));
    LearningStore learning(directory);
    DecodeInput input; input.raw_text="hashi";
    auto base=[] { Candidate first; first.output_text=u8"橋"; Candidate second; second.output_text=u8"端"; return std::vector<Candidate>{first,second}; };
    Check(learning.enabled(),"learning enabled for a new profile");
    Check(learning.Record(input,u8"端"),"committed candidate saved");
    LearningStore reopened(directory); auto list=base(); reopened.Apply(input,&list);
    Check(list.front().output_text==u8"端"&&Contains(list,"hashi"),"learning persists and keeps original input available");
    Check(learning.Record(input,u8"橋")&&learning.Record(input,u8"橋"),"repeated selections counted");
    list=base(); reopened.Apply(input,&list);
    Check(list.front().output_text==u8"橋","frequent selection wins after another process updates history");
    const auto same_tick = std::filesystem::last_write_time(directory/L"learning.json");
    Check(learning.Record(input,u8"端"),"recent selection counted");
    std::filesystem::last_write_time(directory/L"learning.json",same_tick);
    list=base(); reopened.Apply(input,&list);
    Check(list.front().output_text==u8"端","recent selection breaks equal counts");
    Check(std::filesystem::last_write_time(directory/L"learning.json")==same_tick && list.front().output_text==u8"端",
          "learning refreshes when replaced file keeps the same timestamp");
    Check(learning.SetEnabled(false)&&!reopened.enabled(),"learning setting shared between instances");
    Check(!learning.Record(input,u8"箸"),"disabled learning does not save");
    list=base(); reopened.Apply(input,&list);
    Check(list.front().output_text==u8"橋","disabled learning does not reorder candidates");
    Check(learning.SetEnabled(true),"learning can be enabled again");
    for (const auto& field:{"password","identifier","code","url","email"}) {
      input.field=field;
      Check(!learning.Record(input,u8"端"),"protected input excluded from learning");
    }
    input.field="prose";input.raw_text="https://example.com";
    Check(!learning.Record(input,u8"端"),"URL text excluded from learning");
    input.raw_text="hashi";
    Check(!learning.Record(input,"hashi"),"literal input excluded from learning");
    Check(learning.Clear()&&reopened.size()==0,"history deletion reaches other instances");
    {std::ofstream stream(directory/L"learning.json");stream<<"{\"version\":\"invalid\",\"entries\":[]}";}
    list=base(); reopened.Apply(input,&list);
    Check(list.front().output_text==u8"橋","invalid history falls back to dictionary order");
    std::filesystem::remove(directory/L"learning.json");std::filesystem::remove(directory/L"settings.json");std::filesystem::remove(directory);
    auto session=Preview();session.Type("insuto-ru");
    Check(!session.candidates_ready(),"typing distinguishes provisional display from conversion results");
    Candidate candidate;candidate.output_text=u8"インストール";
    Check(session.ApplyCandidates(session.decode_input(),{candidate})&&session.candidates_ready(),"matching engine result marks candidates ready");
    session.PressEnter();
    Check(session.last_commit_learnable(),"resolved conversion can be learned after commit");
    session.Type("ha");Check(!session.candidates_ready(),"next input cannot reuse previous readiness");
  }
  { const char* words[]={"software","hardware","online","version","computer","folder","password","server","database"};
    for(auto raw:words) { DecodeInput in;in.raw_text=raw;auto list=DecodeWithPublic(in);
      Check(!list.empty()&&list.front().output_text==raw,"dictionary English spelling remains literal"); }
    for(auto raw:{"sushi","anime","karaoke","manga","sushiwotaberu","kanawonyuuryoku"}) { DecodeInput in;in.raw_text=raw;
      auto base=Decode(in),list=DecodeWithPublic(in);
      Check(!base.empty()&&!list.empty()&&base.front().output_text==list.front().output_text,"English glossary does not override valid Japanese reading"); }
    DecodeInput in;in.raw_text="softwarewokoushinsuru";auto list=DecodeWithPublic(in);
    Check(!list.empty()&&list.front().output_text==u8"softwareを更新する","dictionary English prefix supports continuous Japanese suffix");
  }
  { const std::pair<const char*,const char*> rules[] = {
      {"we",u8"うぇ"},{"wi",u8"うぃ"},{"whe",u8"うぇ"},{"twu",u8"とぅ"},
      {"dwu",u8"どぅ"},{"she",u8"しぇ"},{"che",u8"ちぇ"},{"vya",u8"ゔゃ"},
      {"matcha",u8"まっちゃ"},{"n'i",u8"んい"},{"nni",u8"んに"},{"xn",u8"ん"}};
    bool all=true;
    for (auto [raw,expected]:rules) { bool ok=false; if(ConvertRomaji(raw,&ok)!=expected || !ok) all=false; }
    Check(all,"standard romaji rules including pending consonants and explicit n boundary"); }
  { const std::pair<const char*,const char*> corrections[] = {
      {"aninsuto-ru",u8"アンインストール"},{"anninsuto-ru",u8"アンインストール"},
      {"insutro-ru",u8"インストール"},{"puroguramnigu",u8"プログラミング"},
      {"pasuaw-do",u8"パスワード"},{"bakkauppu",u8"バックアップ"},
      {"deta-be-su",u8"データベース"},{"ki--bo-do",u8"キーボード"}};
    for(auto [raw,expected]:corrections) {
      DecodeInput in;in.raw_text=raw;in.candidate_limit=6;
      auto list=DecodeWithPublic(in);
      Check(Contains(list,expected)&&Contains(list,raw),raw);
    }
    for(const auto* field:{"code","identifier","password"}) {
      DecodeInput in;in.raw_text="aninsuto-ru";in.field=field;
      auto list=DecodeWithPublic(in);
      Check(!list.empty()&&list.front().output_text==in.raw_text,"literal field never typo-corrected");
    }
  }
  { const std::pair<const char*,const char*> sequences[] = {
      {"insuto-rusitekudasai",u8"インストールしてください"},
      {"konpyu-ta-wotukaimasu",u8"コンピューターを使います"},
      {"aninsuto-rusitekudasai",u8"アンインストールしてください"},
      {"sofutowea",u8"ソフトウェア"},
      {"READMEwokousinnsitekudasaiGithubde",u8"READMEを更新してくださいGithubで"}};
    for(auto [raw,expected]:sequences) {
      auto s=Preview();std::string prefix;bool intact=true;
      for(char ch:std::string(raw)) {
        prefix+=ch;s.Type(std::string(1,ch));auto request=s.decode_input();
        auto list=DecodeWithPublic(request);s.ApplyCandidates(request,list);
        intact=intact && s.raw_text()==prefix;
      }
      Check(intact&&s.visible_text()==expected,"continuous input preserves all keystrokes and converts final text");
      s.Backspace();
      Check(s.raw_text()==prefix.substr(0,prefix.size()-1),"correction remains editable through original keystrokes");
    }
  }
  { auto dir=std::filesystem::temp_directory_path()/(L"ime_public_cache_"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(dir);
    auto bundled=dir/L"bundled.tsv",cached=dir/L"public_dictionary.tsv",metadata=dir/L"public_dictionary.sources.json";
    {std::ofstream out(cached);out<<"# old cache\n";}
    Check(PublicDictionaryPath(bundled,cached)==bundled,"legacy public cache cannot mask expanded bundle");
    {std::ofstream out(metadata);out<<"{\"format_version\":2}";}
    Check(PublicDictionaryPath(bundled,cached)==cached,"versioned public update is selected");
    {std::ofstream out(metadata);out<<"invalid json";}
    Check(PublicDictionaryPath(bundled,cached)==bundled,"invalid update metadata falls back to bundled data");
    std::filesystem::remove(metadata);std::filesystem::remove(cached);std::filesystem::remove(dir);
  }
  auto one=AzookeyConvert(u8"はし",1); auto many=AzookeyConvert(u8"はし",8);
  Check(one.size()==1&&many.size()>1,"cache independent of requested limit");
  const std::pair<const char*,const char*> words[]{
    {"kaigi",u8"会議"},{"keikaku",u8"計画"},{"iryou",u8"医療"},{"nougyou",u8"農業"},
    {"kankyou",u8"環境"},{"seiseki",u8"成績"},{"shinkansen",u8"新幹線"},{"toshokan",u8"図書館"},
    {"yuubinkyoku",u8"郵便局"},{"kakuteishinkoku",u8"確定申告"},{"jinkouchinou",u8"人工知能"},
    {"koutsuu",u8"交通"},{"keizai",u8"経済"},{"shakai",u8"社会"},{"kenkyuu",u8"研究"},
    {"shinsei",u8"申請"},{"kensa",u8"検査"},{"ryokou",u8"旅行"},{"shorui",u8"書類"},{"renraku",u8"連絡"}};
  for (auto [reading,word]:words) { DecodeInput in; in.raw_text=reading; Check(Contains(Decode(in),word),reading); }
  { DecodeInput in; in.raw_text="samukunaltutekimasitane";
    auto candidates=Decode(in);
    Check(!candidates.empty()&&candidates[0].output_text==u8"寒くなってきましたね","user sentence first candidate");
    in.raw_text="yajirushi"; Check(Contains(DecodeWithPublic(in),u8"→"),"arrow candidate from romaji public dictionary");
    Check(DecodeWithPublic(in).front().output_text==u8"→","arrow stays first when general dictionary also contains its name");
    in.raw_text=u8"やじるし"; Check(Contains(DecodeWithPublic(in),u8"→"),"arrow candidate from kana public dictionary");
    in.raw_text="saikilyou"; candidates=Decode(in);
    Check(!candidates.empty()&&candidates[0].output_text==u8"最強","saikilyou first candidate is intended kanji");
    in.raw_text="."; candidates=Decode(in);
    Check(!candidates.empty()&&candidates[0].output_text==u8"。"&&Contains(candidates,"."),"period prefers Japanese punctuation with raw fallback");
    in.field="identifier"; candidates=Decode(in);
    Check(!candidates.empty()&&candidates[0].output_text==".","period stays literal in identifier field");
    in.field="prose"; in.raw_text="samukunaltutekimasitane."; candidates=Decode(in);
    Check(!candidates.empty()&&candidates[0].output_text==u8"寒くなってきましたね。","sentence period first candidate"); }
  { const std::pair<const char*,const char*> symbols[] = {
      {"maru",u8"○"},{"sankaku",u8"△"},{"sikaku",u8"□"},
      {"hosi",u8"☆"},{"onpu",u8"♪"},{"kakeru",u8"×"},
      {"waru",u8"÷"},{"kakko",u8"（）"},
      {"migi",u8"→"},{"hidari",u8"←"}};
    for (auto [reading,symbol]:symbols) {
      DecodeInput in; in.raw_text=reading;
      Check(Contains(DecodeWithPublic(in),symbol),reading);
    }
    DecodeInput in; in.raw_text="maru"; in.field="identifier";
    Check(!Contains(DecodeWithPublic(in),u8"○"),"public symbols disabled in identifier field");
    in.field="prose"; in.raw_text="ri-domi-";
    auto candidates = DecodeWithPublic(in);
    Check(!candidates.empty() && candidates.front().output_text == "README", "README from public computing dictionary");
    in.raw_text="ri-domi-wokousinnsitekudasaiGithubde";
    candidates=DecodeWithPublic(in);
    Check(!candidates.empty() && candidates.front().output_text==u8"READMEを更新してくださいGithubで",
          "public dictionary prefix survives long mixed input");
    in.raw_text="shikaku"; auto lexical=Decode(in); candidates=DecodeWithPublic(in);
    Check(!lexical.empty() && !candidates.empty() && candidates.front().output_text==lexical.front().output_text && Contains(candidates,u8"□"),
          "symbol dictionary preserves ordinary word top candidate"); }
  {
    Check(AzookeySetUserDictionary(u8R"([{"reading":"こでっくすてすと","word":"CodexTestUnit","pos":"proper_noun"}])"), "dynamic dictionary accepted by real engine");
    auto path=std::filesystem::temp_directory_path()/(L"ime_sentence_dictionary_"+std::to_wstring(GetCurrentProcessId())+L".tsv");
    {std::ofstream file(path,std::ios::binary);file<<u8"こでっくすてすと\tCodexTestUnit\t固有名詞\n";}
    UserDictionary dictionary;std::string error;
    Check(dictionary.Load(path,&error),"sentence dictionary fixture validates");
    DecodeInput input;input.raw_text="kodekkusutesutowotsukau";
    auto list=Decode(input);dictionary.Apply(input,&list);
    Check(Contains(list,u8"CodexTestUnitを使う"), "registered word available inside sentence with suffix preserved");
    input.raw_text="kodekkusutesutowotsukau";
    input.field="identifier";list=Decode(input);dictionary.Apply(input,&list);
    Check(list.front().output_text==input.raw_text,"user dictionary does not rewrite identifier fields");
    std::filesystem::remove(path);
    Check(AzookeySetUserDictionary("[]"), "dynamic dictionary clearing invalidates cached candidates");
  }
  { wchar_t module[32768]{}; GetModuleFileNameW(nullptr,module,32768);
    auto host=std::filesystem::path(module).parent_path()/L"ime_engine_host.exe";
    if (argc == 2) host = argv[1];
    const auto profile=std::filesystem::temp_directory_path()/(L"yomitsugu_worker_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(GetTickCount64()));
    EngineChannel channel; Check(channel.Start(host.wstring(),profile.wstring()),"isolated engine starts with private pipes");
    DecodeInput request; request.raw_text="toshokan"; channel.Submit(request);
    bool done=false; std::vector<Candidate> list; DecodeInput reply;
    auto until=GetTickCount64()+30000;
    while(GetTickCount64()<until&&channel.running()) { if(channel.Poll(&reply,&list)){done=true;break;} Sleep(5); }
    Check(done && reply.raw_text==request.raw_text && Contains(list,u8"図書館"),"real dictionary round trip through worker");
    request.raw_text="ri-domi-"; channel.Submit(request); done=false; list.clear();
    until=GetTickCount64()+30000;
    while(GetTickCount64()<until&&channel.running()) { if(channel.Poll(&reply,&list)){done=true;break;} Sleep(5); }
    Check(done && reply.raw_text==request.raw_text && !list.empty() && list.front().output_text=="README",
          "public dictionary round trip through worker");
    request.raw_text="sannkai"; channel.Learn(request,u8"散会"); channel.FinishLearning(5000);
    channel.Stop();
    Check(channel.Start(host.wstring(),profile.wstring()),"worker restarts with saved learning");
    channel.Submit(request);done=false;list.clear();until=GetTickCount64()+30000;
    while(GetTickCount64()<until&&channel.running()){if(channel.Poll(&reply,&list)){done=true;break;}Sleep(5);}
    Check(done&&!list.empty()&&list.front().output_text==u8"散会","chosen candidate survives worker restart");
    channel.Stop(); Check(!channel.running(),"owned worker stops without touching applications");
    std::filesystem::remove(profile/L"learning.json");std::filesystem::remove(profile/L"settings.json");std::filesystem::remove(profile);
    Check(!channel.Start((host.parent_path()/L"missing_engine.exe").wstring()),"missing worker degrades safely"); }
  { auto s=Preview(); std::vector<double> timings;
    for(char c:std::string("ashitahakaishadeatarashiikikakuwosoudanshimasu")) {
      auto begin=std::chrono::steady_clock::now();s.Type(std::string(1,c));
      timings.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());
    }
    std::sort(timings.begin(),timings.end());
    double p95=timings[static_cast<size_t>(timings.size()*0.95)];
    std::cout<<"latency preview p95="<<p95<<"ms n="<<timings.size()<<" (engine preview only)\n";
    Check(p95<10,"new-input preview latency under 10ms"); }
  std::cout<<"TOTAL "<<checks<<" checks; FAILED "<<failures<<'\n'; return failures?1:0;
}

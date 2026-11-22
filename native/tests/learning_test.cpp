#include "learning_store.h"
#include <windows.h>
#include <iostream>
#include <fstream>

int main() {
  {
    ime::Session session; session.set_deferred_decoding(true); session.Type("nihongo");
    for(int i=0;i<5000;++i) session.PressCharacterClass(VK_F6+i%5);
    if(session.candidates().size()>7 || session.visible_text()!="nihongo") return 1;
    std::cout<<"PASS 5000 character-class changes keep candidate storage bounded\n";
  }
  const auto folder=std::filesystem::temp_directory_path()/(L"yomitsugu_learning_cache_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(GetTickCount64()));
  ime::LearningStore writer(folder),reader(folder);
  if (reader.model()!=ime::ConversionModel::Automatic) return 1;
  for(auto model : {ime::ConversionModel::Standard,ime::ConversionModel::Automatic,ime::ConversionModel::HybridLowMemory,ime::ConversionModel::HybridFull,ime::ConversionModel::HybridDictionaryVerify}) {
    if(!writer.SetModel(model) || reader.model()!=model) return 1;
  }
  ime::DecodeInput input; input.raw_text="hashi";
  {
    ime::DecodeInput roman; roman.raw_text = "sushi";
    ime::Session session; session.set_deferred_decoding(true); session.Type("sushi");
    session.PressCharacterClass(VK_F10); session.PressEnter();
    if (!session.last_commit_f10()) return 1;
    if (writer.Record(roman, "sushi") || !writer.Record(roman, "sushi", true, true)) return 1;
    std::vector<ime::Candidate> words{ime::Candidate{u8"寿司"}};
    reader.Apply(roman, &words);
    if (words.front().output_text != u8"寿司" || !writer.Record(roman, "sushi", true, true)) return 1;
    roman.raw_text = "SUSHI"; reader.Apply(roman, &words);
    if (!words.front().is_raw || words.front().output_text != "SUSHI") return 1;
    roman.field = "password"; if (writer.Record(roman, "SUSHI", true, true)) return 1;
    if (!writer.SetEnabled(false) || !writer.SetModel(ime::ConversionModel::Compact) ||
        reader.enabled() || reader.model() != ime::ConversionModel::Compact) return 1;
    if (!writer.SetModel(ime::ConversionModel::CompactExtendedSearch) ||
        reader.model() != ime::ConversionModel::CompactExtendedSearch || reader.enabled()) return 1;
    if (!writer.Clear() || !writer.SetEnabled(true)) return 1;
    std::filesystem::remove(folder / L"settings.json");
    std::cout << "PASS explicit F10 learning, case, sensitive scope and model settings\n";
  }
  const auto fixed_stamp=std::filesystem::file_time_type::clock::now();
  int passed=0;
  for(int i=0;i<100;++i) {
    const std::string chosen=i%2?u8"端":u8"橋";
    if(!writer.Record(input,chosen)) return 1;
    // 時計が進まない環境でも、置換された履歴を読み直せるか確認する。
    std::filesystem::last_write_time(folder/L"learning.json",fixed_stamp);
    ime::Candidate original; original.output_text=u8"箸";
    std::vector<ime::Candidate> candidates{original}; reader.Apply(input,&candidates);
    if(candidates.empty() || candidates.front().output_text!=chosen) {
      std::cerr<<"FAIL learning cache at iteration "<<i<<'\n'; return 1;
    }
    ++passed;
  }
  if(!writer.Clear() || reader.size()!=0) return 1;
  for (int i=0;i<20;++i) if(!writer.Record(input,u8"橋",false)) return 1;
  if(!writer.Record(input,u8"箸",true)) return 1;
  for (int i=0;i<20;++i) if(!writer.Record(input,u8"橋",false)) return 1;
  {
    std::vector<ime::Candidate> list;
    reader.Apply(input,&list);
    if(list.empty() || list.front().output_text!=u8"箸") return 1;
    if(!writer.Record(input,u8"端",true)) return 1;
    reader.Apply(input,&list);
    if(list.empty() || list.front().output_text!=u8"端") return 1;
    std::cout<<"PASS explicit correction overrides frequent automatic commits and can be changed again\n";
  }
  if(!writer.Clear()) return 1;
  {
    ime::DecodeInput request;request.raw_text="kiru";request.left_context=u8"この紙を";
    if(!writer.Record(request,u8"切る",true)) return 1;
    request.left_context=u8"新しい服を";
    if(!writer.Record(request,u8"着る",true)) return 1;
    std::vector<ime::Candidate> list;
    reader.Apply(request,&list);
    if(list.empty() || list.front().output_text!=u8"着る") return 1;
    request.left_context=u8"この紙を";list.clear();reader.Apply(request,&list);
    if(list.empty() || list.front().output_text!=u8"切る") return 1;
    request.left_context.clear();list.clear();reader.Apply(request,&list);
    if(!list.empty()) return 1;
    std::cout<<"PASS choices in different contexts do not override each other\n";
  }
  if(!writer.Clear()) return 1;
  if(!writer.Record(input,u8"橋")) return 1;
  HANDLE locked=CreateFileW((folder/L"learning.json").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
  if(locked==INVALID_HANDLE_VALUE) return 1;
  const bool rejected=!writer.Record(input,u8"端") && !writer.Clear();
  ime::Candidate original; original.output_text=u8"箸";
  std::vector<ime::Candidate> candidates{original}; writer.Apply(input,&candidates);
  CloseHandle(locked);
  if(!rejected || candidates.front().output_text!=u8"橋" || writer.size()!=1) return 1;
  std::cout<<"PASS failed save and clear preserve persisted learning in memory\n";
  wchar_t volume[MAX_PATH]{};
  DWORD flags = 0;
  if (!GetVolumePathNameW(folder.c_str(), volume, MAX_PATH) ||
      !GetVolumeInformationW(volume, nullptr, 0, nullptr, nullptr, &flags, nullptr, 0)) return 1;
  if (flags & FILE_SUPPORTS_POSIX_UNLINK_RENAME) {
    HANDLE snapshot = CreateFileW((folder / L"learning.json").c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (snapshot == INVALID_HANDLE_VALUE) return 1;
    const bool saved = writer.Record(input, u8"端");
    char previous[1024]; DWORD bytes = 0;
    const bool read = ReadFile(snapshot, previous, sizeof(previous), &bytes, nullptr) != FALSE;
    CloseHandle(snapshot);
    const std::string old(previous, bytes);
    candidates = {original}; reader.Apply(input, &candidates);
    if (!saved || !read || old.find(u8"橋") == std::string::npos ||
        old.find(u8"端") != std::string::npos || candidates.front().output_text != u8"端") return 1;
    std::cout<<"PASS concurrent read keeps old learning and next read sees new selection\n";
  }
  if(writer.Record(input,std::string("\xff")) || writer.Record(input,std::string(u8"橋\0端",7))) return 1;
  std::cout<<"PASS malformed UTF-8 and embedded NUL are rejected\n";
  {
    std::ofstream file(folder/L"learning.json",std::ios::binary|std::ios::trunc);
    file<<u8R"({"version":1,"entries":[{"key":"はし","text":"破損","count":-1,"used":1},{"key":"はし","text":"橋","count":1,"used":2}]})";
  }
  ime::LearningStore validated(folder);
  candidates={original}; validated.Apply(input,&candidates);
  if(validated.size()!=1 || candidates.front().output_text!=u8"橋") return 1;
  std::cout<<"PASS negative learning count cannot dominate valid candidates\n";
  std::filesystem::remove(folder/L"learning.json"); std::filesystem::remove(folder);
  std::cout<<"PASS learning cache: "<<passed<<" rapid updates with identical modification timestamps, then clear\n";
  return 0;
}

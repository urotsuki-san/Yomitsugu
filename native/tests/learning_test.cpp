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
  ime::DecodeInput input; input.raw_text="hashi";
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
  if(!writer.Record(input,u8"橋")) return 1;
  HANDLE locked=CreateFileW((folder/L"learning.json").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
  if(locked==INVALID_HANDLE_VALUE) return 1;
  const bool rejected=!writer.Record(input,u8"端") && !writer.Clear();
  ime::Candidate original; original.output_text=u8"箸";
  std::vector<ime::Candidate> candidates{original}; writer.Apply(input,&candidates);
  CloseHandle(locked);
  if(!rejected || candidates.front().output_text!=u8"橋" || writer.size()!=1) return 1;
  std::cout<<"PASS failed save and clear preserve persisted learning in memory\n";
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

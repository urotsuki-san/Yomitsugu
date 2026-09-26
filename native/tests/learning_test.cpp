#include "learning_store.h"
#include <windows.h>
#include <iostream>

int main() {
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
  std::filesystem::remove(folder/L"learning.json"); std::filesystem::remove(folder);
  std::cout<<"PASS learning cache: "<<passed<<" rapid updates with identical modification timestamps, then clear\n";
  return 0;
}

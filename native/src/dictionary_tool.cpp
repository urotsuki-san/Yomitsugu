#include "user_dictionary.h"
#include <windows.h>
#include <iostream>
int wmain(int argc, wchar_t** argv) {
  if (argc != 3 || wcscmp(argv[1], L"--import")) {
    std::cerr << "Usage: ime_dictionary_tool --import UTF8-TSV-file\n"; return 2;
  }
  ime::UserDictionary dictionary; std::string error;
  if (!dictionary.Load(argv[2], &error)) { std::cerr << error << '\n'; return 3; }
  auto target = ime::UserDictionaryPath(); if (target.empty()) return 4;
  std::error_code ec; std::filesystem::create_directories(target.parent_path(),ec); if (ec) return 5;
  // Preserve the previous dictionary; replacement is atomic on the same volume.
  auto backup = target; backup += L".bak";
  if (std::filesystem::exists(target)) {
    std::filesystem::copy_file(target,backup,std::filesystem::copy_options::overwrite_existing,ec); if(ec) return 6;
  }
  auto temp = target; temp += L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
  std::filesystem::copy_file(argv[2],temp,std::filesystem::copy_options::none,ec); if(ec) return 7;
  if (!MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    std::filesystem::remove(temp,ec); return 8;
  }
  std::cout << "Imported " << dictionary.size() << " entries. Previous dictionary: user_dictionary.tsv.bak\n";
  return 0;
}

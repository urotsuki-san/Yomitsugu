#pragma once
#include <filesystem>
#include <map>
#include <set>
#include "ime_engine.h"
namespace ime {
class UserDictionary {
 public:
  bool Load(const std::filesystem::path& path, std::string* error);
  void Apply(const DecodeInput& input, std::vector<Candidate>* candidates,
             bool public_dictionary = false) const;
  size_t size() const { return count_; }
  const std::string& json() const { return json_; }
 private:
  std::map<std::string, std::vector<std::string>> entries_;
  std::set<std::string> symbol_only_readings_;
  size_t count_ = 0;
  std::string json_ = "[]";
};
std::filesystem::path UserDictionaryPath();
}

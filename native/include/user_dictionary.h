#pragma once
#include <filesystem>
#include <map>
#include <set>
#include "ime_engine.h"
namespace ime {
class UserDictionary {
 public:
  bool Load(const std::filesystem::path& path, std::string* error, bool public_dictionary = false);
  void Apply(const DecodeInput& input, std::vector<Candidate>* candidates,
             bool public_dictionary = false) const;
  size_t size() const { return count_; }
  const std::string& json() const { return json_; }
 private:
  void ApplyExact(const DecodeInput& input, std::vector<Candidate>* candidates, bool public_dictionary) const;
  void ApplyCorrections(const DecodeInput& input, std::vector<Candidate>* candidates) const;
  std::map<std::string, std::vector<std::string>> entries_;
  std::set<std::string> symbol_only_readings_;
  std::set<std::string> supplemental_readings_;
  std::set<std::string> priority_readings_;
  std::map<std::string, std::vector<std::string>> supplemental_words_;
  size_t count_ = 0;
  std::string json_ = "[]";
};
std::filesystem::path UserDictionaryPath();
std::filesystem::path PublicDictionaryPath(const std::filesystem::path& bundled,
                                           const std::filesystem::path& cached);
}

#pragma once
#include <filesystem>
#include <map>
#include <set>
#include <memory>
#include <string_view>
#include "ime_engine.h"
namespace ime {
class PublicIndex;
class UserDictionary {
 public:
  bool Load(const std::filesystem::path& path, std::string* error, bool public_dictionary = false);
  void Apply(const DecodeInput& input, std::vector<Candidate>* candidates,
             bool public_dictionary = false) const;
  bool HasPersonalEntry(const std::string& reading) const { return entries_.count(reading) != 0; }
  size_t size() const { return count_; }
  const std::string& json() const { return json_; }
  std::vector<std::pair<std::string,std::string>> UniqueWordsIn(const std::string& reading, bool* has_entry = nullptr) const;
 private:
  void ApplyExact(const DecodeInput& input, std::vector<Candidate>* candidates, bool public_dictionary) const;
  void ApplyCorrections(const DecodeInput& input, std::vector<Candidate>* candidates) const;
  bool ApplyEnglish(const DecodeInput& input, std::vector<Candidate>* candidates) const;
  unsigned Flags(std::string_view reading) const;
  std::vector<std::string> Words(std::string_view reading, bool supplemental_only = false) const;
  std::shared_ptr<const PublicIndex> public_index_;
  std::map<std::string, std::vector<std::string>> entries_;
  std::set<std::string> symbol_only_readings_;
  std::set<std::string> supplemental_readings_;
  std::set<std::string> priority_readings_;
  std::set<std::string> english_words_;
  std::map<std::string, std::vector<std::string>> supplemental_words_;
  size_t count_ = 0;
  std::string json_ = "[]";
};
std::filesystem::path UserDictionaryPath();
std::filesystem::path PublicDictionaryPath(const std::filesystem::path& bundled,
                                           const std::filesystem::path& cached);
}

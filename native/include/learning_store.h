#pragma once
#include <filesystem>
#include "ime_engine.h"

namespace ime {
class LearningStore {
 public:
  explicit LearningStore(std::filesystem::path directory);
  bool enabled() const;
  bool SetEnabled(bool enabled);
  ConversionModel model() const;
  bool SetModel(ConversionModel model);
  bool Clear();
  using WordChoice = std::pair<std::string,std::string>;
  bool Record(const DecodeInput& input, const std::string& chosen, bool explicit_selection = true,
              bool f10 = false, const WordChoice& word = {}, unsigned alternate_n = 0);
  std::vector<WordChoice> ExplicitWordsIn(const DecodeInput& input, const std::string& reading);
  void Apply(const DecodeInput& input, std::vector<Candidate>* candidates);
  size_t size();
 private:
  struct Entry { std::string key, text; unsigned count = 0; std::uint8_t alternate_n = 0; std::uint64_t used = 0, selected = 0; std::string context; WordChoice word; };
  static std::string Key(const DecodeInput& input);
  bool Load(bool force = false);
  bool Save();
  std::filesystem::path directory_, history_, settings_;
  std::uint64_t stamp_ = 0, file_id_ = 0, file_size_ = 0;
  std::vector<Entry> entries_;
};
}

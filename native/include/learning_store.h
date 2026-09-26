#pragma once
#include <filesystem>
#include "ime_engine.h"

namespace ime {
class LearningStore {
 public:
  explicit LearningStore(std::filesystem::path directory);
  bool enabled() const;
  bool SetEnabled(bool enabled);
  bool Clear();
  bool Record(const DecodeInput& input, const std::string& chosen);
  void Apply(const DecodeInput& input, std::vector<Candidate>* candidates);
  size_t size();
 private:
  struct Entry { std::string key, text; unsigned count = 0; std::uint64_t used = 0; };
  static std::string Key(const DecodeInput& input);
  void Load(bool force = false);
  bool Save();
  std::filesystem::path directory_, history_, settings_;
  std::filesystem::file_time_type stamp_{};
  std::uint64_t file_id_ = 0, file_size_ = 0;
  std::vector<Entry> entries_;
};
}

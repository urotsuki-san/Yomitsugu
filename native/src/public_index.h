#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ime {
enum DictionaryFlag : unsigned { Present = 1, SymbolOnly = 2, Supplemental = 4, Priority = 8, English = 16 };

// Offsets refer to an immutable view of the original TSV. An atomic file
// replacement keeps the old view alive until the new index has been validated.
class PublicIndex {
 public:
  static std::shared_ptr<PublicIndex> Open(const std::filesystem::path&, std::string* error);
  // Upper bound for embedded dictionary readings of at least six UTF-8 bytes.
  size_t MaxEmbeddedLength(std::string_view suffix) const;
  unsigned Flags(std::string_view key) const;
  std::vector<std::string> Words(std::string_view key, bool supplemental_only) const;
  size_t size() const;
  bool has_supplemental() const { return has_supplemental_; }
 private:
  struct Row {
    std::uint32_t offset;
    std::uint16_t key_length, word_length;
    std::uint8_t kind, flags, count;
  };
  bool Build(std::string_view file, std::string* error);
  const Row* Find(std::string_view key) const;
  std::string_view Key(const Row& r) const { return {view_.get() + r.offset, r.key_length}; }
  std::string_view Word(const Row& r) const { return {view_.get() + r.offset + r.key_length + 1, r.word_length}; }
  std::shared_ptr<const char> view_;
  std::vector<Row> rows_;
  std::vector<std::uint32_t> slots_;
  bool has_supplemental_ = false;
  bool bundled_ = false;
};
}

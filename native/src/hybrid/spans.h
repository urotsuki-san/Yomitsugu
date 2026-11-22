#pragma once
#include "ime_engine.h"
#include <optional>
#include <utility>

using WordPreference = std::pair<std::string,std::string>;

// A single finalized request owns the lexicon paths and their scratch space.
class ReadingPaths {
 public:
  ReadingPaths(std::string reading, const ime::DecodeInput& input);
  bool Supports(const std::string& surface);
  std::u32string FirstSurface() {Load(); return first_;}
  void AddAlternateSyllabicN(std::vector<ime::Candidate>& rows);
  WordPreference ChangedWord(const std::string& previous, const std::string& selected);
  void Protect(const std::vector<WordPreference>& words, std::vector<ime::Candidate>& rows);
 private:
  struct Edge { size_t end; std::u32string text; };
  struct Alignment {
    size_t width = 0;
    std::vector<unsigned char> prefix, suffix;
    std::optional<size_t> Unique(size_t reading) const;
    bool complete() const { return !prefix.empty() && prefix.back(); }
  };
  Alignment Align(const std::u32string& surface, const std::vector<size_t>& boundaries = {});
  void Load();
  std::string reading_;
  std::u32string kana_;
  std::u32string first_;
  const ime::DecodeInput& request_;
  bool loaded_ = false;
  std::vector<std::vector<Edge>> edges_;
};

void ProtectNumericLiterals(const ime::DecodeInput& input, std::vector<ime::Candidate>& rows,bool allow_dictionary);

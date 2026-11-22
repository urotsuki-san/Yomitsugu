#include "public_index.h"
#include <windows.h>
#include <algorithm>
#include <bcrypt.h>
#include <cstring>
#include <iterator>
#include "bundled_public_index.h"

namespace ime {
namespace {
bool Fail(std::string* error, const char* message) {
  if (error) *error = message;
  return false;
}
uint32_t Hash(std::string_view key) {
  uint32_t h = 2166136261u;
  for (unsigned char ch : key) h = (h ^ ch) * 16777619u;
  return h;
}
bool MatchesBundledDictionary(std::string_view file) {
  using namespace bundled_dictionary;
  if (file.size() != file_size) return false;
  // Hash the exact mapping used for lookups so replacing the path cannot race
  // validation. Failure simply selects the general dictionary reader.
  unsigned char digest[sizeof(sha256)]{};
  auto status = BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
      reinterpret_cast<unsigned char*>(const_cast<char*>(file.data())),
      static_cast<ULONG>(file.size()), digest, sizeof(digest));
  return BCRYPT_SUCCESS(status) && std::memcmp(digest, sha256, sizeof(digest)) == 0;
}
constexpr uint32_t kOffsetMask = (1u << bundled_dictionary::offset_bits) - 1;
bool MatchesReading(std::string_view file, size_t offset, std::string_view key) {
  return key.size() < file.size() - offset && file[offset + key.size()] == '\t' &&
         std::memcmp(file.data() + offset, key.data(), key.size()) == 0;
}
const uint32_t* BundledGroup(std::string_view file, std::string_view key) {
  using namespace bundled_dictionary;
  if (key.empty() || key.size() > 512) return nullptr;
  const auto hash = Hash(key);
  constexpr size_t bucket_count = std::size(bucket_starts) - 1;
  static_assert((bucket_count & (bucket_count - 1)) == 0);
  const size_t bucket = hash & (bucket_count - 1);
  const auto descriptor = bucket_starts[bucket];
  const auto filter_bit = 1u << (20 + ((hash >> 15) % 12));
  if (!(descriptor & filter_bit)) return nullptr;
  constexpr uint32_t bucket_mask = (1u << 20) - 1;
  const auto end = bucket_starts[bucket + 1] & bucket_mask;
  for (auto i = descriptor & bucket_mask; i < end; ++i) {
    const uint32_t entry = groups[i];
    if (((entry ^ hash) & fingerprint_mask) == 0 && MatchesReading(file, entry & kOffsetMask, key))
      return &groups[i];
  }
  return nullptr;
}
unsigned BundledFlags(uint32_t group) {
  using namespace bundled_dictionary;
  return flag_values[(group >> offset_bits) & flag_mask];
}
std::vector<std::string> BundledWords(std::string_view file, std::string_view key, bool supplemental_only) {
  std::vector<std::string> words;
  const auto* group = BundledGroup(file, key);
  if (!group || (supplemental_only && !(BundledFlags(*group) & Supplemental))) return words;
  size_t offset = *group & kOffsetMask;
  do {
    const size_t start = offset + key.size() + 1;
    const size_t tab = file.find('\t', start);
    const size_t newline = file.find('\n', tab + 1);
    const size_t end = newline == file.npos ? file.size() : newline;
    auto pos = file.substr(tab + 1, end - tab - 1);
    if (!pos.empty() && pos.back() == '\r') pos.remove_suffix(1);
    if (!supplemental_only || pos == u8"補助語") words.emplace_back(file.substr(start, tab - start));
    if (newline == file.npos) break;
    offset = newline + 1;
  } while (offset < file.size() && MatchesReading(file, offset, key));
  return words;
}
}
std::shared_ptr<PublicIndex> PublicIndex::Open(const std::filesystem::path& path, std::string* error) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) { Fail(error, "Cannot open public dictionary"); return {}; }
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(file, &size) || size.QuadPart > 32 * 1024 * 1024) {
    CloseHandle(file); Fail(error, "Oversized public dictionary"); return {};
  }
  auto result = std::make_shared<PublicIndex>();
  if (size.QuadPart) {
    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    const char* data = mapping ? static_cast<const char*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0)) : nullptr;
    if (mapping) CloseHandle(mapping);
    if (!data) { CloseHandle(file); Fail(error, "Cannot map public dictionary"); return {}; }
    const std::string_view contents(data, static_cast<size_t>(size.QuadPart));
    if (MatchesBundledDictionary(contents)) {
      // Prevent in-place writes from invalidating the verified offsets. The
      // delete share still allows a new file to replace this path.
      result->view_.reset(data, [file](const char* p) { UnmapViewOfFile(p); CloseHandle(file); });
      result->bundled_ = true;
      result->has_supplemental_ = bundled_dictionary::has_supplemental;
    } else {
      CloseHandle(file);
      result->view_.reset(data, [](const char* p) { UnmapViewOfFile(p); });
      if (!result->Build(contents, error)) return {};
    }
  } else CloseHandle(file);
  if (error) error->clear();
  return result;
}
bool PublicIndex::Build(std::string_view file, std::string* error) {
  size_t cursor = file.substr(0, 3) == "\xEF\xBB\xBF" ? 3 : 0;
  size_t group_count = 0;
  bool sorted = true;
  while (cursor < file.size()) {
    size_t end = file.find('\n', cursor);
    if (end == file.npos) end = file.size();
    size_t start = cursor;
    auto line = file.substr(start, end - start);
    cursor = end + 1;
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line.empty() || line.front() == '#' || line.front() == '!') continue;
    if (line.size() > 4096 || line.find('\0') != line.npos ||
        !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, line.data(), static_cast<int>(line.size()), nullptr, 0))
      return Fail(error, "Invalid public dictionary row");
    auto tab = line.find('\t');
    if (tab == line.npos || tab == 0 || tab > 512) return Fail(error, "Invalid reading");
    auto rest = line.substr(tab + 1);
    auto next = rest.find('\t');
    auto word = rest.substr(0, next);
    if (word.empty() || word.size() > 1024) return Fail(error, "Invalid surface");
    auto pos = next == rest.npos ? std::string_view("noun") : rest.substr(next + 1);
    pos = pos.substr(0, pos.find('\t'));
    auto key = line.substr(0, tab);
    unsigned kind = 0;
    if (pos == u8"補助語") kind = Supplemental;
    else if (pos == u8"名詞" || pos == "noun") kind = Priority;
    else if (pos == u8"記号" || pos == "symbol") kind = SymbolOnly;
    else if (pos == u8"英単語" && key == word) kind = English;
    if (!rows_.empty() && sorted && Key(rows_.back()) > key) sorted = false;
    rows_.push_back({static_cast<uint32_t>(start), static_cast<uint16_t>(tab),
                     static_cast<uint16_t>(word.size()), static_cast<uint8_t>(kind), 0, 0});
  }
  if (!sorted) std::sort(rows_.begin(), rows_.end(), [&](const Row& a, const Row& b) {
    auto order = Key(a).compare(Key(b));
    return order ? order < 0 : a.offset < b.offset;
  });
  size_t out = 0;
  for (size_t i = 0; i < rows_.size();) {
    const size_t begin = out;
    const auto key = Key(rows_[i]);
    unsigned flags = Present | SymbolOnly;
    do {
      const auto row = rows_[i++];
      bool duplicate = false;
      for (size_t j = begin; j < out; ++j)
        if (Word(rows_[j]) == Word(row)) { duplicate = true; break; }
      if (duplicate) continue;
      if (out - begin == 32 || out == 700000) return Fail(error, "Too many dictionary entries");
      if (row.kind != SymbolOnly) flags &= ~SymbolOnly;
      flags |= row.kind & ~SymbolOnly;
      rows_[out++] = row;
    } while (i < rows_.size() && Key(rows_[i]) == key);
    rows_[begin].flags = static_cast<uint8_t>(flags);
    rows_[begin].count = static_cast<uint8_t>(out - begin);
    has_supplemental_ |= (flags & Supplemental) != 0;
    ++group_count;
  }
  rows_.resize(out);
  if (!out) return true;
  rows_.shrink_to_fit();
  size_t capacity = 2;
  while (capacity < group_count + group_count / 2 + 1) capacity *= 2;
  slots_.resize(capacity);
  for (size_t i = 0; i < out; i += rows_[i].count) {
    size_t slot = Hash(Key(rows_[i])) & (capacity - 1);
    while (slots_[slot]) slot = (slot + 1) & (capacity - 1);
    slots_[slot] = static_cast<uint32_t>(i + 1);
  }
  return true;
}
const PublicIndex::Row* PublicIndex::Find(std::string_view key) const {
  if (slots_.empty()) return nullptr;
  const size_t mask = slots_.size() - 1;
  for (size_t slot = Hash(key) & mask; slots_[slot]; slot = (slot + 1) & mask) {
    const auto& row = rows_[slots_[slot] - 1];
    if (Key(row) == key) return &row;
  }
  return nullptr;
}
size_t PublicIndex::size() const {
  return bundled_ ? bundled_dictionary::row_count : rows_.size();
}
size_t PublicIndex::MaxEmbeddedLength(std::string_view suffix) const {
  if (!bundled_ || suffix.size() < 6) return std::min<size_t>(suffix.size(), 512);
  // All keys with the same six-byte prefix land in this bucket. Collisions can
  // increase its bound, but a longer matching dictionary entry cannot exist.
  const auto bucket = Hash(suffix.substr(0, 6)) % std::size(bundled_dictionary::prefix_max);
  return std::min<size_t>(suffix.size(), bundled_dictionary::prefix_max[bucket]);
}
unsigned PublicIndex::Flags(std::string_view key) const {
  if (bundled_) {
    const auto* group = BundledGroup({view_.get(), bundled_dictionary::file_size}, key);
    return group ? BundledFlags(*group) : 0;
  }
  const auto* row = Find(key);
  return row ? row->flags : 0;
}
std::vector<std::string> PublicIndex::Words(std::string_view key, bool supplemental_only) const {
  if (bundled_) return BundledWords({view_.get(), bundled_dictionary::file_size}, key, supplemental_only);
  std::vector<std::string> result;
  if (const auto* first = Find(key)) {
    result.reserve(first->count);
    for (size_t i = 0; i < first->count; ++i)
      if (!supplemental_only || first[i].kind == Supplemental) result.emplace_back(Word(first[i]));
  }
  return result;
}
}

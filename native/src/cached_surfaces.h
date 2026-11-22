#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
namespace ime {
// Store candidate bytes together. Each end offset indexes a frame bounded by
// the bridge's 1 MiB JSON limit plus at most 32 unconverted reading suffixes.
class CachedSurfaces {
  std::string text_;
  std::vector<std::uint32_t> ends_;
public:
  explicit CachedSurfaces(const std::vector<std::string>& rows) {
    std::size_t bytes = 0;
    for (const auto& row : rows) bytes += row.size();
    text_.reserve(bytes);
    ends_.reserve(rows.size());
    for (const auto& row : rows) {
      text_.append(row);
      ends_.push_back(static_cast<std::uint32_t>(text_.size()));
    }
  }
  std::size_t bytes() const { return text_.capacity() + ends_.capacity() * sizeof(ends_[0]); }
  std::vector<std::string> read(std::size_t limit) const {
    std::vector<std::string> rows;
    const auto count = std::min(limit, ends_.size());
    rows.reserve(count);
    std::size_t begin = 0;
    for (std::size_t i = 0; i < count; ++i) {
      rows.emplace_back(text_, begin, ends_[i] - begin);
      begin = ends_[i];
    }
    return rows;
  }
};
}

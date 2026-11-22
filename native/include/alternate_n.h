#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace ime {
// The stored byte points just after ん in the normalized reading. Its vowel
// can be reconstructed without retaining another string for every history row.
inline bool RestoreSyllabicN(std::string& kana, unsigned offset) {
  if (offset < 3 || offset > kana.size() || kana.size() - offset < 3 || kana.compare(offset - 3, 3, u8"ん") != 0) return false;
  constexpr std::string_view syllables[] = {u8"な", u8"に", u8"ぬ", u8"ね", u8"の"};
  constexpr std::string_view vowels[] = {u8"あ", u8"い", u8"う", u8"え", u8"お"};
  for (unsigned i = 0; i < 5; ++i) {
    if (kana.compare(offset, 3, syllables[i]) == 0) {
      kana.replace(offset, 3, vowels[i]);
      return true;
    }
  }
  return false;
}

inline std::uint8_t SyllabicNPosition(const std::string& standard, const std::string& selected) {
  if (standard == selected || standard.size() != selected.size() || standard.size() > 96) return 0;
  for (size_t pos = standard.find(u8"ん"); pos != std::string::npos; pos = standard.find(u8"ん", pos + 3)) {
    const auto offset = static_cast<unsigned>(pos + 3);
    auto alternate = standard;
    if (RestoreSyllabicN(alternate, offset) && alternate == selected) return static_cast<std::uint8_t>(offset);
  }
  return 0;
}
}

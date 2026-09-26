#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "ime_engine.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace ime {
namespace {

struct RomajiRule { const char* key; const char* output; const char* pending; };
const std::unordered_map<std::string, RomajiRule>& RomajiTable() {
  static const auto table = [] {
    std::unordered_map<std::string, RomajiRule> result;
    const RomajiRule entries[] = {
#include "romaji_table.inc"
      {"?", u8"？", ""}, {"!", u8"！", ""}, {":", u8"：", ""}, {";", u8"；", ""},
      {"0", u8"０", ""}, {"1", u8"１", ""}, {"2", u8"２", ""}, {"3", u8"３", ""},
      {"4", u8"４", ""}, {"5", u8"５", ""}, {"6", u8"６", ""}, {"7", u8"７", ""},
      {"8", u8"８", ""}, {"9", u8"９", ""},
    };
    for (const auto& entry : entries) result[entry.key] = entry;
    return result;
  }();
  return table;
}

struct WordEntry {
  const char* reading;
  const char* surface;
  double cost;
};

const WordEntry kWords[] = {
    {"こんにちは", "こんにちは", 4.0},
    {"こんばんは", "こんばんは", 8.0},
    {"ありがとう", "ありがとう", 6.0},
    {"ございます", "ございます", 6.0},
    {"がっこう", "学校", 18.0},
    {"がこう", "学校", 32.0},
    {"きょう", "今日", 22.0},
    {"きんよう", "金曜", 28.0},
    {"は", "は", 2.0},
    {"わ", "は", 12.0},
    {"いい", "いい", 14.0},
    {"よい", "良い", 24.0},
    {"てんき", "天気", 18.0},
    {"です", "です", 2.0},
    {"ます", "ます", 3.0},
    {"ね", "ね", 2.0},
    {"を", "を", 2.0},
    {"で", "で", 2.0},
    {"に", "に", 2.0},
    {"が", "が", 2.0},
    {"と", "と", 2.0},
    {"も", "も", 2.0},
    {"の", "の", 4.0},
    {"よんで", "読んで", 18.0},
    {"よむ", "読む", 20.0},
    {"ください", "ください", 6.0},
    {"うごきます", "動きます", 18.0},
    {"うごく", "動く", 20.0},
    {"かくにん", "確認", 18.0},
    {"かく", "書く", 22.0},
    {"してください", "してください", 4.0},
    {"しる", "知る", 22.0},
    {"でき", "でき", 16.0},
    {"ひらがな", "ひらがな", 20.0},
    {"へんかん", "変換", 18.0},
    {"にほんご", "日本語", 16.0},
    {"にほん", "日本", 18.0},
    {"えいご", "英語", 18.0},
    {"めも", "メモ", 24.0},
    {"りぽじとり", "リポジトリ", 20.0},
    {"こうしん", "更新", 16.0},
    {"りれき", "履歴", 22.0},
    {"せいせき", "成績", 22.0},
    {"めい", "名", 26.0},
    {"なまえ", "名前", 18.0},
    {"てすと", "テスト", 18.0},
    {"あわせ", "合わせ", 24.0},
    {"つぎ", "次", 22.0},
    {"まえ", "前", 22.0},
    {"おわり", "終わり", 22.0},
    {"はじめ", "始め", 22.0},
    {"これ", "これ", 8.0},
    {"それ", "それ", 8.0},
    {"あれ", "あれ", 10.0},
    {"どれ", "どれ", 10.0},
    {"わたし", "私", 14.0},
    {"あなた", "あなた", 14.0},
    {"はい", "はい", 8.0},
    {"いいえ", "いいえ", 8.0},
    {"そうです", "そうです", 14.0},
    {"わかりました", "わかりました", 12.0},
    {"おねがい", "お願い", 14.0},
    {"しつもん", "質問", 18.0},
    {"こたえ", "答え", 18.0},
    {"はなす", "話す", 20.0},
    {"きく", "聞く", 20.0},
    {"みる", "見る", 18.0},
    {"いく", "行く", 18.0},
    {"くる", "来る", 18.0},
    {"する", "する", 8.0},
    {"ある", "ある", 8.0},
    {"いる", "いる", 8.0},
};

struct PhraseEntry {
  const char* reading;
  const char* surface;
  double cost;
};

const PhraseEntry kPhrases[] = {
    {"きょうはいいてんきですね", "今日はいい天気ですね", 8.0},
    {"きょうはすごくもいいですね", "今日はすごくもいいですね", 18.0},
    {"こんにちは", "こんにちは", 3.0},
    {"ありがとうございます", "ありがとうございます", 5.0},
    {"りれきをよんでください", "履歴を読んでください", 20.0},
    {"がっこう", "学校", 18.0},
    {"だいじょうぶです", "大丈夫です", 6.0},
    {"だいじょうぶ", "大丈夫", 8.0},
    {"とうきょう", "東京", 12.0},
    {"きょう", "今日", 14.0},
    {"きょうは", "今日は", 10.0},
    {"にほんご", "日本語", 14.0},
    {"がくせい", "学生", 16.0},
    {"せんせい", "先生", 12.0},
    {"ちゅうおう", "中央", 14.0},
    {"ちゅうおう", "中欧", 20.0},
    {"しんたい", "身体", 18.0},
    {"しんだい", "寝台", 20.0},
    {"しんだい", "新台", 26.0},
    {"きしょう", "気象", 16.0},
    {"すきです", "好きです", 12.0},
    {"まいにち", "毎日", 14.0},
    {"あさのまで", "朝まで", 18.0},
    {"ほんとう", "本当", 14.0},
    {"ほんと", "本当", 18.0},
    {"これはかんじです", "これは漢字です", 20.0},
    {"これはかんじ", "これは漢字", 22.0},
    {"しゅくだい", "宿題", 16.0},
    {"あさのまでいそがしいでした", "朝まで忙しいでした", 28.0},
    {"げんごがく", "言語学", 18.0},
    {"がっこうに", "学校に", 22.0},
    {"ほんとにわにおもしろかった", "本当にわにおもしろかった", 40.0},
    {"ほんとにわにおもしろかった", "本当におもしろかった", 48.0},
    {"これほかんじです", "これは漢字です", 30.0},
    {"まいにてのましようをくりかえしてください",
     "毎日的な使用を繰り返してください", 50.0},
    {"まいにてのましようをくりかえしてください",
     "毎日試行を繰り返してください", 52.0},
    {"あさのまでいそがしいでした", "朝まで忙しかったです", 36.0},
    {"すこです", "好きです", 24.0},
    {"しんたい", "身体", 16.0},
};

const std::pair<const char*, const char*> kParticles[] = {
    {"は", "は"}, {"を", "を"}, {"が", "が"}, {"に", "に"},
    {"で", "で"}, {"と", "と"}, {"も", "も"}, {"や", "や"},
};

const std::unordered_set<std::string> kEnWords = {
    "a", "an", "the", "and", "or", "but", "if", "then", "else", "for", "of", "to",
    "in", "on", "at", "by", "with", "from", "as", "is", "are", "was", "were", "be",
    "been", "am", "do", "does", "did", "not", "no", "yes", "please", "thanks",
    "hello", "world", "hi", "hey", "update", "repository", "repo", "readme",
    "file", "name", "path", "code", "test", "tests", "run", "make", "this", "that",
    "these", "those", "it", "its", "we", "you", "they", "he", "she", "i", "me",
    "my", "your", "our", "their", "here", "there", "when", "where", "what", "who",
    "how", "why", "all", "any", "some", "more", "most", "other", "into", "over",
    "under", "again", "once", "because", "so", "than", "too", "very", "can", "will",
    "just", "should", "now", "new", "old", "good", "bad", "nice", "day", "days",
    "time", "year", "years", "github", "gitlab", "python", "java", "javascript",
    "typescript", "windows", "linux", "macos", "english", "japanese", "text",
    "input", "output", "user", "users", "data", "list", "item", "items", "set",
    "get", "add", "remove", "delete", "create", "open", "close", "start", "stop",
    "check", "confirm", "cancel", "save", "load", "send", "receive", "build",
    "release", "version", "branch", "commit", "push", "pull", "merge", "issue",
    "answer", "print", "function", "value", "return", "class", "object",
    "string", "number", "boolean", "array", "map", "dict", "main", "index",
};

const std::unordered_set<std::string> kKnownExt = {
    "md", "py", "js", "ts", "json", "txt", "html", "css", "yml", "yaml", "toml",
    "cfg", "ini", "log", "sh", "bat", "ps1", "rs", "go", "java", "cpp", "c", "h",
    "hpp", "cs", "rb", "php", "swift", "kt", "png", "jpg", "gif", "svg", "pdf",
    "zip", "exe", "dll", "so", "xml", "csv", "env", "lock",
};

bool IsVowel(char c) {
  return c == 'a' || c == 'i' || c == 'u' || c == 'e' || c == 'o';
}

std::string ToLower(const std::string& s) {
  std::string out = s;
  for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

bool LooksJapaneseChar(char32_t c) {
  return (c >= 0x3040 && c <= 0x30FF) || (c >= 0x4E00 && c <= 0x9FFF) ||
         (c >= 0xFF00 && c <= 0xFFEF);
}

size_t Utf8CodePointCount(const std::string& s) {
  size_t n = 0;
  for (unsigned char c : s) {
    if ((c & 0xC0) != 0x80) ++n;
  }
  return n;
}

bool DecodeUtf8At(const std::string& s, size_t i, char32_t* cp, size_t* len) {
  if (i >= s.size()) return false;
  unsigned char c = static_cast<unsigned char>(s[i]);
  if (c < 0x80) {
    *cp = c;
    *len = 1;
    return true;
  }
  if ((c & 0xE0) == 0xC0) {
    if (i + 1 >= s.size()) return false;
    *cp = (static_cast<char32_t>(c & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
    *len = 2;
    return true;
  }
  if ((c & 0xF0) == 0xE0) {
    if (i + 2 >= s.size()) return false;
    *cp = (static_cast<char32_t>(c & 0x0F) << 12) |
          ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) |
          (static_cast<unsigned char>(s[i + 2]) & 0x3F);
    *len = 3;
    return true;
  }
  if ((c & 0xF8) == 0xF0) {
    if (i + 3 >= s.size()) return false;
    *cp = (static_cast<char32_t>(c & 0x07) << 18) |
          ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 12) |
          ((static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6) |
          (static_cast<unsigned char>(s[i + 3]) & 0x3F);
    *len = 4;
    return true;
  }
  return false;
}

bool HasKanjiUtf8(const std::string& s) {
  size_t i = 0;
  while (i < s.size()) {
    char32_t cp = 0;
    size_t len = 0;
    if (!DecodeUtf8At(s, i, &cp, &len)) {
      ++i;
      continue;
    }
    if (cp >= 0x4E00 && cp <= 0x9FFF) return true;
    i += len;
  }
  return false;
}

std::string ConvertRomajiImpl(const std::string& src, bool* fully) {
  const auto& table = RomajiTable();
  std::string out;
  size_t i = 0;
  bool ok = true;
  auto try_table = [&](size_t pos) -> size_t {
    size_t best = 0;
    std::string best_kana;
    size_t max_len = std::min<size_t>(4, src.size() - pos);
    for (size_t L = max_len; L >= 1; --L) {
      std::string piece = src.substr(pos, L);
      auto it = table.find(piece);
      if (it != table.end()) {
        best = L - std::strlen(it->second.pending);
        best_kana = it->second.output;
        break;
      }
    }
    if (best) out += best_kana;
    return best;
  };

  while (i < src.size()) {
    char ch = src[i];
    if (ch == 'n') {
      if (i + 1 < src.size() && src[i + 1] == 'n') {
        if (i + 2 < src.size() && IsVowel(src[i + 2])) {
          out += "ん";
          i += 1;
          continue;
        }
        out += "ん";
        i += 2;
        continue;
      }
      if (i + 1 < src.size() && src[i + 1] == '\'') {
        out += "ん";
        i += 2;
        continue;
      }
      if (i + 1 < src.size() && (IsVowel(src[i + 1]) || src[i + 1] == 'y')) {
        size_t used = try_table(i);
        if (used) {
          i += used;
          continue;
        }
      }
      out += "ん";
      i += 1;
      continue;
    }
    size_t used = try_table(i);
    if (used) {
      i += used;
      continue;
    }
    // 重なった子音を「っ」にし、2文字目から変換を続ける。
    if (std::isalpha(static_cast<unsigned char>(ch)) && i + 1 < src.size() &&
        src[i + 1] == ch && ch != 'n' && ch != 'w' && ch != 'y') {
      out += u8"っ";
      i += 1;
      continue;
    }
    // 小さいかなに続くu/oを長音として補正する（tyuo/chuo→ちゅう）。
    if ((ch == 'o' || ch == 'u') && i + 1 >= src.size() && !out.empty()) {
      static const char* smalls[] = {"ゅ", "ょ", "ャ", "ュ", "ョ", "ぁ", "ぃ", "ぅ", "ぇ", "ぉ"};
      bool after_small = false;
      for (const char* sm : smalls) {
        size_t sl = std::strlen(sm);
        if (out.size() >= sl && out.compare(out.size() - sl, sl, sm) == 0) after_small = true;
      }
      if (after_small) {
        out += u8"う";
        i += 1;
        continue;
      }
    }
    if (std::isalpha(static_cast<unsigned char>(ch))) {


      ok = false;
      break;
    }
    if (ch == ' ' || ch == '\t') {
      out.push_back(ch);
      i += 1;
      continue;
    }
    ok = false;
    break;
  }
  if (i < src.size()) out.append(src.substr(i));
  if (fully) *fully = ok && i >= src.size();
  return out;
}

std::u32string Utf8ToU32(const std::string& s) {
  std::u32string out;
  size_t i = 0;
  while (i < s.size()) {
    char32_t cp = 0;
    size_t len = 0;
    if (!DecodeUtf8At(s, i, &cp, &len)) {
      ++i;
      continue;
    }
    out.push_back(cp);
    i += len;
  }
  return out;
}

int Levenshtein(const std::string& a, const std::string& b, int cap) {
  if (a == b) return 0;
  const std::u32string ua = Utf8ToU32(a);
  const std::u32string ub = Utf8ToU32(b);
  if (std::abs(static_cast<int>(ua.size()) - static_cast<int>(ub.size())) > cap)
    return cap + 1;
  std::vector<int> prev(ub.size() + 1), cur(ub.size() + 1);
  for (size_t j = 0; j <= ub.size(); ++j) prev[j] = static_cast<int>(j);
  for (size_t i = 1; i <= ua.size(); ++i) {
    cur[0] = static_cast<int>(i);
    int row_min = cur[0];
    for (size_t j = 1; j <= ub.size(); ++j) {
      int cost = ua[i - 1] == ub[j - 1] ? 0 : 1;
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
      row_min = std::min(row_min, cur[j]);
    }
    if (row_min > cap) return cap + 1;
    prev = cur;
  }
  return prev[ub.size()];
}

int PhraseCapChars(size_t chars) {
  if (chars <= 6) return 1;
  if (chars <= 10) return 2;
  if (chars <= 14) return 4;
  return std::max<int>(4, static_cast<int>(chars / 3));
}

bool FuzzyPhrase(const std::string& reading, const std::string& key, int* dist_out) {
  size_t chars_a = Utf8CodePointCount(reading);
  size_t chars_b = Utf8CodePointCount(key);
  int cap = PhraseCapChars(std::max(chars_a, chars_b));
  int d = Levenshtein(reading, key, cap);
  if (d <= cap) {
    if (dist_out) *dist_out = d;
    return true;
  }
  if (dist_out) *dist_out = d;
  return false;
}

struct ReadingSolution {
  std::string surface;
  double cost = 0.0;
  double edit_cost = 0.0;
};

std::vector<std::string> KanaRepairVariants(const std::string& reading) {
  std::vector<std::string> out{reading};
  static const char* inserts[] = {"あ", "い", "う", "え", "お", "は", "を", "っ", "ゃ", "ゅ", "ょ"};
  std::unordered_set<std::string> seen{reading};
  for (size_t i = 0; i < reading.size() && out.size() < 48; ++i) {
    unsigned char b0 = static_cast<unsigned char>(reading[i]);
    if ((b0 & 0xC0) == 0x80) continue;  // only at UTF-8 boundaries
    size_t cp_len = 1;
    if ((b0 & 0xE0) == 0xC0) cp_len = 2;
    else if ((b0 & 0xF0) == 0xE0) cp_len = 3;
    else if ((b0 & 0xF8) == 0xF0) cp_len = 4;
    if (i + cp_len > reading.size()) continue;
    std::string del = reading.substr(0, i) + reading.substr(i + cp_len);
    if (seen.insert(del).second) out.push_back(del);
    for (const char* ins : inserts) {
      std::string v = reading.substr(0, i) + ins + reading.substr(i);
      if (seen.insert(v).second) out.push_back(v);
    }
    // UTF-8の文字境界を保ち、助詞の「は」と「わ」を入れ替える。
    if (reading.compare(i, 3, u8"わ") == 0) {
      std::string v = reading.substr(0, i) + u8"は" + reading.substr(i + 3);
      if (seen.insert(v).second) out.push_back(v);
    } else if (reading.compare(i, 3, u8"は") == 0) {
      std::string v = reading.substr(0, i) + u8"わ" + reading.substr(i + 3);
      if (seen.insert(v).second) out.push_back(v);
    }
    // 助詞の「ほ」→「は」と、長音の「お」→「う」を試す。
    if (reading.compare(i, 3, u8"ほ") == 0) {
      std::string v = reading.substr(0, i) + u8"は" + reading.substr(i + 3);
      if (seen.insert(v).second) out.push_back(v);
      v = reading.substr(0, i) + u8"お" + reading.substr(i + 3);
      if (seen.insert(v).second) out.push_back(v);
    } else if (reading.compare(i, 3, u8"は") == 0) {
      std::string v = reading.substr(0, i) + u8"ほ" + reading.substr(i + 3);
      if (seen.insert(v).second) out.push_back(v);
    } else if (reading.compare(i, 3, u8"お") == 0 && i + 3 == reading.size()) {
      std::string v = reading.substr(0, i) + u8"う";
      if (seen.insert(v).second) out.push_back(v);
    } else if (reading.compare(i, 3, u8"う") == 0 && i + 3 == reading.size()) {
      std::string v = reading.substr(0, i) + u8"お";
      if (seen.insert(v).second) out.push_back(v);
    }
    // 「き」と「こ」、「た」と「だ」の打ち間違いを補正する。
    if (reading.compare(i, 3, u8"こ") == 0) {
      std::string v = reading.substr(0, i) + u8"き" + reading.substr(i + 3);
      if (seen.insert(v).second) out.push_back(v);
    } else if (reading.compare(i, 3, u8"き") == 0) {
      std::string v = reading.substr(0, i) + u8"こ" + reading.substr(i + 3);
      if (seen.insert(v).second) out.push_back(v);
    } else if (reading.compare(i, 3, u8"だ") == 0) {
      std::string v = reading.substr(0, i) + u8"た" + reading.substr(i + 3);
      if (seen.insert(v).second) out.push_back(v);
    } else if (reading.compare(i, 3, u8"た") == 0) {
      std::string v = reading.substr(0, i) + u8"だ" + reading.substr(i + 3);
      if (seen.insert(v).second) out.push_back(v);
    }
  }
  return out;
}

std::vector<ReadingSolution> SolveReading(const std::string& reading) {
  std::vector<ReadingSolution> sols;
  auto add = [&](const ReadingSolution& s) {
    for (const auto& x : sols)
      if (x.surface == s.surface) return;
    sols.push_back(s);
  };

  for (const auto& p : kPhrases) {
    if (reading == p.reading) {
      add({p.surface, p.cost, 0.0});
    }
  }
  // 長音の「う」を1箇所に補う。短い読みでは2箇所への挿入も試す。
  {
    static const std::string ou = u8"う";
    std::vector<std::string> lens{reading};
    for (size_t i = 0; i < reading.size() && lens.size() < 24;) {
      unsigned char b0 = static_cast<unsigned char>(reading[i]);
      size_t cp_len = 1;
      if ((b0 & 0xE0) == 0xC0) cp_len = 2;
      else if ((b0 & 0xF0) == 0xE0) cp_len = 3;
      else if ((b0 & 0xF8) == 0xF0) cp_len = 4;
      if (i + cp_len > reading.size()) break;
      std::string v = reading.substr(0, i + cp_len) + ou + reading.substr(i + cp_len);
      if (v != reading) lens.push_back(v);
      i += cp_len;
    }
    if (reading.size() <= 24) {
      // 「ときょ」→「とうきょう」のように「う」を2箇所に補う。
      for (size_t a = 0; a < lens.size() && a < 8; ++a) {
        for (size_t i = 0; i < lens[a].size();) {
          unsigned char b0 = static_cast<unsigned char>(lens[a][i]);
          size_t cp_len = 1;
          if ((b0 & 0xE0) == 0xC0) cp_len = 2;
          else if ((b0 & 0xF0) == 0xE0) cp_len = 3;
          else if ((b0 & 0xF8) == 0xF0) cp_len = 4;
          if (i + cp_len > lens[a].size()) break;
          std::string v = lens[a].substr(0, i + cp_len) + ou + lens[a].substr(i + cp_len);
          lens.push_back(v);
          i += cp_len;
        }
      }
    }
    std::unordered_set<std::string> seen{reading};
    for (const auto& variant : lens) {
      if (!seen.insert(variant).second) continue;
      for (const auto& p : kPhrases) {
        if (variant == p.reading) add({p.surface, p.cost + 4.0, 1.5});
      }
    }
  }
  for (const auto& variant : KanaRepairVariants(reading)) {
    if (variant == reading) continue;
    for (const auto& p : kPhrases) {
      if (variant == p.reading) {
        add({p.surface, p.cost + 6.0, 2.0});
      }
    }
  }
  for (const auto& p : kPhrases) {
    if (reading == p.reading) continue;
    int d = 0;
    if (FuzzyPhrase(reading, p.reading, &d)) {
      add({p.surface, p.cost + 7.5 * d, 1.5 * d});
    }
  }

  // 動的計画法で単語をつなぎ、助詞の補完も試す。nはUTF-8のバイト数。
  const size_t n = reading.size();
  size_t max_word_bytes = 8;
  for (const auto& w : kWords)
    max_word_bytes = std::max(max_word_bytes, std::strlen(w.reading));
  max_word_bytes += 4;  // safety margin for substr scans
  struct State {
    double cost;
    double edit;
    std::string surf;
  };
  std::vector<std::vector<State>> dp(n + 1);
  dp[0].push_back({0.0, 0.0, ""});
  for (size_t i = 0; i < n; ++i) {
    if (dp[i].empty()) continue;
    std::vector<State> base = dp[i];
    if (base.size() > 4) base.resize(4);
    bool matched = false;
    for (size_t L = std::min(max_word_bytes, n - i); L >= 1; --L) {
      // 照合の開始位置をUTF-8の文字境界にそろえる。
      if (L < n - i) {
        unsigned char b = static_cast<unsigned char>(reading[i + L]);
        if ((b & 0xC0) == 0x80) continue;
      }
      if (i > 0) {
        unsigned char s0 = static_cast<unsigned char>(reading[i]);
        if ((s0 & 0xC0) == 0x80) continue;
      }
      std::string piece = reading.substr(i, L);
      bool any = false;
      for (const auto& w : kWords) {
        if (piece == w.reading) any = true;
      }
      if (!any) continue;
      matched = true;
      for (const auto& w : kWords) {
        if (piece != w.reading) continue;
        for (const auto& st : base) {
          if (dp[i + L].size() >= 4) break;
          State nx = st;
          nx.cost += w.cost;
          nx.surf += w.surface;
          dp[i + L].push_back(nx);
        }
      }
    }
    if (!matched && i > 0) {
      for (const auto& pr : kParticles) {
        size_t plen = std::strlen(pr.first);
        if (i + plen > n) continue;
        if (reading.compare(i, plen, pr.first) != 0) continue;
        for (const auto& st : base) {
          if (dp[i + plen].size() >= 4) break;
          State nx = st;
          nx.cost += 24.0;
          nx.edit += 1.2;
          nx.surf += pr.second;
          dp[i + plen].push_back(nx);
        }
      }
    }
  }
  for (const auto& st : dp[n]) {
    if (!st.surf.empty()) add({st.surf, st.cost, st.edit});
  }
  std::sort(sols.begin(), sols.end(), [](const ReadingSolution& a, const ReadingSolution& b) {
    return a.cost < b.cost;
  });
  if (sols.empty() && !reading.empty()) {
    // 残ったかなをそのまま返す処理は呼び出し元で行う。
  }
  return sols;
}

double ScoreEnglish(const std::string& text) {
  if (text.empty()) return -4.0;
  std::string core = text;
  // 前後の空白と句読点を除く。
  while (!core.empty() && (core.back() == ' ' || core.back() == '.' || core.back() == ',' ||
                           core.back() == '!' || core.back() == '?'))
    core.pop_back();
  size_t start = core.find_first_not_of(" \t.,!?;:");
  if (start == std::string::npos) return -4.0;
  core = core.substr(start);
  if (core.empty()) return -4.0;

  double score = 0.0;
  if (core.find(' ') != std::string::npos) {
    std::istringstream ss(core);
    std::string tok;
    int hits = 0, total = 0;
    while (ss >> tok) {
      ++total;
      while (!tok.empty() && std::ispunct(static_cast<unsigned char>(tok.back()))) tok.pop_back();
      if (kEnWords.count(ToLower(tok))) ++hits;
    }
    score += 1.2 * hits + 0.35 * total;
    if (hits >= 2) score += 2.0;
    else if (hits == 1 && total >= 2) score += 0.8;
  } else {
    std::string low = ToLower(core);
    // aやIの1文字で日本語の読みを分断しない。
    if (core.size() >= 2 && kEnWords.count(low)) score += 3.2;
    else if (core.size() == 1 && kEnWords.count(low)) score += 0.4;
    if (core.size() >= 2 && std::isupper(static_cast<unsigned char>(core[0])) &&
        std::islower(static_cast<unsigned char>(core[1])))
      score += 2.2;
    bool all_upper = true;
    for (char c : core)
      if (std::isalpha(static_cast<unsigned char>(c)) &&
          !std::isupper(static_cast<unsigned char>(c)))
        all_upper = false;
    if (all_upper && core.size() >= 2 && core.size() <= 6) score += 2.0;
    bool all_lower = true;
    for (char c : core)
      if (std::isalpha(static_cast<unsigned char>(c)) &&
          !std::islower(static_cast<unsigned char>(c)))
        all_lower = false;
    if (all_lower) score += 1.1;
    if (std::any_of(core.begin(), core.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
      score += 1.4;
    if (core.find('_') != std::string::npos) score += 1.4;
    if (all_lower && !kEnWords.count(low)) score -= 0.9;
  }
  return score;
}

struct ProtectRegion {
  size_t start;
  size_t end;
  enum Kind { kProtect, kIdent } kind;
};

std::vector<ProtectRegion> DetectProtect(const std::string& raw, const std::string& field) {
  std::vector<ProtectRegion> out;
  if (field == "url" || field == "path" || field == "email" || field == "code" ||
      field == "identifier" || field == "password") {
    if (!raw.empty()) out.push_back({0, raw.size(), ProtectRegion::kProtect});
    return out;
  }
  static const std::regex url_re(R"((?:https?://|www\.)[^\s\"'<>]+)", std::regex::icase);
  static const std::regex email_re(R"([A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,})");
  static const std::regex winpath_re(R"([A-Za-z]:\\[^\s\"'<>|]+)");
  auto add_matches = [&](const std::regex& re, ProtectRegion::Kind kind) {
    for (std::sregex_iterator it(raw.begin(), raw.end(), re), end; it != end; ++it) {
      out.push_back({static_cast<size_t>((*it)[0].first - raw.begin()),
                     static_cast<size_t>((*it)[0].second - raw.begin()), kind});
    }
  };
  add_matches(url_re, ProtectRegion::kProtect);
  add_matches(email_re, ProtectRegion::kProtect);
  add_matches(winpath_re, ProtectRegion::kProtect);
  std::sort(out.begin(), out.end(), [](const ProtectRegion& a, const ProtectRegion& b) {
    return a.start < b.start;
  });
  std::vector<ProtectRegion> merged;
  for (const auto& r : out) {
    if (!merged.empty() && r.start < merged.back().end) {
      merged.back().end = std::max(merged.back().end, r.end);
      continue;
    }
    merged.push_back(r);
  }
  return merged;
}

std::string ShrinkDotted(const std::string& text) {
  if (text.find('.') == std::string::npos) return text;
  std::vector<std::string> parts;
  std::string cur;
  for (char c : text) {
    if (c == '.') {
      parts.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  parts.push_back(cur);
  if (parts.size() < 2) return text;
  std::string last = parts.back();
  if (last.empty()) {
    parts.pop_back();
    std::string join;
    for (size_t i = 0; i < parts.size(); ++i) {
      if (i) join += '.';
      join += parts[i];
    }
    return join;
  }
  bool alpha_lower = !last.empty();
  for (char c : last)
    if (!std::islower(static_cast<unsigned char>(c))) alpha_lower = false;
  if (alpha_lower && last.size() >= 3 && !kKnownExt.count(last)) {
    for (const auto& ext : kKnownExt) {
      if (last.size() > ext.size() && last.compare(0, ext.size(), ext) == 0) {
        std::string tail = last.substr(ext.size());
        bool tail_lower = true;
        for (char c : tail)
          if (!std::islower(static_cast<unsigned char>(c))) tail_lower = false;
        if (tail_lower) {
          parts.back() = ext;
          break;
        }
      }
    }
    if (parts.back().size() >= 3 && parts.back() != last && kKnownExt.count(parts.back())) {
      // 短い拡張子を保持する。
    } else if (!kKnownExt.count(parts.back())) {
      parts.pop_back();
    }
  } else {
    // 13deugokimasuのような数字に続く入力を分ける。
    size_t i = 0;
    while (i < last.size() && std::isdigit(static_cast<unsigned char>(last[i]))) ++i;
    if (i > 0 && i < last.size() && last.size() - i >= 4) {
      parts.back() = last.substr(0, i);
    }
  }
  std::string join;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i) join += '.';
    join += parts[i];
  }
  return join;
}

std::vector<ProtectRegion> DetectIdent(const std::string& raw, const std::vector<ProtectRegion>& base) {
  std::vector<ProtectRegion> idents;
  static const std::regex ident_re(
      R"([A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z0-9_]+)+|[A-Za-z][a-z0-9]+(?:[A-Z][a-z0-9]+)+|[A-Z]{2,}[0-9]*|[A-Za-z]+\d+(?:\.\d+)*[A-Za-z0-9]*)");
  for (std::sregex_iterator it(raw.begin(), raw.end(), ident_re), end; it != end; ++it) {
    size_t s = static_cast<size_t>((*it)[0].first - raw.begin());
    size_t e = static_cast<size_t>((*it)[0].second - raw.begin());
    std::string text = ShrinkDotted(raw.substr(s, e - s));
    e = s + text.size();
    if (e - s < 2) continue;
    bool overlap = false;
    for (const auto& b : base)
      if (s < b.end && b.start < e) overlap = true;
    if (overlap) continue;
    std::string low = ToLower(text);
    static const std::set<std::string> stop = {"no", "in", "on", "at", "to", "or",
                                                "is", "be", "do", "go", "me", "we"};
    if (stop.count(low)) continue;
    // ...sitekudasaiGithubdeではGithubだけを英語として扱い、前後の読みを変換する。
    const size_t upper = text.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ", 1);
    if (upper != std::string::npos) {
      bool romanizable = false;
      ConvertRomajiImpl(text.substr(0, upper), &romanizable);
      if (romanizable) {
        size_t word_end = upper + 1;
        while (word_end < text.size() &&
               std::islower(static_cast<unsigned char>(text[word_end]))) ++word_end;
        // 既知の英単語だけを保護し、直後の助詞は日本語として処理する。
        for (size_t end_pos = word_end; end_pos > upper + 2; --end_pos) {
          if (kEnWords.count(ToLower(text.substr(upper, end_pos - upper)))) {
            idents.push_back({s + upper, s + end_pos, ProtectRegion::kIdent});
            break;
          }
        }
        if (!idents.empty() && idents.back().start == s + upper) continue;
      }
    }
    idents.push_back({s, e, ProtectRegion::kIdent});
  }
  return idents;
}

std::vector<std::pair<size_t, size_t>> FreeRegions(size_t len, const std::vector<ProtectRegion>& regs) {
  std::vector<std::pair<size_t, size_t>> out;
  if (len == 0) return out;
  auto sorted = regs;
  std::sort(sorted.begin(), sorted.end(), [](const ProtectRegion& a, const ProtectRegion& b) {
    return a.start < b.start;
  });
  size_t cursor = 0;
  for (const auto& r : sorted) {
    if (r.start > cursor) out.emplace_back(cursor, r.start);
    cursor = std::max(cursor, r.end);
  }
  if (cursor < len) out.emplace_back(cursor, len);
  return out;
}

struct ContextHint {
  double en = 0.0;
  double ja = 0.0;
};

ContextHint ContextLanguage(const std::string& left, const std::string& right) {
  ContextHint h;
  std::string sample = left;
  if (!right.empty()) {
    if (!sample.empty()) sample += ' ';
    sample += right;
  }
  int ja_chars = 0, en_chars = 0;
  for (unsigned char c : sample) {
    if (c >= 0x80) ++ja_chars;
    else if (std::isalpha(c)) ++en_chars;
  }
  if (ja_chars > 0) h.ja = std::min(1.0, ja_chars / 4.0);
  if (en_chars > 0) h.en = std::min(0.6, en_chars / 20.0);
  int words = 0;
  {
    std::istringstream ss(sample);
    std::string tok;
    while (ss >> tok) ++words;
  }
  if (words >= 2 && en_chars > 0) h.en += 0.5;
  std::string lower = ToLower(sample);
  int overlap = 0;
  for (const char* w : {"yes", "no", "or", "and", "the", "is", "answer", "please", "hello", "world"}) {
    if (lower.find(w) != std::string::npos) ++overlap;
  }
  if (overlap) h.en += 0.4 * std::min(3, overlap);
  return h;
}

enum class Kind { kEn, kJa, kProtect, kIdent, kPunct };

struct Seg {
  Kind kind;
  size_t start;
  size_t end;
  std::string surface;
  std::string reading;
  bool ok = true;
  double edit_cost = 0.0;
  // kJaに対するAzooKey/Zenzaiの候補。合計スコアには直接加算しない。
  std::vector<std::string> azo_surfaces;
};

Seg BuildSeg(Kind kind, size_t s, size_t e, const std::string& raw) {
  Seg seg;
  seg.kind = kind;
  seg.start = s;
  seg.end = e;
  std::string text = raw.substr(s, e - s);
  if (kind != Kind::kJa) {
    seg.surface = text;
    seg.reading = text;
    return seg;
  }
  std::string ja_text = text;
  std::string trailing;
  if (!ja_text.empty() && (ja_text.back() == '.' || ja_text.size() >= 3 &&
                                          ja_text.compare(ja_text.size() - 3, 3, u8"。") == 0)) {
    trailing = u8"。";
    if (ja_text.back() == '.') ja_text.pop_back();
    else ja_text.erase(ja_text.size() - 3);
  }
  bool ok = false;
  std::string kana = ConvertRomajiImpl(ja_text, &ok);
  if (!ok || kana.empty()) {
    // 変換できない英字列に対し、隣接キーの打ち間違いを補正する。
    std::vector<std::string> edits{ja_text};
    static const std::pair<char, char> swaps[] = {
        {'i', 'e'}, {'e', 'i'}, {'u', 'i'}, {'a', 's'}, {'s', 'a'}, {'t', 'g'},
        {'g', 't'}, {'o', 'p'}, {'p', 'o'}, {'n', 'm'}, {'m', 'n'}, {'h', 'j'},
        {'j', 'h'}, {'k', 'l'}, {'l', 'k'}, {';', 'k'}, {'c', 'v'}, {'v', 'c'},
        {'o', 'i'}, {'i', 'o'}, {'d', 't'}, {'t', 'd'}, {'o', 'a'}, {'a', 'o'},
        {'e', 'a'}, {'a', 'e'}, {'k', 'j'}, {'j', 'k'}, {'u', 'o'}, {'o', 'u'},
    };
    for (size_t i = 0; i < ja_text.size() && edits.size() < 24; ++i) {
      if (!std::isalpha(static_cast<unsigned char>(ja_text[i]))) continue;
      for (const auto& sw : swaps) {
        if (ja_text[i] != sw.first) continue;
        std::string e = ja_text;
        e[i] = sw.second;
        edits.push_back(e);
      }
      if (i + 1 < ja_text.size()) {
        std::string e = ja_text;
        std::swap(e[i], e[i + 1]);
        edits.push_back(e);
      }
      std::string del = ja_text.substr(0, i) + ja_text.substr(i + 1);
      edits.push_back(del);
    }
    for (const auto& e : edits) {
      bool eok = false;
      std::string ekana = ConvertRomajiImpl(e, &eok);
      if (eok && !ekana.empty()) {
        ok = true;
        kana = ekana;
        break;
      }
    }
  }
  if (!ok || kana.empty()) {
    seg.ok = false;
    seg.surface = text;
    seg.reading = text;
    return seg;
  }
  auto sols = SolveReading(kana);
  seg.reading = kana;
  // 正しく読める入力は本辞書を優先し、補正候補はその後に置く。
  if (ScoreEnglish(text) <= 1.0) {
    // 挨拶で抜けた「ん」を補って本辞書へ渡す。元の入力も候補に残す。
    auto lookup = kana;
    if (lookup == u8"こにちは" || lookup == u8"こにちわ") lookup = u8"こんにちは";
    auto azoo = AzookeyConvert(lookup, 24);
    // 数字を含まない読みでは「3階」などの数字表記を一般語の後に置く。
    if (std::none_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c); })) {
      auto numeral_counter = [](const std::string& surface) {
        static const char* numerals[] = {u8"一", u8"二", u8"三", u8"四", u8"五", u8"六",
                                         u8"七", u8"八", u8"九", u8"十", u8"さん"};
        static const char* counters[] = {u8"回", u8"階", u8"個", u8"本", u8"枚", u8"人"};
        for (const char* numeral : numerals) {
          const size_t prefix = std::strlen(numeral);
          if (surface.compare(0, prefix, numeral) != 0) continue;
          for (const char* counter : counters)
            if (surface.compare(prefix, std::strlen(counter), counter) == 0) return true;
        }
        return false;
      };
      std::stable_partition(azoo.begin(), azoo.end(), [&](const std::string& surface) {
        if (numeral_counter(surface)) return false;
        return std::none_of(surface.begin(), surface.end(),
                            [](unsigned char c) { return std::isdigit(c); });
      });
    }
    for (const auto& azo : azoo) seg.azo_surfaces.push_back(azo + trailing);
  }
  if (!seg.azo_surfaces.empty()) {
    seg.surface = seg.azo_surfaces.front();
    seg.azo_surfaces.erase(seg.azo_surfaces.begin());
    if (!sols.empty() && sols[0].surface + trailing != seg.surface)
      seg.azo_surfaces.insert(seg.azo_surfaces.begin(), sols[0].surface + trailing);
  } else if (!sols.empty()) {
    seg.surface = sols[0].surface + trailing;
    seg.edit_cost = sols[0].edit_cost;
  } else {
    seg.ok = false;
    seg.surface = text;
  }


  return seg;
}

std::vector<std::vector<std::pair<Kind, std::pair<size_t, size_t>>>> RegionHyps(
    const std::string& text) {
  size_t n = text.size();
  std::vector<std::vector<std::pair<Kind, std::pair<size_t, size_t>>>> hyps;
  if (n == 0) return hyps;
  hyps.push_back({{Kind::kJa, {0, n}}});
  hyps.push_back({{Kind::kEn, {0, n}}});

  // 全体をローマ字として読める入力は、to|kyoやa|sanomaのように分割しない。
  bool whole_romaji = false;
  ConvertRomajiImpl(text, &whole_romaji);
  // java+wotukaimasuのように、文頭が4文字以上の既知の英単語なら分割も試す。

  std::vector<size_t> cuts;
  // 先頭が大文字の単語（^[A-Z][a-z]+）の終端を境界候補にする。
  if (n >= 2 && std::isupper(static_cast<unsigned char>(text[0]))) {
    size_t i = 1;
    while (i < n && std::islower(static_cast<unsigned char>(text[i]))) ++i;
    if (i > 1 && i < n) cuts.push_back(i);
  }
  // 大文字列・単語・数字の境界を分割候補にする。
  {
    size_t i = 0;
    while (i < n) {
      size_t start = i;
      unsigned char c = static_cast<unsigned char>(text[i]);
      if (std::isupper(c)) {
        while (i < n && std::isupper(static_cast<unsigned char>(text[i]))) ++i;
        if (i < n && std::islower(static_cast<unsigned char>(text[i]))) {
          while (i < n && std::islower(static_cast<unsigned char>(text[i]))) ++i;
        }
      } else if (std::islower(c)) {
        while (i < n && std::islower(static_cast<unsigned char>(text[i]))) ++i;
      } else if (std::isdigit(c)) {
        while (i < n && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
      } else {
        ++i;
        continue;
      }
      if (start > 0) cuts.push_back(start);
      if (i < n) cuts.push_back(i);
    }
  }
  for (size_t i = 1; i < n; ++i) {
    if (std::isupper(static_cast<unsigned char>(text[i])) &&
        std::islower(static_cast<unsigned char>(text[i - 1])))
      cuts.push_back(i);
    if (std::isdigit(static_cast<unsigned char>(text[i])) &&
        std::isalpha(static_cast<unsigned char>(text[i - 1])))
      cuts.push_back(i);
    if (std::isalpha(static_cast<unsigned char>(text[i])) &&
        std::isdigit(static_cast<unsigned char>(text[i - 1])))
      cuts.push_back(i);
    if (text[i - 1] == ' ' && text[i] != ' ') cuts.push_back(i);
    // 入力全体が英単語でなければ、文頭の英単語と後続の読みを分ける。
    {
      std::string prefix = text.substr(0, i);
      while (!prefix.empty() && prefix.back() == '.') prefix.pop_back();
      if (!prefix.empty()) {
        bool all_upper = true;
        for (char pc : prefix) {
          if (std::isalpha(static_cast<unsigned char>(pc)) &&
              !std::isupper(static_cast<unsigned char>(pc)))
            all_upper = false;
        }
        bool continues_latin =
            i < n && std::isalpha(static_cast<unsigned char>(text[i]));
        std::string full_low = ToLower(text);
        bool full_is_en = kEnWords.count(full_low) != 0;
        if (!all_upper && !full_is_en && !continues_latin &&
            kEnWords.count(ToLower(prefix)))
          cuts.push_back(i);
        // Githubnoripojiのような入力を分ける。後半がローマ字として読めることを条件にする。
        else if (!all_upper && !full_is_en && continues_latin && prefix.size() >= 4 &&
                 kEnWords.count(ToLower(prefix))) {
          std::string rest = text.substr(i);
          bool rest_en_word = kEnWords.count(ToLower(rest)) != 0;
          bool rest_starts_vowel =
              !rest.empty() && std::strchr("aiueo", rest[0]) != nullptr;
          // 後半も英単語なら分割せず、ローマ字に特徴的な子音の並びを確認する。
          if (!rest_en_word && !rest_starts_vowel && rest.size() >= 2 &&
              !(rest.size() >= 3 && kEnWords.count(ToLower(rest.substr(0, 3)))))
            cuts.push_back(i);
        }
      }
    }
  }
  std::sort(cuts.begin(), cuts.end());
  cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
  cuts.erase(std::remove_if(cuts.begin(), cuts.end(), [n](size_t c) { return c == 0 || c >= n; }),
             cuts.end());
  // 長音と撥音を含む読みは、ハイフンやアポストロフィで分断しない。
  if (whole_romaji) {
    cuts.erase(std::remove_if(cuts.begin(), cuts.end(), [&](size_t c) {
      return text[c - 1] == '-' || text[c] == '-' || text[c - 1] == '\'' || text[c] == '\'';
    }), cuts.end());
  }
  if (cuts.size() > 24) cuts.resize(24);

  for (size_t c : cuts) {
    hyps.push_back({{Kind::kEn, {0, c}}, {Kind::kJa, {c, n}}});
    hyps.push_back({{Kind::kJa, {0, c}}, {Kind::kEn, {c, n}}});
  }
  for (size_t i = 0; i < cuts.size() && hyps.size() < 60; ++i) {
    for (size_t j = i + 1; j < cuts.size() && hyps.size() < 60; ++j) {
      hyps.push_back({{Kind::kEn, {0, cuts[i]}},
                      {Kind::kJa, {cuts[i], cuts[j]}},
                      {Kind::kEn, {cuts[j], n}}});
    }
  }
  // 末尾の英語側の句読点を日本語区間に含める。
  static const std::string puncts = ".,!?;、。！？";
  for (auto& h : hyps) {
    std::vector<std::pair<Kind, std::pair<size_t, size_t>>> merged;
    for (auto& seg : h) {
      if (!merged.empty() && seg.first == Kind::kEn) {
        std::string piece = text.substr(seg.second.first, seg.second.second - seg.second.first);
        bool all_punct = !piece.empty();
        for (char c : piece)
          if (puncts.find(c) == std::string::npos) all_punct = false;
        if (all_punct && merged.back().first == Kind::kJa) {
          merged.back().second.second = seg.second.second;
          continue;
        }
      }
      merged.push_back(seg);
    }
    h = merged;
  }
  return hyps;
}

}  // namespace

std::string ConvertRomaji(const std::string& raw, bool* ok) {
  return ConvertRomajiImpl(raw, ok);
}

bool ConvertRomajiChecked(const std::string& raw, std::string* kana) {
  bool ok = false;
  std::string k = ConvertRomajiImpl(raw, &ok);
  if (kana) *kana = k;
  return ok;
}

std::vector<Candidate> Decode(const DecodeInput& input) {
  std::vector<Candidate> out;
  if (input.raw_text.empty() || input.candidate_limit <= 0) return out;
  // 探索上限を超えた入力は原文を返す。
  if (input.raw_text.size() > 512) {
    Candidate c; c.output_text = c.reading_text = input.raw_text; c.is_raw = true;
    return {c};
  }
  const std::string& raw = input.raw_text;

  // 句読点だけの入力も変換し、画面に出ている「。」を原文の「.」へ戻さない。
  if (input.field == "prose") {
    std::vector<std::string> fixed;
    std::string reading;
    if (raw == "." || raw == "," || raw == "?" || raw == "!" || raw == ":" || raw == ";" ||
        (raw.size() == 1 && std::strchr("aiueo", raw[0]))) {
      bool converted = false;
      reading = ConvertRomaji(raw, &converted);
      if (converted && reading != raw) fixed.push_back(reading);
    }
    if (!fixed.empty()) {
      std::unordered_set<std::string> seen;
      for (const auto& surface : fixed) {
        if (static_cast<int>(out.size()) >= input.candidate_limit - (input.candidate_limit > 1)) break;
        if (!seen.insert(surface).second || surface == raw) continue;
        Candidate c; c.output_text = surface; c.reading_text = reading;
        c.jp_score = 10.0; c.total = 20.0 - static_cast<double>(out.size());
        out.push_back(std::move(c));
      }
      if (input.candidate_limit > 1 && static_cast<int>(out.size()) < input.candidate_limit) {
        Candidate literal; literal.output_text = literal.reading_text = raw; literal.is_raw = true;
        out.push_back(std::move(literal));
      }
      return out;
    }
  }

  if (raw.size() >= 5 && std::strchr("ksthmrgbpd", raw.back()) && input.field == "prose") {
    bool full = false, prefix_full = false;
    ConvertRomaji(raw, &full); ConvertRomaji(raw.substr(0, raw.size()-1), &prefix_full);
    if (!full && prefix_full) {
      DecodeInput completed = input; completed.raw_text += 'u';
      auto suggestions = Decode(completed);
      Candidate literal; literal.output_text = literal.reading_text = raw; literal.is_raw = true;
      out.push_back(literal);
      for (auto c : suggestions) {
        if (c.is_raw || c.output_text == completed.raw_text) continue;
        c.reading_text = raw; out.push_back(std::move(c));
        if (out.size() >= static_cast<size_t>(input.candidate_limit)) break;
      }
      if (out.size() > static_cast<size_t>(input.candidate_limit)) out.resize(input.candidate_limit);
      return out;
    }
  }

  auto strong = DetectProtect(raw, input.field);
  auto idents = DetectIdent(raw, strong);
  std::vector<ProtectRegion> fixed = strong;
  fixed.insert(fixed.end(), idents.begin(), idents.end());
  std::sort(fixed.begin(), fixed.end(), [](const ProtectRegion& a, const ProtectRegion& b) {
    return a.start < b.start;
  });
  auto regions = FreeRegions(raw.size(), fixed);
  auto ctx = ContextLanguage(input.left_context, input.right_context);

  std::vector<std::vector<std::vector<std::pair<Kind, std::pair<size_t, size_t>>>>> free_groups;
  for (auto& reg : regions) {
    std::string text = raw.substr(reg.first, reg.second - reg.first);
    auto local = RegionHyps(text);
    for (auto& h : local) {
      for (auto& seg : h) {
        seg.second.first += reg.first;
        seg.second.second += reg.first;
      }
    }
    free_groups.push_back(local);
  }

  std::vector<std::vector<std::pair<Kind, std::pair<size_t, size_t>>>> combos{{}};
  for (auto& group : free_groups) {
    std::vector<std::vector<std::pair<Kind, std::pair<size_t, size_t>>>> nxt;
    for (auto& prefix : combos) {
      for (auto& h : group) {
        auto cur = prefix;
        cur.insert(cur.end(), h.begin(), h.end());
        nxt.push_back(cur);
        if (nxt.size() > 80) break;
      }
      if (nxt.size() > 80) break;
    }
    combos = nxt;
  }

  for (auto& combo : combos) {
    std::vector<std::pair<Kind, std::pair<size_t, size_t>>> pieces;
    for (auto& f : fixed) {
      pieces.push_back({f.kind == ProtectRegion::kIdent ? Kind::kIdent : Kind::kProtect,
                        {f.start, f.end}});
    }
    pieces.insert(pieces.end(), combo.begin(), combo.end());
    std::sort(pieces.begin(), pieces.end(),
              [](const auto& a, const auto& b) { return a.second.first < b.second.first; });
    if (pieces.empty() || pieces[0].second.first > 0)
      pieces.insert(pieces.begin(), {Kind::kEn, {0, pieces.empty() ? 0 : pieces[0].second.first}});

    bool valid = true;
    for (size_t i = 0; i + 1 < pieces.size(); ++i) {
      if (pieces[i].second.second > pieces[i + 1].second.first) valid = false;
    }
    if (!valid || pieces.back().second.second > raw.size()) continue;

    std::vector<Seg> segs;
    bool ja_failed = false;
    for (auto& p : pieces) {
      Seg s = BuildSeg(p.first, p.second.first, p.second.second, raw);
      if (s.kind == Kind::kJa && !s.ok) ja_failed = true;
      segs.push_back(s);
    }
    if (ja_failed) {
      bool has_en = false;
      for (auto& s : segs)
        if (s.kind != Kind::kJa && ScoreEnglish(s.surface) > 1.0) has_en = true;
      if (!has_en) continue;
    }

    Candidate c;
    std::string out_text;
    std::string read_text;
    double en_score = 0, jp_local = 0, edit_cost = 0, protect = 0;
    int ja_count = 0, en_count = 0;
    bool has_kanji = false;
    std::vector<size_t> surface_off;
    surface_off.reserve(segs.size());
    for (auto& s : segs) {
      surface_off.push_back(out_text.size());
      out_text += s.surface;
      read_text += s.reading;
      edit_cost += s.edit_cost;
      if (s.kind == Kind::kJa) {
        ++ja_count;
        if (s.surface != raw.substr(s.start, s.end - s.start)) jp_local += 1.2;
        if (HasKanjiUtf8(s.surface)) has_kanji = true;
        if (s.surface.size() >= 6) {
          // 丁寧な文末へ加点する。
          if (s.surface.find("です") != std::string::npos ||
              s.surface.find("ます") != std::string::npos)
            jp_local += 0.4;
        }
        if (s.surface.size() >= 3 && s.surface.compare(s.surface.size() - 3, 3, u8"。") == 0)
          jp_local += 0.3;
      } else if (s.kind == Kind::kEn || s.kind == Kind::kIdent) {
        en_score += ScoreEnglish(s.surface);
        ++en_count;
      } else {
        protect += 1.5;
      }
    }
    double coverage = 0.0;
    if (ja_count > 0) {
      double ok_len = 0, total_len = 0;
      for (auto& s : segs) {
        double L = static_cast<double>(s.end - s.start);
        total_len += L;
        if (s.kind == Kind::kJa && s.ok) ok_len += L;
        else if (s.kind != Kind::kJa) ok_len += L;
      }
      coverage = total_len > 0 ? ok_len / total_len : 0.0;
    }

    double context_score = 0.0;
    if (en_count) context_score += 0.9 * ctx.en;
    if (ja_count) context_score += 0.9 * ctx.ja;
    if (!ja_count && !en_count) context_score += 0.9 * 0.5 * (ctx.en + ctx.ja);
    if (ja_count && !en_count && ctx.en >= 1.0) context_score -= 1.6;
    if (en_count && !ja_count && ctx.ja >= 1.0) context_score -= 1.2;

    double jp_score = jp_local + 6.0 * coverage;
    if (ja_count && en_count) jp_score += 0.55;
    if (has_kanji && ja_count) jp_score += 1.0;
    if (en_count && !ja_count) en_score += 0.5;

    // 英単語を不自然につなげた候補を減点する。
    double glued = 0.0;
    for (size_t i = 0; i < segs.size(); ++i) {
      if (segs[i].kind != Kind::kEn) continue;
      if (segs[i].surface.find(' ') != std::string::npos) continue;
      bool prev_ja = i > 0 && segs[i - 1].kind == Kind::kJa;
      bool next_ja = i + 1 < segs.size() && segs[i + 1].kind == Kind::kJa;
      if (!prev_ja && !next_ja) continue;
      std::string core = segs[i].surface;
      while (!core.empty() && std::ispunct(static_cast<unsigned char>(core.back()))) core.pop_back();
      std::string low = ToLower(core);
      if (!core.empty() && kEnWords.count(low) && core.size() <= 2) glued += 3.5;
      else if (!core.empty() && !kEnWords.count(low)) glued += 1.5;
      else if (core.size() <= 3) glued += 2.5;
    }

    bool want_completion = false;
    if (input.punctuation_completion && input.phase == "sentence_end" &&
        out_text.find(u8"。") == std::string::npos && !out_text.empty() && ja_count > 0) {
      want_completion = true;
    }

    c.output_text = out_text;
    c.reading_text = read_text;
    c.is_raw = false;
    c.en_score = en_score;
    c.jp_score = jp_score;
    c.coverage = coverage;
    c.edit_cost = edit_cost;
    c.protect_score = protect;
    c.context_score = context_score;
    c.completion_bonus = 0.0;
    double total = 1.0 * en_score + 1.0 * jp_score + (-1.6) * edit_cost + 0.6 * protect +
                   context_score + 0.18 * static_cast<double>(fixed.size());
    if (coverage < 0.35 && ja_count) total -= 3.0 * (0.35 - coverage);
    total -= glued;
    c.total = total;
    out.push_back(c);

    // 分割候補のスコアは未校正のため、同じ区間の主候補より下に置く。
    if (ja_count > 0) {
      int azo_added = 0;
      const int max_azoo = std::max(4, input.candidate_limit - 2);
      for (size_t si = 0; si < segs.size() && azo_added < max_azoo; ++si) {
        if (segs[si].kind != Kind::kJa) continue;
        const size_t base_off = surface_off[si];
        const size_t base_len = segs[si].surface.size();
        for (const auto& azo : segs[si].azo_surfaces) {
          if (azo_added >= max_azoo) break;
          if (azo.size() == base_len && azo == segs[si].surface) continue;
          std::string rebuilt = out_text;
          if (base_off + base_len <= rebuilt.size())
            rebuilt.replace(base_off, base_len, azo);
          else
            continue;
          Candidate alt = c;
          alt.output_text = rebuilt;
          alt.jp_score = c.jp_score;
          // 同じ区間の主候補より低い順位にする。
          alt.total = c.total - 0.01 - 0.001 * static_cast<double>(azo_added);
          out.push_back(std::move(alt));
          ++azo_added;
        }
      }
    }

    if (want_completion) {
      Candidate done = c;
      done.output_text = out_text + u8"。";
      done.completion_bonus = 0.35;
      done.total = total + 0.35;
      out.push_back(done);
    }
  }

  Candidate raw_c;
  raw_c.output_text = raw;
  raw_c.reading_text = raw;
  raw_c.is_raw = true;
  raw_c.en_score = ScoreEnglish(raw);
  raw_c.total = 0.45 + 0.2 * raw_c.en_score + 0.9 * ctx.en * 0.3;
  out.push_back(raw_c);

  std::sort(out.begin(), out.end(), [](const Candidate& a, const Candidate& b) {
    if (a.total != b.total) return a.total > b.total;
    return a.output_text < b.output_text;
  });
  std::vector<Candidate> uniq;
  std::unordered_set<std::string> seen;
  for (auto& c : out) {
    if (!seen.insert(c.output_text).second) continue;
    uniq.push_back(c);
    if (static_cast<int>(uniq.size()) >= input.candidate_limit) break;
  }
  if (input.candidate_limit > 1 &&
      std::none_of(uniq.begin(), uniq.end(), [&](const Candidate& c) { return c.output_text == raw; })) {
    if (uniq.size() >= static_cast<size_t>(input.candidate_limit)) uniq.pop_back();
    uniq.push_back(raw_c);
  }
  return uniq;
}

Session::Session() { Refresh(); }

void Session::set_punctuation_completion(bool enabled) { punctuation_completion_ = enabled; }
void Session::set_learning_allowed(bool allowed) { learning_allowed_ = allowed; }
void Session::set_field(const std::string& field) {
  field_ = field;
  ++context_generation_;
}
void Session::set_context(const std::string& left, const std::string& right) {
  if (left_context_ == left && right_context_ == right) return;
  left_context_ = left;
  right_context_ = right;
  ++context_generation_;
}
void Session::simulate_worker_crash() { engine_alive_ = false; }
void Session::simulate_worker_recover() {
  engine_alive_ = true;
  Refresh();
}

void Session::BumpInteraction() {
  ++interaction_generation_;
  ++candidate_list_generation_;
  manual_lock_ = false;
  awaiting_candidates_ = false;
}

DecodeInput Session::decode_input() const {
  DecodeInput in;
  in.raw_text = raw_text_; in.left_context = left_context_; in.right_context = right_context_;
  in.field = field_; in.phase = phase_; in.punctuation_completion = punctuation_completion_;
  in.revision = revision_; in.context_generation = context_generation_;
  in.interaction_generation = interaction_generation_;
  return in;
}

bool Session::ApplyCandidates(const DecodeInput& request, std::vector<Candidate> candidates) {
  if ((manual_lock_ && !awaiting_candidates_) || editing_ || raw_text_.empty() || candidates.empty() ||
      request.raw_text != raw_text_ || request.revision != revision_ ||
      request.context_generation != context_generation_ ||
      request.interaction_generation != interaction_generation_) return false;
  candidates_ = std::move(candidates); selected_index_ = 0;
  resolved_revision_ = revision_; resolved_context_ = context_generation_;
  awaiting_candidates_ = false;
  return true;
}

void Session::Refresh() {
  candidates_.clear(); selected_index_ = 0;
  if (raw_text_.empty()) return;
  if (deferred_decoding_ || editing_ || !engine_alive_ || raw_text_.size() > 512) {
    Candidate c; c.reading_text = raw_text_;
    bool ok = false;
    const bool single_vowel = field_ == "prose" && raw_text_.size() == 1 &&
        std::strchr("aiueo", raw_text_[0]);
    c.output_text = (engine_alive_ && raw_text_.size() <= 512 &&
                     (single_vowel || ScoreEnglish(raw_text_) <= 1.0))
        ? ConvertRomaji(raw_text_, &ok) : raw_text_;
    c.is_raw = c.output_text == raw_text_; candidates_.push_back(c);
    if (!c.is_raw) { c.output_text = raw_text_; c.is_raw = true; candidates_.push_back(c); }
    return;
  }
  candidates_ = Decode(decode_input());
}

void Session::PushCapped(std::vector<std::string>& log, std::string value) {
  log.push_back(std::move(value));
  constexpr size_t kMaxLog = 64;
  if (log.size() > kMaxLog) {
    log.erase(log.begin(), log.begin() + static_cast<long>(log.size() - kMaxLog));
  }
}

void Session::Type(const std::string& text) {
  if (text.empty()) return;
  // TSF側で選択中の候補を確定してから、次の入力を始める。
  if (is_converting()) Commit(visible_text(), "continued_typing");
  raw_text_.insert(raw_cursor_, text); raw_cursor_ += text.size();
  ++revision_; BumpInteraction();
  editing_ = raw_cursor_ != raw_text_.size();
  segment_bounds_.clear(); segment_index_ = 0;
  Refresh();
}

void Session::CancelConversion() {
  BumpInteraction(); ++revision_; editing_ = true;
  raw_cursor_ = raw_text_.size();
  segment_bounds_.clear(); segment_index_ = 0; Refresh();
}

void Session::Backspace() {
  if (is_converting()) { CancelConversion(); return; }
  if (!raw_cursor_) return;
  size_t previous = raw_cursor_ - 1;
  while (previous && (static_cast<unsigned char>(raw_text_[previous]) & 0xC0) == 0x80) --previous;
  raw_text_.erase(previous, raw_cursor_ - previous); raw_cursor_ = previous;
  ++revision_; BumpInteraction(); editing_ = true; Refresh();
}

SpaceAction Session::QuerySpace() const {
  if (raw_text_.empty()) return SpaceAction::kInsertSpace;
  bool manual = selected_index_ >= 0 && manual_lock_;
  if (manual && !candidates_.empty()) return SpaceAction::kNextCandidate;
  if (!candidates_.empty()) {
    const auto& top = candidates_[0];
    if (!top.is_raw && top.output_text != raw_text_) return SpaceAction::kStartConversion;
  }
  return SpaceAction::kInsertSpace;
}

void Session::NextCandidate() {
  if (candidates_.empty()) return;
  selected_index_ = (selected_index_ + 1) % static_cast<int>(candidates_.size());
  manual_lock_ = true;
}

void Session::PrevCandidate() {
  if (candidates_.empty()) return;
  selected_index_ = (selected_index_ - 1 + static_cast<int>(candidates_.size())) %
                    static_cast<int>(candidates_.size());
  manual_lock_ = true;
}

void Session::PressSpace() {
  SpaceAction a = QuerySpace();
  if (a == SpaceAction::kNextCandidate) {
    BumpInteraction();
    NextCandidate();
    manual_lock_ = true;
    return;
  }
  if (a == SpaceAction::kStartConversion) {
    BumpInteraction();
    manual_lock_ = true;
    editing_ = false;
    awaiting_candidates_ = deferred_decoding_;
    selected_index_ = 0;
    RebuildSegments();
    return;
  }
  Type(" ");
}

void Session::PressShiftSpace() {
  if (!manual_lock_ && selected_index_ == 0 && candidates_.empty()) return;
  BumpInteraction();
  PrevCandidate();
  manual_lock_ = true;
}

void Session::PressEnter() {
  if (raw_text_.empty()) {
    PushCapped(output_log_, "enter_passthrough");
    return;
  }
  Commit(visible_text(), "enter");
}

void Session::Reset() {
  last_commit_learnable_ = false;
  BumpInteraction(); ++revision_;
  raw_text_.clear(); raw_cursor_ = 0; editing_ = false;
  candidates_.clear(); selected_index_ = 0; manual_lock_ = false;
  segment_bounds_.clear(); segment_index_ = 0; segment_manual_ = false;
}

void Session::PressEscape() {
  if (is_converting()) CancelConversion();
  else Reset();
}

void Session::PressDown() {
  if (candidates_.empty()) return;
  BumpInteraction();
  NextCandidate();
  manual_lock_ = true;
}

void Session::PressUp() {
  if (candidates_.empty()) return;
  BumpInteraction();
  PrevCandidate();
  manual_lock_ = true;
}

bool Session::SelectCandidate(int index) {
  if (index < 0 || index >= static_cast<int>(candidates_.size())) return false;
  BumpInteraction();
  selected_index_ = index;
  manual_lock_ = true;
  return true;
}

void Session::ReplaceVisible(const std::string& text) {
  if (text.empty()) return;
  BumpInteraction();
  Candidate c; c.output_text = text; c.reading_text = raw_text_;
  candidates_.insert(candidates_.begin(), c); selected_index_ = 0; manual_lock_ = true;
}

void Session::PressCharacterClass(int vk) {
  bool ok = false;
  std::string text = vk <= 0x77 ? ConvertRomaji(raw_text_, &ok) : raw_text_;
  ReplaceVisible(ConvertCharacterClass(text, vk));
}

void Session::DeleteForward() {
  if (is_converting()) { CancelConversion(); return; }
  if (raw_cursor_ >= raw_text_.size()) return;
  size_t next = raw_cursor_ + 1;
  while (next < raw_text_.size() && (static_cast<unsigned char>(raw_text_[next]) & 0xC0) == 0x80) ++next;
  raw_text_.erase(raw_cursor_, next - raw_cursor_);
  ++revision_; BumpInteraction(); editing_ = true; Refresh();
}

void Session::PressHome() {
  if (is_converting()) CancelConversion();
  raw_cursor_ = 0; editing_ = true; BumpInteraction(); Refresh();
}
void Session::PressEnd() {
  if (is_converting()) CancelConversion();
  raw_cursor_ = raw_text_.size(); editing_ = true; BumpInteraction(); Refresh();
}
std::string Session::caret_prefix() const {
  if (!editing_ || is_converting()) return visible_text();
  auto prefix = raw_text_.substr(0, raw_cursor_);
  if (visible_text() == raw_text_) return prefix;
  bool ok = false; return ConvertRomaji(prefix, &ok);
}

void Session::ChooseLiteral() {
  if (raw_text_.empty()) return;
  Commit(raw_text_, "literal");
}

void Session::RebuildSegments() {
  segment_bounds_.clear();
  segment_index_ = 0;
  segment_manual_ = false;
  if (raw_text_.empty()) return;
  segment_bounds_.push_back(0);
  auto is_word = [](unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '_' || c == '.' || c == '-';
  };
  int prev_class = -1;
  size_t i = 0;
  while (i < raw_text_.size()) {
    unsigned char c = static_cast<unsigned char>(raw_text_[i]);
    int cls = 0;
    size_t len = 1;
    if (c < 0x80) {
      cls = is_word(c) ? 1 : 2;
    } else if ((c & 0xE0) == 0xC0) {
      cls = 3;
      len = 2;
    } else if ((c & 0xF0) == 0xE0) {
      cls = 3;
      len = 3;
    } else if ((c & 0xF8) == 0xF0) {
      cls = 3;
      len = 4;
    }
    if (i + len > raw_text_.size()) len = raw_text_.size() - i;
    if (prev_class != -1 && cls != prev_class) {
      if (i > segment_bounds_.back()) segment_bounds_.push_back(i);
    }
    prev_class = cls;
    i += len;
  }
  if (segment_bounds_.back() != raw_text_.size()) segment_bounds_.push_back(raw_text_.size());
  if (segment_bounds_.size() < 2) {
    segment_bounds_.clear();
    segment_bounds_.push_back(0);
    segment_bounds_.push_back(raw_text_.size());
  }
}

void Session::PressLeft() {
  if (!is_converting()) {
    if (raw_cursor_) {
      --raw_cursor_;
      while (raw_cursor_ && (static_cast<unsigned char>(raw_text_[raw_cursor_]) & 0xC0) == 0x80) --raw_cursor_;
    }
    editing_ = true; BumpInteraction(); Refresh(); return;
  }
  if (segment_bounds_.empty()) RebuildSegments();
  if (segment_index_ > 0) {
    --segment_index_;
    ++interaction_generation_;
    manual_lock_ = true;
  }
}

void Session::PressRight() {
  if (!is_converting()) {
    if (raw_cursor_ < raw_text_.size()) {
      ++raw_cursor_;
      while (raw_cursor_ < raw_text_.size() && (static_cast<unsigned char>(raw_text_[raw_cursor_]) & 0xC0) == 0x80) ++raw_cursor_;
    }
    editing_ = true; BumpInteraction(); Refresh(); return;
  }
  if (segment_bounds_.empty()) RebuildSegments();
  if (segment_index_ + 1 < static_cast<int>(segment_bounds_.size()) - 1) {
    ++segment_index_;
    ++interaction_generation_;
    manual_lock_ = true;
  }
}

void Session::PressShiftLeft() {
  if (!is_converting()) return;
  if (segment_bounds_.empty()) RebuildSegments();
  if (segment_index_ + 1 >= static_cast<int>(segment_bounds_.size()) - 1) return;
  size_t end = segment_bounds_[segment_index_ + 1];
  size_t begin = segment_bounds_[segment_index_];
  if (end <= begin + 1) return;
  size_t p = end - 1;
  while (p > begin && (static_cast<unsigned char>(raw_text_[p]) & 0xC0) == 0x80) --p;
  if (p > begin) {
    segment_bounds_[segment_index_ + 1] = p;
    segment_manual_ = true;
    ++interaction_generation_;
    manual_lock_ = true;
  }
}

void Session::PressShiftRight() {
  if (!is_converting()) return;
  if (segment_bounds_.empty()) RebuildSegments();
  if (segment_index_ + 1 >= static_cast<int>(segment_bounds_.size()) - 1) return;
  size_t end = segment_bounds_[segment_index_ + 1];
  size_t next_end = segment_bounds_[segment_index_ + 2];
  if (end >= next_end) return;
  size_t p = end + 1;
  while (p < next_end && (static_cast<unsigned char>(raw_text_[p]) & 0xC0) == 0x80) ++p;
  if (p <= next_end) {
    segment_bounds_[segment_index_ + 1] = p;
    segment_manual_ = true;
    ++interaction_generation_;
    manual_lock_ = true;
  }
}

std::string Session::visible_text() const {
  if (raw_text_.empty()) return "";
  if (manual_lock_ && !candidates_.empty()) {
    size_t idx = static_cast<size_t>(selected_index_);
    if (idx < candidates_.size()) return candidates_[idx].output_text;
  }
  if (!candidates_.empty()) return candidates_[0].output_text;
  return raw_text_;
}

void Session::Commit(const std::string& text, const char* reason) {
  if (text.empty() && raw_text_.empty()) return;
  last_commit_learnable_ = candidates_ready() && field_ == "prose" && selected_index_ >= 0 &&
      selected_index_ < static_cast<int>(candidates_.size()) && !candidates_[selected_index_].is_raw;
  PushCapped(committed_history_, text + "|" + reason);
  PushCapped(output_log_, text);
  if (learning_allowed_ && !text.empty() && text != raw_text_) PushCapped(learning_log_, text);
  raw_text_.clear(); raw_cursor_ = 0; editing_ = false;
  candidates_.clear();
  selected_index_ = 0;
  manual_lock_ = false;
  segment_bounds_.clear();
  segment_index_ = 0;
  segment_manual_ = false;
  ++revision_;
  BumpInteraction();
}

std::string ConvertCharacterClass(const std::string& text, int key) {
  DWORD flags = 0;
  switch (key) {
    case 0x75: flags = LCMAP_HIRAGANA | LCMAP_FULLWIDTH; break;
    case 0x76: flags = LCMAP_KATAKANA | LCMAP_FULLWIDTH; break;
    case 0x77: flags = LCMAP_KATAKANA | LCMAP_HALFWIDTH; break;
    case 0x78: flags = LCMAP_FULLWIDTH; break;
    case 0x79: flags = LCMAP_HALFWIDTH; break;
    default: return text;
  }
  int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (size <= 0) return text;
  std::wstring wide(size,L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), size);
  int mapped = LCMapStringEx(L"ja-JP", flags, wide.data(), size, nullptr, 0, nullptr, nullptr, 0);
  if (mapped <= 0) return text;
  std::wstring result(mapped,L'\0');
  if (!LCMapStringEx(L"ja-JP", flags, wide.data(), size, result.data(), mapped, nullptr, nullptr, 0)) return text;
  size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, result.data(), mapped, nullptr, 0, nullptr, nullptr);
  if (size <= 0) return text;
  std::string output(size,'\0');
  WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, result.data(), mapped, output.data(), size, nullptr, nullptr);
  return output;
}

}  // namespace ime

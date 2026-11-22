#pragma once

#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace ime {

struct AzookeyCandidate {
  std::string text;
  int corresponding_count = 0;
  float score = -std::numeric_limits<float>::infinity();
};
std::vector<AzookeyCandidate> ParseAzookeyCandidates(const std::string& json);

// azookey-engine.dllを動的に読み込む。利用できない場合は空の結果を返す。
enum class ConversionModel { Standard, Compact, DictionaryOnly, ExtendedSearch, CompactExtendedSearch, HybridLowMemory, HybridFull, HybridDictionaryVerify, Automatic };
void AzookeySelectStartupModel(ConversionModel model);
void AzookeySetCachePressure(bool low);
void AzookeySetDictionaryPreview(bool enabled);
bool AzookeyEnsureReady();
bool AzookeyAvailable();
std::vector<std::string> AzookeyConvert(const std::string& hiragana, int limit,
                                     const std::string& left_context = {}, const std::string& right_context = {},
                                     bool refine = true, bool shallow_search = false);
void AzookeySetZenzai(bool enabled, const std::string& weight_path, int inference_limit);
std::string AzookeyZenzaiStatus();
bool AzookeySetUserDictionary(const std::string& json);
// Request-local dictionary analysis; callers validate the input and word boundaries.
std::string AzookeyDictionaryParts(const std::string& reading, const std::string& left,
                                   const std::string& right);
// One alternate spelling lookup shared by provisional and final ranking of
// the same worker request. The dictionary and context are fixed for its life.
struct AzookeyReadingSample {
  std::string original, alternate;
  float original_score = std::numeric_limits<float>::quiet_NaN();
  std::vector<AzookeyCandidate> candidates;
};
// Reuse the original reading score across at most two alternate readings
// for one input and its document context. Never share this across requests.
struct AzookeyReadingComparison {
  AzookeyReadingSample* sample = nullptr;
  int remaining = 2;
  float original_score = std::numeric_limits<float>::quiet_NaN();
  float minimum_gain = 0;
  float lead_over_second = std::numeric_limits<float>::infinity();
  float first_gain = -std::numeric_limits<float>::infinity();
  std::string_view incumbent;
  bool include_kana = false;
};
std::vector<std::string> AzookeyAlternativeReading(const std::string& reading, const std::string& alternative,
                                                  const std::string& left, const std::string& right,
                                                  AzookeyReadingComparison* comparison = nullptr);
class AzookeyPartCapture {
 public:
  AzookeyPartCapture(const std::string& reading,const std::string& left,const std::string& right);
  ~AzookeyPartCapture();
  AzookeyPartCapture(const AzookeyPartCapture&) = delete;
  bool Matches(const std::string& reading,const std::string& left,const std::string& right) const;
  std::string json;
  float first_score = -std::numeric_limits<float>::infinity();
 private:
  const std::string &reading_, &left_, &right_;
  AzookeyPartCapture* previous_;
};

}  // namespace ime

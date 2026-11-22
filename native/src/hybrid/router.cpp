#include "router.h"
#include "iroha/unicode.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <psapi.h>
#include <string_view>

namespace {
int InferenceThreads() {
  DWORD_PTR available = 0, system = 0;
  int threads = 4;
  if (GetProcessAffinityMask(GetCurrentProcess(), &available, &system) && available) {
    threads = 0;
    for (; available && threads < 4; available &= available - 1) ++threads;
  }
  if (const char* value = std::getenv("IROHA_THREADS")) {
    char* end = nullptr;
    const long requested = std::strtol(value, &end, 10);
    if (*value && !*end && requested > 0 && requested < threads)
      threads = static_cast<int>(requested);
  }
  return threads;
}
constexpr float threshold = 0.44f;
constexpr std::u32string_view marks = U"、。！？「」（）・";
std::u32string Marks(const std::u32string &text) {
  std::u32string result;
  for (auto c : text)
    if (marks.find(c) != marks.npos)
      result.push_back(c);
  return result;
}
bool Valid(const std::u32string &s, const std::u32string &reading) {
  return !s.empty() && s.size() <= std::max(size_t(12), 2 * reading.size()) &&
         Marks(s) == Marks(reading) &&
         std::none_of(s.begin(), s.end(),
                      [](char32_t c) { return c < 32 || c == 127; });
}
// When both nouns vary across the leading candidates, the dictionary and
// neural ranker can each miss a plausible combination. Consult compact search
// for alternatives on short phrases, retaining the automatic first candidate.
bool AmbiguousPossessive(const std::u32string& reading, const std::vector<ime::Candidate>& rows) {
  if (reading.size() < 3 || reading.size() > 12 || reading.find(U'の', 1) >= reading.size() - 1) return false;
  constexpr std::string_view particle = u8"の";
  std::string_view firstLeft, firstRight;
  for (size_t i = 0; i < std::min<size_t>(4, rows.size()); ++i) {
    std::string_view text = rows[i].output_text;
    const auto pos = text.find(particle);
    if (pos == text.npos || pos == 0 || pos + particle.size() == text.size()) continue;
    const auto left = text.substr(0, pos), right = text.substr(pos + particle.size());
    if (firstLeft.empty()) { firstLeft = left; firstRight = right; }
    else if (left != firstLeft && right != firstRight) return true;
  }
  return false;
}
void TraceMemory(const char *stage) {
  PROCESS_MEMORY_COUNTERS_EX counters{};
  counters.cb = sizeof(counters);
  if (GetProcessMemoryInfo(
          GetCurrentProcess(),
          reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
          sizeof(counters)))
    std::fprintf(stderr, "HM %s %zu %zu\n", stage, counters.WorkingSetSize,
                 counters.PrivateUsage);
}
struct Trace {
  bool enabled;
  const char *why = "bypass";
  double gap = -1;
  int deep = 0, second = 0, swapped = 0;
  ULONGLONG deep_ms = 0, second_ms = 0;
  ~Trace() {
    if (enabled)
      std::fprintf(stderr, "HG %s %.6f %d %d %d %llu %llu\n", why, gap, deep,
                   second, swapped, deep_ms, second_ms);
  }
};
void Merge(const ime::DecodeInput &req, const std::string &reading,
           const std::vector<std::string> &heads,
           const std::vector<ime::Candidate> &deep,
           std::vector<ime::Candidate> &rows) {
  std::vector<ime::Candidate> out;
  const auto is_literal = [&](const auto &c) {
    return c.is_raw || c.output_text == req.raw_text;
  };
  const auto literal = std::find_if(rows.begin(), rows.end(), is_literal);
  const size_t limit =
      req.candidate_limit -
      static_cast<int>(literal != rows.end() && req.candidate_limit > 1);
  auto add = [&](const ime::Candidate &c) {
    if (out.size() < limit && !is_literal(c) && !c.output_text.empty() &&
        std::none_of(out.begin(), out.end(), [&](const auto &v) {
          return v.output_text == c.output_text;
        }))
      out.push_back(c);
  };
  for (const auto &text : heads) {
    if (text.empty())
      continue;
    auto old = std::find_if(rows.begin(), rows.end(), [&](const auto &c) {
      return c.output_text == text;
    });
    if (old != rows.end())
      add(*old);
    else {
      auto d = std::find_if(deep.begin(), deep.end(), [&](const auto &c) {
        return c.output_text == text;
      });
      if (d != deep.end())
        add(*d);
      else {
        ime::Candidate c;
        c.output_text = text;
        c.reading_text = reading;
        c.total = rows.front().total;
        add(c);
      }
    }
  }
  for (size_t i = 0; out.size() < limit && (i < rows.size() || i < deep.size());
       ++i) {
    if (i < deep.size())
      add(deep[i]);
    if (i < rows.size())
      add(rows[i]);
  }
  if (literal != rows.end() &&
      out.size() < static_cast<size_t>(req.candidate_limit))
    out.push_back(*literal);
  if (!out.empty())
    rows.swap(out);
}
double SurfaceRankScore(double log_probability, size_t position) {
  return log_probability - 1.5 * std::log1p(static_cast<double>(position));
}
std::vector<std::string> RankedSurfaces(const std::vector<std::u32string>& pool,
                                        const std::vector<float>& scores) {
  if (scores.size() != pool.size()) return {};
  // Keep all rescored candidates in score order so the next-best result is accessible with one key.
  std::vector<std::pair<double, size_t>> ordered;
  for (size_t i = 0; i < scores.size(); ++i) {
    const double score = SurfaceRankScore(scores[i],i);
    if (std::isfinite(score)) ordered.emplace_back(score, i);
  }
  if (ordered.empty()) return {};
  std::stable_sort(ordered.begin(), ordered.end(),
      [](const auto& a, const auto& b) { return a.first > b.first; });
  std::vector<std::string> result;
  for (const auto& entry : ordered) result.push_back(iroha::Utf32ToUtf8(pool[entry.second]));
  return result;
}
// Score complete surfaces with the same reading and left context. Retain a prior from
// the light model/dictionary order so a small likelihood difference cannot dislodge a reliable candidate.
std::vector<std::string> RerankSurfaces(iroha::ZenzEngine& expert, const std::u32string& reading,
    const std::u32string& left, const std::u32string& primary, const std::vector<ime::Candidate>& rows,
    std::vector<std::pair<std::u32string, float>>* retained,
    const std::function<void(const std::string&)>& early, bool primary_offered) {
  std::vector<std::u32string> pool{primary};
  auto add = [&](const std::u32string& text) {
    if (Valid(text,reading) && std::find(pool.begin(),pool.end(),text)==pool.end()) pool.push_back(text);
  };
  for (size_t i=0; i<std::min<size_t>(4,rows.size()); ++i)
    if (!rows[i].is_raw) add(iroha::Utf8ToUtf32(rows[i].output_text));
  std::string error;
  std::vector<float> scores;
  size_t leader = 0;
  double leader_score = -std::numeric_limits<double>::infinity();
  constexpr double tolerance = 1e-5;
  // Compare a primary already offered for live display with the dictionary's
  // first choice only when the primary is its distinct second choice.
  if (early && primary_offered && rows.size()>=2 && pool.size()>=2 &&
      !rows[0].is_raw && !rows[1].is_raw && rows[1].output_text==iroha::Utf32ToUtf8(primary) &&
      pool[1]==iroha::Utf8ToUtf32(rows[0].output_text)) {
    const std::vector<std::u32string> pair{pool[0],pool[1]};
    if (expert.Score(pair,reading,left,&scores,&error) && scores.size()==pair.size()) {
      for (size_t i=0;i<scores.size();++i) {
        const double score=SurfaceRankScore(scores[i],i);
        if (std::isfinite(score) && score>leader_score) { leader=i; leader_score=score; }
      }
      // Log probability is at most zero. Even a perfect score among the
      // remaining whole-sentence candidates cannot overcome this ordinal prior.
      if (leader_score>SurfaceRankScore(0,pair.size())+tolerance)
        early(iroha::Utf32ToUtf8(pool[leader]));
    } else {
      scores.clear();  // The ordinary joint scoring pass can still succeed.
    }
  }
  std::vector<std::u32string> alternatives;
  auto greedyReady = [&](const std::u32string& surface) {
    if (!early || !Valid(surface,reading)) return;
    if (!scores.empty()) {
      const size_t rival=1-leader;
      if (surface==pool[rival] && leader_score>SurfaceRankScore(scores[rival],rival)+tolerance) return;
    }
    // A different spelling may be what was intended even if later ranked lower.
    early(iroha::Utf32ToUtf8(surface));
  };
  if (!expert.ConvertWithPreview(reading,left,3,&alternatives,&error,greedyReady)) return {};
  for (const auto& text : alternatives) add(text);
  if (scores.empty()) {
    if (!expert.Score(pool,reading,left,&scores,&error) || scores.size()!=pool.size()) return {};
  } else if (pool.size()>scores.size()) {
    const std::vector<std::u32string> remaining(pool.begin()+scores.size(),pool.end());
    std::vector<float> tail;
    if (!expert.Score(remaining,reading,left,&tail,&error) || tail.size()!=remaining.size()) return {};
    scores.insert(scores.end(),tail.begin(),tail.end());
  }
  auto ranked = RankedSurfaces(pool, scores);
  if (retained) {
    for (size_t i = 0; i < pool.size(); ++i)
      retained->emplace_back(std::move(pool[i]), scores[i]);
  }
  return ranked;
}
} // namespace
HybridRouter::HybridRouter(ime::ConversionModel configured,
                           const std::filesystem::path &model_folder)
    : folder_(model_folder) {
  const char *setting = std::getenv("YOMITSUGU_PROBE_POLICY");
  const std::string_view name = setting ? setting : "";
  if (!setting) {
    if (configured == ime::ConversionModel::HybridLowMemory)
      mode_ = Mode::Auto;
    if (configured == ime::ConversionModel::HybridFull)
      mode_ = Mode::Full;
    if (configured == ime::ConversionModel::Automatic || configured == ime::ConversionModel::HybridDictionaryVerify)
      mode_ = Mode::Verify;
    if (enabled()) {
      _putenv_s("YOMITSUGU_PROBE_UBATCH", "32");
      _putenv_s("IROHA_GPU_LAYERS", "0");
      _putenv_s("IROHA_THREADS", std::to_string(InferenceThreads()).c_str());
      _putenv_s("IROHA_CROSS_KV", "1");
    }
  }
  if (name == "hybrid")
    mode_ = Mode::Auto;
  if (name == "verify")
    mode_ = Mode::Verify;
  if (name == "hybrid-warm")
    mode_ = Mode::Warm;
  if (name == "full")
    mode_ = Mode::Full;
  if (!enabled())
    return;
  if (const auto *p = _wgetenv(L"YOMITSUGU_PROBE_GGUF"))
    folder_ = std::filesystem::path(p).parent_path();
  const char* reference = std::getenv("YOMITSUGU_DRAFT_POLICY");
  serial_reference_ = reference && std::string_view(reference) == "serial";
  trace_ = std::getenv("YOMITSUGU_HYBRID_TRACE") != nullptr;
  pressure_ = CreateMemoryResourceNotification(LowMemoryResourceNotification);
}
HybridRouter::~HybridRouter() {
  if (pressure_)
    CloseHandle(pressure_);
}
bool HybridRouter::ConfirmsSuffix(const std::string& left, const std::string& reading, const std::string& surface) {
  if (surface.empty() || LowMemory() || !Load(jinen_, true)) return false;
  std::u32string result;
  std::string error;
  double margin = 0;
  const auto kana = iroha::Utf8ToUtf32(reading);
  return jinen_->ConvertForRouting(kana, iroha::Utf8ToUtf32(left), &result, &margin, &error)
         && Valid(result, kana) && iroha::Utf32ToUtf8(result) == surface;
}

bool HybridRouter::LowMemory() const {
  BOOL low = FALSE;
  return pressure_ && QueryMemoryResourceNotification(pressure_, &low) && low;
}
std::u32string HybridRouter::Reading(const ime::DecodeInput &q) const {
  if (!enabled() || q.phase == "typing" || q.field != "prose" ||
      !q.right_context.empty())
    return {};
  constexpr std::u32string_view extra = U"'.,!?()[]（）「」、。！？ー-";
  const auto raw = iroha::Utf8ToUtf32(q.raw_text);
  // The romaji table uses z/ for the Japanese middle dot. Keep
  // slash paths, extensions and mixed Latin text on the protected-input path.
  const bool middle_dot = mode_ == Mode::Verify && q.raw_text.size() <= 256 &&
      q.raw_text.find("z/") != std::string::npos &&
      q.raw_text.find_first_not_of("abcdefghijklmnopqrstuvwxyz/,'-") == std::string::npos;
  for (size_t i = 0; i < raw.size(); ++i) {
    const auto c = raw[i];
    if (middle_dot && c == U'/' && i && raw[i-1] == U'z') continue;
    if (!(c >= U'a' && c <= U'z') && extra.find(c) == extra.npos)
      return {};
  }
  std::string kana;
  if (!ime::ConvertRomajiChecked(q.raw_text, &kana))
    return {};
  if (ime::ContainsLatinName(q.raw_text))
    return {};
  const auto reading = iroha::Utf8ToUtf32(ime::NormalizeDictionaryReading(kana));
  if (middle_dot && reading.size() < 8) return {};
  for (auto c : reading)
    if (!(c >= U'ぁ' && c <= U'ゖ') && c != U'ー' &&
        marks.find(c) == marks.npos)
      return {};
  return reading;
}
bool HybridRouter::Load(std::unique_ptr<iroha::ZenzEngine> &model, bool primary,
                        unsigned encoder_batch, const std::u32string &reading,
                        const std::u32string &left) {
  CheckCancelled();
  if (model)
    return true;
  bool &failed = primary ? jinen_failed_ : iroha_failed_;
  if (failed || folder_.empty())
    return false;
  const auto file = folder_ / (primary ? L"jinen-v2-xsmall-Q5_K_M.gguf"
                                       : L"iroha-kkc-v1-Q5_K_M.gguf");
  iroha::ZenzEngine::Options options;
  options.jinenNfkc = primary;
  if (primary) options.decoderCapacity = 128;
  options.encoderBatch = encoder_batch;
  if (!primary && !reading.empty() && ReleaseAfterUse()) {
    const auto needed = 1 + 3*iroha::GraphemeCount(reading) + 8;
    options.decoderCapacity = static_cast<unsigned>(std::min<size_t>(1024, needed));
  }
  auto next = std::make_unique<iroha::ZenzEngine>(file.u8string(), options);
  next->SetCancelCheck([this] { return IsCancelled(); });
  next->SetComputeCancelCheck([](void* self) noexcept {
    return static_cast<HybridRouter*>(self)->IsCancelled();
  }, this);
  std::string error;
  const bool ready = reading.empty()
                         ? next->Prewarm(&error)
                         : next->PrewarmForInput(reading, left, &error);
  CheckCancelled();
  if (!ready) {
    failed = error != iroha::ZenzEngine::kEncoderBatchTooSmall;
    return false;
  }
  model = std::move(next);
  return true;
}
HybridRouter::PrimaryResult HybridRouter::Primary(const std::u32string& reading, const std::u32string& left) {
  PrimaryResult result;
  std::string error;
  if (!Load(jinen_, true) ||
      !jinen_->ConvertForRouting(reading, left, &result.text, &result.margin, &error, &result.terminated) ||
      !Valid(result.text, reading)) return {};
  return result;
}
void HybridRouter::RefineWhole(const ime::DecodeInput &req,
                          const std::u32string &reading,
                          std::vector<ime::Candidate> &rows, ReadingPaths& paths, bool keepExpert, bool& disagreement,
                          SurfaceScores* scores, const std::function<void(const std::string&)>& early, const PrimaryResult* prepared) {
  Trace t{trace_};
  const bool low = LowMemory();
  if (low) {
    iroha_.reset();
    jinen_.reset();
    t.why = "pressure";
    return;
  }
  if (reading.empty() || rows.empty() || rows.front().is_raw ||
      rows.front().output_text == req.raw_text)
    return;
  std::string error;
  const auto left = iroha::Utf8ToUtf32(req.left_context);
  const auto computed = prepared ? PrimaryResult{} : Primary(reading, left);
  CheckCancelled();
  const auto& hypothesis = prepared ? *prepared : computed;
  const auto& primary = hypothesis.text;
  const double margin = hypothesis.margin;
  if (primary.empty()) {
    t.why = "no_primary";
    if (!low) {
      ime::AzookeySetDictionaryPreview(false);
      auto fallback = ime::Decode(req);
      if (!fallback.empty())
        rows.swap(fallback);
    }
    return;
  }
  const std::string first = iroha::Utf32ToUtf8(primary);
  if (early && !prepared && hypothesis.terminated) early(first);
  t.gap = margin;
  const bool same = first == rows.front().output_text;
  const bool uncertain = t.gap <= threshold;
  disagreement = !same || uncertain;
  if (verifies_dictionary() && (!same || uncertain) && reading.size()<=96 && req.candidate_limit>=2 &&
      !LowMemory()) {
    jinen_.reset();
    std::vector<std::string> ranked;
    if (Load(iroha_,false,0,reading,left))
      ranked=RerankSurfaces(*iroha_,reading,left,primary,rows,scores,early,hypothesis.terminated);
    CheckCancelled();
    if (!keepExpert) iroha_.reset();
    if (!ranked.empty()) {
      t.why="ranked";
      Merge(req,iroha::Utf32ToUtf8(reading),ranked,{},rows);
      return;
    }
  }
  t.why = low ? "pressure" : same ? "dict_agree" : uncertain ? "close" : "wide";
  std::vector<ime::Candidate> deep;
  std::vector<std::string> heads{first};
  bool verify = false;
  if (verifies_dictionary() && !low && !same && !uncertain && margin <= 1.0 &&
      rows.size() >= 3 && req.candidate_limit >= 3) {
    verify = std::none_of(rows.begin(), rows.begin() + 3, [&](const auto &row) {
      return row.output_text == first;
    });
  }
  std::string deep_top;
  if (!low && (!same && uncertain)) {
    CheckCancelled();
    auto start = GetTickCount64();
    if (ReleaseAfterUse()) jinen_.reset();
    ime::AzookeySetDictionaryPreview(false);
    deep = ime::Decode(req);
    t.deep = 1;
    t.deep_ms = GetTickCount64() - start;
    const bool useful = !deep.empty() && !deep.front().is_raw &&
                        deep.front().output_text != req.raw_text;
    if (useful)
      deep_top = deep.front().output_text;
    if (!deep_top.empty())
      heads.push_back(deep_top);
  }
  if ((verify || (t.deep && deep_top != first)) && !LowMemory()) {
    if (verify)
      t.why = "verify";
    if (ReleaseAfterUse())
      jinen_.reset();
    const auto start = GetTickCount64();
    std::vector<std::u32string> other;
    // Size a newly loaded T5 context to the complete first prompt.
    const bool loaded = ReleaseAfterUse()
                            ? Load(iroha_, false, 0, reading, left)
                            : Load(iroha_, false);
    bool converted = false;
    const bool eligible = loaded && t.deep && !deep_top.empty() && reading.size() >= 8;
    const auto bucket = iroha::DraftPolicy::BucketFor(reading.size(),deep_top==rows.front().output_text);
    const int width = eligible && !serial_reference_ ? policy_.Width(bucket,LowMemory()) : 0;
    if (width) {
      iroha::DraftMeasure m;
      converted = iroha_->ConvertGuided(reading,left,iroha::Utf8ToUtf32(deep_top),width,m,&other,&error);
      if (converted) policy_.Observe(bucket,m);
      if (trace_) std::fprintf(stderr,"HA %zu %d %d %d %d %d %.0f %d %.0f\n",bucket,width,
                               m.blocks,m.proposed,m.confirmed,m.partial,m.blockUs,m.scalarTokens,m.scalarUs);
    } else {
      if (trace_ && eligible) std::fprintf(stderr,"HA %zu 0\n",bucket);
      converted = loaded && iroha_->Convert(reading,left,1,&other,&error);
    }
    if (!converted && iroha_ &&
        error == iroha::ZenzEngine::kEncoderBatchTooSmall &&
        iroha_->NextEncoderBatch() && !LowMemory()) {
      const unsigned required = iroha_->NextEncoderBatch();
      iroha_.reset();
      converted = Load(iroha_, false, required) &&
                  iroha_->Convert(reading, left, 1, &other, &error);
    }
    if (trace_) {
      TraceMemory("expert");
      if (!converted)
        std::fprintf(stderr, "HE expert conversion failed\n");
    }
    if (converted && !other.empty() && Valid(other.front(), reading)) {
      const auto expert = iroha::Utf32ToUtf8(other.front());
      const bool supported =
          verify && std::any_of(rows.begin(), rows.end(), [&](const auto &c) {
            return !c.is_raw && c.output_text == expert;
          });
      const bool locally_supported = verify && expert != first && !supported && paths.Supports(expert) && !paths.Supports(first);
      if ((expert == deep_top || supported || locally_supported) && expert != first) {
        heads = {expert, first};
        t.swapped = 1;
      } else
        heads = {first, expert, deep_top};
    }
    if (ReleaseAfterUse()) {
      iroha_.reset();
      if (trace_)
        TraceMemory("released");
    }
    t.second = 1;
    t.second_ms = GetTickCount64() - start;
  }
  Merge(req, iroha::Utf32ToUtf8(reading), heads, deep, rows);
}

namespace {
size_t ClauseBoundary(std::u32string_view reading) {
  if (reading.size() < 8 || reading.size() > 96) return 0;
  constexpr std::u32string_view particles[] = {
      U"ので", U"から", U"には", U"にも", U"では", U"とは", U"ことを", U"ために", U"ながら",
      U"を", U"が", U"は", U"で", U"に"};
  size_t best = 0;
  int bestCost = std::numeric_limits<int>::max();
  for (size_t i = 0; i < reading.size();) {
    const bool punctuation = std::u32string_view(U"、。！？").find(reading[i]) != std::u32string_view::npos;
    size_t length = punctuation ? 1 : 0;
    if (!punctuation) {
      for (auto word : particles) {
        if (reading.substr(i, word.size()) == word) { length = word.size(); break; }
      }
    }
    i += length ? length : 1;
    if (!length || i < 3 || i + 3 > reading.size()) continue;
    const int cost = (punctuation ? 0 : 1000) + std::abs(2 * static_cast<int>(i) - static_cast<int>(reading.size()));
    if (cost < bestCost) { bestCost = cost; best = i; }
  }
  return best;
}
}

void HybridRouter::RefinePrimary(const ime::DecodeInput& req, const std::u32string& reading,
                          std::vector<ime::Candidate>& rows, ReadingPaths& paths,
                          const std::function<void(const std::string&)>& early, const PrimaryResult* prepared) {
  std::u32string kana = reading;
  // The romaji table also supports z/ for the Japanese middle dot. Only extend
  // a result already recognized as Japanese prose. Paths and literal fields bypass this pass.
  if (kana.empty() && verifies_dictionary() && req.field == "prose" && req.phase != "typing" &&
      req.right_context.empty() && !rows.empty() && !rows[0].is_raw && rows[0].output_text != req.raw_text &&
      req.raw_text.find("z/") != std::string::npos && req.raw_text.size() <= 256 &&
      req.raw_text.find_first_not_of("abcdefghijklmnopqrstuvwxyz/,'-") == std::string::npos &&
      !ime::ContainsLatinName(req.raw_text)) {
    std::string normalized;
    if (ime::ConvertRomajiChecked(req.raw_text, &normalized) &&
        normalized.find(u8"・") != std::string::npos) kana = iroha::Utf8ToUtf32(normalized);
  }
  const bool punctuation = kana.find_first_of(U"、。！？・") != std::u32string::npos;
  const size_t split = (verifies_dictionary() && punctuation) &&
      req.candidate_limit >= 8 && !req.shallow_search
      && !req.dictionary_preview && !LowMemory() ? ClauseBoundary(kana) : 0;
  bool disagreement = false;
  SurfaceScores previous;
  RefineWhole(req, reading, rows, paths, split != 0, disagreement, split && reading==kana ? &previous : nullptr, early, prepared);
  if (split && !disagreement && kana.find(U'・') == std::u32string::npos) {
    iroha_.reset();
    return;
  }
  if (!split || rows.empty() || rows.front().is_raw || rows.front().output_text == req.raw_text) {
    if (split) iroha_.reset();
    return;
  }
  if (mode_ == Mode::Verify && early) {
    // With only trailing punctuation, the pending split was inferred from a
    // particle. Show the whole-reading reranker while exploring its alternatives.
    const auto first_mark = kana.find_first_of(U"、。！？・");
    const auto last_content = kana.find_last_not_of(U"、。！？・");
    if (last_content != std::u32string::npos && first_mark > last_content)
      early(rows.front().output_text);
  }
  AddClause(req, kana, rows, split, true, previous);
}

void HybridRouter::AddClause(const ime::DecodeInput& req, const std::u32string& kana,
                            std::vector<ime::Candidate>& rows, size_t split, bool promote,
                            const SurfaceScores& previous) {
  jinen_.reset();
  if (LowMemory()) { iroha_.reset(); return; }
  const auto left = iroha::Utf8ToUtf32(req.left_context);
  if (!Load(iroha_, false, 0, kana, left)) return;
  auto expert = std::move(iroha_);
  std::string error;
  const auto before = kana.substr(0, split);
  const auto after = kana.substr(split);
  std::vector<std::u32string> prefixes;
  if (!expert->Convert(before, left, 2, &prefixes, &error)) return;
  std::vector<std::u32string> pool;
  auto add = [&](const std::u32string& text) {
    if (Valid(text, kana) && std::find(pool.begin(), pool.end(), text) == pool.end()) pool.push_back(text);
  };
  for (size_t i = 0; i < std::min<size_t>(5, rows.size()); ++i) {
    if (!rows[i].is_raw) add(iroha::Utf8ToUtf32(rows[i].output_text));
  }
  for (const auto& prefix : prefixes) {
    if (!Valid(prefix, before) || LowMemory()) return;
    std::vector<std::u32string> suffixes;
    if (!expert->Convert(after, left + prefix, 3, &suffixes, &error)) return;
    for (const auto& suffix : suffixes) {
      if (Valid(suffix, after)) add(prefix + suffix);
    }
  }
  if (LowMemory()) return;
  std::vector<float> scores(pool.size());
  std::vector<size_t> missing;
  std::vector<std::u32string> fresh;
  // Both passes score complete surfaces with this exact model, reading and
  // left context. The ordinal prior is applied below in the new pool order.
  for (size_t i = 0; i < pool.size(); ++i) {
    const auto found = std::find_if(previous.begin(), previous.end(),
        [&](const auto& value) { return value.first == pool[i]; });
    if (found != previous.end()) scores[i] = found->second;
    else { missing.push_back(i); fresh.push_back(pool[i]); }
  }
  if (!fresh.empty()) {
    std::vector<float> values;
    if (!expert->Score(fresh, kana, left, &values, &error) || values.size()!=missing.size()) return;
    for (size_t i = 0; i < values.size(); ++i) scores[missing[i]] = values[i];
  }
  auto ranked = RankedSurfaces(pool, scores);
  expert.reset();
  if (ranked.empty()) return;
  if (ranked.front() != rows.front().output_text) {
    const auto previous = rows.front().output_text;
    bool confirmed = false;
    if (promote && !LowMemory()) {
      iroha::ZenzEngine::Options options;
      options.decoderCapacity = 128;
      iroha::ZenzEngine verifier((folder_ / L"ggml-model-Q5_K_M.gguf").u8string(), options);
      verifier.SetCancelCheck([this] { return IsCancelled(); });
      verifier.SetComputeCancelCheck([](void* self) noexcept {
        return static_cast<HybridRouter*>(self)->IsCancelled();
      }, this);
      std::vector<std::u32string> answer;
      confirmed = verifier.Convert(kana, left, 1, &answer, &error) && !answer.empty()
          && iroha::Utf32ToUtf8(answer.front()) == ranked.front();
    }
    if (!confirmed) ranked.insert(ranked.begin(), previous);
  }
  Merge(req, iroha::Utf32ToUtf8(kana), ranked, {}, rows);
}

void HybridRouter::Refine(const ime::DecodeInput& request, const std::u32string& reading,
                          std::vector<ime::Candidate>& rows, ReadingPaths& paths,
                          const std::function<void(const std::string&)>& early, const PrimaryResult* prepared) {
  // Beyond the whole-sentence reranker and clause search coverage, an
  // unchecked neural proposal can lose part of the reading. Keep the complete
  // dictionary conversion and its alternatives.
  if (verifies_dictionary() && reading.size() > 96) {
    ReleaseModels();
    return;
  }
  RefinePrimary(request, reading, rows, paths, early, prepared);
  CheckCancelled();
  if (mode_ == Mode::Verify && !reading.empty() && request.raw_text.find("nn")!=std::string::npos && !LowMemory())
    paths.AddAlternateSyllabicN(rows);
  if (reading.empty() || request.phase == "typing" ||
      request.dictionary_preview || request.shallow_search || request.candidate_limit < 2 ||
      rows.empty() || rows.front().is_raw || LowMemory()) return;

  if (mode_ != Mode::Full && (mode_ != Mode::Verify || !AmbiguousPossessive(reading, rows))) return;

  // The expanded search inherits the automatic winner. Run the compact model
  // after releasing the verification contexts, then interleave its alternatives.
  iroha_.reset();
  jinen_.reset();
  ime::AzookeySetDictionaryPreview(false);
  auto expanded = ime::Decode(request);
  // Automatic already explores punctuation boundaries. For other sentences,
  // reuse the same clause generator as a source of alternatives.
  const size_t split = mode_ == Mode::Full && reading.find_first_of(U"、。！？・") == std::u32string::npos
                           ? ClauseBoundary(reading) : 0;
  auto clauses = rows;
  if (split && !LowMemory()) AddClause(request, reading, clauses, split, false);
  std::vector<std::string> heads;
  for (size_t i = 0; i < std::min<size_t>(3, rows.size()); ++i)
    if (!rows[i].is_raw) heads.push_back(rows[i].output_text);
  if (split) {
    // Reserve room for both experts. A clause promotion must remain visible even
    // when the compact engine fills its list with close spelling variants.
    size_t added = 0;
    for (const auto& candidate : clauses) {
      if (candidate.is_raw || std::any_of(rows.begin(), rows.end(), [&](const auto& original) {
            return original.output_text == candidate.output_text;
          })) continue;
      heads.push_back(candidate.output_text);
      if (++added == 2) break;
    }
  }
  Merge(request, iroha::Utf32ToUtf8(reading), heads, expanded, rows);
}

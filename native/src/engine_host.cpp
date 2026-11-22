#include <functional>
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include "ime_engine.h"
#include "user_dictionary.h"
#include "learning_store.h"
#include "file_revision.h"
#include "nlohmann/json.hpp"
#include "probe_refiner.h"
#include "router.h"
#include "iroha/unicode.h"

namespace {
std::u32string OkuriganaKey(const std::string& surface) {
  if (surface.size() < 6 || surface.size() > 36) return {};
  std::u32string key;
  for (const auto c : iroha::Utf8ToUtf32(surface)) {
    if ((U'一' <= c && c <= U'鿿') || c == U'々') key.push_back(c);
    else if (c < U'ぁ' || c > U'ゖ' || key.empty()) return {};
  }
  return key.size() >= 2 ? key : std::u32string{};
}
bool NounBoundary(std::string_view tail) {
  if (tail.empty()) return true;
  const auto first=static_cast<unsigned char>(tail.front());
  if (first<128) return std::ispunct(first) || std::isspace(first);
  if (tail.substr(0,6)==u8"にく" || tail.substr(0,6)==u8"がち" || tail.substr(0,6)==u8"がた") return false;
  constexpr std::string_view boundaries=u8"をがはにのもでとへ、。！？「」（）";
  return tail.size()>=3 && boundaries.find(tail.substr(0,3))!=boundaries.npos;
}
bool MentionsNoun(std::string_view context, bool left, std::string_view word) {
  constexpr size_t window = 384;
  const size_t start = left && context.size() > window ? context.size()-window : 0;
  const size_t end = left ? context.size() : std::min(context.size(),window);
  const auto nearby = context.substr(start,end-start);
  for (size_t pos=nearby.find(word); pos!=nearby.npos; pos=nearby.find(word,pos+word.size())) {
    const auto tail=context.substr(start+pos+word.size());
    if (NounBoundary(tail)) return true;
  }
  return false;
}
void PreferDocumentSpelling(const ime::DecodeInput& request, std::vector<ime::Candidate>& rows) {
  if ((request.left_context.empty() && request.right_context.empty()) || rows.size()<2 ||
      request.field!="prose" || request.phase!="end_of_phrase" || request.raw_text.size()>40 ||
      request.shallow_search || request.dictionary_preview || rows.front().is_raw ||
      !NounBoundary(request.right_context)) return;
  const auto key=OkuriganaKey(rows.front().output_text);
  if (key.empty()) return;
  size_t found=rows.size();
  for (size_t i=0;i<std::min<size_t>(8,rows.size());++i) {
    const auto& word=rows[i].output_text;
    if (rows[i].is_raw || (i && OkuriganaKey(word)!=key)) continue;
    if (MentionsNoun(request.left_context,true,word) || MentionsNoun(request.right_context,false,word)) {
      // Mixed spelling in the surrounding text gives no unique preference.
      if (i==0 || found!=rows.size()) return;
      found=i;
    }
  }
  if (found!=rows.size()) std::rotate(rows.begin(),rows.begin()+found,rows.begin()+found+1);
}
}

static bool Transfer(HANDLE pipe, void* data, DWORD size, bool write) {
  char* p = static_cast<char*>(data);
  while (size) {
    DWORD n = 0;
    BOOL ok = write ? WriteFile(pipe, p, size, &n, nullptr) : ReadFile(pipe, p, size, &n, nullptr);
    if (!ok || !n) return false;
    p += n; size -= n;
  }
  return true;
}
int wmain(int argc, wchar_t** argv) {
  if ((argc != 4 && argc != 6) || wcscmp(argv[1], L"--pipe") || (argc == 6 && wcscmp(argv[4], L"--profile"))) return 2;
  HANDLE input = reinterpret_cast<HANDLE>(_wcstoui64(argv[2], nullptr, 10));
  HANDLE output = reinterpret_cast<HANDLE>(_wcstoui64(argv[3], nullptr, 10));
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  using nlohmann::json;
  ime::UserDictionary dictionary;
  auto dictionary_path = ime::UserDictionaryPath();
  if (argc == 6) dictionary_path = std::filesystem::path(argv[5]) / L"user_dictionary.tsv";
  ime::LearningStore learning(dictionary_path.parent_path());
  std::optional<ime::FileRevision> dictionary_revision;
  ime::UserDictionary public_dictionary;
  auto public_override = dictionary_path.parent_path() / L"public_dictionary.tsv";
  wchar_t executable_path[32768]{};
  const auto path_length=GetModuleFileNameW(nullptr,executable_path,32768);
  if(!path_length || path_length>=32768) return 6;
  auto public_bundled = std::filesystem::path(executable_path).parent_path() / L"public_dictionary.tsv";
  std::filesystem::path public_loaded_path;
  std::optional<ime::FileRevision> public_revision;
  const auto settings_path = dictionary_path.parent_path()/L"settings.json";
  auto settings_revision = ime::ReadFileRevision(settings_path);
  auto configured = learning.model();
  std::optional<HybridRouter> router;
  auto configure = [&]() {
    router.emplace(configured,public_bundled.parent_path()/L"models");
    ime::AzookeySelectStartupModel(router->enabled()?ime::ConversionModel::CompactExtendedSearch:configured);
  };
  ProbeRefiner refiner;
  bool cache_under_pressure = false;
  for (;;) {
    uint32_t size = 0;
    if (!Transfer(input, &size, sizeof(size), false)) return 0;
    if (!size || size > 16384) return 3;
    std::string data(size, '\0');
    if (!Transfer(input, data.data(), size, false)) return 0;
    HANDLE reply_event = nullptr;
    auto write_response = [&](char* bytes, uint32_t length) {
      if (!Transfer(output, &length, sizeof(length), true)) return false;
      // Large frames may exceed the anonymous pipe's capacity. Notify as each
      // chunk becomes readable so the writer does not need a fallback timer.
      while (length) {
        DWORD chunk=(std::min<uint32_t>)(length, 16384);
        if (!Transfer(output, bytes, chunk, true)) return false;
        if (reply_event) SetEvent(reply_event);
        bytes+=chunk;
        length-=chunk;
      }
      return true;
    };
    try {
      auto j = json::parse(data);
      reply_event = reinterpret_cast<HANDLE>(j.value("reply_event", uintptr_t{}));
      if (j.at("version").get<int>() != 1) return 4;
      if (!router) {
        const int budget=j.value("cpu_threads",4);
        if (budget>0 && budget<4) {
          const char* value=std::getenv("IROHA_THREADS");
          const int current=value ? std::atoi(value) : 0;
          if (current<=0 || current>budget) _putenv_s("IROHA_THREADS",std::to_string(budget).c_str());
        }
        configure();
      }
      const auto revision = ime::ReadFileRevision(settings_path);
      if (revision != settings_revision) {
        settings_revision = revision;
        const auto selected = learning.model();
        if (selected != configured) { configured = selected; configure(); }
      }
      auto& hybrid = *router;
      ime::DecodeInput request;
      request.raw_text = j.at("raw").get<std::string>(); request.left_context = j.value("left", "");
      request.right_context = j.value("right", ""); request.field = j.value("field", "prose");
      request.phase = j.value("phase", "end_of_phrase");
      request.punctuation_completion = j.value("punctuation_completion", false);
      request.dictionary_preview = j.value("dictionary_preview", false);
      request.shallow_search = j.value("shallow_search", false);
      request.candidate_limit = (std::clamp)(j.value("limit", 8), 1, 16);
      const bool learning_request = j.value("operation", "decode") == "learn";
      const bool subscribed = !learning_request && j.value("allow_preview", false) &&
          request.phase == "end_of_phrase" && !request.dictionary_preview && !request.shallow_search;
      hybrid.SetCancelEvent(subscribed ? reinterpret_cast<HANDLE>(j.value("cancel_event", uintptr_t{0})) : nullptr);
      if (learning_request) {
        const auto chosen = j.at("chosen").get<std::string>();
        const auto previous = j.value("previous", "");
        const bool explicit_choice = j.value("explicit", true), literal = j.value("f10", false);
        WordPreference word;
        if (hybrid.enabled() && explicit_choice && !literal && !previous.empty() && previous.size() <= 256 &&
            chosen.size() <= 256 && request.raw_text.size() <= 64 && !hybrid.LowMemory() && learning.enabled()) {
          auto commit = request;
          commit.phase = "end_of_phrase";
          const auto reading = hybrid.Reading(commit);
          if (!reading.empty()) {
            ReadingPaths paths(iroha::Utf32ToUtf8(reading),commit);
            word = paths.ChangedWord(previous,chosen);
          }
        }
        const auto alternate_n = j.value("alternate_n", 0u);
        const bool saved = learning.Record(request,chosen,explicit_choice,literal,word,alternate_n);
        data = json({{"learned", saved}}).dump(); size = static_cast<uint32_t>(data.size());
        if (!write_response(data.data(), size)) return 0;
        continue;
      }
      hybrid.CheckCancelled();
      std::error_code ec;
      auto stamp = ime::ReadFileRevision(dictionary_path);
      if (stamp && stamp != dictionary_revision) {
        std::string error;
        if (dictionary.Load(dictionary_path, &error)) {
          // 上流の動的辞書は線形検索のため、大量の登録語はC++側の索引で検索する。
          ime::AzookeySetUserDictionary(dictionary.json());
          dictionary_revision = stamp;
        }
      } else if (!stamp && dictionary.size() && !std::filesystem::exists(dictionary_path, ec) && !ec) {
        dictionary = ime::UserDictionary(); ime::AzookeySetUserDictionary("[]"); dictionary_revision.reset();
      }
      auto public_path = ime::PublicDictionaryPath(public_bundled, public_override);
      auto public_stamp = ime::ReadFileRevision(public_path);
      if (public_stamp && (public_path != public_loaded_path || public_stamp != public_revision)) {
        std::string error;
        if (public_dictionary.Load(public_path, &error, true)) {
          public_loaded_path = public_path;
          public_revision = public_stamp;
        } else if (public_loaded_path.empty() && public_path != public_bundled) {
          if (public_dictionary.Load(public_bundled, &error, true)) public_loaded_path = public_bundled;
        }
      }
      const auto displayLimit = static_cast<size_t>(request.candidate_limit);
      // The widget controls how many alternatives to show, not the evidence used to select the first.
      if (hybrid.enabled() && request.phase != "typing" && request.field == "prose" &&
          !request.shallow_search && !request.dictionary_preview && !hybrid.LowMemory())
        request.candidate_limit = 16;
      const bool low = hybrid.LowMemory();
      if (low != cache_under_pressure) {
        ime::AzookeySetCachePressure(low);
        cache_under_pressure = low;
      }
      if (low) hybrid.ReleaseModels();
      const auto hybrid_reading=hybrid.Reading(request);
      auto kana=iroha::Utf32ToUtf8(hybrid_reading);
      if (hybrid.enabled() && kana.empty() && !request.right_context.empty() && request.phase!="typing" &&
          request.field=="prose" && request.raw_text.size()<=96 &&
          request.raw_text.find_first_not_of("abcdefghijklmnopqrstuvwxyz'-")==std::string::npos &&
          !ime::ConvertRomajiChecked(request.raw_text,&kana)) kana.clear();
      std::vector<WordPreference> words;
      bool has_personal_part = false;
      bool capture_parts = false;
      if (!kana.empty() && !hybrid.LowMemory()) {
        words=dictionary.UniqueWordsIn(kana, &has_personal_part);
        auto learned=learning.ExplicitWordsIn(request,kana);
        words.insert(words.end(),learned.begin(),learned.end());
        capture_parts = hybrid.verifies_dictionary() || !words.empty();
      }
      std::string latin_prefix, suffix_reading;
      const bool search_models = !request.dictionary_preview && !request.shallow_search;
      const bool verify_suffix = search_models && hybrid.verifies_dictionary() && request.field == "prose" &&
          request.phase != "typing" && request.right_context.empty() && hybrid_reading.empty() &&
          ime::SplitShortLatinSuffix(request.raw_text, &latin_prefix, &suffix_reading);
      const bool dictionary_first=hybrid.enabled() && (verify_suffix || request.phase=="typing" || !hybrid_reading.empty() || hybrid.LowMemory());
      ime::AzookeySetDictionaryPreview(dictionary_first || !search_models);
      std::vector<ime::Candidate> result;
      auto apply_preferences = [&](std::vector<ime::Candidate>& rows, bool protect_numbers = false, bool final = false) {
        ime::ApplyStandalonePreference(request,rows);
        public_dictionary.Apply(request, &rows, true, final);
        if (final && words.empty() && !has_personal_part &&
            (!dictionary.size() || (!kana.empty() && !hybrid.LowMemory()))) PreferDocumentSpelling(request,rows);
        learning.Apply(request, &rows);
        // An exact personal entry may intentionally omit or replace a number.
        // For partial matches the ordinary digit-preservation rule still applies.
        if (protect_numbers && dictionary.HasPersonalEntry(request.raw_text)) {
          ProtectNumericLiterals(request,rows,!hybrid.LowMemory());
          protect_numbers = false;
        }
        dictionary.Apply(request, &rows);
        if (protect_numbers) ProtectNumericLiterals(request,rows,!hybrid.LowMemory());
      };
      std::function<void(const std::string&)> preview;
      std::string last_surface;
      unsigned previews_left = std::min(j.value("max_previews", 2u), 3u);
      if (hybrid.enabled() && search_models && j.value("allow_preview",false) && request.phase=="end_of_phrase" && request.field=="prose" &&
          !request.shallow_search && !kana.empty() && words.empty() &&
          request.raw_text.find_first_of("0123456789") == std::string::npos) {
        preview = [&](const std::string& surface) {
          hybrid.CheckCancelled();
          if (!previews_left || surface.empty() || surface == last_surface) return;
          auto interim = result;
          interim.erase(std::remove_if(interim.begin(),interim.end(),[&](const auto& c){
            return c.output_text == surface;
          }),interim.end());
          interim.insert(interim.begin(),ime::Candidate{surface,kana,false});
          apply_preferences(interim);
          if (interim.empty() || interim.front().is_raw || interim.front().output_text == last_surface) return;
          --previews_left;
          last_surface = interim.front().output_text;
          json packet={{"preview",json::array({{{"text",last_surface},{"reading",kana},{"raw",false}}})}};
          auto bytes=packet.dump(); uint32_t length=static_cast<uint32_t>(bytes.size());
          if (!write_response(bytes.data(),length))
            throw std::runtime_error("preview pipe closed");
        };
      }
      // A subscribed long prose request can display Jinen while the dictionary
      // prepares its alternatives. Keep the result local to this request.
      std::optional<HybridRouter::PrimaryResult> primary;
      if (preview && previews_left && hybrid.verifies_dictionary() && hybrid_reading.size() >= 12 &&
          hybrid_reading.size() <= 96 && !hybrid.LowMemory()) {
        primary = hybrid.Primary(hybrid_reading,iroha::Utf8ToUtf32(request.left_context));
        if (primary->terminated && !primary->text.empty()) preview(iroha::Utf32ToUtf8(primary->text));
      }
      std::optional<ime::AzookeyPartCapture> capture;
      hybrid.CheckCancelled();
      if (capture_parts) capture.emplace(kana,request.left_context,request.right_context);
      result = ime::Decode(request);
      hybrid.CheckCancelled();
      if (verify_suffix && !hybrid.LowMemory()) {
        const bool confirmed = !result.empty() && result.front().output_text.compare(0,latin_prefix.size(),latin_prefix)==0 &&
            hybrid.ConfirmsSuffix(request.left_context+latin_prefix,suffix_reading,result.front().output_text.substr(latin_prefix.size()));
        if (!confirmed) {
          hybrid.CheckCancelled();
          ime::AzookeySetDictionaryPreview(false);
          result = ime::Decode(request);
        }
      }
      if (hybrid.enabled()) {
        hybrid.CheckCancelled();
        ReadingPaths paths(kana,request);
        if (search_models) {
          hybrid.Refine(request,hybrid_reading,result,paths,preview,primary ? &*primary : nullptr);
        }
        if (!words.empty() && !hybrid.LowMemory()) paths.Protect(words,result);
      } else if (search_models) refiner.Apply(request, &result);
      apply_preferences(result, hybrid.enabled() && request.raw_text.find_first_of("0123456789") != std::string::npos, true);
      if (result.size() > displayLimit) {
        auto literal = std::find_if(result.begin() + displayLimit, result.end(), [](const auto& row) { return row.is_raw; });
        if (displayLimit > 1 && literal != result.end()) result[displayLimit - 1] = std::move(*literal);
        result.resize(displayLimit);
      }
      json response = json::array();
      for (const auto& c : result) response.push_back({{"text",c.output_text},{"reading",c.reading_text},{"raw",c.is_raw}});
      data = response.dump(); size = static_cast<uint32_t>(data.size());
      if (size > 1024*1024 || !write_response(data.data(), size)) return 0;
    } catch (const HybridRouter::Cancelled&) {
      router->ReleaseModels();
      // A final array completes this frame even if previews were already sent.
      // The sender drains it before reusing the event for another edit.
      char empty[] = "[]";
      size = 2;
      if (!write_response(empty, size)) return 0;
    } catch (...) { return 5; }
  }
}

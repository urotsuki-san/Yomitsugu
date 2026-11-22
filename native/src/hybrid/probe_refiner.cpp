#include "probe_refiner.h"
#include "iroha/unicode.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace {
constexpr std::u32string_view punctuation = U"、。！？「」（）";
std::u32string Marks(const std::u32string& s) {
    std::u32string marks;
    for (auto c : s) if (punctuation.find(c) != punctuation.npos) marks.push_back(c);
    return marks;
}
bool RawIsProse(const std::string& raw) {
    constexpr std::u32string_view extra = U"'.,!?()[]（）「」、。！？ー-";
    for (char32_t c : iroha::Utf8ToUtf32(raw))
        if (!(c >= U'a' && c <= U'z') && extra.find(c) == extra.npos) return false;
    return true;
}
void AddShallowCandidates(ime::DecodeInput request, std::vector<ime::Candidate>& deep) {
    request.shallow_search = true;
    const auto shallow = ime::Decode(request);
    std::vector<ime::Candidate> merged;
    auto append = [&](const ime::Candidate& c) {
        if (!c.is_raw && c.output_text!=request.raw_text && std::none_of(merged.begin(),merged.end(),[&](const auto& v){return v.output_text==c.output_text;}))
            merged.push_back(c);
    };
    for (size_t i=0; i<deep.size() || i<shallow.size(); ++i) {
        if (i<deep.size()) append(deep[i]);
        if (i<shallow.size()) append(shallow[i]);
    }
    auto literal = std::find_if(deep.begin(),deep.end(),[&](const auto& c){return c.is_raw || c.output_text==request.raw_text;});
    const size_t slots = request.candidate_limit - static_cast<int>(literal!=deep.end());
    if (merged.size()>slots) merged.resize(slots);
    if (literal!=deep.end()) merged.push_back(*literal);
    deep.swap(merged);
}
}
ProbeRefiner::ProbeRefiner() {
    if (const auto* path = _wgetenv(L"YOMITSUGU_PROBE_GGUF")) path_ = std::filesystem::path(path).u8string();
    const auto* policy = std::getenv("YOMITSUGU_PROBE_POLICY");
    prefer_ = policy && std::string_view(policy) == "direct";
    diverse_ = policy && std::string_view(policy) == "diverse";
}
void ProbeRefiner::Apply(const ime::DecodeInput& req, std::vector<ime::Candidate>* candidates) {
    auto& rows = *candidates;
    if ((!diverse_ && path_.empty()) || req.phase=="typing" || req.field!="prose" || (!diverse_ && !req.right_context.empty()) ||
        rows.empty() || rows.front().is_raw || rows.front().output_text==req.raw_text || !RawIsProse(req.raw_text)) return;
    std::string kana;
    if (!ime::ConvertRomajiChecked(req.raw_text, &kana)) return;
    auto reading = iroha::Utf8ToUtf32(kana);
    if (reading.empty() || std::any_of(reading.begin(), reading.end(), [](char32_t c) {
        return !(c >= U'ぁ' && c <= U'ゖ') && c != U'ー' && punctuation.find(c)==punctuation.npos;
    })) return;
    if (diverse_) {
        if (reading.size()<=12 && req.candidate_limit>1) AddShallowCandidates(req,rows);
        return;
    }
    std::string error;
    if (!model_) {
        iroha::ZenzEngine::Options options;
        options.jinenNfkc = std::getenv("YOMITSUGU_PROBE_NFKC") != nullptr;
        auto model = std::make_unique<iroha::ZenzEngine>(path_, options);
        if (!model->Prewarm(&error)) { std::cerr << error << '\n'; path_.clear(); return; }
        model_ = std::move(model);
    }
    std::vector<std::u32string> surfaces;
    if (!model_->Convert(reading, iroha::Utf8ToUtf32(req.left_context), 1, &surfaces, &error) || surfaces.empty()) return;
    const auto& surface = surfaces.front();
    if (surface.empty() || surface.size()>(std::max)(size_t(12),2*reading.size()) || Marks(surface)!=Marks(reading) ||
        std::any_of(surface.begin(),surface.end(),[](char32_t c){return c<32 || c==127;})) return;
    const auto choice = iroha::Utf32ToUtf8(surface);
    if (choice==req.raw_text) return;
    auto found = std::find_if(rows.begin(),rows.end(),[&](const auto& c){return c.output_text==choice;});
    if (found!=rows.end()) { std::rotate(rows.begin(),found,found+1); return; }
    const auto at = prefer_ ? 0u : 1u;
    if (at >= static_cast<size_t>(req.candidate_limit)) return;
    ime::Candidate candidate;
    candidate.output_text=choice;
    candidate.reading_text=kana;
    candidate.total=rows.front().total;
    rows.insert(rows.begin()+at,std::move(candidate));
    if (rows.size()>static_cast<size_t>(req.candidate_limit)) {
        const bool preserve = rows.back().is_raw || rows.back().output_text==req.raw_text;
        rows.erase(preserve && rows.size()>1 ? rows.end()-2 : rows.end()-1);
    }
}

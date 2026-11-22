#include "iroha/zenz_engine.h"
#include "iroha/inference_timer.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <deque>
#include <limits>
#include <optional>

#include "iroha/kana.h"
#include "iroha/reading_constraint.h"
#include "iroha/unicode.h"

#include "llama.h"
#include <windows.h>

namespace iroha {

namespace {

constexpr float kNegativeInfinity = -std::numeric_limits<float>::infinity();

// zenzの特殊トークン（私用領域 U+EE00-U+EE0F）のUTF-8は EE B8 80-8F の3バイト
bool EndsWithZenzSpecial(const std::string& bytes) {
    const size_t n = bytes.size();
    if (n < 3) return false;
    return static_cast<unsigned char>(bytes[n - 3]) == 0xEE &&
           static_cast<unsigned char>(bytes[n - 2]) == 0xB8 &&
           static_cast<unsigned char>(bytes[n - 1]) >= 0x80 &&
           static_cast<unsigned char>(bytes[n - 1]) <= 0x8F;
}

// IROHA_KV_REUSE=0 で、前回の計算の再利用（同じプロンプトの状態とlogits、デコーダ専用モデルの
// KVキャッシュの共通プレフィックス、エンコーダ・デコーダ型のエンコーダ出力）を無効にし、
// 毎回プロンプト全体を評価し直す（A/B比較・切り分け用）。
// 同じプロンプトの再利用とエンコーダ出力の再利用は結果を変えない。読みが伸びたときの共通プレフィックスの
// 再利用は、一括評価と値がわずかに変わり、僅差の候補が入れ替わることがある（出力の妥当性は同等）
bool ReuseEnabled() {
    static const bool enabled = [] {
        const char* env = std::getenv("IROHA_KV_REUSE");
        return !(env && std::atoi(env) == 0);
    }();
    return enabled;
}

// log Σ exp(logits)（logitを対数確率に直す正規化項）
float LogSumExp(const float* logits, int count) {
    float maxLogit = kNegativeInfinity;
    for (int index = 0; index < count; ++index) maxLogit = std::max(maxLogit, logits[index]);
    float sum = 0;
    for (int index = 0; index < count; ++index) sum += std::exp(logits[index] - maxLogit);
    return maxLogit + std::log(sum);
}

// The terminator IDs are in vocabulary order, preserving the original sum.
float TerminatorLogSumExp(const float* logits, const std::vector<llama_token>& ids) {
    float maxLogit = kNegativeInfinity;
    for (llama_token id : ids) maxLogit = std::max(maxLogit, logits[id]);
    if (!(maxLogit > kNegativeInfinity)) return kNegativeInfinity;
    float sum = 0;
    for (llama_token id : ids) sum += std::exp(logits[id] - maxLogit);
    return maxLogit + std::log(sum);
}

bool Tokenize(const llama_vocab* vocab, const std::u32string& text, bool addSpecial,
              std::vector<llama_token>* tokens, std::string* error) {
    const std::string utf8 = Utf32ToUtf8(text);
    tokens->assign(utf8.size() + 8, 0);
    const int count = llama_tokenize(vocab, utf8.data(),
                                     static_cast<int32_t>(utf8.size()), tokens->data(),
                                     static_cast<int32_t>(tokens->size()), addSpecial,
                                     /*parse_special=*/true);
    if (count < 0) {
        if (error) *error = "推論に失敗: トークン化に失敗";
        return false;
    }
    tokens->resize(count);
    return true;
}

bool Decode(llama_context* ctx, llama_token* tokens, int count, std::string* error) {
    const int result = llama_decode(ctx, llama_batch_get_one(tokens, count));
    if (result != 0) {
        if (error) *error = result == 2 ? ZenzEngine::kCancelledError : "推論に失敗: llama_decode=" + std::to_string(result);
        return false;
    }
    return true;
}

// Most logits are below the current runner-up before the reading constraint
// sees them. Resolve only inspected tokens, even when an expert is reloaded.
class TokenPieces {
public:
    enum : uint32_t { unseen, invalid, terminator, firstPiece };
    void Init(const llama_vocab* vocab) {
        vocab_ = vocab;
        slots_.resize(llama_vocab_n_tokens(vocab));
    }
    uint32_t Slot(llama_token token) {
        auto& slot = slots_[token];
        if (slot != unseen) return slot;
        if (llama_vocab_is_eog(vocab_, token)) return slot = terminator;
        char buffer[128];
        const int n = llama_token_to_piece(vocab_, token, buffer, sizeof(buffer), 0, true);
        if (n <= 0) return slot = invalid;
        auto text = Utf8ToUtf32Strict(std::string_view(buffer,n));
        if (!text || text->empty()) return slot = invalid;
        if (std::any_of(text->begin(),text->end(),[](char32_t c) { return c >= 0xEE00 && c <= 0xEE0F; }))
            return slot = terminator;
        if (llama_vocab_get_attr(vocab_,token) & (LLAMA_TOKEN_ATTR_CONTROL | LLAMA_TOKEN_ATTR_UNKNOWN))
            return slot = invalid;
        pieces_.push_back(std::move(*text));
        return slot = static_cast<uint32_t>(pieces_.size()-1) + firstPiece;
    }
    const std::u32string& Text(uint32_t slot) const { return pieces_[slot-firstPiece]; }
    const std::vector<llama_token>& Terminators() {
        if (terminatorsReady_) return terminators_;
        // Most pieces never participate in decoding. Looking for the marker in bytes
        // first avoids materializing every vocabulary entry as a UTF-32 string.
        for (llama_token id = 0; static_cast<size_t>(id) < slots_.size(); ++id) {
            if (slots_[id] == unseen && !llama_vocab_is_eog(vocab_, id)) {
                char buffer[128];
                const int size = llama_token_to_piece(vocab_, id, buffer, sizeof(buffer), 0, true);
                if (size <= 0 || std::string_view(buffer, size).find("\xee\xb8") == std::string_view::npos)
                    continue;
            }
            if (Slot(id) == terminator) terminators_.push_back(id);
        }
        terminatorsReady_ = true;
        return terminators_;
    }
private:
    const llama_vocab* vocab_ = nullptr;
    std::vector<uint32_t> slots_;
    std::deque<std::u32string> pieces_;
    std::vector<llama_token> terminators_;
    bool terminatorsReady_ = false;
};

} // namespace

// llama.cppのリソース一式。デストラクタで確実に解放する
struct ZenzEngine::Runtime {
    struct AbortProbe {
        ComputeCancelCheck check = nullptr;
        void* data = nullptr;
        // ggml-cpu evaluates the callback only on worker zero during synchronous
        // graph execution. Limit Win32 event queries to one per millisecond.
        std::chrono::steady_clock::time_point next{};
        bool Poll() noexcept {
            const auto now = std::chrono::steady_clock::now();
            if (now < next) return false;
            next = now + std::chrono::milliseconds(1);
            return check(data);
        }
    } abort;
    llama_model* model = nullptr;
    llama_context* context = nullptr;
    const llama_vocab* vocab = nullptr;
    // 追加学習で作った LoRA アダプタ（adapterPath 指定時。context に適用済み）
    llama_adapter_lora* adapter = nullptr;
    // GPU に載せると指定した層の数（RuntimeInfo の実行先に使う）
    int gpuLayers = 0;
    TokenPieces pieces;
    // エンコーダ・デコーダ型か（T5系）。真ならプロンプトは llama_encode で読み、
    // デコーダは decoderStartToken から生成する
    bool isEncoderDecoder = false;
    llama_token decoderStartToken = 0;

    // 現在KVキャッシュ（デコーダ側）に載っているトークン列
    std::vector<llama_token> cached;
    // エンコーダ・デコーダ型: 現在エンコーダ出力を持っているプロンプト
    std::vector<llama_token> encoded;
    bool hasEncoded = false;

    // 直前に評価したプロンプトと、その直後のlogits。同じプロンプトでもう一度始めるとき
    // （n-bestの分岐・変換のあとの採点）は、KVキャッシュをプロンプトだけの状態に戻してこれを使う
    bool primedValid = false;
    std::vector<llama_token> primedPrompt;
    // プロンプトだけの状態でデコーダ側に載っているトークン数（デコーダ専用モデルならプロンプトの長さ、
    // エンコーダ・デコーダ型なら開始トークンの 1）
    size_t primedCount = 0;
    std::vector<float> primedLogits;

    ~Runtime() {
        if (context) llama_free(context);
        // アダプタはモデルに属するので、モデルより先に解放する
        if (adapter) llama_adapter_lora_free(adapter);
        if (model) llama_model_free(model);
    }
};

struct ZenzEngine::FirstToken {
    llama_token token;
    float logProb;
    int branchStep = -1;
    const Generated* greedy = nullptr;
};

struct ZenzEngine::Generated {
    struct Decision {
        float margin;
        llama_token token;
        float cumulativeLogProb;
        uint64_t mask;
        bool constrained;
    };
    bool recordBranches = false;
    bool terminated = false;
    std::vector<Decision> decisions;
    std::u32string text;
    // 先頭トークン・各トークン・終端まで含めた系列の対数確率
    float logProb = 0;
    // text の文字ごとの自信度
    std::vector<CharacterConfidence> confidences;
};

namespace {

// トークン列（バイト断片）を出力文字列の文字ごとの自信度に対応づける（macOS版 ConfidenceAligner）。
// バイト単位のトークンが多バイト文字の途中で切れていても、その文字が完成した時点で、関わったトークン全てをまとめて割り当てる
class ConfidenceAligner {
public:
    using Confidence = ZenzEngine::CharacterConfidence;

    void Append(std::string_view piece, const Confidence& confidence) {
        bytes_.append(piece);
        pending_ = pending_ ? Merged(*pending_, confidence) : confidence;
        const auto text = Utf8ToUtf32Strict(bytes_);
        if (!text) return;
        const size_t count = GraphemeCount(*text);
        if (count <= assigned_ || !pending_) return;
        confidences_.insert(confidences_.end(), count - assigned_, *pending_);
        assigned_ = count;
        pending_.reset();
    }

    // 最終的な出力文字列（不正バイトの除去・前後の空白除去を経たもの）に長さをそろえて返す
    std::vector<Confidence> Aligned(const std::u32string& untrimmed, const std::u32string& trimmed) const {
        std::vector<Confidence> result = confidences_;
        const size_t untrimmedCount = GraphemeCount(untrimmed);
        if (result.size() < untrimmedCount) {
            // 途中に不正バイトがあって割り当てが止まった: 残りを保留中の値（無ければ末尾の値）で埋める
            const Confidence filler = pending_ ? *pending_
                                      : !result.empty()
                                          ? result.back()
                                          : Confidence{0, std::numeric_limits<float>::infinity(), false};
            result.insert(result.end(), untrimmedCount - result.size(), filler);
        }
        size_t leading = 0;
        for (const std::u32string& grapheme : SplitGraphemes(untrimmed)) {
            if (TrimWhitespace(grapheme).empty()) {
                ++leading;
            } else {
                break;
            }
        }
        const size_t take = GraphemeCount(trimmed);
        if (leading >= result.size()) return {};
        return std::vector<Confidence>(result.begin() + static_cast<std::ptrdiff_t>(leading),
                                       result.begin() + static_cast<std::ptrdiff_t>(std::min(result.size(), leading + take)));
    }

private:
    static Confidence Merged(const Confidence& a, const Confidence& b) {
        return Confidence{std::min(a.logProb, b.logProb), std::min(a.margin, b.margin), a.relaxed || b.relaxed};
    }

    std::string bytes_;
    std::optional<Confidence> pending_;
    size_t assigned_ = 0;
    std::vector<Confidence> confidences_;
};

} // namespace

ZenzEngine::ZenzEngine(std::string modelPath) : ZenzEngine(std::move(modelPath), Options()) {}
ZenzEngine::ZenzEngine(std::string modelPath, Options options)
    : modelPath_(std::move(modelPath)), options_(std::move(options)) {}
ZenzEngine::~ZenzEngine() = default;

bool ZenzEngine::Prewarm(std::string* error) {
    return EnsureLoaded(error);
}

bool ZenzEngine::PrewarmForInput(const std::u32string& reading, const std::u32string& left,
                                 std::string* error) {
    const auto prompt = ModelPrompt(reading, left);
    return EnsureLoaded(error, &prompt);
}

std::u32string ZenzEngine::BuildPrompt(const std::u32string& reading,
                                       const std::u32string& leftContext,
                                       size_t maxContextLength) {
    // zenz-v3の特殊トークン（私用領域）
    constexpr char32_t kContextMarker = 0xEE02; // 左文脈の開始
    constexpr char32_t kReadingMarker = 0xEE00; // 読みの開始
    constexpr char32_t kOutputMarker = 0xEE01;  // 変換結果の開始
    std::u32string prompt;
    if (!leftContext.empty()) {
        prompt += kContextMarker;
        // 末尾の maxContextLength 文字（Swift の Character 単位）
        const std::vector<std::u32string> characters = SplitGraphemes(leftContext);
        const size_t start =
            characters.size() > maxContextLength ? characters.size() - maxContextLength : 0;
        for (size_t i = start; i < characters.size(); ++i) prompt += characters[i];
    }
    prompt += kReadingMarker;
    prompt += HiraganaToKatakana(reading);
    prompt += kOutputMarker;
    return prompt;
}

std::u32string ZenzEngine::ModelPrompt(const std::u32string& reading, const std::u32string& left) const {
    if (!options_.jinenNfkc) return BuildPrompt(reading, left, kMaxContextLength);
    const auto prompt = BuildPrompt(reading, left, 64);
    const auto text = Utf32ToUtf16(prompt);
    const int n = NormalizeString(NormalizationKC, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (n <= 0) return prompt;
    std::wstring output(n, L'\0');
    const int length = NormalizeString(NormalizationKC, text.data(), static_cast<int>(text.size()), output.data(), n);
    if (length <= 0) return prompt;
    output.resize(length);
    return Utf16ToUtf32(output);
}

std::u32string ZenzEngine::DecodeUtf8DroppingFragments(const std::string& bytes) {
    // 末尾の断片は最大3バイト（4バイト文字の先頭3バイト）
    std::string_view view(bytes);
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (auto text = Utf8ToUtf32Strict(view)) return *text;
        if (view.empty()) break;
        view.remove_suffix(1);
    }
    // 途中にも不正なバイトがある: 置換文字に落としてから取り除く
    std::u32string text = Utf8ToUtf32(bytes);
    text.erase(std::remove(text.begin(), text.end(), char32_t(0xFFFD)), text.end());
    return text;
}

// 読み込みにかかった時間をデバッグ表示のために測る（読み込み済みなら測らない）
bool ZenzEngine::EnsureLoadedMeasured(std::string* error) {
    if (runtime_) return true;
    const inference_timer::Measure measure(inference_timer::Measure::Kind::ModelLoad);
    return EnsureLoaded(error);
}

bool ZenzEngine::GetRuntimeInfo(RuntimeInfo* info) const {
    if (!runtime_ || !runtime_->context || !info) return false;
    info->device = "CPU";
    if (runtime_->gpuLayers != 0) {
        for (size_t index = 0; index < ggml_backend_dev_count(); ++index) {
            ggml_backend_dev_t device = ggml_backend_dev_get(index);
            if (!device) continue;
            const enum ggml_backend_dev_type type = ggml_backend_dev_type(device);
            if (type != GGML_BACKEND_DEVICE_TYPE_GPU && type != GGML_BACKEND_DEVICE_TYPE_IGPU) continue;
            info->device = std::string(ggml_backend_dev_name(device)) + ": " + ggml_backend_dev_description(device);
            break;
        }
    }
    info->threads = static_cast<int>(llama_n_threads(runtime_->context));
    return true;
}

bool ZenzEngine::EnsureLoaded(std::string* error, const std::u32string* firstPrompt) {
    if (runtime_) return true;

    const std::wstring widePath = Utf32ToUtf16(Utf8ToUtf32(modelPath_));
    if (!std::filesystem::exists(std::filesystem::path(widePath))) {
        if (error) *error = "モデルファイルが見つかりません: " + modelPath_;
        return false;
    }

    llama_backend_init();
    llama_model_params modelParams = llama_model_default_params();
    // CPUビルドでは無視されるが、GPU対応ビルドに切り替えたときのためmacOS版に合わせる。
    // IROHA_GPU_LAYERS=0 でCPU推論に切替可能（計測用）
    if (const char* env = std::getenv("IROHA_GPU_LAYERS")) {
        modelParams.n_gpu_layers = std::atoi(env);
    } else {
        modelParams.n_gpu_layers = 99;
    }

    auto runtime = std::make_unique<Runtime>();
    runtime->gpuLayers = modelParams.n_gpu_layers;
    runtime->model = llama_model_load_from_file(modelPath_.c_str(), modelParams);
    if (!runtime->model) {
        if (error) *error = "モデルの読み込みに失敗: " + modelPath_;
        return false;
    }

    // 系列は 1 つだけ使う（macOS版は採点のために 32 系列を確保する。Score のコメント参照）
    llama_context_params contextParams = llama_context_default_params();
    contextParams.n_ctx = 1024;
    contextParams.n_batch = 1024;
    // Decoder-only prompts can be processed in smaller physical batches while
    // keeping the same logical prompt length and the full context capacity.
    if (llama_model_has_encoder(runtime->model) && firstPrompt) {
        std::vector<llama_token> tokens;
        const auto* vocab = llama_model_get_vocab(runtime->model);
        if (!vocab || !Tokenize(vocab, *firstPrompt, false, &tokens, error)) return false;
        const size_t required = tokens.size() + 1; // TokenizePrompt appends EOS to encoder input.
        if (required > contextParams.n_batch) {
            if (error) *error = kEncoderBatchTooSmall;
            return false;
        }
        unsigned capacity = 32;
        while (capacity < required) capacity *= 2;
        contextParams.n_ubatch = capacity;
        if (options_.decoderCapacity) {
            contextParams.n_ctx = std::max(capacity, options_.decoderCapacity);
            contextParams.n_batch = capacity;
        }
    } else if (llama_model_has_encoder(runtime->model) && options_.encoderBatch) {
        contextParams.n_ubatch = options_.encoderBatch;
    } else if (!llama_model_has_encoder(runtime->model)) {
        if (options_.decoderCapacity) {
            contextParams.n_ctx = options_.decoderCapacity;
            contextParams.n_batch = std::min(contextParams.n_ctx, contextParams.n_batch);
        }
        if (const char* env = std::getenv("YOMITSUGU_PROBE_UBATCH")) {
            const int batch = std::atoi(env);
            if (batch >= 16 && batch <= 512) contextParams.n_ubatch = batch;
        }
    }
    contextParams.n_seq_max = 1;
    // IROHA_THREADS で推論のスレッド数を変える（計測用。既定は llama.cpp の既定値）
    if (const char* env = std::getenv("IROHA_THREADS")) {
        const int threads = std::atoi(env);
        if (threads > 0) {
            contextParams.n_threads = threads;
            contextParams.n_threads_batch = threads;
        }
    }

    runtime->context = llama_init_from_model(runtime->model, contextParams);
    if (!runtime->context) {
        if (error) *error = "モデルの読み込みに失敗: コンテキストの作成に失敗";
        return false;
    }

    runtime->vocab = llama_model_get_vocab(runtime->model);
    if (!runtime->vocab) {
        if (error) *error = "モデルの読み込みに失敗: vocabの取得に失敗";
        return false;
    }
    const llama_vocab* vocab = runtime->vocab;

    // LoRA アダプタ（追加学習の成果）。読めないときは失敗にする: 黙ってベースで動くと
    // 「アダプタを使っているつもりで計測していた」事故になる
    if (!options_.adapterPath.empty()) {
        const std::wstring wideAdapter = Utf32ToUtf16(Utf8ToUtf32(options_.adapterPath));
        if (!std::filesystem::exists(std::filesystem::path(wideAdapter))) {
            if (error) *error = "モデルファイルが見つかりません: " + options_.adapterPath;
            return false;
        }
        runtime->adapter = llama_adapter_lora_init(runtime->model, options_.adapterPath.c_str());
        if (!runtime->adapter) {
            if (error) {
                *error = "モデルの読み込みに失敗: LoRAアダプタを読めません"
                         "（ベースモデルと合っていない可能性）: " + options_.adapterPath;
            }
            return false;
        }
        llama_adapter_lora* adapters[] = {runtime->adapter};
        float scales[] = {1.0f};
        if (llama_set_adapters_lora(runtime->context, adapters, 1, scales) != 0) {
            if (error) {
                *error = "モデルの読み込みに失敗: LoRAアダプタの適用に失敗: " + options_.adapterPath;
            }
            return false;
        }
    }

    runtime->pieces.Init(vocab);

    runtime->isEncoderDecoder =
        llama_model_has_encoder(runtime->model) && llama_model_has_decoder(runtime->model);
    runtime->decoderStartToken = llama_model_decoder_start_token(runtime->model);
    if (runtime->decoderStartToken == LLAMA_TOKEN_NULL) {
        runtime->decoderStartToken = llama_vocab_bos(vocab);
    }

    runtime_ = std::move(runtime);
    if (computeCancelCheck_) SetComputeCancelCheck(computeCancelCheck_, computeCancelData_);
    return true;
}

void ZenzEngine::SetComputeCancelCheck(ComputeCancelCheck check, void* data) {
    computeCancelCheck_ = check;
    computeCancelData_ = data;
    if (!runtime_ || runtime_->gpuLayers != 0) return;
    runtime_->abort = {check, data, {}};
    if (check) {
        llama_set_abort_callback(runtime_->context, [](void* state) {
            return static_cast<Runtime::AbortProbe*>(state)->Poll();
        }, &runtime_->abort);
    } else {
        llama_set_abort_callback(runtime_->context, nullptr, nullptr);
    }
}

bool ZenzEngine::Cancelled(std::string* error) const {
    if (!cancelCheck_ || !cancelCheck_()) return false;
    if (error) *error = kCancelledError;
    return true;
}

void ZenzEngine::ResetMemory() {
    llama_memory_clear(llama_get_memory(runtime_->context), true);
    runtime_->cached.clear();
    runtime_->primedValid = false;
}

// プロンプトをトークン化する。エンコーダ・デコーダ型は特殊トークンを自動で付けず、
// 末尾に </s> を明示的に置く（学習側と同じ形。llama.cppの既定に依存しない）
bool ZenzEngine::TokenizePrompt(const std::u32string& prompt, std::vector<int32_t>* tokens,
                                std::string* error) {
    Runtime& rt = *runtime_;
    if (!rt.isEncoderDecoder) return Tokenize(rt.vocab, prompt, /*addSpecial=*/true, tokens, error);
    if (!Tokenize(rt.vocab, prompt, /*addSpecial=*/false, tokens, error)) return false;
    tokens->push_back(llama_vocab_eos(rt.vocab));
    return true;
}

// KVキャッシュとの共通プレフィックスを差し引き、続きのトークンだけを評価する（デコーダ専用モデル）。
// 読みが伸びるだけの再変換は数トークンの評価で済む
bool ZenzEngine::DecodePrompt(const std::vector<int32_t>& tokens, std::string* error) {
    Runtime& rt = *runtime_;
    llama_memory_t memory = llama_get_memory(rt.context);
    rt.primedValid = false;
    size_t common = 0;
    if (ReuseEnabled()) {
        while (common < rt.cached.size() && common < tokens.size() &&
               rt.cached[common] == tokens[common]) {
            ++common;
        }
    }
    // 末尾トークンのlogitsが必要なので、完全一致でも最後の1つは評価し直す
    if (common == tokens.size() && common > 0) --common;
    // 共有プレフィックスが短いときはフルクリアの方が速い
    // （部分削除はKVキャッシュを断片化させ、以後のデコードを遅くする）
    constexpr size_t kMinReusePrefix = 4;
    if (common < kMinReusePrefix) common = 0;

    if (common == 0) {
        llama_memory_clear(memory, true);
        rt.cached.clear();
    } else if (common < rt.cached.size()) {
        if (llama_memory_seq_rm(memory, 0, static_cast<llama_pos>(common), -1)) {
            rt.cached.resize(common);
        } else {
            // 部分削除できないメモリ実装ではフルクリアに落とす
            llama_memory_clear(memory, true);
            rt.cached.clear();
            common = 0;
        }
    }

    std::vector<llama_token> suffix(tokens.begin() + common, tokens.end());
    if (!Decode(rt.context, suffix.data(), static_cast<int>(suffix.size()), error)) {
        ResetMemory();
        return false;
    }
    rt.cached = tokens;
    return true;
}

bool ZenzEngine::PrimePrompt(const std::vector<int32_t>& promptTokens, size_t* primedCount,
                             const float** logits, std::string* error) {
    Runtime& rt = *runtime_;
    if (rt.isEncoderDecoder && promptTokens.size() > llama_n_ubatch(rt.context)) {
        nextEncoderBatch_ = 0;
        for (unsigned capacity : {256u, 512u, 1024u}) {
            if (promptTokens.size() <= capacity) {
                nextEncoderBatch_ = capacity;
                break;
            }
        }
        if (error) *error = kEncoderBatchTooSmall;
        return false;
    }
    llama_memory_t memory = llama_get_memory(rt.context);

    // 直前と同じプロンプト: 続きのトークンだけを外してプロンプトだけの状態に戻し、取っておいたlogitsを使う。
    // 計算し直さないので、最初に評価したときと同じ値から始まる
    if (ReuseEnabled() && rt.primedValid && rt.primedPrompt == promptTokens) {
        bool restored = true;
        if (rt.cached.size() > rt.primedCount) {
            restored = llama_memory_seq_rm(memory, 0, static_cast<llama_pos>(rt.primedCount), -1);
            if (restored) rt.cached.resize(rt.primedCount);
        }
        if (restored) {
            *primedCount = rt.primedCount;
            *logits = rt.primedLogits.data();
            return true;
        }
    }
    rt.primedValid = false;

    if (rt.isEncoderDecoder) {
        if (!(ReuseEnabled() && rt.hasEncoded && rt.encoded == promptTokens)) {
            rt.hasEncoded = false;
            // デコーダの交差注意は、エンコード時に同じ系列番号を持っていたトークンにしか向かない
            // （llama.cpp の llm_graph_input_attn_cross）。使う系列すべての番号を付けておく
            const int32_t sequenceCount = static_cast<int32_t>(llama_n_seq_max(rt.context));
            const int32_t tokenCount = static_cast<int32_t>(promptTokens.size());
            llama_batch batch = llama_batch_init(tokenCount, 0, sequenceCount);
            for (int32_t index = 0; index < tokenCount; ++index) {
                batch.token[index] = promptTokens[index];
                batch.pos[index] = index;
                batch.n_seq_id[index] = sequenceCount;
                for (int32_t sequence = 0; sequence < sequenceCount; ++sequence) {
                    batch.seq_id[index][sequence] = sequence;
                }
                batch.logits[index] = 0;
            }
            batch.n_tokens = tokenCount;
            const int result = llama_encode(rt.context, batch);
            llama_batch_free(batch);
            if (result != 0) {
                ResetMemory();
                if (error) *error = result == 2 ? kCancelledError : "推論に失敗: llama_encode=" + std::to_string(result);
                return false;
            }
            rt.encoded = promptTokens;
            rt.hasEncoded = true;
        }
        // デコーダは開始トークンから始める
        ResetMemory();
        llama_token start = rt.decoderStartToken;
        if (!Decode(rt.context, &start, 1, error)) {
            ResetMemory();
            return false;
        }
        rt.cached.assign(1, start);
    } else if (!DecodePrompt(promptTokens, error)) {
        return false;
    }

    const float* current = llama_get_logits_ith(rt.context, -1);
    if (!current) {
        if (error) *error = "推論に失敗: プロンプトのlogitsが取れません";
        return false;
    }
    rt.primedLogits.assign(current, current + llama_vocab_n_tokens(rt.vocab));
    rt.primedPrompt = promptTokens;
    rt.primedCount = rt.cached.size();
    rt.primedValid = true;
    *primedCount = rt.primedCount;
    *logits = rt.primedLogits.data();
    return true;
}

namespace {

struct Picked {
    llama_token token;
    uint64_t mask;
    bool relaxed;
    float logProb;
    // 選んだトークンと、同じく制約を満たす2位のトークンとの対数確率の差
    // （同音異義語で迷っているほど小さい。競合が無ければ +inf）
    float margin;
};

// logitsから、読みの制約を満たすもっとも尤度の高いトークンを選ぶ。
// 制約を満たすトークンが1つもなければ制約を諦めて素の最尤トークンを返す（relaxed）
std::optional<Picked> SelectToken(
    const float* logits, int vocabSize,
    TokenPieces& pieces, const ReadingConstraint* constraint,
    uint64_t mask, bool scoreProbability, int excluded = -1) {
    if (!logits) return std::nullopt;
    const float logZ = scoreProbability ? LogSumExp(logits, vocabSize) : 0.0f;
    int bestIndex = -1;
    float bestLogit = 0;
    float second = kNegativeInfinity;
    uint64_t bestMask = 0;
    int fallbackIndex = -1;
    float fallbackLogit = 0;
    float fallbackSecond = kNegativeInfinity;

    for (int index = 0; index < vocabSize; ++index) {
        if (index == excluded) continue;
        const float logit = logits[index];
        if (fallbackIndex < 0 || logit > fallbackLogit) {
            fallbackSecond = fallbackIndex < 0 ? kNegativeInfinity : fallbackLogit;
            fallbackIndex = index;
            fallbackLogit = logit;
        } else if (logit > fallbackSecond) {
            fallbackSecond = logit;
        }
        if (!constraint) continue;
        // 現在の2位より低いトークンは制約を調べるまでもない
        if (logit <= second) continue;
        uint64_t nextMask = 0;
        const auto slot = pieces.Slot(index);
        if (slot == TokenPieces::terminator) {
            // 読みを使い切っていなければ終端は許さない（食い残し防止）
            if (!constraint->IsComplete(mask)) continue;
            nextMask = mask;
        } else {
            if (slot == TokenPieces::invalid) continue;
            nextMask = constraint->Advance(mask, pieces.Text(slot));
            if (nextMask == 0) continue;
        }
        if (bestIndex >= 0 && logit <= bestLogit) {
            second = logit;
        } else {
            second = bestIndex >= 0 ? bestLogit : kNegativeInfinity;
            bestIndex = index;
            bestLogit = logit;
            bestMask = nextMask;
        }
    }

    if (constraint && bestIndex >= 0) {
        return Picked{static_cast<llama_token>(bestIndex), bestMask, false, bestLogit - logZ,
                      bestLogit - second};
    }
    if (fallbackIndex < 0) return std::nullopt;
    return Picked{static_cast<llama_token>(fallbackIndex), 0, constraint != nullptr,
                  fallbackLogit - logZ, fallbackLogit - fallbackSecond};
}

} // namespace

// 貪欲法で1候補を生成する。forcedFirstTokenがあれば先頭をそのトークンに固定する。
// 各ステップでは読みと辻褄の合うトークンだけを選ぶ（constrained decoding）
bool ZenzEngine::Generate(const std::vector<int32_t>& promptTokens,
                          const FirstToken* forcedFirstToken, const std::u32string& reading,
                          Generated* out, std::string* error, Stats stats,
                          const std::vector<int32_t>* draft, int width, DraftMeasure* measure) {
    Runtime& rt = *runtime_;
    const int vocabSize = llama_vocab_n_tokens(rt.vocab);

    // プロンプトを評価して（同じプロンプトなら前の状態に戻して）生成開始状態にする
    size_t primedCount = 0;
    const float* logits = nullptr;
    // Internal forks share an already decoded prefix with the preceding path.
    // Keep its KV entries and replay one token to obtain the boundary logits.
    // The single sequence never retains a second model context or vocabulary row.
    size_t shared = 0;
    if (forcedFirstToken && forcedFirstToken->greedy && ReuseEnabled() &&
        rt.primedValid && rt.primedPrompt == promptTokens) {
        const auto& path = forcedFirstToken->greedy->decisions;
        primedCount = rt.primedCount;
        while (shared < static_cast<size_t>(forcedFirstToken->branchStep) &&
               shared < path.size() && primedCount + shared < rt.cached.size() &&
               path[shared].token == rt.cached[primedCount + shared]) ++shared;
        if (shared < 2 || !llama_memory_seq_rm(llama_get_memory(rt.context), 0,
                static_cast<llama_pos>(primedCount + shared - 1), -1)) {
            shared = 0;
        } else {
            rt.cached.resize(primedCount + shared - 1);
            auto token = path[shared-1].token;
            if (!Decode(rt.context, &token, 1, error)) { ResetMemory(); return false; }
            rt.cached.push_back(token);
            logits = llama_get_logits_ith(rt.context, -1);
            if (!logits) { ResetMemory(); return false; }
        }
    }
    if (!shared && !PrimePrompt(promptTokens, &primedCount, &logits, error)) return false;
    float logProb = 0;

    std::string outputBytes;
    char pieceBuffer[128];
    const bool alignCharacters = stats == Stats::Full || stats == Stats::Margin;
    const bool scoreProbability = stats == Stats::Full || stats == Stats::SequenceProbability;
    ConfidenceAligner aligner;
    // トークンの文字列断片を出力に足す。戻り値はその断片（自信度の対応づけ用）
    auto appendPiece = [&](llama_token token) -> std::string_view {
        const int written = llama_token_to_piece(
            rt.vocab, token, pieceBuffer,
            static_cast<int32_t>(sizeof(pieceBuffer)), 0, true);
        if (written <= 0) return {};
        outputBytes.append(pieceBuffer, written);
        return {pieceBuffer, static_cast<size_t>(written)};
    };
    using Clock = std::chrono::steady_clock;
    auto elapsedUs = [](Clock::time_point start) {
        return std::chrono::duration<double, std::micro>(Clock::now()-start).count();
    };
    size_t position = 0, blockBase = 0;
    int proposed = 0, used = 0;
    auto finishBlock = [&]() {
        if (!proposed) return true;
        measure->confirmed += used;
        if (used < proposed) {
            ++measure->partial;
            // Discard only guesses beyond the verified prefix.
            if (!llama_memory_seq_rm(llama_get_memory(rt.context), 0,
                                     static_cast<llama_pos>(blockBase+used), -1)) {
                ResetMemory();
                return false;
            }
            rt.cached.resize(blockBase+used);
        }
        proposed = used = 0;
        return true;
    };
    auto feed = [&](llama_token token) {
        if (proposed && used < proposed && rt.cached[blockBase+used] == token) {
            logits = llama_get_logits_ith(rt.context, used++);
            ++position;
            return true;
        }
        if (!finishBlock()) return false;
        // Always time a scalar token on this request. Stop proposing blocks if
        // two measured batches already cost more than the accepted scalar work.
        const bool economical = measure && (measure->blocks < 2 ||
            measure->blockUs <= 1.05 * measure->scalarUs / measure->scalarTokens * measure->confirmed);
        if (draft && position && position < draft->size() && (*draft)[position] == token && economical) {
            const int count = static_cast<int>(std::min({size_t(width), draft->size()-position,
                size_t(llama_n_ctx(rt.context))-rt.cached.size()}));
            if (count >= 2) {
                const auto start = Clock::now();
                auto batch = llama_batch_init(count, 0, 1);
                batch.n_tokens = count;
                blockBase = rt.cached.size();
                for (int j=0; j<count; ++j) {
                    batch.token[j] = (*draft)[position+j];
                    batch.pos[j] = static_cast<llama_pos>(blockBase+j);
                    batch.n_seq_id[j] = 1;
                    batch.seq_id[j][0] = 0;
                    batch.logits[j] = 1;
                }
                const int status = llama_decode(rt.context, batch);
                llama_batch_free(batch);
                if (status != 0) {
                    ResetMemory();
                    if (status == 2 && error) *error = kCancelledError;
                    return false;
                }
                rt.cached.insert(rt.cached.end(), draft->begin()+position, draft->begin()+position+count);
                logits = llama_get_logits_ith(rt.context, 0);
                measure->blockUs += elapsedUs(start);
                ++measure->blocks;
                measure->proposed += count;
                proposed = count;
                used = 1;
                ++position;
                return true;
            }
        }
        const auto start = measure ? Clock::now() : Clock::time_point{};
        if (!Decode(rt.context, &token, 1, error)) {
            ResetMemory();
            return false;
        }
        rt.cached.push_back(token);
        logits = llama_get_logits_ith(rt.context, -1);
        if (measure) { ++measure->scalarTokens; measure->scalarUs += elapsedUs(start); }
        ++position;
        return true;
    };

    // 読みの消費状況（constrained decoding用）。追跡できない読みや
    // 制約を満たすトークンが尽きた場合は制約なしの貪欲生成に戻す
    std::optional<ReadingConstraint> constraintStorage;
    if (options_.usesReadingConstraint) {
        constraintStorage = ReadingConstraint::Create(reading, options_.restrictLatinToReading);
    }
    const ReadingConstraint* constraint = constraintStorage ? &*constraintStorage : nullptr;
    uint64_t mask = constraint ? constraint->InitialMask() : 0;

    if (shared) {
        const auto& path = forcedFirstToken->greedy->decisions;
        for (size_t i = 0; i < shared; ++i) appendPiece(path[i].token);
        const auto& last = path[shared-1];
        logProb = last.cumulativeLogProb;
        mask = last.mask;
        if (!last.constrained) constraint = nullptr;
        position = shared;
    }

    if (forcedFirstToken && forcedFirstToken->branchStep < 0) {
        if (!feed(forcedFirstToken->token)) return false;
        logProb += forcedFirstToken->logProb;
        // 先頭を固定した候補では、先頭トークンのマージンは +inf
        const auto piece = appendPiece(forcedFirstToken->token);
        if (alignCharacters)
            aligner.Append(piece, CharacterConfidence{forcedFirstToken->logProb, std::numeric_limits<float>::infinity(), false});
        if (constraint) {
            const auto slot = rt.pieces.Slot(forcedFirstToken->token);
            if (slot >= TokenPieces::firstPiece) mask = constraint->Advance(mask,rt.pieces.Text(slot));
            if (mask == 0) constraint = nullptr;
        }
    }

    // 生成上限: 読みの長さに比例させつつ、KVキャッシュ（n_ctx）を超えて
    // llama_decodeが失敗しないように残り容量で抑える
    const int remainingContext =
        static_cast<int>(llama_n_ctx(rt.context)) - static_cast<int>(rt.cached.size() - shared);
    const int maxNewTokens = std::max(
        0, std::min(static_cast<int>(GraphemeCount(reading)) * 3 + 8, remainingContext));
    for (int step = static_cast<int>(shared); step < maxNewTokens; ++step) {
        if (Cancelled(error)) { finishBlock(); return false; }
        const int excluded = forcedFirstToken && forcedFirstToken->branchStep == step ? forcedFirstToken->token : -1;
        const auto picked = SelectToken(logits, vocabSize, rt.pieces, constraint, mask, scoreProbability, excluded);
        if (!picked) break;
        if (picked->relaxed) constraint = nullptr;
        mask = picked->mask;
        logProb += picked->logProb;
        if (llama_vocab_is_eog(rt.vocab, picked->token)) { out->terminated = true; break; }
        if (out->recordBranches)
            out->decisions.push_back({picked->margin, picked->token, logProb, mask, constraint != nullptr});

        const auto piece = appendPiece(picked->token);
        // zenzの特殊トークン（私用領域 U+EE00-U+EE0F）が出たら終了
        if (EndsWithZenzSpecial(outputBytes)) {
            out->terminated = true;
            outputBytes.resize(outputBytes.size() - 3);
            break;
        }
        if (alignCharacters)
            aligner.Append(piece, CharacterConfidence{picked->logProb, picked->margin, picked->relaxed});
        if (!feed(picked->token)) return false;
    }

    if (!finishBlock()) return false;
    const std::u32string untrimmed = DecodeUtf8DroppingFragments(outputBytes);
    out->text = TrimWhitespace(untrimmed);
    out->logProb = logProb;
    if (alignCharacters) out->confidences = aligner.Aligned(untrimmed, out->text);
    return true;
}

bool ZenzEngine::ConvertGuided(const std::u32string& reading, const std::u32string& left,
                               const std::u32string& draft, int width, DraftMeasure& measure,
                               std::vector<std::u32string>* results, std::string* error) {
    if (!results || !EnsureLoadedMeasured(error)) return false;
    results->clear();
    std::vector<llama_token> prompt, tokens;
    if (!TokenizePrompt(ModelPrompt(reading,left), &prompt, error)) return false;
    if (draft.empty() || draft.size()>96 || width<2 ||
        !Tokenize(runtime_->vocab,draft,false,&tokens,nullptr) || tokens.size()<3)
        return Convert(reading,left,1,results,error);
    Generated out;
    if (!Generate(prompt,nullptr,reading,&out,error,Stats::Text,&tokens,std::min(width,4),&measure)) {
        if (error && *error==kCancelledError) return false;
        return Convert(reading,left,1,results,error);
    }
    results->push_back(out.text.empty() ? reading : std::move(out.text));
    return true;
}

bool ZenzEngine::Convert(const std::u32string& reading,
                         const std::u32string& leftContext, int candidateCount,
                         std::vector<std::u32string>* results, std::string* error) {
    return ConvertWithPreview(reading, leftContext, candidateCount, results, error, {});
}

bool ZenzEngine::ConvertWithPreview(const std::u32string& reading,
                         const std::u32string& leftContext, int candidateCount,
                         std::vector<std::u32string>* results, std::string* error,
                         const std::function<void(const std::u32string&)>& greedyReady) {
    if (!results) return false;
    results->clear();
    if (!EnsureLoadedMeasured(error)) return false;
    const inference_timer::Measure measure(inference_timer::Measure::Kind::NeuralNetwork);
    Runtime& rt = *runtime_;

    const std::u32string prompt = ModelPrompt(reading, leftContext);
    std::vector<llama_token> promptTokens;
    if (!TokenizePrompt(prompt, &promptTokens, error)) return false;

    if (candidateCount <= 1) {
        Generated generated;
        if (!Generate(promptTokens, nullptr, reading, &generated, error, Stats::Text)) return false;
        if (greedyReady && generated.terminated && !generated.text.empty()) greedyReady(generated.text);
        results->push_back(generated.text.empty() ? reading : generated.text);
        return true;
    }

    // n-best: 先頭トークンを上位候補に分岐し、それぞれ貪欲に補完する。
    // かな漢字変換では先頭の文字が同音異義語をほぼ決めるため、これで多様な候補が得られる。
    // 各候補には系列全体の対数確率を付け、最良候補から kNbestLogProbWindow 以上
    // 離れたものは捨てる（読み制約は漢字・英字の読みを検証できないので、
    // 「活用を」「NIPPON」のような読みの合わない候補が探索の埋め草として通ってしまう。
    // モデル自身の確からしさで足切りすることでそれを除く）
    size_t primedCount = 0;
    const float* logits = nullptr;
    if (!PrimePrompt(promptTokens, &primedCount, &logits, error)) return false;

    std::optional<ReadingConstraint> constraint;
    if (options_.usesReadingConstraint) {
        constraint = ReadingConstraint::Create(reading, options_.restrictLatinToReading);
    }

    // プロンプト評価直後のlogitsから、上位トークンを対数確率つきで取る
    std::vector<FirstToken> firstTokens;
    {
        const int vocabSize = llama_vocab_n_tokens(rt.vocab);
        const float logZ = LogSumExp(logits, vocabSize);
        const size_t top = std::min<size_t>(static_cast<size_t>(candidateCount) * 8, vocabSize);
        // Keep only the K first-token branches. The worst retained branch is at
        // the heap root. Equal logits always prefer the lower vocabulary ID.
        auto better = [](const FirstToken& a, const FirstToken& b) {
            return a.logProb > b.logProb || (a.logProb == b.logProb && a.token < b.token);
        };
        firstTokens.reserve(top);
        for (int i = 0; i < static_cast<int>(top); ++i) firstTokens.push_back({i, logits[i]});
        std::make_heap(firstTokens.begin(), firstTokens.end(), better);
        for (int i = static_cast<int>(top); i < vocabSize; ++i) {
            if (logits[i] <= firstTokens.front().logProb) continue;
            std::pop_heap(firstTokens.begin(), firstTokens.end(), better);
            firstTokens.back() = {i, logits[i]};
            std::push_heap(firstTokens.begin(), firstTokens.end(), better);
        }
        std::sort_heap(firstTokens.begin(), firstTokens.end(), better);
        for (auto& token : firstTokens) token.logProb -= logZ;
    }

    struct Scored {
        std::u32string text;
        float logProb;
    };
    std::vector<Scored> scored;
    float bestLogProb = kNegativeInfinity;
    Generated greedy;
    greedy.recordBranches = true;
    if (!Generate(promptTokens, nullptr, reading, &greedy, error, Stats::SequenceProbability)) return false;
    if (!greedy.text.empty()) {
        if (greedyReady && greedy.terminated) greedyReady(greedy.text);
        scored.push_back({greedy.text,greedy.logProb});
        bestLogProb = greedy.logProb;
    }
    std::vector<size_t> positions;
    for (size_t i=1; i<greedy.decisions.size(); ++i)
        if (greedy.decisions[i].margin <= 3.0f) positions.push_back(i);
    std::stable_sort(positions.begin(),positions.end(),[&](size_t a,size_t b){
        return greedy.decisions[a].margin < greedy.decisions[b].margin;
    });
    // Re-evaluate only the most ambiguous internal decisions. Each pass uses the same single KV sequence.
    positions.resize(std::min(positions.size(),static_cast<size_t>(candidateCount-1)));
    for (size_t step : positions) {
        FirstToken fork{greedy.decisions[step].token,0.0f,static_cast<int>(step),&greedy};
        Generated alternate;
        if (!Generate(promptTokens,&fork,reading,&alternate,error,Stats::SequenceProbability)) return false;
        if (!alternate.text.empty() && std::none_of(scored.begin(),scored.end(),[&](const auto& other){return other.text==alternate.text;})) {
            scored.push_back({alternate.text,alternate.logProb});
            bestLogProb=std::max(bestLogProb,alternate.logProb);
        }
    }
    for (const FirstToken& first : firstTokens) {
        // 先頭トークンの確率だけで既に足切り線を下回るなら、続きを生成しても届かない
        // （後続の対数確率は0以下）。先頭トークンは確率の高い順なので以降も同様
        if (first.logProb < bestLogProb - kNbestLogProbWindow) break;
        if (llama_vocab_is_eog(rt.vocab, first.token)) continue;
        // 読みと辻褄の合わない先頭トークンは候補にしない
        if (constraint) {
            const auto slot = rt.pieces.Slot(first.token);
            if (slot < TokenPieces::firstPiece) continue;
            if (constraint->Advance(constraint->InitialMask(), rt.pieces.Text(slot)) == 0) continue;
        }
        // This constrained first token already produced the complete greedy path.
        if (greedy.terminated && !greedy.decisions.empty() && first.token == greedy.decisions.front().token) {
            if (static_cast<int>(scored.size()) >= candidateCount) break;
            continue;
        }
        Generated generated;
        if (!Generate(promptTokens, &first, reading, &generated, error, Stats::SequenceProbability)) return false;
        if (generated.text.empty()) continue;
        const auto existing =
            std::find_if(scored.begin(), scored.end(),
                         [&](const Scored& item) { return item.text == generated.text; });
        if (existing != scored.end()) {
            // 同じ文字列に別のトークン列で到達した: 確率の高い方を残す
            existing->logProb = std::max(existing->logProb, generated.logProb);
        } else {
            scored.push_back({generated.text, generated.logProb});
        }
        bestLogProb = std::max(bestLogProb, generated.logProb);
        if (static_cast<int>(scored.size()) >= candidateCount) break;
    }

    scored.erase(std::remove_if(scored.begin(), scored.end(),
                                [&](const Scored& item) {
                                    return !(item.logProb >= bestLogProb - kNbestLogProbWindow);
                                }),
                 scored.end());
    std::stable_sort(scored.begin(), scored.end(),
                     [](const Scored& a, const Scored& b) { return a.logProb > b.logProb; });
    for (Scored& item : scored) {
        if (results->size() == static_cast<size_t>(candidateCount)) break;
        results->push_back(std::move(item.text));
    }
    if (results->empty()) results->push_back(reading);
    return true;
}

bool ZenzEngine::GenerateBest(const std::u32string& reading, const std::u32string& leftContext,
                              Generated* out, Stats stats, std::string* error) {
    if (!EnsureLoadedMeasured(error)) return false;
    const inference_timer::Measure measure(inference_timer::Measure::Kind::NeuralNetwork);
    const std::u32string prompt = ModelPrompt(reading, leftContext);
    std::vector<llama_token> promptTokens;
    if (!TokenizePrompt(prompt, &promptTokens, error)) return false;
    if (!runtime_->isEncoderDecoder && options_.decoderCapacity) {
        const size_t required = std::min<size_t>(1024, promptTokens.size() + GraphemeCount(reading) * 3 + 8);
        unsigned capacity = llama_n_ctx(runtime_->context);
        if (capacity < required) {
            while (capacity < required) capacity = std::min(1024u, capacity * 2);
            runtime_.reset();
            options_.decoderCapacity = capacity;
            if (!EnsureLoadedMeasured(error)) return false;
        }
    }
    return Generate(promptTokens, nullptr, reading, out, error, stats);
}

bool ZenzEngine::ConvertForRouting(const std::u32string& reading, const std::u32string& left,
                                   std::u32string* text, double* minMargin, std::string* error) {
    return ConvertForRouting(reading, left, text, minMargin, error, nullptr);
}

bool ZenzEngine::ConvertForRouting(const std::u32string& reading, const std::u32string& left,
                                   std::u32string* text, double* minMargin, std::string* error, bool* terminated) {
    if (!text || !minMargin) return false;
    Generated out;
    if (!GenerateBest(reading, left, &out, Stats::Margin, error)) return false;
    if (terminated) *terminated = out.terminated && !out.text.empty();
    *minMargin = out.text.empty() ? 0.0 : 1000.0;
    for (const auto& c : out.confidences) *minMargin = std::min(*minMargin, double(c.margin));
    *text = out.text.empty() ? reading : std::move(out.text);
    return true;
}

bool ZenzEngine::ConvertScored(const std::u32string& reading, const std::u32string& leftContext,
                               ScoredConversion* result, std::string* error) {
    if (!result) return false;
    Generated generated;
    if (!GenerateBest(reading, leftContext, &generated, Stats::Full, error)) return false;
    if (generated.text.empty()) {
        // 何も生成できなかった: 読みをそのまま返す（自信は無いものとして扱う）
        result->text = reading;
        result->logProb = kNegativeInfinity;
        result->confidences.assign(GraphemeCount(reading), CharacterConfidence{kNegativeInfinity, 0, true});
        return true;
    }
    result->text = std::move(generated.text);
    result->logProb = generated.logProb;
    result->confidences = std::move(generated.confidences);
    return true;
}

bool ZenzEngine::Score(const std::vector<std::u32string>& candidates,
                       const std::u32string& reading, const std::u32string& leftContext,
                       std::vector<float>* scores, std::string* error) {
    if (!scores) return false;
    scores->clear();
    if (!EnsureLoadedMeasured(error)) return false;
    if (candidates.empty()) return true;
    const inference_timer::Measure measure(inference_timer::Measure::Kind::NeuralNetwork);
    Runtime& rt = *runtime_;
    llama_context* ctx = rt.context;
    const int vocabSize = llama_vocab_n_tokens(rt.vocab);

    const std::u32string prompt = ModelPrompt(reading, leftContext);
    std::vector<llama_token> promptTokens;
    if (!TokenizePrompt(prompt, &promptTokens, error)) return false;

    const size_t capacity = llama_n_ctx(ctx);
    scores->assign(candidates.size(), kNegativeInfinity);
    size_t primedCount = 0;
    const float* initialLogits = nullptr;
    if (!PrimePrompt(promptTokens, &primedCount, &initialLogits, error)) return false;
    const float initialNormalizer = LogSumExp(initialLogits, vocabSize);
    // Score siblings consecutively. Keep only the previous path's scalar prefix
    // scores. Replaying its last shared token supplies every divergent logit.
    struct Path { size_t index; std::vector<llama_token> tokens; };
    std::vector<Path> paths;
    for (size_t i = 0; i < candidates.size(); ++i) {
        Path path{i, {}};
        if (!Tokenize(rt.vocab, candidates[i], false, &path.tokens, error)) return false;
        if (!path.tokens.empty() && primedCount + path.tokens.size() <= capacity)
            paths.push_back(std::move(path));
    }
    std::stable_sort(paths.begin(), paths.end(), [](const Path& a, const Path& b) {
        return a.tokens < b.tokens;
    });
    std::vector<float> prefixScores;
    const Path* previous = nullptr;
    for (const auto& path : paths) {
        if (Cancelled(error)) return false;
        const auto& tokens = path.tokens;
        size_t shared = 0;
        if (previous && ReuseEnabled()) {
            const auto& last = previous->tokens;
            while (shared < std::min(tokens.size(), last.size()) && tokens[shared] == last[shared]) ++shared;
            if (shared == tokens.size() && shared == last.size()) {
                (*scores)[path.index] = (*scores)[previous->index];
                continue;
            }
        }
        size_t start = shared > 1 ? shared - 1 : 0;
        if (start && llama_memory_seq_rm(llama_get_memory(ctx), 0, static_cast<llama_pos>(primedCount + start), -1))
            rt.cached.resize(primedCount + start);
        else
            start = 0;
        float score;
        if (start) {
            score = prefixScores[start];
        } else {
            const float* promptLogits = initialLogits;
            if (previous && !PrimePrompt(promptTokens, &primedCount, &promptLogits, error)) return false;
            score = promptLogits[tokens.front()] - initialNormalizer;
        }
        prefixScores.resize(tokens.size());
        prefixScores[start] = score;
        // llama.cpp keeps its largest logits output buffer for the life of the
        // context. Limit live vocabulary rows even for a long sentence.
        constexpr size_t maxRows = 8;
        for (size_t base = start; base < tokens.size(); base += maxRows) {
            const auto count = static_cast<int32_t>(std::min(maxRows, tokens.size() - base));
            llama_batch batch = llama_batch_init(count, 0, 1);
            batch.n_tokens = count;
            for (int32_t offset = 0; offset < count; ++offset) {
                batch.token[offset] = tokens[base + offset];
                batch.pos[offset] = static_cast<llama_pos>(primedCount + base + offset);
                batch.n_seq_id[offset] = 1;
                batch.seq_id[offset][0] = 0;
                batch.logits[offset] = 1;
            }
            const int status = llama_decode(ctx, batch);
            llama_batch_free(batch);
            if (status != 0) {
                ResetMemory();
                if (error) *error = status == 2 ? kCancelledError : "Candidate scoring failed in llama_decode";
                return false;
            }
            rt.cached.insert(rt.cached.end(), tokens.begin() + base, tokens.begin() + base + count);
            for (int32_t offset = 0; offset < count; ++offset) {
                const float* logits = llama_get_logits_ith(ctx, offset);
                if (!logits) {
                    if (error) *error = "Missing candidate logits";
                    return false;
                }
                const float normalizer = LogSumExp(logits, vocabSize);
                const size_t next = base + offset + 1;
                if (next < tokens.size()) {
                    score += logits[tokens[next]] - normalizer;
                    prefixScores[next] = score;
                } else {
                    score += TerminatorLogSumExp(logits, rt.pieces.Terminators()) - normalizer;
                }
            }
        }
        (*scores)[path.index] = score;
        previous = &path;
    }
    return true;
}

} // namespace iroha

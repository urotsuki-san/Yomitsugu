#pragma once
#include "ime_engine.h"
#include "iroha/zenz_engine.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <utility>
#include <windows.h>
#include "spans.h"

class HybridRouter {
public:
  HybridRouter(ime::ConversionModel configured,
               const std::filesystem::path &model_folder);
  ~HybridRouter();
  bool enabled() const { return mode_ != Mode::Off; }
  bool LowMemory() const;
  void ReleaseModels() { iroha_.reset(); jinen_.reset(); }
  struct Cancelled {};
  void SetCancelEvent(HANDLE event) { cancel_event_ = event; }
  void CheckCancelled() const { if (IsCancelled()) throw Cancelled{}; }
  bool verifies_dictionary() const {return mode_ == Mode::Verify || mode_ == Mode::Full;}
  bool ConfirmsSuffix(const std::string& left, const std::string& reading, const std::string& surface);
  std::u32string Reading(const ime::DecodeInput &) const;
  struct PrimaryResult {
    std::u32string text;
    double margin = 0;
    bool terminated = false;
  };
  PrimaryResult Primary(const std::u32string& reading, const std::u32string& left);
  void Refine(const ime::DecodeInput &, const std::u32string &,
              std::vector<ime::Candidate> &, ReadingPaths&,
              const std::function<void(const std::string&)>& early = {}, const PrimaryResult* prepared = nullptr);

private:
  using SurfaceScores = std::vector<std::pair<std::u32string, float>>;
  void AddClause(const ime::DecodeInput&, const std::u32string&, std::vector<ime::Candidate>&, size_t split, bool promote,
                 const SurfaceScores& previous = {});
  void RefinePrimary(const ime::DecodeInput&, const std::u32string&, std::vector<ime::Candidate>&, ReadingPaths&,
                     const std::function<void(const std::string&)>& early, const PrimaryResult* prepared);
  void RefineWhole(const ime::DecodeInput&, const std::u32string&, std::vector<ime::Candidate>&, ReadingPaths&,
                   bool keepExpert, bool& disagreement, SurfaceScores* scores,
                   const std::function<void(const std::string&)>& early, const PrimaryResult* prepared);
  enum class Mode { Off, Auto, Verify, Warm, Full };
  bool ReleaseAfterUse() const { return mode_ == Mode::Auto || verifies_dictionary(); }
  iroha::DraftPolicy policy_;
  bool serial_reference_ = false;
  Mode mode_ = Mode::Off;
  bool trace_ = false, jinen_failed_ = false, iroha_failed_ = false;
  std::filesystem::path folder_;
  std::unique_ptr<iroha::ZenzEngine> jinen_, iroha_;
  HANDLE pressure_ = nullptr;
  HANDLE cancel_event_ = nullptr;
  bool IsCancelled() const {
    return cancel_event_ && WaitForSingleObject(cancel_event_, 0) == WAIT_OBJECT_0;
  }
  bool Load(std::unique_ptr<iroha::ZenzEngine> &, bool primary,
            unsigned encoder_batch = 0, const std::u32string &reading = {},
            const std::u32string &left = {});
};

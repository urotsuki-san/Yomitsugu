#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "azookey_bridge.h"

namespace ime {

struct Candidate {
  std::string output_text;
  std::string reading_text;
  bool is_raw = false;
  double total = 0.0;
  double en_score = 0.0;
  double jp_score = 0.0;
  double coverage = 0.0;
  double edit_cost = 0.0;
  double protect_score = 0.0;
  double context_score = 0.0;
  double completion_bonus = 0.0;
};

struct DecodeInput {
  std::string raw_text;
  std::string left_context;
  std::string right_context;
  std::string field = "prose";
  std::string phase = "end_of_phrase";
  bool punctuation_completion = false;
  int candidate_limit = 8;
  int revision = 1;
  int context_generation = 1;
  int interaction_generation = 1;
};

std::vector<Candidate> Decode(const DecodeInput& input);
std::string ConvertRomaji(const std::string& raw, bool* ok);
bool ConvertRomajiChecked(const std::string& raw, std::string* kana);

// UTF-8文字列の文字種を変換する。vkにはVK_F6～VK_F10を指定する。
std::string ConvertCharacterClass(const std::string& utf8, int vk_fkey);

enum class SpaceAction { kInsertSpace, kStartConversion, kNextCandidate, kPrevCandidate };

class Session {
 public:
  Session();

  void set_punctuation_completion(bool enabled);
  void set_learning_allowed(bool allowed);
  void set_field(const std::string& field);
  void set_context(const std::string& left, const std::string& right);
  void simulate_worker_crash();
  void simulate_worker_recover();
  void set_deferred_decoding(bool enabled) { deferred_decoding_ = enabled; }
  DecodeInput decode_input() const;
  bool ApplyCandidates(const DecodeInput& request, std::vector<Candidate> candidates);
  void CancelConversion();
  void Reset();
  void PressCharacterClass(int vk);
  void PressHome();
  void PressEnd();
  std::string caret_prefix() const;

  void Type(const std::string& text);
  void Backspace();
  void DeleteForward();
  SpaceAction QuerySpace() const;
  void PressSpace();
  void PressShiftSpace();
  void PressEnter();
  void PressEscape();
  void PressDown();
  void PressUp();
  bool SelectCandidate(int index);
  void ReplaceVisible(const std::string& text);
  void ChooseLiteral();
  // 変換中の文節移動と境界の変更を扱う。
  void PressLeft();
  void PressRight();
  void PressShiftLeft();
  void PressShiftRight();
  int segment_index() const { return segment_index_; }
  int segment_count() const { return segment_bounds_.empty() ? 0 : static_cast<int>(segment_bounds_.size()) - 1; }

  std::string visible_text() const;
  const std::string& raw_text() const { return raw_text_; }
  const std::vector<Candidate>& candidates() const { return candidates_; }
  int selected_index() const { return selected_index_; }
  int revision() const { return revision_; }
  int context_generation() const { return context_generation_; }
  int interaction_generation() const { return interaction_generation_; }
  bool composing() const { return !raw_text_.empty(); }
  bool is_converting() const { return manual_lock_ && !candidates_.empty(); }
  bool candidates_ready() const {
    return !deferred_decoding_ || editing_ || raw_text_.size() > 512 ||
        (manual_lock_ && !awaiting_candidates_) ||
        (resolved_revision_ == revision_ && resolved_context_ == context_generation_ && !awaiting_candidates_);
  }
  bool last_commit_learnable() const { return last_commit_learnable_; }
  const std::vector<std::string>& output_log() const { return output_log_; }
  const std::vector<std::string>& learning_log() const { return learning_log_; }

 private:
  void Refresh();
  void Commit(const std::string& text, const char* reason);
  void BumpInteraction();
  void NextCandidate();
  void PrevCandidate();
  void RebuildSegments();
  static void PushCapped(std::vector<std::string>& log, std::string value);

  int revision_ = 0;
  int context_generation_ = 0;
  int interaction_generation_ = 0;
  int candidate_list_generation_ = 0;
  int resolved_revision_ = -1, resolved_context_ = -1;
  std::string raw_text_;
  size_t raw_cursor_ = 0;
  bool editing_ = false;
  bool deferred_decoding_ = false;
  bool awaiting_candidates_ = false;
  std::string left_context_;
  std::string right_context_;
  std::string field_ = "prose";
  std::string phase_ = "end_of_phrase";
  // 文末の句点補完は既定で無効（AUTO-01）。
  bool punctuation_completion_ = false;
  bool learning_allowed_ = true;
  bool manual_lock_ = false;
  bool engine_alive_ = true;
  bool last_commit_learnable_ = false;
  int selected_index_ = 0;
  // 文節境界はraw_text_のUTF-8バイト位置。先頭は0、末尾は文字列長。
  std::vector<size_t> segment_bounds_;
  int segment_index_ = 0;
  bool segment_manual_ = false;
  std::vector<Candidate> candidates_;
  std::vector<std::string> output_log_;
  std::vector<std::string> learning_log_;
  std::vector<std::string> committed_history_;
};

}  // namespace ime

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

// F6-F10 character class conversion on UTF-8 text. vk: VK_F6..VK_F10.
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
  // Segment (bunsetsu) navigation during conversion. ←/→ move, Shift+←/→ resize.
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
  std::string raw_text_;
  size_t raw_cursor_ = 0;
  bool editing_ = false;
  bool deferred_decoding_ = false;
  bool awaiting_candidates_ = false;
  std::string left_context_;
  std::string right_context_;
  std::string field_ = "prose";
  std::string phase_ = "end_of_phrase";
  // Spec AUTO-01: punctuation completion default OFF
  bool punctuation_completion_ = false;
  bool learning_allowed_ = true;
  bool manual_lock_ = false;
  bool engine_alive_ = true;
  int selected_index_ = 0;
  // Segment boundaries as UTF-8 byte offsets into raw_text_ (starts 0, ends size).
  std::vector<size_t> segment_bounds_;
  int segment_index_ = 0;
  bool segment_manual_ = false;
  std::vector<Candidate> candidates_;
  std::vector<std::string> output_log_;
  std::vector<std::string> learning_log_;
  std::vector<std::string> committed_history_;
};

}  // namespace ime

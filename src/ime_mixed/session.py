from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional

from .decoder import MixedDecoder
from .types import (
    Candidate,
    CompositionPhase,
    DisplayMode,
    InputSnapshot,
    SpaceAction,
)


@dataclass
class SessionConfig:
    punctuation_completion_enabled: bool = False
    learning_allowed: bool = True
    display_mode: DisplayMode = DisplayMode.LIVE
    candidate_limit: int = 8


class ImeSession:
    def __init__(self, config: Optional[SessionConfig] = None) -> None:
        self.config = config or SessionConfig()
        self.session_id = "s0"
        self.revision = 0
        self.context_generation = 0
        self.interaction_generation = 0
        self.phase = CompositionPhase.EMPTY
        self.manual_override = False
        self.visible_candidate_id: Optional[str] = None
        self.candidate_list_generation = 0
        self.raw_text = ""
        self.raw_cursor = 0
        self.left_context = ""
        self.right_context = ""
        self.field_hints: Dict[str, Any] = {"field": "prose"}
        self.candidates: List[Candidate] = []
        self.selected_index = 0
        self.committed_history: List[Dict[str, Any]] = []
        self.output_log: List[str] = []
        self.learning_log: List[str] = []
        self.diagnostic_log: List[str] = []
        self.consumed_spaces: List[int] = []
        self.engine_alive = True
        self.text_service_disabled = False
        self.pressed_keys: List[str] = []
        self.send_events: List[str] = []
        self.decoder = MixedDecoder()
        self._pending_manual_lock = False

    def snapshot(self) -> InputSnapshot:
        return InputSnapshot(
            session_id=self.session_id,
            revision=self.revision,
            context_generation=self.context_generation,
            interaction_generation=self.interaction_generation,
            phase=self.phase,
            display_mode=self.config.display_mode,
            manual_override=self.manual_override,
            visible_candidate_id=self.visible_candidate_id,
            candidate_list_generation=self.candidate_list_generation,
            raw_text=self.raw_text,
            raw_cursor=self.raw_cursor,
            left_context=self.left_context,
            right_context=self.right_context,
            field_hints=dict(self.field_hints),
            learning_allowed=self.config.learning_allowed,
            punctuation_completion_enabled=self.config.punctuation_completion_enabled,
            candidate_limit=self.config.candidate_limit,
        )

    def set_field(self, field_type: str) -> None:
        self.field_hints = {"field": field_type}
        self._bump_context()

    def set_context(self, left: str, right: str) -> None:
        self.left_context = left
        self.right_context = right
        self._bump_context()

    def set_learning(self, allowed: bool) -> None:
        self.config.learning_allowed = allowed

    def set_punctuation_completion(self, enabled: bool) -> None:
        self.config.punctuation_completion_enabled = enabled

    def simulate_worker_crash(self) -> None:
        self.engine_alive = False

    def simulate_worker_recover(self) -> None:
        self.engine_alive = True

    def activate_text_service_disabled_context(self) -> None:
        self.text_service_disabled = True
        self.raw_text = ""
        self.candidates = []
        self.phase = CompositionPhase.EMPTY

    def _bump_context(self) -> None:
        self.context_generation += 1

    def _bump_interaction(self) -> None:
        self.interaction_generation += 1
        self.candidate_list_generation += 1
        self._pending_manual_lock = False

    def focus_other_field(self, left: str = "", right: str = "", field_type: str = "prose") -> None:
        if self.raw_text and self.phase != CompositionPhase.EMPTY:
            self.commit(reason="focus_change")
        self.left_context = left
        self.right_context = right
        self.field_hints = {"field": field_type}
        self._bump_context()
        self.phase = CompositionPhase.EMPTY
        self.raw_text = ""
        self.raw_cursor = 0
        self.candidates = []
        self.visible_candidate_id = None

    def type(self, text: str) -> None:
        if self.text_service_disabled:
            self.pressed_keys.append(f"type_ignored:{text}")
            return
        self.pressed_keys.append(f"type:{text}")
        if self.phase in (CompositionPhase.MANUAL_CONVERSION, CompositionPhase.MANUAL_OVERRIDE):
            self.phase = CompositionPhase.AUTO_COMPOSING
            self.visible_candidate_id = None
            self.selected_index = 0
        self.raw_text += text
        self.raw_cursor = len(self.raw_text)
        self.revision += 1
        self.phase = CompositionPhase.AUTO_COMPOSING
        self._bump_interaction()
        self._refresh_candidates()

    def backspace(self) -> None:
        if self.text_service_disabled:
            self.pressed_keys.append("backspace_ignored")
            return
        self.pressed_keys.append("backspace")
        if self.phase in (CompositionPhase.MANUAL_CONVERSION, CompositionPhase.MANUAL_OVERRIDE):
            self.phase = CompositionPhase.AUTO_COMPOSING
            self.visible_candidate_id = None
            self.selected_index = 0
        if not self.raw_text:
            self.output_log.append("backspace_passthrough")
            return
        self.raw_text = self.raw_text[:-1]
        self.raw_cursor = len(self.raw_text)
        self.revision += 1
        if not self.raw_text:
            self.phase = CompositionPhase.EMPTY
            self.candidates = []
            self.visible_candidate_id = None
            return
        self.phase = CompositionPhase.AUTO_COMPOSING
        self._bump_interaction()
        self._refresh_candidates()

    def _refresh_candidates(self) -> None:
        if not self.engine_alive:
            snap = self.snapshot()
            raw_cand = Candidate(
                output_text=self.raw_text,
                spans=[],
                edit_operations=[],
                source_revision=snap.revision,
                source_context_generation=snap.context_generation,
                source_interaction_generation=snap.interaction_generation,
                stable_candidate_id="raw_fallback",
                is_raw=True,
                reading_text=self.raw_text,
            )
            self.candidates = [raw_cand]
            self.visible_candidate_id = raw_cand.stable_candidate_id
            self.selected_index = 0
            return
        if self._pending_manual_lock and self.candidates:
            return
        self.candidates = self.decoder.decode(self.snapshot())
        if self.candidates and self.phase == CompositionPhase.AUTO_COMPOSING:
            self.selected_index = 0
            self.visible_candidate_id = self.candidates[0].stable_candidate_id

    def space_action(self) -> SpaceAction:
        if self.text_service_disabled or not self.raw_text:
            return SpaceAction.INSERT_SPACE
        if self.phase == CompositionPhase.MANUAL_CONVERSION and self.candidates:
            return SpaceAction.NEXT_CANDIDATE
        preview_is_ja = False
        if self.candidates:
            top = self.candidates[0]
            if not top.is_raw and top.output_text != self.raw_text:
                preview_is_ja = True
            elif any(
                s.kind.value == "ja" for s in top.spans
            ):
                preview_is_ja = True
        if preview_is_ja:
            return SpaceAction.START_CONVERSION
        return SpaceAction.INSERT_SPACE

    def press_space(self) -> None:
        if self.text_service_disabled:
            self.pressed_keys.append("space_ignored")
            return
        action = self.space_action()
        self.pressed_keys.append(f"space:{action.value}")
        self.consumed_spaces.append(self.revision)
        if action == SpaceAction.NEXT_CANDIDATE:
            self._bump_interaction()
            self._next_candidate()
            return
        if action == SpaceAction.START_CONVERSION:
            self._bump_interaction()
            self.phase = CompositionPhase.MANUAL_CONVERSION
            self._pending_manual_lock = True
            if self.candidates:
                self.selected_index = 0
                self.visible_candidate_id = self.candidates[0].stable_candidate_id
            return
        self.raw_text += " "
        self.raw_cursor = len(self.raw_text)
        self.revision += 1
        self._bump_interaction()
        self._refresh_candidates()

    def press_shift_space(self) -> None:
        if self.text_service_disabled:
            return
        self.pressed_keys.append("shift_space")
        if self.phase == CompositionPhase.MANUAL_CONVERSION:
            self._bump_interaction()
            self._prev_candidate()

    def _next_candidate(self) -> None:
        if not self.candidates:
            return
        self.selected_index = (self.selected_index + 1) % len(self.candidates)
        self.visible_candidate_id = self.candidates[self.selected_index].stable_candidate_id
        self.phase = CompositionPhase.MANUAL_CONVERSION
        self._pending_manual_lock = True

    def _prev_candidate(self) -> None:
        if not self.candidates:
            return
        self.selected_index = (self.selected_index - 1) % len(self.candidates)
        self.visible_candidate_id = self.candidates[self.selected_index].stable_candidate_id
        self.phase = CompositionPhase.MANUAL_CONVERSION
        self._pending_manual_lock = True

    def visible_text(self) -> str:
        if not self.raw_text:
            return ""
        if self.phase == CompositionPhase.EMPTY:
            return ""
        if self.phase == CompositionPhase.AUTO_COMPOSING and self.candidates:
            return self.candidates[0].output_text
        if self.phase in (CompositionPhase.MANUAL_CONVERSION, CompositionPhase.MANUAL_OVERRIDE):
            if 0 <= self.selected_index < len(self.candidates):
                return self.candidates[self.selected_index].output_text
            if self.candidates:
                return self.candidates[0].output_text
        return self.raw_text

    def press_enter(self) -> None:
        if self.text_service_disabled:
            self.pressed_keys.append("enter_ignored")
            return
        self.pressed_keys.append("enter")
        if not self.raw_text:
            self.output_log.append("enter_passthrough")
            self.send_events.append("enter_passthrough")
            return
        self.commit(reason="enter")

    def press_escape(self) -> None:
        if self.text_service_disabled:
            return
        self.pressed_keys.append("escape")
        if self.phase == CompositionPhase.MANUAL_CONVERSION:
            self._bump_interaction()
            self.phase = CompositionPhase.AUTO_COMPOSING
            self.selected_index = 0
            self.visible_candidate_id = (
                self.candidates[0].stable_candidate_id if self.candidates else None
            )
            return
        if self.phase != CompositionPhase.EMPTY:
            self._bump_interaction()
            self.raw_text = ""
            self.raw_cursor = 0
            self.candidates = []
            self.visible_candidate_id = None
            self.phase = CompositionPhase.EMPTY

    def choose_literal(self) -> None:
        if self.text_service_disabled or not self.raw_text:
            return
        self.pressed_keys.append("choose_literal")
        self._bump_interaction()
        text = self.raw_text
        self._commit_text(text, reason="literal")

    def choose_index(self, index: int) -> None:
        if self.text_service_disabled or not self.candidates:
            return
        if index < 0 or index >= len(self.candidates):
            return
        self._bump_interaction()
        if self.phase == CompositionPhase.AUTO_COMPOSING:
            self.phase = CompositionPhase.MANUAL_CONVERSION
        self.selected_index = index
        self.visible_candidate_id = self.candidates[index].stable_candidate_id
        self.commit(reason="choose_index")

    def convert_chunk(self, key: str) -> None:
        if self.text_service_disabled or not self.raw_text:
            return
        self.pressed_keys.append(f"convert:{key}")
        self._bump_interaction()
        self.manual_override = True
        self.phase = CompositionPhase.MANUAL_OVERRIDE

    def commit(self, reason: str = "enter") -> None:
        text = self.visible_text()
        if not text and not self.raw_text:
            return
        if not text:
            text = self.raw_text
        self._commit_text(text, reason=reason)

    def _commit_text(self, text: str, reason: str) -> None:
        record = {
            "text": text,
            "raw": self.raw_text,
            "reason": reason,
            "revision": self.revision,
            "candidate_id": self.visible_candidate_id,
        }
        self.committed_history.append(record)
        self.output_log.append(text)
        if self.config.learning_allowed and text and text != self.raw_text:
            if not self.text_service_disabled:
                self.learning_log.append(text)
        self.raw_text = ""
        self.raw_cursor = 0
        self.candidates = []
        self.visible_candidate_id = None
        self.selected_index = 0
        self.phase = CompositionPhase.COMMITTED
        self.manual_override = False
        self._pending_manual_lock = False
        self.revision += 1
        self._bump_interaction()
        self.phase = CompositionPhase.EMPTY

    def undo_commit(self) -> None:
        if self.text_service_disabled:
            return
        self.pressed_keys.append("undo_commit")
        if not self.committed_history:
            self.output_log.append("undo_unavailable")
            return
        rec = self.committed_history.pop()
        self.output_log.append(f"undo:{rec['text']}")
        self._bump_interaction()
        self.raw_text = rec["raw"]
        self.raw_cursor = len(self.raw_text)
        self.phase = CompositionPhase.AUTO_COMPOSING if self.raw_text else CompositionPhase.EMPTY
        self.revision += 1
        if self.raw_text:
            self._refresh_candidates()

    def deliver_result(
        self,
        candidate: Candidate,
        source_revision: int,
        source_context_generation: int,
        source_interaction_generation: int,
    ) -> bool:
        if self.text_service_disabled:
            return False
        if source_revision != self.revision:
            self.diagnostic_log.append(
                f"drop_stale_revision:{source_revision}!={self.revision}"
            )
            return False
        if source_context_generation != self.context_generation:
            self.diagnostic_log.append(
                f"drop_stale_context:{source_context_generation}!={self.context_generation}"
            )
            return False
        if source_interaction_generation != self.interaction_generation:
            self.diagnostic_log.append(
                f"drop_stale_interaction:{source_interaction_generation}!={self.interaction_generation}"
            )
            return False
        if self.phase == CompositionPhase.MANUAL_CONVERSION or self._pending_manual_lock:
            self.diagnostic_log.append("drop_while_manual_lock")
            return False
        if self.phase in (CompositionPhase.EMPTY, CompositionPhase.COMMITTED):
            self.diagnostic_log.append("drop_empty_phase")
            return False
        self.candidates = [candidate] + [
            c for c in self.candidates if c.output_text != candidate.output_text
        ]
        if self.phase == CompositionPhase.AUTO_COMPOSING:
            self.selected_index = 0
            self.visible_candidate_id = self.candidates[0].stable_candidate_id
        return True

    def simulate_keys(self, text: str) -> None:
        for ch in text:
            self.type(ch)

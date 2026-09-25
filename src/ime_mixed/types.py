from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Dict, List, Optional, Tuple


class SpanKind(str, Enum):
    EN = "en"
    JA = "ja"
    PROTECT = "protect"
    IDENT = "ident"
    PUNCT = "punct"


class CompositionPhase(str, Enum):
    EMPTY = "empty"
    AUTO_COMPOSING = "auto_composing"
    MANUAL_CONVERSION = "manual_conversion"
    MANUAL_OVERRIDE = "manual_override"
    COMMITTED = "committed"


class DisplayMode(str, Enum):
    LIVE = "live"
    READING_ONLY = "reading_only"


class SpaceAction(str, Enum):
    NONE = "none"
    START_CONVERSION = "start_conversion"
    NEXT_CANDIDATE = "next_candidate"
    PREV_CANDIDATE = "prev_candidate"
    INSERT_SPACE = "insert_space"


Range = Tuple[int, int]


@dataclass
class Span:
    raw_range: Range
    reading_range: Range
    output_range: Range
    kind: SpanKind
    raw_text: str = ""
    reading: str = ""
    output_text: str = ""

    def to_dict(self) -> Dict[str, Any]:
        return {
            "raw_range": list(self.raw_range),
            "reading_range": list(self.reading_range),
            "output_range": list(self.output_range),
            "kind": self.kind.value,
            "raw_text": self.raw_text,
            "reading": self.reading,
            "output_text": self.output_text,
        }


@dataclass
class EditOp:
    op: str
    raw_range: Optional[Range] = None
    detail: str = ""
    cost: float = 0.0

    def to_dict(self) -> Dict[str, Any]:
        return {
            "op": self.op,
            "raw_range": list(self.raw_range) if self.raw_range else None,
            "detail": self.detail,
            "cost": self.cost,
        }


@dataclass
class ScoreFeatures:
    en_score: float = 0.0
    jp_score: float = 0.0
    coverage: float = 0.0
    edit_cost: float = 0.0
    protect_score: float = 0.0
    context_score: float = 0.0
    completion_bonus: float = 0.0
    total: float = 0.0

    def to_dict(self) -> Dict[str, Any]:
        return {
            "en_score": round(self.en_score, 4),
            "jp_score": round(self.jp_score, 4),
            "coverage": round(self.coverage, 4),
            "edit_cost": round(self.edit_cost, 4),
            "protect_score": round(self.protect_score, 4),
            "context_score": round(self.context_score, 4),
            "completion_bonus": round(self.completion_bonus, 4),
            "total": round(self.total, 4),
        }


@dataclass
class Candidate:
    output_text: str
    spans: List[Span] = field(default_factory=list)
    edit_operations: List[EditOp] = field(default_factory=list)
    score_features: ScoreFeatures = field(default_factory=ScoreFeatures)
    source_revision: int = 0
    source_context_generation: int = 0
    source_interaction_generation: int = 0
    stable_candidate_id: str = ""
    is_raw: bool = False
    reading_text: str = ""

    def to_dict(self) -> Dict[str, Any]:
        return {
            "stable_candidate_id": self.stable_candidate_id,
            "output_text": self.output_text,
            "reading_text": self.reading_text,
            "is_raw": self.is_raw,
            "spans": [s.to_dict() for s in self.spans],
            "edit_operations": [e.to_dict() for e in self.edit_operations],
            "score_features": self.score_features.to_dict(),
            "source_revision": self.source_revision,
            "source_context_generation": self.source_context_generation,
            "source_interaction_generation": self.source_interaction_generation,
        }


@dataclass
class InputSnapshot:
    session_id: str = "s0"
    revision: int = 0
    context_generation: int = 0
    interaction_generation: int = 0
    phase: CompositionPhase = CompositionPhase.AUTO_COMPOSING
    display_mode: DisplayMode = DisplayMode.LIVE
    manual_override: bool = False
    visible_candidate_id: Optional[str] = None
    candidate_list_generation: int = 0
    raw_text: str = ""
    raw_cursor: int = 0
    left_context: str = ""
    right_context: str = ""
    field_hints: Dict[str, Any] = field(default_factory=dict)
    learning_allowed: bool = True
    punctuation_completion_enabled: bool = False
    candidate_limit: int = 8

    @property
    def field(self) -> str:
        return str(self.field_hints.get("field", "prose"))

    @property
    def phase_hint(self) -> str:
        return str(self.field_hints.get("phase", "end_of_phrase"))

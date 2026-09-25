from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple


@dataclass(frozen=True)
class WordEntry:
    reading: str
    surface: str
    cost: float


@dataclass(frozen=True)
class PhraseEntry:
    reading: str
    surface: str
    cost: float


WORDS: List[WordEntry] = [
    WordEntry("こんにちは", "こんにちは", 4.0),
    WordEntry("こんばんは", "こんばんは", 8.0),
    WordEntry("ありがとう", "ありがとう", 6.0),
    WordEntry("ございます", "ございます", 6.0),
    WordEntry("がっこう", "学校", 18.0),
    WordEntry("がこう", "学校", 32.0),
    WordEntry("きょう", "今日", 22.0),
    WordEntry("きんよう", "金曜", 28.0),
    WordEntry("は", "は", 2.0),
    WordEntry("わ", "は", 12.0),
    WordEntry("いい", "いい", 14.0),
    WordEntry("よい", "良い", 24.0),
    WordEntry("てんき", "天気", 18.0),
    WordEntry("です", "です", 2.0),
    WordEntry("ます", "ます", 3.0),
    WordEntry("ね", "ね", 2.0),
    WordEntry("を", "を", 2.0),
    WordEntry("で", "で", 2.0),
    WordEntry("に", "に", 2.0),
    WordEntry("が", "が", 2.0),
    WordEntry("と", "と", 2.0),
    WordEntry("も", "も", 2.0),
    WordEntry("の", "の", 4.0),
    WordEntry("よんで", "読んで", 18.0),
    WordEntry("よむ", "読む", 20.0),
    WordEntry("ください", "ください", 6.0),
    WordEntry("うごきます", "動きます", 18.0),
    WordEntry("うごく", "動く", 20.0),
    WordEntry("かくにん", "確認", 18.0),
    WordEntry("かく", "書く", 22.0),
    WordEntry("してください", "してください", 4.0),
    WordEntry("しる", "知る", 22.0),
    WordEntry("でき", "でき", 16.0),
    WordEntry("ひらがな", "ひらがな", 20.0),
    WordEntry("へんかん", "変換", 18.0),
    WordEntry("にほんご", "日本語", 16.0),
    WordEntry("にほん", "日本", 18.0),
    WordEntry("えいご", "英語", 18.0),
    WordEntry("めも", "メモ", 24.0),
    WordEntry("りぽじとり", "リポジトリ", 20.0),
    WordEntry("こうしん", "更新", 16.0),
    WordEntry("りれき", "履歴", 22.0),
    WordEntry("せいせき", "成果", 22.0),
    WordEntry("めい", "名", 26.0),
    WordEntry("なまえ", "名前", 18.0),
    WordEntry("てすと", "テスト", 18.0),
    WordEntry("あわせ", "合わせ", 24.0),
    WordEntry("つぎ", "次", 22.0),
    WordEntry("まえ", "前", 22.0),
    WordEntry("おわり", "終わり", 22.0),
    WordEntry("はじめ", "始め", 22.0),
    WordEntry("これ", "これ", 8.0),
    WordEntry("それ", "それ", 8.0),
    WordEntry("あれ", "あれ", 10.0),
    WordEntry("どれ", "どれ", 10.0),
    WordEntry("わたし", "私", 14.0),
    WordEntry("あなた", "あなた", 14.0),
    WordEntry("はい", "はい", 8.0),
    WordEntry("いいえ", "いいえ", 8.0),
    WordEntry("そうです", "そうです", 14.0),
    WordEntry("わかりました", "わかりました", 12.0),
    WordEntry("おねがい", "お願い", 14.0),
    WordEntry("しつもん", "質問", 18.0),
    WordEntry("こたえ", "答え", 18.0),
    WordEntry("はなす", "話す", 20.0),
    WordEntry("かくす", "書く", 22.0),
    WordEntry("よむ", "読む", 20.0),
    WordEntry("きく", "聞く", 20.0),
    WordEntry("みる", "見る", 18.0),
    WordEntry("いく", "行く", 18.0),
    WordEntry("くる", "来る", 18.0),
    WordEntry("する", "する", 8.0),
    WordEntry("ある", "ある", 8.0),
    WordEntry("いる", "いる", 8.0),
]

PHRASES: List[PhraseEntry] = [
    PhraseEntry("きょうはいいてんきですね", "今日はいい天気ですね", 8.0),
    PhraseEntry("こんにちは", "こんにちは", 3.0),
    PhraseEntry("ありがとうございます", "ありがとうございます", 5.0),
    PhraseEntry("りれきをよんでください", "履歴を読んでください", 20.0),
]

PARTICLE_INSERTS: List[Tuple[str, str, float]] = [
    ("は", "は", 24.0),
    ("を", "を", 24.0),
    ("が", "が", 26.0),
    ("に", "に", 26.0),
    ("で", "で", 26.0),
    ("と", "と", 28.0),
    ("も", "も", 28.0),
    ("や", "や", 30.0),
]

EN_WORDS = {
    "a", "an", "the", "and", "or", "but", "if", "then", "else", "for", "of", "to",
    "in", "on", "at", "by", "with", "from", "as", "is", "are", "was", "were", "be",
    "been", "am", "do", "does", "did", "not", "no", "yes", "please", "thanks",
    "hello", "world", "hi", "hey", "update", "repository", "repo", "readme",
    "file", "name", "path", "code", "test", "tests", "run", "make", "this", "that",
    "these", "those", "it", "its", "we", "you", "they", "he", "she", "i", "me",
    "my", "your", "our", "their", "here", "there", "when", "where", "what", "who",
    "how", "why", "all", "any", "some", "more", "most", "other", "into", "over",
    "under", "again", "once", "because", "so", "than", "too", "very", "can", "will",
    "just", "should", "now", "new", "old", "good", "bad", "nice", "day", "days",
    "time", "year", "years", "github", "gitlab", "python", "java", "javascript",
    "typescript", "windows", "linux", "macos", "english", "japanese", "text",
    "input", "output", "user", "users", "data", "list", "item", "items", "set",
    "get", "add", "remove", "delete", "create", "open", "close", "start", "stop",
    "check", "confirm", "cancel", "save", "load", "send", "receive", "build",
    "release", "version", "branch", "commit", "push", "pull", "merge", "issue",
    "please", "update", "the", "repository", "answer", "or", "nice", "day",
    "hello", "world", "print", "function", "value", "return", "class", "object",
    "string", "number", "boolean", "array", "map", "dict", "main", "index",
}


def build_word_index() -> Dict[str, List[WordEntry]]:
    idx: Dict[str, List[WordEntry]] = {}
    for w in WORDS:
        idx.setdefault(w.reading, []).append(w)
    return idx


WORD_INDEX = build_word_index()
MAX_WORD_LEN = max(len(w.reading) for w in WORDS)
PHRASE_MAX_LEN = max(len(p.reading) for p in PHRASES)

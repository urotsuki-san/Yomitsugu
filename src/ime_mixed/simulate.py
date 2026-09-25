from __future__ import annotations

import sys
from typing import List, Optional

from .session import ImeSession, SessionConfig


HELP = """\
対話シミュレータ（実機TSF前の入力フロー検証用）
  文字        入力（AUTO_COMPOSING / プレビュー更新）
  /space      Space（変換開始 or 次候補 or 英語空白）
  /prev       Shift+Space（前候補）
  /enter      Enter（可視文字列を確定）
  /esc        Escape（取消）
  /bs         Backspace
  /lit        原文をそのまま確定
  /list       候補一覧
  /show       現在の表示
  /left TEXT  左文脈設定
  /right TEXT 右文脈設定
  /field NAME field: prose|url|path|email|code|identifier
  /punct on|off  文末句点補完
  /phase end|sentence  フェーズ
  /crash      エンジン異常を再現
  /recover    エンジン復帰
  /log        確定・学習・診断ログ
  /clear      コンポジションとログをクリア
  /quit       終了
"""


def _render(session: ImeSession) -> str:
    vis = session.visible_text()
    phase = session.phase.value
    raw = session.raw_text
    lines = [
        f"phase={phase} rev={session.revision} ctx={session.context_generation} ig={session.interaction_generation}",
        f"raw     : {raw}",
        f"visible : {vis}",
    ]
    if session.candidates:
        lines.append("candidates:")
        for i, c in enumerate(session.candidates):
            mark = "*" if i == session.selected_index else " "
            raw_mark = "R" if c.is_raw else " "
            lines.append(f" {mark}{raw_mark} [{i}] {c.output_text}  total={c.score_features.total:.3f}")
    else:
        lines.append("candidates: (none)")
    return "\n".join(lines)


def run_interactive() -> int:
    session = ImeSession(
        SessionConfig(
            punctuation_completion_enabled=True,
            learning_allowed=True,
        )
    )
    print(HELP)
    print(_render(session))
    while True:
        try:
            line = input("ime> ")
        except (EOFError, KeyboardInterrupt):
            print()
            return 0
        if not line:
            continue
        if line.startswith("/"):
            parts = line.split(maxsplit=1)
            cmd = parts[0].lower()
            arg = parts[1] if len(parts) > 1 else ""
            if cmd in ("/quit", "/q", "/exit"):
                return 0
            elif cmd == "/space":
                session.press_space()
            elif cmd == "/prev":
                session.press_shift_space()
            elif cmd == "/enter":
                session.press_enter()
            elif cmd == "/esc":
                session.press_escape()
            elif cmd == "/bs":
                session.backspace()
            elif cmd == "/lit":
                session.choose_literal()
            elif cmd == "/list":
                pass
            elif cmd == "/show":
                pass
            elif cmd == "/left":
                session.set_context(left=arg, right=session.right_context)
            elif cmd == "/right":
                session.set_context(left=session.left_context, right=arg)
            elif cmd == "/field":
                session.set_field(arg.strip() or "prose")
            elif cmd == "/punct":
                session.set_punctuation_completion(arg.strip().lower() in ("on", "1", "true"))
            elif cmd == "/phase":
                phase = "sentence_end" if arg.strip().lower() in ("sentence", "sentence_end") else "end_of_phrase"
                session.field_hints["phase"] = phase
            elif cmd == "/crash":
                session.simulate_worker_crash()
            elif cmd == "/recover":
                session.simulate_worker_recover()
            elif cmd == "/log":
                print(f"committed={session.committed_history}")
                print(f"output={session.output_log}")
                print(f"learning={session.learning_log}")
                print(f"keys={session.pressed_keys}")
                print(f"diag={session.diagnostic_log}")
                continue
            elif cmd == "/clear":
                session = ImeSession(
                    SessionConfig(
                        punctuation_completion_enabled=session.config.punctuation_completion_enabled,
                        learning_allowed=session.config.learning_allowed,
                    )
                )
                print("cleared")
                print(_render(session))
                continue
            elif cmd == "/help":
                print(HELP)
                continue
            else:
                print(f"unknown command {cmd}")
                continue
        else:
            session.type(line)
        print(_render(session))


def main(argv: Optional[List[str]] = None) -> int:
    return run_interactive()


if __name__ == "__main__":
    sys.exit(main())

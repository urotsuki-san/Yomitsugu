"""Ensure typed and provisional spellings stay consistent through the real worker protocol."""
import json
import os
from pathlib import Path
import struct
import sys

from context_quality import Engine


def frames(engine, request):
    payload = json.dumps({"version": 1, "field": "prose", "limit": 16, **request}).encode()
    frame = struct.pack("<I", len(payload)) + payload
    assert os.write(engine.write, frame) == len(frame)
    packets = []
    while True:
        size = struct.unpack("<I", engine.receive(4))[0]
        assert 0 < size <= 1024 * 1024
        packet = json.loads(engine.receive(size))
        packets.append(packet)
        if not isinstance(packet, dict) or "preview" not in packet:
            return packets


def conversion(engine, raw, left=""):
    common = {"raw": raw, "left": left}
    typed, = frames(engine, {**common, "phase": "typing", "dictionary_preview": True})
    packets = frames(engine, {**common, "phase": "end_of_phrase", "allow_preview": True, "max_previews": 3})
    interim = [p["preview"][0]["text"] for p in packets[:-1]]
    final_rows, = frames(engine, {**common, "phase": "end_of_phrase"})
    assert packets[-1] == final_rows, (raw, "subscribing changed the final candidates")
    return typed[0]["text"], interim, [c["text"] for c in final_rows]


def main(host):
    engine = Engine(host, "automatic")
    profile = Path(engine.profile.name)
    try:
        (profile / "settings.json").write_text(json.dumps({"conversion_model": "automatic", "learning_enabled": True}), encoding="utf-8")
        # The last n repeats the same dictionary reading in an adjacent edit.
        # Check both representations and invalidate the match on either context edge.
        for first, last, left, right, want in [
            ("kinen", "kinenn", "", "", "記念"),
            ("genin", "geninn", "", "", "原因"),
            ("genin", "geninn", "昔の下人は、", "", "下人"),
            ("genin", "geninn", "", "昔の下人は、", "下人"),
        ]:
            first_rows, = frames(engine, {"raw": first, "phase": "typing", "dictionary_preview": True})
            assert first_rows[0]["text"] == ("記念" if first == "kinen" else "原因")
            last_rows, = frames(engine, {"raw": last, "phase": "typing", "left": left, "right": right, "dictionary_preview": True})
            assert last_rows[0]["text"] == want, (first, last, left, right, last_rows)
        for raw, want in [("geninn", "原因"), ("genin", "原因"), ("teninn", "店員"), ("geninnwoshiraberu", "原因を調べる")]:
            typed, interim, final = conversion(engine, raw)
            assert interim and [typed, *interim, final[0]] == [want] * (len(interim) + 2), (raw, typed, interim, final)
            assert raw in final
        typed, interim, final = conversion(engine, "geninn", "昔の下人は、")
        assert typed == final[0] == "下人" and "原因" not in interim and "原因" in final
        typed, _, _ = conversion(engine, "kanin")
        assert typed != "完飲", "a delayed typing response could contaminate the rest of kaninchen"
        for incomplete, completed in [("tenin", "店員"), ("senin", "船員")]:
            _, interim, final = conversion(engine, incomplete)
            assert interim and all(text == completed for text in interim) and final[0] == completed
        typed, interim, final = conversion(engine, "kaninchen")
        assert final[0] == "カニンチェン" and "完飲ちぇん" not in [typed, *interim]
        learned, = frames(engine, {"operation": "learn", "phase": "end_of_phrase", "raw": "geninn",
                                   "chosen": "下人", "previous": "原因", "explicit": True})
        assert learned["learned"]
        typed, interim, final = conversion(engine, "geninn")
        assert typed == final[0] == "下人" and "原因" not in interim
        (profile / "user_dictionary.tsv").write_text("げにん\t現人\t名詞\n", encoding="utf-8")
        typed, interim, final = conversion(engine, "geninn")
        assert typed == final[0] == "現人" and "原因" not in interim
    finally:
        engine.close()


if __name__ == "__main__":
    main(Path(sys.argv[1]))

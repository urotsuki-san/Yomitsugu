"""Create local source files for the updater's history and recovery test."""
import gzip
from pathlib import Path
import sys


def write_sources(dictionary, directory):
    directory.mkdir(parents=True, exist_ok=True)
    lexical = 0
    with (dictionary.open(encoding="utf-8") as entries,
          gzip.open(directory / "edict2u.gz", "wt", encoding="utf-8") as glossary,
          (directory / "symbol.tsv").open("w", encoding="utf-8") as symbols):
        symbols.write("category\tsymbol\treading\n")
        for line in entries:
            if line.startswith("#"):
                continue
            reading, word, pos = line.rstrip("\r\n").split("\t")
            if pos == "記号":
                symbols.write(f"symbol\t{word}\t{reading}\n")
            elif pos == "名詞":
                glossary.write(f"{word} [{reading}] /(n) {{comp}} {word}/\n")
            elif pos == "補助語" and lexical < 30000:
                glossary.write(f"{word} [{reading}] /(n) example/\n")
                lexical += 1


if __name__ == "__main__":
    write_sources(Path(sys.argv[1]), Path(sys.argv[2]))

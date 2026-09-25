"""Data-format acceptance checks, independent of the native candidate ranker."""
import importlib.util
from pathlib import Path

spec=importlib.util.spec_from_file_location('dictionary_updater',Path(__file__).resolve().parents[1]/'scripts/update_public_dictionary.py')
updater=importlib.util.module_from_spec(spec)
spec.loader.exec_module(updater)

def test_loanword_surface_is_not_the_english_definition():
    assert list(updater.lexical_entries('アンインストール','(n,vs) uninstalling/')) == [('あんいんすとーる','アンインストール','補助語')]

def test_reading_restrictions_prevent_cross_product_errors():
    rows=list(updater.lexical_entries('今日;今朝 [きょう(今日);けさ(今朝)]','(n) today/morning/'))
    assert {(r,w) for r,w,_ in rows} == {('きょう','今日'),('けさ','今朝')}

def test_irregular_forms_and_obsolete_entries_are_not_correction_targets():
    assert list(updater.lexical_entries('テスト(ik)','(n) test/')) == []
    assert list(updater.lexical_entries('古語 [こご]','(n) (obs) old word/')) == []

def test_unmarked_noun_is_available_without_common_frequency_flag():
    assert ('あんにん','杏仁','補助語') in list(updater.lexical_entries('杏仁 [あんにん;きょうにん]','(n) apricot kernel/'))

def test_inflection_is_left_to_the_main_dictionary():
    assert list(updater.lexical_entries('歩む [あゆむ]','(v5m,vi) walk/')) == []

def test_kanji_source_with_supplementary_characters_remains_intact():
    assert list(updater.lexical_entries('𠮷野 [よしの]','(n) place/')) == [('よしの','𠮷野','補助語')]

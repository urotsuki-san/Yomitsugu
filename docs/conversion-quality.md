# 0.2.8の変換改善と測定

`aninsuto-ru` が変換できないという報告を受け、単語の追加だけでは直らない箇所を調べた。

## 見つかった問題と変更

- ハイフンの長音を語の境界として分割し、分割した分だけ点数が増えていた。「インストール」「コンピューター」は本辞書に存在するのに、末尾がひらがなになる原因だった。長音・`n'` の途中での分割をやめた。
- ローマ字表に `we` などの規則が欠けていた。Mozcの固定リビジョンから323行の表を生成し、拡張音と次の打鍵へ持ち越す子音を扱うようにした。通常の `ni` と `n'i` の区別は維持する。
- 従来の公開辞書は記号・英語略語が中心だった。EDRDG EDICT2の名詞、一般的な語、外来語の日本語見出しと読みも取り込む。読みの使用制限、旧表記の注記、重複を処理する。
- 補正候補は、ローマ字として成立した最初の編集を選ぶだけだった。追加した補正経路では、`n` の区切り、重複、隣接文字の入れ替え、1文字の抜け・置換、長音位置のずれをまず辞書の索引で照合する。候補となる読みが見つかった後で必要な後続部分だけを変換する。
- 複数の修正が考えられる入力は、元の変換を先頭に残して補正を候補として提示する。辞書に一致する読みは補正対象から除外する。原文候補は残り、Backspaceは元の打鍵を編集する。
- 古い記号中心の更新キャッシュが新しい同梱辞書を隠さないよう、更新データの形式を識別する。更新器はWindows標準のPowerShell/.NETで動き、Pythonは不要。
- 旧EUC-JP版を.NETで読むと一部の漢字が文字化けした。公式のUTF-8版へ切り替え、Python生成器とWindows生成器の出力SHA-256一致を確認した。

公開辞書は **176,481読み・277,889候補**、約12.0 MB。候補には異表記や本辞書との重複がある。「追加27万語」とは数えない。本辞書は引き続きAzooKeyのものを使う。入力文を送信するAPIは使わない。

## 測定

[85例の固定入力](../native/tests/conversion_cases.tsv)を実エンジンで順に評価した。候補の全文一致を要求し、部分一致を成功に数えない。比較元は0.2.7のコードと旧公開辞書。以下はこの入力集合での結果であり、日常入力全体の正解率ではない。

| 種類 | 件数 | 修正前の先頭 / 5候補以内 | 修正後の先頭 / 5候補以内 |
|---|---:|---:|---:|
| 外来語 | 31 | 17 / 17 | 31 / 31 |
| タイポ・非標準綴り | 23 | 0 / 0 | 6 / 20 |
| 一般語 | 16 | 14 / 15 | 14 / 16 |
| 文章 | 7 | 2 / 3 | 6 / 7 |
| 英日混在 | 3 | 3 / 3 | 3 / 3 |
| 原文・読みの保持 | 5 | 5 / 5 | 5 / 5 |

同じPCで初回変換を除いた中央値は50.0→21.1 ms、p95は177.2→58.9 ms。辞書の初期読み込み、描画、TSFや入力先アプリを含む応答時間ではない。計測値と入力分類の修正内容は[結果JSON](conversion-quality-results.json)に記録した。

既存の品質試験46/46、`engine_test`成功、拡張した`release_test`130/130、pytest17/17、Python受入32/32。`release_test`には1文字ずつ入力する5系列、補正後の削除、保護対象の入力欄、旧辞書キャッシュからの移行を含む。デスクトップを操作するE2Eは今回実行していない。

```powershell
python scripts/evaluate_conversion.py --output audit/conversion-quality.json --check
```

`--check`は、測定で得た最低件数と主要な報告例の先頭候補をCIで検査する。すべての評価例が合格したという意味ではない。

## 残る問題

`fuo-ruda-`、`ake-se-sari-`、`downro-do`は期待した語を5候補以内に出せない。複数編集や英語綴りの混入を広く許すと、正しい単語への誤補正も増えるため、今回は探索を制限している。

「鳥たちが散開する」は候補に残るが、先頭では「散会」が選ばれる。文脈による同音語の順位付けは引き続き必要。前後文脈をZenzaiへ渡す対応、文節編集、実アプリ横断試験も未完了。

## 調べた研究・OSSと採用範囲

- [Mozcのローマ字表](https://github.com/google/mozc/blob/b9c3fcbd6d76b19649ef572324fa9da2559bc18e/src/data/preedit/romanji-hiragana.tsv)：固定版のデータを取り込んだ。BSD-3-ClauseのNOTICEを同梱。
- [Mozc KeyCorrector](https://github.com/google/mozc/blob/b9c3fcbd6d76b19649ef572324fa9da2559bc18e/src/converter/key_corrector.cc)：撥音や促音を補正対象として扱い、補正にはペナルティを付ける設計を参照。クラスのコードは移植していない。
- [EDRDGの利用条件](https://www.edrdg.org/edrdg/licence.html)と[公式UTF-8 EDICT2](https://www.edrdg.org/pub/Nihongo/edict2u.gz)：日本語見出しを追加。派生辞書のEDRDG由来部分はCC BY-SA 4.0。ソフトウェア全体のMIT表記で置き換えない。
- [AzooKeyKanaKanjiConverter](https://github.com/azooKey/AzooKeyKanaKanjiConverter)：既存の本変換器として継続使用。上流には予測やタイポ補正もあるが、今回それらを無条件に有効化したわけではない。
- [SymSpell](https://github.com/wolfgarbe/SymSpell)：辞書候補の検索と補正順位の分離を調査。今回の実装は有界の編集生成と索引照合であり、SymSpell本体や頻度データの導入ではない。
- [Building a Japanese Typo Dataset from Wikipedia’s Revision History](https://aclanthology.org/2020.acl-srw.31/)：日本語の誤りを一種類として評価しないための調査資料。論文のデータや学習済みモデルは今回の配布物へ取り込んでいない。
- [SudachiDict](https://github.com/WorksApplications/SudachiDict)も確認した。[LEGAL](https://github.com/WorksApplications/SudachiDict/blob/develop/LEGAL)にはUniDic・NEologd由来データの条件がある。変換用の品詞・コストへの対応を検証していないため、今回は未採用。

将来の辞書追加も、読みの対応、候補順位、誤補正、速度と出典を確認してから反映する。

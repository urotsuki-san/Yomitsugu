# Third-party components and data

Yomitsugu's original code is MIT licensed. The components below retain their own copyrights and licenses; the root `LICENSE` does not relicense them. This inventory records the current source and preview-package inputs. It is not a blanket clearance for every binary produced by the toolchain.

| Component | Use | License / source | Local notice |
|---|---|---|---|
| [myime](https://github.com/unok/myime) at `a8486eca5312556ff88fed7f1850a28843b67977` | conversion-engine integration and Windows build | MIT; its tree also includes other licensed components | `upstream/myime/LICENSE` in the build checkout; copied to package `licenses/myime-LICENSE` |
| [AzooKeyKanaKanjiConverter](https://github.com/azooKey/AzooKeyKanaKanjiConverter) | kana-kanji converter | MIT | upstream `LICENSE`; include in binary package |
| [azooKey_dictionary_storage](https://github.com/azooKey/azooKey_dictionary_storage) | main dictionary | Apache-2.0 | upstream `LICENSE`; copied to package `licenses/azookey-dictionary-LICENSE` |
| [azooKey_emoji_dictionary_storage](https://github.com/azooKey/azooKey_emoji_dictionary_storage) | emoji lookup data in the AzooKey resource bundle | generated from Mozc emoji data and Unicode emoji/CLDR data | upstream `README.md` and `data/README.md`; package includes those provenance notes plus Mozc and Unicode notices |
| [Zenzai v3.2 small GGUF](https://huggingface.co/Miwa-Keita/zenz-v3.2-small-gguf) at revision `c67e03e07d215c869f591b274c1631170d3e11fe` | optional local model, SHA-256 `29c223d4c23327b80fd13ebb5ab2555057a46317997d5da391584ffbef0db673` | model repository declares Apache-2.0 | identify model and include Apache-2.0 text in binary package |
| [llama.cpp](https://github.com/ggml-org/llama.cpp) | model runtime used by myime | MIT | include in binary package |
| [Swift runtime and Foundation](https://www.swift.org/) | Windows Swift, Foundation and Dispatch DLLs used by the isolated engine | Apache-2.0 with Swift Runtime Library Exception; Unicode data inside Foundation follows the Unicode License | package includes `licenses/swift-LICENSE` and `licenses/unicode-LICENSE` |
| Microsoft Visual C++ runtime | `vcruntime`, `msvcp`, `concrt` DLLs | Microsoft redistributable terms for the licensed Visual Studio toolchain | only release DLLs named in Microsoft's [redistributable list](https://learn.microsoft.com/en-us/cpp/windows/determining-which-dlls-to-redistribute?view=msvc-170) are included |
| [nlohmann/json](https://github.com/nlohmann/json) | JSON parser | MIT | `native/third_party/nlohmann/LICENSE.MIT` |
| [Mozc `symbol.tsv`](https://github.com/google/mozc/blob/master/src/data/symbol/symbol.tsv) | symbol entries in the derived public dictionary | Mozc repository lists BSD-3-Clause for Google-authored code/data; preserve accompanying notice | `native/third_party/public-dictionary-NOTICE.txt` |
| [EDRDG EDICT2](https://www.edrdg.org/pub/Nihongo/edict2.gz) | computing terms in the derived public dictionary | CC BY-SA 4.0; attribution and ShareAlike apply to derived dictionary entries | `native/third_party/public-dictionary-NOTICE.txt` |

The generated `native/assets/public_dictionary.tsv` combines separately attributed source rows. The EDRDG-derived portion is offered under CC BY-SA 4.0. The software code is not automatically subject to that data license. The dictionary update fetches only public source files; typed input and the user's dictionary are not uploaded.

The package carries the notices found for the DLLs, model and dictionary assets above. A fresh-machine installation, upgrade and uninstall, and compatibility across real applications still need testing before calling the binary a stable public release. Source publication and permission to redistribute a built package are separate checks.

#include "ime_engine.h"
#include <cstdio>
#include <string>
using namespace ime;
int main() {
  AzookeyEnsureReady();
  const char* keys[] = {"だいじょうぶです", "ちゅう", "しんだい",
                        "きしょう", "ときょ", "せんせい", "がくせい",
                        "あさのまでいそがしいでした", "ほんとわにおもしろかった",
                        "すこです", "これほかんじです", "まいにてのましよう"};
  for (const char* k : keys) {
    auto v = AzookeyConvert(k, 8);
    std::printf("KEY %s n=%zu\n", k, v.size());
    for (size_t i = 0; i < v.size(); ++i) std::printf("  [%zu] %s\n", i, v[i].c_str());
  }
  return 0;
}

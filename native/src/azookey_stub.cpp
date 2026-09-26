#include "azookey_bridge.h"
// Swift・辞書・モデルは専用の変換プロセスで読み込む。
namespace ime {
bool AzookeyEnsureReady() { return false; }
bool AzookeyAvailable() { return false; }
std::vector<std::string> AzookeyConvert(const std::string&, int, const std::string&, const std::string&, bool) { return {}; }
void AzookeySetZenzai(bool, const std::string&, int) {}
std::string AzookeyZenzaiStatus() { return "{}"; }
bool AzookeySetUserDictionary(const std::string&) { return false; }
}

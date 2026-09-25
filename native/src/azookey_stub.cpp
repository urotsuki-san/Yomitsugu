#include "azookey_bridge.h"
// The in-process TIP must never load Swift, its bundles, or the language model.
namespace ime {
bool AzookeyEnsureReady() { return false; }
bool AzookeyAvailable() { return false; }
std::vector<std::string> AzookeyConvert(const std::string&, int) { return {}; }
void AzookeySetZenzai(bool, const std::string&, int) {}
std::string AzookeyZenzaiStatus() { return "{}"; }
bool AzookeySetUserDictionary(const std::string&) { return false; }
}

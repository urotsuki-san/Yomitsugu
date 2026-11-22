#include "../src/settings.cpp"
int wmain(int argc,wchar_t** argv) {
  if (argc!=2) return 2;
  user_dir=argv[1]; base=OwnDirectory();
  std::filesystem::create_directories(user_dir);
  ime::LearningStore settings(user_dir);
  if (settings.model()!=ime::ConversionModel::Automatic) return 3;
  wchar_t empty[]={0};
  return RunSettings(GetModuleHandleW(nullptr),empty,SW_SHOWNORMAL);
}

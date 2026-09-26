#include "dictionary_status.h"
#include "user_dictionary.h"
#include <windows.h>
#include <fstream>
#include <cstdio>
#include "nlohmann/json.hpp"

namespace ime {
namespace {
nlohmann::json Metadata(const std::filesystem::path& path) {
  std::error_code ec;
  if(std::filesystem::file_size(path,ec)>8192 || ec) return {};
  std::ifstream stream(path,std::ios::binary);
  auto json=nlohmann::json::parse(stream,nullptr,false);
  return json.is_object()?json:nlohmann::json();
}
std::string Field(const nlohmann::json& json,const char* key) {
  return json.contains(key) && json[key].is_string()?json[key].get<std::string>():"";
}
}
std::wstring DictionaryTimeLabel(const std::string& utc) {
  unsigned year,month,day,hour,minute,second;
  if(utc.size()!=20 || utc.back()!='Z' || std::sscanf(utc.c_str(),"%4u-%2u-%2uT%2u:%2u:%2uZ",&year,&month,&day,&hour,&minute,&second)!=6 ||
      year<2000 || year>2100 || month<1 || month>12 || day<1 || day>31 || hour>23 || minute>59 || second>59) return L"記録なし";
  SYSTEMTIME time{};time.wYear=static_cast<WORD>(year);time.wMonth=static_cast<WORD>(month);time.wDay=static_cast<WORD>(day);
  time.wHour=static_cast<WORD>(hour);time.wMinute=static_cast<WORD>(minute);time.wSecond=static_cast<WORD>(second);
  FILETIME file{},local_file{};SYSTEMTIME local{};
  if(!SystemTimeToFileTime(&time,&file) || !FileTimeToLocalFileTime(&file,&local_file) || !FileTimeToSystemTime(&local_file,&local)) return L"記録なし";
  wchar_t result[32]{};
  swprintf_s(result,L"%04u/%02u/%02u %02u:%02u",local.wYear,local.wMonth,local.wDay,local.wHour,local.wMinute);
  return result;
}
DictionaryStatus ReadDictionaryStatus(const std::filesystem::path& bundled,const std::filesystem::path& cached) {
  DictionaryStatus result;
  const auto active=PublicDictionaryPath(bundled,cached);
  result.bundled=active==bundled;
  auto path=active;path.replace_extension(L".sources.json");
  const auto metadata=Metadata(path);
  if(!result.bundled && !Field(metadata,"output_sha256").empty()) {
    auto bundled_metadata_path=bundled;bundled_metadata_path.replace_extension(L".sources.json");
    result.bundled=Field(metadata,"output_sha256")==Field(Metadata(bundled_metadata_path),"output_sha256");
  }
  if(metadata.contains("entries") && metadata["entries"].is_number_unsigned() && metadata["entries"].get<unsigned long long>()<=700000)
    result.entries=metadata["entries"].get<unsigned>();
  result.updated_at=Field(metadata,"updated_at_utc");
  result.checked_at=Field(metadata,"checked_at_utc");
  result.result=Field(metadata,"update_result");
  if(result.result!="updated" && result.result!="unchanged") result.result="unknown";
  if(DictionaryTimeLabel(result.checked_at)==L"記録なし") result.result="unknown";
  auto status_path=cached;status_path.replace_extension(L".status.json");
  const auto status=Metadata(status_path);
  const auto attempt=Field(status,"checked_at_utc");
  if(status.is_object() && status.value("format_version",nlohmann::json())==1 && Field(status,"result")=="failed" &&
      DictionaryTimeLabel(attempt)!=L"記録なし" && attempt>=result.checked_at) {
    result.checked_at=attempt;result.result="failed";
  }
  return result;
}
}

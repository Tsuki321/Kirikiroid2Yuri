#include "IndividualConfigManager.h"
#include "StorageImpl.h"
#include "LocaleConfigManager.h"
#include "Platform.h"

#define FILENAME "Kirikiroid2Preference.xml"

IndividualConfigManager* IndividualConfigManager::GetInstance() {
	static IndividualConfigManager instance;
	return &instance;
}

std::string IndividualConfigManager::GetFilePath() {
	return CurrentPath;
}

void IndividualConfigManager::Clear()
{
	AllConfig.clear();
	CustomArguments.clear();
	KeyMap.clear();
	ConfigUpdated = false;
	CurrentPath.clear();
	PreferencePath.clear();
	LoadStatus = "Not loaded";
}

bool IndividualConfigManager::CheckExistAt(const std::string &folder) {
	std::string fullpath = folder + "/" FILENAME;
	return TVPCheckExistentLocalFile(ttstr(fullpath));
}

bool IndividualConfigManager::CreatePreferenceAt(const std::string &folder) {
	std::string fullpath = folder + "/" FILENAME;
// 	FILE *fp =
// #ifdef _MSC_VER
// 		_wfopen(ttstr(fullpath).c_str(), TJS_W("w"));
// #else
// 		fopen(fullpath.c_str(), "w");
// #endif
	Clear();
// 	if (!fp) {
// 		TVPShowSimpleMessageBox(
// 			LocaleConfigManager::GetInstance()->GetText("cannot_create_preference"),
// 			LocaleConfigManager::GetInstance()->GetText("readonly_storage"));
// 		return false;
// 	}
	CurrentPath = fullpath;
	PreferencePath = fullpath;
	LoadStatus = "New preference file";
	return true;
}

bool IndividualConfigManager::UsePreferenceAt(const std::string &folder, bool reload)
{
	std::string fullpath = folder;
	while (fullpath.size() > 1 && (fullpath.back() == '/' || fullpath.back() == '\\')) fullpath.pop_back();
	fullpath += "/" FILENAME;
	if (!reload && CurrentPath == fullpath) return true;
	Clear();
	PreferencePath = fullpath;
	if (!TVPCheckExistentLocalFile(ttstr(fullpath))) {
		LoadStatus = "Not found";
		return false;
	}
	CurrentPath = fullpath;
	const bool loaded = Initialize();
	if (!loaded) CurrentPath.clear();
	return loaded;
}

template<>
bool IndividualConfigManager::GetValue<bool>(const std::string &name, const bool &defVal /*= false*/)
{
	return inherit::GetValue<bool>(name, GlobalConfigManager::GetInstance()->GetValue<bool>(name, defVal));
}
template<>
int IndividualConfigManager::GetValue<int>(const std::string &name, const int &defVal /*= 0*/)
{
	return inherit::GetValue<int>(name, GlobalConfigManager::GetInstance()->GetValue<int>(name, defVal));
}
template<>
float IndividualConfigManager::GetValue<float>(const std::string &name, const float &defVal /*= 0*/)
{
	return inherit::GetValue<float>(name, GlobalConfigManager::GetInstance()->GetValue<float>(name, defVal));
}
template<>
std::string IndividualConfigManager::GetValue<std::string>(const std::string &name, const std::string &defVal /*= ""*/)
{
	return inherit::GetValue<std::string>(name, GlobalConfigManager::GetInstance()->GetValue<std::string>(name, defVal));
}

std::vector<std::string> IndividualConfigManager::GetCustomArgumentsForPush()
{
	std::vector<std::string> arguments = inherit::GetCustomArgumentsForPush();
	const auto defaults = GlobalConfigManager::GetInstance()->GetCustomArgumentsForPush();
	// First occurrence wins in TVPGetCommandLine, just like typed preferences.
	arguments.insert(arguments.end(), defaults.begin(), defaults.end());
	return arguments;
}

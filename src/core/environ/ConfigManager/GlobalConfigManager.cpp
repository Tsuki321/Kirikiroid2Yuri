#include "GlobalConfigManager.h"
#include "tinyxml2/tinyxml2.h"
#include "platform/CCFileUtils.h"
#include "Platform.h"
#include "UtilStreams.h"
#include "LocaleConfigManager.h"
#include <vector>
#include <cstring>
#ifdef __ANDROID__
#include "android/AndroidStorage.h"
#include <unistd.h>
#endif

bool TVPWriteDataToFile(const ttstr &filepath, const void *data, unsigned int len);
class XMLMemPrinter : public tinyxml2::XMLPrinter {
	tTVPMemoryStream _stream;
	char _buffer[4096];
public:
    virtual void Print(const char* format, ...) {
        va_list args, copy;
        va_start(args, format);
        va_copy(copy, args);
        int length = vsnprintf(_buffer, sizeof(_buffer), format, copy);
        va_end(copy);
        if (length >= 0 && static_cast<size_t>(length) < sizeof(_buffer)) {
            va_end(args);
            _stream.Write(_buffer, length);
        } else if (length >= 0) {
            std::vector<char> buffer(static_cast<size_t>(length) + 1);
            vsnprintf(buffer.data(), buffer.size(), format, args);
            va_end(args);
            _stream.Write(buffer.data(), length);
        } else va_end(args);
    }
	void SaveFile(const std::string &path) {
		if (!TVPWriteDataToFile(path, _stream.GetInternalBuffer(), _stream.GetSize())) {
			TVPShowSimpleMessageBox(
				LocaleConfigManager::GetInstance()->GetText("cannot_create_preference"),
				LocaleConfigManager::GetInstance()->GetText("readonly_storage"));
		}
	}
};


GlobalConfigManager::GlobalConfigManager() {
	Initialize();
}

GlobalConfigManager* GlobalConfigManager::GetInstance() {
	static GlobalConfigManager instance;
	return &instance;
}

bool iSysConfigManager::Initialize() {
	AllConfig.clear();
    CustomArguments.clear();
    KeyMap.clear();
	ConfigUpdated = false;

	tinyxml2::XMLDocument doc;

	FILE *fp = nullptr;
#ifdef _MSC_VER
	fp = _wfopen(ttstr(GetFilePath()).c_str(), TJS_W("rb"));
#else
	fp = fopen(GetFilePath().c_str(), "rb");
#ifdef __ANDROID__
    if (!fp) {
        int fd = TVPOpenDocumentFile(GetFilePath(), TJS_BS_READ);
        if (fd >= 0) { fp = fdopen(fd, "rb"); if (!fp) close(fd); }
    }
#endif
#endif

	if (!fp) {
		LoadStatus = "Could not read the preference file";
		return false;
	}
	// Document providers may return pipes, but XMLDocument::LoadFile seeks.
	std::string contents;
	char buffer[4096];
	size_t count;
	const size_t maxBytes = 1024 * 1024;
	while ((count = fread(buffer, 1, sizeof(buffer), fp)) != 0) {
		contents.append(buffer, count);
		if (contents.size() > maxBytes) break;
	}
	const bool readFailed = ferror(fp) != 0;
	fclose(fp);
	if (readFailed || contents.size() > maxBytes) {
		LoadStatus = readFailed ? "Could not read the preference file" : "Preference file is too large";
		return false;
	}
	if (doc.Parse(contents.c_str(), contents.size()) != tinyxml2::XML_SUCCESS ||
		!doc.RootElement() || strcmp(doc.RootElement()->Name(), "GlobalPreference") != 0) {
		LoadStatus = "Invalid preference XML (expected GlobalPreference)";
		return false;
	}
	{
		tinyxml2::XMLElement *rootElement = doc.RootElement();
		if (rootElement) {
			for (tinyxml2::XMLElement *item = rootElement->FirstChildElement("Item"); item; item = item->NextSiblingElement("Item")) {
				const char *key = item->Attribute("key");
				const char *val = item->Attribute("value");
				if (key && val) {
					AllConfig[key] = val;
				}
			}
			for (tinyxml2::XMLElement *item = rootElement->FirstChildElement("Custom"); item; item = item->NextSiblingElement("Custom")) {
				const char *key = item->Attribute("key");
				const char *val = item->Attribute("value");
				if (key && val) {
					CustomArguments.emplace_back(key, val);
				}
			}
			for (tinyxml2::XMLElement *item = rootElement->FirstChildElement("KeyMap"); item; item = item->NextSiblingElement("KeyMap")) {
				int key, val;
				if (tinyxml2::XML_SUCCESS == item->QueryIntAttribute("key", &key) &&
					tinyxml2::XML_SUCCESS == item->QueryIntAttribute("value", &val) &&
					key && val) {
					KeyMap.emplace(key, val);
				}
			}
		}
	}
	LoadStatus = "Loaded";
	return true;
}

void iSysConfigManager::SaveToFile() {
	if (!ConfigUpdated) return;
	std::string filepath = GetFilePath();
	if (filepath.empty()) return;
	tinyxml2::XMLDocument doc;
	doc.LinkEndChild(doc.NewDeclaration());
	tinyxml2::XMLElement *rootElement = doc.NewElement("GlobalPreference");
	for (auto it = AllConfig.begin(); it != AllConfig.end(); ++it) {
		tinyxml2::XMLElement *item = doc.NewElement("Item");
		item->SetAttribute("key", it->first.c_str());
		item->SetAttribute("value", it->second.c_str());
		rootElement->LinkEndChild(item);
	}
	for (auto it = CustomArguments.begin(); it != CustomArguments.end(); ++it) {
		tinyxml2::XMLElement *item = doc.NewElement("Custom");
		item->SetAttribute("key", it->first.c_str());
		item->SetAttribute("value", it->second.c_str());
		rootElement->LinkEndChild(item);
	}
	for (auto &it : KeyMap) {
		if (it.first && it.second) {
			tinyxml2::XMLElement *item = doc.NewElement("KeyMap");
			item->SetAttribute("key", it.first);
			item->SetAttribute("value", it.second);
			rootElement->LinkEndChild(item);
		}
	}
	doc.LinkEndChild(rootElement);
	XMLMemPrinter stream;
	doc.Print(&stream);
	stream.SaveFile(GetFilePath());
	ConfigUpdated = false;
}

bool iSysConfigManager::IsValueExist(const std::string &name)
{
	auto it = AllConfig.find(name);
	return it != AllConfig.end();
}

std::string GlobalConfigManager::GetFilePath() {
	return TVPGetInternalPreferencePath() + "GlobalPreference.xml";
}

template<>
bool iSysConfigManager::GetValue<bool>(const std::string &name, const bool& defVal) {
	return !!GetValue<int>(name, defVal);
}

template<>
int iSysConfigManager::GetValue<int>(const std::string &name, const int& defVal) {
	auto it = AllConfig.find(name);
	if (it == AllConfig.end()) {
		SetValueInt(name, defVal);
		return defVal;
	}
	return atoi(it->second.c_str());
}

template<>
float iSysConfigManager::GetValue<float>(const std::string &name, const float& defVal) {
	auto it = AllConfig.find(name);
	if (it == AllConfig.end()) {
		SetValueFloat(name, defVal);
		return defVal;
	}
	return atof(it->second.c_str());
}

template<>
std::string iSysConfigManager::GetValue<std::string>(const std::string &name, const std::string& defVal) {
	auto it = AllConfig.find(name);
	if (it == AllConfig.end()) {
		SetValue(name, defVal);
		return defVal;
	}
	return it->second;
}

void iSysConfigManager::SetValueInt(const std::string &name, int val) {
	char buf[16];
	sprintf(buf, "%d", val);
	AllConfig[name] = buf;
	ConfigUpdated = true;
}

void iSysConfigManager::SetValueFloat(const std::string &name, float val) {
	char buf[24];
	sprintf(buf, "%g", val);
	AllConfig[name] = buf;
	ConfigUpdated = true;
}

void iSysConfigManager::SetValue(const std::string &name, const std::string & val) {
	AllConfig[name] = val;
	ConfigUpdated = true;
}

void iSysConfigManager::SetKeyMap(int k/* 0 means remove */, int v)
{
	if (v == 0) {
		KeyMap.erase(k);
	} else {
		KeyMap[k] = v;
	}
}

std::vector<std::string> iSysConfigManager::GetCustomArgumentsForPush() {
	std::vector<std::string> ret;
	for (auto arg : CustomArguments) {
		std::string line("-");
		line += arg.first;
		line += "=";
		line += arg.second;
		ret.emplace_back(line);
	}
	return ret;
}

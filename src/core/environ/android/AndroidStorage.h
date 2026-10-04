#pragma once
#include <functional>
#include <string>
#include <vector>
#include "tjsCommHead.h"
struct tTVPLocalFileInfo;
struct tTVP_stat;
int TVPOpenDocumentFile(const std::string &path, int access);
bool TVPStatDocumentFile(const std::string &path, tTVP_stat &info);
bool TVPRemoveDocument(const std::string &path, bool directory);
bool TVPListDocuments(const std::string &path,
                      const std::function<void(const ttstr &, tTVPLocalFileInfo *)> &callback);
std::vector<std::string> TVPDocumentRoots();

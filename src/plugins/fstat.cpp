// Portable storage operations used by KAG frameworks. All I/O goes through
// the runtime's local/Android document storage implementation.
#include "tp_stub.h"
#include <sys/stat.h>
#include "Platform.h"
#include "ncbind/ncbind.hpp"
#include "tjsDictionary.h"
#include <algorithm>
#include <memory>

#define NCB_MODULE_NAME TJS_W("fstat.dll")

namespace {
tTJSVariant array(const std::vector<ttstr> &items) {
    iTJSDispatch2 *object = TJSCreateArrayObject();
    tTJSVariant value(object, object);
    object->Release();
    for(size_t i = 0; i < items.size(); ++i) {
        tTJSVariant item(items[i]);
        object->PropSetByNum(TJS_MEMBERENSURE, static_cast<tjs_int>(i), &item, object);
    }
    return value;
}

ttstr localName(const ttstr &path) {
    ttstr name = TVPNormalizeStorageName(path);
    TVPGetLocalName(name);
    return name;
}

ttstr directoryName(const ttstr &path) {
    ttstr name = TVPNormalizeStorageName(path);
    if(name.GetLastChar() != TJS_W('/') && name.GetLastChar() != TVPArchiveDelimiter)
        name += TJS_W('/');
    return name;
}

void set(iTJSDispatch2 *object, const tjs_char *name, const tTJSVariant &value) {
    object->PropSet(TJS_MEMBERENSURE, name, nullptr, &value, object);
}

tTJSVariant date(tjs_uint64 seconds) {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    tTJSVariant cls;
    tjs_error error = global->PropGet(0, TJS_W("Date"), nullptr, &cls, global);
    global->Release();
    if(TJS_FAILED(error)) TVPThrowExceptionMessage(TJS_W("Date class is unavailable"));
    iTJSDispatch2 *object = nullptr;
    auto closure = cls.AsObjectClosureNoAddRef();
    if(TJS_FAILED(closure.CreateNew(0, nullptr, nullptr, &object, 0, nullptr, nullptr)))
        TVPThrowExceptionMessage(TJS_W("Cannot create a file timestamp"));
    tTJSVariant value(object, object);
    object->Release();
    tTJSVariant milliseconds(static_cast<tjs_int64>(seconds * 1000));
    tTJSVariant *args[] = {&milliseconds};
    if(TJS_FAILED(object->FuncCall(0, TJS_W("setTime"), nullptr, nullptr, 1, args, object)))
        TVPThrowExceptionMessage(TJS_W("Cannot set a file timestamp"));
    return value;
}

class StorageFiles {
public:
    static tTJSVariant dirlist(const tjs_char *name) {
        return array(TVPGetStorageDirectoryNames(name));
    }

    static tjs_error TJS_INTF_METHOD dirtree(tTJSVariant *result, tjs_int count,
                                             tTJSVariant **args, iTJSDispatch2 *) {
        if(count < 1) return TJS_E_BADPARAMCOUNT;
        bool onlyDirectories = count > 1 && args[1]->operator bool();
        ttstr base = directoryName(*args[0]);
        std::vector<ttstr> entries;
        struct Pending { ttstr path; unsigned depth; };
        std::vector<Pending> pending = {{ttstr(), 0}};
        while(!pending.empty()) {
            Pending next = pending.back();
            pending.pop_back();
            for(const auto &child : TVPGetStorageDirectoryNames(base + next.path)) {
                bool isDirectory = child.GetLastChar() == TJS_W('/');
                ttstr relative = next.path + child;
                if(isDirectory || !onlyDirectories) entries.push_back(relative);
                if(entries.size() + pending.size() > 100000 || next.depth >= 128)
                    TVPThrowExceptionMessage(TJS_W("Directory tree exceeds the traversal limit"));
                if(isDirectory) pending.push_back({relative, next.depth + 1});
            }
        }
        std::sort(entries.begin(), entries.end());
        if(result) *result = array(entries);
        return TJS_S_OK;
    }

    static bool isExistentDirectory(const tjs_char *name) {
        return TVPIsExistentStorageDirectory(name);
    }

    static bool createDirectory(const tjs_char *name) {
        bool ok = TVPCreateFolders(localName(name));
        if(ok) TVPClearStorageCaches();
        return ok;
    }

    static bool removeDirectory(const tjs_char *name) {
        bool ok = TVPRemoveFolder(localName(name));
        if(ok) TVPClearStorageCaches();
        return ok;
    }

    static bool deleteFile(const tjs_char *name) {
        bool ok = TVPRemoveFile(localName(name));
        if(ok) TVPClearStorageCaches();
        return ok;
    }

    static bool moveFile(const tjs_char *from, const tjs_char *to) {
        bool ok = TVPRenameFile(localName(from).AsStdString(), localName(to).AsStdString());
        if(ok) TVPClearStorageCaches();
        return ok;
    }

    static tjs_error TJS_INTF_METHOD copyFile(tTJSVariant *result, tjs_int count,
                                             tTJSVariant **args, iTJSDispatch2 *) {
        if(count < 2) return TJS_E_BADPARAMCOUNT;
        ttstr from = TVPSearchPlacedPath(*args[0]);
        ttstr to = TVPNormalizeStorageName(*args[1]);
        if(from == to || (count > 2 && args[2]->operator bool() && TVPIsExistentStorageNoSearch(to))) {
            if(result) *result = tjs_int(0);
            return TJS_S_OK;
        }
        std::unique_ptr<tTJSBinaryStream> input(TVPCreateStream(from, TJS_BS_READ));
        std::unique_ptr<tTJSBinaryStream> output(TVPCreateStream(to, TJS_BS_WRITE));
        tjs_uint8 buffer[65536];
        for(tjs_uint size; (size = input->Read(buffer, sizeof(buffer))) != 0;)
            output->WriteBuffer(buffer, size);
        output.reset();
        TVPClearStorageCaches();
        if(result) *result = tjs_int(1);
        return TJS_S_OK;
    }

    static tTJSVariant fstat(const tjs_char *path) {
        ttstr placed = TVPGetPlacedPath(path);
        if(placed.IsEmpty()) placed = TVPNormalizeStorageName(path);
        iTJSDispatch2 *object = TJSCreateDictionaryObject();
        tTJSVariant value(object, object);
        object->Release();
        if(TJS_strchr(placed.c_str(), TVPArchiveDelimiter)) {
            std::unique_ptr<tTJSBinaryStream> input(TVPCreateStream(placed, TJS_BS_READ));
            set(object, TJS_W("size"), tTJSVariant(static_cast<tjs_int64>(input->GetSize())));
        } else {
            tTVP_stat info;
            if(!TVP_stat(localName(placed).c_str(), info))
                TVPThrowExceptionMessage((ttstr(TJS_W("Cannot stat storage: ")) + path).c_str());
            if((info.st_mode & S_IFMT) != S_IFDIR)
                set(object, TJS_W("size"), tTJSVariant(static_cast<tjs_int64>(info.st_size)));
            set(object, TJS_W("atime"), date(info.st_atime));
            set(object, TJS_W("mtime"), date(info.st_mtime));
            set(object, TJS_W("ctime"), date(info.st_ctime));
        }
        return value;
    }

    static tTJSVariant getTime(const tjs_char *path) {
        if(TJS_strchr(TVPNormalizeStorageName(path).c_str(), TVPArchiveDelimiter))
            TVPThrowExceptionMessage(TJS_W("Archive entries do not have file timestamps"));
        tTJSVariant value = fstat(path);
        auto object = value.AsObjectNoAddRef();
        object->DeleteMember(0, TJS_W("size"), nullptr, object);
        return value;
    }

    static void clearStorageCaches() { TVPClearStorageCaches(); }
};
}

NCB_ATTACH_CLASS(StorageFiles, Storages) {
    NCB_METHOD(dirlist);
    RawCallback("dirtree", &Class::dirtree, TJS_STATICMEMBER);
    NCB_METHOD(isExistentDirectory);
    NCB_METHOD(createDirectory);
    NCB_METHOD(removeDirectory);
    NCB_METHOD(deleteFile);
    NCB_METHOD(moveFile);
    RawCallback("copyFile", &Class::copyFile, TJS_STATICMEMBER);
    RawCallback("exportFile", &Class::copyFile, TJS_STATICMEMBER);
    NCB_METHOD(fstat);
    NCB_METHOD(getTime);
    NCB_METHOD(clearStorageCaches);
}

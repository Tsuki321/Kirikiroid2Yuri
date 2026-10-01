#include "AndroidStorage.h"
#include "StorageImpl.h"
#include "Platform.h"
#include "platform/android/jni/JniHelper.h"
#include <cstdlib>

namespace {
struct Call {
    cocos2d::JniMethodInfo method;
    bool valid;
    Call(const char *name, const char *signature)
        : valid(cocos2d::JniHelper::getStaticMethodInfo(method,
            "org/tvp/kirikiri2/KR2Activity", name, signature)) {}
    ~Call() { if (valid) { ok(); method.env->DeleteLocalRef(method.classID); } }
    bool ok() {
        if (!valid) return false;
        if (!method.env->ExceptionCheck()) return true;
        method.env->ExceptionDescribe();
        method.env->ExceptionClear();
        return false;
    }
    jstring string(const std::string &path) {
        ttstr wide(path);
        return method.env->NewString(reinterpret_cast<const jchar *>(wide.c_str()), wide.length());
    }
    std::string string(jstring value) {
        if (!value) return std::string();
        const jchar *chars = method.env->GetStringChars(value, nullptr);
        if (!chars) return std::string();
        ttstr wide(reinterpret_cast<const tjs_char *>(chars), method.env->GetStringLength(value));
        method.env->ReleaseStringChars(value, chars);
        return wide.AsStdString();
    }
};
}

int TVPOpenDocumentFile(const std::string &path, int access) {
    Call call("OpenDocument", "(Ljava/lang/String;I)I");
    if (!call.valid) return -1;
    jstring name = call.string(path);
    jint fd = name ? call.method.env->CallStaticIntMethod(call.method.classID, call.method.methodID, name, access) : -1;
    call.method.env->DeleteLocalRef(name);
    return call.ok() ? fd : -1;
}

bool TVPStatDocumentFile(const std::string &path, tTVP_stat &info) {
    Call call("StatDocument", "(Ljava/lang/String;)[J");
    if (!call.valid) return false;
    jstring name = call.string(path);
    jlongArray result = name ? static_cast<jlongArray>(call.method.env->CallStaticObjectMethod(
        call.method.classID, call.method.methodID, name)) : nullptr;
    call.method.env->DeleteLocalRef(name);
    bool valid = call.ok() && result && call.method.env->GetArrayLength(result) == 3;
    if (valid) {
        jlong values[3];
        call.method.env->GetLongArrayRegion(result, 0, 3, values);
        valid = call.ok();
        if (valid) {
            info.st_mode = static_cast<uint16_t>(values[0]);
            info.st_size = values[1];
            info.st_atime = info.st_mtime = info.st_ctime = values[2];
        }
    }
    call.method.env->DeleteLocalRef(result);
    return valid;
}

bool TVPListDocuments(const std::string &path,
    const std::function<void(const ttstr &, tTVPLocalFileInfo *)> &callback) {
    Call call("ListDocuments", "(Ljava/lang/String;)[Ljava/lang/String;");
    if (!call.valid) return false;
    jstring name = call.string(path);
    jobjectArray entries = name ? static_cast<jobjectArray>(call.method.env->CallStaticObjectMethod(
        call.method.classID, call.method.methodID, name)) : nullptr;
    call.method.env->DeleteLocalRef(name);
    if (!call.ok() || !entries) return false;
    jsize count = call.method.env->GetArrayLength(entries);
    bool valid = count % 4 == 0;
    for (jsize i = 0; valid && i < count; i += 4) {
        std::string fields[4];
        for (int j = 0; j < 4; ++j) {
            jstring field = static_cast<jstring>(call.method.env->GetObjectArrayElement(entries, i + j));
            fields[j] = call.string(field);
            call.method.env->DeleteLocalRef(field);
        }
        if (!(valid = call.ok())) break;
        tTVPLocalFileInfo info;
        info.NativeName = fields[0].c_str();
        info.Mode = fields[1] == "d" ? S_IFDIR : S_IFREG;
        info.Size = strtoull(fields[2].c_str(), nullptr, 10);
        info.AccessTime = info.ModifyTime = info.CreationTime = strtoll(fields[3].c_str(), nullptr, 10);
        ttstr normalized(fields[0]);
        tjs_char *chars = normalized.Independ();
        for (; *chars; ++chars) if (*chars >= 'A' && *chars <= 'Z') *chars += 'a' - 'A';
        callback(normalized, &info);
    }
    call.method.env->DeleteLocalRef(entries);
    return valid;
}

std::vector<std::string> TVPDocumentRoots() {
    std::vector<std::string> result;
    Call call("DocumentRoots", "()[Ljava/lang/String;");
    if (!call.valid) return result;
    jobjectArray roots = static_cast<jobjectArray>(call.method.env->CallStaticObjectMethod(call.method.classID, call.method.methodID));
    if (call.ok() && roots) {
        jsize count = call.method.env->GetArrayLength(roots);
        for (jsize i = 0; i < count; ++i) {
            jstring root = static_cast<jstring>(call.method.env->GetObjectArrayElement(roots, i));
            result.push_back(call.string(root));
            call.method.env->DeleteLocalRef(root);
        }
    }
    call.method.env->DeleteLocalRef(roots);
    call.ok();
    return result;
}

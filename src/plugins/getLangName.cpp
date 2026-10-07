#include "ncbind/ncbind.hpp"
#include "Platform.h"
#include "SystemLocale.h"

#ifdef __ANDROID__
#include "platform/android/jni/JniHelper.h"
#endif

#define NCB_MODULE_NAME TJS_W("getLangName.dll")

namespace {
std::string CurrentLanguageName() {
#ifdef __ANDROID__
    // Boot-class JNI calls also work on the engine's native script thread.
    // Locale's English display language covers all Android locales; the tag
    // preserves an explicit Hans/Hant script even when no country is selected.
    JNIEnv *env = cocos2d::JniHelper::getEnv();
    if (env && env->PushLocalFrame(12) == JNI_OK) {
        std::string name, tag;
        do {
            jclass cls = env->FindClass("java/util/Locale");
            if (!cls || env->ExceptionCheck()) break;
            jmethodID getDefault = env->GetStaticMethodID(cls, "getDefault", "()Ljava/util/Locale;");
            if (!getDefault || env->ExceptionCheck()) break;
            jmethodID toLanguageTag = env->GetMethodID(cls, "toLanguageTag", "()Ljava/lang/String;");
            if (!toLanguageTag || env->ExceptionCheck()) break;
            jmethodID display = env->GetMethodID(cls, "getDisplayLanguage", "(Ljava/util/Locale;)Ljava/lang/String;");
            if (!display || env->ExceptionCheck()) break;
            jfieldID englishId = env->GetStaticFieldID(cls, "ENGLISH", "Ljava/util/Locale;");
            if (!englishId || env->ExceptionCheck()) break;
            jobject locale = env->CallStaticObjectMethod(cls, getDefault);
            if (!locale || env->ExceptionCheck()) break;
            jobject english = env->GetStaticObjectField(cls, englishId);
            if (!english || env->ExceptionCheck()) break;
            jstring tagString = static_cast<jstring>(env->CallObjectMethod(locale, toLanguageTag));
            if (!tagString || env->ExceptionCheck()) break;
            tag = cocos2d::JniHelper::jstring2string(tagString);
            if (env->ExceptionCheck()) break;
            jstring nameString = static_cast<jstring>(env->CallObjectMethod(locale, display, english));
            if (!nameString || env->ExceptionCheck()) break;
            name = cocos2d::JniHelper::jstring2string(nameString);
        } while (false);
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->PopLocalFrame(nullptr);
        if (!tag.empty()) return krkr::EnglishLanguageName(tag, name);
    } else if (env && env->ExceptionCheck()) {
        env->ExceptionClear();
    }
#endif
    return krkr::EnglishLanguageName(TVPGetCurrentLanguage());
}

tjs_error GetLanguageName(tTJSVariant *result, tjs_int, tTJSVariant **,
                         iTJSDispatch2 *) {
    // The optional Windows system/user selector has the same meaning on
    // Android, which supplies one current application/device Locale.
    if (result) *result = ttstr(CurrentLanguageName().c_str());
    return TJS_S_OK;
}
}

NCB_ATTACH_FUNCTION(getCurrentUILangName, System, GetLanguageName);
NCB_ATTACH_FUNCTION(getCurrentLocaleName, System, GetLanguageName);

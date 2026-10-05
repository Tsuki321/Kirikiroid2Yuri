#include "ncbind/ncbind.hpp"
#include "DataPackReader.h"
#include <memory>

#define NCB_MODULE_NAME TJS_W("tjsdatapack.dll")

namespace {
ttstr packString(const std::u16string &text) {
    std::basic_string<tjs_char> units(text.begin(), text.end());
    return ttstr(units.data(), static_cast<int>(units.size()));
}
tTJSVariant packVariant(const DataPack::Value &value) {
    using V = DataPack::Value;
    switch(value.kind) {
    case V::Void: return tTJSVariant();
    case V::Null: return tTJSVariant(static_cast<iTJSDispatch2 *>(nullptr));
    case V::String: return tTJSVariant(packString(value.text));
    case V::Octet: {
        static const tjs_uint8 empty = 0;
        return tTJSVariant(value.bytes.empty() ? &empty : value.bytes.data(),
                           static_cast<tjs_uint>(value.bytes.size()));
    }
    case V::Integer: return tTJSVariant(static_cast<tjs_int64>(value.integer));
    case V::Real: return tTJSVariant(static_cast<tjs_real>(value.real));
    default: break;
    }
    iTJSDispatch2 *object = value.kind == V::Array ? TJSCreateArrayObject() : TJSCreateDictionaryObject();
    try {
        for(size_t i = 0; i < value.values.size(); ++i) {
            tTJSVariant child = packVariant(value.values[i]);
            tjs_error status;
            if(value.kind == V::Array)
                status = object->PropSetByNum(TJS_MEMBERENSURE, static_cast<tjs_int>(i), &child, object);
            else {
                if(value.keys[i].find(u'\0') != std::u16string::npos)
                    DataPack::fail("DataPack: dictionary key contains a null character");
                ttstr key = packString(value.keys[i]);
                status = object->PropSet(TJS_MEMBERENSURE, key.c_str(), nullptr, &child, object);
            }
            if(TJS_FAILED(status)) DataPack::fail("DataPack: cannot create script value");
        }
        tTJSVariant result(object, object);
        object->Release();
        return result;
    } catch(...) { object->Release(); throw; }
}

struct DataPackScripts {
    static tjs_error TJS_INTF_METHOD load(tTJSVariant *result, tjs_int count,
                                        tTJSVariant **args, iTJSDispatch2 *) {
        if(count < 1) return TJS_E_BADPARAMCOUNT;
        ttstr name(*args[0]);
        std::vector<uint8_t> outer;
        bool hasOuter = false;
        if(count > 1 && args[1]->Type() != tvtVoid) {
            iTJSDispatch2 *options = args[1]->Type() == tvtObject ? args[1]->AsObjectNoAddRef() : nullptr;
            if(!options) TVPThrowExceptionMessage(TJS_W("DataPack: options must be a dictionary"));
            tTJSVariant iv;
            if(TJS_SUCCEEDED(options->PropGet(0, TJS_W("outeriv"), nullptr, &iv, options)) && iv.Type() != tvtVoid) {
                hasOuter = true;
                if(iv.Type() == tvtOctet) {
                    auto octet = iv.AsOctetNoAddRef();
                    if(octet) outer.assign(octet->GetData(), octet->GetData() + octet->GetLength());
                } else if(iv.Type() == tvtString) {
                    std::string utf8 = ttstr(iv).AsStdString();
                    outer.assign(utf8.begin(), utf8.end());
                } else TVPThrowExceptionMessage(TJS_W("DataPack: outeriv must be a string or octet"));
            }
        }
        std::unique_ptr<tTJSBinaryStream> stream(TVPCreateStream(name, TJS_BS_READ));
        uint64_t size = stream->GetSize();
        if(size > DataPack::MaxBytes) TVPThrowExceptionMessage(TJS_W("DataPack: file exceeds the size limit"));
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        if(size) stream->ReadBuffer(bytes.data(), static_cast<tjs_uint>(size));
        try {
            auto value = DataPack::decode(bytes, hasOuter ? &outer : nullptr);
            if(result) *result = packVariant(value);
        } catch(const std::exception &error) {
            TVPThrowExceptionMessage((ttstr(error.what()) + TJS_W(" (") + name + TJS_W(")")).c_str());
        }
        return TJS_S_OK;
    }
};
}

// This extension is used without explicitly linking a separate DLL.
NCB_ATTACH_CLASS(DataPackScripts, Scripts) {
    RawCallback(TJS_W("loadDataPack"), &DataPackScripts::load, TJS_STATICMEMBER);
}

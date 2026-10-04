#include "ncbind/ncbind.hpp"
#include "StorageIntf.h"
#include "PsbReader.h"
#include "tjsDictionary.h"
#include <map>
#include <memory>

#define NCB_MODULE_NAME TJS_W("psbfile.dll")

namespace {
ttstr lower(ttstr text) {
    if(text.IsEmpty()) return text;
    tjs_char *p = text.Independ();
    for(; *p; ++p) if(*p >= TJS_W('A') && *p <= TJS_W('Z')) *p += TJS_W('a') - TJS_W('A');
    return text;
}
void error(const tjs_char *message) { TVPThrowExceptionMessage(message); }

ttstr scriptString(const std::string &text) {
    // Reader validates UTF-8. Preserve supplementary characters as UTF-16
    // pairs; the engine's older narrow-string constructor truncates them.
    std::basic_string<tjs_char> output;
    output.reserve(text.size());
    for(size_t i = 0; i < text.size();) {
        uint32_t code = static_cast<uint8_t>(text[i++]);
        unsigned count = 0;
        if(code >= 0xf0) { code &= 7; count = 3; }
        else if(code >= 0xe0) { code &= 15; count = 2; }
        else if(code >= 0xc0) { code &= 31; count = 1; }
        while(count--) code = (code << 6) | (static_cast<uint8_t>(text[i++]) & 63);
        if(code > 0xffff) {
            code -= 0x10000;
            output.push_back(static_cast<tjs_char>(0xd800 + (code >> 10)));
            output.push_back(static_cast<tjs_char>(0xdc00 + (code & 1023)));
        } else output.push_back(static_cast<tjs_char>(code));
    }
    return ttstr(output.c_str());
}

tTJSCriticalSection sourceLock;
std::map<ttstr, ttstr> sourcePaths;

struct Source {
    ttstr storage;
    Psb::Document document;
    static std::shared_ptr<Source> load(const ttstr &name) {
        // A PSB domain resolves to a regular engine storage, never to itself.
        // Nested PSB URLs could otherwise recurse through CheckExistentStorage.
        if(lower(TVPNormalizeStorageName(name)).StartsWith(TJS_W("psb:")))
            error(TJS_W("PSB: nested PSB storages are unsupported"));
        auto source = std::make_shared<Source>();
        source->storage = TVPGetPlacedPath(name);
        if(source->storage.IsEmpty()) error(TJS_W("PSB: storage was not found"));
        std::unique_ptr<tTJSBinaryStream> stream(TVPCreateStream(source->storage, TJS_BS_READ));
        uint64_t length = stream->GetSize();
        uint8_t header[44] = {};
        tjs_uint got = stream->Read(header, sizeof(header));
        try {
            size_t size = Psb::Reader::metadataSize(header, got, length);
            std::vector<uint8_t> metadata(size);
            stream->SetPosition(0);
            stream->ReadBuffer(metadata.data(), static_cast<tjs_uint>(size));
            source->document = Psb::Reader(metadata.data(), metadata.size(), length).read();
        } catch(const std::exception &e) { TVPThrowExceptionMessage(ttstr(e.what()).c_str()); }
        return source;
    }
};

class ResourceStream : public tTJSBinaryStream {
    std::unique_ptr<tTJSBinaryStream> stream;
    Psb::Range range;
    uint64_t position = 0;
public:
    ResourceStream(const Source &source, size_t index)
        : stream(TVPCreateStream(source.storage, TJS_BS_READ)), range(source.document.resources.at(index)) {
        uint64_t size = stream->GetSize();
        if(range.offset > size || range.size > size - range.offset) error(TJS_W("PSB: resource source was truncated"));
    }
    tjs_uint64 TJS_INTF_METHOD Seek(tjs_int64 offset, tjs_int origin) override {
        uint64_t base = 0;
        if(origin == TJS_BS_SEEK_CUR) base = position;
        else if(origin == TJS_BS_SEEK_END) base = range.size;
        else if(origin != TJS_BS_SEEK_SET) error(TJS_W("PSB: invalid seek origin"));
        uint64_t magnitude = offset < 0 ? uint64_t(-(offset + 1)) + 1 : uint64_t(offset);
        if((offset < 0 && magnitude > base) || (offset >= 0 && magnitude > range.size - base))
            error(TJS_W("PSB: seek is outside the resource"));
        position = offset < 0 ? base - magnitude : base + magnitude;
        return position;
    }
    tjs_uint TJS_INTF_METHOD Read(void *buffer, tjs_uint count) override {
        tjs_uint size = static_cast<tjs_uint>(std::min<uint64_t>(count, range.size - position));
        if(!size) return 0;
        stream->SetPosition(range.offset + position);
        stream->ReadBuffer(buffer, size);
        position += size;
        return size;
    }
    tjs_uint TJS_INTF_METHOD Write(const void *, tjs_uint) override {
        error(TJS_W("PSB resources are read-only")); return 0;
    }
    tjs_uint64 TJS_INTF_METHOD GetSize() override { return range.size; }
};

// A property defers octet allocation until that resource is actually requested.
// Scene/layer dictionaries and retained root values survive PSBFile invalidation.
class ResourceProperty : public tTJSDispatch {
    std::shared_ptr<Source> source;
    size_t index;
public:
    ResourceProperty(std::shared_ptr<Source> source, size_t index) : source(std::move(source)), index(index) {}
    tjs_error TJS_INTF_METHOD PropGet(tjs_uint32, const tjs_char *member, tjs_uint32 *,
                                     tTJSVariant *result, iTJSDispatch2 *) override {
        if(member) return TJS_E_MEMBERNOTFOUND;
        if(result) {
            ResourceStream stream(*source, index);
            std::vector<uint8_t> bytes(static_cast<size_t>(stream.GetSize()));
            if(!bytes.empty()) stream.ReadBuffer(bytes.data(), static_cast<tjs_uint>(bytes.size()));
            *result = tTJSVariant(bytes.data(), static_cast<tjs_uint>(bytes.size()));
        }
        return TJS_S_OK;
    }
};

tTJSVariant toScript(const Psb::Value &value, const std::shared_ptr<Source> &source) {
    switch(value.kind) {
    case Psb::Value::Null: return tTJSVariant();
    case Psb::Value::Boolean:
    case Psb::Value::Integer: return tTJSVariant(static_cast<tjs_int64>(value.integer));
    case Psb::Value::Real: return tTJSVariant(value.real);
    case Psb::Value::String: return tTJSVariant(scriptString(value.text));
    case Psb::Value::Resource: {
        auto property = new ResourceProperty(source, static_cast<size_t>(value.integer));
        tTJSVariant result(property); property->Release(); return result;
    }
    case Psb::Value::Array:
    case Psb::Value::Object: {
        iTJSDispatch2 *object = value.kind == Psb::Value::Array ? TJSCreateArrayObject() : TJSCreateDictionaryObject();
        tTJSVariant result(object, object); object->Release();
        for(size_t i = 0; i < value.values.size(); ++i) {
            auto child = toScript(value.values[i], source);
            tjs_uint32 flags = TJS_MEMBERENSURE | TJS_IGNOREPROP;
            tjs_error status = value.kind == Psb::Value::Array ?
                object->PropSetByNum(flags, static_cast<tjs_int>(i), &child, object) :
                object->PropSet(flags, scriptString(value.keys[i]).c_str(), nullptr, &child, object);
            if(TJS_FAILED(status)) error(TJS_W("PSB: could not construct script data"));
        }
        return result;
    }
    }
    return tTJSVariant();
}

struct Location { std::shared_ptr<Source> source; ttstr path; };
Location locate(const ttstr &name) {
    const tjs_char *slash = TJS_strchr(name.c_str(), TJS_W('/'));
    if(!slash || slash == name.c_str()) error(TJS_W("PSB: expected psb://file/resource"));
    ttstr domain(name.c_str(), static_cast<tjs_int>(slash - name.c_str()));
    if(TJS_strchr(domain.c_str(), TJS_W(':')) || TJS_strchr(domain.c_str(), TVPArchiveDelimiter))
        error(TJS_W("PSB: invalid resource domain"));
    ttstr storage = domain;
    {
        tTJSCriticalSectionHolder lock(sourceLock);
        auto found = sourcePaths.find(lower(domain));
        if(found != sourcePaths.end()) storage = found->second;
    }
    return {Source::load(storage), ttstr(slash + 1)};
}

const Psb::Value *resource(const Location &location) {
    const auto &root = location.source->document.root;
    ttstr path = lower(location.path);
    for(size_t i = 0; i < root.keys.size(); ++i) {
        if(root.values[i].kind == Psb::Value::Resource && lower(scriptString(root.keys[i])) == path)
            return &root.values[i];
    }
    return nullptr;
}

class PsbMedia : public iTVPStorageMedia {
    tjs_uint refs = 1;
public:
    void TJS_INTF_METHOD AddRef() override { ++refs; }
    void TJS_INTF_METHOD Release() override { if(!--refs) delete this; }
    void TJS_INTF_METHOD GetName(ttstr &name) override { name = TJS_W("psb"); }
    void TJS_INTF_METHOD NormalizeDomainName(ttstr &name) override { name = lower(name); }
    void TJS_INTF_METHOD NormalizePathName(ttstr &name) override { name = lower(name); }
    bool TJS_INTF_METHOD CheckExistentStorage(const ttstr &name) override {
        try { return resource(locate(name)) != nullptr; } catch(...) { return false; }
    }
    tTJSBinaryStream *TJS_INTF_METHOD Open(const ttstr &name, tjs_uint32 flags) override {
        if(flags != TJS_BS_READ) error(TJS_W("PSB resources are read-only"));
        auto location = locate(name);
        auto item = resource(location);
        if(!item) error(TJS_W("PSB: resource was not found"));
        return new ResourceStream(*location.source, static_cast<size_t>(item->integer));
    }
    void TJS_INTF_METHOD GetListAt(const ttstr &name, iTVPStorageLister *lister) override {
        auto location = locate(name);
        const auto &root = location.source->document.root;
        for(size_t i = 0; i < root.keys.size(); ++i) {
            ttstr key = lower(scriptString(root.keys[i]));
            if(root.values[i].kind == Psb::Value::Resource && key.StartsWith(location.path))
                lister->Add(ttstr(key.c_str() + location.path.length()));
        }
    }
    void TJS_INTF_METHOD GetLocallyAccessibleName(ttstr &name) override { name.Clear(); }
};

PsbMedia *media = nullptr;
void registerMedia() {
    if(!media) { media = new PsbMedia(); TVPRegisterStorageMedia(media); }
}
void unregisterMedia() {
    if(media) { TVPUnregisterStorageMedia(media); media->Release(); media = nullptr; }
    tTJSCriticalSectionHolder lock(sourceLock); sourcePaths.clear();
}

class PSBFile {
    std::shared_ptr<Source> source;
    tTJSVariant root;
public:
    static tjs_error TJS_INTF_METHOD create(PSBFile **result, tjs_int count, tTJSVariant **params, iTJSDispatch2 *) {
        if(count > 1) return TJS_E_BADPARAMCOUNT;
        std::unique_ptr<PSBFile> file(new PSBFile());
        if(count && params[0]->Type() != tvtVoid) file->load(ttstr(*params[0]).c_str());
        *result = file.release(); return TJS_S_OK;
    }
    bool load(const tjs_char *name) {
        auto next = Source::load(name);
        {
            tTJSCriticalSectionHolder lock(sourceLock);
            if(sourcePaths.size() >= 4096) sourcePaths.clear();
            sourcePaths[lower(TVPExtractStorageName(name))] = next->storage;
        }
        source = std::move(next); root.Clear();
        TVPClearStorageCaches();
        return true;
    }
    tTJSVariant getRoot() {
        if(!source) return tTJSVariant();
        if(root.Type() == tvtVoid) root = toScript(source->document.root, source);
        return root;
    }
};
} // namespace

NCB_REGISTER_CLASS(PSBFile) {
    Factory(&Class::create);
    NCB_METHOD(load);
    NCB_PROPERTY_RO(root, getRoot);
}
NCB_PRE_REGIST_CALLBACK(registerMedia);
NCB_POST_UNREGIST_CALLBACK(unregisterMedia);

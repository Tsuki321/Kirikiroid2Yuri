#include "ncbind/ncbind.hpp"
//extern void InitPlugin_CSVParser();

void TVPLoadInternalPlugins()
{
    ncbAutoRegister::AllRegist();
	ncbAutoRegister::LoadModule(TJS_W("tjsdatapack.dll"));
	ncbAutoRegister::LoadModule(TJS_W("xp3filter.dll"));
	ncbAutoRegister::LoadModule(TJS_W("fstat.dll"));
	ncbAutoRegister::LoadModule(TJS_W("dirlist.dll"));
    //InitPlugin_CSVParser();
}

void TVPUnloadInternalPlugins()
{
    ncbAutoRegister::AllUnregist();
}

ttstr TVPNormalizeInternalPluginName(const ttstr &name)
{
    if (name.IsEmpty()) return name;
    ttstr module = TVPExtractStorageName(name).AsLowerCase();
    // Kirikiri also distributes DLL plugins under the autoload suffix .tpm.
    // Resolve both spellings to the same built-in module and registration.
    const tjs_int length = module.GetLen();
    if (length >= 4 && module.SubString(length - 4, 4) == TJS_W(".tpm"))
        module = module.SubString(0, length - 4) + TJS_W(".dll");
    return module;
}

bool TVPLoadInternalPlugin(const ttstr &name)
{
    return ncbAutoRegister::LoadModule(TVPNormalizeInternalPluginName(name));
}

bool TVPIsInternalPluginAvailable(const ttstr &name)
{
    return ncbAutoRegister::HasModule(TVPNormalizeInternalPluginName(name));
}

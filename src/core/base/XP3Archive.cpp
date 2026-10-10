//---------------------------------------------------------------------------
/*
	TVP2 ( T Visual Presenter 2 )  A script authoring tool
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// XP3 virtual file system support
//---------------------------------------------------------------------------

#include "tjsCommHead.h"

#include "XP3Archive.h"
#include "XP3IndexValidator.h"
#include "MsgIntf.h"
#include "DebugIntf.h"
#include "EventIntf.h"
#include "UtilStreams.h"
#include "SysInitIntf.h"

#include <zlib.h>
#include <algorithm>
#include <memory>
#include <unordered_set>

bool TVPAllowExtractProtectedStorage = true;


//---------------------------------------------------------------------------
// archive filter related
//---------------------------------------------------------------------------
tTVPXP3ArchiveExtractionFilter TVPXP3ArchiveExtractionFilter  = NULL;
void TVPSetXP3ArchiveExtractionFilter(tTVPXP3ArchiveExtractionFilter filter)
{
	TVPXP3ArchiveExtractionFilter = filter;
}
//---------------------------------------------------------------------------

static tTVPXP3ArchiveContentFilter TVPXP3ArchiveContentFilter = nullptr;
void TVPSetXP3ArchiveContentFilter(tTVPXP3ArchiveContentFilter filter)
{
	TVPXP3ArchiveContentFilter = filter;
}


//---------------------------------------------------------------------------
// tTVPXP3ArchiveHandleCache
//---------------------------------------------------------------------------
#define TVP_MAX_ARCHIVE_HANDLE_CACHE 8
static tjs_uint TVPArchiveHandleCacheAge = 0;
struct tTVPArchiveHandleCacheItem
{
	void * Pointer;
	tTJSBinaryStream * Stream;
	tjs_uint Age;
};
//---------------------------------------------------------------------------
static tTVPArchiveHandleCacheItem * TVPArchiveHandleCachePool = NULL;
static bool TVPArchiveHandleCacheInit = false;
static bool TVPArchiveHandleCacheShutdown = false;
static tTJSCriticalSection TVPArchiveHandleCacheCS;
//---------------------------------------------------------------------------
tTJSBinaryStream * TVPGetCachedArchiveHandle(void * pointer, const ttstr & name)
{
	// get cached archive file handle from the pool
	if(TVPArchiveHandleCacheShutdown)
	{
		// the pool has shutdown
		return TVPCreateStream(name);
	}

	tTJSCriticalSectionHolder cs_holder(TVPArchiveHandleCacheCS);

	if(!TVPArchiveHandleCacheInit)
	{
		// initialize the pool
		TVPArchiveHandleCachePool =
			new tTVPArchiveHandleCacheItem[TVP_MAX_ARCHIVE_HANDLE_CACHE];
		for(tjs_int i =0; i < TVP_MAX_ARCHIVE_HANDLE_CACHE; i++)
		{
			TVPArchiveHandleCachePool[i].Pointer = NULL;
			TVPArchiveHandleCachePool[i].Stream = NULL;
			TVPArchiveHandleCachePool[i].Age = 0;
		}
		TVPArchiveHandleCacheInit = true;
	}

	// linear search wiil be enough here because the 
	// TVP_MAX_ARCHIVE_HANDLE_CACHE is relatively small
	for(tjs_int i =0; i < TVP_MAX_ARCHIVE_HANDLE_CACHE; i++)
	{
		tTVPArchiveHandleCacheItem *item =
			TVPArchiveHandleCachePool + i;
		if(item->Stream && item->Pointer == pointer)
		{
			// found in the pool
			tTJSBinaryStream * stream = item->Stream;
			item->Stream = NULL;
			return stream;
		}
	}

	// not found in the pool
	// simply create a stream and return it
	return TVPCreateStream(name);
}
//---------------------------------------------------------------------------
/*static*/ void TVPReleaseCachedArchiveHandle(void * pointer,
						tTJSBinaryStream * stream)
{
	// release archive file handle
	if(TVPArchiveHandleCacheShutdown) return;
	if(!TVPArchiveHandleCacheInit) return;

	tTJSCriticalSectionHolder cs_holder(TVPArchiveHandleCacheCS);

	// search empty cell in the pool
	tjs_uint oldest_age = 0;
	tjs_int oldest = 0;
	for(tjs_int i = 0; i < TVP_MAX_ARCHIVE_HANDLE_CACHE; i++)
	{
		tTVPArchiveHandleCacheItem *item =
			TVPArchiveHandleCachePool + i;
		if(item->Stream == NULL)
		{
			// found the empty cell; fill it
			item->Pointer = pointer;
			item->Stream = stream;
			item->Age = ++TVPArchiveHandleCacheAge;
				// counter overflow in TVPArchiveHandleCacheAge
				// is not so a big problem.
				// counter overflow can worsen the cache performance,
				// but it occurs only when the counter is overflowed
				// (it's too far less than usual)
			return;
		}

		if(i == 0 || oldest_age > item->Age)
		{
			oldest_age = item->Age;
			oldest = i;
		}
	}

	// empty cell not found
	// free oldest cell and fill it
	tTVPArchiveHandleCacheItem *oldest_item =
		TVPArchiveHandleCachePool + oldest;
	delete oldest_item->Stream, oldest_item->Stream = NULL;
	oldest_item->Pointer = pointer;
	oldest_item->Stream = stream;
	oldest_item->Age = ++TVPArchiveHandleCacheAge;
}
//---------------------------------------------------------------------------
/*static*/ void TVPFreeArchiveHandlePoolByPointer(void * pointer)
{
	// free all streams which have specified pointer
	if(TVPArchiveHandleCacheShutdown) return;
	if(!TVPArchiveHandleCacheInit) return;

	tTJSCriticalSectionHolder cs_holder(TVPArchiveHandleCacheCS);

	for(tjs_int i = 0; i < TVP_MAX_ARCHIVE_HANDLE_CACHE; i++)
	{
		tTVPArchiveHandleCacheItem *item =
			TVPArchiveHandleCachePool + i;
		if(item->Stream && item->Pointer == pointer)
		{
			delete item->Stream, item->Stream = NULL;
			item->Pointer = NULL;
			item->Age = 0;
		}
	}
}
//---------------------------------------------------------------------------
static void TVPFreeArchiveHandlePool()
{
	// free all streams
	if(TVPArchiveHandleCacheShutdown) return;
	if(!TVPArchiveHandleCacheInit) return;

	tTJSCriticalSectionHolder cs_holder(TVPArchiveHandleCacheCS);

	for(tjs_int i = 0; i < TVP_MAX_ARCHIVE_HANDLE_CACHE; i++)
	{
		tTVPArchiveHandleCacheItem *item =
			TVPArchiveHandleCachePool + i;
		if(item->Stream)
		{
			delete item->Stream, item->Stream = NULL;
			item->Pointer = NULL;
			item->Age = 0;
		}
	}
}
//---------------------------------------------------------------------------
static void TVPShutdownArchiveHandleCache()
{
	// free all stream and shutdown the pool
	tTJSCriticalSectionHolder cs_holder(TVPArchiveHandleCacheCS);

	TVPArchiveHandleCacheShutdown = true;
	if(!TVPArchiveHandleCacheInit) return;

	for(tjs_int i =0; i < TVP_MAX_ARCHIVE_HANDLE_CACHE; i++)
	{
		if(TVPArchiveHandleCachePool[i].Stream)
			delete TVPArchiveHandleCachePool[i].Stream;
	}
	delete [] TVPArchiveHandleCachePool;
}
//---------------------------------------------------------------------------
static tTVPAtExit TVPShutdownArchiveCacheAtExit
	(TVP_ATEXIT_PRI_CLEANUP, TVPShutdownArchiveHandleCache);
//---------------------------------------------------------------------------





//---------------------------------------------------------------------------
// tTVPXP3Archive
//---------------------------------------------------------------------------
/*
	TVP XPK3 virtual file system support. (in short : XP3)
	TVP supports no longer archive type of "XPK1/XPK2"
	( XPK1/XPK2 is used by TVP ver under 0.9x ).

	here word "in-archive" is used for the storages which are contained in
	archive.
*/
//---------------------------------------------------------------------------
bool TVPGetXP3ArchiveOffset(tTJSBinaryStream *st, const ttstr name,
	tjs_uint64 & offset, bool raise)
{
	st->SetPosition(0);
	tjs_uint8 mark[11+1];
	static tjs_uint8 XP3Mark1[] =
		{ 0x58/*'X'*/, 0x50/*'P'*/, 0x33/*'3'*/, 0x0d/*'\r'*/,
		  0x0a/*'\n'*/, 0x20/*' '*/, 0x0a/*'\n'*/, 0x1a/*EOF*/,
		  0xff /* sentinel */ };
	static tjs_uint8 XP3Mark2[] =
		{ 0x8b, 0x67, 0x01, 0xff/* sentinel */ };

	// XP3 header mark contains:
	// 1. line feed and carriage return to detect corruption by unnecessary
	//    line-feeds convertion
	// 2. 1A EOF mark which indicates file's text readable header ending.
	// 3. 8B67 KANJI-CODE to detect curruption by unnecessary code convertion
	// 4. 01 file structure version and character coding
	//    higher 4 bits are file structure version, currently 0.
	//    lower 4 bits are character coding, currently 1, is BMP 16bit Unicode.

	static tjs_uint8 XP3Mark[11+1];
		// +1: I was warned by CodeGuard that the code will do
		// access overrun... because a number of 11 is not aligned by DWORD, 
		// and the processor may read the value of DWORD at last of this array
		// from offset 8. Then the last 1 byte would cause a fail.
	static bool DoInit = true;
	if(DoInit)
	{
		// the XP3 header above is splitted into two part; to avoid
		// mis-finding of the header in the program's initialized data area.
		DoInit = false;
		memcpy(XP3Mark, XP3Mark1, 8);
		memcpy(XP3Mark + 8, XP3Mark2, 3);
		// here joins it.
	}

	mark[0] = 0; // sentinel
	st->ReadBuffer(mark, 11);
	if(mark[0] == 0x4d/*'M'*/ && mark[1] == 0x5a/*'Z'*/)
	{
		// "MZ" is a mark of Win32/DOS executables,
		// TVP searches the first mark of XP3 archive
		// in the executeble file.
		bool found = false;

		offset = 16;
		st->SetPosition(16);

		// XP3 mark must be aligned by a paragraph ( 16 bytes )
		const tjs_uint one_read_size = 256*1024;
		tjs_uint read;
		tjs_uint8 *buffer = new tjs_uint8[one_read_size]; // read 256kbytes at once

		while(0!=(read = st->Read(buffer, one_read_size)))
		{
			tjs_uint p = 0;
				while(p <= read && read - p >= 11)
			{
				if(!memcmp(XP3Mark, buffer + p, 11))
				{
					// found the mark
					offset += p;
					found = true;
					break;
				}
				p+=16;
			}
			if(found) break;
			offset += one_read_size;
		}
        delete[] buffer;
		if(!found)
		{
			if(raise)
				TVPThrowExceptionMessage(TVPCannotUnbindXP3EXE, name);
			else
				return false;
		}
	}
	else if(!memcmp(XP3Mark, mark, 11))
	{
		// XP3 mark found
		offset = 0;
	}
	else
	{
		if (raise) {
			ttstr msg(TVPGetMessageByLocale("err_not_xp3_archive"));
			TVPThrowExceptionMessage(msg.c_str(), name);
			//TVPThrowExceptionMessage(TVPCannotFindXP3Mark, name);
		}
		return false;
	}

	return true;
}
//---------------------------------------------------------------------------
bool TVPIsXP3Archive(const ttstr &name)
{
	tTVPStreamHolder holder(name);
	try
	{
		tjs_uint64 offset;
		return TVPGetXP3ArchiveOffset(holder.Get(), name, offset, false);
	}
	catch(...)
	{
		return false;
	}
}

//---------------------------------------------------------------------------
void tTVPXP3Archive::Init(tTJSBinaryStream *st, tjs_int64 off, bool normalizeName)
{
	tjs_uint64 offset = off;

	std::unique_ptr<tTJSBinaryStream> streamOwner(st);
	std::vector<tjs_uint8> indexbuffer;
	std::vector<tjs_uint8> companionbuffer;

	static const tjs_uint8 cn_File[] =
		{ 0x46/*'F'*/, 0x69/*'i'*/, 0x6c/*'l'*/, 0x65/*'e'*/ };
	static const tjs_uint8 cn_info[] =
		{ 0x69/*'i'*/, 0x6e/*'n'*/, 0x66/*'f'*/, 0x6f/*'o'*/ };
	static const tjs_uint8 cn_segm[] =
		{ 0x73/*'s'*/, 0x65/*'e'*/, 0x67/*'g'*/, 0x6d/*'m'*/ };
	static const tjs_uint8 cn_adlr[] =
		{ 0x61/*'a'*/, 0x64/*'d'*/, 0x6c/*'l'*/, 0x72/*'r'*/ };

	TVPAddLog( TVPFormatMessage(TVPInfoTryingToReadXp3VirtualFileSystemInformationFrom, ArchiveName) );

	int segmentcount = 0;
	try
	{
		// retrieve archive offset
		if(off < 0) TVPGetXP3ArchiveOffset(st, ArchiveName, offset, true);
		const tjs_uint64 archive_size = st->GetSize();
		if(!TVPXP3::Contains(archive_size, offset, 19))
			TVPThrowExceptionMessage(TVPReadError);

		// read index position and seek
		st->SetPosition(11 + offset);

		std::unordered_set<tjs_uint64> visited;
		tjs_uint64 total_index_bytes = 0;
		// read all XP3 indices
		while(true)
		{
			if(!TVPXP3::Contains(archive_size, st->GetPosition(), 8))
				TVPThrowExceptionMessage(TVPReadError);
			tjs_uint64 index_ofs = st->ReadI64LE();
			if(!TVPXP3::Contains(archive_size - offset, index_ofs, 9) ||
				!visited.insert(index_ofs).second || visited.size() > TVPXP3::MaxIndexCount)
				TVPThrowExceptionMessage(TJS_W("XP3: invalid or cyclic index chain"));
			st->SetPosition(index_ofs + offset);

			// read index to memory
			tjs_uint8 index_flag;
			st->ReadBuffer(&index_flag, 1);
			const unsigned method = index_flag & TVP_XP3_INDEX_ENCODE_METHOD_MASK;
			if(method > TVP_XP3_INDEX_ENCODE_ZLIB || (index_flag & ~0x87))
				TVPThrowExceptionMessage(TVPReadError);
			tjs_uint64 stored_size = st->ReadI64LE();
			tjs_uint64 unpacked_size = method == TVP_XP3_INDEX_ENCODE_ZLIB ? st->ReadI64LE() : stored_size;
			if(stored_size > TVPXP3::MaxIndexBytes ||
				unpacked_size > TVPXP3::MaxIndexBytes - total_index_bytes)
				TVPThrowExceptionMessage(TJS_W("XP3: archive index exceeds the supported memory limit"));
			if(!TVPXP3::Contains(archive_size, st->GetPosition(), stored_size))
				TVPThrowExceptionMessage(TVPReadError);
			total_index_bytes += unpacked_size;
			tjs_uint index_size = static_cast<tjs_uint>(unpacked_size);
			indexbuffer.resize(std::max<tjs_uint>(1, index_size));
			tjs_uint8 *indexdata = indexbuffer.data();
			if(method == TVP_XP3_INDEX_ENCODE_ZLIB)
			{
				std::vector<tjs_uint8> compressed(static_cast<size_t>(stored_size));
				st->ReadBuffer(compressed.data(), static_cast<tjs_uint>(stored_size));
				unsigned long destlen = static_cast<unsigned long>(indexbuffer.size());
				int result = uncompress(indexdata, &destlen, compressed.data(),
					static_cast<unsigned long>(stored_size));
				if(result != Z_OK || destlen != index_size)
					TVPThrowExceptionMessage(TVPUncompressionFailed);
			}
			else if(index_size)
				st->ReadBuffer(indexdata, index_size);

			auto index_status = TVPXP3::ValidateIndex(indexdata, index_size, archive_size, offset);
			if(index_status == TVPXP3::IndexStatus::UnsupportedNameTable)
			{
				// Bind the companion to the original index and encrypted Hxv4 table.
				if(offset || (index_flag & TVP_XP3_INDEX_CONTINUE) || total_index_bytes != unpacked_size)
					TVPThrowExceptionMessage(TJS_W("XP3: Hxv4 companion requires a standalone single-index archive"));
				std::vector<tjs_uint8> binding(indexdata, indexdata + index_size);
				TVPXP3::Chunk chunk;
				size_t position = 0;
				unsigned tables = 0;
				while(position < index_size)
				{
					if(!TVPXP3::NextChunk(indexdata, index_size, position, chunk))
						TVPThrowExceptionMessage(TVPReadError);
					if(!chunk.Is("Hxv4")) continue;
					if(++tables != 1 || chunk.size != 14)
						TVPThrowExceptionMessage(TJS_W("XP3: malformed Hxv4 table descriptor"));
					const auto start = TVPXP3::Read64(chunk.data);
					const auto bytes = TVPHxv4::Read32(chunk.data + 8);
					if(bytes < 16 || bytes > TVPXP3::MaxIndexBytes - binding.size() ||
						!TVPXP3::Contains(archive_size, start, bytes))
						TVPThrowExceptionMessage(TJS_W("XP3: invalid Hxv4 table range"));
					size_t begin = binding.size();
					binding.resize(begin + bytes);
					st->SetPosition(start);
					st->ReadBuffer(binding.data() + begin, bytes);
				}
				if(tables != 1) TVPThrowExceptionMessage(TVPReadError);
				ttstr companionName = ArchiveName + TJS_W(".hxidx");
				if(!TVPIsExistentStorageNoSearchNoNormalize(companionName))
					TVPThrowExceptionMessage(TJS_W("XP3: Hxv4 requires a matching .hxidx companion generated by script/prepare_hxv4.py"));
				tTVPStreamHolder companionStream(companionName);
				auto bytes = companionStream.Get()->GetSize();
				if(bytes < TVPHxv4::HeaderSize || bytes > TVPXP3::MaxIndexBytes + TVPHxv4::HeaderSize)
					TVPThrowExceptionMessage(TJS_W("XP3: invalid Hxv4 companion size"));
				companionbuffer.resize(static_cast<size_t>(bytes));
				companionStream.Get()->ReadBuffer(companionbuffer.data(), static_cast<tjs_uint>(bytes));
				TVPHxv4::Companion companion;
				if(!TVPHxv4::ReadCompanion(companionbuffer.data(), companionbuffer.size(), archive_size,
					TVPHxv4::Blake2s(binding.data(), binding.size()), companion))
					TVPThrowExceptionMessage(TJS_W("XP3: invalid or mismatched Hxv4 companion"));
				HxEnabled = true;
				HxMedia = companion.media;
				indexdata = const_cast<tjs_uint8 *>(companion.index);
				index_size = static_cast<tjs_uint>(companion.indexSize);
				index_status = TVPXP3::IndexStatus::Valid;
			}
			if(index_status != TVPXP3::IndexStatus::Valid)
				TVPThrowExceptionMessage(TJS_W("XP3: malformed archive index"));

			// read index information from memory
			tjs_uint ch_file_start = 0;
			tjs_uint ch_file_size = index_size;
			//Count = 0;
			for(;;)
			{
				// find 'File' chunk
				if(!FindChunk(indexdata, cn_File, ch_file_start, ch_file_size))
					break; // not found

				// find 'info' sub-chunk
				tjs_uint ch_info_start = ch_file_start;
				tjs_uint ch_info_size = ch_file_size;
				if(!FindChunk(indexdata, cn_info, ch_info_start, ch_info_size))
					TVPThrowExceptionMessage(TVPReadError);

				// read info sub-chunk
				tArchiveItem item;
				tjs_uint32 flags = ReadI32FromMem(indexdata + ch_info_start + 0);
				if(!TVPAllowExtractProtectedStorage && (flags & TVP_XP3_FILE_PROTECTED))
					TVPThrowExceptionMessage( TVPSpecifiedStorageHadBeenProtected );
				item.OrgSize = ReadI64FromMem(indexdata + ch_info_start + 4);
				item.ArcSize = ReadI64FromMem(indexdata + ch_info_start + 12);

				const tjs_uint16 len = TVPXP3::Read16(indexdata + ch_info_start + 20);
				std::vector<tjs_uint16> name_units(len);
				for(tjs_uint i = 0; i < len; ++i)
					name_units[i] = TVPXP3::Read16(indexdata + ch_info_start + 22 + i * 2);
				ttstr name = TVPStringFromBMPUnicode(name_units.data(), len);
				item.Name = name;
				if (normalizeName)
					NormalizeInArchiveStorageName(item.Name);

				// find 'segm' sub-chunk
				// Each of in-archive storages can be splitted into some segments.
				// Each segment can be compressed or uncompressed independently.
				// segments can share partial area of archive storage. ( this is used for
				// OggVorbis' VQ code book sharing )
				tjs_uint ch_segm_start = ch_file_start;
				tjs_uint ch_segm_size = ch_file_size;
				if(!FindChunk(indexdata, cn_segm, ch_segm_start, ch_segm_size))
					TVPThrowExceptionMessage(TVPReadError);

				// read segm sub-chunk
				tjs_int segment_count = ch_segm_size / 28;
				tjs_uint64 offset_in_archive = 0;
				for(tjs_int i = 0; i<segment_count; i++)
				{
					tjs_uint pos_base = i * 28 + ch_segm_start;
					tTVPXP3ArchiveSegment seg;
					tjs_uint32 flags = ReadI32FromMem(indexdata + pos_base);

					if((flags & TVP_XP3_SEGM_ENCODE_METHOD_MASK) ==
							TVP_XP3_SEGM_ENCODE_RAW)
						seg.IsCompressed = false;
					else if((flags & TVP_XP3_SEGM_ENCODE_METHOD_MASK) ==
							TVP_XP3_SEGM_ENCODE_ZLIB)
						seg.IsCompressed = true;
					else
						TVPThrowExceptionMessage(TVPReadError); // unknown encode method
						
					seg.Start = ReadI64FromMem(indexdata + pos_base + 4) + offset;
						// data offset in archive
					seg.Offset = offset_in_archive; // offset in in-archive storage
					seg.OrgSize = ReadI64FromMem(indexdata + pos_base + 12); // original size
					seg.ArcSize = ReadI64FromMem(indexdata + pos_base + 20); // archived size
					item.Segments.push_back(seg);
					offset_in_archive += seg.OrgSize;
					segmentcount ++;
				}

				// find 'aldr' sub-chunk
				tjs_uint ch_adlr_start = ch_file_start;
				tjs_uint ch_adlr_size = ch_file_size;
				if(!FindChunk(indexdata, cn_adlr, ch_adlr_start, ch_adlr_size))
					TVPThrowExceptionMessage(TVPReadError);

				// read 'aldr' sub-chunk
				item.FileHash = ReadI32FromMem(indexdata + ch_adlr_start);
				if(HxEnabled)
				{
					size_t at = 0;
					TVPXP3::Chunk part;
					while(at < ch_file_size)
					{
						if(!TVPXP3::NextChunk(indexdata + ch_file_start, ch_file_size, at, part))
							TVPThrowExceptionMessage(TVPReadError);
						if(part.Is("hxky")) item.HxFilter.Read(part.data, part.size);
						if(part.Is("hnam")) std::copy(part.data, part.data + 40, item.HxLookup.begin());
					}
				}

				// push information
				ItemVector.push_back(item);

				// to next file
				ch_file_start += ch_file_size;
				ch_file_size = index_size - ch_file_start;
				Count++;
			}

			if(!(index_flag & TVP_XP3_INDEX_CONTINUE))
				break; // continue reading index when the bit sets
		}

		// sort item vector by its name (required for tTVPArchive specification)
		if(normalizeName)
			std::stable_sort(ItemVector.begin(), ItemVector.end());
		if(HxEnabled)
			for(tjs_uint i = 0; i < ItemVector.size(); ++i) HxNames.emplace(ItemVector[i].HxLookup, i);
	}
	catch(...)
	{
 		TVPAddLog( (const tjs_char*)TVPInfoFailed );
		throw;
	}

	TVPAddLog( TVPFormatMessage( TVPInfoDoneWithContains, ttstr(Count), ttstr(segmentcount) ) );
}

//---------------------------------------------------------------------------
tTVPXP3Archive::tTVPXP3Archive(const ttstr & name, tTJSBinaryStream *st, tjs_int64 offset, bool normalizeFileName) : tTVPArchive(name)
{
	if (!st) st = TVPCreateStream(name);
	Init(st, offset, normalizeFileName);
}
//---------------------------------------------------------------------------
tTVPXP3Archive::~tTVPXP3Archive()
{
	TVPFreeArchiveHandlePoolByPointer(this);
}

tTVPArchive * tTVPXP3Archive::Create(const ttstr & name, tTJSBinaryStream *st, bool normalizeFileName)
{
	bool refStream = st;
	if (!st) {
		st = TVPCreateStream(name);
	}
	tjs_uint64 offset;
	if (!TVPGetXP3ArchiveOffset(st, name, offset, false)) {
		if (!refStream) delete st;
		return nullptr;
	}
	return new tTVPXP3Archive(name, st, offset, normalizeFileName);
}

//---------------------------------------------------------------------------
bool tTVPXP3Archive::FindHashedStorage(const ttstr &name, tjs_uint &index)
{
	if(!HxEnabled || name.IsEmpty()) return false;
	ttstr normalized(name);
	NormalizeInArchiveStorageName(normalized);
	std::u16string text;
	for(const tjs_char *p = normalized.c_str(); *p; ++p) text.push_back(static_cast<char16_t>(*p));
	if(!TVPHxv4::ValidName(text)) return false;
	auto found = HxNames.find(TVPHxv4::HashName(text, HxMedia));
	if(found == HxNames.end()) return false;
	index = found->second;
	return true;
}
//---------------------------------------------------------------------------
tTJSBinaryStream * tTVPXP3Archive::CreateStreamByIndex(tjs_uint idx)
{
	if(idx >= ItemVector.size()) TVPThrowExceptionMessage(TVPReadError);

	tArchiveItem &item = ItemVector[idx];

	tTJSBinaryStream *stream = TVPGetCachedArchiveHandle(this, ArchiveName);

	tTVPXP3ArchiveStream *out;
	try
	{
		out = new tTVPXP3ArchiveStream(this, idx, &(item.Segments), stream,
			item.OrgSize);
		if (!HxEnabled && TVPXP3ArchiveContentFilter) {
			tjs_int result = TVPXP3ArchiveContentFilter(item.Name, ArchiveName, item.OrgSize, &out->GetFilterContext());
#define XP3_CONTENT_FILTER_FETCH_FULLDATA 1
			if (result == XP3_CONTENT_FILTER_FETCH_FULLDATA) {
				tTVPMemoryStream *memstr = new tTVPMemoryStream();
				memstr->SetSize(item.OrgSize);
				out->ReadBuffer(memstr->GetInternalBuffer(), item.OrgSize);
				delete out;
				return memstr;
			}
		}
	}
	catch(...)
	{
		TVPReleaseCachedArchiveHandle(this, stream);
		throw;
	}

	return out;
}
//---------------------------------------------------------------------------
bool tTVPXP3Archive::FindChunk(const tjs_uint8 *data, const tjs_uint8 * name,
		tjs_uint &start, tjs_uint &size)
{
	tjs_uint start_save = start;
	tjs_uint size_save = size;

	tjs_uint pos = 0;
	while(pos < size)
	{
		if(size - pos < 12)
			TVPThrowExceptionMessage(TVPReadError);
		bool found = !memcmp(data + start, name, 4);
		start += 4;
		tjs_uint64 r_size = ReadI64FromMem(data + start);
		start += 8;
		tjs_uint size_chunk = (tjs_uint)r_size;
		if(size_chunk != r_size || size_chunk > size - pos - 12)
			TVPThrowExceptionMessage(TVPReadError);
		if(found)
		{
			// found
			size = size_chunk;
			return true;
		}
		start += size_chunk;
		pos += size_chunk + 4 + 8;
	}

	start = start_save;
	size = size_save;
	return false;
}
//---------------------------------------------------------------------------
tjs_int16 tTVPXP3Archive::ReadI16FromMem(const tjs_uint8 *mem)
{
	tjs_uint16 ret = (tjs_uint16)mem[0] | ((tjs_uint16)mem[1] << 8);
	return (tjs_int16)ret;
}
//---------------------------------------------------------------------------
tjs_int32 tTVPXP3Archive::ReadI32FromMem(const tjs_uint8 *mem)
{
	tjs_uint32 ret =  (tjs_uint32)mem[0] | ((tjs_uint32)mem[1] << 8) |
		((tjs_uint32)mem[2] << 16) | ((tjs_uint32)mem[3] << 24);
	return (tjs_int32)ret;
}
//---------------------------------------------------------------------------
tjs_int64 tTVPXP3Archive::ReadI64FromMem(const tjs_uint8 *mem)
{
	tjs_uint64 ret = (tjs_uint64)mem[0] | ((tjs_uint64)mem[1] << 8) |
		((tjs_uint64)mem[2] << 16) | ((tjs_uint64)mem[3] << 24) |
		((tjs_uint64)mem[4] << 32) | ((tjs_uint64)mem[5] << 40) |
		((tjs_uint64)mem[6] << 48) | ((tjs_uint64)mem[7] << 56);
	return (tjs_int64)ret;
}
//---------------------------------------------------------------------------




//---------------------------------------------------------------------------
// Compressed segment cache related
//---------------------------------------------------------------------------
#define TVP_SEGCACHE_ONE_LIMIT (1024*1024)     // max size limit for each segment
#define TVP_SEGCACHE_TOTAL_LIMIT (1024*1024)   // total segment cache size
tjs_uint TVPSegmentCacheLimit = TVP_SEGCACHE_TOTAL_LIMIT;
//---------------------------------------------------------------------------
struct tTVPSegmentCacheSearchData
{
	ttstr Name; // archive name
	tjs_int StorageIndex; // storage index in archive
	tjs_int SegmentIndex; // segment index in storage

	bool operator == (const tTVPSegmentCacheSearchData &rhs) const
	{
		return Name == rhs.Name && StorageIndex == rhs.StorageIndex &&
			SegmentIndex == rhs.SegmentIndex;
	}
};
//---------------------------------------------------------------------------
class tTVPSegmentCacheSearchHashFunc
{
public:
	static tjs_uint32 Make(const tTVPSegmentCacheSearchData &val)
	{
		tjs_uint32 v = tTJSHashFunc<ttstr>::Make(val.Name);

		v ^= val.StorageIndex;
		v ^= (val.SegmentIndex<<2);
		return v;
	}
};
//---------------------------------------------------------------------------
class tTVPSegmentData
{
	tjs_int RefCount;
	tjs_uint Size;
	tjs_uint8 *Data;

public:
	tTVPSegmentData() { RefCount = 1; Size = 0; Data = NULL; }
	~tTVPSegmentData() { if(Data) delete [] Data; }

	void SetData(unsigned long outsize, tTJSBinaryStream *instream,
		unsigned long insize)
	{
		// uncompress data
		tjs_uint8 * indata = new tjs_uint8 [insize];
		try
		{
			instream->Read(indata, insize);

			Data = new tjs_uint8 [outsize];
			unsigned long destlen = outsize;
			int result = uncompress( (unsigned char*)Data, &outsize,
				(unsigned char*)indata, insize);
			if(result != Z_OK || destlen != outsize)
				TVPThrowExceptionMessage(TVPUncompressionFailed);
			Size = outsize;
		}
		catch(...)
		{
			delete [] indata;
			throw;
		}
		delete [] indata;
	}

	const tjs_uint8 * GetData() const { return Data; }
	tjs_uint GetSize() const { return Size; }

	void AddRef() { RefCount ++; }
	void Release()
	{
		if(RefCount == 1)
		{
			delete this;
		}
		else
		{
			RefCount--;
		}
	}
};
//---------------------------------------------------------------------------
typedef tTJSRefHolder<tTVPSegmentData> tTVPSegmentDataHolder;

typedef
tTJSHashTable<tTVPSegmentCacheSearchData, tTVPSegmentDataHolder, tTVPSegmentCacheSearchHashFunc>
	tTVPSegmentCache;
static tTVPSegmentCache TVPSegmentCache;
static tjs_uint TVPSegmentCacheTotalBytes = 0;

static tTJSCriticalSection TVPSegmentCacheCS;
//---------------------------------------------------------------------------
static void TVPCheckSegmentCacheLimit()
{
	tTJSCriticalSectionHolder cs_holder(TVPSegmentCacheCS);

	while(TVPSegmentCacheTotalBytes > TVPSegmentCacheLimit)
	{
		// chop last segment
		tTVPSegmentCache::tIterator i;
		i = TVPSegmentCache.GetLast();
		if(!i.IsNull())
		{
			tjs_uint size = i.GetValue().GetObjectNoAddRef()->GetSize();
			TVPSegmentCacheTotalBytes -= size;
			TVPSegmentCache.ChopLast(1);
		}
		else
		{
			break;
		}
	}
}
//---------------------------------------------------------------------------
void TVPClearXP3SegmentCache()
{
	tTJSCriticalSectionHolder cs_holder(TVPSegmentCacheCS);

	TVPSegmentCache.Clear();
	TVPSegmentCacheTotalBytes = 0;
}
//---------------------------------------------------------------------------
struct tTVPClearSegmentCacheCallback : public tTVPCompactEventCallbackIntf
{
	virtual void TJS_INTF_METHOD OnCompact(tjs_int level)
	{
		if(level >= TVP_COMPACT_LEVEL_DEACTIVATE)
		{
			// clear the segment cache on application deactivate
			TVPClearXP3SegmentCache();
			// also free archive handle pool
			TVPFreeArchiveHandlePool();
		}
	}
} static TVPClearSegmentCacheCallback;
static bool TVPClearSegmentCacheCallbackInit = false;
//---------------------------------------------------------------------------
static tTVPSegmentData * TVPSearchFromSegmentCache(
	const tTVPSegmentCacheSearchData &sdata, tjs_uint32 hash)
{
	tTJSCriticalSectionHolder cs_holder(TVPSegmentCacheCS);

	tTVPSegmentDataHolder * ptr =
		TVPSegmentCache.FindAndTouchWithHash(sdata, hash);
	if(ptr)
	{
		// found in cache
		return ptr->GetObject(); // add-refed
	}

	return NULL; // not found in cache
}
//---------------------------------------------------------------------------
static void TVPPushToSegmentCache(const tTVPSegmentCacheSearchData &sdata, tjs_uint32 hash,
	tTVPSegmentData *data)
{
	if(!TVPClearSegmentCacheCallbackInit)
	{
		TVPAddCompactEventHook(&TVPClearSegmentCacheCallback);
		TVPClearSegmentCacheCallbackInit = true;
	}

	tTJSCriticalSectionHolder cs_holder(TVPSegmentCacheCS);

	tTVPSegmentDataHolder holder(data);
	TVPSegmentCache.AddWithHash(sdata, hash, holder);
	TVPSegmentCacheTotalBytes += data->GetSize();

	TVPCheckSegmentCacheLimit();
}
//---------------------------------------------------------------------------






//---------------------------------------------------------------------------
// tTVPXP3ArchiveStream : stream class for in-archive storage
//---------------------------------------------------------------------------
tTVPXP3ArchiveStream::tTVPXP3ArchiveStream(tTVPXP3Archive *owner,
	tjs_int storageindex,
	std::vector<tTVPXP3ArchiveSegment> *segments, tTJSBinaryStream * stream,
		tjs_uint64 orgsize)
{
	StorageIndex = storageindex;
	Segments = segments;
	SegmentData = NULL;
	CurSegmentNum = 0;
	CurSegment = &(Segments->operator [](0));
	SegmentPos = 0;
	SegmentRemain = CurSegment->OrgSize;
	SegmentOpened = false;
	CurPos = 0;

	LastOpenedSegmentNum = -1;

	Owner = owner;
	Owner->AddRef(); // hook
	Stream = stream;
	OrgSize = orgsize;
}
//---------------------------------------------------------------------------
tTVPXP3ArchiveStream::~tTVPXP3ArchiveStream()
{
	TVPReleaseCachedArchiveHandle(Owner, Stream);
	Owner->Release(); // unhook
	if(SegmentData) SegmentData->Release();
}
//---------------------------------------------------------------------------
void tTVPXP3ArchiveStream::EnsureSegment()
{
	// ensure accessing to current segment
	if(SegmentOpened) return;

	if(LastOpenedSegmentNum == CurSegmentNum)
	{
		if(!CurSegment->IsCompressed)
			Stream->SetPosition(CurSegment->Start + SegmentPos);
		// A same-segment seek is now resolved; following reads are sequential.
		SegmentOpened = true;
		return;
	}

	// erase buffer
	if(SegmentData) SegmentData->Release(), SegmentData = NULL;

	// is compressed segment ?
	if(CurSegment->IsCompressed)
	{
		// a compressed segment

		if(CurSegment->OrgSize >= TVP_SEGCACHE_ONE_LIMIT)
		{
			// too large to cache
			Stream->SetPosition(CurSegment->Start);
			SegmentData = new tTVPSegmentData;
			SegmentData->SetData((tjs_uint)CurSegment->OrgSize,
				Stream, (tjs_uint)CurSegment->ArcSize);
		}
		else
		{
			// search thru segment cache
			tTVPSegmentCacheSearchData sdata;
			sdata.Name = Owner->GetName();
			sdata.StorageIndex = StorageIndex;
			sdata.SegmentIndex = CurSegmentNum;

			tjs_uint32 hash;
			hash = tTVPSegmentCacheSearchHashFunc::Make(sdata);

			SegmentData = TVPSearchFromSegmentCache(sdata, hash);
			if(!SegmentData)
			{
				// not found in cache
				Stream->SetPosition(CurSegment->Start);
				SegmentData = new tTVPSegmentData;
				SegmentData->SetData((tjs_uint)CurSegment->OrgSize,
					Stream, (tjs_uint)CurSegment->ArcSize);

				// add to cache
				TVPPushToSegmentCache(sdata, hash, SegmentData);
			}
		}
	}
	else
	{
		// not a compressed segment

		Stream->SetPosition(CurSegment->Start + SegmentPos);
	}

	SegmentOpened = true;
	LastOpenedSegmentNum = CurSegmentNum;
}
//---------------------------------------------------------------------------
void tTVPXP3ArchiveStream::SeekToPosition(tjs_uint64 pos)
{
	// open segment at 'pos' and seek
	// pos must between zero thru OrgSize
	if(CurPos == pos) return;

	// do binary search to determine current segment number
	tjs_int st = 0;
	tjs_int et = (tjs_int)Segments->size();
	tjs_int seg_num;

	while(true)
	{
		if(et-st <= 1) { seg_num = st; break; }
		tjs_int m = st + (et-st)/2;
		if(Segments->operator[](m).Offset > pos)
			et = m;
		else
			st = m;
	}

	CurSegmentNum = seg_num;
	CurSegment = &(Segments->operator [](CurSegmentNum));
	SegmentOpened = false;

	SegmentPos = pos - CurSegment->Offset;
	SegmentRemain = CurSegment->OrgSize - SegmentPos;
	CurPos = pos;
}
//---------------------------------------------------------------------------
bool tTVPXP3ArchiveStream::OpenNextSegment()
{
	// open next segment
	if(CurSegmentNum == (tjs_int)(Segments->size() -1))
		return false; // no more segments
	CurSegmentNum ++;
	CurSegment = &(Segments->operator [](CurSegmentNum));
	SegmentOpened = false;
	SegmentPos = 0;
	SegmentRemain = CurSegment->OrgSize;
	CurPos = CurSegment->Offset;
	EnsureSegment();
	return true;
}
//---------------------------------------------------------------------------
tjs_uint64 TJS_INTF_METHOD tTVPXP3ArchiveStream::Seek(tjs_int64 offset, tjs_int whence)
{
	tjs_int64 newpos;
	switch(whence)
	{
	case TJS_BS_SEEK_SET:
		newpos = offset;
		if(newpos >= 0 && newpos <= static_cast<tjs_int64>(OrgSize) )
		{
			SeekToPosition(newpos);
		}
		return CurPos;

	case TJS_BS_SEEK_CUR:
		newpos = offset + CurPos;
		if(newpos >= 0 && newpos <= static_cast<tjs_int64>(OrgSize) )
		{
			SeekToPosition(newpos);
		}
		return CurPos;

	case TJS_BS_SEEK_END:
		newpos = offset + OrgSize;
		if(newpos >= 0 && newpos <= static_cast<tjs_int64>(OrgSize) )
		{
			SeekToPosition(newpos);
		}
		return CurPos;
	}
	return CurPos;
}
//---------------------------------------------------------------------------
tjs_uint TJS_INTF_METHOD tTVPXP3ArchiveStream::Read(void *buffer, tjs_uint read_size)
{
	EnsureSegment();

	tjs_uint write_size = 0;
	while(read_size)
	{
		while(SegmentRemain == 0)
		{
			// must go next segment
			if(!OpenNextSegment()) // open next segment
				return write_size; // could not read more
		}

		tjs_uint one_size =
			read_size > SegmentRemain ? (tjs_uint)SegmentRemain : read_size;

		if(CurSegment->IsCompressed)
		{
			// compressed segment; read from uncompressed data in memory
			memcpy((tjs_uint8*)buffer + write_size,
				SegmentData->GetData() + (tjs_uint)SegmentPos, one_size);
		}
		else
		{
			// read directly from stream
			Stream->ReadBuffer((tjs_uint8*)buffer + write_size, one_size);
		}

		// execute filter (for encryption method)
		if(Owner->HasHashedNames())
			Owner->GetHxFilter(StorageIndex).Apply(CurPos, (tjs_uint8*)buffer + write_size, one_size);
		else if(TVPXP3ArchiveExtractionFilter)
		{
			tTVPXP3ExtractionFilterInfo info(CurPos, (tjs_uint8*)buffer + write_size,
				one_size, Owner->GetFileHash(StorageIndex), Owner->GetName(StorageIndex));
			TVPXP3ArchiveExtractionFilter
				( (tTVPXP3ExtractionFilterInfo*) &info, &FilterContext );
		}

		// adjust members
		SegmentPos += one_size;
		CurPos += one_size;
		SegmentRemain -= one_size;
		read_size -= one_size;
		write_size += one_size;
	}

	return write_size;
}
//---------------------------------------------------------------------------
tjs_uint TJS_INTF_METHOD tTVPXP3ArchiveStream::Write(const void *buffer, tjs_uint write_size)
{
	return 0;
}
//---------------------------------------------------------------------------
tjs_uint64 TJS_INTF_METHOD tTVPXP3ArchiveStream::GetSize()
{
	return OrgSize;
}
//---------------------------------------------------------------------------





//---------------------------------------------------------------------------
// Archive extraction utility
//---------------------------------------------------------------------------
#define TVP_LOCAL_TEMP_COPY_BLOCK_SIZE 65536*2
#if 0
// this routine is obsoleted because the Releaser includes over 256-characters
// file name if the user specifies "protect over the archive" option.
// Windows cannot handle such too long filename.
void TVPExtractArchive(const ttstr & name, const ttstr & _destdir, bool allowextractprotected)
{
	// extract file to
	bool TVPAllowExtractProtectedStorage_save =
		TVPAllowExtractProtectedStorage;
	TVPAllowExtractProtectedStorage = allowextractprotected;
	try
	{

		ttstr destdir(_destdir);
		tjs_char last = _destdir.GetLastChar();
		if(_destdir.GetLen() >= 1 && (last != TJS_W('/') && last != TJS_W('\\')))
			destdir += TJS_W('/');

		tTVPArchive *arc = TVPOpenArchive(name);
		try
		{
			tjs_int count = arc->GetCount();
			for(tjs_int i = 0; i < count; i++)
			{
				ttstr name = arc->GetName(i);

				tTJSBinaryStream *src = arc->CreateStreamByIndex(i);
				try
				{
					tTVPStreamHolder dest(destdir + name, TJS_BS_WRITE);
					tjs_uint8 * buffer = new tjs_uint8[TVP_LOCAL_TEMP_COPY_BLOCK_SIZE];
					try
					{
						tjs_uint read;
						while(true)
						{
							read = src->Read(buffer, TVP_LOCAL_TEMP_COPY_BLOCK_SIZE);
							if(read == 0) break;
							dest->WriteBuffer(buffer, read);
						}
					}
					catch(...)
					{
						delete [] buffer;
						throw;
					}
					delete [] buffer;
				}
				catch(...)
				{
//					delete src;
//					throw;
				}
				delete src;
			}

		}
		catch(...)
		{
			arc->Release();
			throw;
		}

		arc->Release();
	}
	catch(...)
	{
		TVPAllowExtractProtectedStorage =
			TVPAllowExtractProtectedStorage_save;
		throw;
	}
	TVPAllowExtractProtectedStorage =
		TVPAllowExtractProtectedStorage_save;
}
#endif
//---------------------------------------------------------------------------


/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*- 
 *
 * Copyright (c) 2006 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <mach-o/loader.h>
#include <mach-o/fat.h>
#include <mach-o/stab.h>

#include <vector>

#include "MachOFileAbstraction.hpp"
#include "Architectures.hpp"


 __attribute__((noreturn))
void throwf(const char* format, ...) 
{
	va_list	list;
	char*	p;
	va_start(list, format);
	vasprintf(&p, format, list);
	va_end(list);
	
	const char*	t = p;
	throw t;
}


template <typename A>
class MachOChecker
{
public:
	static bool									validFile(const uint8_t* fileContent);
	static MachOChecker<A>*						make(const uint8_t* fileContent, uint32_t fileLength, const char* path) 
														{ return new MachOChecker<A>(fileContent, fileLength, path); }
	virtual										~MachOChecker() {}


private:
	typedef typename A::P					P;
	typedef typename A::P::E				E;
	typedef typename A::P::uint_t			pint_t;
	
												MachOChecker(const uint8_t* fileContent, uint32_t fileLength, const char* path);
	void										checkMachHeader();
	void										checkLoadCommands();
	void										checkSection(const macho_segment_command<P>* segCmd, const macho_section<P>* sect);
	uint8_t										loadCommandSizeMask();
	void										checkIndirectSymbolTable();

	const char*									fPath;
	const macho_header<P>*						fHeader;
	uint32_t									fLength;
	const char*									fStrings;
	const char*									fStringsEnd;
	const macho_nlist<P>*						fSymbols;
	uint32_t									fSymbolCount;
	const uint32_t*								fIndirectTable;
	uint32_t									fIndirectTableCount;

};



template <>
bool MachOChecker<ppc>::validFile(const uint8_t* fileContent)
{	
	const macho_header<P>* header = (const macho_header<P>*)fileContent;
	if ( header->magic() != MH_MAGIC )
		return false;
	if ( header->cputype() != CPU_TYPE_POWERPC )
		return false;
	switch (header->filetype()) {
		case MH_EXECUTE:
		case MH_DYLIB:
		case MH_BUNDLE:
		case MH_DYLINKER:
			return true;
	}
	return false;
}

template <>
bool MachOChecker<ppc64>::validFile(const uint8_t* fileContent)
{	
	const macho_header<P>* header = (const macho_header<P>*)fileContent;
	if ( header->magic() != MH_MAGIC_64 )
		return false;
	if ( header->cputype() != CPU_TYPE_POWERPC64 )
		return false;
	switch (header->filetype()) {
		case MH_EXECUTE:
		case MH_DYLIB:
		case MH_BUNDLE:
		case MH_DYLINKER:
			return true;
	}
	return false;
}

template <>
bool MachOChecker<x86>::validFile(const uint8_t* fileContent)
{	
	const macho_header<P>* header = (const macho_header<P>*)fileContent;
	if ( header->magic() != MH_MAGIC )
		return false;
	if ( header->cputype() != CPU_TYPE_I386 )
		return false;
	switch (header->filetype()) {
		case MH_EXECUTE:
		case MH_DYLIB:
		case MH_BUNDLE:
		case MH_DYLINKER:
			return true;
	}
	return false;
}



template <> uint8_t MachOChecker<ppc>::loadCommandSizeMask()	{ return 0x03; }
template <> uint8_t MachOChecker<ppc64>::loadCommandSizeMask()	{ return 0x07; }
template <> uint8_t MachOChecker<x86>::loadCommandSizeMask()	{ return 0x03; }


template <typename A>
MachOChecker<A>::MachOChecker(const uint8_t* fileContent, uint32_t fileLength, const char* path)
 : fHeader(NULL), fLength(fileLength), fStrings(NULL), fSymbols(NULL), fSymbolCount(0), fIndirectTableCount(0)
{
	// sanity check
	if ( ! validFile(fileContent) )
		throw "not a mach-o file that can be checked";

	fPath = strdup(path);
	fHeader = (const macho_header<P>*)fileContent;
	
	// sanity check header
	checkMachHeader();
	
	// check load commands
	checkLoadCommands();
	
	checkIndirectSymbolTable();

}


template <typename A>
void MachOChecker<A>::checkMachHeader()
{
	if ( (fHeader->sizeofcmds() + sizeof(macho_header<P>)) > fLength )
		throw "sizeofcmds in mach_header is larger than file";
	
	uint32_t flags = fHeader->flags();
	uint32_t invalidBits = MH_INCRLINK | MH_LAZY_INIT | 0xFFFC0000;
	if ( flags & invalidBits )
		throw "invalid bits in mach_header flags";
		
}

template <typename A>
void MachOChecker<A>::checkLoadCommands()
{
	// check that all load commands fit within the load command space file
	const uint8_t* const endOfFile = (uint8_t*)fHeader + fLength;
	const uint8_t* const endOfLoadCommands = (uint8_t*)fHeader + sizeof(macho_header<P>) + fHeader->sizeofcmds();
	const uint32_t cmd_count = fHeader->ncmds();
	const macho_load_command<P>* const cmds = (macho_load_command<P>*)((uint8_t*)fHeader + sizeof(macho_header<P>));
	const macho_load_command<P>* cmd = cmds;
	for (uint32_t i = 0; i < cmd_count; ++i) {
		uint32_t size = cmd->cmdsize();
		if ( (size & this->loadCommandSizeMask()) != 0 )
			throwf("load command #%d has a unaligned size", i);
		const uint8_t* endOfCmd = ((uint8_t*)cmd)+cmd->cmdsize();
		if ( endOfCmd > endOfLoadCommands )
			throwf("load command #%d extends beyond the end of the load commands", i);
		if ( endOfCmd > endOfFile )
			throwf("load command #%d extends beyond the end of the file", i);
		switch ( cmd->cmd()	) {
			case macho_segment_command<P>::CMD:
			case LC_SYMTAB:
			case LC_UNIXTHREAD:
			case LC_DYSYMTAB:
			case LC_LOAD_DYLIB:
			case LC_ID_DYLIB:
			case LC_LOAD_DYLINKER:
			case LC_ID_DYLINKER:
			case macho_routines_command<P>::CMD:
			case LC_SUB_FRAMEWORK:
			case LC_SUB_UMBRELLA:
			case LC_SUB_CLIENT:
			case LC_TWOLEVEL_HINTS:
			case LC_PREBIND_CKSUM:
			case LC_LOAD_WEAK_DYLIB:
			case LC_UUID:
				break;
			default:
				throwf("load command #%d is an unknown kind 0x%X", i, cmd->cmd());
		}
		cmd = (const macho_load_command<P>*)endOfCmd;
	}
	
	// check segments
	cmd = cmds;
	std::vector<std::pair<pint_t, pint_t> > segmentAddressRanges;
	std::vector<std::pair<pint_t, pint_t> > segmentFileOffsetRanges;
	const macho_segment_command<P>* linkEditSegment = NULL;
	for (uint32_t i = 0; i < cmd_count; ++i) {
		if ( cmd->cmd() == macho_segment_command<P>::CMD ) {
			const macho_segment_command<P>* segCmd = (const macho_segment_command<P>*)cmd;
			if ( segCmd->cmdsize() != (sizeof(macho_segment_command<P>) + segCmd->nsects() * sizeof(macho_section_content<P>)) )
				throw "invalid segment load command size";
				
			// see if this overlaps another segment address range
			uint64_t startAddr = segCmd->vmaddr();
			uint64_t endAddr = startAddr + segCmd->vmsize();
			for (typename std::vector<std::pair<pint_t, pint_t> >::iterator it = segmentAddressRanges.begin(); it != segmentAddressRanges.end(); ++it) {
				if ( it->first < startAddr ) {
					if ( it->second > startAddr )
						throw "overlapping segment vm addresses";
				}
				else if ( it->first > startAddr ) {
					if ( it->first < endAddr )
						throw "overlapping segment vm addresses";
				}
				else {
					throw "overlapping segment vm addresses";
				}
				segmentAddressRanges.push_back(std::make_pair<pint_t, pint_t>(startAddr, endAddr));
			}
			// see if this overlaps another segment file offset range
			uint64_t startOffset = segCmd->fileoff();
			uint64_t endOffset = startOffset + segCmd->filesize();
			for (typename std::vector<std::pair<pint_t, pint_t> >::iterator it = segmentFileOffsetRanges.begin(); it != segmentFileOffsetRanges.end(); ++it) {
				if ( it->first < startOffset ) {
					if ( it->second > startOffset )
						throw "overlapping segment file data";
				}
				else if ( it->first > startOffset ) {
					if ( it->first < endOffset )
						throw "overlapping segment file data";
				}
				else {
					throw "overlapping segment file data";
				}
				segmentFileOffsetRanges.push_back(std::make_pair<pint_t, pint_t>(startOffset, endOffset));
				// check is within file bounds
				if ( (startOffset > fLength) || (endOffset > fLength) )
					throw "segment file data is past end of file";
			}
			// verify it fits in file
			if ( startOffset > fLength )
				throw "segment fileoff does not fit in file";
			if ( endOffset > fLength )
				throw "segment fileoff+filesize does not fit in file";
				
			// keep LINKEDIT segment 
			if ( strcmp(segCmd->segname(), "__LINKEDIT") == 0 )
				linkEditSegment = segCmd;
				
			// check section ranges
			const macho_section<P>* const sectionsStart = (macho_section<P>*)((char*)segCmd + sizeof(macho_segment_command<P>));
			const macho_section<P>* const sectionsEnd = &sectionsStart[segCmd->nsects()];
			for(const macho_section<P>* sect = sectionsStart; sect < sectionsEnd; ++sect) {
				// check all sections are within segment
				if ( sect->addr() < startAddr )
					throwf("section %s vm address not within segment", sect->sectname());
				if ( (sect->addr()+sect->size()) > endAddr )
					throwf("section %s vm address not within segment", sect->sectname());
				if ( (sect->flags() &SECTION_TYPE) != S_ZEROFILL ) {
					if ( sect->offset() < startOffset )
						throwf("section %s file offset not within segment", sect->sectname());
					if ( (sect->offset()+sect->size()) > endOffset )
						throwf("section %s file offset not within segment", sect->sectname());
				}	
				checkSection(segCmd, sect);
			}
		}
		cmd = (const macho_load_command<P>*)(((uint8_t*)cmd)+cmd->cmdsize());
	}
	
	// verify there was a LINKEDIT segment
	if ( linkEditSegment == NULL )
		throw "no __LINKEDIT segment";
	
	// checks for executables
	bool isStaticExecutable = false;
	if ( fHeader->filetype() == MH_EXECUTE ) {
		isStaticExecutable = true;
		cmd = cmds;
		for (uint32_t i = 0; i < cmd_count; ++i) {
			switch ( cmd->cmd() ) {
				case LC_LOAD_DYLINKER:
					// the existence of a dyld load command makes a executable dynamic
					isStaticExecutable = false;
					break;
			}
			cmd = (const macho_load_command<P>*)(((uint8_t*)cmd)+cmd->cmdsize());
		}
		if ( isStaticExecutable ) {
			if ( fHeader->flags() != MH_NOUNDEFS )
				throw "invalid bits in mach_header flags for static executable";
		}
	}

	// check LC_SYMTAB and LC_DYSYMTAB
	cmd = cmds;
	bool foundDynamicSymTab = false;
	for (uint32_t i = 0; i < cmd_count; ++i) {
		switch ( cmd->cmd() ) {
			case LC_SYMTAB:
				{
					const macho_symtab_command<P>* symtab = (macho_symtab_command<P>*)cmd;
					fSymbolCount = symtab->nsyms();
					fSymbols = (const macho_nlist<P>*)((char*)fHeader + symtab->symoff());
					if ( symtab->symoff() < linkEditSegment->fileoff() )
						throw "symbol table not in __LINKEDIT";
					if ( (symtab->symoff() + fSymbolCount*sizeof(macho_nlist<P>*)) > (linkEditSegment->fileoff()+linkEditSegment->filesize()) )
						throw "symbol table end not in __LINKEDIT";
					fStrings = (char*)fHeader + symtab->stroff();
					fStringsEnd = fStrings + symtab->strsize();
					if ( symtab->stroff() < linkEditSegment->fileoff() )
						throw "string pool not in __LINKEDIT";
					if ( (symtab->stroff()+symtab->strsize()) > (linkEditSegment->fileoff()+linkEditSegment->filesize()) )
						throw "string pool extends beyond __LINKEDIT";
				}
				break;
			case LC_DYSYMTAB:
				{
					if ( isStaticExecutable )
						throw "LC_DYSYMTAB should not be used in static executable";
					foundDynamicSymTab = true;
					const macho_dysymtab_command<P>* dsymtab = (struct macho_dysymtab_command<P>*)cmd;
					fIndirectTable = (uint32_t*)((char*)fHeader + dsymtab->indirectsymoff());
					fIndirectTableCount = dsymtab->nindirectsyms();
					if ( dsymtab->indirectsymoff() < linkEditSegment->fileoff() )
						throw "indirect symbol table not in __LINKEDIT";
					if ( (dsymtab->indirectsymoff()+fIndirectTableCount*8) > (linkEditSegment->fileoff()+linkEditSegment->filesize()) )
						throw "indirect symbol table not in __LINKEDIT";
				}
				break;
		}
		cmd = (const macho_load_command<P>*)(((uint8_t*)cmd)+cmd->cmdsize());
	}
	if ( !isStaticExecutable && !foundDynamicSymTab )
		throw "missing dynamic symbol table";
	if ( fStrings == NULL )
		throw "missing symbol table";
	
	
	
}

template <typename A>
void MachOChecker<A>::checkSection(const macho_segment_command<P>* segCmd, const macho_section<P>* sect)
{
	uint8_t sectionType = (sect->flags() & SECTION_TYPE);
	if ( sectionType == S_ZEROFILL ) {
		if ( sect->offset() != 0 )
			throwf("section offset should be zero for zero-fill section %s", sect->sectname());
	}
	
	// more section tests here
}

template <typename A>
void MachOChecker<A>::checkIndirectSymbolTable()
{
	const macho_load_command<P>* const cmds = (macho_load_command<P>*)((uint8_t*)fHeader + sizeof(macho_header<P>));
	const uint32_t cmd_count = fHeader->ncmds();
	const macho_load_command<P>* cmd = cmds;
	for (uint32_t i = 0; i < cmd_count; ++i) {
		if ( cmd->cmd() == macho_segment_command<P>::CMD ) {
			const macho_segment_command<P>* segCmd = (const macho_segment_command<P>*)cmd;
			const macho_section<P>* const sectionsStart = (macho_section<P>*)((char*)segCmd + sizeof(macho_segment_command<P>));
			const macho_section<P>* const sectionsEnd = &sectionsStart[segCmd->nsects()];
			for(const macho_section<P>* sect = sectionsStart; sect < sectionsEnd; ++sect) {
				// make sure all magic sections that use indirect symbol table fit within it
				uint32_t start = 0;
				uint32_t elementSize = 0;
				switch ( sect->flags() & SECTION_TYPE ) {
					case S_SYMBOL_STUBS:
						elementSize = sect->reserved2();
						start = sect->reserved1();
						break;
					case S_LAZY_SYMBOL_POINTERS:
					case S_NON_LAZY_SYMBOL_POINTERS:
						elementSize = sizeof(pint_t);
						start = sect->reserved1();
						break;
				}
				if ( elementSize != 0 ) {
					uint32_t count = sect->size() / elementSize;
					if ( (count*elementSize) != sect->size() )
						throwf("%s section size is not an even multiple of element size", sect->sectname());
					if ( (start+count) > fIndirectTableCount )
						throwf("%s section references beyond end of indirect symbol table (%d > %d)", sect->sectname(), start+count, fIndirectTableCount );
				}
			}
		}
		cmd = (const macho_load_command<P>*)(((uint8_t*)cmd)+cmd->cmdsize());
	}
}


static void check(const char* path)
{
	struct stat stat_buf;
	
	try {
		int fd = ::open(path, O_RDONLY, 0);
		if ( fd == -1 )
			throw "cannot open file";
		::fstat(fd, &stat_buf);
		uint32_t length = stat_buf.st_size;
		uint8_t* p = (uint8_t*)::mmap(NULL, stat_buf.st_size, PROT_READ, MAP_FILE, fd, 0);
		::close(fd);
		const mach_header* mh = (mach_header*)p;
		if ( mh->magic == OSSwapBigToHostInt32(FAT_MAGIC) ) {
			const struct fat_header* fh = (struct fat_header*)p;
			const struct fat_arch* archs = (struct fat_arch*)(p + sizeof(struct fat_header));
			for (unsigned long i=0; i < fh->nfat_arch; ++i) {
				if ( archs[i].cputype == CPU_TYPE_POWERPC ) {
					if ( MachOChecker<ppc>::validFile(p + archs[i].offset) )
						MachOChecker<ppc>::make(p + archs[i].offset, archs[i].size, path);
					else
						throw "in universal file, ppc slice does not contain ppc mach-o";
				}
				else if ( archs[i].cputype == CPU_TYPE_I386 ) {
					if ( MachOChecker<x86>::validFile(p + archs[i].offset) )
						MachOChecker<x86>::make(p + archs[i].offset, archs[i].size, path);
					else
						throw "in universal file, i386 slice does not contain i386 mach-o";
				}
				else if ( archs[i].cputype == CPU_TYPE_POWERPC64 ) {
					if ( MachOChecker<ppc64>::validFile(p + archs[i].offset) )
						MachOChecker<ppc64>::make(p + archs[i].offset, archs[i].size, path);
					else
						throw "in universal file, ppc64 slice does not contain ppc64 mach-o";
				}
				else {
						throw "in universal file, unknown architecture slice";
				}
			}
		}
		else if ( MachOChecker<x86>::validFile(p) ) {
			MachOChecker<x86>::make(p, length, path);
		}
		else if ( MachOChecker<ppc>::validFile(p) ) {
			MachOChecker<ppc>::make(p, length, path);
		}
		else if ( MachOChecker<ppc64>::validFile(p) ) {
			MachOChecker<ppc64>::make(p, length, path);
		}
		else {
			throw "not a known file type";
		}
	}
	catch (const char* msg) {
		throwf("%s in %s", msg, path);
	}
}


int main(int argc, const char* argv[])
{
	try {
		for(int i=1; i < argc; ++i) {
			const char* arg = argv[i];
			if ( arg[0] == '-' ) {
				if ( strcmp(arg, "-no_content") == 0 ) {
					
				}
				else {
					throwf("unknown option: %s\n", arg);
				}
			}
			else {
				check(arg);
			}
		}
	}
	catch (const char* msg) {
		fprintf(stderr, "machocheck failed: %s\n", msg);
		return 1;
	}
	
	return 0;
}




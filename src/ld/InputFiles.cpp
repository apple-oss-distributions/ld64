/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-*
 *
 * Copyright (c) 2009-2011 Apple Inc. All rights reserved.
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
 

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <mach/mach_time.h>
#include <mach/vm_statistics.h>
#include <mach/mach_init.h>
#include <mach/mach_host.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/fat.h>

#include <string>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <list>
#include <algorithm>
#include <ext/hash_map>
#include <ext/hash_set>
#include <dlfcn.h>
#include <AvailabilityMacros.h>

#include "Options.h"

#include "InputFiles.h"
#include "macho_relocatable_file.h"
#include "macho_dylib_file.h"
#include "archive_file.h"
#include "lto_file.h"
#include "opaque_section_file.h"


namespace ld {
namespace tool {




class DSOHandleAtom : public ld::Atom {
public:
									DSOHandleAtom(const char* nm, ld::Atom::Scope sc, 
														ld::Atom::SymbolTableInclusion inc, bool preload=false)
										: ld::Atom(preload ? _s_section_preload : _s_section, 
													ld::Atom::definitionRegular, ld::Atom::combineNever,
													sc, ld::Atom::typeUnclassified, inc, true, false, false, 
													 ld::Atom::Alignment(1)), _name(nm) {}

	virtual ld::File*						file() const					{ return NULL; }
	virtual bool							translationUnitSource(const char** dir, const char** ) const 
																			{ return false; }
	virtual const char*						name() const					{ return _name; }
	virtual uint64_t						size() const					{ return 0; }
	virtual uint64_t						objectAddress() const			{ return 0; }
	virtual void							copyRawContent(uint8_t buffer[]) const
																			{ }
	virtual void							setScope(Scope)					{ }

	virtual									~DSOHandleAtom() {}
	
	static ld::Section						_s_section;
	static ld::Section						_s_section_preload;
	static DSOHandleAtom					_s_atomAll;
	static DSOHandleAtom					_s_atomExecutable;
	static DSOHandleAtom					_s_atomDylib;
	static DSOHandleAtom					_s_atomBundle;
	static DSOHandleAtom					_s_atomDyld;
	static DSOHandleAtom					_s_atomObjectFile;
	static DSOHandleAtom					_s_atomPreload;
private:
	const char*								_name;
};
ld::Section DSOHandleAtom::_s_section("__TEXT", "__mach_header", ld::Section::typeMachHeader, true);
ld::Section DSOHandleAtom::_s_section_preload("__HEADER", "__mach_header", ld::Section::typeMachHeader, true);
DSOHandleAtom DSOHandleAtom::_s_atomAll("___dso_handle", ld::Atom::scopeLinkageUnit, ld::Atom::symbolTableNotIn);
DSOHandleAtom DSOHandleAtom::_s_atomExecutable("__mh_execute_header", ld::Atom::scopeGlobal, ld::Atom::symbolTableInAndNeverStrip);
DSOHandleAtom DSOHandleAtom::_s_atomDylib("__mh_dylib_header", ld::Atom::scopeLinkageUnit, ld::Atom::symbolTableNotIn);
DSOHandleAtom DSOHandleAtom::_s_atomBundle("__mh_bundle_header", ld::Atom::scopeLinkageUnit, ld::Atom::symbolTableNotIn);
DSOHandleAtom DSOHandleAtom::_s_atomDyld("__mh_dylinker_header", ld::Atom::scopeLinkageUnit, ld::Atom::symbolTableNotIn);
DSOHandleAtom DSOHandleAtom::_s_atomObjectFile("__mh_object_header", ld::Atom::scopeLinkageUnit, ld::Atom::symbolTableNotIn);
DSOHandleAtom DSOHandleAtom::_s_atomPreload("__mh_preload_header", ld::Atom::scopeLinkageUnit, ld::Atom::symbolTableNotIn, true);



class PageZeroAtom : public ld::Atom {
public:
									PageZeroAtom(uint64_t sz)
										: ld::Atom(_s_section, ld::Atom::definitionRegular, ld::Atom::combineNever,
											ld::Atom::scopeTranslationUnit, ld::Atom::typeZeroFill, 
											symbolTableNotIn, true, false, false, ld::Atom::Alignment(12)),
											_size(sz) {}

	virtual ld::File*						file() const					{ return NULL; }
	virtual bool							translationUnitSource(const char** dir, const char** ) const 
																			{ return false; }
	virtual const char*						name() const					{ return "page zero"; }
	virtual uint64_t						size() const					{ return _size; }
	virtual uint64_t						objectAddress() const			{ return 0; }
	virtual void							copyRawContent(uint8_t buffer[]) const 
																			{ }
	virtual void							setScope(Scope)					{ }

	virtual									~PageZeroAtom() {}
	
	static ld::Section						_s_section;
	static DSOHandleAtom					_s_atomAll;
private:
	uint64_t								_size;
};
ld::Section PageZeroAtom::_s_section("__PAGEZERO", "__pagezero", ld::Section::typePageZero, true);


class CustomStackAtom : public ld::Atom {
public:
									CustomStackAtom(uint64_t sz)
										: ld::Atom(_s_section, ld::Atom::definitionRegular, ld::Atom::combineNever,
											ld::Atom::scopeTranslationUnit, ld::Atom::typeZeroFill, 
											symbolTableNotIn, false, false, false, ld::Atom::Alignment(12)),
											_size(sz) {}

	virtual ld::File*						file() const					{ return NULL; }
	virtual bool							translationUnitSource(const char** dir, const char** ) const 
																			{ return false; }
	virtual const char*						name() const					{ return "custom stack"; }
	virtual uint64_t						size() const					{ return _size; }
	virtual uint64_t						objectAddress() const			{ return 0; }
	virtual void							copyRawContent(uint8_t buffer[]) const 
																			{ }
	virtual void							setScope(Scope)					{ }

	virtual									~CustomStackAtom() {}
	
private:
	uint64_t								_size;
	static ld::Section						_s_section;
};
ld::Section CustomStackAtom::_s_section("__UNIXSTACK", "__stack", ld::Section::typeStack, true);



const char* InputFiles::fileArch(const uint8_t* p, unsigned len)
{
	const char* result = mach_o::relocatable::archName(p);
	if ( result != NULL  )
		 return result;
		 
	result = lto::archName(p, len);
	if ( result != NULL  )
		 return result;
	
	if ( strncmp((const char*)p, "!<arch>\n", 8) == 0 )
		return "archive";
	
	return "unsupported file format";	 
}


ld::File* InputFiles::makeFile(const Options::FileInfo& info, bool indirectDylib)
{
	// map in whole file
	uint64_t len = info.fileLen;
	int fd = ::open(info.path, O_RDONLY, 0);
	if ( fd == -1 )
		throwf("can't open file, errno=%d", errno);
	if ( info.fileLen < 20 )
		throw "file too small";

	uint8_t* p = (uint8_t*)::mmap(NULL, info.fileLen, PROT_READ, MAP_FILE | MAP_PRIVATE, fd, 0);
	if ( p == (uint8_t*)(-1) )
		throwf("can't map file, errno=%d", errno);

	// if fat file, skip to architecture we want
	// Note: fat header is always big-endian
	bool isFatFile = false;
	const fat_header* fh = (fat_header*)p;
	if ( fh->magic == OSSwapBigToHostInt32(FAT_MAGIC) ) {
		isFatFile = true;
		const struct fat_arch* archs = (struct fat_arch*)(p + sizeof(struct fat_header));
		uint32_t sliceToUse;
		bool sliceFound = false;
		if ( _options.preferSubArchitecture() ) {
			// first try to find a slice that match cpu-type and cpu-sub-type
			for (uint32_t i=0; i < OSSwapBigToHostInt32(fh->nfat_arch); ++i) {
				if ( (OSSwapBigToHostInt32(archs[i].cputype) == (uint32_t)_options.architecture())
				  && (OSSwapBigToHostInt32(archs[i].cpusubtype) == (uint32_t)_options.subArchitecture()) ) {
					sliceToUse = i;
					sliceFound = true;
					break;
				}
			}
		}
		if ( !sliceFound ) {
			// look for any slice that matches just cpu-type
			for (uint32_t i=0; i < OSSwapBigToHostInt32(fh->nfat_arch); ++i) {
				if ( OSSwapBigToHostInt32(archs[i].cputype) == (uint32_t)_options.architecture() ) {
					sliceToUse = i;
					sliceFound = true;
					break;
				}
			}
		}
		if ( sliceFound ) {
			uint32_t fileOffset = OSSwapBigToHostInt32(archs[sliceToUse].offset);
			len = OSSwapBigToHostInt32(archs[sliceToUse].size);
			if ( fileOffset+len > info.fileLen ) {
				throwf("truncated fat file. Slice from %u to %llu is past end of file with length %llu", 
						fileOffset, fileOffset+len, info.fileLen);
			}
			// if requested architecture is page aligned within fat file, then remap just that portion of file
			if ( (fileOffset & 0x00000FFF) == 0 ) {
				// unmap whole file
				munmap((caddr_t)p, info.fileLen);
				// re-map just part we need
				p = (uint8_t*)::mmap(NULL, len, PROT_READ, MAP_FILE | MAP_PRIVATE, fd, fileOffset);
				if ( p == (uint8_t*)(-1) )
					throwf("can't re-map file, errno=%d", errno);
			}
			else {
				p = &p[fileOffset];
			}
		}
	}
	::close(fd);

	// see if it is an object file
	mach_o::relocatable::ParserOptions objOpts;
	objOpts.architecture		= _options.architecture();
	objOpts.objSubtypeMustMatch = !_options.allowSubArchitectureMismatches();
	objOpts.logAllFiles			= _options.logAllFiles();
	objOpts.convertUnwindInfo	= _options.needsUnwindInfoSection();
	objOpts.subType				= _options.subArchitecture();
	ld::relocatable::File* objResult = mach_o::relocatable::parse(p, len, info.path, info.modTime, _nextInputOrdinal, objOpts);
	if ( objResult != NULL ) 
		return this->addObject(objResult, info, len);

	// see if it is an llvm object file
	objResult = lto::parse(p, len, info.path, info.modTime, _nextInputOrdinal, _options.architecture(), _options.subArchitecture(), _options.logAllFiles());
	if ( objResult != NULL ) 
		return this->addObject(objResult, info, len);

	// see if it is a dynamic library
	ld::dylib::File* dylibResult = mach_o::dylib::parse(p, len, info.path, info.modTime, _options, _nextInputOrdinal, info.options.fBundleLoader, indirectDylib);
	if ( dylibResult != NULL ) 
		return this->addDylib(dylibResult, info, len);

	// see if it is a static library
	::archive::ParserOptions archOpts;
	archOpts.objOpts				= objOpts;
	archOpts.forceLoadThisArchive	= info.options.fForceLoad;
	archOpts.forceLoadAll			= _options.fullyLoadArchives();
	archOpts.forceLoadObjC			= _options.loadAllObjcObjectsFromArchives();
	archOpts.objcABI2				= _options.objCABIVersion2POverride();
	archOpts.verboseLoad			= _options.whyLoad();
	archOpts.logAllFiles			= _options.logAllFiles();
	ld::archive::File* archiveResult = ::archive::parse(p, len, info.path, info.modTime, _nextInputOrdinal, archOpts);
	if ( archiveResult != NULL ) {
		// <rdar://problem/9740166> force loaded archives should be in LD_TRACE
		if ( (info.options.fForceLoad || _options.fullyLoadArchives()) && _options.traceArchives() ) 
			logArchive(archiveResult);
		return this->addArchive(archiveResult, info, len);
	}
	
	// does not seem to be any valid linker input file, check LTO misconfiguration problems
	if ( lto::archName((uint8_t*)p, len) != NULL ) {
		if ( lto::libLTOisLoaded() ) {
			throwf("file was built for %s which is not the architecture being linked (%s)", fileArch(p, len), _options.architectureName());
		}
		else {
			const char* libLTO = "libLTO.dylib";
			char ldPath[PATH_MAX];
			char tmpPath[PATH_MAX];
			char libLTOPath[PATH_MAX];
			uint32_t bufSize = PATH_MAX;
			if ( _NSGetExecutablePath(ldPath, &bufSize) != -1 ) {
				if ( realpath(ldPath, tmpPath) != NULL ) {
					char* lastSlash = strrchr(tmpPath, '/');
					if ( lastSlash != NULL )
						strcpy(lastSlash, "/../lib/libLTO.dylib");
					libLTO = tmpPath;
					if ( realpath(tmpPath, libLTOPath) != NULL ) 
						libLTO = libLTOPath;
				}
			}
			throwf("could not process llvm bitcode object file, because %s could not be loaded", libLTO);
		}
	}

	// error handling
	if ( ((fat_header*)p)->magic == OSSwapBigToHostInt32(FAT_MAGIC) ) {
		throwf("missing required architecture %s in file", _options.architectureName());
	}
	else {
		if ( isFatFile )
			throwf("file is universal but does not contain a(n) %s slice", _options.architectureName());
		else
			throwf("file was built for %s which is not the architecture being linked (%s)", fileArch(p, len), _options.architectureName());
	}
}

void InputFiles::logDylib(ld::File* file, bool indirect)
{
	if ( _options.traceDylibs() ) {
		const char* fullPath = file->path();
		char realName[MAXPATHLEN];
		if ( realpath(fullPath, realName) != NULL )
			fullPath = realName;
		const ld::dylib::File* dylib = dynamic_cast<const ld::dylib::File*>(file);
		if ( (dylib != NULL ) && dylib->willBeUpwardDylib() ) {
			// don't log upward dylibs when XBS is computing dependencies
			logTraceInfo("[Logging for XBS] Used upward dynamic library: %s\n", fullPath);
		}
		else {
			if ( indirect )
				logTraceInfo("[Logging for XBS] Used indirect dynamic library: %s\n", fullPath);
			else
				logTraceInfo("[Logging for XBS] Used dynamic library: %s\n", fullPath);
		}
	}
}

void InputFiles::logArchive(ld::File* file) const
{
	if ( _options.traceArchives() && (_archiveFilesLogged.count(file) == 0) ) {
		// <rdar://problem/4947347> LD_TRACE_ARCHIVES should only print out when a .o is actually used from an archive
		_archiveFilesLogged.insert(file);
		const char* fullPath = file->path();
		char realName[MAXPATHLEN];
		if ( realpath(fullPath, realName) != NULL )
			fullPath = realName;
		logTraceInfo("[Logging for XBS] Used static archive: %s\n", fullPath);
	}
}


void InputFiles::logTraceInfo(const char* format, ...) const
{
	// one time open() of custom LD_TRACE_FILE
	static int trace_file = -1;
	if ( trace_file == -1 ) {
		const char *trace_file_path = _options.traceOutputFile();
		if ( trace_file_path != NULL ) {
			trace_file = open(trace_file_path, O_WRONLY | O_APPEND | O_CREAT, 0666);
			if ( trace_file == -1 )
				throwf("Could not open or create trace file: %s", trace_file_path);
		}
		else {
			trace_file = fileno(stderr);
		}
	}

	char trace_buffer[MAXPATHLEN * 2];
    va_list ap;
	va_start(ap, format);
	int length = vsnprintf(trace_buffer, sizeof(trace_buffer), format, ap);
	va_end(ap);
	char* buffer_ptr = trace_buffer;

	while (length > 0) {
		ssize_t amount_written = write(trace_file, buffer_ptr, length);
		if(amount_written == -1)
			/* Failure to write shouldn't fail the build. */
			return;
		buffer_ptr += amount_written;
		length -= amount_written;
	}
}

ld::dylib::File* InputFiles::findDylib(const char* installPath, const char* fromPath)
{
	//fprintf(stderr, "findDylib(%s, %s)\n", installPath, fromPath);
	InstallNameToDylib::iterator pos = _installPathToDylibs.find(installPath);
	if ( pos != _installPathToDylibs.end() ) {
		return pos->second;
	}
	else {
		// allow -dylib_path option to override indirect library to use
		for (std::vector<Options::DylibOverride>::const_iterator dit = _options.dylibOverrides().begin(); dit != _options.dylibOverrides().end(); ++dit) {
			if ( strcmp(dit->installName,installPath) == 0 ) {
				try {
					Options::FileInfo info = _options.findFile(dit->useInstead);
					ld::File* reader = this->makeFile(info, true);
					ld::dylib::File* dylibReader = dynamic_cast<ld::dylib::File*>(reader);
					if ( dylibReader != NULL ) {
						//_installPathToDylibs[strdup(installPath)] = dylibReader;
						this->logDylib(dylibReader, true);
						return dylibReader;
					}
					else 
						throwf("indirect dylib at %s is not a dylib", dit->useInstead);
				}
				catch (const char* msg) {
					warning("ignoring -dylib_file option, %s", msg);
				}
			}
		}
		char newPath[MAXPATHLEN];
		// handle @loader_path
		if ( strncmp(installPath, "@loader_path/", 13) == 0 ) {
			strcpy(newPath, fromPath);
			char* addPoint = strrchr(newPath,'/');
			if ( addPoint != NULL )
				strcpy(&addPoint[1], &installPath[13]);
			else
				strcpy(newPath, &installPath[13]);
			installPath = newPath;
		}
		// note: @executable_path case is handled inside findFileUsingPaths()
		// search for dylib using -F and -L paths
		Options::FileInfo info = _options.findFileUsingPaths(installPath);
		try {
			ld::File* reader = this->makeFile(info, true);
			ld::dylib::File* dylibReader = dynamic_cast<ld::dylib::File*>(reader);
			if ( dylibReader != NULL ) {
				//assert(_installPathToDylibs.find(installPath) !=  _installPathToDylibs.end());
				//_installPathToDylibs[strdup(installPath)] = dylibReader;
				this->logDylib(dylibReader, true);
				return dylibReader;
			}
			else 
				throwf("indirect dylib at %s is not a dylib", info.path);
		}
		catch (const char* msg) {
			throwf("in %s, %s", info.path, msg);
		}
	}
}



void InputFiles::createIndirectDylibs()
{
	_allDirectDylibsLoaded = true;

	// mark all dylibs initially specified as required and check if they can be used
	for (InstallNameToDylib::iterator it=_installPathToDylibs.begin(); it != _installPathToDylibs.end(); it++) {
		it->second->setExplicitlyLinked();
		this->checkDylibClientRestrictions(it->second);
	}
	
	// keep processing dylibs until no more dylibs are added
	unsigned long lastMapSize = 0;
	std::set<ld::dylib::File*>  dylibsProcessed;
	while ( lastMapSize != _allDylibs.size() ) {
		lastMapSize = _allDylibs.size();
		// can't iterator _installPathToDylibs while modifying it, so use temp buffer
		std::vector<ld::dylib::File*> unprocessedDylibs;
		for (std::set<ld::dylib::File*>::iterator it=_allDylibs.begin(); it != _allDylibs.end(); it++) {
			if ( dylibsProcessed.count(*it) == 0 )
				unprocessedDylibs.push_back(*it);
		}
		for (std::vector<ld::dylib::File*>::iterator it=unprocessedDylibs.begin(); it != unprocessedDylibs.end(); it++) {
			dylibsProcessed.insert(*it);
			(*it)->processIndirectLibraries(this, _options.implicitlyLinkIndirectPublicDylibs());
		}
	}
	
	// go back over original dylibs and mark sub frameworks as re-exported
	if ( _options.outputKind() == Options::kDynamicLibrary ) {
		const char* myLeaf = strrchr(_options.installPath(), '/');
		if ( myLeaf != NULL ) {
			for (std::vector<class ld::File*>::const_iterator it=_inputFiles.begin(); it != _inputFiles.end(); it++) {
				ld::dylib::File* dylibReader = dynamic_cast<ld::dylib::File*>(*it);
				if ( dylibReader != NULL ) {
					const char* childParent = dylibReader->parentUmbrella();
					if ( childParent != NULL ) {
						if ( strcmp(childParent, &myLeaf[1]) == 0 ) {
							// mark that this dylib will be re-exported
							dylibReader->setWillBeReExported();
						}
					}
				}
			}
		}
	}
	
}

void InputFiles::createOpaqueFileSections()
{
	// extra command line section always at end
	for (Options::ExtraSection::const_iterator it=_options.extraSectionsBegin(); it != _options.extraSectionsEnd(); ++it) {
		_inputFiles.push_back(opaque_section::parse(it->segmentName, it->sectionName, it->path, it->data,
															it->dataLen, _nextInputOrdinal));
		// bump ordinal
		_nextInputOrdinal++;
	}

}


void InputFiles::checkDylibClientRestrictions(ld::dylib::File* dylib)
{
	// Check for any restrictions on who can link with this dylib  
	const char* dylibParentName = dylib->parentUmbrella() ;
	const std::vector<const char*>* clients = dylib->allowableClients();
	if ( (dylibParentName != NULL) || (clients != NULL) ) {
		// only dylibs that are in an umbrella or have a client list need verification
		const char* installName = _options.installPath();
		const char* installNameLastSlash = strrchr(installName, '/');
		bool isParent = false;
		bool isSibling = false;
		bool isAllowableClient = false;
		// There are three cases:
		if ( (dylibParentName != NULL) && (installNameLastSlash != NULL) ) {
			// starts after last slash
			const char* myName = &installNameLastSlash[1];
			unsigned int myNameLen = strlen(myName);
			if ( strncmp(myName, "lib", 3) == 0 )
				myName = &myName[3];
			// up to first dot
			const char* firstDot = strchr(myName, '.');
			if ( firstDot != NULL )
				myNameLen = firstDot - myName;
			// up to first underscore
			const char* firstUnderscore = strchr(myName, '_');
			if ( (firstUnderscore != NULL) && ((firstUnderscore - myName) < (int)myNameLen) )
				myNameLen = firstUnderscore - myName;
		
			// case 1) The dylib has a parent umbrella, and we are creating the parent umbrella
			isParent = ( (strlen(dylibParentName) == myNameLen) && (strncmp(myName, dylibParentName, myNameLen) == 0) );
			
			// case 2) The dylib has a parent umbrella, and we are creating a sibling with the same parent
			isSibling = ( (_options.umbrellaName() != NULL) && (strcmp(_options.umbrellaName(), dylibParentName) == 0) );
		}

		if ( !isParent && !isSibling && (clients != NULL) ) {
			// case 3) the dylib has a list of allowable clients, and we are creating one of them
			const char* clientName = _options.clientName();
			int clientNameLen = 0;
			if ( clientName != NULL ) {
				// use client name as specified on command line
				clientNameLen = strlen(clientName);
			}
			else {
				// infer client name from output path (e.g. xxx/libfoo_variant.A.dylib --> foo, Bar.framework/Bar_variant --> Bar)
				clientName = installName;
				clientNameLen = strlen(clientName);
				// starts after last slash
				if ( installNameLastSlash != NULL )
					clientName = &installNameLastSlash[1];
				if ( strncmp(clientName, "lib", 3) == 0 )
					clientName = &clientName[3];
				// up to first dot
				const char* firstDot = strchr(clientName, '.');
				if ( firstDot != NULL )
					clientNameLen = firstDot - clientName;
				// up to first underscore
				const char* firstUnderscore = strchr(clientName, '_');
				if ( (firstUnderscore != NULL) && ((firstUnderscore - clientName) < clientNameLen) )
					clientNameLen = firstUnderscore - clientName;
			}

			// Use clientName to check if this dylib is able to link against the allowable clients.
			for (std::vector<const char*>::const_iterator it = clients->begin(); it != clients->end(); it++) {
				if ( strncmp(*it, clientName, clientNameLen) == 0 )
					isAllowableClient = true;
			}
		}
	
		if ( !isParent && !isSibling && !isAllowableClient ) {
			if ( dylibParentName != NULL ) {
				throwf("cannot link directly with %s.  Link against the umbrella framework '%s.framework' instead.", 
					dylib->path(), dylibParentName);
			}
			else {
				throwf("cannot link directly with %s", dylib->path());
			}
		}
	}
}


void InputFiles::inferArchitecture(Options& opts, const char** archName)
{
	_inferredArch = true;
	// scan all input files, looking for a thin .o file.
	// the first one found is presumably the architecture to link
	uint8_t buffer[sizeof(mach_header_64)];
	const std::vector<Options::FileInfo>& files = opts.getInputFiles();
	for (std::vector<Options::FileInfo>::const_iterator it = files.begin(); it != files.end(); ++it) {
		int fd = ::open(it->path, O_RDONLY, 0);
		if ( fd != -1 ) {
			ssize_t amount = read(fd, buffer, sizeof(buffer));
			::close(fd);
			if ( amount >= (ssize_t)sizeof(buffer) ) {
				cpu_type_t type;
				cpu_subtype_t subtype;
				if ( mach_o::relocatable::isObjectFile(buffer, &type, &subtype) ) {
					opts.setArchitecture(type, subtype);
					*archName = opts.architectureName();
					return;
				}
			}
		}
	}

	// no thin .o files found, so default to same architecture this tool was built as
	warning("-arch not specified");
#if __ppc__
	opts.setArchitecture(CPU_TYPE_POWERPC, CPU_SUBTYPE_POWERPC_ALL);
#elif __i386__
	opts.setArchitecture(CPU_TYPE_I386, CPU_SUBTYPE_X86_ALL);
#elif __ppc64__
	opts.setArchitecture(CPU_TYPE_POWERPC64, CPU_SUBTYPE_POWERPC_ALL);
#elif __x86_64__
	opts.setArchitecture(CPU_TYPE_X86_64, CPU_SUBTYPE_X86_64_ALL);
#elif __arm__
	opts.setArchitecture(CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V6);
#else
	#error unknown default architecture
#endif
	*archName = opts.architectureName();
}


InputFiles::InputFiles(Options& opts, const char** archName) 
 : _totalObjectSize(0), _totalArchiveSize(0), 
   _totalObjectLoaded(0), _totalArchivesLoaded(0), _totalDylibsLoaded(0),
	_options(opts), _bundleLoader(NULL), _nextInputOrdinal(1), 
	_allDirectDylibsLoaded(false), _inferredArch(false)
{
//	fStartCreateReadersTime = mach_absolute_time();
	if ( opts.architecture() == 0 ) {
		// command line missing -arch, so guess arch
		inferArchitecture(opts, archName);
	}

	const std::vector<Options::FileInfo>& files = opts.getInputFiles();
	if ( files.size() == 0 )
		throw "no object files specified";
	// add all direct object, archives, and dylibs
	_inputFiles.reserve(files.size());
	for (std::vector<Options::FileInfo>::const_iterator it = files.begin(); it != files.end(); ++it) {
		const Options::FileInfo& entry = *it;
		try {
			_inputFiles.push_back(this->makeFile(entry, false));
		}
		catch (const char* msg) {
			if ( (strstr(msg, "architecture") != NULL) && !_options.errorOnOtherArchFiles() ) {
				if ( opts.ignoreOtherArchInputFiles() ) {
					// ignore, because this is about an architecture not in use
				}
				else {
					warning("ignoring file %s, %s", entry.path, msg);
				}
			}
			else {
				throwf("in %s, %s", entry.path, msg);
			}
		}
	}

	this->createIndirectDylibs();
	this->createOpaqueFileSections();
}



ld::File* InputFiles::addArchive(ld::File* reader, const Options::FileInfo& info, uint64_t mappedLen)
{
	// bump ordinal
	_nextInputOrdinal += reader->subFileCount();

	// update stats
	_totalArchiveSize += mappedLen;
	_totalArchivesLoaded++;
	return reader;
}


ld::File* InputFiles::addObject(ld::relocatable::File* file, const Options::FileInfo& info, uint64_t mappedLen)
{
	// bump ordinal
	_nextInputOrdinal++;

	// update stats
	_totalObjectSize += mappedLen;
	_totalObjectLoaded++;
	return file;
}


ld::File* InputFiles::addDylib(ld::dylib::File* reader, const Options::FileInfo& info, uint64_t mappedLen)
{
	_allDylibs.insert(reader);
	
	if ( (reader->installPath() == NULL) && !info.options.fBundleLoader ) {
		// this is a "blank" stub
		// silently ignore it
		return reader;
	}
	// store options about how dylib will be used in dylib itself
	if ( info.options.fWeakImport )
		reader->setForcedWeakLinked();
	if ( info.options.fReExport )
		reader->setWillBeReExported();
	if ( info.options.fUpward ) {
		if ( _options.outputKind() == Options::kDynamicLibrary ) 
			reader->setWillBeUpwardDylib();
		else 
			warning("ignoring upward dylib option for %s\n", info.path);
	}
	if ( info.options.fLazyLoad )
		reader->setWillBeLazyLoadedDylb();
	
	// add to map of loaded dylibs
	const char* installPath = reader->installPath();
	if ( installPath != NULL ) {
		InstallNameToDylib::iterator pos = _installPathToDylibs.find(installPath);
		if ( pos == _installPathToDylibs.end() ) {
			_installPathToDylibs[strdup(installPath)] = reader;
		}
		else {
			bool dylibOnCommandLineTwice = ( strcmp(pos->second->path(), reader->path()) == 0 );
			bool isSymlink = false;
			// ignore if this is a symlink to a dylib we've already loaded
			if ( !dylibOnCommandLineTwice ) {
				char existingDylibPath[PATH_MAX];
				if ( realpath(pos->second->path(), existingDylibPath) != NULL ) {
					char newDylibPath[PATH_MAX];
					if ( realpath(reader->path(), newDylibPath) != NULL ) {
						isSymlink = ( strcmp(existingDylibPath, newDylibPath) == 0 );
					}
				}
			}
			if ( !dylibOnCommandLineTwice && !isSymlink )
				warning("dylibs with same install name: %s and %s", pos->second->path(), reader->path());
		}
	}
	else if ( info.options.fBundleLoader )
		_bundleLoader = reader;

	// log direct readers
	if ( !_allDirectDylibsLoaded ) 
		this->logDylib(reader, false);

	// bump ordinal
	_nextInputOrdinal++;

	// update stats
	_totalDylibsLoaded++;

	return reader;
}


bool InputFiles::forEachInitialAtom(ld::File::AtomHandler& handler) const
{
	bool didSomething = false;
	for (std::vector<ld::File*>::const_iterator it=_inputFiles.begin(); it != _inputFiles.end(); ++it) {
		didSomething |= (*it)->forEachAtom(handler);
	}
	if ( didSomething || true ) {
		switch ( _options.outputKind() ) {
			case Options::kStaticExecutable:
			case Options::kDynamicExecutable:
				// add implicit __dso_handle label
				handler.doAtom(DSOHandleAtom::_s_atomExecutable);
				handler.doAtom(DSOHandleAtom::_s_atomAll);
				if ( _options.pageZeroSize() != 0 ) 
					handler.doAtom(*new PageZeroAtom(_options.pageZeroSize()));
				if ( _options.hasCustomStack() ) 
					handler.doAtom(*new CustomStackAtom(_options.customStackSize()));
				break;
			case Options::kDynamicLibrary:
				// add implicit __dso_handle label
				handler.doAtom(DSOHandleAtom::_s_atomDylib);
				handler.doAtom(DSOHandleAtom::_s_atomAll);
				break;
			case Options::kDynamicBundle:
				// add implicit __dso_handle label
				handler.doAtom(DSOHandleAtom::_s_atomBundle);
				handler.doAtom(DSOHandleAtom::_s_atomAll);
				break;
			case Options::kDyld:
				// add implicit __dso_handle label
				handler.doAtom(DSOHandleAtom::_s_atomDyld);
				handler.doAtom(DSOHandleAtom::_s_atomAll);
				break;
			case Options::kPreload:
				// add implicit __mh_preload_header label
				handler.doAtom(DSOHandleAtom::_s_atomPreload);
				break;
			case Options::kObjectFile:
				handler.doAtom(DSOHandleAtom::_s_atomObjectFile);
				break;
			case Options::kKextBundle:
				// add implicit __dso_handle label
				handler.doAtom(DSOHandleAtom::_s_atomAll);
				break;
		}
	}
	return didSomething;
}


bool InputFiles::searchLibraries(const char* name, bool searchDylibs, bool searchArchives, bool dataSymbolOnly, ld::File::AtomHandler& handler) const
{
	// check each input file 
	for (std::vector<ld::File*>::const_iterator it=_inputFiles.begin(); it != _inputFiles.end(); ++it) {
		ld::File* file = *it;
		// if this reader is a static archive that has the symbol we need, pull in all atoms in that module
		// if this reader is a dylib that exports the symbol we need, have it synthesize an atom for us.
		ld::dylib::File* dylibFile = dynamic_cast<ld::dylib::File*>(file);
		ld::archive::File* archiveFile = dynamic_cast<ld::archive::File*>(file);
		if ( searchDylibs && (dylibFile != NULL) ) {
			//fprintf(stderr, "searchLibraries(%s), looking in linked %s\n", name, dylibFile->path() );
			if ( dylibFile->justInTimeforEachAtom(name, handler) ) {
				// we found a definition in this dylib
				// done, unless it is a weak definition in which case we keep searching
				if ( !dylibFile->hasWeakExternals() || !dylibFile->hasWeakDefinition(name))
					return true;
				// else continue search for a non-weak definition
			}
		}
		else if ( searchArchives && (archiveFile != NULL) ) {
			if ( dataSymbolOnly ) {
				if ( archiveFile->justInTimeDataOnlyforEachAtom(name, handler) ) {
					if ( _options.traceArchives() ) 
						logArchive(file);
					// found data definition in static library, done
					return true;
				}
			}
			else {
				if ( archiveFile->justInTimeforEachAtom(name, handler) ) {
					if ( _options.traceArchives() ) 
						logArchive(file);
					// found definition in static library, done
					return true;
				}
			}
		}
	}

	// search indirect dylibs
	if ( searchDylibs ) {
		for (InstallNameToDylib::const_iterator it=_installPathToDylibs.begin(); it != _installPathToDylibs.end(); ++it) {
			ld::dylib::File* dylibFile = it->second;
			bool searchThisDylib = false;
			if ( _options.nameSpace() == Options::kTwoLevelNameSpace ) {
				// for two level namesapce, just check all implicitly linked dylibs
				searchThisDylib = dylibFile->implicitlyLinked() && !dylibFile->explicitlyLinked();
			}
			else {
				// for flat namespace, check all indirect dylibs
				searchThisDylib = ! dylibFile->explicitlyLinked();
			}
			if ( searchThisDylib ) {
				//fprintf(stderr, "searchLibraries(%s), looking in implicitly linked %s\n", name, dylibFile->path() );
				if ( dylibFile->justInTimeforEachAtom(name, handler) ) {
					// we found a definition in this dylib
					// done, unless it is a weak definition in which case we keep searching
					if ( !dylibFile->hasWeakExternals() || !dylibFile->hasWeakDefinition(name))
						return true;
					// else continue search for a non-weak definition
				}
			}			
		}
	}

	return false;
}


bool InputFiles::searchWeakDefInDylib(const char* name) const
{
	// search all relevant dylibs to see if any of a weak-def with this name
	for (InstallNameToDylib::const_iterator it=_installPathToDylibs.begin(); it != _installPathToDylibs.end(); ++it) {
		ld::dylib::File* dylibFile = it->second;
		if ( dylibFile->implicitlyLinked() || dylibFile->explicitlyLinked() ) {
			if ( dylibFile->hasWeakExternals() && dylibFile->hasWeakDefinition(name) ) {
				return true;
			}
		}
	}
	return false;
}

void InputFiles::dylibs(ld::Internal& state)
{
	bool dylibsOK = false;
	switch ( _options.outputKind() ) {
		case Options::kDynamicExecutable:
		case Options::kDynamicLibrary:
		case Options::kDynamicBundle:
			dylibsOK = true;
			break;
		case Options::kStaticExecutable:
		case Options::kDyld:
		case Options::kPreload:
		case Options::kObjectFile:
		case Options::kKextBundle:
			dylibsOK = false;
			break;
	}

	// add command line dylibs in order
	for (std::vector<ld::File*>::const_iterator it=_inputFiles.begin(); it != _inputFiles.end(); ++it) {
		ld::dylib::File* dylibFile = dynamic_cast<ld::dylib::File*>(*it);
		// only add dylibs that are not "blank" dylib stubs
		if ( (dylibFile != NULL) && ((dylibFile->installPath() != NULL) || (dylibFile == _bundleLoader)) ) {
			if ( dylibsOK )
				state.dylibs.push_back(dylibFile);
			else
				warning("unexpected dylib (%s) on link line", dylibFile->path());
		}
	}
	// add implicitly linked dylibs
	if ( _options.nameSpace() == Options::kTwoLevelNameSpace ) {
		for (InstallNameToDylib::const_iterator it=_installPathToDylibs.begin(); it != _installPathToDylibs.end(); ++it) {
			ld::dylib::File* dylibFile = it->second;
			if ( dylibFile->implicitlyLinked() && dylibsOK ) 
				state.dylibs.push_back(dylibFile);
		}
	}
	// and -bundle_loader
	state.bundleLoader = _bundleLoader;
}


} // namespace tool 
} // namespace ld 


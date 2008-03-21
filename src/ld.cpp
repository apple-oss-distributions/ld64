/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-
 *
 * Copyright (c) 2005-2006 Apple Computer, Inc. All rights reserved.
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
#include <mach-o/fat.h>


#include <string>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <list>
#include <algorithm>
#include <ext/hash_map>

#include "Options.h"

#include "ObjectFile.h"

#include "MachOReaderRelocatable.hpp"
#include "MachOReaderArchive.hpp"
#include "MachOReaderDylib.hpp"
#include "MachOWriterExecutable.hpp"

#include "SectCreate.h"

#if 0
static void dumpAtom(ObjectFile::Atom* atom)
{
	//printf("atom:    %p\n", atom);

	// name
	printf("name:    %s\n",  atom->getDisplayName());

	// scope
	switch ( atom->getScope() ) {
		case ObjectFile::Atom::scopeTranslationUnit:
			printf("scope:   translation unit\n");
			break;
		case ObjectFile::Atom::scopeLinkageUnit:
			printf("scope:   linkage unit\n");
			break;
		case ObjectFile::Atom::scopeGlobal:
			printf("scope:   global\n");
			break;
		default:
			printf("scope:   unknown\n");
	}

	// kind
	switch ( atom->getDefinitinonKind() ) {
		case ObjectFile::Atom::kRegularDefinition:
			printf("kind:     regular\n");
			break;
		case ObjectFile::Atom::kWeakDefinition:
			printf("kind:     weak\n");
			break;
		case ObjectFile::Atom::kTentativeDefinition:
			printf("kind:     tentative\n");
			break;
		case ObjectFile::Atom::kExternalDefinition:
			printf("kind:     import\n");
			break;
		case ObjectFile::Atom::kExternalWeakDefinition:
			printf("kind:     weak import\n");
			break;
		default:
			printf("scope:   unknown\n");
	}

	// segment and section
	printf("section: %s,%s\n", atom->getSegment().getName(), atom->getSectionName());

	// attributes
	printf("attrs:   ");
	if ( atom->dontDeadStrip() )
		printf("dont-dead-strip ");
	if ( atom->isZeroFill() )
		printf("zero-fill ");
	printf("\n");

	// size
	printf("size:    0x%012llX\n", atom->getSize());

	// content
	uint8_t content[atom->getSize()];
	atom->copyRawContent(content);
	printf("content: ");
	if ( strcmp(atom->getSectionName(), "__cstring") == 0 ) {
		printf("\"%s\"", content);
	}
	else {
		for (unsigned int i=0; i < sizeof(content); ++i)
			printf("%02X ", content[i]);
	}
	printf("\n");

	// references
	std::vector<ObjectFile::Reference*>&  references = atom->getReferences();
	const int refCount = references.size();
	printf("references: (%u)\n", refCount);
	for (int i=0; i < refCount; ++i) {
		ObjectFile::Reference* ref = references[i];
		printf("   %s\n", ref->getDescription());
	}

	// attributes

}

#endif

class CStringComparor
{
public:
	bool operator()(const char* left, const char* right) const { return (strcmp(left, right) < 0); }
};

class CStringEquals
{
public:
	bool operator()(const char* left, const char* right) const { return (strcmp(left, right) == 0); }
};

class Section : public ObjectFile::Section
{
public:
	static Section*	find(const char* sectionName, const char* segmentName, bool zeroFill);
	static void		assignIndexes();

private:
					Section(const char* sectionName, const char* segmentName, bool zeroFill);

	struct Sorter {
		static int	segmentOrdinal(const char* segName);
		bool operator()(Section* left, Section* right);
	};

	typedef __gnu_cxx::hash_map<const char*, class Section*, __gnu_cxx::hash<const char*>, CStringEquals> NameToSection;
	//typedef std::map<const char*, class Section*, CStringComparor> NameToSection;

	const char*		fSectionName;
	const char*		fSegmentName;
	bool			fZeroFill;

	static NameToSection			fgMapping;
	static std::vector<Section*>	fgSections;
};

Section::NameToSection	Section::fgMapping;
std::vector<Section*>	Section::fgSections;

Section::Section(const char* sectionName, const char* segmentName, bool zeroFill)
 : fSectionName(sectionName), fSegmentName(segmentName), fZeroFill(zeroFill)
{
	//fprintf(stderr, "new Section(%s, %s)\n", sectionName, segmentName);
}

Section* Section::find(const char* sectionName, const char* segmentName, bool zeroFill)
{
	NameToSection::iterator pos = fgMapping.find(sectionName);
	if ( pos != fgMapping.end() ) {
		if ( strcmp(pos->second->fSegmentName, segmentName) == 0 )
			return pos->second;
		// otherwise same section name is used in different segments, look slow way
		for (std::vector<Section*>::iterator it=fgSections.begin(); it != fgSections.end(); it++) {
			if ( (strcmp((*it)->fSectionName, sectionName) == 0) && (strcmp((*it)->fSegmentName, segmentName) == 0) )
				return *it;
		}
	}

	// does not exist, so make a new one
	Section* sect = new Section(sectionName, segmentName, zeroFill);
	sect->fIndex = fgMapping.size();
	fgMapping[sectionName] = sect;
	fgSections.push_back(sect);

	if ( (strcmp(sectionName, "__text") == 0) && (strcmp(segmentName, "__TEXT") == 0) ) {
		// special case __textcoal_nt to be right after __text
		find("__textcoal_nt", "__TEXT", false);
	}

	return sect;
}

int Section::Sorter::segmentOrdinal(const char* segName)
{
	if ( strcmp(segName, "__PAGEZERO") == 0 )
		return 1;
	if ( strcmp(segName, "__TEXT") == 0 )
		return 2;
	if ( strcmp(segName, "__DATA") == 0 )
		return 3;
	if ( strcmp(segName, "__OBJC") == 0 )
		return 4;
	if ( strcmp(segName, "__LINKEDIT") == 0 )
		return INT_MAX;	// linkedit segment should always sort last
	else
		return 5;
}


bool Section::Sorter::operator()(Section* left, Section* right)
{
	// Segment is primary sort key
	const char* leftSegName = left->fSegmentName;
	const char* rightSegName = right->fSegmentName;
	int segNameCmp = strcmp(leftSegName, rightSegName);
	if ( segNameCmp != 0 )
	{
		int leftSegOrdinal = segmentOrdinal(leftSegName);
		int rightSegOrdinal = segmentOrdinal(rightSegName);
		if ( leftSegOrdinal < rightSegOrdinal )
			return true;
		if ( leftSegOrdinal == rightSegOrdinal )
			return segNameCmp < 0;
		return false;
	}

	// zerofill section sort to the end
	if ( !left->fZeroFill && right->fZeroFill )
		return true;
	if ( left->fZeroFill && !right->fZeroFill )
		return false;

	// section discovery order is last sort key
	return left->fIndex < right->fIndex;
}

void Section::assignIndexes()
{
	//printf("unsorted:\n");
	//for (std::vector<Section*>::iterator it=fgSections.begin(); it != fgSections.end(); it++) {
	//	printf("section: name=%s, segment: name=%s, discovery order=%d\n", (*it)->fSectionName, (*it)->fSegmentName, (*it)->fIndex);
	//}

	// sort it
	std::sort(fgSections.begin(), fgSections.end(), Section::Sorter());

	// assign correct section ordering to each Section object
	unsigned int newOrder = 1;
	for (std::vector<Section*>::iterator it=fgSections.begin(); it != fgSections.end(); it++)
		(*it)->fIndex = newOrder++;

	//printf("sorted:\n");
	//for (std::vector<Section*>::iterator it=fgSections.begin(); it != fgSections.end(); it++) {
	//	printf("section: name=%s\n", (*it)->fSectionName);
	//}
}

class Linker {
public:
						Linker(int argc, const char* argv[]);

	const char*			getArchPrefix();
	const char*			architectureName();
	bool				showArchitectureInErrors();
	bool				isInferredArchitecture();
	void				createReaders();
	void				createWriter();
	void				addInputFile(ObjectFile::Reader* reader);
	void				setOutputFile(ExecutableFile::Writer* writer);
	void				link();


private:
	struct WhyLiveBackChain
	{
		WhyLiveBackChain*	previous;
		const char*			name;
	};

	ObjectFile::Reader*	createReader(const Options::FileInfo&);
	void				addAtom(ObjectFile::Atom& atom);
	void				addAtoms(std::vector<class ObjectFile::Atom*>& atoms);
	void				buildAtomList();
	void				loadAndResolve();
	void				loadUndefines();
	void				checkUndefines();
	void				addWeakAtomOverrides();
	void				resolveReferences();
	void				deadStripResolve();
	void				addLiveRoot(const char* name);
	void				sortAtoms();
	void				tweakLayout();
	void				writeDotOutput();
	static bool			minimizeStab(ObjectFile::Reader::Stab& stab);
	static const char*	truncateStabString(const char* str);
	void				collectDebugInfo();
	void				writeOutput();
	ObjectFile::Atom*	entryPoint();
	ObjectFile::Atom*	dyldHelper();
	const char*			assureFullPath(const char* path);
	void				markLive(ObjectFile::Atom& atom, Linker::WhyLiveBackChain* previous);
	void				collectStabs(ObjectFile::Reader* reader, std::map<const class ObjectFile::Atom*, uint32_t>& atomOrdinals);
	void				synthesizeDebugNotes(std::vector<class ObjectFile::Atom*>& allAtomsByReader);
	void				printStatistics();
	void				printTime(const char* msg, uint64_t partTime, uint64_t totalTime);
	char*				commatize(uint64_t in, char* out);
	void				getVMInfo(vm_statistics_data_t& info);
	cpu_type_t			inferArchitecture();

	void				resolve(ObjectFile::Reference* reference);
	void				resolveFrom(ObjectFile::Reference* reference);
	void				addJustInTimeAtoms(const char* name);

	ObjectFile::Reader*	addDylib(ObjectFile::Reader* reader, const Options::FileInfo& info, uint64_t mappedLen);
	ObjectFile::Reader*	addObject(ObjectFile::Reader* reader, const Options::FileInfo& info, uint64_t mappedLen);
	ObjectFile::Reader*	addArchive(ObjectFile::Reader* reader, const Options::FileInfo& info, uint64_t mappedLen);
	void				addIndirectLibraries(ObjectFile::Reader* reader);
	bool				haveIndirectLibrary(const char* path, ObjectFile::Reader* reader);
	bool				haveDirectLibrary(const char* path);

	void				logTraceInfo(const char* format, ...);


	class SymbolTable
	{
	public:
							SymbolTable(Linker&);
		void				require(const char* name);
		bool				add(ObjectFile::Atom& atom);
		ObjectFile::Atom*	find(const char* name);
		unsigned int		getRequireCount() { return fRequireCount; }
		void				getNeededNames(bool andWeakDefintions, std::vector<const char*>& undefines);
	private:
		typedef __gnu_cxx::hash_map<const char*, ObjectFile::Atom*, __gnu_cxx::hash<const char*>, CStringEquals> Mapper;
		Linker&				fOwner;
		Mapper				fTable;
		unsigned int		fRequireCount;
	};

	struct AtomSorter
	{
		bool operator()(ObjectFile::Atom* left, ObjectFile::Atom* right);
	};

	typedef std::map<const char*, uint32_t, CStringComparor> SectionOrder;

	struct IndirectLibrary {
		const char*							path;
		uint64_t							fileLen;
		ObjectFile::Reader*					reader;
		std::set<ObjectFile::Reader*>		parents;
		ObjectFile::Reader*					reExportedViaDirectLibrary;
	};

	ObjectFile::Reader* findDirectLibraryWhichReExports(struct IndirectLibrary& indirectLib);

	Options												fOptions;
	SymbolTable											fGlobalSymbolTable;
	unsigned int										fWeakSymbolsAddedCount;
	std::vector<class ObjectFile::Reader*>				fInputFiles;
	ExecutableFile::Writer*								fOutputFile;
	std::vector<ExecutableFile::DyLibUsed>				fDynamicLibraries;
	std::list<IndirectLibrary>							fIndirectDynamicLibraries;
	std::vector<class ObjectFile::Reader*>				fReadersThatHaveSuppliedAtoms;
	std::vector<class ObjectFile::Atom*>				fAllAtoms;
	std::set<class ObjectFile::Atom*>					fDeadAtoms;
	std::set<ObjectFile::Atom*>							fLiveAtoms;
	std::set<ObjectFile::Atom*>							fLiveRootAtoms;
	std::vector<class ObjectFile::Reader::Stab>			fStabs;
	std::vector<class ObjectFile::Atom*>				fAtomsWithUnresolvedReferences;
	bool												fCreateUUID;
	SectionOrder										fSectionOrder;
	unsigned int										fNextSortOrder;
	unsigned int										fNextObjectFileOrder;
	cpu_type_t											fArchitecture;
	const char*											fArchitectureName;
	bool												fArchitectureInferred;
	bool												fDirectLibrariesComplete;
	uint64_t											fOutputFileSize;
	uint64_t											fStartTime;
	uint64_t											fStartCreateReadersTime;
	uint64_t											fStartCreateWriterTime;
	uint64_t											fStartBuildAtomsTime;
	uint64_t											fStartLoadUndefinesTime;
	uint64_t											fStartResolveTime;
	uint64_t											fStartSortTime;
	uint64_t											fStartDebugTime;
	uint64_t											fStartWriteTime;
	uint64_t											fEndTime;
	uint64_t											fTotalObjectSize;
	uint64_t											fTotalArchiveSize;
	uint32_t											fTotalObjectLoaded;
	uint32_t											fTotalArchivesLoaded;
	uint32_t											fTotalDylibsLoaded;
	vm_statistics_data_t								fStartVMInfo;
};


Linker::Linker(int argc, const char* argv[])
	: fOptions(argc, argv), fGlobalSymbolTable(*this), fOutputFile(NULL), fCreateUUID(false), fNextSortOrder(1),
	  fNextObjectFileOrder(1), fArchitecture(0), fArchitectureInferred(false), fDirectLibrariesComplete(false),
	  fOutputFileSize(0), fTotalObjectSize(0),
	  fTotalArchiveSize(0),  fTotalObjectLoaded(0), fTotalArchivesLoaded(0), fTotalDylibsLoaded(0)
{
	fStartTime = mach_absolute_time();
	if ( fOptions.printStatistics() )
		getVMInfo(fStartVMInfo);

	fArchitecture = fOptions.architecture();
	if ( fArchitecture == 0 ) {
		// -arch not specified, scan .o files to figure out what it should be
		fArchitecture = inferArchitecture();
		fArchitectureInferred = true;
	}
	switch (fArchitecture) {
		case CPU_TYPE_POWERPC:
			fArchitectureName = "ppc";
			break;
		case CPU_TYPE_POWERPC64:
			fArchitectureName = "ppc64";
			break;
		case CPU_TYPE_I386:
			fArchitectureName = "i386";
			break;
		case CPU_TYPE_X86_64:
			fArchitectureName = "x86_64";
			break;
		default:
			fArchitectureName = "unknown architecture";
			break;
	}
}

const char*	Linker::architectureName()
{
	return fArchitectureName;
}

bool Linker::showArchitectureInErrors()
{
	return fOptions.printArchPrefix();
}

bool Linker::isInferredArchitecture()
{
	return fArchitectureInferred;
}

cpu_type_t Linker::inferArchitecture()
{
	// scan all input files, looking for a thin .o file.
	// the first one found is presumably the architecture to link
	uint8_t buffer[sizeof(mach_header_64)];
	std::vector<Options::FileInfo>& files = fOptions.getInputFiles();
	for (std::vector<Options::FileInfo>::iterator it = files.begin(); it != files.end(); ++it) {
		int fd = ::open(it->path, O_RDONLY, 0);
		if ( fd != -1 ) {
			ssize_t amount = read(fd, buffer, sizeof(buffer));
			::close(fd);
			if ( amount >= (ssize_t)sizeof(buffer) ) {
				if ( mach_o::relocatable::Reader<ppc>::validFile(buffer) ) {
					//fprintf(stderr, "ld64 warning: -arch not used, infering -arch ppc based on %s\n", it->path);
					return CPU_TYPE_POWERPC;
				}
				else if ( mach_o::relocatable::Reader<ppc64>::validFile(buffer) ) {
					//fprintf(stderr, "ld64 warning: -arch not used, infering -arch ppc64 based on %s\n", it->path);
					return CPU_TYPE_POWERPC64;
				}
				else if ( mach_o::relocatable::Reader<x86>::validFile(buffer) ) {
					//fprintf(stderr, "ld64 warning: -arch not used, infering -arch i386 based on %s\n", it->path);
					return CPU_TYPE_I386;
				}
				else if ( mach_o::relocatable::Reader<x86_64>::validFile(buffer) ) {
					//fprintf(stderr, "ld64 warning: -arch not used, infering -arch x86_64 based on %s\n", it->path);
					return CPU_TYPE_X86_64;
				}
			}
		}
	}

	// no thin .o files found, so default to same architecture this was built as
	fprintf(stderr, "ld64 warning: -arch not specified\n");
#if __ppc__
	return CPU_TYPE_POWERPC;
#elif __i386__
	return CPU_TYPE_I386;
#elif __ppc64__
	return CPU_TYPE_POWERPC64;
#elif __x86_64__
	return CPU_TYPE_X86_64;
#else
	#error unknown default architecture
#endif
}


void Linker::addInputFile(ObjectFile::Reader* reader)
{
	reader->setSortOrder(fNextObjectFileOrder++);
	fInputFiles.push_back(reader);
}

void Linker::setOutputFile(ExecutableFile::Writer* writer)
{
	fOutputFile = writer;
}

class InSet
{
public:
	InSet(std::set<ObjectFile::Atom*>& deadAtoms) : fDeadAtoms(deadAtoms) {}

	bool operator()(ObjectFile::Atom*& atom) const {
		return ( fDeadAtoms.count(atom) != 0 );
	}

private:
	std::set<ObjectFile::Atom*>& fDeadAtoms;
};

void Linker::loadAndResolve()
{
	if ( fOptions.deadStrip() == Options::kDeadStripOff ) {
		// without dead-code-stripping:
		// find atoms to resolve all undefines
		this->loadUndefines();
		// verify nothing is missing
		this->checkUndefines();
		// once all undefines fulfill, then bind all references
		this->resolveReferences();
		// remove atoms weak atoms that have been overridden
		fAllAtoms.erase(std::remove_if(fAllAtoms.begin(), fAllAtoms.end(), InSet(fDeadAtoms)), fAllAtoms.end());
	}
	else {
		// with dead code stripping:
		// start binding references from roots,
		this->deadStripResolve();
		// verify nothing is missing
		this->checkUndefines();
	}
}

void Linker::link()
{
	this->buildAtomList();
	this->loadAndResolve();
	this->sortAtoms();
	this->tweakLayout();
	this->writeDotOutput();
	this->collectDebugInfo();
	this->writeOutput();
	this->printStatistics();

	if ( fOptions.pauseAtEnd() )
		sleep(10);
}

void Linker::printTime(const char* msg, uint64_t partTime, uint64_t totalTime)
{
	static uint64_t sUnitsPerSecond = 0;
	if ( sUnitsPerSecond == 0 ) {
		struct mach_timebase_info timeBaseInfo;
		if ( mach_timebase_info(&timeBaseInfo) == KERN_SUCCESS ) {
			sUnitsPerSecond = 1000000000LL * timeBaseInfo.denom / timeBaseInfo.numer;
			//fprintf(stderr, "sUnitsPerSecond=%llu\n", sUnitsPerSecond);
		}
	}
	if ( partTime < sUnitsPerSecond ) {
		uint32_t milliSecondsTimeTen = (partTime*10000)/sUnitsPerSecond;
		uint32_t milliSeconds = milliSecondsTimeTen/10;
		uint32_t percentTimesTen = (partTime*1000)/totalTime;
		uint32_t percent = percentTimesTen/10;
		fprintf(stderr, "%s: %u.%u milliseconds (%u.%u%%)\n", msg, milliSeconds, milliSecondsTimeTen-milliSeconds*10, percent, percentTimesTen-percent*10);
	}
	else {
		uint32_t secondsTimeTen = (partTime*10)/sUnitsPerSecond;
		uint32_t seconds = secondsTimeTen/10;
		uint32_t percentTimesTen = (partTime*1000)/totalTime;
		uint32_t percent = percentTimesTen/10;
		fprintf(stderr, "%s: %u.%u seconds (%u.%u%%)\n", msg, seconds, secondsTimeTen-seconds*10, percent, percentTimesTen-percent*10);
	}
}

char* Linker::commatize(uint64_t in, char* out)
{
	char* result = out;
	char rawNum[30];
	sprintf(rawNum, "%llu", in);
	const int rawNumLen = strlen(rawNum);
	for(int i=0; i < rawNumLen-1; ++i) {
		*out++ = rawNum[i];
		if ( ((rawNumLen-i) % 3) == 1 )
			*out++ = ',';
	}
	*out++ = rawNum[rawNumLen-1];
	*out = '\0';
	return result;
}

void Linker::getVMInfo(vm_statistics_data_t& info)
{
	mach_msg_type_number_t count = sizeof(vm_statistics_data_t) / sizeof(natural_t);
	kern_return_t error = host_statistics(mach_host_self(), HOST_VM_INFO,
							(host_info_t)&info, &count);
	if (error != KERN_SUCCESS) {
		bzero(&info, sizeof(vm_statistics_data_t));
	}
}

void Linker::printStatistics()
{
	fEndTime = mach_absolute_time();
	if ( fOptions.printStatistics() ) {
		vm_statistics_data_t endVMInfo;
		getVMInfo(endVMInfo);

		uint64_t totalTime = fEndTime - fStartTime;
		printTime("ld64 total time", totalTime, totalTime);
		printTime(" option parsing time",	fStartCreateReadersTime -	fStartTime,					totalTime);
		printTime(" object file processing",fStartCreateWriterTime -	fStartCreateReadersTime,	totalTime);
		printTime(" output file setup",		fStartBuildAtomsTime -		fStartCreateWriterTime,		totalTime);
		printTime(" build atom list",		fStartLoadUndefinesTime -	fStartBuildAtomsTime,		totalTime);
		printTime(" load undefines",		fStartResolveTime -			fStartLoadUndefinesTime,	totalTime);
		printTime(" resolve references",	fStartSortTime -			fStartResolveTime,			totalTime);
		printTime(" sort output",			fStartDebugTime -			fStartSortTime,				totalTime);
		printTime(" process debug info",	fStartWriteTime -			fStartDebugTime,			totalTime);
		printTime(" write output",			fEndTime -					fStartWriteTime,			totalTime);
		fprintf(stderr, "pageins=%u, pageouts=%u, faults=%u\n", endVMInfo.pageins-fStartVMInfo.pageins,
										endVMInfo.pageouts-fStartVMInfo.pageouts, endVMInfo.faults-fStartVMInfo.faults);
		char temp[40];
		fprintf(stderr, "processed %3u object files,  totaling %15s bytes\n", fTotalObjectLoaded, commatize(fTotalObjectSize, temp));
		fprintf(stderr, "processed %3u archive files, totaling %15s bytes\n", fTotalArchivesLoaded, commatize(fTotalArchiveSize, temp));
		fprintf(stderr, "processed %3u dylib files\n", fTotalDylibsLoaded);
		fprintf(stderr, "wrote output file            totaling %15s bytes\n", commatize(fOutputFileSize, temp));
	}
}

inline void Linker::addAtom(ObjectFile::Atom& atom)
{
	// add to list of all atoms
	fAllAtoms.push_back(&atom);

	if ( fOptions.deadStrip() == Options::kDeadStripOff ) {
		// not dead-stripping code, so add atom's references's names to symbol table as to-be-resolved-later
		std::vector<class ObjectFile::Reference*>& references = atom.getReferences();
		for (std::vector<ObjectFile::Reference*>::iterator it=references.begin(); it != references.end(); it++) {
			ObjectFile::Reference* reference = *it;
			if ( reference->isTargetUnbound() ) {
				fGlobalSymbolTable.require(reference->getTargetName());
			}
			if ( reference->hasFromTarget() && reference->isFromTargetUnbound() )
				fGlobalSymbolTable.require(reference->getFromTargetName());
		}
	}
	else {
		if ( atom.dontDeadStrip() )
			fLiveRootAtoms.insert(&atom);
	}

	// if in global namespace, add atom itself to symbol table
	ObjectFile::Atom::Scope scope = atom.getScope();
	const char* name = atom.getName();
	if ( (scope != ObjectFile::Atom::scopeTranslationUnit) && (name != NULL) ) {
		fGlobalSymbolTable.add(atom);

		// update scope based on export list (possible that globals are downgraded to private_extern)
		if ( (scope == ObjectFile::Atom::scopeGlobal) && fOptions.hasExportRestrictList() ) {
			bool doExport = fOptions.shouldExport(name);
			if ( !doExport ) {
				atom.setScope(ObjectFile::Atom::scopeLinkageUnit);
			}
		}
	}

	// record section orders so output file can have same order
	atom.setSection(Section::find(atom.getSectionName(), atom.getSegment().getName(), atom.isZeroFill()));

	// assign order in which this atom was originally seen
	if ( atom.getSortOrder() == 0 )
		fNextSortOrder = atom.setSortOrder(fNextSortOrder);
}

inline void Linker::addAtoms(std::vector<class ObjectFile::Atom*>& atoms)
{
	bool first = true; // assume all atoms are from same reader
	for (std::vector<ObjectFile::Atom*>::iterator it=atoms.begin(); it != atoms.end(); it++) {
		if ( first ) {
			// update fReadersThatHaveSuppliedAtoms
			ObjectFile::Reader* reader = (*it)->getFile();
			if ( std::find(fReadersThatHaveSuppliedAtoms.begin(), fReadersThatHaveSuppliedAtoms.end(), reader)
					== fReadersThatHaveSuppliedAtoms.end() ) {
				fReadersThatHaveSuppliedAtoms.push_back(reader);
			}
		}
		this->addAtom(**it);
		first = false;
	}
}

void Linker::buildAtomList()
{
	fStartBuildAtomsTime = mach_absolute_time();
	// add initial undefines from -u option
	std::vector<const char*>& initialUndefines = fOptions.initialUndefines();
	for (std::vector<const char*>::iterator it=initialUndefines.begin(); it != initialUndefines.end(); it++) {
		fGlobalSymbolTable.require(*it);
	}

	// writer can contribute atoms
	this->addAtoms(fOutputFile->getAtoms());

	// each reader contributes atoms
	const int readerCount = fInputFiles.size();
	for (int i=0; i < readerCount; ++i) {
		this->addAtoms(fInputFiles[i]->getAtoms());
	}

	// extra command line section always at end
	std::vector<Options::ExtraSection>& extraSections = fOptions.extraSections();
	for( std::vector<Options::ExtraSection>::iterator it=extraSections.begin(); it != extraSections.end(); ++it) {
		this->addAtoms(SectCreate::MakeReader(it->segmentName, it->sectionName, it->path, it->data, it->dataLen)->getAtoms());
	}
}

static const char* pathLeafName(const char* path)
{
	const char* shortPath = strrchr(path, '/');
	if ( shortPath == NULL )
		return path;
	else
		return &shortPath[1];
}

void Linker::loadUndefines()
{
	fStartLoadUndefinesTime = mach_absolute_time();
	// keep looping until no more undefines were added in last loop
	unsigned int undefineCount = 0xFFFFFFFF;
	while ( undefineCount != fGlobalSymbolTable.getRequireCount() ) {
		undefineCount = fGlobalSymbolTable.getRequireCount();
		std::vector<const char*> undefineNames;
		fGlobalSymbolTable.getNeededNames(false, undefineNames);
		for(std::vector<const char*>::iterator it = undefineNames.begin(); it != undefineNames.end(); ++it) {
			const char* name = *it;
			ObjectFile::Atom* possibleAtom = fGlobalSymbolTable.find(name);
			if ( (possibleAtom == NULL)
			  || ((possibleAtom->getDefinitionKind()==ObjectFile::Atom::kWeakDefinition) && (fOptions.outputKind() != Options::kObjectFile) && (possibleAtom->getScope() == ObjectFile::Atom::scopeGlobal)) )
				this->addJustInTimeAtoms(name);
		}
	}
}

void Linker::checkUndefines()
{
	if ( fOptions.outputKind() != Options::kObjectFile ) {
		// error out on any remaining undefines
		bool doPrint = true;
		bool doError = true;
		switch ( fOptions.undefinedTreatment() ) {
			case Options::kUndefinedError:
				break;
			case Options::kUndefinedDynamicLookup:
				doError = false;
				break;
			case Options::kUndefinedWarning:
				doError = false;
				break;
			case Options::kUndefinedSuppress:
				doError = false;
				doPrint = false;
				break;
		}
		std::vector<const char*> unresolvableUndefines;
		fGlobalSymbolTable.getNeededNames(false, unresolvableUndefines);
		const int unresolvableCount = unresolvableUndefines.size();
		int unresolvableExportsCount  = 0;
		if ( unresolvableCount != 0 ) {
			if ( doPrint ) {
				if ( fOptions.printArchPrefix() )
					fprintf(stderr, "Undefined symbols for architecture %s:\n", fArchitectureName);
				else
					fprintf(stderr, "Undefined symbols:\n");
				for (int i=0; i < unresolvableCount; ++i) {
					const char* name = unresolvableUndefines[i];
					fprintf(stderr, "  %s, referenced from:\n", name);
					// scan all atoms for references
					bool foundAtomReference = false;
					for (std::vector<ObjectFile::Atom*>::iterator it=fAllAtoms.begin(); it != fAllAtoms.end(); it++) {
						ObjectFile::Atom* atom = *it;
						std::vector<class ObjectFile::Reference*>& references = atom->getReferences();
						for (std::vector<ObjectFile::Reference*>::iterator rit=references.begin(); rit != references.end(); rit++) {
							ObjectFile::Reference* reference = *rit;
							if ( reference->isTargetUnbound() ) {
								if ( strcmp(reference->getTargetName(), name) == 0 ) {
									fprintf(stderr, "      %s in %s\n", atom->getDisplayName(), pathLeafName(atom->getFile()->getPath()));
									foundAtomReference = true;
								}
							}
							if ( reference->hasFromTarget() && reference->isFromTargetUnbound() ) {
								if ( strcmp(reference->getFromTargetName(), name) == 0 ) {
									fprintf(stderr, "      %s in %s\n", atom->getDisplayName(), pathLeafName(atom->getFile()->getPath()));
									foundAtomReference = true;
								}
							}
						}
					}
					// scan command line options
					if  ( !foundAtomReference && fOptions.hasExportRestrictList() && fOptions.shouldExport(name) ) {
						fprintf(stderr, "     -exported_symbols_list command line option\n");
						++unresolvableExportsCount;
					}
				}
			}
			if ( doError && (unresolvableCount > unresolvableExportsCount) ) // last check should be removed.  It exists so broken projects still build
				throw "symbol(s) not found";
		}
	}
}



void Linker::addJustInTimeAtoms(const char* name)
{
	// when creating final linked image, writer gets first chance
	if ( fOptions.outputKind() != Options::kObjectFile ) {
		std::vector<class ObjectFile::Atom*>* atoms = fOutputFile->getJustInTimeAtomsFor(name);
		if ( atoms != NULL ) {
			this->addAtoms(*atoms);
			delete atoms;
			//fprintf(stderr, "addJustInTimeAtoms(%s) => found in file %s\n", name, fOutputFile->getPath() );
			return;  // found a definition, no need to search anymore
		}
	}

	// give direct readers a chance
	for (std::vector<class ObjectFile::Reader*>::iterator it=fInputFiles.begin(); it != fInputFiles.end(); it++) {
		ObjectFile::Reader* reader = *it;
		if ( reader != NULL ) {
			// if this reader is a static archive that has the symbol we need, pull in all atoms in that module
			// if this reader is a dylib that exports the symbol we need, have it synthesize an atom for us.
			std::vector<class ObjectFile::Atom*>* atoms = reader->getJustInTimeAtomsFor(name);
			if ( atoms != NULL ) {
				this->addAtoms(*atoms);
				delete atoms;
				//fprintf(stderr, "addJustInTimeAtoms(%s) => found in file %s\n", name, fInputFiles[i]->getPath() );
				return;  // found a definition, no need to search anymore
			}
		}
	}

	// give indirect readers a chance
	for (std::list<IndirectLibrary>::iterator it=fIndirectDynamicLibraries.begin(); it != fIndirectDynamicLibraries.end(); it++) {
		ObjectFile::Reader* reader = it->reader;
		// for two-level namespace, only search re-exported indirect libraries
		if ( (reader != NULL) && ((it->reExportedViaDirectLibrary != NULL) || (fOptions.nameSpace() != Options::kTwoLevelNameSpace)) ) {
			std::vector<class ObjectFile::Atom*>* atoms = reader->getJustInTimeAtomsFor(name);
			if ( atoms != NULL ) {
				this->addAtoms(*atoms);
				//fprintf(stderr, "addJustInTimeAtoms(%s) => found in file %s\n", name, reader->getPath() );
				delete atoms;
				return;  // found a definition, no need to search anymore
			}
		}
	}

	// when creating .o file, writer goes last (this is so any static archives will be searched above)
	if ( (fOptions.outputKind() == Options::kObjectFile) || (fOptions.undefinedTreatment() != Options::kUndefinedError) ) {
		ObjectFile::Atom* atom = fOutputFile->getUndefinedProxyAtom(name);
		if ( atom != NULL ) {
			this->addAtom(*atom);
			return;
		}
	}
	//fprintf(stderr, "addJustInTimeAtoms(%s) => not found\n", name);
}

void Linker::resolve(ObjectFile::Reference* reference)
{
	// look in global symbol table
	const char* targetName = reference->getTargetName();
	ObjectFile::Atom* target = fGlobalSymbolTable.find(targetName);
	if ( target == NULL ) {
		fprintf(stderr, "Undefined symbol: %s\n", targetName);
	}
	reference->setTarget(*target, reference->getTargetOffset());
}

void Linker::resolveFrom(ObjectFile::Reference* reference)
{
	// handle references that have two (from and to) targets
	const char* fromTargetName = reference->getFromTargetName();
	ObjectFile::Atom* fromTarget = fGlobalSymbolTable.find(fromTargetName);
	if ( fromTarget == NULL ) {
		fprintf(stderr, "Undefined symbol: %s\n", fromTargetName);
	}
	reference->setFromTarget(*fromTarget);
}


void Linker::resolveReferences()
{
	fStartResolveTime = mach_absolute_time();
	// note: the atom list may grow during this loop as libraries supply needed atoms
	for (unsigned int j=0; j < fAllAtoms.size(); ++j) {
		ObjectFile::Atom* atom = fAllAtoms[j];
		std::vector<class ObjectFile::Reference*>& references = atom->getReferences();
		for (std::vector<ObjectFile::Reference*>::iterator it=references.begin(); it != references.end(); it++) {
			ObjectFile::Reference* reference = *it;
			if ( reference->isTargetUnbound() )
				this->resolve(reference);
			if ( reference->hasFromTarget() && reference->isFromTargetUnbound() )
				this->resolveFrom(reference);
		}
	}
}


// used to remove stabs associated with atoms that won't be in output file
class NotInSet
{
public:
	NotInSet(std::set<ObjectFile::Atom*>& theSet) : fSet(theSet) {}

	bool operator()(const ObjectFile::Reader::Stab& stab) const {
		if ( stab.atom == NULL )
			return false;	// leave stabs that are not associated with any atome
		else
			return ( fSet.count(stab.atom) == 0 );
	}

private:
	std::set<ObjectFile::Atom*>& fSet;
};


class NotLive
{
public:
	NotLive(std::set<ObjectFile::Atom*>& set) : fLiveAtoms(set)  {}

	bool operator()(ObjectFile::Atom*& atom) const {
		//if ( fLiveAtoms.count(atom) == 0 )
		//	fprintf(stderr, "dead strip %s\n", atom->getDisplayName());
		return ( fLiveAtoms.count(atom) == 0 );
	}
private:
	std::set<ObjectFile::Atom*>& fLiveAtoms;
};



void Linker::markLive(ObjectFile::Atom& atom, struct Linker::WhyLiveBackChain* previous)
{
	if ( fLiveAtoms.count(&atom) == 0 ) {
		// if -whylive cares about this symbol, then dump chain
		if ( (previous->name != NULL) && fOptions.printWhyLive(previous->name) ) {
			int depth = 0;
			for(WhyLiveBackChain* p = previous; p != NULL; p = p->previous, ++depth) {
				for(int i=depth; i > 0; --i)
					fprintf(stderr, "  ");
				fprintf(stderr, "%s\n", p->name);
			}
		}
		// set up next chain
		WhyLiveBackChain thisChain;
		thisChain.previous = previous;
		// this atom is live
		fLiveAtoms.insert(&atom);
		// and all atoms it references
		std::vector<class ObjectFile::Reference*>& references = atom.getReferences();
		for (std::vector<ObjectFile::Reference*>::iterator it=references.begin(); it != references.end(); it++) {
			ObjectFile::Reference* reference = *it;
			if ( reference->isTargetUnbound() ) {
				// look in global symbol table
				const char* targetName = reference->getTargetName();
				ObjectFile::Atom* target = fGlobalSymbolTable.find(targetName);
				if ( target == NULL ) {
					// load archives or dylibs
					this->addJustInTimeAtoms(targetName);
				}
				// look again
				target = fGlobalSymbolTable.find(targetName);
				if ( target != NULL ) {
					reference->setTarget(*target, reference->getTargetOffset());
				}
				else {
					// mark as undefined, for later error processing
					fAtomsWithUnresolvedReferences.push_back(&atom);
					fGlobalSymbolTable.require(targetName);
				}
			}
			if ( ! reference->isTargetUnbound() ) {
				thisChain.name = reference->getTargetName();
				markLive(reference->getTarget(), &thisChain);
			}
			if ( reference->hasFromTarget() ) {
				// do the same as above, for from target
				if ( reference->isFromTargetUnbound() ) {
					// look in global symbol table
					const char* targetName = reference->getFromTargetName();
					ObjectFile::Atom* target = fGlobalSymbolTable.find(targetName);
					if ( target == NULL ) {
						// load archives or dylibs
						this->addJustInTimeAtoms(targetName);
					}
					// look again
					target = fGlobalSymbolTable.find(targetName);
					if ( target != NULL ) {
						reference->setFromTarget(*target);
					}
					else {
						// mark as undefined, for later error processing
						fGlobalSymbolTable.require(targetName);
					}
				}
				if ( ! reference->isFromTargetUnbound() ) {
					thisChain.name = reference->getFromTargetName();
					markLive(reference->getFromTarget(), &thisChain);
				}
			}
		}
	}
}


void Linker::addLiveRoot(const char* name)
{
	ObjectFile::Atom* target = fGlobalSymbolTable.find(name);
	if ( target == NULL ) {
		this->addJustInTimeAtoms(name);
		target = fGlobalSymbolTable.find(name);
	}
	if ( target != NULL )
		fLiveRootAtoms.insert(target);
}


void Linker::deadStripResolve()
{
	// add main() to live roots
	ObjectFile::Atom* entryPoint = this->entryPoint();
	if ( entryPoint != NULL )
		fLiveRootAtoms.insert(entryPoint);

	// add dyld_stub_binding_helper() to live roots
	ObjectFile::Atom* dyldHelper = this->dyldHelper();
	if ( dyldHelper != NULL )
		fLiveRootAtoms.insert(dyldHelper);

	// add -exported_symbols_list, -init, and -u entries to live roots
	std::vector<const char*>& initialUndefines = fOptions.initialUndefines();
	for (std::vector<const char*>::iterator it=initialUndefines.begin(); it != initialUndefines.end(); it++)
		addLiveRoot(*it);

	// in some cases, every global scope atom in initial .o files is a root
	if ( fOptions.allGlobalsAreDeadStripRoots() ) {
		for (std::vector<ObjectFile::Atom*>::iterator it=fAllAtoms.begin(); it != fAllAtoms.end(); it++) {
			ObjectFile::Atom* atom = *it;
			if ( atom->getScope() == ObjectFile::Atom::scopeGlobal )
				fLiveRootAtoms.insert(atom);
		}
	}

	// mark all roots as live, and all atoms they reference
	for (std::set<ObjectFile::Atom*>::iterator it=fLiveRootAtoms.begin(); it != fLiveRootAtoms.end(); it++) {
		WhyLiveBackChain rootChain;
		rootChain.previous = NULL;
		rootChain.name = (*it)->getDisplayName();
		markLive(**it, &rootChain);
	}

	// it is possible that there are unresolved references that can be resolved now
	// this can happen if the first reference to a common symbol in an archive.
	// common symbols are not in the archive TOC, but the .o could have been pulled in later.
	// <rdar://problem/4654131> ld64 while linking cc1 [ when dead_strip is ON]
	for (std::vector<ObjectFile::Atom*>::iterator it=fAtomsWithUnresolvedReferences.begin(); it != fAtomsWithUnresolvedReferences.end(); it++) {
		std::vector<class ObjectFile::Reference*>& references = (*it)->getReferences();
		for (std::vector<ObjectFile::Reference*>::iterator rit=references.begin(); rit != references.end(); rit++) {
			ObjectFile::Reference* reference = *rit;
			if ( reference->isTargetUnbound() ) {
				ObjectFile::Atom* target = fGlobalSymbolTable.find(reference->getTargetName());
				if ( target != NULL ) {
					reference->setTarget(*target, reference->getTargetOffset());
					fLiveAtoms.insert(target);
					// by just adding this atom to fLiveAtoms set, we are assuming it has no
					// references, which is true for commons.
					if ( target->getDefinitionKind() != ObjectFile::Atom::kTentativeDefinition )
						fprintf(stderr, "warning: ld64 internal error %s is not a tentative definition\n", target->getDisplayName());
				}
			}
			if ( reference->hasFromTarget() && reference->isFromTargetUnbound() ) {
				ObjectFile::Atom* target = fGlobalSymbolTable.find(reference->getFromTargetName());
				if ( target != NULL ) {
					reference->setFromTarget(*target);
					fLiveAtoms.insert(target);
					// by just adding this atom to fLiveAtoms set, we are assuming it has no
					// references, which is true for commons.
					if ( target->getDefinitionKind() != ObjectFile::Atom::kTentativeDefinition )
						fprintf(stderr, "warning: ld64 internal error %s is not a tentative definition\n", target->getDisplayName());
				}
			}
		}
	}
	
	// now remove all non-live atoms from fAllAtoms
	fAllAtoms.erase(std::remove_if(fAllAtoms.begin(), fAllAtoms.end(), NotLive(fLiveAtoms)), fAllAtoms.end());
}

void Linker::sortAtoms()
{
	fStartSortTime = mach_absolute_time();
	Section::assignIndexes();
	std::sort(fAllAtoms.begin(), fAllAtoms.end(), Linker::AtomSorter());
	//fprintf(stderr, "Sorted atoms:\n");
	//for (std::vector<ObjectFile::Atom*>::iterator it=fAllAtoms.begin(); it != fAllAtoms.end(); it++) {
	//	fprintf(stderr, "\t%s\n", (*it)->getDisplayName());
	//}
}


// make sure given addresses are within reach of branches, etc
void Linker::tweakLayout()
{
}


void Linker::writeDotOutput()
{
	const char* dotOutFilePath = fOptions.dotOutputFile();
	if ( dotOutFilePath != NULL ) {
		FILE* out = fopen(dotOutFilePath, "w");
		if ( out != NULL ) {
			// print header
			fprintf(out, "digraph dg\n{\n");
			fprintf(out, "\tconcentrate = true;\n");
			fprintf(out, "\trankdir = LR;\n");

			// print each atom as a node
			for (std::vector<ObjectFile::Atom*>::iterator it=fAllAtoms.begin(); it != fAllAtoms.end(); it++) {
				ObjectFile::Atom* atom = *it;
				if ( atom->getFile() != fOutputFile ) {
					const char* name = atom->getDisplayName();
					if ( (atom->getDefinitionKind() == ObjectFile::Atom::kExternalDefinition)
					  || (atom->getDefinitionKind() == ObjectFile::Atom::kExternalWeakDefinition) ) {
						fprintf(out, "\taddr%p [ shape = plaintext, label = \"%s\" ];\n", atom, name);
					}
					else if ( strcmp(atom->getSectionName(), "__cstring") == 0 ) {
						char cstring[atom->getSize()+2];
						atom->copyRawContent((uint8_t*)cstring);
						fprintf(out, "\taddr%p [ label = \"string: '", atom);
						for (const char* s=cstring; *s != '\0'; ++s) {
							if ( *s == '\n' )
								fprintf(out, "\\\\n");
							else
								fputc(*s, out);
						}
						fprintf(out, "'\" ];\n");
					}
					else {
						fprintf(out, "\taddr%p [ label = \"%s\" ];\n", atom, name);
					}
				}
			}
			fprintf(out, "\n");

			// print each reference as an edge
			for (std::vector<ObjectFile::Atom*>::iterator it=fAllAtoms.begin(); it != fAllAtoms.end(); it++) {
				ObjectFile::Atom* fromAtom = *it;
				if ( fromAtom->getFile() != fOutputFile ) {
					std::vector<ObjectFile::Reference*>&  references = fromAtom->getReferences();
					std::set<ObjectFile::Atom*> seenTargets;
					for (std::vector<ObjectFile::Reference*>::iterator rit=references.begin(); rit != references.end(); rit++) {
						ObjectFile::Reference* reference = *rit;
						ObjectFile::Atom* toAtom = &(reference->getTarget());
						if ( seenTargets.count(toAtom) == 0 ) {
							seenTargets.insert(toAtom);
							fprintf(out, "\taddr%p -> addr%p;\n", fromAtom, toAtom);
						}
					}
				}
			}
			fprintf(out, "\n");

			// push all imports to bottom of graph
			fprintf(out, "{ rank = same; ");
			for (std::vector<ObjectFile::Atom*>::iterator it=fAllAtoms.begin(); it != fAllAtoms.end(); it++) {
				ObjectFile::Atom* atom = *it;
				if ( atom->getFile() != fOutputFile )
					if ( (atom->getDefinitionKind() == ObjectFile::Atom::kExternalDefinition)
					  || (atom->getDefinitionKind() == ObjectFile::Atom::kExternalWeakDefinition) ) {
						fprintf(out, "addr%p; ", atom);
					}
			}
			fprintf(out, "};\n ");

			// print footer
			fprintf(out, "}\n");
			fclose(out);
		}
		else {
			fprintf(stderr, "ld64 warning: could not write dot output file: %s\n", dotOutFilePath);
		}
	}
}

ObjectFile::Atom* Linker::entryPoint()
{
	// if main executable, find entry point atom
	ObjectFile::Atom* entryPoint = NULL;
	switch ( fOptions.outputKind() ) {
		case Options::kDynamicExecutable:
		case Options::kStaticExecutable:
		case Options::kDyld:
			entryPoint = fGlobalSymbolTable.find(fOptions.entryName());
			if ( entryPoint == NULL ) {
				throwf("could not find entry point \"%s\" (perhaps missing crt1.o)", fOptions.entryName());
			}
			break;
		case Options::kDynamicLibrary:
			if ( fOptions.initFunctionName() != NULL ) {
				entryPoint = fGlobalSymbolTable.find(fOptions.initFunctionName());
				if ( entryPoint == NULL ) {
					throwf("could not find -init function: \"%s\"", fOptions.initFunctionName());
				}
			}
			break;
		case Options::kObjectFile:
		case Options::kDynamicBundle:
			entryPoint = NULL;
			break;
	}
	return entryPoint;
}

ObjectFile::Atom* Linker::dyldHelper()
{
	return fGlobalSymbolTable.find("dyld_stub_binding_helper");
}

const char* Linker::assureFullPath(const char* path)
{
	if ( path[0] == '/' )
		return path;
	char cwdbuff[MAXPATHLEN];
	if ( getcwd(cwdbuff, MAXPATHLEN) != NULL ) {
		char* result;
		asprintf(&result, "%s/%s", cwdbuff, path);
		if ( result != NULL )
			return result;
	}
	return path;
}


//
// The stab strings are of the form:
//		<name> ':' <type-code> <number-pari>
//  but the <name> contain a colon.
//  For C++ <name> may contain a double colon (e.g. std::string:f(0,1) )
//  For Objective-C name may contain a colon instead square bracket (e.g. [Foo doit:]:f(0,1) )
//
const char* Linker::truncateStabString(const char* str)
{
	enum { start, inObjc } state = start;
	for (const char* s = str; *s != 0; ++s) {
		char c = *s;
		switch (state) {
			case start:
				if ( c == '[' ) {
					state = inObjc;
				}
				else {
					if ( c == ':' ) {
						if ( s[1] == ':' ) {
							++s;
						}
						else {
							// found colon
							// Duplicate strndup behavior here.
							int trunStrLen = s-str+2;
							char* temp = new char[trunStrLen+1];
							memcpy(temp, str, trunStrLen);
							temp[trunStrLen] = '\0';
							return temp;
						}
					}
				}
				break;
			case inObjc:
				if ( c == ']' ) {
					state = start;
				}
				break;
		}
	}
	// malformed
	return str;
}


bool Linker::minimizeStab(ObjectFile::Reader::Stab& stab)
{
	switch(stab.type){
		case N_GSYM:
		case N_STSYM:
		case N_LCSYM:
		case N_FUN:
			// these all need truncated strings
			stab.string = truncateStabString(stab.string);
			return true;
		case N_SO:
		case N_OSO:
		case N_OPT:
		case N_SOL:
			// these are included in the minimal stabs, but they keep their full string
			return true;
		default:
			return false;
	}
}


struct HeaderRange {
	std::vector<ObjectFile::Reader::Stab>::iterator	begin;
	std::vector<ObjectFile::Reader::Stab>::iterator	end;
	int												parentRangeIndex;
	uint32_t										sum;
	bool											sumPrecomputed;
	bool											useEXCL;
	bool											cannotEXCL; // because of SLINE, etc stabs
};


typedef __gnu_cxx::hash_map<const char*, std::vector<uint32_t>, __gnu_cxx::hash<const char*>, CStringEquals> PathToSums;

// hash table that maps header path to a vector of known checksums for that path
static PathToSums sKnownBINCLs;


void Linker::collectStabs(ObjectFile::Reader* reader, std::map<const class ObjectFile::Atom*, uint32_t>& atomOrdinals)
{
	bool log = false;
	bool minimal = ( fOptions.readerOptions().fDebugInfoStripping == ObjectFile::ReaderOptions::kDebugInfoMinimal );
	std::vector<class ObjectFile::Reader::Stab>* readerStabs = reader->getStabs();
	if ( readerStabs == NULL )
		return;

	if ( log ) fprintf(stderr, "processesing %lu stabs for %s\n", readerStabs->size(), reader->getPath());
	std::vector<HeaderRange> ranges;
	int curRangeIndex = -1;
	int count = 0;
	ObjectFile::Atom* atomWithLowestOrdinal = NULL;
	ObjectFile::Atom* atomWithHighestOrdinal = NULL;
	uint32_t highestOrdinal = 0;
	uint32_t lowestOrdinal = UINT_MAX;
	std::vector<std::pair<ObjectFile::Atom*,ObjectFile::Atom*> > soRanges;
	// 1) find all (possibly nested) BINCL/EINCL ranges and their checksums
	// 2) find all SO/SO ranges and the first/last atom own by a FUN stab therein
	for(std::vector<class ObjectFile::Reader::Stab>::iterator it=readerStabs->begin(); it != readerStabs->end(); ++it) {
		++count;
		switch ( it->type ) {
			case N_BINCL:
				{
					HeaderRange range;
					range.begin = it;
					range.end = readerStabs->end();
					range.parentRangeIndex = curRangeIndex;
					range.sum = it->value;
					range.sumPrecomputed = (range.sum != 0);
					range.useEXCL = false;
					range.cannotEXCL = false;
					curRangeIndex = ranges.size();
					if ( log ) fprintf(stderr, "[%d]BINCL %s\n", curRangeIndex, it->string);
					ranges.push_back(range);
				}
				break;
			case N_EINCL:
				if ( curRangeIndex == -1 ) {
					fprintf(stderr, "ld64 warning: EINCL missing BINCL in %s\n", reader->getPath());
				}
				else {
					ranges[curRangeIndex].end = it+1;
					if ( log ) fprintf(stderr, "[%d->%d]EINCL %s\n", curRangeIndex, ranges[curRangeIndex].parentRangeIndex, it->string);
					curRangeIndex = ranges[curRangeIndex].parentRangeIndex;
				}
				break;
			case N_FUN:
				{
					std::map<const class ObjectFile::Atom*, uint32_t>::iterator pos = atomOrdinals.find(it->atom);
					if ( pos != atomOrdinals.end() ) {
						uint32_t ordinal = pos->second;
						if ( ordinal > highestOrdinal ) {
							highestOrdinal = ordinal;
							atomWithHighestOrdinal = it->atom;
						}
						if ( ordinal < lowestOrdinal ) {
							lowestOrdinal = ordinal;
							atomWithLowestOrdinal = it->atom;
						}
					}
				}
				// fall through
			case N_BNSYM:
			case N_ENSYM:
			case N_LBRAC:
			case N_RBRAC:
			case N_SLINE:
			case N_STSYM:
			case N_LCSYM:
				if ( curRangeIndex != -1 ) {
					ranges[curRangeIndex].cannotEXCL = true;
					if ( fOptions.warnStabs() )
						fprintf(stderr, "ld64: cannot do BINCL/EINCL optimzation because of stabs kinds in %s for %s\n", ranges[curRangeIndex].begin->string, reader->getPath());
				}
				break;
			case N_SO:
				if ( (it->string != NULL) && (strlen(it->string) > 0) ) {
					// start SO, reset hi/low FUN tracking
					atomWithLowestOrdinal = NULL;
					atomWithHighestOrdinal = NULL;
					highestOrdinal = 0;
					lowestOrdinal = UINT_MAX;
				}
				else {
					// end SO, record hi/low atoms for this SO range
					soRanges.push_back(std::make_pair<ObjectFile::Atom*,ObjectFile::Atom*>(atomWithLowestOrdinal, atomWithHighestOrdinal));
				}
				// fall through
			default:
				if ( curRangeIndex != -1 ) {
					if ( ! ranges[curRangeIndex].sumPrecomputed ) {
						uint32_t sum = 0;
						const char* s = it->string;
						char c;
						while ( (c = *s++) != 0 ) {
							sum += c;
							// don't checkusm first number (file index) after open paren in string
							if ( c == '(' ) {
								while(isdigit(*s))
									++s;
							}
						}
						ranges[curRangeIndex].sum += sum;
					}
				}

		}
	}
	if ( log ) fprintf(stderr, "processesed %d stabs for %s\n", count, reader->getPath());
	if ( curRangeIndex != -1 )
		fprintf(stderr, "ld64 warning: BINCL (%s) missing EINCL in %s\n", ranges[curRangeIndex].begin->string, reader->getPath());

	// if no BINCLs
	if ( ranges.size() == 0 ) {
		int soIndex = 0;
		for(std::vector<ObjectFile::Reader::Stab>::iterator it=readerStabs->begin(); it != readerStabs->end(); ++it) {
			// copy minimal or all stabs
			ObjectFile::Reader::Stab stab = *it;
			if ( !minimal || minimizeStab(stab) ) {
				if ( stab.type == N_SO ) {
					if ( (stab.string != NULL) && (strlen(stab.string) > 0) ) {
						// starting SO is associated with first atom
						stab.atom = soRanges[soIndex].first;
					}
					else {
						// ending SO is associated with last atom
						stab.atom = soRanges[soIndex].second;
						++soIndex;
					}
				}
				fStabs.push_back(stab);
			}
		}
		return;
	}

	//fprintf(stderr, "BINCL/EINCL info for %s\n", reader->getPath());
	//for(std::vector<HeaderRange>::iterator it=ranges.begin(); it != ranges.end(); ++it) {
	//	fprintf(stderr, "%08X %s\n", it->sum, it->begin->string);
	//}

	// see if any of these BINCL/EINCL ranges have already been seen and therefore can be replaced with EXCL
	for(std::vector<HeaderRange>::iterator it=ranges.begin(); it != ranges.end(); ++it) {
		if ( ! it->cannotEXCL ) {
			const char* header = it->begin->string;
			uint32_t sum = it->sum;
			PathToSums::iterator pos = sKnownBINCLs.find(header);
			if ( pos != sKnownBINCLs.end() ) {
				std::vector<uint32_t>& sums = pos->second;
				for(std::vector<uint32_t>::iterator sit=sums.begin(); sit != sums.end(); ++sit) {
					if (*sit == sum) {
						//fprintf(stderr, "use EXCL for %s in %s\n", header, reader->getPath());
						it->useEXCL = true;
						break;
					}
				}
				if ( ! it->useEXCL ) {
					// have seen this path, but not this checksum
					//fprintf(stderr, "registering another checksum %08X for %s\n", sum, header);
					sums.push_back(sum);
				}
			}
			else {
				// have not seen this path, so add to known BINCLs
				std::vector<uint32_t> empty;
				sKnownBINCLs[header] = empty;
				sKnownBINCLs[header].push_back(sum);
				//fprintf(stderr, "registering checksum %08X for %s\n", sum, header);
			}
		}
	}

	// add a new set of stabs with BINCL/EINCL runs that have been seen before, replaced with EXCLs
	curRangeIndex = -1;
	const int maxRangeIndex = ranges.size();
	int soIndex = 0;
	for(std::vector<ObjectFile::Reader::Stab>::iterator it=readerStabs->begin(); it != readerStabs->end(); ++it) {
		switch ( it->type ) {
			case N_BINCL:
				for(int i=curRangeIndex+1; i < maxRangeIndex; ++i) {
					if ( ranges[i].begin == it ) {
						curRangeIndex = i;
						HeaderRange& range = ranges[curRangeIndex];
						ObjectFile::Reader::Stab stab = *it;
						stab.value = range.sum; // BINCL and EXCL have n_value set to checksum
						if ( range.useEXCL )
							stab.type = N_EXCL;	// transform BINCL into EXCL
						if ( !minimal )
							fStabs.push_back(stab);
						break;
					}
				}
				break;
			case N_EINCL:
				if ( curRangeIndex != -1 ) {
					if ( !ranges[curRangeIndex].useEXCL && !minimal )
						fStabs.push_back(*it);
					curRangeIndex = ranges[curRangeIndex].parentRangeIndex;
				}
				break;
			default:
				if ( (curRangeIndex == -1) || !ranges[curRangeIndex].useEXCL ) {
					ObjectFile::Reader::Stab stab = *it;
					if ( !minimal || minimizeStab(stab) ) {
						if ( stab.type == N_SO ) {
							if ( (stab.string != NULL) || (strlen(stab.string) > 0) ) {
								// starting SO is associated with first atom
								stab.atom = soRanges[soIndex].first;
							}
							else {
								// ending SO is associated with last atom
								stab.atom = soRanges[soIndex].second;
								++soIndex;
							}
						}
						fStabs.push_back(stab);
					}
				}
		}
	}

}


// used to prune out atoms that don't need debug notes generated
class NoDebugNoteAtom
{
public:
	NoDebugNoteAtom(const std::map<class ObjectFile::Reader*, uint32_t>& readersWithDwarfOrdinals) 
			: fReadersWithDwarfOrdinals(readersWithDwarfOrdinals) {}

	bool operator()(const ObjectFile::Atom* atom) const {
		if ( atom->getSymbolTableInclusion() == ObjectFile::Atom::kSymbolTableNotIn )
			return true;
		if ( atom->getName() == NULL )
			return true;
		if ( fReadersWithDwarfOrdinals.find(atom->getFile()) == fReadersWithDwarfOrdinals.end() )
			return true;
		return false;
	}

private:
	const std::map<class ObjectFile::Reader*, uint32_t>& fReadersWithDwarfOrdinals;
};

// used to sort atoms with debug notes
class ReadersWithDwarfSorter
{
public:
	ReadersWithDwarfSorter(const std::map<class ObjectFile::Reader*, uint32_t>& readersWithDwarfOrdinals, 
						   const std::map<const class ObjectFile::Atom*, uint32_t>& atomOrdinals) 
			: fReadersWithDwarfOrdinals(readersWithDwarfOrdinals), fAtomOrdinals(atomOrdinals) {}

	bool operator()(const ObjectFile::Atom* left, const ObjectFile::Atom* right) const
	{
		// first sort by reader
		unsigned int leftReaderIndex  = fReadersWithDwarfOrdinals.find(left->getFile())->second;
		unsigned int rightReaderIndex = fReadersWithDwarfOrdinals.find(right->getFile())->second;
		if ( leftReaderIndex != rightReaderIndex )
			return (leftReaderIndex < rightReaderIndex);

		// then sort by atom ordinal
		unsigned int leftAtomIndex  = fAtomOrdinals.find(left)->second;
		unsigned int rightAtomIndex = fAtomOrdinals.find(right)->second;
		return leftAtomIndex < rightAtomIndex;
	}

private:
	const std::map<class ObjectFile::Reader*, uint32_t>& fReadersWithDwarfOrdinals;
	const std::map<const class ObjectFile::Atom*, uint32_t>& fAtomOrdinals;
};





void Linker::synthesizeDebugNotes(std::vector<class ObjectFile::Atom*>& allAtomsByReader)
{
	// synthesize "debug notes" and add them to master stabs vector
	const char* dirPath = NULL;
	const char* filename = NULL;
	bool wroteStartSO = false;
	__gnu_cxx::hash_set<const char*, __gnu_cxx::hash<const char*>, CStringEquals>  seenFiles;
	for (std::vector<ObjectFile::Atom*>::iterator it=allAtomsByReader.begin(); it != allAtomsByReader.end(); it++) {
		ObjectFile::Atom* atom = *it;
		const char* newDirPath;
		const char* newFilename;
		//fprintf(stderr, "debug note for %s\n", atom->getDisplayName());
		if ( atom->getTranslationUnitSource(&newDirPath, &newFilename) ) {
			// need SO's whenever the translation unit source file changes
			if ( newFilename != filename ) {
				// gdb like directory SO's to end in '/', but dwarf DW_AT_comp_dir usually does not have trailing '/'
				if ( (newDirPath != NULL) && (strlen(newDirPath) > 1 ) && (newDirPath[strlen(newDirPath)-1] != '/') )
					asprintf((char**)&newDirPath, "%s/", newDirPath);
				if ( filename != NULL ) {
					// translation unit change, emit ending SO
					ObjectFile::Reader::Stab endFileStab;
					endFileStab.atom		= NULL;
					endFileStab.type		= N_SO;
					endFileStab.other		= 1;
					endFileStab.desc		= 0;
					endFileStab.value		= 0;
					endFileStab.string		= "";
					fStabs.push_back(endFileStab);
				}
				// new translation unit, emit start SO's
				ObjectFile::Reader::Stab dirPathStab;
				dirPathStab.atom		= NULL;
				dirPathStab.type		= N_SO;
				dirPathStab.other		= 0;
				dirPathStab.desc		= 0;
				dirPathStab.value		= 0;
				dirPathStab.string		= newDirPath;
				fStabs.push_back(dirPathStab);
				ObjectFile::Reader::Stab fileStab;
				fileStab.atom		= NULL;
				fileStab.type		= N_SO;
				fileStab.other		= 0;
				fileStab.desc		= 0;
				fileStab.value		= 0;
				fileStab.string		= newFilename;
				fStabs.push_back(fileStab);
				// Synthesize OSO for start of file
				ObjectFile::Reader::Stab objStab;
				objStab.atom		= NULL;
				objStab.type		= N_OSO;
				objStab.other		= 0;
				objStab.desc		= 1;
				objStab.value		= atom->getFile()->getModificationTime();
				objStab.string		= assureFullPath(atom->getFile()->getPath());
				fStabs.push_back(objStab);
				wroteStartSO = true;
				// add the source file path to seenFiles so it does not show up in SOLs
				seenFiles.insert(newFilename);
			}
			filename = newFilename;
			dirPath = newDirPath;
			if ( atom->getSegment().isContentExecutable() && (strncmp(atom->getSectionName(), "__text", 6) == 0) ) {
				// Synthesize BNSYM and start FUN stabs
				ObjectFile::Reader::Stab beginSym;
				beginSym.atom		= atom;
				beginSym.type		= N_BNSYM;
				beginSym.other		= 1;
				beginSym.desc		= 0;
				beginSym.value		= 0;
				beginSym.string		= "";
				fStabs.push_back(beginSym);
				ObjectFile::Reader::Stab startFun;
				startFun.atom		= atom;
				startFun.type		= N_FUN;
				startFun.other		= 1;
				startFun.desc		= 0;
				startFun.value		= 0;
				startFun.string		= atom->getName();
				fStabs.push_back(startFun);
				// Synthesize any SOL stabs needed
				std::vector<ObjectFile::LineInfo>* lineInfo = atom->getLineInfo();
				if ( lineInfo != NULL ) {
					const char* curFile = NULL;
					for (std::vector<ObjectFile::LineInfo>::iterator it = lineInfo->begin(); it != lineInfo->end(); ++it) {
						if ( it->fileName != curFile ) {
							if ( seenFiles.count(it->fileName) == 0 ) {
								seenFiles.insert(it->fileName);
								ObjectFile::Reader::Stab sol;
								sol.atom		= 0;
								sol.type		= N_SOL;
								sol.other		= 0;
								sol.desc		= 0;
								sol.value		= 0;
								sol.string		= it->fileName;
								fStabs.push_back(sol);
							}
							curFile = it->fileName;
						}
					}
				}
				// Synthesize end FUN and ENSYM stabs
				ObjectFile::Reader::Stab endFun;
				endFun.atom			= atom;
				endFun.type			= N_FUN;
				endFun.other		= 0;
				endFun.desc			= 0;
				endFun.value		= 0;
				endFun.string		= "";
				fStabs.push_back(endFun);
				ObjectFile::Reader::Stab endSym;
				endSym.atom			= atom;
				endSym.type			= N_ENSYM;
				endSym.other		= 1;
				endSym.desc			= 0;
				endSym.value		= 0;
				endSym.string		= "";
				fStabs.push_back(endSym);
			}
			else {
				ObjectFile::Reader::Stab globalsStab;
				if ( atom->getScope() == ObjectFile::Atom::scopeTranslationUnit ) {
					// Synthesize STSYM stab for statics
					const char* name = atom->getName();
					if ( name[0] == '_' ) {
						globalsStab.atom		= atom;
						globalsStab.type		= N_STSYM;
						globalsStab.other		= 1;
						globalsStab.desc		= 0;
						globalsStab.value		= 0;
						globalsStab.string		= name;
						fStabs.push_back(globalsStab);
					}
				}
				else {
					// Synthesize GSYM stab for other globals (but not .eh exception frame symbols)
					const char* name = atom->getName();
					if ( (name[0] == '_') && (strcmp(atom->getSectionName(), "__eh_frame") != 0) ) {
						globalsStab.atom		= atom;
						globalsStab.type		= N_GSYM;
						globalsStab.other		= 1;
						globalsStab.desc		= 0;
						globalsStab.value		= 0;
						globalsStab.string		= name;
						fStabs.push_back(globalsStab);
					}
				}
			}
		}
	}

	if ( wroteStartSO ) {
		//  emit ending SO
		ObjectFile::Reader::Stab endFileStab;
		endFileStab.atom		= NULL;
		endFileStab.type		= N_SO;
		endFileStab.other		= 1;
		endFileStab.desc		= 0;
		endFileStab.value		= 0;
		endFileStab.string		= "";
		fStabs.push_back(endFileStab);
	}
}




void Linker::collectDebugInfo()
{
	std::map<const class ObjectFile::Atom*, uint32_t>	atomOrdinals;
	fStartDebugTime = mach_absolute_time();
	if ( fOptions.readerOptions().fDebugInfoStripping != ObjectFile::ReaderOptions::kDebugInfoNone ) {

		// determine mixture of stabs and dwarf
		bool someStabs = false;
		bool someDwarf = false;
		for (std::vector<class ObjectFile::Reader*>::iterator it=fReadersThatHaveSuppliedAtoms.begin();
				it != fReadersThatHaveSuppliedAtoms.end();
				it++) {
			ObjectFile::Reader* reader = *it;
			if ( reader != NULL ) {
				switch ( reader->getDebugInfoKind() ) {
					case ObjectFile::Reader::kDebugInfoNone:
						break;
					case ObjectFile::Reader::kDebugInfoStabs:
						someStabs = true;
						break;
					case ObjectFile::Reader::kDebugInfoDwarf:
						someDwarf = true;
						fCreateUUID = true;
						break;
				    case ObjectFile::Reader::kDebugInfoStabsUUID:
						someStabs = true;
						fCreateUUID = true;
						break;
					default:
						throw "Unhandled type of debug information";
				}
			}
		}
		
		if ( someDwarf || someStabs ) {
			// try to minimize re-allocations
			fStabs.reserve(1024); 

			// make mapping from atoms to ordinal
			uint32_t ordinal = 1;
			for (std::vector<ObjectFile::Atom*>::iterator it=fAllAtoms.begin(); it != fAllAtoms.end(); it++) {
				atomOrdinals[*it] = ordinal++;
			}
		}
			
		// process all dwarf .o files as a batch
		if ( someDwarf ) {
			// make mapping from readers with dwarf to ordinal
			std::map<class ObjectFile::Reader*, uint32_t>	readersWithDwarfOrdinals;
			uint32_t readerOrdinal = 1;
			for (std::vector<class ObjectFile::Reader*>::iterator it=fReadersThatHaveSuppliedAtoms.begin();
					it != fReadersThatHaveSuppliedAtoms.end();
					it++) {
				ObjectFile::Reader* reader = *it;
				if ( (reader != NULL) && (reader->getDebugInfoKind() == ObjectFile::Reader::kDebugInfoDwarf) ) {
					readersWithDwarfOrdinals[reader] = readerOrdinal++;
				}
			}
		
			// make a vector of atoms 
			std::vector<class ObjectFile::Atom*> allAtomsByReader(fAllAtoms.begin(), fAllAtoms.end());
			// remove those not from a reader that has dwarf
			allAtomsByReader.erase(std::remove_if(allAtomsByReader.begin(), allAtomsByReader.end(), 
								NoDebugNoteAtom(readersWithDwarfOrdinals)), allAtomsByReader.end());
			// sort by reader then atom ordinal
			std::sort(allAtomsByReader.begin(), allAtomsByReader.end(), ReadersWithDwarfSorter(readersWithDwarfOrdinals, atomOrdinals));
			// add debug notes for each atom
			this->synthesizeDebugNotes(allAtomsByReader);
		}
		
		// process all stabs .o files one by one
		if ( someStabs ) {
			// get stabs from each reader, in command line order
			for (std::vector<class ObjectFile::Reader*>::iterator it=fReadersThatHaveSuppliedAtoms.begin();
					it != fReadersThatHaveSuppliedAtoms.end();
					it++) {
				ObjectFile::Reader* reader = *it;
				if ( reader != NULL ) {
					switch ( reader->getDebugInfoKind() ) {
						case ObjectFile::Reader::kDebugInfoDwarf:
						case ObjectFile::Reader::kDebugInfoNone:
							// do nothing
							break;
						case ObjectFile::Reader::kDebugInfoStabs:
						case ObjectFile::Reader::kDebugInfoStabsUUID:
							collectStabs(reader, atomOrdinals);
							break;
						default:
							throw "Unhandled type of debug information";
					}
				}
			}
			// remove stabs associated with atoms that won't be in output
			std::set<class ObjectFile::Atom*>	allAtomsSet;
			allAtomsSet.insert(fAllAtoms.begin(), fAllAtoms.end());
			fStabs.erase(std::remove_if(fStabs.begin(), fStabs.end(), NotInSet(allAtomsSet)), fStabs.end());
		}
	}
}

void Linker::writeOutput()
{
	fStartWriteTime = mach_absolute_time();
	// tell writer about each segment's atoms
	fOutputFileSize = fOutputFile->write(fAllAtoms, fStabs, this->entryPoint(), this->dyldHelper(), (fCreateUUID && fOptions.emitUUID()));
}

ObjectFile::Reader* Linker::createReader(const Options::FileInfo& info)
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
	const fat_header* fh = (fat_header*)p;
	if ( fh->magic == OSSwapBigToHostInt32(FAT_MAGIC) ) {
		// Fat header is always big-endian
		const struct fat_arch* archs = (struct fat_arch*)(p + sizeof(struct fat_header));
		for (unsigned long i=0; i < OSSwapBigToHostInt32(fh->nfat_arch); ++i) {
			if ( OSSwapBigToHostInt32(archs[i].cputype) == (uint32_t)fArchitecture ) {
				uint32_t fileOffset = OSSwapBigToHostInt32(archs[i].offset);
				len = OSSwapBigToHostInt32(archs[i].size);
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
				break;
			}
		}
	}
	::close(fd);

	switch (fArchitecture) {
		case CPU_TYPE_POWERPC:
			if ( mach_o::relocatable::Reader<ppc>::validFile(p) )
				return this->addObject(mach_o::relocatable::Reader<ppc>::make(p, info.path, info.modTime, fOptions.readerOptions()), info, len);
			else if ( mach_o::dylib::Reader<ppc>::validFile(p, info.options.fBundleLoader) )
				return this->addDylib(mach_o::dylib::Reader<ppc>::make(p, len, info.path, info.options.fBundleLoader, fOptions.readerOptions()), info, len);
			else if ( mach_o::archive::Reader<ppc>::validFile(p, len) )
				return this->addArchive(mach_o::archive::Reader<ppc>::make(p, len, info.path, info.modTime, fOptions.readerOptions()), info, len);
			break;
		case CPU_TYPE_POWERPC64:
			if ( mach_o::relocatable::Reader<ppc64>::validFile(p) )
				return this->addObject(mach_o::relocatable::Reader<ppc64>::make(p, info.path, info.modTime, fOptions.readerOptions()), info, len);
			else if ( mach_o::dylib::Reader<ppc64>::validFile(p, info.options.fBundleLoader) )
				return this->addDylib(mach_o::dylib::Reader<ppc64>::make(p, len, info.path, info.options.fBundleLoader, fOptions.readerOptions()), info, len);
			else if ( mach_o::archive::Reader<ppc64>::validFile(p, len) )
				return this->addArchive(mach_o::archive::Reader<ppc64>::make(p, len, info.path, info.modTime, fOptions.readerOptions()), info, len);
			break;
		case CPU_TYPE_I386:
			if ( mach_o::relocatable::Reader<x86>::validFile(p) )
				return this->addObject(mach_o::relocatable::Reader<x86>::make(p, info.path, info.modTime, fOptions.readerOptions()), info, len);
			else if ( mach_o::dylib::Reader<x86>::validFile(p, info.options.fBundleLoader) )
				return this->addDylib(mach_o::dylib::Reader<x86>::make(p, len, info.path, info.options.fBundleLoader, fOptions.readerOptions()), info, len);
			else if ( mach_o::archive::Reader<x86>::validFile(p, len) )
				return this->addArchive(mach_o::archive::Reader<x86>::make(p, len, info.path, info.modTime, fOptions.readerOptions()), info, len);
			break;
		case CPU_TYPE_X86_64:
			if ( mach_o::relocatable::Reader<x86_64>::validFile(p) )
				return this->addObject(mach_o::relocatable::Reader<x86_64>::make(p, info.path, info.modTime, fOptions.readerOptions()), info, len);
			else if ( mach_o::dylib::Reader<x86_64>::validFile(p, info.options.fBundleLoader) )
				return this->addDylib(mach_o::dylib::Reader<x86_64>::make(p, len, info.path, info.options.fBundleLoader, fOptions.readerOptions()), info, len);
			else if ( mach_o::archive::Reader<x86_64>::validFile(p, len) )
				return this->addArchive(mach_o::archive::Reader<x86_64>::make(p, len, info.path, info.modTime, fOptions.readerOptions()), info, len);
			break;
	}

	// error handling
	if ( ((fat_header*)p)->magic == OSSwapBigToHostInt32(FAT_MAGIC) ) {
		throwf("missing required architecture %s in file", fArchitectureName);
	}
	else {
		throw "file is not of required architecture";
	}
}


void Linker::createReaders()
{
	fStartCreateReadersTime = mach_absolute_time();
	std::vector<Options::FileInfo>& files = fOptions.getInputFiles();
	const int count = files.size();
	if ( count == 0 )
		throw "no object files specified";
	// add all direct object, archives, and dylibs
	for (int i=0; i < count; ++i) {
		Options::FileInfo& entry = files[i];
		// ignore /usr/lib/dyld on command line in crt.o build
		if ( strcmp(entry.path, "/usr/lib/dyld") != 0 ) {
			try {
				this->addInputFile(this->createReader(entry));
			}
			catch (const char* msg) {
				if ( strstr(msg, "architecture") != NULL ) {
					if (  fOptions.ignoreOtherArchInputFiles() ) {
						// ignore, because this is about an architecture not in use
					}
					else {
						fprintf(stderr, "ld64 warning: in %s, %s\n", entry.path, msg);
					}
				}
				else {
					throwf("in %s, %s", entry.path, msg);
				}
			}
		}
	}

	// add first level of indirect dylibs
	fDirectLibrariesComplete = true;
	for (std::vector<ExecutableFile::DyLibUsed>::iterator it=fDynamicLibraries.begin(); it != fDynamicLibraries.end(); it++) {
		this->addIndirectLibraries(it->reader);
	}

	// indirect handling depends on namespace
	switch ( fOptions.nameSpace() ) {
		case Options::kFlatNameSpace:
		case Options::kForceFlatNameSpace:
			// with flat namespace, blindly load all indirect libraries
			// the indirect list will grow as indirect libraries are loaded
			for (std::list<IndirectLibrary>::iterator it=fIndirectDynamicLibraries.begin(); it != fIndirectDynamicLibraries.end(); it++) {
				try {
					it->reader = this->createReader(fOptions.findFile(it->path));
					it->reader->setSortOrder(fNextObjectFileOrder++);
				}
				catch (const char* msg) {
					fprintf(stderr, "ld64 warning: indirect library %s could not be loaded: %s\n", it->path, msg);
				}
			}
			break;

		case Options::kTwoLevelNameSpace:
			// with two-level namespace we only want to use indirect libraries that are re-exported through a library that is used
			{
				bool indirectAdded = true;
				while ( indirectAdded ) {
					indirectAdded = false;
					// instantiate a reader for each indirect library and try to find parent that re-exports it
					for (std::list<IndirectLibrary>::iterator it=fIndirectDynamicLibraries.begin(); it != fIndirectDynamicLibraries.end(); it++) {
						if ( it->reader == NULL ) {
							try {
								it->reader = this->createReader(fOptions.findFile(it->path));
								it->reader->setSortOrder(fNextObjectFileOrder++);
								indirectAdded = true;
							}
							catch (const char* msg) {
								fprintf(stderr, "ld64 warning: indirect library %s could not be loaded: %s\n", it->path, msg);
							}
						}
						// if an indirect library does not have an assigned parent, look for one
						if ( (it->reader != NULL) && (it->reExportedViaDirectLibrary == NULL) ) {
							it->reExportedViaDirectLibrary = this->findDirectLibraryWhichReExports(*it);
						}
					}
				}
			}
			break;
	}

	// add relevant indirect libraries to the end of fDynamicLibraries
	for (std::list<IndirectLibrary>::iterator it=fIndirectDynamicLibraries.begin(); it != fIndirectDynamicLibraries.end(); it++) {
		if ( (it->reader != NULL) && (it->reExportedViaDirectLibrary != NULL) || (fOptions.nameSpace() != Options::kTwoLevelNameSpace) ) {
			ExecutableFile::DyLibUsed dylibInfo;
			dylibInfo.reader = it->reader;
			dylibInfo.options.fWeakImport = false;
			dylibInfo.options.fReExport = false;
			dylibInfo.options.fInstallPathOverride = NULL;
			dylibInfo.indirect = true;
			dylibInfo.directReader = it->reExportedViaDirectLibrary;
			fDynamicLibraries.push_back(dylibInfo);
			if ( fOptions.readerOptions().fTraceIndirectDylibs ) {
				const char* fullPath = it->reader->getPath();
				char realName[MAXPATHLEN];
				if ( realpath(fullPath, realName) != NULL )
					fullPath = realName;
				logTraceInfo("[Logging for XBS] Used indirect dynamic library: %s\n", fullPath);
			}
		}
	}
}


ObjectFile::Reader* Linker::findDirectLibraryWhichReExports(IndirectLibrary& indirectLib)
{
	// ask each parent if they re-export this dylib
	for (std::set<ObjectFile::Reader*>::iterator pit=indirectLib.parents.begin(); pit != indirectLib.parents.end(); pit++) {
		if ( (*pit)->reExports(indirectLib.reader) ) {
			ObjectFile::Reader* lib = *pit;
			// first check if we found a direct library, if so return it
			for (std::vector<ExecutableFile::DyLibUsed>::iterator dit=fDynamicLibraries.begin(); dit != fDynamicLibraries.end(); dit++) {
				if ( dit->reader == lib && dit->indirect == false )
					return lib;
			}
			// otherwise search indirects for parent and see how it is reexported
			for (std::list<IndirectLibrary>::iterator iit=fIndirectDynamicLibraries.begin(); iit != fIndirectDynamicLibraries.end(); iit++) {
				if ( iit->reader == lib ) {
					ObjectFile::Reader* lib2 = this->findDirectLibraryWhichReExports(*iit);
					if ( lib2 != NULL )
						return lib2;
				}
			}
		}
	}
	return NULL;
}



ObjectFile::Reader* Linker::addArchive(ObjectFile::Reader* reader, const Options::FileInfo& info, uint64_t mappedLen)
{
	if (fOptions.readerOptions().fTraceArchives) {
		const char* fullPath = reader->getPath();
		char realName[MAXPATHLEN];
		if ( realpath(fullPath, realName) != NULL )
			fullPath = realName;
		logTraceInfo("[Logging for XBS] Used static archive: %s\n", fullPath);
	}

	// update stats
	fTotalArchiveSize += mappedLen;
	++fTotalArchivesLoaded;
	return reader;
}

ObjectFile::Reader* Linker::addObject(ObjectFile::Reader* reader, const Options::FileInfo& info, uint64_t mappedLen)
{
	// update stats
	fTotalObjectSize += mappedLen;
	++fTotalObjectLoaded;
	return reader;
}

ObjectFile::Reader* Linker::addDylib(ObjectFile::Reader* reader, const Options::FileInfo& info, uint64_t mappedLen)
{
	if ( (reader->getInstallPath() == NULL) && !info.options.fBundleLoader ) {
		// this is a "blank" stub
		// silently ignore it
		return reader;
	}

	if ( fDirectLibrariesComplete ) {
		this->addIndirectLibraries(reader);
	}
	else {
		if ( fOptions.readerOptions().fTraceDylibs ) {
			const char* fullPath = reader->getPath();
			char realName[MAXPATHLEN];
			if ( realpath(fullPath, realName) != NULL )
				fullPath = realName;
			logTraceInfo("[Logging for XBS] Used dynamic library: %s\n", fullPath);
		}
		ExecutableFile::DyLibUsed dylibInfo;
		dylibInfo.reader = reader;
		dylibInfo.options = info.options;
		dylibInfo.indirect = false;
		dylibInfo.directReader = NULL;
		fDynamicLibraries.push_back(dylibInfo);


		// Verify that a client is allowed to link to this dylib.  There are three cases.
		bool okToLink = true;
		const char* outputFilePath = fOptions.installPath();
		const char* outputFilePathLastSlash = strrchr(outputFilePath, '/');
		if ( reader->parentUmbrella() != NULL ) {
			// case 1) The dylib has a parent umbrella, and we are creating the parent umbrella
			okToLink = ( (outputFilePathLastSlash != NULL) && (strcmp(&outputFilePathLastSlash[1], reader->parentUmbrella()) == 0) );
		}

		if ( !okToLink && (reader->parentUmbrella() != NULL) ) {
			// case 2) The dylib has a parent umbrella, and we are creating a sibling with the same parent
			okToLink = ( (outputFilePathLastSlash != NULL)
					&& (fOptions.umbrellaName() != NULL)
					&& (strcmp(fOptions.umbrellaName(), reader->parentUmbrella()) == 0) );
		}

		std::vector<const char*>* clients = reader->getAllowableClients();
		if ( !okToLink && (clients != NULL) ) {
			// case 3) the dylib has a list of allowable clients, and we are creating one of them
			const char* clientName = fOptions.clientName();
			int clientNameLen = 0;
			if ( clientName != NULL ) {
				// use client name as specified on command line
				clientNameLen = strlen(clientName);
			}
			else {
				// infer client name from output path (e.g. xxx/libfoo.A.dylib --> foo, Bar.framework/Bar --> Bar)
				clientName = outputFilePath;
				// starts after last slash
				if ( outputFilePathLastSlash != NULL )
					clientName = &outputFilePathLastSlash[1];
				if ( strncmp(clientName, "lib", 3) == 0 )
					clientName = &clientName[3];
				// up to first dot
				const char* firstDot = strchr(clientName, '.');
				if ( firstDot == NULL )
					clientNameLen = strlen(clientName);
				else
					clientNameLen = firstDot - clientName;
			}

			// Use clientName to check if this dylib is able to link against the allowable clients.
			for (std::vector<const char*>::iterator it = clients->begin(); it != clients->end(); it++) {
				if ( strncmp(*it, clientName, clientNameLen) == 0 )
					okToLink = true;
			}
		}

		// error out if we are not allowed to link
		if ( ! okToLink )
			//throwf("'%s' is a subframework. Link against the umbrella framework '%s.framework' instead.",
			fprintf(stderr, "'%s' is a subframework. Link against the umbrella framework '%s.framework' instead.",
					reader->getPath(), reader->parentUmbrella());
	}

	// update stats
	++fTotalDylibsLoaded;

	return reader;
}


void Linker::addIndirectLibraries(ObjectFile::Reader* reader)
{
	std::vector<const char*>* dependentLibs = reader->getDependentLibraryPaths();
	if ( dependentLibs != NULL ) {
		for (std::vector<const char*>::iterator it=dependentLibs->begin(); it != dependentLibs->end(); it++) {
			if ( this->haveDirectLibrary(*it) ) {
				// do nothing, direct library already exists
			}
			else if ( this->haveIndirectLibrary(*it, reader) ) {
				// side effect of haveIndirectLibrary() added reader to parent list
			}
			else {
				// add to list of indirect libraries
				IndirectLibrary indirectLib;
				indirectLib.path = *it;
				indirectLib.fileLen = 0;
				indirectLib.reader = NULL;
				indirectLib.parents.insert(reader);
				indirectLib.reExportedViaDirectLibrary = NULL;
				fIndirectDynamicLibraries.push_back(indirectLib);
				//fprintf(stderr, "add indirect library: %s\n", *it);
			}
		}
	}
}

bool Linker::haveIndirectLibrary(const char* path, ObjectFile::Reader* parentReader)
{
	for (std::list<IndirectLibrary>::iterator it=fIndirectDynamicLibraries.begin(); it != fIndirectDynamicLibraries.end(); it++) {
		if ( strcmp(path, it->path) == 0 ) {
			it->parents.insert(parentReader);
			return true;
		}
		if ( it->reader != NULL ) {
			const char* installPath = it->reader->getInstallPath();
			if ( (installPath != NULL) && (strcmp(path, installPath) == 0) )
				return true;
		}
	}
	return false;
}

bool Linker::haveDirectLibrary(const char* path)
{
	for (std::vector<ExecutableFile::DyLibUsed>::iterator it=fDynamicLibraries.begin(); it != fDynamicLibraries.end(); it++) {
		if ( strcmp(path, it->reader->getPath()) == 0 )
			return true;
		const char* installPath = it->reader->getInstallPath();
		if ( (installPath != NULL) && (strcmp(path, installPath) == 0) )
			return true;
	}
	return false;
}

void Linker::logTraceInfo (const char* format, ...)
{
	static int trace_file = -1;
	char trace_buffer[MAXPATHLEN * 2];
	char *buffer_ptr;
	int length;
	ssize_t amount_written;
	const char *trace_file_path = fOptions.readerOptions().fTraceOutputFile;

	if(trace_file == -1) {
		if(trace_file_path != NULL) {
			trace_file = open(trace_file_path, O_WRONLY | O_APPEND | O_CREAT, 0666);
			if(trace_file == -1)
				throwf("Could not open or create trace file: %s\n", trace_file_path);
		}
		else {
			trace_file = fileno(stderr);
		}
	}

    va_list ap;
	va_start(ap, format);
	length = vsnprintf(trace_buffer, sizeof(trace_buffer), format, ap);
	va_end(ap);
	buffer_ptr = trace_buffer;

	while(length > 0) {
		amount_written = write(trace_file, buffer_ptr, length);
		if(amount_written == -1)
			/* Failure to write shouldn't fail the build. */
			return;
		buffer_ptr += amount_written;
		length -= amount_written;
	}
}



void Linker::createWriter()
{
	fStartCreateWriterTime = mach_absolute_time();
	const char* path = fOptions.getOutputFilePath();
	switch ( fArchitecture ) {
		case CPU_TYPE_POWERPC:
			this->setOutputFile(new mach_o::executable::Writer<ppc>(path, fOptions, fDynamicLibraries));
			break;
		case CPU_TYPE_POWERPC64:
			this->setOutputFile(new mach_o::executable::Writer<ppc64>(path, fOptions, fDynamicLibraries));
			break;
		case CPU_TYPE_I386:
			this->setOutputFile(new mach_o::executable::Writer<x86>(path, fOptions, fDynamicLibraries));
			break;
		case CPU_TYPE_X86_64:
			this->setOutputFile(new mach_o::executable::Writer<x86_64>(path, fOptions, fDynamicLibraries));
			break;
		default:
			throw "unknown architecture";
	}
}


Linker::SymbolTable::SymbolTable(Linker& owner)
 : fOwner(owner), fRequireCount(0)
{
}

void Linker::SymbolTable::require(const char* name)
{
	//fprintf(stderr, "require(%s)\n", name);
	Mapper::iterator pos = fTable.find(name);
	if ( pos == fTable.end() ) {
		fTable[name] = NULL;
		++fRequireCount;
	}
}

// convenience labels for 2-dimensional switch statement
enum {
	kRegAndReg				= (ObjectFile::Atom::kRegularDefinition << 3)	| ObjectFile::Atom::kRegularDefinition,
	kRegAndWeak				= (ObjectFile::Atom::kRegularDefinition << 3)	| ObjectFile::Atom::kWeakDefinition,
	kRegAndTent				= (ObjectFile::Atom::kRegularDefinition << 3)	| ObjectFile::Atom::kTentativeDefinition,
	kRegAndExtern			= (ObjectFile::Atom::kRegularDefinition << 3)	| ObjectFile::Atom::kExternalDefinition,
	kRegAndExternWeak		= (ObjectFile::Atom::kRegularDefinition << 3)	| ObjectFile::Atom::kExternalWeakDefinition,
	kWeakAndReg				= (ObjectFile::Atom::kWeakDefinition << 3)		| ObjectFile::Atom::kRegularDefinition,
	kWeakAndWeak			= (ObjectFile::Atom::kWeakDefinition << 3)		| ObjectFile::Atom::kWeakDefinition,
	kWeakAndTent			= (ObjectFile::Atom::kWeakDefinition << 3)		| ObjectFile::Atom::kTentativeDefinition,
	kWeakAndExtern			= (ObjectFile::Atom::kWeakDefinition << 3)		| ObjectFile::Atom::kExternalDefinition,
	kWeakAndExternWeak		= (ObjectFile::Atom::kWeakDefinition << 3)		| ObjectFile::Atom::kExternalWeakDefinition,
	kTentAndReg				= (ObjectFile::Atom::kTentativeDefinition << 3) | ObjectFile::Atom::kRegularDefinition,
	kTentAndWeak			= (ObjectFile::Atom::kTentativeDefinition << 3) | ObjectFile::Atom::kWeakDefinition,
	kTentAndTent			= (ObjectFile::Atom::kTentativeDefinition << 3) | ObjectFile::Atom::kTentativeDefinition,
	kTentAndExtern			= (ObjectFile::Atom::kTentativeDefinition << 3) | ObjectFile::Atom::kExternalDefinition,
	kTentAndExternWeak		= (ObjectFile::Atom::kTentativeDefinition << 3) | ObjectFile::Atom::kExternalWeakDefinition,
	kExternAndReg			= (ObjectFile::Atom::kExternalDefinition << 3)	| ObjectFile::Atom::kRegularDefinition,
	kExternAndWeak			= (ObjectFile::Atom::kExternalDefinition << 3)	| ObjectFile::Atom::kWeakDefinition,
	kExternAndTent			= (ObjectFile::Atom::kExternalDefinition << 3)	| ObjectFile::Atom::kTentativeDefinition,
	kExternAndExtern		= (ObjectFile::Atom::kExternalDefinition << 3)	| ObjectFile::Atom::kExternalDefinition,
	kExternAndExternWeak	= (ObjectFile::Atom::kExternalDefinition << 3)	| ObjectFile::Atom::kExternalWeakDefinition,
	kExternWeakAndReg		= (ObjectFile::Atom::kExternalWeakDefinition << 3) | ObjectFile::Atom::kRegularDefinition,
	kExternWeakAndWeak		= (ObjectFile::Atom::kExternalWeakDefinition << 3) | ObjectFile::Atom::kWeakDefinition,
	kExternWeakAndTent		= (ObjectFile::Atom::kExternalWeakDefinition << 3) | ObjectFile::Atom::kTentativeDefinition,
	kExternWeakAndExtern	= (ObjectFile::Atom::kExternalWeakDefinition << 3) | ObjectFile::Atom::kExternalDefinition,
	kExternWeakAndExternWeak= (ObjectFile::Atom::kExternalWeakDefinition << 3) | ObjectFile::Atom::kExternalWeakDefinition
};

bool Linker::SymbolTable::add(ObjectFile::Atom& newAtom)
{
	bool useNew = true;
	const char* name = newAtom.getName();
	//fprintf(stderr, "map.add(%s => %p from %s)\n", name, &newAtom, newAtom.getFile()->getPath());
	Mapper::iterator pos = fTable.find(name);
	ObjectFile::Atom* existingAtom = NULL;
	if ( pos != fTable.end() )
		existingAtom = pos->second;
	if ( existingAtom != NULL ) {
		// already have atom with same name in symbol table
		switch ( (existingAtom->getDefinitionKind() << 3) | newAtom.getDefinitionKind() ) {
			case kRegAndReg:
				throwf("duplicate symbol %s in %s and %s\n", name, newAtom.getFile()->getPath(), existingAtom->getFile()->getPath());
			case kRegAndWeak:
				// ignore new weak atom, because we already have a non-weak one
				useNew = false;
				break;
			case kRegAndTent:
				// ignore new tentative atom, because we already have a regular one
				useNew = false;
				break;
			case kRegAndExtern:
				// ignore external atom, because we already have a one
				useNew = false;
				break;
			case kRegAndExternWeak:
				// ignore external atom, because we already have a one
				useNew = false;
				break;
			case kWeakAndReg:
				// replace existing weak atom with regular one
				break;
			case kWeakAndWeak:
				// have another weak atom, use whichever has largest alignment requirement
				// because codegen of some client may require alignment
				useNew = ( newAtom.getAlignment().leadingZeros() > existingAtom->getAlignment().leadingZeros() );
				break;
			case kWeakAndTent:
				// replace existing weak atom with tentative one ???
				break;
			case kWeakAndExtern:
				// keep weak atom, at runtime external one may override
				useNew = false;
				break;
			case kWeakAndExternWeak:
				// keep weak atom, at runtime external one may override
				useNew = false;
				break;
			case kTentAndReg:
				// replace existing tentative atom with regular one
				break;
			case kTentAndWeak:
				// replace existing tentative atom with weak one ???
				break;
			case kTentAndTent:
				// use largest
				if ( newAtom.getSize() < existingAtom->getSize() ) {
					useNew = false;
				} else {
					if ( newAtom.getAlignment().leadingZeros() < existingAtom->getAlignment().leadingZeros() )
						fprintf(stderr, "ld64 warning: alignment lost in merging tentative definition %s\n", newAtom.getDisplayName());
				}
				break;
			case kTentAndExtern:
			case kTentAndExternWeak:
				// a tentative definition and a dylib definition, so commons-mode decides how to handle
				switch ( fOwner.fOptions.commonsMode() ) {
					case Options::kCommonsIgnoreDylibs:
						if ( fOwner.fOptions.warnCommons() )
							fprintf(stderr, "ld64: using common symbol %s from %s and ignoring defintion from dylib %s\n",
									existingAtom->getName(), existingAtom->getFile()->getPath(), newAtom.getFile()->getPath());
						useNew = false;
						break;
					case Options::kCommonsOverriddenByDylibs:
						if ( fOwner.fOptions.warnCommons() )
							fprintf(stderr, "ld64: replacing common symbol %s from %s with true definition from dylib %s\n",
									existingAtom->getName(), existingAtom->getFile()->getPath(), newAtom.getFile()->getPath());
						break;
					case Options::kCommonsConflictsDylibsError:
						throwf("common symbol %s from %s conflicts with defintion from dylib %s",
								existingAtom->getName(), existingAtom->getFile()->getPath(), newAtom.getFile()->getPath());
				}
				break;
			case kExternAndReg:
				// replace external atom with regular one
				break;
			case kExternAndWeak:
				// replace external atom with weak one
				break;
			case kExternAndTent:
				// a tentative definition and a dylib definition, so commons-mode decides how to handle
				switch ( fOwner.fOptions.commonsMode() ) {
					case Options::kCommonsIgnoreDylibs:
						if ( fOwner.fOptions.warnCommons() )
							fprintf(stderr, "ld64: using common symbol %s from %s and ignoring defintion from dylib %s\n",
									newAtom.getName(), newAtom.getFile()->getPath(), existingAtom->getFile()->getPath());
						break;
					case Options::kCommonsOverriddenByDylibs:
						if ( fOwner.fOptions.warnCommons() )
							fprintf(stderr, "ld64: replacing defintion of %s from dylib %s with common symbol from %s\n",
									newAtom.getName(), existingAtom->getFile()->getPath(), newAtom.getFile()->getPath());
						useNew = false;
						break;
					case Options::kCommonsConflictsDylibsError:
						throwf("common symbol %s from %s conflicts with defintion from dylib %s",
									newAtom.getName(), newAtom.getFile()->getPath(), existingAtom->getFile()->getPath());
				}
				break;
			case kExternAndExtern:
				throwf("duplicate symbol %s in %s and %s\n", name, newAtom.getFile()->getPath(), existingAtom->getFile()->getPath());
			case kExternAndExternWeak:
				// keep strong dylib atom, ignore weak one
				useNew = false;
				break;
			case kExternWeakAndReg:
				// replace existing weak external with regular
				break;
			case kExternWeakAndWeak:
				// replace existing weak external with weak (let dyld decide at runtime which to use)
				break;
			case kExternWeakAndTent:
				// a tentative definition and a dylib definition, so commons-mode decides how to handle
				switch ( fOwner.fOptions.commonsMode() ) {
					case Options::kCommonsIgnoreDylibs:
						if ( fOwner.fOptions.warnCommons() )
							fprintf(stderr, "ld64: using common symbol %s from %s and ignoring defintion from dylib %s\n",
									newAtom.getName(), newAtom.getFile()->getPath(), existingAtom->getFile()->getPath());
						break;
					case Options::kCommonsOverriddenByDylibs:
						if ( fOwner.fOptions.warnCommons() )
							fprintf(stderr, "ld64: replacing defintion of %s from dylib %s with common symbol from %s\n",
									newAtom.getName(), existingAtom->getFile()->getPath(), newAtom.getFile()->getPath());
						useNew = false;
						break;
					case Options::kCommonsConflictsDylibsError:
						throwf("common symbol %s from %s conflicts with defintion from dylib %s",
									newAtom.getName(), newAtom.getFile()->getPath(), existingAtom->getFile()->getPath());
				}
				break;
			case kExternWeakAndExtern:
				// replace existing weak external with external
				break;
			case kExternWeakAndExternWeak:
				// keep existing external weak
				useNew = false;
				break;
		}
	}
	if ( useNew ) {
		fTable[name] = &newAtom;
		if ( existingAtom != NULL )
			fOwner.fDeadAtoms.insert(existingAtom);
	}
	else {
		fOwner.fDeadAtoms.insert(&newAtom);
	}
	return useNew;
}



ObjectFile::Atom* Linker::SymbolTable::find(const char* name)
{
	Mapper::iterator pos = fTable.find(name);
	if ( pos != fTable.end() ) {
		return pos->second;
	}
	return NULL;
}


void Linker::SymbolTable::getNeededNames(bool andWeakDefintions, std::vector<const char*>& undefines)
{
	for (Mapper::iterator it=fTable.begin(); it != fTable.end(); it++) {
		if ( (it->second == NULL) || (andWeakDefintions && (it->second->getDefinitionKind()==ObjectFile::Atom::kWeakDefinition)) ) {
			undefines.push_back(it->first);
		}
	}
}




bool Linker::AtomSorter::operator()(ObjectFile::Atom* left, ObjectFile::Atom* right)
{
	// first sort by section order (which is already sorted by segment)
	unsigned int leftSectionIndex  =  left->getSection()->getIndex();
	unsigned int rightSectionIndex = right->getSection()->getIndex();
	if ( leftSectionIndex != rightSectionIndex)
		return (leftSectionIndex < rightSectionIndex);

	// then sort by .o file order
	ObjectFile::Reader* leftReader = left->getFile();
	ObjectFile::Reader* rightReader = right->getFile();
	if ( leftReader !=  rightReader )
		return leftReader->getSortOrder() < rightReader->getSortOrder();

	// lastly sort by atom within a .o file
	return left->getSortOrder() < right->getSortOrder();
}


int main(int argc, const char* argv[])
{
	const char* archName = NULL;
	bool showArch = false;
	bool archInferred = false;
	try {
		// create linker object given command line arguments
		Linker ld(argc, argv);

		// save error message prefix
		archName = ld.architectureName();
		archInferred = ld.isInferredArchitecture();
		showArch = ld.showArchitectureInErrors();

		// open all input files
		ld.createReaders();

		// open output file
		ld.createWriter();

		// do linking
		ld.link();
	}
	catch (const char* msg) {
		extern const double ld64VersionNumber;
		if ( archInferred )
			fprintf(stderr, "ld64-%g failed: %s for inferred architecture %s\n", ld64VersionNumber, msg, archName);
		else if ( showArch )
			fprintf(stderr, "ld64-%g failed: %s for architecture %s\n", ld64VersionNumber, msg, archName);
		else
			fprintf(stderr, "ld64-%g failed: %s\n", ld64VersionNumber, msg);
		return 1;
	}

	return 0;
}

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

#ifndef __OBJECT_FILE_DYLIB_MACH_O__
#define __OBJECT_FILE_DYLIB_MACH_O__

#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include <sys/param.h>


#include <vector>
#include <set>
#include <algorithm>
#include <ext/hash_map>

#include "MachOFileAbstraction.hpp"
#include "ObjectFile.h"

//
//
//	To implement architecture xxx, you must write template specializations for the following method:
//			Reader<xxx>::validFile()
//
//




namespace mach_o {
namespace dylib {


// forward reference
template <typename A> class Reader;


class Segment : public ObjectFile::Segment
{
public:
								Segment(const char* name)		{ fName = name; }
	virtual const char*			getName() const					{ return fName; }
	virtual bool				isContentReadable() const		{ return true; }
	virtual bool				isContentWritable() const		{ return false; }
	virtual bool				isContentExecutable() const		{ return false; }
private:
	const char*					fName;
};


//
// An ExportAtom has no content.  It exists so that the linker can track which imported
// symbols can from which dynamic libraries.
//
template <typename A>
class ExportAtom : public ObjectFile::Atom
{
public:
	virtual ObjectFile::Reader*					getFile() const				{ return &fOwner; }
	virtual bool								getTranslationUnitSource(const char** dir, const char** name) const { return false; }
	virtual const char*							getName() const				{ return fName; }
	virtual const char*							getDisplayName() const		{ return fName; }
	virtual Scope								getScope() const			{ return ObjectFile::Atom::scopeGlobal; }
	virtual DefinitionKind						getDefinitionKind() const	{ return fWeakDefinition ? kExternalWeakDefinition : kExternalDefinition; }
	virtual SymbolTableInclusion				getSymbolTableInclusion() const	{ return ObjectFile::Atom::kSymbolTableIn; }
	virtual	bool								dontDeadStrip() const		{ return false; }
	virtual bool								isZeroFill() const			{ return false; }
	virtual uint64_t							getSize() const				{ return 0; }
	virtual std::vector<ObjectFile::Reference*>&  getReferences() const		{ return fgEmptyReferenceList; }
	virtual bool								mustRemainInSection() const { return false; }
	virtual const char*							getSectionName() const		{ return "._imports"; }
	virtual Segment&							getSegment() const			{ return fgImportSegment; }
	virtual bool								requiresFollowOnAtom() const{ return false; }
	virtual ObjectFile::Atom&					getFollowOnAtom() const		{ return *((ObjectFile::Atom*)NULL); }
	virtual std::vector<ObjectFile::LineInfo>*	getLineInfo() const			{ return NULL; }
	virtual uint8_t								getAlignment() const		{ return 0; }
	virtual void								copyRawContent(uint8_t buffer[]) const  {}

	virtual void								setScope(Scope)				{ }

protected:
	friend class Reader<A>;
	typedef typename A::P					P;

											ExportAtom(ObjectFile::Reader& owner, const char* name, bool weak)
												: fOwner(owner), fName(name), fWeakDefinition(weak) {}
	virtual									~ExportAtom() {}

	ObjectFile::Reader&						fOwner;
	const char*								fName;
	bool									fWeakDefinition;

	static std::vector<ObjectFile::Reference*>	fgEmptyReferenceList;
	static Segment								fgImportSegment;
};

template <typename A>
Segment								ExportAtom<A>::fgImportSegment("__LINKEDIT");

template <typename A>
std::vector<ObjectFile::Reference*>	ExportAtom<A>::fgEmptyReferenceList;


//
// The reader for a dylib extracts all exported symbols names from the memory-mapped
// dylib, builds a hash table, then unmaps the file.  This is an important memory
// savings for large dylibs.
//
template <typename A>
class Reader : public ObjectFile::Reader
{
public:
	static bool										validFile(const uint8_t* fileContent, bool executableOrDylib);
	static Reader<A>*								make(const uint8_t* fileContent, uint64_t fileLength, const char* path, 
														bool executableOrDylib, const ObjectFile::ReaderOptions& options)
														{ return new Reader<A>(fileContent, fileLength, path, executableOrDylib, options); }
	virtual											~Reader() {}

	virtual const char*								getPath()					{ return fPath; }
	virtual time_t									getModificationTime()		{ return 0; }
	virtual DebugInfoKind							getDebugInfoKind()			{ return ObjectFile::Reader::kDebugInfoNone; }
	virtual std::vector<class ObjectFile::Atom*>&	getAtoms();
	virtual std::vector<class ObjectFile::Atom*>*	getJustInTimeAtomsFor(const char* name);
	virtual std::vector<Stab>*						getStabs()					{ return NULL; }
	virtual const char*								getInstallPath()			{ return fDylibInstallPath; }
	virtual uint32_t								getTimestamp()				{ return fDylibTimeStamp; }
	virtual uint32_t								getCurrentVersion()			{ return fDylibtCurrentVersion; }
	virtual uint32_t								getCompatibilityVersion()	{ return fDylibCompatibilityVersion; }
	virtual std::vector<const char*>*				getDependentLibraryPaths();
	virtual bool									reExports(ObjectFile::Reader*);
	virtual std::vector<const char*>*				getAllowableClients();

protected:
		const char*									parentUmbrella()			{ return fParentUmbrella; }

private:
	typedef typename A::P						P;
	typedef typename A::P::E					E;

	class CStringEquals
	{
	public:
		bool operator()(const char* left, const char* right) const { return (strcmp(left, right) == 0); }
	};
	struct AtomAndWeak { ObjectFile::Atom* atom; bool weak; };
	typedef __gnu_cxx::hash_map<const char*, AtomAndWeak, __gnu_cxx::hash<const char*>, CStringEquals> NameToAtomMap;
	typedef typename NameToAtomMap::iterator		NameToAtomMapIterator;

	struct PathAndFlag { const char* path; bool reExport; };

												Reader(const uint8_t* fileContent, uint64_t fileLength, const char* path,
														bool executableOrDylib, const ObjectFile::ReaderOptions& options);

	const char*									fPath;
	const char*									fParentUmbrella;
	std::vector<const char*>   					fAllowableClients;
	const char*									fDylibInstallPath;
	uint32_t									fDylibTimeStamp;
	uint32_t									fDylibtCurrentVersion;
	uint32_t									fDylibCompatibilityVersion;
	std::vector<PathAndFlag>					fDependentLibraryPaths;
	NameToAtomMap								fAtoms;

	static bool									fgLogHashtable;
	static std::vector<class ObjectFile::Atom*>	fgEmptyAtomList;
};

template <typename A>
std::vector<class ObjectFile::Atom*>	Reader<A>::fgEmptyAtomList;
template <typename A>
bool									Reader<A>::fgLogHashtable = false;


template <typename A>
Reader<A>::Reader(const uint8_t* fileContent, uint64_t fileLength, const char* path, bool executableOrDylib, const ObjectFile::ReaderOptions& options)
	: fParentUmbrella(NULL), fDylibInstallPath(NULL), fDylibTimeStamp(0), fDylibtCurrentVersion(0), fDylibCompatibilityVersion(0)
{
	// sanity check
	if ( ! validFile(fileContent, executableOrDylib) )
		throw "not a valid mach-o object file";

	fPath = strdup(path);
	
	const macho_header<P>* header = (const macho_header<P>*)fileContent;
	const uint32_t cmd_count = header->ncmds();
	const macho_load_command<P>* const cmds = (macho_load_command<P>*)((char*)header + sizeof(macho_header<P>));

	// a "blank" stub has zero load commands
	if ( (header->filetype() == MH_DYLIB_STUB) && (cmd_count == 0) ) {	
		// no further processing needed
		munmap((caddr_t)fileContent, fileLength);
		return;
	}

	// pass 1 builds list of all dependent libraries
	const macho_load_command<P>* cmd = cmds;
	for (uint32_t i = 0; i < cmd_count; ++i) {
		switch (cmd->cmd()) {
			case LC_LOAD_DYLIB:
			case LC_LOAD_WEAK_DYLIB:
				PathAndFlag entry;
				entry.path = strdup(((struct macho_dylib_command<P>*)cmd)->name());
				entry.reExport = false;
				fDependentLibraryPaths.push_back(entry);
				break;
		}
		cmd = (const macho_load_command<P>*)(((char*)cmd)+cmd->cmdsize());
	}

	// pass 2 determines re-export info
	const macho_dysymtab_command<P>* dynamicInfo = NULL;
	const macho_nlist<P>* symbolTable = NULL;
	const char*	strings = NULL;
	cmd = cmds;
	for (uint32_t i = 0; i < cmd_count; ++i) {
		switch (cmd->cmd()) {
			case LC_SYMTAB:
				{
					const macho_symtab_command<P>* symtab = (macho_symtab_command<P>*)cmd;
					symbolTable = (const macho_nlist<P>*)((char*)header + symtab->symoff());
					strings = (char*)header + symtab->stroff();
				}
				break;
			case LC_DYSYMTAB:
				dynamicInfo = (macho_dysymtab_command<P>*)cmd;
				break;
			case LC_ID_DYLIB:
				macho_dylib_command<P>* dylibID = (macho_dylib_command<P>*)cmd;
				fDylibInstallPath			= strdup(dylibID->name());
				fDylibTimeStamp				= dylibID->timestamp();
				fDylibtCurrentVersion		= dylibID->current_version();
				fDylibCompatibilityVersion	= dylibID->compatibility_version();
				break;
			case LC_SUB_UMBRELLA:
				if ( !options.fFlatNamespace ) {
					const char* frameworkLeafName = ((macho_sub_umbrella_command<P>*)cmd)->sub_umbrella();
					for (typename std::vector<PathAndFlag>::iterator it = fDependentLibraryPaths.begin(); it != fDependentLibraryPaths.end(); it++) {
						const char* dylibName = it->path;
						const char* lastSlash = strrchr(dylibName, '/');
						if ( (lastSlash != NULL) && (strcmp(&lastSlash[1], frameworkLeafName) == 0) )
							it->reExport = true;
					}
				}
				break;
			case LC_SUB_LIBRARY:
				if ( !options.fFlatNamespace ) {
					const char* dylibBaseName = ((macho_sub_library_command<P>*)cmd)->sub_library();
					for (typename std::vector<PathAndFlag>::iterator it = fDependentLibraryPaths.begin(); it != fDependentLibraryPaths.end(); it++) {
						const char* dylibName = it->path;
						const char* lastSlash = strrchr(dylibName, '/');
						const char* leafStart = &lastSlash[1];
						if ( lastSlash == NULL )
							leafStart = dylibName;
						const char* firstDot = strchr(leafStart, '.');
						int len = strlen(leafStart);
						if ( firstDot != NULL )
							len = firstDot - leafStart;
						if ( strncmp(leafStart, dylibBaseName, len) == 0 )
							it->reExport = true;
					}
				}
				break;
			case LC_SUB_FRAMEWORK:
				fParentUmbrella = strdup(((macho_sub_framework_command<P>*)cmd)->umbrella());
				break;
		}

		cmd = (const macho_load_command<P>*)(((char*)cmd)+cmd->cmdsize());
	}
	// Process the rest of the commands here.
	cmd = cmds;
	for (uint32_t i = 0; i < cmd_count; ++i) {
		switch (cmd->cmd()) {
		case LC_SUB_CLIENT:
			const char *temp = strdup(((macho_sub_client_command<P>*)cmd)->client());

			fAllowableClients.push_back(temp);
			break;
		}

		cmd = (const macho_load_command<P>*)(((char*)cmd)+cmd->cmdsize());
	}

	// validate minimal load commands
	if ( (fDylibInstallPath == NULL) && (header->filetype() != MH_EXECUTE) ) 
		throw "dylib missing LC_ID_DYLIB load command";
	if ( symbolTable == NULL )
		throw "dylib missing LC_SYMTAB load command";
	if ( dynamicInfo == NULL )
		throw "dylib missing LC_DYSYMTAB load command";
	
	// build hash table
	if ( dynamicInfo->tocoff() == 0 ) {
		if ( fgLogHashtable ) fprintf(stderr, "ld64: building hashtable of %u toc entries for %s\n", dynamicInfo->nextdefsym(), path);
		const macho_nlist<P>* start = &symbolTable[dynamicInfo->iextdefsym()];
		const macho_nlist<P>* end = &start[dynamicInfo->nextdefsym()];
		fAtoms.resize(dynamicInfo->nextdefsym()); // set initial bucket count
		for (const macho_nlist<P>* sym=start; sym < end; ++sym) {
			AtomAndWeak bucket;
			bucket.atom = NULL;
			bucket.weak = ((sym->n_desc() & N_WEAK_DEF) != 0);
			const char* name = strdup(&strings[sym->n_strx()]);
			if ( fgLogHashtable ) fprintf(stderr, "  adding %s to hash table for %s\n", name, this->getPath());
			fAtoms[name] = bucket;
		}
	}
	else {
		int32_t count = dynamicInfo->ntoc();
		fAtoms.resize(count); // set initial bucket count
		if ( fgLogHashtable ) fprintf(stderr, "ld64: building hashtable of %u entries for %s\n", count, path);
		const struct dylib_table_of_contents* toc = (dylib_table_of_contents*)((char*)header + dynamicInfo->tocoff());
		for (int32_t i = 0; i < count; ++i) {
			const uint32_t index = E::get32(toc[i].symbol_index);
			const macho_nlist<P>* sym = &symbolTable[index];
			AtomAndWeak bucket;
			bucket.atom = NULL;
			bucket.weak = ((sym->n_desc() & N_WEAK_DEF) != 0);
			const char* name = strdup(&strings[sym->n_strx()]);
			if ( fgLogHashtable ) fprintf(stderr, "  adding %s to hash table for %s\n", name, this->getPath());
			fAtoms[name] = bucket;
		}
	}

	// unmap file
	munmap((caddr_t)fileContent, fileLength);
}

template <typename A>
std::vector<class ObjectFile::Atom*>& Reader<A>::getAtoms()
{
	// TO DO:	for flat-namespace libraries, when linking flat_namespace
	//			we need to create an atom which references all undefines
	return fgEmptyAtomList;
}


template <typename A>
std::vector<class ObjectFile::Atom*>* Reader<A>::getJustInTimeAtomsFor(const char* name)
{
	std::vector<class ObjectFile::Atom*>* atoms = NULL;

	NameToAtomMapIterator pos = fAtoms.find(name);
	if ( pos != fAtoms.end() ) {
		if ( pos->second.atom == NULL ) {
			// instantiate atom and update hash table
			pos->second.atom = new ExportAtom<A>(*this, name, pos->second.weak);
			if ( fgLogHashtable ) fprintf(stderr, "getJustInTimeAtomsFor: %s found in %s\n", name, this->getPath());
		}
		// return a vector of one atom
		atoms = new std::vector<class ObjectFile::Atom*>;
		atoms->push_back(pos->second.atom);
	}
	else {
		if ( fgLogHashtable ) fprintf(stderr, "getJustInTimeAtomsFor: %s NOT found in %s\n", name, this->getPath());
	}
	return atoms;
}



template <typename A>
std::vector<const char*>* Reader<A>::getDependentLibraryPaths()
{
	std::vector<const char*>* result = new std::vector<const char*>;
	for (typename std::vector<PathAndFlag>::iterator it = fDependentLibraryPaths.begin(); it != fDependentLibraryPaths.end(); it++) {
		result->push_back(it->path);
	}
	return result;
}

template <typename A>
std::vector<const char*>* Reader<A>::getAllowableClients()
{
	std::vector<const char*>* result = new std::vector<const char*>;
	for (typename std::vector<const char*>::iterator it = fAllowableClients.begin();
		 it != fAllowableClients.end();
		 it++) {
		result->push_back(*it);
	}
	return (fAllowableClients.size() != 0 ? result : NULL);
}

template <typename A>
bool Reader<A>::reExports(ObjectFile::Reader* child)
{
	// A dependent dylib is re-exported under two conditions:
	//  1) parent contains LC_SUB_UMBRELLA or LC_SUB_LIBRARY with child name
	const char* childInstallPath = child->getInstallPath();
	for (typename std::vector<PathAndFlag>::iterator it = fDependentLibraryPaths.begin(); it != fDependentLibraryPaths.end(); it++) {
		if ( it->reExport && ((strcmp(it->path, child->getPath()) == 0) || ((childInstallPath!=NULL) && (strcmp(it->path, childInstallPath)==0))) )
			return true;
	}

	//  2) child contains LC_SUB_FRAMEWORK with parent name
	const char* parentUmbrellaName = ((Reader<A>*)child)->parentUmbrella();
	if ( parentUmbrellaName != NULL ) {
		const char* parentName = this->getPath();
		const char* lastSlash = strrchr(parentName, '/');
		if ( (lastSlash != NULL) && (strcmp(&lastSlash[1], parentUmbrellaName) == 0) )
			return true;
	}

	return false;
}

template <>
bool Reader<ppc>::validFile(const uint8_t* fileContent, bool executableOrDylib)
{
	const macho_header<P>* header = (const macho_header<P>*)fileContent;
	if ( header->magic() != MH_MAGIC )
		return false;
	if ( header->cputype() != CPU_TYPE_POWERPC )
		return false;
	switch ( header->filetype() ) {
		case MH_DYLIB:
		case MH_DYLIB_STUB:
			return true;
		case MH_EXECUTE:
			return executableOrDylib;
		default:
			return false;
	}
}

template <>
bool Reader<ppc64>::validFile(const uint8_t* fileContent, bool executableOrDylib)
{
	const macho_header<P>* header = (const macho_header<P>*)fileContent;
	if ( header->magic() != MH_MAGIC_64 )
		return false;
	if ( header->cputype() != CPU_TYPE_POWERPC64 )
		return false;
	switch ( header->filetype() ) {
		case MH_DYLIB:
		case MH_DYLIB_STUB:
			return true;
		case MH_EXECUTE:
			return executableOrDylib;
		default:
			return false;
	}
}

template <>
bool Reader<x86>::validFile(const uint8_t* fileContent, bool executableOrDylib)
{
	const macho_header<P>* header = (const macho_header<P>*)fileContent;
	if ( header->magic() != MH_MAGIC )
		return false;
	if ( header->cputype() != CPU_TYPE_I386 )
		return false;
	switch ( header->filetype() ) {
		case MH_DYLIB:
		case MH_DYLIB_STUB:
			return true;
		case MH_EXECUTE:
			return executableOrDylib;
		default:
			return false;
	}
}

template <>
bool Reader<x86_64>::validFile(const uint8_t* fileContent, bool executableOrDylib)
{
	const macho_header<P>* header = (const macho_header<P>*)fileContent;
	if ( header->magic() != MH_MAGIC_64 )
		return false;
	if ( header->cputype() != CPU_TYPE_X86_64 )
		return false;
	switch ( header->filetype() ) {
		case MH_DYLIB:
		case MH_DYLIB_STUB:
			return true;
		case MH_EXECUTE:
			return executableOrDylib;
		default:
			return false;
	}
}



}; // namespace dylib
}; // namespace mach_o


#endif // __OBJECT_FILE_DYLIB_MACH_O__

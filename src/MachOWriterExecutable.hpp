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

#ifndef __EXECUTABLE_MACH_O__
#define __EXECUTABLE_MACH_O__

#include <stdint.h>
#include <stddef.h>
#include <fcntl.h>
#include <sys/time.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/reloc.h>
//#include <mach-o/ppc/reloc.h>
#include <mach-o/stab.h>
#include <uuid/uuid.h>
#include <mach/i386/thread_status.h>
#include <mach/ppc/thread_status.h>

#include <vector>
#include <algorithm>
#include <map>
#include <set>
#include <ext/hash_map>

#include "ObjectFile.h"
#include "ExecutableFile.h"
#include "Options.h"

#include "MachOFileAbstraction.hpp"


//
//
//	To implement architecture xxx, you must write template specializations for the following methods:
//			MachHeaderAtom<xxx>::setHeaderInfo()
//			ThreadsLoadCommandsAtom<xxx>::getSize()
//			ThreadsLoadCommandsAtom<xxx>::copyRawContent()
//			Writer<xxx>::addObjectRelocs()
//			Writer<xxx>::fixUpReferenceRelocatable()
//			Writer<xxx>::fixUpReferenceFinal()
//			Writer<xxx>::stubableReferenceKind()
//			Writer<xxx>::weakImportReferenceKind()
//			Writer<xxx>::GOTReferenceKind()
//


namespace mach_o {
namespace executable {

// forward references
template <typename A> class WriterAtom;
template <typename A> class PageZeroAtom;
template <typename A> class CustomStackAtom;
template <typename A> class MachHeaderAtom;
template <typename A> class SegmentLoadCommandsAtom;
template <typename A> class SymbolTableLoadCommandsAtom;
template <typename A> class ThreadsLoadCommandsAtom;
template <typename A> class DylibIDLoadCommandsAtom;
template <typename A> class RoutinesLoadCommandsAtom;
template <typename A> class DyldLoadCommandsAtom;
template <typename A> class UUIDLoadCommandAtom;
template <typename A> class LinkEditAtom;
template <typename A> class SectionRelocationsLinkEditAtom;
template <typename A> class LocalRelocationsLinkEditAtom;
template <typename A> class ExternalRelocationsLinkEditAtom;
template <typename A> class SymbolTableLinkEditAtom;
template <typename A> class IndirectTableLinkEditAtom;
template <typename A> class StringsLinkEditAtom;
template <typename A> class LoadCommandsPaddingAtom;
template <typename A> class StubAtom;
template <typename A> class StubHelperAtom;
template <typename A> class LazyPointerAtom;
template <typename A> class NonLazyPointerAtom;


// SectionInfo should be nested inside Writer, but I can't figure out how to make the type accessible to the Atom classes
class SectionInfo : public ObjectFile::Section {
public:
										SectionInfo() : fFileOffset(0), fSize(0), fRelocCount(0), fRelocOffset(0), fIndirectSymbolOffset(0),
														fAlignment(0), fAllLazyPointers(false), fAllNonLazyPointers(false), fAllStubs(false),
														fAllSelfModifyingStubs(false), fAllZeroFill(false), fVirtualSection(false)
														{ fSegmentName[0] = '\0'; fSectionName[0] = '\0'; }
	void								setIndex(unsigned int index) { fIndex=index; }
	std::vector<ObjectFile::Atom*>		fAtoms;
	char								fSegmentName[20];
	char								fSectionName[20];
	uint64_t							fFileOffset;
	uint64_t							fSize;
	uint32_t							fRelocCount;
	uint32_t							fRelocOffset;
	uint32_t							fIndirectSymbolOffset;
	uint8_t								fAlignment;
	bool								fAllLazyPointers;
	bool								fAllNonLazyPointers;
	bool								fAllStubs;
	bool								fAllSelfModifyingStubs;
	bool								fAllZeroFill;
	bool								fVirtualSection;
};

// SegmentInfo should be nested inside Writer, but I can't figure out how to make the type accessible to the Atom classes
class SegmentInfo
{
public:
										SegmentInfo() : fInitProtection(0), fMaxProtection(0), fFileOffset(0), fFileSize(0),
														fBaseAddress(0), fSize(0), fFixedAddress(false) { fName[0] = '\0'; }
	std::vector<class SectionInfo*>		fSections;
	char								fName[20];
	uint32_t							fInitProtection;
	uint32_t							fMaxProtection;
	uint64_t							fFileOffset;
	uint64_t							fFileSize;
	uint64_t							fBaseAddress;
	uint64_t							fSize;
	bool								fFixedAddress;
};

template <typename A>
class Writer : public ExecutableFile::Writer
{
public:
	Writer(const char* path, Options& options, std::vector<ExecutableFile::DyLibUsed>& dynamicLibraries);
	virtual						~Writer();

	virtual const char*								getPath()								{ return fFilePath; }
	virtual time_t									getModificationTime()					{ return 0; }
	virtual DebugInfoKind							getDebugInfoKind()						{ return ObjectFile::Reader::kDebugInfoNone; }
	virtual std::vector<class ObjectFile::Atom*>&	getAtoms()								{ return fWriterSynthesizedAtoms; }
	virtual std::vector<class ObjectFile::Atom*>*	getJustInTimeAtomsFor(const char* name)	{ return NULL; }
	virtual std::vector<Stab>*						getStabs()								{ return NULL; }

	virtual class ObjectFile::Atom*					getUndefinedProxyAtom(const char* name);
	virtual uint64_t								write(std::vector<class ObjectFile::Atom*>& atoms,
														  std::vector<class ObjectFile::Reader::Stab>& stabs,
														  class ObjectFile::Atom* entryPointAtom,
														  class ObjectFile::Atom* dyldHelperAtom,
														  bool createUUID);

private:
	typedef typename A::P			P;
	typedef typename A::P::uint_t	pint_t;

	enum RelocKind { kRelocNone, kRelocInternal, kRelocExternal };

	void						assignFileOffsets();
	void						synthesizeStubs();
	void						partitionIntoSections();
	bool						addBranchIslands();
	bool						addPPCBranchIslands();
	uint8_t						branch24Reference();
	void						adjustLoadCommandsAndPadding();
	void						createDynamicLinkerCommand();
	void						createDylibCommands();
	void						buildLinkEdit();
	uint64_t					writeAtoms();
	void						writeNoOps(uint32_t from, uint32_t to);
	void						collectExportedAndImportedAndLocalAtoms();
	void						setNlistRange(std::vector<class ObjectFile::Atom*>& atoms, uint32_t startIndex, uint32_t count);
	void						buildSymbolTable();
	void						setExportNlist(const ObjectFile::Atom* atom, macho_nlist<P>* entry);
	void						setImportNlist(const ObjectFile::Atom* atom, macho_nlist<P>* entry);
	void						setLocalNlist(const ObjectFile::Atom* atom, macho_nlist<P>* entry);
	uint64_t					getAtomLoadAddress(const ObjectFile::Atom* atom);
	uint8_t						ordinalForLibrary(ObjectFile::Reader* file);
	bool						shouldExport(const ObjectFile::Atom& atom) const;
	void						buildFixups();
	void						adjustLinkEditSections();
	void						buildObjectFileFixups();
	void						buildExecutableFixups();
	bool						referenceRequiresRuntimeFixUp(const ObjectFile::Reference* ref, bool slideable) const;
	void						fixUpReferenceFinal(const ObjectFile::Reference* ref, const ObjectFile::Atom* inAtom, uint8_t buffer[]) const;
	void						fixUpReferenceRelocatable(const ObjectFile::Reference* ref, const ObjectFile::Atom* inAtom, uint8_t buffer[]) const;
	void						fixUpReference_powerpc(const ObjectFile::Reference* ref, const ObjectFile::Atom* inAtom,
														uint8_t buffer[], bool finalLinkedImage) const;
	uint32_t					symbolIndex(ObjectFile::Atom& atom);
	uint32_t					addObjectRelocs(ObjectFile::Atom* atom, ObjectFile::Reference* ref);
	uint32_t					addObjectRelocs_powerpc(ObjectFile::Atom* atom, ObjectFile::Reference* ref);
	uint8_t						getRelocPointerSize();
	bool						stubableReferenceKind(uint8_t kind);
	bool						GOTReferenceKind(uint8_t kind);
	bool						weakImportReferenceKind(uint8_t kind);
	unsigned int				collectStabs();
	uint64_t					valueForStab(const ObjectFile::Reader::Stab& stab);
	uint32_t					stringOffsetForStab(const ObjectFile::Reader::Stab& stab);
	uint8_t						sectionIndexForStab(const ObjectFile::Reader::Stab& stab);
	void						addStabs(uint32_t startIndex);
	RelocKind					relocationNeededInFinalLinkedImage(const ObjectFile::Atom& target) const;
	bool						illegalRelocInFinalLinkedImage(uint8_t kind, bool slideable);


	struct DirectLibrary {
		class ObjectFile::Reader*	fLibrary;
		bool						fWeak;
		bool						fReExport;
	};

	friend class WriterAtom<A>;
	friend class PageZeroAtom<A>;
	friend class CustomStackAtom<A>;
	friend class MachHeaderAtom<A>;
	friend class SegmentLoadCommandsAtom<A>;
	friend class SymbolTableLoadCommandsAtom<A>;
	friend class ThreadsLoadCommandsAtom<A>;
	friend class DylibIDLoadCommandsAtom<A>;
	friend class RoutinesLoadCommandsAtom<A>;
	friend class DyldLoadCommandsAtom<A>;
	friend class UUIDLoadCommandAtom<A>;
	friend class LinkEditAtom<A>;
	friend class SectionRelocationsLinkEditAtom<A>;
	friend class LocalRelocationsLinkEditAtom<A>;
	friend class ExternalRelocationsLinkEditAtom<A>;
	friend class SymbolTableLinkEditAtom<A>;
//	friend class IndirectTableLinkEditAtom<A>;
	friend class StringsLinkEditAtom<A>;
	friend class LoadCommandsPaddingAtom<A>;
	friend class StubAtom<A>;
	friend class StubHelperAtom<A>;
	friend class LazyPointerAtom<A>;
	friend class NonLazyPointerAtom<A>;

	const char*										fFilePath;
	Options&										fOptions;
	int												fFileDescriptor;
	std::vector<class ObjectFile::Atom*>*			fAllAtoms;
	std::vector<class ObjectFile::Reader::Stab>*	fStabs;
	class SectionInfo*								fLoadCommandsSection;
	class SegmentInfo*								fLoadCommandsSegment;
	class SegmentLoadCommandsAtom<A>*				fSegmentCommands;
	class SymbolTableLoadCommandsAtom<A>*			fSymbolTableCommands;
	class LoadCommandsPaddingAtom<A>*				fHeaderPadding;
	class UUIDLoadCommandAtom<A>*				    fUUIDAtom;
	std::vector<class ObjectFile::Atom*>			fWriterSynthesizedAtoms;
	std::vector<SegmentInfo*>						fSegmentInfos;
	class ObjectFile::Atom*							fEntryPoint;
	class ObjectFile::Atom*							fDyldHelper;
	std::vector<DirectLibrary>						fDirectLibraries;
	std::map<class ObjectFile::Reader*, uint32_t>	fLibraryToOrdinal;
	std::vector<class ObjectFile::Atom*>			fExportedAtoms;
	std::vector<class ObjectFile::Atom*>			fImportedAtoms;
	std::vector<class ObjectFile::Atom*>			fLocalSymbolAtoms;
	class SectionRelocationsLinkEditAtom<A>*		fSectionRelocationsAtom;
	class LocalRelocationsLinkEditAtom<A>*			fLocalRelocationsAtom;
	class ExternalRelocationsLinkEditAtom<A>*		fExternalRelocationsAtom;
	class SymbolTableLinkEditAtom<A>*				fSymbolTableAtom;
	class IndirectTableLinkEditAtom<A>*				fIndirectTableAtom;
	class StringsLinkEditAtom<A>*					fStringsAtom;
	macho_nlist<P>*									fSymbolTable;
	std::vector<macho_relocation_info<P> >			fSectionRelocs;
	std::vector<macho_relocation_info<P> >			fInternalRelocs;
	std::vector<macho_relocation_info<P> >			fExternalRelocs;
	std::map<ObjectFile::Atom*,ObjectFile::Atom*>	fStubsMap;
	std::map<ObjectFile::Atom*,ObjectFile::Atom*>	fGOTMap;
	std::vector<class StubAtom<A>*>					fAllSynthesizedStubs;
	std::vector<ObjectFile::Atom*>					fAllSynthesizedStubHelpers;
	std::vector<class LazyPointerAtom<A>*>			fAllSynthesizedLazyPointers;
	std::vector<class NonLazyPointerAtom<A>*>		fAllSynthesizedNonLazyPointers;
	uint32_t										fSymbolTableCount;
	uint32_t										fSymbolTableStabsCount;
	uint32_t										fSymbolTableStabsStartIndex;
	uint32_t										fSymbolTableLocalCount;
	uint32_t										fSymbolTableLocalStartIndex;
	uint32_t										fSymbolTableExportCount;
	uint32_t										fSymbolTableExportStartIndex;
	uint32_t										fSymbolTableImportCount;
	uint32_t										fSymbolTableImportStartIndex;
	uint32_t										fLargestAtomSize;
	bool											fEmitVirtualSections;
	bool											fHasWeakExports;
	bool											fReferencesWeakImports;
	bool											fSeenFollowOnReferences;
	std::map<const ObjectFile::Atom*,bool>			fWeakImportMap;
};


class Segment : public ObjectFile::Segment
{
public:
								Segment(const char* name, bool readable, bool writable, bool executable, bool fixedAddress)
											 : fName(name), fReadable(readable), fWritable(writable), fExecutable(executable), fFixedAddress(fixedAddress) {}
	virtual const char*			getName() const					{ return fName; }
	virtual bool				isContentReadable() const		{ return fReadable; }
	virtual bool				isContentWritable() const		{ return fWritable; }
	virtual bool				isContentExecutable() const		{ return fExecutable; }
	virtual bool				hasFixedAddress() const			{ return fFixedAddress; }

	static Segment								fgTextSegment;
	static Segment								fgPageZeroSegment;
	static Segment								fgLinkEditSegment;
	static Segment								fgStackSegment;
	static Segment								fgImportSegment;
	static Segment								fgDataSegment;
private:
	const char*					fName;
	const bool					fReadable;
	const bool					fWritable;
	const bool					fExecutable;
	const bool					fFixedAddress;
};

Segment		Segment::fgPageZeroSegment("__PAGEZERO", false, false, false, true);
Segment		Segment::fgTextSegment("__TEXT", true, false, true, false);
Segment		Segment::fgLinkEditSegment("__LINKEDIT", true, false, false, false);
Segment		Segment::fgStackSegment("__UNIXSTACK", true, true, false, true);
Segment		Segment::fgImportSegment("__IMPORT", true, true, true, false);
Segment		Segment::fgDataSegment("__DATA", true, true, false, false);


template <typename A>
class WriterAtom : public ObjectFile::Atom
{
public:
	enum Kind { zeropage, machHeaderApp, machHeaderDylib, machHeaderBundle, machHeaderObject, loadCommands, undefinedProxy };
											WriterAtom(Writer<A>& writer, Segment& segment) : fWriter(writer), fSegment(segment) { setDontDeadStrip(); }

	virtual ObjectFile::Reader*				getFile() const					{ return &fWriter; }
	virtual bool							getTranslationUnitSource(const char** dir, const char** name) const { return false; }
	virtual const char*						getName() const					{ return NULL; }
	virtual const char*						getDisplayName() const			{ return this->getName(); }
	virtual Scope							getScope() const				{ return ObjectFile::Atom::scopeTranslationUnit; }
	virtual DefinitionKind					getDefinitionKind() const		{ return kRegularDefinition; }
	virtual SymbolTableInclusion			getSymbolTableInclusion() const	{ return ObjectFile::Atom::kSymbolTableNotIn; }
	virtual bool							isZeroFill() const				{ return false; }
	virtual std::vector<ObjectFile::Reference*>&  getReferences() const		{ return fgEmptyReferenceList; }
	virtual bool							mustRemainInSection() const		{ return true; }
	virtual ObjectFile::Segment&			getSegment() const				{ return fSegment; }
	virtual bool							requiresFollowOnAtom() const	{ return false; }
	virtual ObjectFile::Atom&				getFollowOnAtom() const			{ return *((ObjectFile::Atom*)NULL); }
	virtual std::vector<ObjectFile::LineInfo>*	getLineInfo() const			{ return NULL; }
	virtual uint8_t							getAlignment() const			{ return 2; }
	virtual void							copyRawContent(uint8_t buffer[]) const { throw "don't use copyRawContent"; }
	virtual void							setScope(Scope)					{ }


protected:
	virtual									~WriterAtom() {}
	typedef typename A::P					P;
	typedef typename A::P::E				E;

	static std::vector<ObjectFile::Reference*>	fgEmptyReferenceList;

	Writer<A>&									fWriter;
	Segment&									fSegment;
};

template <typename A> std::vector<ObjectFile::Reference*>	WriterAtom<A>::fgEmptyReferenceList;


template <typename A>
class PageZeroAtom : public WriterAtom<A>
{
public:
											PageZeroAtom(Writer<A>& writer) : WriterAtom<A>(writer, Segment::fgPageZeroSegment) {}
	virtual const char*						getDisplayName() const	{ return "page zero content"; }
	virtual bool							isZeroFill() const		{ return true; }
	virtual uint64_t						getSize() const 		{ return fWriter.fOptions.zeroPageSize(); }
	virtual const char*						getSectionName() const	{ return "._zeropage"; }
	virtual uint8_t							getAlignment() const	{ return 12; }
private:
	using WriterAtom<A>::fWriter;
	typedef typename A::P					P;
};

template <typename A>
class DsoHandleAtom : public WriterAtom<A>
{
public:
													DsoHandleAtom(Writer<A>& writer) : WriterAtom<A>(writer, Segment::fgTextSegment) {}
	virtual const char*								getName() const				{ return "___dso_handle"; }
	virtual ObjectFile::Atom::Scope					getScope() const			{ return ObjectFile::Atom::scopeLinkageUnit; }
	virtual ObjectFile::Atom::SymbolTableInclusion	getSymbolTableInclusion() const { return ObjectFile::Atom::kSymbolTableNotIn; }
	virtual uint64_t								getSize() const				{ return 0; }
	virtual uint8_t									getAlignment() const		{ return 12; }
	virtual const char*								getSectionName() const		{ return "._mach_header"; }
	virtual void									copyRawContent(uint8_t buffer[]) const {}
};


template <typename A>
class MachHeaderAtom : public WriterAtom<A>
{
public:
													MachHeaderAtom(Writer<A>& writer) : WriterAtom<A>(writer, Segment::fgTextSegment) {}
	virtual const char*								getName() const;
	virtual const char*								getDisplayName() const;
	virtual ObjectFile::Atom::Scope					getScope() const;
	virtual ObjectFile::Atom::SymbolTableInclusion	getSymbolTableInclusion() const;
	virtual uint64_t								getSize() const				{ return sizeof(macho_header<typename A::P>); }
	virtual uint8_t									getAlignment() const		{ return 12; }
	virtual const char*								getSectionName() const		{ return "._mach_header"; }
	virtual void									copyRawContent(uint8_t buffer[]) const;
private:
	using WriterAtom<A>::fWriter;
	typedef typename A::P					P;
	void									setHeaderInfo(macho_header<typename A::P>& header) const;
};

template <typename A>
class CustomStackAtom : public WriterAtom<A>
{
public:
											CustomStackAtom(Writer<A>& writer);
	virtual const char*						getDisplayName() const	{ return "custom stack content"; }
	virtual bool							isZeroFill() const		{ return true; }
	virtual uint64_t						getSize() const 		{ return fWriter.fOptions.customStackSize(); }
	virtual const char*						getSectionName() const	{ return "._stack"; }
	virtual uint8_t							getAlignment() const	{ return 12; }
private:
	using WriterAtom<A>::fWriter;
	typedef typename A::P					P;
	static bool								stackGrowsDown();
};

template <typename A>
class LoadCommandAtom : public WriterAtom<A>
{
protected:
											LoadCommandAtom(Writer<A>& writer, Segment& segment) : WriterAtom<A>(writer, segment) {}
	static uint64_t							alignedSize(uint64_t size);
};


template <typename A>
class SegmentLoadCommandsAtom : public LoadCommandAtom<A>
{
public:
											SegmentLoadCommandsAtom(Writer<A>& writer)  
												: LoadCommandAtom<A>(writer, Segment::fgTextSegment), fCommandCount(0), fSize(0) 
												{ writer.fSegmentCommands = this; }
	virtual const char*						getDisplayName() const	{ return "segment load commands"; }
	virtual uint64_t						getSize() const			{ return fSize; }
	virtual uint8_t							getAlignment() const	{ return 2; }
	virtual const char*						getSectionName() const	{ return "._load_commands"; }
	virtual void							copyRawContent(uint8_t buffer[]) const;

	void									computeSize();
	void									setup();
	unsigned int							commandCount()			{ return fCommandCount; }
	void									assignFileOffsets();
private:
	using WriterAtom<A>::fWriter;
	typedef typename A::P					P;
	unsigned int							fCommandCount;
	uint32_t								fSize;
};

template <typename A>
class SymbolTableLoadCommandsAtom : public LoadCommandAtom<A>
{
public:
											SymbolTableLoadCommandsAtom(Writer<A>&);
	virtual const char*						getDisplayName() const { return "symbol table load commands"; }
	virtual uint64_t						getSize() const;
	virtual uint8_t							getAlignment() const	{ return 2; }
	virtual const char*						getSectionName() const	{ return "._load_commands"; }
	virtual void							copyRawContent(uint8_t buffer[]) const;
	unsigned int							commandCount();

private:
	using WriterAtom<A>::fWriter;
	typedef typename A::P					P;
	macho_symtab_command<typename A::P>		fSymbolTable;
	macho_dysymtab_command<typename A::P>	fDynamicSymbolTable;
};

template <typename A>
class ThreadsLoadCommandsAtom : public LoadCommandAtom<A>
{
public:
											ThreadsLoadCommandsAtom(Writer<A>& writer) 
												: LoadCommandAtom<A>(writer, Segment::fgTextSegment) {}
	virtual const char*						getDisplayName() const { return "thread load commands"; }
	virtual uint64_t						getSize() const;
	virtual uint8_t							getAlignment() const	{ return 2; }
	virtual const char*						getSectionName() const	{ return "._load_commands"; }
	virtual void							copyRawContent(uint8_t buffer[]) const;
private:
	using WriterAtom<A>::fWriter;
	typedef typename A::P					P;
	uint8_t*								fBuffer;
	uint32_t								fBufferSize;
};

template <typename A>
class DyldLoadCommandsAtom : public LoadCommandAtom<A>
{
public:
											DyldLoadCommandsAtom(Writer<A>& writer)  : LoadCommandAtom<A>(writer, Segment::fgTextSegment) {}
	virtual const char*						getDisplayName() const	{ return "dyld load command"; }
	virtual uint64_t						getSize() const;
	virtual uint8_t							getAlignment() const	{ return 2; }
	virtual const char*						getSectionName() const	{ return "._load_commands"; }
	virtual void							copyRawContent(uint8_t buffer[]) const;
private:
	using WriterAtom<A>::fWriter;
	typedef typename A::P					P;
};

template <typename A>
class AllowableClientLoadCommandsAtom : public LoadCommandAtom<A>
{
public:
	AllowableClientLoadCommandsAtom(Writer<A>& writer, const char* client)  :
		LoadCommandAtom<A>(writer, Segment::fgTextSegment), clientString(client) {}
	virtual const char*							getDisplayName() const  { return "allowable_client load command"; }
	virtual uint64_t							getSize() const;
	virtual uint8_t								getAlignment() const    { return 2; }
	virtual const char*							getSectionName() const  { return "._load_commands"; }
	virtual void								copyRawContent(uint8_t buffer[]) const;
private:
	using WriterAtom<A>::fWriter;
	typedef typename A::P						P;
	const char*							   		clientString;
};

template <typename A>
class DylibLoadCommandsAtom : public LoadCommandAtom<A>
{
public:
											DylibLoadCommandsAtom(Writer<A>& writer, ExecutableFile::DyLibUsed& info) 
											 : LoadCommandAtom<A>(writer, Segment::fgTextSegment), fInfo(info) {}
	virtual const char*						getDisplayName() const	{ return "dylib load command"; }
	virtual uint64_t						getSize() const;
	virtual uint8_t							getAlignment() const	{ return 2; }
	virtual const char*						getSectionName() const	{ return "._load_commands"; }
	virtual void							copyRawContent(uint8_t buffer[]) const;
private:
	using WriterAtom<A>::fWriter;
	typedef typename A::P					P;
	ExecutableFile::DyLibUsed&				fInfo;
};

template <typename A>
class DylibIDLoadCommandsAtom : public LoadCommandAtom<A>
{
public:
											DylibIDLoadCommandsAtom(Writer<A>& writer) : LoadCommandAtom<A>(writer, Segment::fgTextSegment) {}
	virtual const char*						getDisplayName() const { return "dylib ID load command"; }
	virtual uint64_t						getSize() const;
	virtual uint8_t							getAlignment() const	{ return 2; }
	virtual const char*						getSectionName() const	{ return "._load_commands"; }
	virtual void							copyRawContent(uint8_t buffer[]) const;
private:
	using WriterAtom<A>::fWriter;
	typedef typename A::P					P;
};

template <typename A>
class RoutinesLoadCommandsAtom : public LoadCommandAtom<A>
{
public:
											RoutinesLoadCommandsAtom(Writer<A>& writer) : LoadCommandAtom<A>(writer, Segment::fgTextSegment) {}
	virtual const char*						getDisplayName() const { return "routines load command"; }
	virtual uint64_t						getSize() const			{ return sizeof(macho_routines_command<typename A::P>); }
	virtual uint8_t							getAlignment() const	{ return 2; }
	virtual const char*						getSectionName() const	{ return "._load_commands"; }
	virtual void							copyRawContent(uint8_t buffer[]) const;
private:
	using WriterAtom<A>::fWriter;
	typedef typename A::P					P;
};

template <typename A>
class SubUmbrellaLoadCommandsAtom : public LoadCommandAtom<A>
{
public:
											SubUmbrellaLoadCommandsAtom(Writer<A>& writer, const char* name) 
											 : LoadCommandAtom<A>(writer, Segment::fgTextSegment), fName(name) {}
	virtual const char*						getDisplayName() const	{ return "sub-umbrella load command"; }
	virtual uint64_t						getSize() const;
	virtual uint8_t							getAlignment() const	{ return 2; }
	virtual const char*						getSectionName() const	{ return "._load_commands"; }
	virtual void							copyRawContent(uint8_t buffer[]) const;
private:
	typedef typename A::P					P;
	const char*								fName;
};

template <typename A>
class SubLibraryLoadCommandsAtom : public LoadCommandAtom<A>
{
public:
											SubLibraryLoadCommandsAtom(Writer<A>& writer,  const char* nameStart, int nameLen)
												: LoadCommandAtom<A>(writer, Segment::fgTextSegment), fNameStart(nameStart), fNameLength(nameLen) {}
	virtual const char*						getDisplayName() const	{ return "sub-library load command"; }
	virtual uint64_t						getSize() const;
	virtual uint8_t							getAlignment() const	{ return 2; }
	virtual const char*						getSectionName() const	{ return "._load_commands"; }
	virtual void							copyRawContent(uint8_t buffer[]) const;
private:
	using WriterAtom<A>::fWriter;
	typedef typename A::P					P;
	const char*								fNameStart;
	int										fNameLength;
};

template <typename A>
class UmbrellaLoadCommandsAtom : public LoadCommandAtom<A>
{
public:
											UmbrellaLoadCommandsAtom(Writer<A>& writer, const char* name)
													: LoadCommandAtom<A>(writer, Segment::fgTextSegment), fName(name) {}
	virtual const char*						getDisplayName() const	{ return "umbrella load command"; }
	virtual uint64_t						getSize() const;
	virtual uint8_t							getAlignment() const	{ return 2; }
	virtual const char*						getSectionName() const	{ return "._load_commands"; }
	virtual void							copyRawContent(uint8_t buffer[]) const;
private:
	using WriterAtom<A>::fWriter;
	typedef typename A::P					P;
	const char*								fName;
};

template <typename A>
class UUIDLoadCommandAtom : public LoadCommandAtom<A>
{
public:
											UUIDLoadCommandAtom(Writer<A>& writer)
												: LoadCommandAtom<A>(writer, Segment::fgTextSegment), fEmit(false) { ::uuid_generate_random(fUUID);}
	virtual const char*						getDisplayName() const	{ return "uuid load command"; }
	virtual uint64_t						getSize() const			{ return fEmit ? sizeof(macho_uuid_command<typename A::P>) : 0; }
	virtual uint8_t							getAlignment() const	{ return 2; }
	virtual const char*						getSectionName() const	{ return "._load_commands"; }
	virtual void							copyRawContent(uint8_t buffer[]) const;
	virtual void						    emit()					{ fEmit = true; }
private:
	using WriterAtom<A>::fWriter;
	typedef typename A::P					P;
	uuid_t									fUUID;
	bool								    fEmit;
};

template <typename A>
class LoadCommandsPaddingAtom : public WriterAtom<A>
{
public:
											LoadCommandsPaddingAtom(Writer<A>& writer)
													: WriterAtom<A>(writer, Segment::fgTextSegment), fSize(0) {}
	virtual const char*						getDisplayName() const	{ return "header padding"; }
	virtual uint64_t						getSize() const			{ return fSize; }
	virtual uint8_t							getAlignment() const	{ return 2; }
	virtual const char*						getSectionName() const	{ return "._load_cmds_pad"; }
	virtual void							copyRawContent(uint8_t buffer[]) const;

	void									setSize(uint64_t newSize);
private:
	using WriterAtom<A>::fWriter;
	typedef typename A::P					P;
	uint64_t								fSize;
};

template <typename A>
class LinkEditAtom : public WriterAtom<A>
{
public:
											LinkEditAtom(Writer<A>& writer) : WriterAtom<A>(writer, Segment::fgLinkEditSegment) {}
	uint64_t								getFileOffset() const;
private:
	typedef typename A::P					P;
};

template <typename A>
class SectionRelocationsLinkEditAtom : public LinkEditAtom<A>
{
public:
											SectionRelocationsLinkEditAtom(Writer<A>& writer) : LinkEditAtom<A>(writer) { }
	virtual const char*						getDisplayName() const	{ return "section relocations"; }
	virtual uint64_t						getSize() const;
	virtual uint8_t							getAlignment() const	{ return 3; }
	virtual const char*						getSectionName() const	{ return "._section_relocs"; }
	virtual void							copyRawContent(uint8_t buffer[]) const;
private:
	using WriterAtom<A>::fWriter;
	typedef typename A::P					P;
};

template <typename A>
class LocalRelocationsLinkEditAtom : public LinkEditAtom<A>
{
public:
											LocalRelocationsLinkEditAtom(Writer<A>& writer) : LinkEditAtom<A>(writer) { }
	virtual const char*						getDisplayName() const	{ return "local relocations"; }
	virtual uint64_t						getSize() const;
	virtual uint8_t							getAlignment() const	{ return 3; }
	virtual const char*						getSectionName() const	{ return "._local_relocs"; }
	virtual void							copyRawContent(uint8_t buffer[]) const;
private:
	using WriterAtom<A>::fWriter;
	typedef typename A::P					P;
};

template <typename A>
class SymbolTableLinkEditAtom : public LinkEditAtom<A>
{
public:
											SymbolTableLinkEditAtom(Writer<A>& writer) : LinkEditAtom<A>(writer) { }
	virtual const char*						getDisplayName() const	{ return "symbol table"; }
	virtual uint64_t						getSize() const;
	virtual uint8_t							getAlignment() const	{ return 2; }
	virtual const char*						getSectionName() const	{ return "._symbol_table"; }
	virtual void							copyRawContent(uint8_t buffer[]) const;
private:
	using WriterAtom<A>::fWriter;
	typedef typename A::P					P;
};

template <typename A>
class ExternalRelocationsLinkEditAtom : public LinkEditAtom<A>
{
public:
											ExternalRelocationsLinkEditAtom(Writer<A>& writer) : LinkEditAtom<A>(writer) { }
	virtual const char*						getDisplayName() const	{ return "external relocations"; }
	virtual uint64_t						getSize() const;
	virtual uint8_t							getAlignment() const	{ return 3; }
	virtual const char*						getSectionName() const	{ return "._extern_relocs"; }
	virtual void							copyRawContent(uint8_t buffer[]) const;
private:
	using WriterAtom<A>::fWriter;
	typedef typename A::P					P;
};

struct IndirectEntry {
	uint32_t	indirectIndex;
	uint32_t	symbolIndex;
};

template <typename A>
class IndirectTableLinkEditAtom : public LinkEditAtom<A>
{
public:
											IndirectTableLinkEditAtom(Writer<A>& writer) : LinkEditAtom<A>(writer) { }
	virtual const char*						getDisplayName() const	{ return "indirect symbol table"; }
	virtual uint64_t						getSize() const;
	virtual uint8_t							getAlignment() const	{ return 2; }
	virtual const char*						getSectionName() const	{ return "._indirect_syms"; }
	virtual void							copyRawContent(uint8_t buffer[]) const;

	std::vector<IndirectEntry>				fTable;

private:
	using WriterAtom<A>::fWriter;
	typedef typename A::P					P;
};

class CStringEquals
{
public:
	bool operator()(const char* left, const char* right) const { return (strcmp(left, right) == 0); }
};

template <typename A>
class StringsLinkEditAtom : public LinkEditAtom<A>
{
public:
											StringsLinkEditAtom(Writer<A>& writer);
	virtual const char*						getDisplayName() const	{ return "string pool"; }
	virtual uint64_t						getSize() const;
	virtual uint8_t							getAlignment() const	{ return 2; }
	virtual const char*						getSectionName() const	{ return "._string_pool"; }
	virtual void							copyRawContent(uint8_t buffer[]) const;

	int32_t									add(const char* name);
	int32_t									addUnique(const char* name);
	int32_t									emptyString()			{ return 1; }

private:
	using WriterAtom<A>::fWriter;
	typedef typename A::P					P;
	enum { kBufferSize = 0x01000000 };
	class CStringComparor {
	public:
		bool operator()(const char* left, const char* right) const { return (strcmp(left, right) < 0); }
	};
	typedef __gnu_cxx::hash_map<const char*, int32_t, __gnu_cxx::hash<const char*>, CStringEquals> StringToOffset;

	std::vector<char*>						fFullBuffers;
	char*									fCurrentBuffer;
	uint32_t								fCurrentBufferUsed;
	StringToOffset							fUniqueStrings;
};



template <typename A>
class UndefinedSymbolProxyAtom : public WriterAtom<A>
{
public:
													UndefinedSymbolProxyAtom(Writer<A>& writer, const char* name) : WriterAtom<A>(writer, Segment::fgLinkEditSegment), fName(name) {}
	virtual const char*								getName() const				{ return fName; }
	virtual ObjectFile::Atom::Scope					getScope() const			{ return ObjectFile::Atom::scopeGlobal; }
	virtual ObjectFile::Atom::DefinitionKind		getDefinitionKind() const	{ return ObjectFile::Atom::kExternalDefinition; }
	virtual ObjectFile::Atom::SymbolTableInclusion	getSymbolTableInclusion() const	{ return ObjectFile::Atom::kSymbolTableIn; }
	virtual uint64_t								getSize() const				{ return 0; }
	virtual const char*								getSectionName() const		{ return "._imports"; }
private:
	using WriterAtom<A>::fWriter;
	typedef typename A::P					P;
	const char*								fName;
};

template <typename A>
class BranchIslandAtom : public WriterAtom<A>
{
public:
											BranchIslandAtom(Writer<A>& writer, const char* name, int islandRegion, ObjectFile::Atom& target, uint32_t targetOffset);
	virtual const char*						getName() const				{ return fName; }
	virtual ObjectFile::Atom::Scope			getScope() const			{ return ObjectFile::Atom::scopeLinkageUnit; }
	virtual uint64_t						getSize() const;
	virtual const char*						getSectionName() const		{ return "__text"; }
	virtual void							copyRawContent(uint8_t buffer[]) const;
private:
	using WriterAtom<A>::fWriter;
	const char*								fName;
	ObjectFile::Atom&						fTarget;
	uint32_t								fTargetOffset;
};

template <typename A>
class StubAtom : public WriterAtom<A>
{
public:
											StubAtom(Writer<A>& writer, ObjectFile::Atom& target);
	virtual const char*						getName() const				{ return fName; }
	virtual ObjectFile::Atom::Scope			getScope() const			{ return ObjectFile::Atom::scopeLinkageUnit; }
	virtual uint8_t							getAlignment() const		{ return 2; }
	virtual uint64_t						getSize() const;
	virtual const char*						getSectionName() const		{ return "__symbol_stub1"; }
	virtual std::vector<ObjectFile::Reference*>&  getReferences() const		{ return (std::vector<ObjectFile::Reference*>&)(fReferences); }
	virtual void							copyRawContent(uint8_t buffer[]) const;
	ObjectFile::Atom*						getTarget()					{ return &fTarget; }
private:
	static const char*						stubName(const char* importName);
	bool									pic() const;
	using WriterAtom<A>::fWriter;
	const char*								fName;
	ObjectFile::Atom&						fTarget;
	std::vector<ObjectFile::Reference*>		fReferences;
};


template <typename A>
class LazyPointerAtom : public WriterAtom<A>
{
public:
											LazyPointerAtom(Writer<A>& writer, ObjectFile::Atom& target);
	virtual const char*						getName() const				{ return fName; }
	virtual ObjectFile::Atom::Scope			getScope() const			{ return ObjectFile::Atom::scopeLinkageUnit; }
	virtual uint64_t						getSize() const				{ return sizeof(typename A::P::uint_t); }
	virtual const char*						getSectionName() const		{ return "__la_symbol_ptr"; }
	virtual std::vector<ObjectFile::Reference*>&  getReferences() const		{ return (std::vector<ObjectFile::Reference*>&)(fReferences); }
	virtual void							copyRawContent(uint8_t buffer[]) const;
	ObjectFile::Atom*						getTarget()					{ return &fTarget; }
private:
	using WriterAtom<A>::fWriter;
	static const char*						lazyPointerName(const char* importName);
	const char*								fName;
	ObjectFile::Atom&						fTarget;
	std::vector<ObjectFile::Reference*>		fReferences;
};


template <typename A>
class NonLazyPointerAtom : public WriterAtom<A>
{
public:
											NonLazyPointerAtom(Writer<A>& writer, ObjectFile::Atom& target);
	virtual const char*						getName() const				{ return fName; }
	virtual ObjectFile::Atom::Scope			getScope() const			{ return ObjectFile::Atom::scopeLinkageUnit; }
	virtual uint64_t						getSize() const				{ return sizeof(typename A::P::uint_t); }
	virtual const char*						getSectionName() const		{ return "__nl_symbol_ptr"; }
	virtual std::vector<ObjectFile::Reference*>&  getReferences() const		{ return (std::vector<ObjectFile::Reference*>&)(fReferences); }
	virtual void							copyRawContent(uint8_t buffer[]) const;
	ObjectFile::Atom*						getTarget()					{ return &fTarget; }
private:
	using WriterAtom<A>::fWriter;
	static const char*						nonlazyPointerName(const char* importName);
	const char*								fName;
	ObjectFile::Atom&						fTarget;
	std::vector<ObjectFile::Reference*>		fReferences;
};


template <typename A>
class WriterReference : public ObjectFile::Reference
{
public:
	typedef typename A::ReferenceKinds			Kinds;

							WriterReference(uint32_t offset, Kinds kind, ObjectFile::Atom* target,
											uint32_t toOffset=0, ObjectFile::Atom* fromTarget=NULL, uint32_t fromOffset=0)
										  : fKind(kind), fFixUpOffsetInSrc(offset), fTarget(target),
											fTargetOffset(toOffset), fFromTarget(fromTarget), fFromTargetOffset(fromOffset) {}

	virtual					~WriterReference() {}

	virtual bool			isTargetUnbound() const							{ return false; }
	virtual bool			isFromTargetUnbound() const						{ return false; }
	virtual uint8_t			getKind() const									{ return (uint8_t)fKind; }
	virtual uint64_t		getFixUpOffset() const							{ return fFixUpOffsetInSrc; }
	virtual const char*		getTargetName() const							{ return fTarget->getName(); }
	virtual ObjectFile::Atom& getTarget() const								{ return *fTarget; }
	virtual uint64_t		getTargetOffset() const							{ return fTargetOffset; }
	virtual bool			hasFromTarget() const							{ return (fFromTarget != NULL); }
	virtual ObjectFile::Atom& getFromTarget() const							{ return *fFromTarget; }
	virtual const char*		getFromTargetName() const						{ return fFromTarget->getName(); }
	virtual void			setTarget(ObjectFile::Atom& target, uint64_t offset)	{ fTarget = &target; fTargetOffset = offset; }
	virtual void			setFromTarget(ObjectFile::Atom& target)			{ fFromTarget = &target; }
	virtual void			setFromTargetName(const char* name)				{  }
	virtual void			setFromTargetOffset(uint64_t offset)			{ fFromTargetOffset = offset; }
	virtual const char*		getDescription() const							{ return "writer refrence"; }
	virtual uint64_t		getFromTargetOffset() const						{ return fFromTargetOffset; }

private:
	Kinds					fKind;
	uint32_t				fFixUpOffsetInSrc;
	ObjectFile::Atom*		fTarget;
	uint32_t				fTargetOffset;
	ObjectFile::Atom*		fFromTarget;
	uint32_t				fFromTargetOffset;
};



struct ExportSorter
{
     bool operator()(ObjectFile::Atom* left, ObjectFile::Atom* right)
     {
          return (strcmp(left->getName(), right->getName()) < 0);
     }
};




template <typename A>
Writer<A>::Writer(const char* path, Options& options, std::vector<ExecutableFile::DyLibUsed>& dynamicLibraries)
	: ExecutableFile::Writer(dynamicLibraries), fFilePath(strdup(path)), fOptions(options), fLoadCommandsSection(NULL),
	  fLoadCommandsSegment(NULL), fLargestAtomSize(1),
	  fEmitVirtualSections(false), fHasWeakExports(false), fReferencesWeakImports(false),
	  fSeenFollowOnReferences(false)
{
	int permissions = 0777;
	if ( fOptions.outputKind() == Options::kObjectFile )
		permissions = 0666;
	// Calling unlink first assures the file is gone so that open creates it with correct permissions
	// It also handles the case where fFilePath file is not writeable but its directory is
	// And it means we don't have to truncate the file when done writing (in case new is smaller than old)
	(void)unlink(fFilePath);
	fFileDescriptor = open(fFilePath, O_CREAT | O_WRONLY | O_TRUNC, permissions);
	if ( fFileDescriptor == -1 ) {
		throw "can't open file for writing";
	}

	switch ( fOptions.outputKind() ) {
		case Options::kDynamicExecutable:
		case Options::kStaticExecutable:
			fWriterSynthesizedAtoms.push_back(new PageZeroAtom<A>(*this));
			if ( fOptions.outputKind() == Options::kDynamicExecutable )
				fWriterSynthesizedAtoms.push_back(new DsoHandleAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(new MachHeaderAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(new SegmentLoadCommandsAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(new SymbolTableLoadCommandsAtom<A>(*this));
			if ( fOptions.outputKind() == Options::kDynamicExecutable )
				fWriterSynthesizedAtoms.push_back(new DyldLoadCommandsAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(fUUIDAtom = new UUIDLoadCommandAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(new ThreadsLoadCommandsAtom<A>(*this));
			if ( fOptions.hasCustomStack() )
				fWriterSynthesizedAtoms.push_back(new CustomStackAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(fHeaderPadding = new LoadCommandsPaddingAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(fSectionRelocationsAtom = new SectionRelocationsLinkEditAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(fLocalRelocationsAtom = new LocalRelocationsLinkEditAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(fSymbolTableAtom = new SymbolTableLinkEditAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(fExternalRelocationsAtom = new ExternalRelocationsLinkEditAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(fIndirectTableAtom = new IndirectTableLinkEditAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(fStringsAtom = new StringsLinkEditAtom<A>(*this));
			break;
		case Options::kDynamicLibrary:
		case Options::kDynamicBundle:
			fWriterSynthesizedAtoms.push_back(new DsoHandleAtom<A>(*this));
			// fall through
		case Options::kObjectFile:
			fWriterSynthesizedAtoms.push_back(new MachHeaderAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(new SegmentLoadCommandsAtom<A>(*this));
			if ( fOptions.outputKind() == Options::kDynamicLibrary ) {
				fWriterSynthesizedAtoms.push_back(new DylibIDLoadCommandsAtom<A>(*this));
				if ( fOptions.initFunctionName() != NULL )
					fWriterSynthesizedAtoms.push_back(new RoutinesLoadCommandsAtom<A>(*this));
			}
			fWriterSynthesizedAtoms.push_back(fUUIDAtom = new UUIDLoadCommandAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(new SymbolTableLoadCommandsAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(fHeaderPadding = new LoadCommandsPaddingAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(fSectionRelocationsAtom = new SectionRelocationsLinkEditAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(fLocalRelocationsAtom = new LocalRelocationsLinkEditAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(fSymbolTableAtom = new SymbolTableLinkEditAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(fExternalRelocationsAtom = new ExternalRelocationsLinkEditAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(fIndirectTableAtom = new IndirectTableLinkEditAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(fStringsAtom = new StringsLinkEditAtom<A>(*this));
			break;
		case Options::kDyld:
			fWriterSynthesizedAtoms.push_back(new DsoHandleAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(new MachHeaderAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(new SegmentLoadCommandsAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(new SymbolTableLoadCommandsAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(new DyldLoadCommandsAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(fUUIDAtom = new UUIDLoadCommandAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(new ThreadsLoadCommandsAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(fHeaderPadding = new LoadCommandsPaddingAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(fLocalRelocationsAtom = new LocalRelocationsLinkEditAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(fSymbolTableAtom = new SymbolTableLinkEditAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(fExternalRelocationsAtom = new ExternalRelocationsLinkEditAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(fIndirectTableAtom = new IndirectTableLinkEditAtom<A>(*this));
			fWriterSynthesizedAtoms.push_back(fStringsAtom = new StringsLinkEditAtom<A>(*this));
			break;
	}

	// add extra commmands
	uint8_t ordinal = 1;
	switch ( fOptions.outputKind() ) {
		case Options::kDynamicExecutable:
		case Options::kDynamicLibrary:
		case Options::kDynamicBundle:
			{
				// add dylib load command atoms for all dynamic libraries
				const unsigned int libCount = dynamicLibraries.size();
				for (unsigned int i=0; i < libCount; ++i) {
					ExecutableFile::DyLibUsed& dylibInfo = dynamicLibraries[i];
					if ( dylibInfo.indirect ) {
						// find ordinal of direct reader
						if ( fOptions.nameSpace() == Options::kTwoLevelNameSpace ) {
							bool found = false;
							for (std::map<class ObjectFile::Reader*, uint32_t>::iterator it = fLibraryToOrdinal.begin(); it != fLibraryToOrdinal.end(); ++it) {
								if ( it->first == dylibInfo.directReader ) {
									//fprintf(stderr, "ordinal %d for indirect %s\n", it->second, dylibInfo.reader->getPath());
									fLibraryToOrdinal[dylibInfo.reader] = it->second;
									found = true;
									break;
								}
							}
							if ( ! found )
								fprintf(stderr, "ld64 warning: ordinal not found for %s, parent %s\n", dylibInfo.reader->getPath(), dylibInfo.directReader != NULL ? dylibInfo.directReader->getPath() : NULL);
						}
					}
					else {
						// see if a DylibLoadCommandsAtom has already been created for this install path
						bool newDylib = true;
						const char* dylibInstallPath = dylibInfo.reader->getInstallPath();
						if ( dylibInfo.options.fInstallPathOverride != NULL )
							dylibInstallPath = dylibInfo.options.fInstallPathOverride;
						for (unsigned int seenLib=0; seenLib < i; ++seenLib) {
							ExecutableFile::DyLibUsed& seenDylibInfo = dynamicLibraries[seenLib];
							if ( !seenDylibInfo.indirect ) {
								const char* seenDylibInstallPath = seenDylibInfo.reader->getInstallPath();
								if ( seenDylibInfo.options.fInstallPathOverride != NULL )
									seenDylibInstallPath = dylibInfo.options.fInstallPathOverride;
								if ( strcmp(seenDylibInstallPath, dylibInstallPath) == 0 ) {
									fLibraryToOrdinal[dylibInfo.reader] = fLibraryToOrdinal[seenDylibInfo.reader];
									newDylib = false;
									break;
								}
							}
						}

						if ( newDylib ) {
							// assign new ordinal and check for other paired load commands
							fLibraryToOrdinal[dylibInfo.reader] = ordinal++;
							fWriterSynthesizedAtoms.push_back(new DylibLoadCommandsAtom<A>(*this, dylibInfo));
							if ( dylibInfo.options.fReExport ) {
								// this dylib also needs a sub_x load command
								bool isFrameworkReExport = false;
								const char* lastSlash = strrchr(dylibInstallPath, '/');
								if ( lastSlash != NULL ) {
									char frameworkName[strlen(lastSlash)+20];
									sprintf(frameworkName, "/%s.framework/", &lastSlash[1]);
									isFrameworkReExport = (strstr(dylibInstallPath, frameworkName) != NULL);
								}
								if ( isFrameworkReExport ) {
									// needs a LC_SUB_UMBRELLA command
									fWriterSynthesizedAtoms.push_back(new SubUmbrellaLoadCommandsAtom<A>(*this, &lastSlash[1]));
								}
								else {
									// needs a LC_SUB_LIBRARY command
									const char* nameStart = &lastSlash[1];
									if ( lastSlash == NULL )
										nameStart = dylibInstallPath;
									int len = strlen(nameStart);
									const char* dot = strchr(nameStart, '.');
									if ( dot != NULL )
										len = dot - nameStart;
									fWriterSynthesizedAtoms.push_back(new SubLibraryLoadCommandsAtom<A>(*this, nameStart, len));
								}
							}
						}
					}
				}
				// add umbrella command if needed
				if ( fOptions.umbrellaName() != NULL ) {
					fWriterSynthesizedAtoms.push_back(new UmbrellaLoadCommandsAtom<A>(*this, fOptions.umbrellaName()));
				}
				std::vector<const char*>& allowableClients = fOptions.allowableClients();
				if ( allowableClients.size() != 0 ) {
					for (std::vector<const char*>::iterator it=allowableClients.begin();
						 it != allowableClients.end();
						 it++)
						fWriterSynthesizedAtoms.push_back(new AllowableClientLoadCommandsAtom<A>(*this, *it));
				}
			}
			break;
		case Options::kStaticExecutable:
		case Options::kObjectFile:
		case Options::kDyld:
			break;
	}

	//fprintf(stderr, "ordinals table:\n");
	//for (std::map<class ObjectFile::Reader*, uint32_t>::iterator it = fLibraryToOrdinal.begin(); it != fLibraryToOrdinal.end(); ++it) {
	//	fprintf(stderr, "%d <== %s\n", it->second, it->first->getPath());
	//}
}

template <typename A>
Writer<A>::~Writer()
{
	if ( fFilePath != NULL )
		free((void*)fFilePath);
	if ( fSymbolTable != NULL )
		delete [] fSymbolTable;
}


template <typename A>
ObjectFile::Atom* Writer<A>::getUndefinedProxyAtom(const char* name)
{
	if ( (fOptions.outputKind() == Options::kObjectFile)
		|| (fOptions.undefinedTreatment() != Options::kUndefinedError) )
		return new UndefinedSymbolProxyAtom<A>(*this, name);
	else
		return NULL;
}

template <typename A>
uint8_t Writer<A>::ordinalForLibrary(ObjectFile::Reader* lib)
{
	// flat namespace images use zero for all ordinals
	if (  fOptions.nameSpace() != Options::kTwoLevelNameSpace )
		return 0;

	// is an UndefinedSymbolProxyAtom
	if ( lib == this )
		if ( fOptions.nameSpace() == Options::kTwoLevelNameSpace )
			return DYNAMIC_LOOKUP_ORDINAL;

	std::map<class ObjectFile::Reader*, uint32_t>::iterator pos = fLibraryToOrdinal.find(lib);
	if ( pos != fLibraryToOrdinal.end() )
		return pos->second;

	throw "can't find ordinal for imported symbol";
}


template <typename A>
uint64_t Writer<A>::write(std::vector<class ObjectFile::Atom*>& atoms,
						  std::vector<class ObjectFile::Reader::Stab>& stabs,
						  class ObjectFile::Atom* entryPointAtom, class ObjectFile::Atom* dyldHelperAtom,
						  bool createUUID)
{
	fAllAtoms =  &atoms;
	fStabs =  &stabs;
	fEntryPoint = entryPointAtom;
	fDyldHelper = dyldHelperAtom;

	// Set for create UUID
	if (createUUID)
		fUUIDAtom->emit();

	// create inter-library stubs
	synthesizeStubs();

	// create SegmentInfo and SectionInfo objects and assign all atoms to a section
	partitionIntoSections();

	// segment load command can now be sized and padding can be set
	adjustLoadCommandsAndPadding();

	// assign each section a file offset
	assignFileOffsets();

	// if need to add branch islands, reassign file offsets
	if ( addBranchIslands() )
		assignFileOffsets();

	// build symbol table and relocations
	buildLinkEdit();

	// write everything
	return writeAtoms();
}

template <typename A>
void Writer<A>::buildLinkEdit()
{
	this->collectExportedAndImportedAndLocalAtoms();
	this->buildSymbolTable();
	this->buildFixups();
	this->adjustLinkEditSections();
}



template <typename A>
uint64_t Writer<A>::getAtomLoadAddress(const ObjectFile::Atom* atom)
{
	return atom->getAddress();
//	SectionInfo* info = (SectionInfo*)atom->getSection();
//	return info->getBaseAddress() + atom->getSectionOffset();
}

template <typename A>
void Writer<A>::setExportNlist(const ObjectFile::Atom* atom, macho_nlist<P>* entry)
{
	// set n_type
	entry->set_n_type(N_EXT | N_SECT);
	if ( (atom->getScope() == ObjectFile::Atom::scopeLinkageUnit) && (fOptions.outputKind() == Options::kObjectFile) ) {
		if ( fOptions.keepPrivateExterns() )
			entry->set_n_type(N_EXT | N_SECT | N_PEXT);
	}

	// set n_sect (section number of implementation )
	uint8_t sectionIndex = atom->getSection()->getIndex();
	entry->set_n_sect(sectionIndex);

	// the __mh_execute_header is magic and must be an absolute symbol
	if ( (fOptions.outputKind() == Options::kDynamicExecutable) && (sectionIndex==0)
		&& (atom->getSymbolTableInclusion() == ObjectFile::Atom::kSymbolTableInAndNeverStrip ))
		entry->set_n_type(N_EXT | N_ABS);

	// set n_desc
	uint16_t desc = 0;
	if ( atom->getSymbolTableInclusion() == ObjectFile::Atom::kSymbolTableInAndNeverStrip )
		desc |= REFERENCED_DYNAMICALLY;
	if ( atom->getDefinitionKind() == ObjectFile::Atom::kWeakDefinition ) {
		desc |= N_WEAK_DEF;
		fHasWeakExports = true;
	}
	entry->set_n_desc(desc);

	// set n_value ( address this symbol will be at if this executable is loaded at it preferred address )
	entry->set_n_value(this->getAtomLoadAddress(atom));
}

template <typename A>
void Writer<A>::setImportNlist(const ObjectFile::Atom* atom, macho_nlist<P>* entry)
{
	// set n_type
	entry->set_n_type(N_UNDF | N_EXT);
	if ( fOptions.keepPrivateExterns()
		&& (atom->getScope() == ObjectFile::Atom::scopeLinkageUnit)
		&& (fOptions.outputKind() == Options::kObjectFile) )
		entry->set_n_type(N_UNDF | N_EXT | N_PEXT);

	// set n_sect
	entry->set_n_sect(0);

	uint16_t desc = 0;
	if ( fOptions.outputKind() != Options::kObjectFile ) {
		// set n_desc ( high byte is library ordinal, low byte is reference type )
		desc = REFERENCE_FLAG_UNDEFINED_LAZY; // FIXME
		try {
			uint8_t ordinal = this->ordinalForLibrary(atom->getFile());
			SET_LIBRARY_ORDINAL(desc, ordinal);
		}
		catch (const char* msg) {
			throwf("%s %s from %s", msg, atom->getDisplayName(), atom->getFile()->getPath());
		}
	}
	if ( atom->getSymbolTableInclusion() == ObjectFile::Atom::kSymbolTableInAndNeverStrip )
		desc |= REFERENCED_DYNAMICALLY;
	if ( ( fOptions.outputKind() != Options::kObjectFile) && (atom->getDefinitionKind() == ObjectFile::Atom::kExternalWeakDefinition) ) {
		desc |= N_REF_TO_WEAK;
		fReferencesWeakImports = true;
	}
	// set weak_import attribute
	if ( fWeakImportMap[atom] )
		desc |= N_WEAK_REF;
	entry->set_n_desc(desc);

	// set n_value, zero for import proxy and size for tentative definition
	entry->set_n_value(atom->getSize());
}

template <typename A>
void Writer<A>::setLocalNlist(const ObjectFile::Atom* atom, macho_nlist<P>* entry)
{
	// set n_type
	uint8_t type = N_SECT;
	if ( atom->getScope() == ObjectFile::Atom::scopeLinkageUnit )
		type |= N_PEXT;
	entry->set_n_type(type);

	// set n_sect (section number of implementation )
	uint8_t sectIndex = atom->getSection()->getIndex();
	if ( sectIndex == 0 ) {
		// see <mach-o/ldsyms.h> synthesized lable for mach_header needs special section number...
		if ( strcmp(atom->getSectionName(), "._mach_header") == 0 )
			sectIndex = 1;
	}
	entry->set_n_sect(sectIndex);

	// set n_desc
	uint16_t desc = 0;
	if ( atom->getDefinitionKind() == ObjectFile::Atom::kWeakDefinition )
		desc |= N_WEAK_DEF;
	entry->set_n_desc(desc);

	// set n_value ( address this symbol will be at if this executable is loaded at it preferred address )
	entry->set_n_value(this->getAtomLoadAddress(atom));
}


template <typename A>
void Writer<A>::setNlistRange(std::vector<class ObjectFile::Atom*>& atoms, uint32_t startIndex, uint32_t count)
{
	macho_nlist<P>* entry = &fSymbolTable[startIndex];
	for (uint32_t i=0; i < count; ++i, ++entry) {
		ObjectFile::Atom* atom = atoms[i];
		entry->set_n_strx(this->fStringsAtom->add(atom->getName()));
		if ( &atoms == &fExportedAtoms ) {
			this->setExportNlist(atom, entry);
		}
		else if ( &atoms == &fImportedAtoms ) {
			this->setImportNlist(atom, entry);
		}
		else {
			this->setLocalNlist(atom, entry);
		}
	}
}

template <typename A>
void Writer<A>::buildSymbolTable()
{
	fSymbolTableStabsStartIndex		= 0;
	fSymbolTableStabsCount			= fStabs->size();
	fSymbolTableLocalStartIndex		= fSymbolTableStabsStartIndex + fSymbolTableStabsCount;
	fSymbolTableLocalCount			= fLocalSymbolAtoms.size();
	fSymbolTableExportStartIndex	= fSymbolTableLocalStartIndex + fSymbolTableLocalCount;
	fSymbolTableExportCount			= fExportedAtoms.size();
	fSymbolTableImportStartIndex	= fSymbolTableExportStartIndex + fSymbolTableExportCount;
	fSymbolTableImportCount			= fImportedAtoms.size();

	// allocate symbol table
	fSymbolTableCount = fSymbolTableStabsCount + fSymbolTableLocalCount + fSymbolTableExportCount + fSymbolTableImportCount;
	fSymbolTable = new macho_nlist<P>[fSymbolTableCount];

	// fill in symbol table and string pool (do stabs last so strings are at end of pool)
	setNlistRange(fLocalSymbolAtoms, fSymbolTableLocalStartIndex,  fSymbolTableLocalCount);
	setNlistRange(fExportedAtoms,    fSymbolTableExportStartIndex, fSymbolTableExportCount);
	setNlistRange(fImportedAtoms,    fSymbolTableImportStartIndex, fSymbolTableImportCount);
	addStabs(fSymbolTableStabsStartIndex);
}



template <typename A>
bool Writer<A>::shouldExport(const ObjectFile::Atom& atom) const
{
	if ( atom.getSymbolTableInclusion() == ObjectFile::Atom::kSymbolTableNotIn )
		return false;
	switch ( atom.getScope() ) {
		case ObjectFile::Atom::scopeGlobal:
			return true;
		case ObjectFile::Atom::scopeLinkageUnit:
			return ( (fOptions.outputKind() == Options::kObjectFile) && fOptions.keepPrivateExterns() );
		default:
			return false;
	}
}

template <typename A>
void Writer<A>::collectExportedAndImportedAndLocalAtoms()
{
	const int atomCount = fAllAtoms->size();
	// guess at sizes of each bucket to minimize re-allocations
	fImportedAtoms.reserve(100);
	fExportedAtoms.reserve(atomCount/2);
	fLocalSymbolAtoms.reserve(atomCount);
	for (int i=0; i < atomCount; ++i) {
		ObjectFile::Atom* atom = (*fAllAtoms)[i];
		// only named atoms go in symbol table
		if ( atom->getName() != NULL ) {
			// put atom into correct bucket: imports, exports, locals
			//printf("collectExportedAndImportedAndLocalAtoms() name=%s\n", atom->getDisplayName());
			switch ( atom->getDefinitionKind() ) {
				case ObjectFile::Atom::kExternalDefinition:
				case ObjectFile::Atom::kExternalWeakDefinition:
					fImportedAtoms.push_back(atom);
					break;
				case ObjectFile::Atom::kTentativeDefinition:
					if ( fOptions.outputKind() == Options::kObjectFile ) {
						fImportedAtoms.push_back(atom);
						break;
					}
					// else fall into
				case ObjectFile::Atom::kRegularDefinition:
				case ObjectFile::Atom::kWeakDefinition:
					if ( this->shouldExport(*atom) )
						fExportedAtoms.push_back(atom);
					else if ( !fOptions.stripLocalSymbols()
						&& (atom->getSymbolTableInclusion() != ObjectFile::Atom::kSymbolTableNotIn) )
						fLocalSymbolAtoms.push_back(atom);
					break;
			}
		}
	}

	// sort exported atoms by name
	std::sort(fExportedAtoms.begin(), fExportedAtoms.end(), ExportSorter());
	// sort imported atoms by name (not required by runtime, but helps make generated files binary diffable)
	std::sort(fImportedAtoms.begin(), fImportedAtoms.end(), ExportSorter());
}


template <typename A>
uint64_t Writer<A>::valueForStab(const ObjectFile::Reader::Stab& stab)
{
	switch ( stab.type ) {
		case N_FUN:
			if ( stab.other == 0 ) {
				// end of function N_FUN has size
				return stab.atom->getSize();
			}
			else {
				// start of function N_FUN has address
				return getAtomLoadAddress(stab.atom);
			}
		case N_LBRAC:
		case N_RBRAC:
		case N_SLINE:
			if ( stab.atom == NULL )
				// some weird assembly files have slines not associated with a function
				return stab.value;
			else
				// all these stab types need their value changed from an offset in the atom to an address
				return getAtomLoadAddress(stab.atom) + stab.value;
		case N_STSYM:
		case N_LCSYM:
		case N_BNSYM:
			// all these need address of atom
			return getAtomLoadAddress(stab.atom);;
		case N_ENSYM:
			return stab.atom->getSize();
		case N_SO:
			return 0;
		default:
			return stab.value;
	}
}

template <typename A>
uint32_t Writer<A>::stringOffsetForStab(const ObjectFile::Reader::Stab& stab)
{
	switch (stab.type) {
		case N_SO:
			if ( (stab.string == NULL) || stab.string[0] == '\0' ) {
				return this->fStringsAtom->emptyString();
				break;
			}
			// fall into uniquing case
		case N_SOL:
		case N_BINCL:
		case N_EXCL:
			return this->fStringsAtom->addUnique(stab.string);
			break;
		default:
			if ( stab.string == NULL )
				return 0;
			else if ( stab.string[0] == '\0' )
				return this->fStringsAtom->emptyString();
			else
				return this->fStringsAtom->add(stab.string);
	}
	return 0;
}

template <typename A>
uint8_t Writer<A>::sectionIndexForStab(const ObjectFile::Reader::Stab& stab)
{
	if ( stab.atom != NULL ) 
		return stab.atom->getSection()->getIndex();
	else
		return stab.other;
}

template <typename A>
void Writer<A>::addStabs(uint32_t startIndex)
{
	macho_nlist<P>* entry = &fSymbolTable[startIndex];
	for(std::vector<ObjectFile::Reader::Stab>::iterator it = fStabs->begin(); it != fStabs->end(); ++it, ++entry) {
		const ObjectFile::Reader::Stab& stab = *it;
		entry->set_n_type(stab.type);
		entry->set_n_sect(sectionIndexForStab(stab));
		entry->set_n_desc(stab.desc);
		entry->set_n_value(valueForStab(stab));
		entry->set_n_strx(stringOffsetForStab(stab));
	}
}



template <typename A>
uint32_t Writer<A>::symbolIndex(ObjectFile::Atom& atom)
{
	// search imports
	int i = 0;
	for(std::vector<ObjectFile::Atom*>::iterator it=fImportedAtoms.begin(); it != fImportedAtoms.end(); ++it) {
		if ( &atom == *it )
			return i + fSymbolTableImportStartIndex;
		++i;
	}

	// search locals
	i = 0;
	for(std::vector<ObjectFile::Atom*>::iterator it=fLocalSymbolAtoms.begin(); it != fLocalSymbolAtoms.end(); ++it) {
		if ( &atom == *it )
			return i + fSymbolTableLocalStartIndex;
		++i;
	}

	// search exports
	i = 0;
	for(std::vector<ObjectFile::Atom*>::iterator it=fExportedAtoms.begin(); it != fExportedAtoms.end(); ++it) {
		if ( &atom == *it )
			return i + fSymbolTableExportStartIndex;
		++i;
	}

	throwf("atom not found in symbolIndex(%s) for %s", atom.getDisplayName(), atom.getFile()->getPath());
}


template <typename A>
void Writer<A>::buildFixups()
{
	if ( fOptions.outputKind() == Options::kObjectFile ) {
		this->buildObjectFileFixups();
	}
	else {
		if ( fOptions.keepRelocations() )
			this->buildObjectFileFixups();
		this->buildExecutableFixups();
	}
}


template <>
uint32_t Writer<x86>::addObjectRelocs(ObjectFile::Atom* atom, ObjectFile::Reference* ref)
{
	ObjectFile::Atom& target = ref->getTarget();
	bool isExtern = false;
	switch ( target.getDefinitionKind() ) {
		case ObjectFile::Atom::kRegularDefinition:
			isExtern = false;
			break;
		case ObjectFile::Atom::kWeakDefinition:
		case ObjectFile::Atom::kTentativeDefinition:
		case ObjectFile::Atom::kExternalDefinition:
		case ObjectFile::Atom::kExternalWeakDefinition:
			isExtern = shouldExport(target);
			break;
	}
	uint32_t symbolIndex = 0;
	if ( isExtern )
		symbolIndex = this->symbolIndex(target);
	uint32_t sectionNum = target.getSection()->getIndex();
	uint32_t address = atom->getSectionOffset()+ref->getFixUpOffset();
	macho_relocation_info<P> reloc1;
	macho_relocation_info<P> reloc2;
	macho_scattered_relocation_info<P>* sreloc1 = (macho_scattered_relocation_info<P>*)&reloc1;
	macho_scattered_relocation_info<P>* sreloc2 = (macho_scattered_relocation_info<P>*)&reloc2;
	x86::ReferenceKinds kind = (x86::ReferenceKinds)ref->getKind();

	switch ( kind ) {
		case x86::kNoFixUp:
		case x86::kFollowOn:
			return 0;

		case x86::kPointer:
		case x86::kPointerWeakImport:
			if ( !isExtern && (ref->getTargetOffset() != 0) ) {
				// use scattered reloc is target offset is non-zero
				sreloc1->set_r_scattered(true);
				sreloc1->set_r_pcrel(false);
				sreloc1->set_r_length(2);
				sreloc1->set_r_type(GENERIC_RELOC_VANILLA);
				sreloc1->set_r_address(address);
				sreloc1->set_r_value(target.getAddress());
			}
			else {
				reloc1.set_r_address(address);
				reloc1.set_r_symbolnum(isExtern ? symbolIndex : sectionNum);
				reloc1.set_r_pcrel(false);
				reloc1.set_r_length(2);
				reloc1.set_r_extern(isExtern);
				reloc1.set_r_type(GENERIC_RELOC_VANILLA);
			}
			fSectionRelocs.insert(fSectionRelocs.begin(), reloc1);
			return 1;

		case x86::kPointerDiff:
			{
				pint_t fromAddr = ref->getFromTarget().getAddress() + ref->getFromTargetOffset();
				sreloc1->set_r_scattered(true);
				sreloc1->set_r_pcrel(false);
				sreloc1->set_r_length(2);
				if ( ref->getTarget().getScope() == ObjectFile::Atom::scopeTranslationUnit )
					sreloc1->set_r_type(GENERIC_RELOC_LOCAL_SECTDIFF);
				else
					sreloc1->set_r_type(GENERIC_RELOC_SECTDIFF);
				sreloc1->set_r_address(address);
				sreloc1->set_r_value(target.getAddress());
				sreloc2->set_r_scattered(true);
				sreloc2->set_r_pcrel(false);
				sreloc2->set_r_length(2);
				sreloc2->set_r_type(PPC_RELOC_PAIR);
				sreloc2->set_r_address(0);
				sreloc2->set_r_value(fromAddr);
				fSectionRelocs.insert(fSectionRelocs.begin(), reloc2);
				fSectionRelocs.insert(fSectionRelocs.begin(), reloc1);
				return 2;
			}

		case x86::kPCRel32WeakImport:
		case x86::kPCRel32:
			if ( !isExtern && (ref->getTargetOffset() != 0) ) {
				// use scattered reloc is target offset is non-zero
				sreloc1->set_r_scattered(true);
				sreloc1->set_r_pcrel(true);
				sreloc1->set_r_length(2);
				sreloc1->set_r_type(GENERIC_RELOC_VANILLA);
				sreloc1->set_r_address(address);
				sreloc1->set_r_value(target.getAddress());
			}
			else {
				reloc1.set_r_address(address);
				reloc1.set_r_symbolnum(isExtern ? symbolIndex : sectionNum);
				reloc1.set_r_pcrel(true);
				reloc1.set_r_length(2);
				reloc1.set_r_extern(isExtern);
				reloc1.set_r_type(GENERIC_RELOC_VANILLA);
			}
			fSectionRelocs.insert(fSectionRelocs.begin(), reloc1);
			return 1;

	}
	return 0;
}



template <>
uint8_t Writer<ppc>::getRelocPointerSize()
{
	return 2;
}

template <>
uint8_t Writer<ppc64>::getRelocPointerSize()
{
	return 3;
}

template <>
uint32_t Writer<ppc>::addObjectRelocs(ObjectFile::Atom* atom, ObjectFile::Reference* ref)
{
	return addObjectRelocs_powerpc(atom, ref);
}

template <>
uint32_t Writer<ppc64>::addObjectRelocs(ObjectFile::Atom* atom, ObjectFile::Reference* ref)
{
	return addObjectRelocs_powerpc(atom, ref);
}

//
// addObjectRelocs<ppc> and addObjectRelocs<ppc64> are almost exactly the same, so
// they use a common addObjectRelocs_powerpc() method.
//
template <typename A>
uint32_t Writer<A>::addObjectRelocs_powerpc(ObjectFile::Atom* atom, ObjectFile::Reference* ref)
{
	ObjectFile::Atom& target = ref->getTarget();
	bool isExtern = false;
	switch ( target.getDefinitionKind() ) {
		case ObjectFile::Atom::kRegularDefinition:
			isExtern = false;
			break;
		case ObjectFile::Atom::kWeakDefinition:
		case ObjectFile::Atom::kTentativeDefinition:
		case ObjectFile::Atom::kExternalDefinition:
		case ObjectFile::Atom::kExternalWeakDefinition:
			isExtern = shouldExport(target);
			break;
	}
	
	uint32_t symbolIndex = 0;
	if ( isExtern )
		symbolIndex = this->symbolIndex(target);
	uint32_t sectionNum = target.getSection()->getIndex();
	uint32_t address = atom->getSectionOffset()+ref->getFixUpOffset();
	macho_relocation_info<P> reloc1;
	macho_relocation_info<P> reloc2;
	macho_scattered_relocation_info<P>* sreloc1 = (macho_scattered_relocation_info<P>*)&reloc1;
	macho_scattered_relocation_info<P>* sreloc2 = (macho_scattered_relocation_info<P>*)&reloc2;
	typename A::ReferenceKinds kind = (typename A::ReferenceKinds)ref->getKind();

	switch ( kind ) {
		case A::kNoFixUp:
		case A::kFollowOn:
			return 0;

		case A::kPointer:
		case A::kPointerWeakImport:
			if ( !isExtern && (ref->getTargetOffset() >= target.getSize()) ) {
				// use scattered reloc is target offset is outside target
				sreloc1->set_r_scattered(true);
				sreloc1->set_r_pcrel(false);
				sreloc1->set_r_length(getRelocPointerSize());
				sreloc1->set_r_type(GENERIC_RELOC_VANILLA);
				sreloc1->set_r_address(address);
				sreloc1->set_r_value(target.getAddress());
			}
			else {
				reloc1.set_r_address(address);
				if ( isExtern )
					reloc1.set_r_symbolnum(symbolIndex);
				else
					reloc1.set_r_symbolnum(sectionNum);
				reloc1.set_r_pcrel(false);
				reloc1.set_r_length(getRelocPointerSize());
				reloc1.set_r_extern(isExtern);
				reloc1.set_r_type(GENERIC_RELOC_VANILLA);
			}
			fSectionRelocs.insert(fSectionRelocs.begin(), reloc1);
			return 1;

		case A::kPointerDiff32:
		case A::kPointerDiff64:
			{
				pint_t toAddr = target.getAddress() + ref->getTargetOffset();
				pint_t fromAddr = ref->getFromTarget().getAddress() + ref->getFromTargetOffset();
				sreloc1->set_r_scattered(true);
				sreloc1->set_r_pcrel(false);
				sreloc1->set_r_length( (kind == A::kPointerDiff32) ? 2 : 3);
				sreloc1->set_r_type(ref->getTargetOffset() != 0 ? PPC_RELOC_LOCAL_SECTDIFF : PPC_RELOC_SECTDIFF);
				sreloc1->set_r_address(address);
				sreloc1->set_r_value(toAddr);
				sreloc2->set_r_scattered(true);
				sreloc2->set_r_pcrel(false);
				sreloc2->set_r_length( (kind == A::kPointerDiff32) ? 2 : 3);
				sreloc2->set_r_type(PPC_RELOC_PAIR);
				sreloc2->set_r_address(0);
				sreloc2->set_r_value(fromAddr);
				fSectionRelocs.insert(fSectionRelocs.begin(), reloc2);
				fSectionRelocs.insert(fSectionRelocs.begin(), reloc1);
				return 2;
			}

		case A::kBranch24WeakImport:
		case A::kBranch24:
			if ( (ref->getTargetOffset() == 0) || isExtern ) {
				reloc1.set_r_address(address);
				if ( isExtern )
					reloc1.set_r_symbolnum(symbolIndex);
				else
					reloc1.set_r_symbolnum(sectionNum);
				reloc1.set_r_pcrel(true);
				reloc1.set_r_length(2);
				reloc1.set_r_type(PPC_RELOC_BR24);
				reloc1.set_r_extern(isExtern);
			}
			else {
				sreloc1->set_r_scattered(true);
				sreloc1->set_r_pcrel(true);
				sreloc1->set_r_length(2);
				sreloc1->set_r_type(PPC_RELOC_BR24);
				sreloc1->set_r_address(address);
				sreloc1->set_r_value(target.getAddress());
			}
			fSectionRelocs.insert(fSectionRelocs.begin(), reloc1);
			return 1;

		case A::kBranch14:
			if ( (ref->getTargetOffset() == 0) || isExtern ) {
				reloc1.set_r_address(address);
				if ( isExtern )
					reloc1.set_r_symbolnum(symbolIndex);
				else
					reloc1.set_r_symbolnum(sectionNum);
				reloc1.set_r_pcrel(true);
				reloc1.set_r_length(2);
				reloc1.set_r_type(PPC_RELOC_BR14);
				reloc1.set_r_extern(isExtern);
			}
			else {
				sreloc1->set_r_scattered(true);
				sreloc1->set_r_pcrel(true);
				sreloc1->set_r_length(2);
				sreloc1->set_r_type(PPC_RELOC_BR14);
				sreloc1->set_r_address(address);
				sreloc1->set_r_value(target.getAddress());
			}
			fSectionRelocs.insert(fSectionRelocs.begin(), reloc1);
			return 1;

		case A::kPICBaseLow16:
		case A::kPICBaseLow14:
			{
				pint_t fromAddr = atom->getAddress() + ref->getFromTargetOffset();
				pint_t toAddr = target.getAddress() + ref->getTargetOffset();
				sreloc1->set_r_scattered(true);
				sreloc1->set_r_pcrel(false);
				sreloc1->set_r_length(2);
				sreloc1->set_r_type(kind == A::kPICBaseLow16 ? PPC_RELOC_LO16_SECTDIFF : PPC_RELOC_LO14_SECTDIFF);
				sreloc1->set_r_address(address);
				sreloc1->set_r_value(target.getAddress());
				sreloc2->set_r_scattered(true);
				sreloc2->set_r_pcrel(false);
				sreloc2->set_r_length(2);
				sreloc2->set_r_type(PPC_RELOC_PAIR);
				sreloc2->set_r_address(((toAddr-fromAddr) >> 16));
				sreloc2->set_r_value(fromAddr);
				fSectionRelocs.insert(fSectionRelocs.begin(), reloc2);
				fSectionRelocs.insert(fSectionRelocs.begin(), reloc1);
				return 2;
			}

		case A::kPICBaseHigh16:
			{
				pint_t fromAddr = atom->getAddress() + ref->getFromTargetOffset();
				pint_t toAddr = target.getAddress() + ref->getTargetOffset();
				sreloc1->set_r_scattered(true);
				sreloc1->set_r_pcrel(false);
				sreloc1->set_r_length(2);
				sreloc1->set_r_type(PPC_RELOC_HA16_SECTDIFF);
				sreloc1->set_r_address(address);
				sreloc1->set_r_value(target.getAddress());
				sreloc2->set_r_scattered(true);
				sreloc2->set_r_pcrel(false);
				sreloc2->set_r_length(2);
				sreloc2->set_r_type(PPC_RELOC_PAIR);
				sreloc2->set_r_address((toAddr-fromAddr) & 0xFFFF);
				sreloc2->set_r_value(fromAddr);
				fSectionRelocs.insert(fSectionRelocs.begin(), reloc2);
				fSectionRelocs.insert(fSectionRelocs.begin(), reloc1);
				return 2;
			}

		case A::kAbsLow14:
		case A::kAbsLow16:
			{
				pint_t toAddr = target.getAddress() + ref->getTargetOffset();
				if ( (ref->getTargetOffset() == 0) || isExtern ) {
					reloc1.set_r_address(address);
					if ( isExtern )
						reloc1.set_r_symbolnum(symbolIndex);
					else
						reloc1.set_r_symbolnum(sectionNum);
					reloc1.set_r_pcrel(false);
					reloc1.set_r_length(2);
					reloc1.set_r_extern(isExtern);
					reloc1.set_r_type(kind==A::kAbsLow16 ? PPC_RELOC_LO16 : PPC_RELOC_LO14);
				}
				else {
					sreloc1->set_r_scattered(true);
					sreloc1->set_r_pcrel(false);
					sreloc1->set_r_length(2);
					sreloc1->set_r_type(kind==A::kAbsLow16 ? PPC_RELOC_LO16 : PPC_RELOC_LO14);
					sreloc1->set_r_address(address);
					sreloc1->set_r_value(target.getAddress());
				}
				if ( isExtern )
					reloc2.set_r_address(ref->getTargetOffset() >> 16);
				else
					reloc2.set_r_address(toAddr >> 16);
				reloc2.set_r_symbolnum(0);
				reloc2.set_r_pcrel(false);
				reloc2.set_r_length(2);
				reloc2.set_r_extern(false);
				reloc2.set_r_type(PPC_RELOC_PAIR);
				fSectionRelocs.insert(fSectionRelocs.begin(), reloc2);
				fSectionRelocs.insert(fSectionRelocs.begin(), reloc1);
				return 2;
			}

		case A::kAbsHigh16:
			{
				pint_t toAddr = target.getAddress() + ref->getTargetOffset();
				if ( (ref->getTargetOffset() == 0) || isExtern ) {
					reloc1.set_r_address(address);
					if ( isExtern )
						reloc1.set_r_symbolnum(symbolIndex);
					else
						reloc1.set_r_symbolnum(sectionNum);
					reloc1.set_r_pcrel(false);
					reloc1.set_r_length(2);
					reloc1.set_r_extern(isExtern);
					reloc1.set_r_type(PPC_RELOC_HI16);
				}
				else {
					sreloc1->set_r_scattered(true);
					sreloc1->set_r_pcrel(false);
					sreloc1->set_r_length(2);
					sreloc1->set_r_type(PPC_RELOC_HI16);
					sreloc1->set_r_address(address);
					sreloc1->set_r_value(target.getAddress());
				}
				if ( isExtern )
					reloc2.set_r_address(ref->getTargetOffset() & 0xFFFF);
				else
					reloc2.set_r_address(toAddr & 0xFFFF);
				reloc2.set_r_symbolnum(0);
				reloc2.set_r_pcrel(false);
				reloc2.set_r_length(2);
				reloc2.set_r_extern(false);
				reloc2.set_r_type(PPC_RELOC_PAIR);
				fSectionRelocs.insert(fSectionRelocs.begin(), reloc2);
				fSectionRelocs.insert(fSectionRelocs.begin(), reloc1);
				return 2;
			}

		case A::kAbsHigh16AddLow:
			{
				pint_t toAddr = target.getAddress() + ref->getTargetOffset();
				uint32_t overflow = 0;
				if ( (toAddr & 0x00008000) != 0 )
					overflow = 0x10000;
				if ( (ref->getTargetOffset() == 0) || isExtern ) {
					reloc1.set_r_address(address);
					if ( isExtern )
						reloc1.set_r_symbolnum(symbolIndex);
					else
						reloc1.set_r_symbolnum(sectionNum);
					reloc1.set_r_pcrel(false);
					reloc1.set_r_length(2);
					reloc1.set_r_extern(isExtern);
					reloc1.set_r_type(PPC_RELOC_HA16);
				}
				else {
					sreloc1->set_r_scattered(true);
					sreloc1->set_r_pcrel(false);
					sreloc1->set_r_length(2);
					sreloc1->set_r_type(PPC_RELOC_HA16);
					sreloc1->set_r_address(address);
					sreloc1->set_r_value(target.getAddress());
				}
				if ( isExtern )
					reloc2.set_r_address(ref->getTargetOffset() & 0xFFFF);
				else
					reloc2.set_r_address(toAddr & 0xFFFF);
				reloc2.set_r_symbolnum(0);
				reloc2.set_r_pcrel(false);
				reloc2.set_r_length(2);
				reloc2.set_r_extern(false);
				reloc2.set_r_type(PPC_RELOC_PAIR);
				fSectionRelocs.insert(fSectionRelocs.begin(), reloc2);
				fSectionRelocs.insert(fSectionRelocs.begin(), reloc1);
				return 2;
			}

	}
	return 0;
}


template <typename A>
void Writer<A>::buildObjectFileFixups()
{
	uint32_t relocIndex = 0;
	std::vector<SegmentInfo*>& segmentInfos = fSegmentInfos;
	const int segCount = segmentInfos.size();
	for(int i=0; i < segCount; ++i) {
		SegmentInfo* curSegment = segmentInfos[i];
		std::vector<SectionInfo*>& sectionInfos = curSegment->fSections;
		const int sectionCount = sectionInfos.size();
		for(int j=0; j < sectionCount; ++j) {
			SectionInfo* curSection = sectionInfos[j];
			//fprintf(stderr, "buildObjectFileFixups(): starting section %s\n", curSection->fSectionName);
			std::vector<ObjectFile::Atom*>& sectionAtoms = curSection->fAtoms;
			if ( ! curSection->fAllZeroFill ) {
				if ( curSection->fAllNonLazyPointers || curSection->fAllLazyPointers || curSection->fAllStubs )
					curSection->fIndirectSymbolOffset = fIndirectTableAtom->fTable.size();
				curSection->fRelocOffset = relocIndex;
				const int atomCount = sectionAtoms.size();
				for (int k=0; k < atomCount; ++k) {
					ObjectFile::Atom* atom = sectionAtoms[k];
					//fprintf(stderr, "buildObjectFileFixups(): atom %s\n", atom->getDisplayName());
					std::vector<ObjectFile::Reference*>& refs = atom->getReferences();
					const int refCount = refs.size();
					for (int l=0; l < refCount; ++l) {
						ObjectFile::Reference* ref = refs[l];
						if ( ref->getKind() == A::kFollowOn )
							fSeenFollowOnReferences = true;
						if ( curSection->fAllNonLazyPointers || curSection->fAllLazyPointers || curSection->fAllStubs ) {
							uint32_t offsetInSection = atom->getSectionOffset();
							uint32_t indexInSection = offsetInSection / atom->getSize();
							uint32_t undefinedSymbolIndex;
							if ( curSection->fAllStubs ) {
								ObjectFile::Atom& stubTarget =ref->getTarget();
								ObjectFile::Atom& stubTargetTarget = stubTarget.getReferences()[0]->getTarget();
								undefinedSymbolIndex = this->symbolIndex(stubTargetTarget);
								//fprintf(stderr, "stub %s ==> %s ==> %s ==> index:%u\n", atom->getDisplayName(), stubTarget.getDisplayName(), stubTargetTarget.getDisplayName(), undefinedSymbolIndex);
							}
							else {
								if ( curSection->fAllNonLazyPointers
									 && (ref->getTarget().getScope() == ObjectFile::Atom::scopeLinkageUnit)
									 && !fOptions.keepPrivateExterns() )
									undefinedSymbolIndex = INDIRECT_SYMBOL_LOCAL;
								else
									undefinedSymbolIndex = this->symbolIndex(ref->getTarget());
							}
							uint32_t indirectTableIndex = indexInSection + curSection->fIndirectSymbolOffset;
							IndirectEntry entry = { indirectTableIndex, undefinedSymbolIndex };
							//printf("fIndirectTableAtom->fTable.add(sectionIndex=%u, indirectTableIndex=%u => %u), size=%lld\n", indexInSection, indirectTableIndex, undefinedSymbolIndex, atom->getSize());
							fIndirectTableAtom->fTable.push_back(entry);
							if ( curSection->fAllLazyPointers ) {
								ObjectFile::Atom& target = ref->getTarget();
								ObjectFile::Atom& fromTarget = ref->getFromTarget();
								if ( &fromTarget == NULL ) {
									fprintf(stderr, "lazy pointer %s missing initial binding\n", atom->getDisplayName());
								}
								else {
									bool isExtern = ( ((target.getDefinitionKind() == ObjectFile::Atom::kExternalDefinition)
										|| (target.getDefinitionKind() == ObjectFile::Atom::kExternalWeakDefinition))
										&& (target.getSymbolTableInclusion() != ObjectFile::Atom::kSymbolTableNotIn) );
									macho_relocation_info<P> reloc1;
									reloc1.set_r_address(atom->getSectionOffset());
									reloc1.set_r_symbolnum(isExtern ? this->symbolIndex(target) : target.getSection()->getIndex());
									reloc1.set_r_pcrel(false);
									reloc1.set_r_length();
									reloc1.set_r_extern(isExtern);
									reloc1.set_r_type(GENERIC_RELOC_VANILLA);
									fSectionRelocs.insert(fSectionRelocs.begin(), reloc1);
									++relocIndex;
								}
							}
							else if ( curSection->fAllStubs ) {
								relocIndex += this->addObjectRelocs(atom, ref);
							}
						}
						else {
							relocIndex += this->addObjectRelocs(atom, ref);
						}
					}
				}
				curSection->fRelocCount = relocIndex - curSection->fRelocOffset;
			}
		}
	}

	// now reverse reloc entries
	for(int i=0; i < segCount; ++i) {
		SegmentInfo* curSegment = segmentInfos[i];
		std::vector<SectionInfo*>& sectionInfos = curSegment->fSections;
		const int sectionCount = sectionInfos.size();
		for(int j=0; j < sectionCount; ++j) {
			SectionInfo* curSection = sectionInfos[j];
			curSection->fRelocOffset = relocIndex - curSection->fRelocOffset - curSection->fRelocCount;
		}
	}

}

template <>
bool Writer<ppc>::illegalRelocInFinalLinkedImage(uint8_t kind, bool slideable)
{
	switch ( kind ) {
		case ppc::kAbsLow16:
		case ppc::kAbsLow14:
		case ppc::kAbsHigh16:
		case ppc::kAbsHigh16AddLow:
			if ( slideable )
				return true;
	}
	return false;
}


template <>
bool Writer<ppc64>::illegalRelocInFinalLinkedImage(uint8_t kind, bool slideable)
{
	switch ( kind ) {
		case ppc::kAbsLow16:
		case ppc::kAbsLow14:
		case ppc::kAbsHigh16:
		case ppc::kAbsHigh16AddLow:
			if ( slideable )
				return true;
	}
	return false;
}

template <>
bool Writer<x86>::illegalRelocInFinalLinkedImage(uint8_t kind, bool slideable)
{
	return false;
}



template <typename A>
typename Writer<A>::RelocKind Writer<A>::relocationNeededInFinalLinkedImage(const ObjectFile::Atom& target) const
{
	const bool slideable = (fOptions.outputKind() != Options::kDynamicExecutable) && (fOptions.outputKind() != Options::kStaticExecutable);

	switch ( target.getDefinitionKind() ) {
		case ObjectFile::Atom::kTentativeDefinition:
		case ObjectFile::Atom::kRegularDefinition:
			// for flat-namespace or interposable two-level-namespace
			// all references to exported symbols get indirected
			if ( this->shouldExport(target) &&
				((fOptions.nameSpace() == Options::kFlatNameSpace)
			  || (fOptions.nameSpace() == Options::kForceFlatNameSpace)
			  || fOptions.interposable()) )
				return kRelocExternal;
			else if ( slideable )
				return kRelocInternal;
			else
				return kRelocNone;
		case ObjectFile::Atom::kWeakDefinition:
			// all calls to global weak definitions get indirected
			if ( this->shouldExport(target) )
				return kRelocExternal;
			else if ( slideable )
				return kRelocInternal;
			else
				return kRelocNone;
		case ObjectFile::Atom::kExternalDefinition:
		case ObjectFile::Atom::kExternalWeakDefinition:
			return kRelocExternal;
	}
	return kRelocNone;
}

template <typename A>
void Writer<A>::buildExecutableFixups()
{
	const bool slideable = (fOptions.outputKind() != Options::kDynamicExecutable) && (fOptions.outputKind() != Options::kStaticExecutable);
	fIndirectTableAtom->fTable.reserve(50);  // minimize reallocations
	std::vector<SegmentInfo*>& segmentInfos = fSegmentInfos;
	const int segCount = segmentInfos.size();
	for(int i=0; i < segCount; ++i) {
		SegmentInfo* curSegment = segmentInfos[i];
		std::vector<SectionInfo*>& sectionInfos = curSegment->fSections;
		const int sectionCount = sectionInfos.size();
		for(int j=0; j < sectionCount; ++j) {
			SectionInfo* curSection = sectionInfos[j];
			//fprintf(stderr, "starting section %p\n", curSection->fSectionName);
			std::vector<ObjectFile::Atom*>& sectionAtoms = curSection->fAtoms;
			if ( ! curSection->fAllZeroFill ) {
				if ( curSection->fAllNonLazyPointers || curSection->fAllLazyPointers || curSection->fAllStubs || curSection->fAllSelfModifyingStubs )
					curSection->fIndirectSymbolOffset = fIndirectTableAtom->fTable.size();
				const int atomCount = sectionAtoms.size();
				for (int k=0; k < atomCount; ++k) {
					ObjectFile::Atom* atom = sectionAtoms[k];
					std::vector<ObjectFile::Reference*>& refs = atom->getReferences();
					const int refCount = refs.size();
					//fprintf(stderr, "atom %s has %d references in section %s, %p\n", atom->getDisplayName(), refCount, curSection->fSectionName, atom->getSection());
					for (int l=0; l < refCount; ++l) {
						ObjectFile::Reference* ref = refs[l];
						if ( curSection->fAllNonLazyPointers || curSection->fAllLazyPointers ) {
							// if atom is in (non)lazy_pointer section, this is encoded as an indirect symbol
							if ( atom->getSize() != sizeof(pint_t) ) {
								printf("wrong size pointer atom %s from file %s\n", atom->getDisplayName(), atom->getFile()->getPath());
							}
							ObjectFile::Atom* pointerTarget = &(ref->getTarget());
							if ( curSection->fAllLazyPointers ) {
								pointerTarget = ((LazyPointerAtom<A>*)atom)->getTarget();
							}
							uint32_t offsetInSection = atom->getSectionOffset();
							uint32_t indexInSection = offsetInSection / sizeof(pint_t);
							uint32_t undefinedSymbolIndex = INDIRECT_SYMBOL_LOCAL;
							if ( this->relocationNeededInFinalLinkedImage(*pointerTarget) == kRelocExternal )
								undefinedSymbolIndex = this->symbolIndex(*pointerTarget);
							uint32_t indirectTableIndex = indexInSection + curSection->fIndirectSymbolOffset;
							IndirectEntry entry = { indirectTableIndex, undefinedSymbolIndex };
							//fprintf(stderr,"fIndirectTableAtom->fTable.add(%d-%d => 0x%X-%s), size=%lld\n", indexInSection, indirectTableIndex, undefinedSymbolIndex, ref->getTarget().getName(), atom->getSize());
							fIndirectTableAtom->fTable.push_back(entry);
							if ( slideable && curSection->fAllLazyPointers ) {
								// if this is a dylib/bundle, need vanilla internal relocation to fix up binding handler if image slides
								macho_relocation_info<P> pblaReloc;
								uint32_t sectionNum = 1;
								if ( fDyldHelper != NULL )
									sectionNum = ((SectionInfo*)(fDyldHelper->getSection()))->getIndex();
								//fprintf(stderr, "lazy pointer reloc, section index=%u, section name=%s\n", sectionNum, curSection->fSectionName);
								pblaReloc.set_r_address(atom->getAddress()-fOptions.baseAddress());
								pblaReloc.set_r_symbolnum(sectionNum);
								pblaReloc.set_r_pcrel(false);
								pblaReloc.set_r_length();
								pblaReloc.set_r_extern(false);
								pblaReloc.set_r_type(GENERIC_RELOC_VANILLA);
								fInternalRelocs.push_back(pblaReloc);
							}
						}
 						else if ( ref->getKind() == A::kPointer ) {
							if ( slideable && ((curSegment->fInitProtection & VM_PROT_WRITE) == 0) ) {
								throwf("pointer in read-only segment not allowed in slidable image, used in %s from %s", atom->getDisplayName(), atom->getFile()->getPath());
							}
							switch ( this->relocationNeededInFinalLinkedImage(ref->getTarget()) ) {
								case kRelocNone:
									// no reloc needed
									break;
								case kRelocInternal:
									{
										macho_relocation_info<P> internalReloc;
										SectionInfo* sectInfo = (SectionInfo*)ref->getTarget().getSection();
										uint32_t sectionNum = sectInfo->getIndex();
										// special case _mh_dylib_header and friends which are not in any real section
										if ( (sectionNum ==0) && sectInfo->fVirtualSection && (strcmp(sectInfo->fSectionName, "._mach_header") == 0) )
											sectionNum = 1;
										internalReloc.set_r_address(atom->getAddress()+ref->getFixUpOffset()-fOptions.baseAddress());
										internalReloc.set_r_symbolnum(sectionNum);
										internalReloc.set_r_pcrel(false);
										internalReloc.set_r_length();
										internalReloc.set_r_extern(false);
										internalReloc.set_r_type(GENERIC_RELOC_VANILLA);
										fInternalRelocs.push_back(internalReloc);
									}
									break;
								case kRelocExternal:
									{
										macho_relocation_info<P> externalReloc;
										externalReloc.set_r_address(atom->getAddress()+ref->getFixUpOffset()-fOptions.baseAddress());
										externalReloc.set_r_symbolnum(this->symbolIndex(ref->getTarget()));
										externalReloc.set_r_pcrel(false);
										externalReloc.set_r_length();
										externalReloc.set_r_extern(true);
										externalReloc.set_r_type(GENERIC_RELOC_VANILLA);
										fExternalRelocs.push_back(externalReloc);
									}
									break;
							}
						}
						else if ( this->illegalRelocInFinalLinkedImage(ref->getKind(), slideable) ) {
							throwf("absolute addressing (perhaps -mdynamic-no-pic) used in %s from %s not allowed in slidable image", atom->getDisplayName(), atom->getFile()->getPath());
						}
					}
					if ( curSection->fAllSelfModifyingStubs || curSection->fAllStubs ) {
						ObjectFile::Atom* stubTarget = ((StubAtom<A>*)atom)->getTarget();
						uint32_t undefinedSymbolIndex = this->symbolIndex(*stubTarget);
						uint32_t offsetInSection = atom->getSectionOffset();
						uint32_t indexInSection = offsetInSection / atom->getSize();
						uint32_t indirectTableIndex = indexInSection + curSection->fIndirectSymbolOffset;
						IndirectEntry entry = { indirectTableIndex, undefinedSymbolIndex };
						//fprintf(stderr,"for stub: fIndirectTableAtom->fTable.add(%d-%d => 0x%X-%s), size=%lld\n", indexInSection, indirectTableIndex, undefinedSymbolIndex, stubTarget->getName(), atom->getSize());
						fIndirectTableAtom->fTable.push_back(entry);
					}
				}
			}
		}
	}
}


template <>
void Writer<ppc>::writeNoOps(uint32_t from, uint32_t to)
{
	uint32_t ppcNop;
	OSWriteBigInt32(&ppcNop, 0, 0x60000000);
	for (uint32_t p=from; p < to; p += 4)
		::pwrite(fFileDescriptor, &ppcNop, 4, p);
}

template <>
void Writer<ppc64>::writeNoOps(uint32_t from, uint32_t to)
{
	uint32_t ppcNop;
	OSWriteBigInt32(&ppcNop, 0, 0x60000000);
	for (uint32_t p=from; p < to; p += 4)
		::pwrite(fFileDescriptor, &ppcNop, 4, p);
}

template <>
void Writer<x86>::writeNoOps(uint32_t from, uint32_t to)
{
	uint8_t x86Nop = 0x90;
	for (uint32_t p=from; p < to; ++p)
		::pwrite(fFileDescriptor, &x86Nop, 1, p);
}


template <typename A>
uint64_t Writer<A>::writeAtoms()
{
	uint32_t end = 0;
	uint8_t* buffer = new uint8_t[(fLargestAtomSize+4095) & (-4096)];
	std::vector<SegmentInfo*>& segmentInfos = fSegmentInfos;
	const int segCount = segmentInfos.size();
	for(int i=0; i < segCount; ++i) {
		SegmentInfo* curSegment = segmentInfos[i];
		bool isTextSeg = ((curSegment->fInitProtection & VM_PROT_EXECUTE) != 0);
		std::vector<SectionInfo*>& sectionInfos = curSegment->fSections;
		const int sectionCount = sectionInfos.size();
		for(int j=0; j < sectionCount; ++j) {
			SectionInfo* curSection = sectionInfos[j];
			std::vector<ObjectFile::Atom*>& sectionAtoms = curSection->fAtoms;
			//printf("writing with max atom size 0x%X\n", fLargestAtomSize);
			//fprintf(stderr, "writing %d atoms for section %s\n", (int)sectionAtoms.size(), curSection->fSectionName);
			if ( ! curSection->fAllZeroFill ) {
				const int atomCount = sectionAtoms.size();
				end = curSection->fFileOffset;
				bool needsNops = isTextSeg && (strcmp(curSection->fSectionName, "__cstring") != 0);
				for (int k=0; k < atomCount; ++k) {
					ObjectFile::Atom* atom = sectionAtoms[k];
					if ( (atom->getDefinitionKind() != ObjectFile::Atom::kExternalDefinition)
					  && (atom->getDefinitionKind() != ObjectFile::Atom::kExternalWeakDefinition) ) {
						uint32_t offset = curSection->fFileOffset + atom->getSectionOffset();
						if ( offset != end ) {
							if ( needsNops ) {
								// fill gaps with no-ops
								writeNoOps(end, offset);
							}
							else {
								// zero fill gaps
								if ( (offset-end) == 4 ) {
									uint32_t zero = 0;
									::pwrite(fFileDescriptor, &zero, 4, end);
								}
								else {
									uint8_t zero = 0x00;
									for (uint32_t p=end; p < offset; ++p)
										::pwrite(fFileDescriptor, &zero, 1, p);
								}
							}
						}
						uint64_t atomSize = atom->getSize();
						if ( atomSize > fLargestAtomSize ) {
							throwf("ld64 internal error: atom \"%s\"is larger than expected 0x%X > 0x%llX", 
								atom->getDisplayName(), atomSize, fLargestAtomSize);
						}
						end = offset+atomSize;
						// copy raw bytes
						atom->copyRawContent(buffer);
						// apply any fix-ups
						try {
							std::vector<ObjectFile::Reference*>&  references = atom->getReferences();
							for (std::vector<ObjectFile::Reference*>::iterator it=references.begin(); it != references.end(); it++) {
								ObjectFile::Reference* ref = *it;
								if ( fOptions.outputKind() == Options::kObjectFile ) {
									// doing ld -r
									// skip fix-ups for undefined targets
									if ( &(ref->getTarget()) != NULL )
										this->fixUpReferenceRelocatable(ref, atom, buffer);
								}
								else {
									// producing final linked image
									this->fixUpReferenceFinal(ref, atom, buffer);
								}
							}
						}
						catch (const char* msg) {
							throwf("%s in %s from %s", msg, atom->getDisplayName(), atom->getFile()->getPath());
						}
						//fprintf(stderr, "writing 0x%08X -> 0x%08X (addr=0x%llX, size=0x%llX), atom %s\n", offset, end, atom->getAddress(), atom->getSize(), atom->getDisplayName());
						// write out
						::pwrite(fFileDescriptor, buffer, atom->getSize(), offset);
					}
				}
			}
		}
	}
	delete [] buffer;
	return end;
}


template <>
void Writer<x86>::fixUpReferenceFinal(const ObjectFile::Reference* ref, const ObjectFile::Atom* inAtom, uint8_t buffer[]) const
{
	uint32_t* fixUp = (uint32_t*)&buffer[ref->getFixUpOffset()];
	switch ( (x86::ReferenceKinds)(ref->getKind()) ) {
		case x86::kNoFixUp:
		case x86::kFollowOn:
			// do nothing
			break;
		case x86::kPointerWeakImport:
		case x86::kPointer:
			{
				if ( ref->getTarget().getDefinitionKind() != ObjectFile::Atom::kRegularDefinition ) {
					// external realocation ==> pointer contains addend
					LittleEndian::set32(*fixUp, ref->getTargetOffset());
				}
				else {
					// internal relocation => pointer contains target address
					//printf("Atom::fixUpReferenceFinal() target.name=%s, target.address=0x%08llX\n", target.getDisplayName(), target.getAddress());
					LittleEndian::set32(*fixUp, ref->getTarget().getAddress() + ref->getTargetOffset());
				}
			}
			break;
		case x86::kPointerDiff:
				LittleEndian::set32(*fixUp,
					(ref->getTarget().getAddress() + ref->getTargetOffset()) - (ref->getFromTarget().getAddress() + ref->getFromTargetOffset()) );
			break;
		case x86::kPCRel32WeakImport:
		case x86::kPCRel32:
			int64_t displacement = 0;
			switch ( ref->getTarget().getDefinitionKind() ) {
				case ObjectFile::Atom::kRegularDefinition:
				case ObjectFile::Atom::kWeakDefinition:
					displacement = (ref->getTarget().getAddress() + ref->getTargetOffset()) - (inAtom->getAddress() + ref->getFixUpOffset() + 4);
					break;
				case ObjectFile::Atom::kExternalDefinition:
				case ObjectFile::Atom::kExternalWeakDefinition:
					throw "codegen problem, can't use rel32 to external symbol";
				case ObjectFile::Atom::kTentativeDefinition:
					displacement = 0;
					break;
			}
			const int64_t bl_twoGigLimit = 0x7FFFFFFF;
			if ( (displacement > bl_twoGigLimit) || (displacement < (-bl_twoGigLimit)) ) {
				//fprintf(stderr, "call out of range from %s in %s to %s in %s\n", this->getDisplayName(), this->getFile()->getPath(), target.getDisplayName(), target.getFile()->getPath());
				throw "rel32 out of range";
			}
			LittleEndian::set32(*fixUp, (int32_t)displacement);
			break;
	}
}

template <>
void Writer<x86>::fixUpReferenceRelocatable(const ObjectFile::Reference* ref, const ObjectFile::Atom* inAtom, uint8_t buffer[]) const
{
	uint32_t* fixUp = (uint32_t*)&buffer[ref->getFixUpOffset()];
	bool isExternal = ( (ref->getTarget().getDefinitionKind() != ObjectFile::Atom::kRegularDefinition)
						&& shouldExport(ref->getTarget()) );
	switch (ref->getKind()) {
		case x86::kNoFixUp:
		case x86::kFollowOn:
			// do nothing
			break;
		case x86::kPointer:
			{
				if ( isExternal ) {
					// external realocation ==> pointer contains addend
					LittleEndian::set32(*fixUp, ref->getTargetOffset());
				}
				else {
					// internal relocation
					if ( ref->getTarget().getDefinitionKind() != ObjectFile::Atom::kTentativeDefinition ) {
						// pointer contains target address
						LittleEndian::set32(*fixUp, ref->getTarget().getAddress() + ref->getTargetOffset());
					}
					else {
						// pointer contains addend
						LittleEndian::set32(*fixUp, ref->getTargetOffset());
					}
				}
			}
			break;
		case x86::kPointerDiff:
				LittleEndian::set32(*fixUp,
					(ref->getTarget().getAddress() + ref->getTargetOffset()) - (ref->getFromTarget().getAddress() + ref->getFromTargetOffset()) );
			break;
		case x86::kPCRel32:
			int64_t displacement = 0;
			if ( isExternal )
				displacement = ref->getTargetOffset() - (inAtom->getAddress() + ref->getFixUpOffset() + 4);
			else
				displacement = (ref->getTarget().getAddress() + ref->getTargetOffset()) - (inAtom->getAddress() + ref->getFixUpOffset() + 4);
			const int64_t bl_twoGigLimit = 0x7FFFFFFF;
			if ( (displacement > bl_twoGigLimit) || (displacement < (-bl_twoGigLimit)) ) {
				//fprintf(stderr, "call out of range from %s in %s to %s in %s\n", this->getDisplayName(), this->getFile()->getPath(), target.getDisplayName(), target.getFile()->getPath());
				throw "rel32 out of range";
			}
			LittleEndian::set32(*fixUp, (int32_t)displacement);
			break;
	}
}


template <>
void Writer<ppc>::fixUpReferenceFinal(const ObjectFile::Reference* ref, const ObjectFile::Atom* inAtom, uint8_t buffer[]) const
{
	fixUpReference_powerpc(ref, inAtom, buffer, true);
}

template <>
void Writer<ppc64>::fixUpReferenceFinal(const ObjectFile::Reference* ref, const ObjectFile::Atom* inAtom, uint8_t buffer[]) const
{
	fixUpReference_powerpc(ref, inAtom, buffer, true);
}

template <>
void Writer<ppc>::fixUpReferenceRelocatable(const ObjectFile::Reference* ref, const ObjectFile::Atom* inAtom, uint8_t buffer[]) const
{
	fixUpReference_powerpc(ref, inAtom, buffer, false);
}

template <>
void Writer<ppc64>::fixUpReferenceRelocatable(const ObjectFile::Reference* ref, const ObjectFile::Atom* inAtom, uint8_t buffer[]) const
{
	fixUpReference_powerpc(ref, inAtom, buffer, false);
}

//
// ppc and ppc64 are mostly the same, so they share a template specialzation
//
template <typename A>
void Writer<A>::fixUpReference_powerpc(const ObjectFile::Reference* ref, const ObjectFile::Atom* inAtom, uint8_t buffer[], bool finalLinkedImage) const
{
	uint32_t	instruction;
	uint32_t	newInstruction;
	int64_t		displacement;
	uint64_t	targetAddr = ref->getTarget().getAddress() + ref->getTargetOffset();
	uint64_t	picBaseAddr;
	uint16_t	instructionLowHalf;
	uint16_t	instructionHighHalf;
	uint32_t*	fixUp = (uint32_t*)&buffer[ref->getFixUpOffset()];
	pint_t*		fixUpPointer = (pint_t*)&buffer[ref->getFixUpOffset()];
	bool		relocateableExternal;

	if ( finalLinkedImage )
		relocateableExternal = (relocationNeededInFinalLinkedImage(ref->getTarget()) == kRelocExternal);
	else
		relocateableExternal = ( (ref->getTarget().getDefinitionKind() != ObjectFile::Atom::kRegularDefinition)
									&& shouldExport(ref->getTarget()) );

	const int64_t picbase_twoGigLimit = 0x80000000;

	switch ( (typename A::ReferenceKinds)(ref->getKind()) ) {
		case A::kNoFixUp:
		case A::kFollowOn:
			// do nothing
			break;
		case A::kPointerWeakImport:
		case A::kPointer:
			{
				//fprintf(stderr, "fixUpReferenceFinal: %s reference to %s\n", this->getDisplayName(), target.getDisplayName());
				if ( finalLinkedImage && ((SectionInfo*)inAtom->getSection())->fAllLazyPointers ) {
					// lazy-symbol ==> pointer contains address of dyld_stub_binding_helper (stored in "from" target)
					if ( fDyldHelper == NULL )
						throw "symbol dyld_stub_binding_helper not defined (usually in crt1.o/dylib1.o/bundle1.o)";
					P::setP(*fixUpPointer, fDyldHelper->getAddress());
				}
				else if ( relocateableExternal ) {
					// external realocation ==> pointer contains addend
					P::setP(*fixUpPointer, ref->getTargetOffset());
				}
				else {
					// internal relocation
					if ( finalLinkedImage || (ref->getTarget().getDefinitionKind() != ObjectFile::Atom::kTentativeDefinition)  ) {
						// pointer contains target address
						//printf("Atom::fixUpReference_powerpc() target.name=%s, target.address=0x%08llX\n", target.getDisplayName(), target.getAddress());
						P::setP(*fixUpPointer, targetAddr);
					}
					else {
						// pointer contains addend
						P::setP(*fixUpPointer, ref->getTargetOffset());
					}
				}
			}
			break;
		case A::kPointerDiff64:
			P::setP(*fixUpPointer, targetAddr - (ref->getFromTarget().getAddress() + ref->getFromTargetOffset()) );
			break;
		case A::kPointerDiff32:
			P::E::set32(*fixUp, targetAddr - (ref->getFromTarget().getAddress() + ref->getFromTargetOffset()) );
			break;
		case A::kBranch24WeakImport:
		case A::kBranch24:
			{
				//fprintf(stderr, "bl fixup to %s at 0x%08llX, ", target.getDisplayName(), target.getAddress());
				int64_t displacement = targetAddr - (inAtom->getAddress() + ref->getFixUpOffset());
				if ( relocateableExternal )  {
					// doing "ld -r" to an external symbol
					// the mach-o way of encoding this is that the bl instruction's target addr is the offset into the target
					displacement -= ref->getTarget().getAddress();
				}
				else {
					const int64_t bl_eightMegLimit = 0x00FFFFFF;
					if ( (displacement > bl_eightMegLimit) || (displacement < (-bl_eightMegLimit)) ) {
						//fprintf(stderr, "bl out of range (%lld max is +/-16M) from %s in %s to %s in %s\n", displacement, this->getDisplayName(), this->getFile()->getPath(), target.getDisplayName(), target.getFile()->getPath());
						throwf("bl out of range (%lld max is +/-16M) from %s in %s to %s in %s",
							displacement, inAtom->getDisplayName(), inAtom->getFile()->getPath(),
							ref->getTarget().getDisplayName(), ref->getTarget().getFile()->getPath());
					}
				}
				instruction = BigEndian::get32(*fixUp);
				newInstruction = (instruction & 0xFC000003) | ((uint32_t)displacement & 0x03FFFFFC);
				//fprintf(stderr, "bl fixup: 0x%08X -> 0x%08X\n", instruction, newInstruction);
				BigEndian::set32(*fixUp, newInstruction);
			}
			break;
		case A::kBranch14:
			{
				//fprintf(stderr, "bc fixup %p to %s+0x%08X == 0x%08llX\n", this, ref->getTarget().getDisplayName(), ref->getTargetOffset(), targetAddr);
				int64_t displacement = targetAddr - (inAtom->getAddress() + ref->getFixUpOffset());
				if ( relocateableExternal )  {
					// doing "ld -r" to an external symbol
					// the mach-o way of encoding this is that the bl instruction's target addr is the offset into the target
					displacement -= ref->getTarget().getAddress();
				}
				else {
					const int64_t b_sixtyFourKiloLimit = 0x0000FFFF;
					if ( (displacement > b_sixtyFourKiloLimit) || (displacement < (-b_sixtyFourKiloLimit)) ) {
						//fprintf(stderr, "bl out of range (%lld max is +/-16M) from %s in %s to %s in %s\n", displacement, this->getDisplayName(), this->getFile()->getPath(), target.getDisplayName(), target.getFile()->getPath());
						throwf("bc out of range (%lld max is +/-64K) from %s in %s to %s in %s",
							displacement, inAtom->getDisplayName(), inAtom->getFile()->getPath(),
							ref->getTarget().getDisplayName(), ref->getTarget().getFile()->getPath());
					}
				}
				//fprintf(stderr, "bc fixup displacement=0x%08llX, atom.addr=0x%08llX, atom.offset=0x%08X\n", displacement, inAtom->getAddress(), (uint32_t)ref->getFixUpOffset());
				instruction = BigEndian::get32(*fixUp);
				newInstruction = (instruction & 0xFFFF0003) | ((uint32_t)displacement & 0x0000FFFC);
				//fprintf(stderr, "bc fixup: 0x%08X -> 0x%08X\n", instruction, newInstruction);
				BigEndian::set32(*fixUp, newInstruction);
			}
			break;
		case A::kPICBaseLow16:
			picBaseAddr = inAtom->getAddress() + ref->getFromTargetOffset();
			displacement = targetAddr - picBaseAddr;
			if ( (displacement > picbase_twoGigLimit) || (displacement < (-picbase_twoGigLimit)) )
				throw "32-bit pic-base out of range";
			instructionLowHalf = (displacement & 0xFFFF);
			instruction = BigEndian::get32(*fixUp);
			newInstruction = (instruction & 0xFFFF0000) | instructionLowHalf;
			BigEndian::set32(*fixUp, newInstruction);
			break;
		case A::kPICBaseLow14:
			picBaseAddr = inAtom->getAddress() + ref->getFromTargetOffset();
			displacement = targetAddr - picBaseAddr;
			if ( (displacement > picbase_twoGigLimit) || (displacement < (-picbase_twoGigLimit)) )
				throw "32-bit pic-base out of range";
			if ( (displacement & 0x3) != 0 )
				throwf("bad offset (0x%08X) for lo14 instruction pic-base fix-up", (uint32_t)displacement);
			instructionLowHalf = (displacement & 0xFFFC);
			instruction = BigEndian::get32(*fixUp);
			newInstruction = (instruction & 0xFFFF0003) | instructionLowHalf;
			BigEndian::set32(*fixUp, newInstruction);
			break;
		case A::kPICBaseHigh16:
			picBaseAddr = inAtom->getAddress() + ref->getFromTargetOffset();
			displacement = targetAddr - picBaseAddr;
			if ( (displacement > picbase_twoGigLimit) || (displacement < (-picbase_twoGigLimit)) )
				throw "32-bit pic-base out of range";
			instructionLowHalf = displacement >> 16;
			if ( (displacement & 0x00008000) != 0 )
				++instructionLowHalf;
			instruction = BigEndian::get32(*fixUp);
			newInstruction = (instruction & 0xFFFF0000) | instructionLowHalf;
			BigEndian::set32(*fixUp, newInstruction);
			break;
		case A::kAbsLow16:
			if ( relocateableExternal )
				targetAddr -= ref->getTarget().getAddress();
			instructionLowHalf = (targetAddr & 0xFFFF);
			instruction = BigEndian::get32(*fixUp);
			newInstruction = (instruction & 0xFFFF0000) | instructionLowHalf;
			BigEndian::set32(*fixUp, newInstruction);
			break;
		case A::kAbsLow14:
			if ( relocateableExternal )
				targetAddr -= ref->getTarget().getAddress();
			if ( (targetAddr & 0x3) != 0 )
				throw "bad address for absolute lo14 instruction fix-up";
			instructionLowHalf = (targetAddr & 0xFFFF);
			instruction = BigEndian::get32(*fixUp);
			newInstruction = (instruction & 0xFFFF0003) | instructionLowHalf;
			BigEndian::set32(*fixUp, newInstruction);
			break;
		case A::kAbsHigh16:
			if ( relocateableExternal )
				targetAddr -= ref->getTarget().getAddress();
			instructionHighHalf = (targetAddr >> 16);
			instruction = BigEndian::get32(*fixUp);
			newInstruction = (instruction & 0xFFFF0000) | instructionHighHalf;
			BigEndian::set32(*fixUp, newInstruction);
			break;
		case A::kAbsHigh16AddLow:
			if ( relocateableExternal )
				targetAddr -= ref->getTarget().getAddress();
			if ( targetAddr & 0x00008000 )
				targetAddr += 0x00010000;
			instruction = BigEndian::get32(*fixUp);
			newInstruction = (instruction & 0xFFFF0000) | (targetAddr >> 16);
			BigEndian::set32(*fixUp, newInstruction);
			break;
	}
}

template <>
bool Writer<ppc>::stubableReferenceKind(uint8_t kind)
{
	return (kind == ppc::kBranch24 || kind == ppc::kBranch24WeakImport);
}

template <>
bool Writer<ppc64>::stubableReferenceKind(uint8_t kind)
{
	return (kind == ppc64::kBranch24 || kind == ppc64::kBranch24WeakImport);
}

template <>
bool Writer<x86>::stubableReferenceKind(uint8_t kind)
{
	return (kind == x86::kPCRel32 || kind == x86::kPCRel32WeakImport);
}


template <>
bool Writer<ppc>::weakImportReferenceKind(uint8_t kind)
{
	return (kind == ppc::kBranch24WeakImport || kind == ppc::kPointerWeakImport);
}

template <>
bool Writer<ppc64>::weakImportReferenceKind(uint8_t kind)
{
	return (kind == ppc64::kBranch24WeakImport || kind == ppc64::kPointerWeakImport);
}

template <>
bool Writer<x86>::weakImportReferenceKind(uint8_t kind)
{
	return (kind == x86::kPCRel32WeakImport || kind == x86::kPointerWeakImport);
}



template <>
bool Writer<ppc>::GOTReferenceKind(uint8_t kind)
{
	return false;
}

template <>
bool Writer<ppc64>::GOTReferenceKind(uint8_t kind)
{
	return false;
}

template <>
bool Writer<x86>::GOTReferenceKind(uint8_t kind)
{
	return false;
}



template <typename A>
void Writer<A>::synthesizeStubs()
{
	switch ( fOptions.outputKind() ) {
		case Options::kStaticExecutable:
		case Options::kDyld:
		case Options::kObjectFile:
			// these output kinds never have stubs
			return;
		case Options::kDynamicLibrary:
		case Options::kDynamicBundle:
		case Options::kDynamicExecutable:
			// try to synthesize stubs for these
			break;
	}

	// walk every atom and reference
	for (std::vector<ObjectFile::Atom*>::iterator it=fAllAtoms->begin(); it != fAllAtoms->end(); it++) {
		ObjectFile::Atom* atom = *it;
		std::vector<ObjectFile::Reference*>&  references = atom->getReferences();
		for (std::vector<ObjectFile::Reference*>::iterator rit=references.begin(); rit != references.end(); rit++) {
			ObjectFile::Reference* ref = *rit;
			ObjectFile::Atom& target = ref->getTarget();
			// build map of which symbols need weak importing
			if ( (target.getDefinitionKind() == ObjectFile::Atom::kExternalDefinition)
				|| (target.getDefinitionKind() == ObjectFile::Atom::kExternalWeakDefinition) ) {
				bool weakImport = this->weakImportReferenceKind(ref->getKind());
				std::map<const ObjectFile::Atom*,bool>::iterator pos = fWeakImportMap.find(&target);
				if ( pos == fWeakImportMap.end() ) {
					// target not in fWeakImportMap, so add
					fWeakImportMap[&target] = weakImport;
				}
				else {
					// target in fWeakImportMap, check for weakness mismatch
					if ( pos->second != weakImport ) {
						// found mismatch
						switch ( fOptions.weakReferenceMismatchTreatment() ) {
							case Options::kWeakReferenceMismatchError:
								throwf("mismatching weak references for symbol: %s", target.getName());
							case Options::kWeakReferenceMismatchWeak:
								pos->second = true;
								break;
							case Options::kWeakReferenceMismatchNonWeak:
								pos->second = false;
								break;
						}
					}
				}
			}
			// create stubs as needed
			if ( this->stubableReferenceKind(ref->getKind())
				&& this->relocationNeededInFinalLinkedImage(target) == kRelocExternal ) {
				ObjectFile::Atom* stub = NULL;
				std::map<ObjectFile::Atom*,ObjectFile::Atom*>::iterator pos = fStubsMap.find(&target);
				if ( pos == fStubsMap.end() ) {
					stub = new StubAtom<A>(*this, target);
					fStubsMap[&target] = stub;
				}
				else {
					stub = pos->second;
				}
				// alter reference to use stub instead
				ref->setTarget(*stub, 0);
			}
			// create GOT slots (non-lazy pointers) as needed
			else if ( this->GOTReferenceKind(ref->getKind()) ) {
				ObjectFile::Atom* nlp = NULL;
				std::map<ObjectFile::Atom*,ObjectFile::Atom*>::iterator pos = fGOTMap.find(&target);
				if ( pos == fGOTMap.end() ) {
					nlp = new NonLazyPointerAtom<A>(*this, target);
					fGOTMap[&target] = nlp;
				}
				else {
					nlp = pos->second;
				}
				// alter reference to use non lazy pointer instead
				ref->setTarget(*nlp, 0);
			}
		}
	}

	// sort stubs

	// sort lazy pointers

	// add stubs to fAllAtoms
	if ( fAllSynthesizedStubs.size() != 0 ) {
		std::vector<ObjectFile::Atom*>* stubs = (std::vector<ObjectFile::Atom*>*)&fAllSynthesizedStubs;
		std::vector<ObjectFile::Atom*> mergedStubs;
		if ( fAllSynthesizedStubHelpers.size() != 0 ) {
			// when we have stubs and helpers, insert both into fAllAtoms
			mergedStubs.insert(mergedStubs.end(), fAllSynthesizedStubs.begin(), fAllSynthesizedStubs.end());
			mergedStubs.insert(mergedStubs.end(), fAllSynthesizedStubHelpers.begin(), fAllSynthesizedStubHelpers.end());
			stubs = &mergedStubs;
		}
		ObjectFile::Section* curSection = NULL;
		ObjectFile::Atom* prevAtom = NULL;
		for (std::vector<ObjectFile::Atom*>::iterator it=fAllAtoms->begin(); it != fAllAtoms->end(); it++) {
			ObjectFile::Atom* atom = *it;
			ObjectFile::Section* nextSection = atom->getSection();
			if ( nextSection != curSection ) {
				// HACK HACK for i386 where stubs are not in _TEXT segment
				if ( strcmp(fAllSynthesizedStubs[0]->getSegment().getName(), "__IMPORT") == 0 ) {
					if ( ((prevAtom != NULL) && (strcmp(prevAtom->getSegment().getName(), "__IMPORT") == 0))
						|| (strcmp(atom->getSegment().getName(), "__LINKEDIT") == 0) ) {
						// insert stubs at end of __IMPORT segment, or before __LINKEDIT
						fAllAtoms->insert(it, fAllSynthesizedStubs.begin(), fAllSynthesizedStubs.end());
						break;
					}
				}
				else {
					if ( (prevAtom != NULL) && (strcmp(prevAtom->getSectionName(), "__text") == 0) ) {
						// found end of __text section, insert stubs here
						fAllAtoms->insert(it, stubs->begin(), stubs->end());
						break;
					}
				}
				curSection = nextSection;
			}
			prevAtom = atom;
		}
	}


	// add lazy pointers to fAllAtoms
	if ( fAllSynthesizedLazyPointers.size() != 0 ) {
		ObjectFile::Section* curSection = NULL;
		ObjectFile::Atom* prevAtom = NULL;
		bool inserted = false;
		for (std::vector<ObjectFile::Atom*>::iterator it=fAllAtoms->begin(); it != fAllAtoms->end(); it++) {
			ObjectFile::Atom* atom = *it;
			ObjectFile::Section* nextSection = atom->getSection();
			if ( nextSection != curSection ) {
				if ( (prevAtom != NULL) && (strcmp(prevAtom->getSectionName(), "__dyld") == 0) ) {
					// found end of __dyld section, insert lazy pointers here
					fAllAtoms->insert(it, fAllSynthesizedLazyPointers.begin(), fAllSynthesizedLazyPointers.end());
					inserted = true;
					break;
				}
				curSection = nextSection;
			}
			prevAtom = atom;
		}
		if ( !inserted ) {
			throw "can't insert lazy pointers, __dyld section not found";
		}
	}
	
	// add non-lazy pointers to fAllAtoms
	if ( fAllSynthesizedNonLazyPointers.size() != 0 ) {
		ObjectFile::Section* curSection = NULL;
		ObjectFile::Atom* prevAtom = NULL;
		bool inserted = false;
		for (std::vector<ObjectFile::Atom*>::iterator it=fAllAtoms->begin(); it != fAllAtoms->end(); it++) {
			ObjectFile::Atom* atom = *it;
			ObjectFile::Section* nextSection = atom->getSection();
			if ( nextSection != curSection ) {
				if ( (prevAtom != NULL) && (strcmp(prevAtom->getSectionName(), "__dyld") == 0) ) {
					// found end of __dyld section, insert lazy pointers here
					fAllAtoms->insert(it, fAllSynthesizedNonLazyPointers.begin(), fAllSynthesizedNonLazyPointers.end());
					inserted = true;
					break;
				}
				curSection = nextSection;
			}
			prevAtom = atom;
		}
		if ( !inserted ) {
			throw "can't insert non-lazy pointers, __dyld section not found";
		}
	}
}


template <typename A>
void Writer<A>::partitionIntoSections()
{
	const bool oneSegmentCommand = (fOptions.outputKind() == Options::kObjectFile);

	// for every atom, set its sectionInfo object and section offset
	// build up fSegmentInfos along the way
	ObjectFile::Section* curSection = NULL;
	SectionInfo* currentSectionInfo = NULL;
	SegmentInfo* currentSegmentInfo = NULL;
	unsigned int sectionIndex = 1;
	fSegmentInfos.reserve(8);
	for (unsigned int i=0; i < fAllAtoms->size(); ++i) {
		ObjectFile::Atom* atom = (*fAllAtoms)[i];
		if ( (atom->getSection() != curSection) || ((curSection==NULL) && (strcmp(atom->getSectionName(),currentSectionInfo->fSectionName) != 0)) ) {
			if ( oneSegmentCommand ) {
				if ( currentSegmentInfo == NULL ) {
					currentSegmentInfo = new SegmentInfo();
					currentSegmentInfo->fInitProtection = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE;
					currentSegmentInfo->fMaxProtection = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE;
					this->fSegmentInfos.push_back(currentSegmentInfo);
				}
				currentSectionInfo = new SectionInfo();
				strcpy(currentSectionInfo->fSectionName, atom->getSectionName());
				strcpy(currentSectionInfo->fSegmentName, atom->getSegment().getName());
				currentSectionInfo->fAlignment = atom->getAlignment();
				currentSectionInfo->fAllZeroFill = atom->isZeroFill();
				currentSectionInfo->fVirtualSection = ( (currentSectionInfo->fSectionName[0] == '.') || (oneSegmentCommand && (atom->getDefinitionKind()==ObjectFile::Atom::kTentativeDefinition)) );
				if ( !currentSectionInfo->fVirtualSection || fEmitVirtualSections )
					currentSectionInfo->setIndex(sectionIndex++);
				currentSegmentInfo->fSections.push_back(currentSectionInfo);
			}
			else {
				if ( (currentSegmentInfo == NULL) || (strcmp(currentSegmentInfo->fName, atom->getSegment().getName()) != 0) ) {
					currentSegmentInfo = new SegmentInfo();
					strcpy(currentSegmentInfo->fName, atom->getSegment().getName());
					uint32_t initprot  = 0;
					if ( atom->getSegment().isContentReadable() )
						initprot |= VM_PROT_READ;
					if ( atom->getSegment().isContentWritable() )
						initprot |= VM_PROT_WRITE;
					if ( atom->getSegment().isContentExecutable() )
						initprot |= VM_PROT_EXECUTE;
					currentSegmentInfo->fInitProtection = initprot;
					if ( initprot == 0 )
						currentSegmentInfo->fMaxProtection = 0;  // pagezero should have maxprot==initprot==0
					else
						currentSegmentInfo->fMaxProtection = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE;
					currentSegmentInfo->fBaseAddress = atom->getSegment().getBaseAddress();
					currentSegmentInfo->fFixedAddress = atom->getSegment().hasFixedAddress();
					this->fSegmentInfos.push_back(currentSegmentInfo);
				}
				currentSectionInfo = new SectionInfo();
				currentSectionInfo->fAtoms.reserve(fAllAtoms->size()/4); // reduce reallocations by starting large
				strcpy(currentSectionInfo->fSectionName, atom->getSectionName());
				strcpy(currentSectionInfo->fSegmentName, atom->getSegment().getName());
				currentSectionInfo->fAlignment = atom->getAlignment();
				// check for -sectalign override
				std::vector<Options::SectionAlignment>&	alignmentOverrides = fOptions.sectionAlignments();
				for(std::vector<Options::SectionAlignment>::iterator it=alignmentOverrides.begin(); it != alignmentOverrides.end(); ++it) {
					if ( (strcmp(it->segmentName, currentSectionInfo->fSegmentName) == 0) && (strcmp(it->sectionName, currentSectionInfo->fSectionName) == 0) )
						currentSectionInfo->fAlignment = it->alignment;
				}
				currentSectionInfo->fAllZeroFill = atom->isZeroFill();
				currentSectionInfo->fVirtualSection = ( currentSectionInfo->fSectionName[0] == '.');
				if ( !currentSectionInfo->fVirtualSection || fEmitVirtualSections )
					currentSectionInfo->setIndex(sectionIndex++);
				currentSegmentInfo->fSections.push_back(currentSectionInfo);
			}
			if ( (strcmp(currentSectionInfo->fSegmentName, "__TEXT") == 0) && (strcmp(currentSectionInfo->fSectionName, "._load_commands") == 0) ) {
				fLoadCommandsSection = currentSectionInfo;
				fLoadCommandsSegment = currentSegmentInfo;
			}
			if ( (strcmp(currentSectionInfo->fSegmentName, "__DATA") == 0) && (strcmp(currentSectionInfo->fSectionName, "__la_symbol_ptr") == 0) )
				currentSectionInfo->fAllLazyPointers = true;
			if ( (strcmp(currentSectionInfo->fSegmentName, "__DATA") == 0) && (strcmp(currentSectionInfo->fSectionName, "__la_sym_ptr2") == 0) )
				currentSectionInfo->fAllLazyPointers = true;
			if ( (strcmp(currentSectionInfo->fSegmentName, "__DATA") == 0) && (strcmp(currentSectionInfo->fSectionName, "__nl_symbol_ptr") == 0) )
				currentSectionInfo->fAllNonLazyPointers = true;
			if ( (strcmp(currentSectionInfo->fSegmentName, "__IMPORT") == 0) && (strcmp(currentSectionInfo->fSectionName, "__pointers") == 0) )
				currentSectionInfo->fAllNonLazyPointers = true;
			if ( (strcmp(currentSectionInfo->fSegmentName, "__TEXT") == 0) && (strcmp(currentSectionInfo->fSectionName, "__picsymbolstub1") == 0) )
				currentSectionInfo->fAllStubs = true;
			if ( (strcmp(currentSectionInfo->fSegmentName, "__TEXT") == 0) && (strcmp(currentSectionInfo->fSectionName, "__symbol_stub1") == 0) )
				currentSectionInfo->fAllStubs = true;
			if ( (strcmp(currentSectionInfo->fSegmentName, "__TEXT") == 0) && (strcmp(currentSectionInfo->fSectionName, "__picsymbolstub2") == 0) )
				currentSectionInfo->fAllStubs = true;
			if ( (strcmp(currentSectionInfo->fSegmentName, "__TEXT") == 0) && (strcmp(currentSectionInfo->fSectionName, "__symbol_stub") == 0) )
				currentSectionInfo->fAllStubs = true;
			if ( (strcmp(currentSectionInfo->fSegmentName, "__IMPORT") == 0) && (strcmp(currentSectionInfo->fSectionName, "__jump_table") == 0) )
				currentSectionInfo->fAllSelfModifyingStubs = true;
			curSection = atom->getSection();
		}
		// any non-zero fill atoms make whole section marked not-zero-fill
		if ( currentSectionInfo->fAllZeroFill && ! atom->isZeroFill() )
			currentSectionInfo->fAllZeroFill = false;
		// change section object to be Writer's SectionInfo object
		atom->setSection(currentSectionInfo);
		// section alignment is that of a contained atom with the greatest alignment
		uint8_t atomAlign = atom->getAlignment();
		if ( currentSectionInfo->fAlignment < atomAlign )
			currentSectionInfo->fAlignment = atomAlign;
		// calculate section offset for this atom
		uint64_t offset = currentSectionInfo->fSize;
		uint64_t alignment = 1 << atomAlign;
		offset = ( (offset+alignment-1) & (-alignment) );
		atom->setSectionOffset(offset);
		uint64_t curAtomSize = atom->getSize();
		currentSectionInfo->fSize = offset + curAtomSize;
		// add atom to section vector
		currentSectionInfo->fAtoms.push_back(atom);
		// update largest size
		if ( !currentSectionInfo->fAllZeroFill && (curAtomSize > fLargestAtomSize) )
			fLargestAtomSize = curAtomSize;
	}
}


struct TargetAndOffset { ObjectFile::Atom* atom; uint32_t offset; };
class TargetAndOffsetComparor
{
public:
	bool operator()(const TargetAndOffset& left, const TargetAndOffset& right) const
	{
		if ( left.atom != right.atom )
			return ( left.atom < right.atom );
		return ( left.offset < right.offset );
	}
};

template <>
bool Writer<ppc>::addBranchIslands()
{
	return this->addPPCBranchIslands();
}

template <>
bool Writer<ppc64>::addBranchIslands()
{
	return this->addPPCBranchIslands();
}

template <>
bool Writer<x86>::addBranchIslands()
{
	// x86 branches can reach entire 4G address space, so no need for branch islands
	return false;
}


template <>
inline uint8_t Writer<ppc>::branch24Reference()
{
	return ppc::kBranch24;
}

template <>
inline uint8_t Writer<ppc64>::branch24Reference()
{
	return ppc64::kBranch24;
}

//
// PowerPC can do PC relative branches as far as +/-16MB.
// If a branch target is >16MB then we insert one or more
// "branch islands" between the branch and its target that
// allows island hoping to the target.
//
// Branch Island Algorithm
//
// If the __TEXT segment < 16MB, then no branch islands needed
// Otherwise, every 15MB into the __TEXT segment is region is
// added which can contain branch islands.  Every out of range
// bl instruction is checked.  If it crosses a region, an island
// is added to that region with the same target and the bl is
// adjusted to target the island instead.
//
// In theory, if too many islands are added to one region, it
// could grow the __TEXT enough that other previously in-range
// bl branches could be pushed out of range.  We reduce the
// probability this could happen by placing the ranges every
// 15MB which means the region would have to be 1MB (256K islands)
// before any branches could be pushed out of range.
//
template <typename A>
bool Writer<A>::addPPCBranchIslands()
{
	bool result = false;
	// Can only possibly need branch islands if __TEXT segment > 16M
	if ( fLoadCommandsSegment->fSize > 16000000 ) {
		//fprintf(stderr, "ld64: checking for branch islands, __TEXT segment size=%llu\n", fLoadCommandsSegment->fSize);
		const uint32_t kBetweenRegions = 15000000; // place regions of islands every 15MB in __text section
		SectionInfo* textSection = NULL;
		for (std::vector<SectionInfo*>::iterator it=fLoadCommandsSegment->fSections.begin(); it != fLoadCommandsSegment->fSections.end(); it++) {
			if ( strcmp((*it)->fSectionName, "__text") == 0 ) {
				textSection = *it;
				//fprintf(stderr, "ld64: checking for branch islands, __text section size=%llu\n", textSection->fSize);
				break;
			}
		}
		const int kIslandRegionsCount = fLoadCommandsSegment->fSize / kBetweenRegions;
		typedef std::map<TargetAndOffset,ObjectFile::Atom*, TargetAndOffsetComparor> AtomToIsland;
		AtomToIsland regionsMap[kIslandRegionsCount];
		std::vector<ObjectFile::Atom*> regionsIslands[kIslandRegionsCount];
		unsigned int islandCount = 0;

		// create islands for branch references that are out of range
		for (std::vector<ObjectFile::Atom*>::iterator it=fAllAtoms->begin(); it != fAllAtoms->end(); it++) {
			ObjectFile::Atom* atom = *it;
			std::vector<ObjectFile::Reference*>&  references = atom->getReferences();
			for (std::vector<ObjectFile::Reference*>::iterator rit=references.begin(); rit != references.end(); rit++) {
				ObjectFile::Reference* ref = *rit;
				if ( ref->getKind() == this->branch24Reference() ) {
					ObjectFile::Atom& target = ref->getTarget();
					int64_t srcAddr = atom->getAddress() + ref->getFixUpOffset();
					int64_t dstAddr = target.getAddress() + ref->getTargetOffset();
					int64_t displacement = dstAddr - srcAddr;
					const int64_t kFifteenMegLimit = kBetweenRegions;
					if ( (displacement > kFifteenMegLimit) || (displacement < (-kFifteenMegLimit)) ) {
						for (int i=0; i < kIslandRegionsCount; ++i) {
							AtomToIsland* region=&regionsMap[i];
							int64_t islandRegionAddr = kBetweenRegions * (i+1);
							if ( ((srcAddr < islandRegionAddr) && (dstAddr > islandRegionAddr))
							   ||((dstAddr < islandRegionAddr) && (srcAddr > islandRegionAddr)) ) {
								TargetAndOffset islandTarget = { &target, ref->getTargetOffset() };
								AtomToIsland::iterator pos = region->find(islandTarget);
								if ( pos == region->end() ) {
									BranchIslandAtom<A>* island = new BranchIslandAtom<A>(*this, target.getDisplayName(), i, target, ref->getTargetOffset());
									island->setSection(textSection);
									(*region)[islandTarget] = island;
									regionsIslands[i].push_back(island);
									++islandCount;
									ref->setTarget(*island, 0);
								}
								else {
									ref->setTarget(*(pos->second), 0);
								}
							}
						}
					}
				}
			}
		}

		// insert islands into __text section and adjust section offsets
		if ( islandCount > 0 ) {
			//fprintf(stderr, "ld64: %u branch islands required\n", islandCount);
			std::vector<ObjectFile::Atom*> newAtomList;
			newAtomList.reserve(textSection->fAtoms.size()+islandCount);
			uint64_t islandRegionAddr = kBetweenRegions;
			int regionIndex = 0;
			uint64_t sectionOffset = 0;
			for (std::vector<ObjectFile::Atom*>::iterator it=textSection->fAtoms.begin(); it != textSection->fAtoms.end(); it++) {
				ObjectFile::Atom* atom = *it;
				newAtomList.push_back(atom);
				if ( atom->getAddress() > islandRegionAddr ) {
					std::vector<ObjectFile::Atom*>* regionIslands = &regionsIslands[regionIndex];
					for (std::vector<ObjectFile::Atom*>::iterator rit=regionIslands->begin(); rit != regionIslands->end(); rit++) {
						ObjectFile::Atom* islandAtom = *rit;
						newAtomList.push_back(islandAtom);
						uint64_t alignment = 1 << (islandAtom->getAlignment());
						sectionOffset = ( (sectionOffset+alignment-1) & (-alignment) );
						islandAtom->setSectionOffset(sectionOffset);
						sectionOffset += islandAtom->getSize();
					}
					++regionIndex;
					islandRegionAddr += kBetweenRegions;
				}
				uint64_t alignment = 1 << (atom->getAlignment());
				sectionOffset = ( (sectionOffset+alignment-1) & (-alignment) );
				atom->setSectionOffset(sectionOffset);
				sectionOffset += atom->getSize();
			}
			// put any remaining islands at end of __text section
			if ( regionIndex < kIslandRegionsCount ) {
				sectionOffset = textSection->fSize;
				std::vector<ObjectFile::Atom*>* regionIslands = &regionsIslands[regionIndex];
				for (std::vector<ObjectFile::Atom*>::iterator rit=regionIslands->begin(); rit != regionIslands->end(); rit++) {
					ObjectFile::Atom* islandAtom = *rit;
					newAtomList.push_back(islandAtom);
					uint64_t alignment = 1 << (islandAtom->getAlignment());
					sectionOffset = ( (sectionOffset+alignment-1) & (-alignment) );
					islandAtom->setSectionOffset(sectionOffset);
					sectionOffset += islandAtom->getSize();
				}
			}

			textSection->fAtoms = newAtomList;
			textSection->fSize = sectionOffset;
			result = true;
		}

	}
	return result;
}


template <typename A>
void Writer<A>::adjustLoadCommandsAndPadding()
{
	fSegmentCommands->computeSize();

	// recompute load command section offsets
	uint64_t offset = 0;
	std::vector<class ObjectFile::Atom*>& loadCommandAtoms = fLoadCommandsSection->fAtoms;
	const unsigned int atomCount = loadCommandAtoms.size();
	for (unsigned int i=0; i < atomCount; ++i) {
		ObjectFile::Atom* atom = loadCommandAtoms[i];
		uint64_t alignment = 1 << atom->getAlignment();
		offset = ( (offset+alignment-1) & (-alignment) );
		atom->setSectionOffset(offset);
		uint32_t atomSize = atom->getSize();
		if ( atomSize > fLargestAtomSize )
			fLargestAtomSize = atomSize;
		offset += atomSize;
		fLoadCommandsSection->fSize = offset;
	}

	std::vector<SectionInfo*>& sectionInfos = fLoadCommandsSegment->fSections;
	const int sectionCount = sectionInfos.size();
	uint64_t paddingSize = 0;
	if ( fOptions.outputKind() == Options::kDyld ) {
		// dyld itself has special padding requirements.  We want the beginning __text section to start at a stable address
		uint32_t totalSizeOfHeaderAndLoadCommands = 0;
		for(int j=0; j < sectionCount; ++j) {
			SectionInfo* curSection = sectionInfos[j];
			totalSizeOfHeaderAndLoadCommands += curSection->fSize;
			if ( strcmp(curSection->fSectionName, fHeaderPadding->getSectionName()) == 0 )
				break;
		}
		paddingSize = 4096 - (totalSizeOfHeaderAndLoadCommands % 4096);
	}
	else if ( fOptions.outputKind() == Options::kObjectFile ) {
		// mach-o .o files need no padding between load commands and first section
		paddingSize = 0;
	}
	else {
		// calculate max padding to keep segment size same, but all free space at end of load commands
		uint64_t totalSize = 0;
		uint64_t worstCaseAlignmentPadding = 0;
		for(int j=0; j < sectionCount; ++j) {
			SectionInfo* curSection = sectionInfos[j];
			totalSize += curSection->fSize;
			if ( j != 0 ) // don't count aligment of mach_header which is page-aligned
				worstCaseAlignmentPadding += (1 << curSection->fAlignment) - 1;
		}
		uint64_t segmentSize = ((totalSize+worstCaseAlignmentPadding+4095) & (-4096));
		// don't know exactly how it will layout, but we can inflate padding atom this big and still keep aligment constraints
		paddingSize = segmentSize - (totalSize+worstCaseAlignmentPadding);

		// if command line requires more padding than this
		if ( paddingSize < fOptions.minimumHeaderPad() ) {
			int extraPages = (fOptions.minimumHeaderPad() - paddingSize + 4095)/4096;
			paddingSize += extraPages * 4096;
		}
	}

	// adjust atom size and update section size
	fHeaderPadding->setSize(paddingSize);
	for(int j=0; j < sectionCount; ++j) {
		SectionInfo* curSection = sectionInfos[j];
		if ( strcmp(curSection->fSectionName, fHeaderPadding->getSectionName()) == 0 )
			curSection->fSize = paddingSize;
	}
}

// assign file offsets and logical address to all segments
template <typename A>
void Writer<A>::assignFileOffsets()
{
	bool finalLinkedImage = (fOptions.outputKind() != Options::kObjectFile);
	bool haveFixedSegments = false;
	uint64_t fileOffset = 0;
	uint64_t nextContiguousAddress = fOptions.baseAddress();

	// Run through the segments and each segment's sections to assign addresses
	for (std::vector<SegmentInfo*>::iterator segit = fSegmentInfos.begin(); segit != fSegmentInfos.end(); ++segit) {
		SegmentInfo* curSegment = *segit;
		
		fileOffset = (fileOffset+4095) & (-4096);
		curSegment->fFileOffset = fileOffset;
		
		// Set the segment base address
		if ( curSegment->fFixedAddress )
			haveFixedSegments = true;
		else
			curSegment->fBaseAddress = nextContiguousAddress;

		// We've set the segment address, now run through each section.
		uint64_t address = curSegment->fBaseAddress;
		SectionInfo* firstZeroFillSection = NULL;
		SectionInfo* prevSection = NULL;
		
		std::vector<SectionInfo*>& sectionInfos = curSegment->fSections;
		
		for (std::vector<SectionInfo*>::iterator it = sectionInfos.begin(); it != sectionInfos.end(); ++it) {
			SectionInfo* curSection = *it;
		
			// adjust section address based on alignment
			uint64_t alignment = 1 << curSection->fAlignment;
			address    = ( (address+alignment-1) & (-alignment) );
			
			// adjust file offset to match address
			if ( prevSection != NULL ) {
				fileOffset = (address - prevSection->getBaseAddress()) + prevSection->fFileOffset;
			}
			
			// update section info
			curSection->fFileOffset = fileOffset;
			curSection->setBaseAddress(address);
			
			// keep track of trailing zero fill sections
			if ( curSection->fAllZeroFill && (firstZeroFillSection == NULL) )
				firstZeroFillSection = curSection;
			if ( !curSection->fAllZeroFill && (firstZeroFillSection != NULL) && finalLinkedImage ) 
				throwf("zero-fill section %s not at end of segment", curSection->fSectionName);
			
			// update running pointers
			address += curSection->fSize;
			fileOffset += curSection->fSize;
			
			// update segment info
			curSegment->fFileSize = fileOffset - curSegment->fFileOffset;
			curSegment->fSize = curSegment->fFileSize;
			prevSection = curSection;
		}
		
		if ( fOptions.outputKind() == Options::kObjectFile ) {
			// don't page align .o files
		}
		else {
			// optimize trailing zero-fill sections to not occupy disk space
			if ( firstZeroFillSection != NULL ) {
				curSegment->fFileSize = firstZeroFillSection->fFileOffset - curSegment->fFileOffset;
				fileOffset = firstZeroFillSection->fFileOffset;
			}
			// page align segment size
			curSegment->fFileSize = (curSegment->fFileSize+4095) & (-4096);
			curSegment->fSize	  = (curSegment->fSize+4095) & (-4096);
			if ( curSegment->fBaseAddress == nextContiguousAddress )
				nextContiguousAddress = (curSegment->fBaseAddress+curSegment->fSize+4095) & (-4096);
		}
	}

	// check for segment overlaps caused by user specified fixed segments (e.g. __PAGEZERO, __UNIXSTACK)
	if ( haveFixedSegments ) {
		int segCount = fSegmentInfos.size();
		
		for(int i=0; i < segCount; ++i) {
			SegmentInfo* segment1 = fSegmentInfos[i];
			
			for(int j=0; j < segCount; ++j) {
				if ( i != j ) {
					SegmentInfo* segment2 = fSegmentInfos[j];
					
					if ( segment1->fBaseAddress < segment2->fBaseAddress ) {
						if ( (segment1->fBaseAddress+segment1->fSize) > segment2->fBaseAddress )
							throwf("segments overlap: %s (0x%08llX + 0x%08llX) and %s (0x%08llX + 0x%08llX)",
								segment1->fName, segment1->fBaseAddress, segment1->fSize, segment2->fName, segment2->fBaseAddress, segment2->fSize);
					}
					else if ( segment1->fBaseAddress > segment2->fBaseAddress ) {
						if ( (segment2->fBaseAddress+segment2->fSize) > segment1->fBaseAddress )
							throwf("segments overlap: %s (0x%08llX + 0x%08llX) and %s (0x%08llX + 0x%08llX)",
								segment1->fName, segment1->fBaseAddress, segment1->fSize, segment2->fName, segment2->fBaseAddress, segment2->fSize);
					}
					else {
							throwf("segments overlap: %s (0x%08llX + 0x%08llX) and %s (0x%08llX + 0x%08llX)",
								segment1->fName, segment1->fBaseAddress, segment1->fSize, segment2->fName, segment2->fBaseAddress, segment2->fSize);
					}
				}
			}
		}
	}
}

template <typename A>
void Writer<A>::adjustLinkEditSections()
{
	// link edit content is always in last segment
	SegmentInfo* lastSeg = fSegmentInfos[fSegmentInfos.size()-1];
	unsigned int firstLinkEditSectionIndex = 0;
	while ( strcmp(lastSeg->fSections[firstLinkEditSectionIndex]->fSegmentName, "__LINKEDIT") != 0 )
		++firstLinkEditSectionIndex;

	const unsigned int sectionCount = lastSeg->fSections.size();
	uint64_t fileOffset = lastSeg->fSections[firstLinkEditSectionIndex]->fFileOffset;
	uint64_t address = lastSeg->fSections[firstLinkEditSectionIndex]->getBaseAddress();
	for (unsigned int i=firstLinkEditSectionIndex; i < sectionCount; ++i) {
		std::vector<class ObjectFile::Atom*>& atoms = lastSeg->fSections[i]->fAtoms;
		const unsigned int atomCount = atoms.size();
		uint64_t sectionOffset = 0;
		lastSeg->fSections[i]->fFileOffset = fileOffset;
		lastSeg->fSections[i]->setBaseAddress(address);
		for (unsigned int j=0; j < atomCount; ++j) {
			ObjectFile::Atom* atom = atoms[j];
			uint64_t alignment = 1 << atom->getAlignment();
			sectionOffset = ( (sectionOffset+alignment-1) & (-alignment) );
			atom->setSectionOffset(sectionOffset);
			uint64_t size = atom->getSize();
			sectionOffset += size;
			if ( size > fLargestAtomSize )
				fLargestAtomSize = size;
		}
		lastSeg->fSections[i]->fSize = sectionOffset;
		fileOffset += sectionOffset;
		address += sectionOffset;
	}
	if ( fOptions.outputKind() == Options::kObjectFile ) {
		//lastSeg->fBaseAddress = 0;
		//lastSeg->fSize = lastSeg->fSections[firstLinkEditSectionIndex]->
		//lastSeg->fFileOffset = 0;
		//lastSeg->fFileSize =
	}
	else {
		lastSeg->fFileSize = fileOffset - lastSeg->fFileOffset;
		lastSeg->fSize     = (address - lastSeg->fBaseAddress+4095) & (-4096);
	}
}


template <typename A>
ObjectFile::Atom::Scope MachHeaderAtom<A>::getScope() const
{
	switch ( fWriter.fOptions.outputKind() ) {
		case Options::kDynamicExecutable:
		case Options::kStaticExecutable:
			return ObjectFile::Atom::scopeGlobal;
		case Options::kDynamicLibrary:
		case Options::kDynamicBundle:
		case Options::kDyld:
		case Options::kObjectFile:
			return ObjectFile::Atom::scopeLinkageUnit;
	}
	throw "unknown header type";
}

template <typename A>
ObjectFile::Atom::SymbolTableInclusion MachHeaderAtom<A>::getSymbolTableInclusion() const
{
	switch ( fWriter.fOptions.outputKind() ) {
		case Options::kDynamicExecutable:
		case Options::kStaticExecutable:
			return ObjectFile::Atom::kSymbolTableInAndNeverStrip;
		case Options::kDynamicLibrary:
		case Options::kDynamicBundle:
		case Options::kDyld:
			return ObjectFile::Atom::kSymbolTableIn;
		case Options::kObjectFile:
			return ObjectFile::Atom::kSymbolTableNotIn;
	}
	throw "unknown header type";
}

template <typename A>
const char* MachHeaderAtom<A>::getName() const
{
	switch ( fWriter.fOptions.outputKind() ) {
		case Options::kDynamicExecutable:
		case Options::kStaticExecutable:
			return "__mh_execute_header";
		case Options::kDynamicLibrary:
			return "__mh_dylib_header";
		case Options::kDynamicBundle:
			return "__mh_bundle_header";
		case Options::kObjectFile:
			return NULL;
		case Options::kDyld:
			return "__mh_dylinker_header";
	}
	throw "unknown header type";
}

template <typename A>
const char* MachHeaderAtom<A>::getDisplayName() const
{
	switch ( fWriter.fOptions.outputKind() ) {
		case Options::kDynamicExecutable:
		case Options::kStaticExecutable:
		case Options::kDynamicLibrary:
		case Options::kDynamicBundle:
		case Options::kDyld:
			return this->getName();
		case Options::kObjectFile:
			return "mach header";
	}
	throw "unknown header type";
}

template <typename A>
void MachHeaderAtom<A>::copyRawContent(uint8_t buffer[]) const
{
	// get file type
	uint32_t fileType = 0;
	switch ( fWriter.fOptions.outputKind() ) {
		case Options::kDynamicExecutable:
		case Options::kStaticExecutable:
			fileType = MH_EXECUTE;
			break;
		case Options::kDynamicLibrary:
			fileType = MH_DYLIB;
			break;
		case Options::kDynamicBundle:
			fileType = MH_BUNDLE;
			break;
		case Options::kObjectFile:
			fileType = MH_OBJECT;
			break;
		case Options::kDyld:
			fileType = MH_DYLINKER;
			break;
	}

	// get flags
	uint32_t flags = 0;
	if ( fWriter.fOptions.outputKind() == Options::kObjectFile ) {
		if ( ! fWriter.fSeenFollowOnReferences )
			flags = MH_SUBSECTIONS_VIA_SYMBOLS;
	}
	else {
		if ( fWriter.fOptions.outputKind() == Options::kStaticExecutable ) {
			flags |= MH_NOUNDEFS;
		}
		else {
			flags = MH_DYLDLINK;
			if ( fWriter.fOptions.bindAtLoad() )
				flags |= MH_BINDATLOAD;
			switch ( fWriter.fOptions.nameSpace() ) {
				case Options::kTwoLevelNameSpace:
					flags |= MH_TWOLEVEL | MH_NOUNDEFS;
					break;
				case Options::kFlatNameSpace:
					break;
				case Options::kForceFlatNameSpace:
					flags |= MH_FORCE_FLAT;
					break;
			}
			if ( fWriter.fHasWeakExports )
				flags |= MH_WEAK_DEFINES;
			if ( fWriter.fReferencesWeakImports || fWriter.fHasWeakExports )
				flags |= MH_BINDS_TO_WEAK;
		}
		if ( fWriter.fOptions.hasExecutableStack() )
			flags |= MH_ALLOW_STACK_EXECUTION;
	}

	// get commands info
	uint32_t commandsSize = 0;
	uint32_t commandsCount = 0;

	std::vector<class ObjectFile::Atom*>& loadCommandAtoms = fWriter.fLoadCommandsSection->fAtoms;
	const unsigned int atomCount = loadCommandAtoms.size();
	for (unsigned int i=0; i < atomCount; ++i) {
		ObjectFile::Atom* atom = loadCommandAtoms[i];
		commandsSize += atom->getSize();
		// segment and symbol table atoms can contain more than one load command
		if ( atom == fWriter.fSegmentCommands )
			commandsCount += fWriter.fSegmentCommands->commandCount();
		else if ( atom == fWriter.fSymbolTableCommands )
			commandsCount += fWriter.fSymbolTableCommands->commandCount();
		else if ( atom->getSize() != 0)
			++commandsCount;
	}

	// fill out mach_header
	macho_header<typename A::P>* mh = (macho_header<typename A::P>*)buffer;
	setHeaderInfo(*mh);
	mh->set_filetype(fileType);
	mh->set_ncmds(commandsCount);
	mh->set_sizeofcmds(commandsSize);
	mh->set_flags(flags);
}

template <>
void MachHeaderAtom<ppc>::setHeaderInfo(macho_header<ppc::P>& header) const
{
	header.set_magic(MH_MAGIC);
	header.set_cputype(CPU_TYPE_POWERPC);
	header.set_cpusubtype(CPU_SUBTYPE_POWERPC_ALL);
}

template <>
void MachHeaderAtom<ppc64>::setHeaderInfo(macho_header<ppc64::P>& header) const
{
	header.set_magic(MH_MAGIC_64);
	header.set_cputype(CPU_TYPE_POWERPC64);
	header.set_cpusubtype(CPU_SUBTYPE_POWERPC_ALL);
	header.set_reserved(0);
}

template <>
void MachHeaderAtom<x86>::setHeaderInfo(macho_header<x86::P>& header) const
{
	header.set_magic(MH_MAGIC);
	header.set_cputype(CPU_TYPE_I386);
	header.set_cpusubtype(CPU_SUBTYPE_I386_ALL);
}


template <typename A>
CustomStackAtom<A>::CustomStackAtom(Writer<A>& writer)
 : WriterAtom<A>(writer, Segment::fgStackSegment)
{
	if ( stackGrowsDown() )
		Segment::fgStackSegment.setBaseAddress(writer.fOptions.customStackAddr() - writer.fOptions.customStackSize());
	else
		Segment::fgStackSegment.setBaseAddress(writer.fOptions.customStackAddr());
}


template <>
bool CustomStackAtom<ppc>::stackGrowsDown()
{
	return true;
}

template <>
bool CustomStackAtom<ppc64>::stackGrowsDown()
{
	return true;
}

template <>
bool CustomStackAtom<x86>::stackGrowsDown()
{
	return true;
}


template <typename A>
void SegmentLoadCommandsAtom<A>::computeSize()
{
	uint64_t size = 0;
	std::vector<SegmentInfo*>& segmentInfos = fWriter.fSegmentInfos;
	const int segCount = segmentInfos.size();
	for(int i=0; i < segCount; ++i) {
		size += sizeof(macho_segment_command<P>);
		std::vector<SectionInfo*>& sectionInfos = segmentInfos[i]->fSections;
		const int sectionCount = sectionInfos.size();
		for(int j=0; j < sectionCount; ++j) {
			if ( fWriter.fEmitVirtualSections || ! sectionInfos[j]->fVirtualSection )
				size += sizeof(macho_section<P>);
		}
	}
	fSize = size;
	fCommandCount = segCount;
}

template <>
uint64_t LoadCommandAtom<ppc>::alignedSize(uint64_t size)
{
	return ((size+3) & (-4));	// 4-byte align all load commands for 32-bit mach-o
}

template <>
uint64_t LoadCommandAtom<ppc64>::alignedSize(uint64_t size)
{
	return ((size+7) & (-8));	// 8-byte align all load commands for 64-bit mach-o
}

template <>
uint64_t LoadCommandAtom<x86>::alignedSize(uint64_t size)
{
	return ((size+3) & (-4));	// 4-byte align all load commands for 32-bit mach-o
}



template <typename A>
void SegmentLoadCommandsAtom<A>::copyRawContent(uint8_t buffer[]) const
{
	uint64_t size = this->getSize();
	const bool oneSegment =( fWriter.fOptions.outputKind() == Options::kObjectFile );
	bzero(buffer, size);
	uint8_t* p = buffer;
	typename std::vector<SegmentInfo*>& segmentInfos = fWriter.fSegmentInfos;
	const int segCount = segmentInfos.size();
	for(int i=0; i < segCount; ++i) {
		SegmentInfo* segInfo = segmentInfos[i];
		const int sectionCount = segInfo->fSections.size();
		macho_segment_command<P>* cmd = (macho_segment_command<P>*)p;
		cmd->set_cmd(macho_segment_command<P>::CMD);
		cmd->set_segname(segInfo->fName);
		cmd->set_vmaddr(segInfo->fBaseAddress);
		cmd->set_vmsize(segInfo->fSize);
		cmd->set_fileoff(segInfo->fFileOffset);
		cmd->set_filesize(segInfo->fFileSize);
		cmd->set_maxprot(segInfo->fMaxProtection);
		cmd->set_initprot(segInfo->fInitProtection);
		// add sections array
		macho_section<P>* const sections = (macho_section<P>*)&p[sizeof(macho_segment_command<P>)];
		unsigned int sectionsEmitted = 0;
		for (int j=0; j < sectionCount; ++j) {
			SectionInfo* sectInfo = segInfo->fSections[j];
			if ( fWriter.fEmitVirtualSections || !sectInfo->fVirtualSection ) {
				macho_section<P>* sect = &sections[sectionsEmitted++];
				if ( oneSegment ) {
					// .o file segment does not cover load commands, so recalc at first real section
					if ( sectionsEmitted == 1 ) {
						cmd->set_vmaddr(sectInfo->getBaseAddress());
						cmd->set_fileoff(sectInfo->fFileOffset);
					}
					cmd->set_filesize((sectInfo->fFileOffset+sectInfo->fSize)-cmd->fileoff());
					cmd->set_vmsize(sectInfo->getBaseAddress() + sectInfo->fSize);
				}
				sect->set_sectname(sectInfo->fSectionName);
				sect->set_segname(sectInfo->fSegmentName);
				sect->set_addr(sectInfo->getBaseAddress());
				sect->set_size(sectInfo->fSize);
				sect->set_offset(sectInfo->fFileOffset);
				sect->set_align(sectInfo->fAlignment);
				if ( sectInfo->fRelocCount != 0 ) {
					sect->set_reloff(sectInfo->fRelocOffset * sizeof(macho_relocation_info<P>) + fWriter.fSectionRelocationsAtom->getFileOffset());
					sect->set_nreloc(sectInfo->fRelocCount);
				}
				if ( sectInfo->fAllZeroFill ) {
					sect->set_flags(S_ZEROFILL);
					sect->set_offset(0);
				}
				else if ( sectInfo->fAllLazyPointers ) {
					sect->set_flags(S_LAZY_SYMBOL_POINTERS);
					sect->set_reserved1(sectInfo->fIndirectSymbolOffset);
				}
				else if ( sectInfo->fAllNonLazyPointers ) {
					sect->set_flags(S_NON_LAZY_SYMBOL_POINTERS);
					sect->set_reserved1(sectInfo->fIndirectSymbolOffset);
				}
				else if ( sectInfo->fAllStubs ) {
					sect->set_flags(S_SYMBOL_STUBS);
					sect->set_reserved1(sectInfo->fIndirectSymbolOffset);
					sect->set_reserved2(sectInfo->fSize / sectInfo->fAtoms.size());
				}
				else if ( sectInfo->fAllSelfModifyingStubs ) {
					sect->set_flags(S_SYMBOL_STUBS | S_ATTR_SELF_MODIFYING_CODE);
					sect->set_reserved1(sectInfo->fIndirectSymbolOffset);
					sect->set_reserved2(sectInfo->fSize / sectInfo->fAtoms.size());
				}
				else if ( (strcmp(sectInfo->fSectionName, "__mod_init_func") == 0) && (strcmp(sectInfo->fSegmentName, "__DATA") == 0) ) {
					sect->set_flags(S_MOD_INIT_FUNC_POINTERS);
				}
				else if ( (strcmp(sectInfo->fSectionName, "__mod_term_func") == 0) && (strcmp(sectInfo->fSegmentName, "__DATA") == 0) ) {
					sect->set_flags(S_MOD_TERM_FUNC_POINTERS);
				}
				else if ( (strcmp(sectInfo->fSectionName, "__eh_frame") == 0) && (strcmp(sectInfo->fSegmentName, "__TEXT") == 0) ) {
					sect->set_flags(S_COALESCED);
				}
				else if ( (strcmp(sectInfo->fSectionName, "__textcoal_nt") == 0) && (strcmp(sectInfo->fSegmentName, "__TEXT") == 0) ) {
					sect->set_flags(S_COALESCED);
				}
				else if ( (strcmp(sectInfo->fSectionName, "__const_coal") == 0) && (strcmp(sectInfo->fSegmentName, "__DATA") == 0) ) {
					sect->set_flags(S_COALESCED);
				}
				else if ( (strcmp(sectInfo->fSectionName, "__interpose") == 0) && (strcmp(sectInfo->fSegmentName, "__DATA") == 0) ) {
					sect->set_flags(S_INTERPOSING);
				}
				else if ( (strcmp(sectInfo->fSectionName, "__cstring") == 0) && (strcmp(sectInfo->fSegmentName, "__TEXT") == 0) ) {
					sect->set_flags(S_CSTRING_LITERALS);
				}
				else if ( (strcmp(sectInfo->fSectionName, "__literal4") == 0) && (strcmp(sectInfo->fSegmentName, "__TEXT") == 0) ) {
					sect->set_flags(S_4BYTE_LITERALS);
				}
				else if ( (strcmp(sectInfo->fSectionName, "__literal8") == 0) && (strcmp(sectInfo->fSegmentName, "__TEXT") == 0) ) {
					sect->set_flags(S_8BYTE_LITERALS);
				}
				else if ( (strcmp(sectInfo->fSectionName, "__message_refs") == 0) && (strcmp(sectInfo->fSegmentName, "__OBJC") == 0) ) {
					sect->set_flags(S_LITERAL_POINTERS);
				}
			}
		}
		p = &p[sizeof(macho_segment_command<P>) + sectionsEmitted*sizeof(macho_section<P>)];
		cmd->set_cmdsize(sizeof(macho_segment_command<P>) + sectionsEmitted*sizeof(macho_section<P>));
		cmd->set_nsects(sectionsEmitted);
	}
}


template <typename A>
SymbolTableLoadCommandsAtom<A>::SymbolTableLoadCommandsAtom(Writer<A>& writer)
 : LoadCommandAtom<A>(writer, Segment::fgTextSegment)
{
	bzero(&fSymbolTable, sizeof(macho_symtab_command<P>));
	bzero(&fDynamicSymbolTable, sizeof(macho_dysymtab_command<P>));
	writer.fSymbolTableCommands = this;
}

template <typename A>
uint64_t SymbolTableLoadCommandsAtom<A>::getSize() const
{
	if ( fWriter.fOptions.outputKind() == Options::kStaticExecutable )
		return this->alignedSize(sizeof(macho_symtab_command<P>));
	else
		return this->alignedSize(sizeof(macho_symtab_command<P>) + sizeof(macho_dysymtab_command<P>));
}

template <typename A>
void SymbolTableLoadCommandsAtom<A>::copyRawContent(uint8_t buffer[]) const
{
	// build LC_DYSYMTAB command
	macho_symtab_command<P>*   symbolTableCmd = (macho_symtab_command<P>*)buffer;
	bzero(symbolTableCmd, sizeof(macho_symtab_command<P>));
	symbolTableCmd->set_cmd(LC_SYMTAB);
	symbolTableCmd->set_cmdsize(sizeof(macho_symtab_command<P>));
	symbolTableCmd->set_nsyms(fWriter.fSymbolTableCount);
	symbolTableCmd->set_symoff(fWriter.fSymbolTableAtom->getFileOffset());
	symbolTableCmd->set_stroff(fWriter.fStringsAtom->getFileOffset());
	symbolTableCmd->set_strsize(fWriter.fStringsAtom->getSize());

	// build LC_DYSYMTAB command
	if ( fWriter.fOptions.outputKind() != Options::kStaticExecutable ) {
		macho_dysymtab_command<P>* dynamicSymbolTableCmd = (macho_dysymtab_command<P>*)&buffer[sizeof(macho_symtab_command<P>)];
		bzero(dynamicSymbolTableCmd, sizeof(macho_dysymtab_command<P>));
		dynamicSymbolTableCmd->set_cmd(LC_DYSYMTAB);
		dynamicSymbolTableCmd->set_cmdsize(sizeof(macho_dysymtab_command<P>));
		dynamicSymbolTableCmd->set_ilocalsym(fWriter.fSymbolTableStabsStartIndex);
		dynamicSymbolTableCmd->set_nlocalsym(fWriter.fSymbolTableStabsCount + fWriter.fSymbolTableLocalCount);
		dynamicSymbolTableCmd->set_iextdefsym(fWriter.fSymbolTableExportStartIndex);
		dynamicSymbolTableCmd->set_nextdefsym(fWriter.fSymbolTableExportCount);
		dynamicSymbolTableCmd->set_iundefsym(fWriter.fSymbolTableImportStartIndex);
		dynamicSymbolTableCmd->set_nundefsym(fWriter.fSymbolTableImportCount);
		dynamicSymbolTableCmd->set_indirectsymoff(fWriter.fIndirectTableAtom->getFileOffset());
		dynamicSymbolTableCmd->set_nindirectsyms(fWriter.fIndirectTableAtom->fTable.size());
		if ( fWriter.fOptions.outputKind() != Options::kObjectFile ) {
			dynamicSymbolTableCmd->set_extreloff((fWriter.fExternalRelocs.size()==0) ? 0 : fWriter.fExternalRelocationsAtom->getFileOffset());
			dynamicSymbolTableCmd->set_nextrel(fWriter.fExternalRelocs.size());
			dynamicSymbolTableCmd->set_locreloff((fWriter.fInternalRelocs.size()==0) ? 0 : fWriter.fLocalRelocationsAtom->getFileOffset());
			dynamicSymbolTableCmd->set_nlocrel(fWriter.fInternalRelocs.size());
		}
	}
}

template <typename A>
unsigned int SymbolTableLoadCommandsAtom<A>::commandCount()
{
	return (fWriter.fOptions.outputKind() == Options::kStaticExecutable) ? 1 : 2;
}

template <typename A>
uint64_t DyldLoadCommandsAtom<A>::getSize() const
{
	return this->alignedSize(sizeof(macho_dylinker_command<P>) + strlen("/usr/lib/dyld") + 1);
}

template <typename A>
void DyldLoadCommandsAtom<A>::copyRawContent(uint8_t buffer[]) const
{
	uint64_t size = this->getSize();
	bzero(buffer, size);
	macho_dylinker_command<P>* cmd = (macho_dylinker_command<P>*)buffer;
	if ( fWriter.fOptions.outputKind() == Options::kDyld )
		cmd->set_cmd(LC_ID_DYLINKER);
	else
		cmd->set_cmd(LC_LOAD_DYLINKER);
	cmd->set_cmdsize(this->getSize());
	cmd->set_name_offset();
	strcpy((char*)&buffer[sizeof(macho_dylinker_command<P>)], "/usr/lib/dyld");
}

template <typename A>
uint64_t AllowableClientLoadCommandsAtom<A>::getSize() const
{
	return this->alignedSize(sizeof(macho_sub_client_command<P>) + strlen(this->clientString) + 1);
}

template <typename A>
void AllowableClientLoadCommandsAtom<A>::copyRawContent(uint8_t buffer[]) const
{
	uint64_t size = this->getSize();

	bzero(buffer, size);
	macho_sub_client_command<P>* cmd = (macho_sub_client_command<P>*)buffer;
	cmd->set_cmd(LC_SUB_CLIENT);
	cmd->set_cmdsize(size);
	cmd->set_client_offset();
	strcpy((char*)&buffer[sizeof(macho_sub_client_command<P>)], this->clientString);

}

template <typename A>
uint64_t DylibLoadCommandsAtom<A>::getSize() const
{
	const char* path = fInfo.reader->getInstallPath();
	if ( fInfo.options.fInstallPathOverride != NULL )
		path = fInfo.options.fInstallPathOverride;
	return this->alignedSize(sizeof(macho_dylib_command<P>) + strlen(path) + 1);
}

template <typename A>
void DylibLoadCommandsAtom<A>::copyRawContent(uint8_t buffer[]) const
{
	uint64_t size = this->getSize();
	bzero(buffer, size);
	const char* path = fInfo.reader->getInstallPath();
	if ( fInfo.options.fInstallPathOverride != NULL )
		path = fInfo.options.fInstallPathOverride;
	macho_dylib_command<P>* cmd = (macho_dylib_command<P>*)buffer;
	if ( fInfo.options.fWeakImport )
		cmd->set_cmd(LC_LOAD_WEAK_DYLIB);
	else
		cmd->set_cmd(LC_LOAD_DYLIB);
	cmd->set_cmdsize(this->getSize());
	cmd->set_timestamp(fInfo.reader->getTimestamp());
	cmd->set_current_version(fInfo.reader->getCurrentVersion());
	cmd->set_compatibility_version(fInfo.reader->getCompatibilityVersion());
	cmd->set_name_offset();
	strcpy((char*)&buffer[sizeof(macho_dylib_command<P>)], path);
}



template <typename A>
uint64_t DylibIDLoadCommandsAtom<A>::getSize() const
{
	return this->alignedSize(sizeof(macho_dylib_command<P>) + strlen(fWriter.fOptions.installPath()) + 1);
}

template <typename A>
void DylibIDLoadCommandsAtom<A>::copyRawContent(uint8_t buffer[]) const
{
	struct timeval currentTime = { 0 , 0 };
	gettimeofday(&currentTime, NULL);
	time_t timestamp = currentTime.tv_sec;
	uint64_t size = this->getSize();
	bzero(buffer, size);
	macho_dylib_command<P>* cmd = (macho_dylib_command<P>*)buffer;
	cmd->set_cmd(LC_ID_DYLIB);
	cmd->set_cmdsize(this->getSize());
	cmd->set_name_offset();
	cmd->set_timestamp(timestamp);
	cmd->set_current_version(fWriter.fOptions.currentVersion());
	cmd->set_compatibility_version(fWriter.fOptions.compatibilityVersion());
	strcpy((char*)&buffer[sizeof(macho_dylib_command<P>)], fWriter.fOptions.installPath());
}


template <typename A>
void RoutinesLoadCommandsAtom<A>::copyRawContent(uint8_t buffer[]) const
{
	uint64_t initAddr = fWriter.getAtomLoadAddress(fWriter.fEntryPoint);
	bzero(buffer, sizeof(macho_routines_command<P>));
	macho_routines_command<P>* cmd = (macho_routines_command<P>*)buffer;
	cmd->set_cmd(macho_routines_command<P>::CMD);
	cmd->set_cmdsize(this->getSize());
	cmd->set_init_address(initAddr);
}


template <typename A>
uint64_t SubUmbrellaLoadCommandsAtom<A>::getSize() const
{
	return this->alignedSize(sizeof(macho_sub_umbrella_command<P>) + strlen(fName) + 1);
}

template <typename A>
void SubUmbrellaLoadCommandsAtom<A>::copyRawContent(uint8_t buffer[]) const
{
	uint64_t size = this->getSize();
	bzero(buffer, size);
	macho_sub_umbrella_command<P>* cmd = (macho_sub_umbrella_command<P>*)buffer;
	cmd->set_cmd(LC_SUB_UMBRELLA);
	cmd->set_cmdsize(this->getSize());
	cmd->set_sub_umbrella_offset();
	strcpy((char*)&buffer[sizeof(macho_sub_umbrella_command<P>)], fName);
}

template <typename A>
void UUIDLoadCommandAtom<A>::copyRawContent(uint8_t buffer[]) const
{
	if (fEmit) {
		uint64_t size = this->getSize();
		bzero(buffer, size);
		macho_uuid_command<P>* cmd = (macho_uuid_command<P>*)buffer;
		cmd->set_cmd(LC_UUID);
		cmd->set_cmdsize(this->getSize());
		cmd->set_uuid((uint8_t*)fUUID);
	}
}

template <typename A>
uint64_t SubLibraryLoadCommandsAtom<A>::getSize() const
{
	return this->alignedSize(sizeof(macho_sub_library_command<P>) + fNameLength + 1);
}

template <typename A>
void SubLibraryLoadCommandsAtom<A>::copyRawContent(uint8_t buffer[]) const
{
	uint64_t size = this->getSize();
	bzero(buffer, size);
	macho_sub_library_command<P>* cmd = (macho_sub_library_command<P>*)buffer;
	cmd->set_cmd(LC_SUB_LIBRARY);
	cmd->set_cmdsize(this->getSize());
	cmd->set_sub_library_offset();
	strncpy((char*)&buffer[sizeof(macho_sub_library_command<P>)], fNameStart, fNameLength);
	buffer[sizeof(macho_sub_library_command<P>)+fNameLength] = '\0';
}

template <typename A>
uint64_t UmbrellaLoadCommandsAtom<A>::getSize() const
{
	return this->alignedSize(sizeof(macho_sub_framework_command<P>) + strlen(fName) + 1);
}

template <typename A>
void UmbrellaLoadCommandsAtom<A>::copyRawContent(uint8_t buffer[]) const
{
	uint64_t size = this->getSize();
	bzero(buffer, size);
	macho_sub_framework_command<P>* cmd = (macho_sub_framework_command<P>*)buffer;
	cmd->set_cmd(LC_SUB_FRAMEWORK);
	cmd->set_cmdsize(this->getSize());
	cmd->set_umbrella_offset();
	strcpy((char*)&buffer[sizeof(macho_sub_framework_command<P>)], fName);
}

template <>
uint64_t ThreadsLoadCommandsAtom<ppc>::getSize() const
{
	return this->alignedSize(16 + 40*4);	// base size + PPC_THREAD_STATE_COUNT * 4
}

template <>
uint64_t ThreadsLoadCommandsAtom<ppc64>::getSize() const
{
	return this->alignedSize(16 + 76*4);	// base size + PPC_THREAD_STATE64_COUNT * 4
}

template <>
uint64_t ThreadsLoadCommandsAtom<x86>::getSize() const
{
	return this->alignedSize(16 + 16*4);	// base size + i386_THREAD_STATE_COUNT * 4
}


template <>
void ThreadsLoadCommandsAtom<ppc>::copyRawContent(uint8_t buffer[]) const
{
	uint64_t size = this->getSize();
	uint64_t start = fWriter.getAtomLoadAddress(fWriter.fEntryPoint);
	bzero(buffer, size);
	macho_thread_command<ppc::P>* cmd = (macho_thread_command<ppc::P>*)buffer;
	cmd->set_cmd(LC_UNIXTHREAD);
	cmd->set_cmdsize(size);
	cmd->set_flavor(1);				// PPC_THREAD_STATE
	cmd->set_count(40);				// PPC_THREAD_STATE_COUNT;
	cmd->set_thread_register(0, start);
	if ( fWriter.fOptions.hasCustomStack() )
		cmd->set_thread_register(3, fWriter.fOptions.customStackAddr());	// r1
}


template <>
void ThreadsLoadCommandsAtom<ppc64>::copyRawContent(uint8_t buffer[]) const
{
	uint64_t size = this->getSize();
	uint64_t start = fWriter.getAtomLoadAddress(fWriter.fEntryPoint);
	bzero(buffer, size);
	macho_thread_command<ppc64::P>* cmd = (macho_thread_command<ppc64::P>*)buffer;
	cmd->set_cmd(LC_UNIXTHREAD);
	cmd->set_cmdsize(size);
	cmd->set_flavor(5);				// PPC_THREAD_STATE64
	cmd->set_count(76);				// PPC_THREAD_STATE64_COUNT;
	cmd->set_thread_register(0, start);
	if ( fWriter.fOptions.hasCustomStack() )
		cmd->set_thread_register(6, fWriter.fOptions.customStackAddr());	// r1
}

template <>
void ThreadsLoadCommandsAtom<x86>::copyRawContent(uint8_t buffer[]) const
{
	uint64_t size = this->getSize();
	uint64_t start = fWriter.getAtomLoadAddress(fWriter.fEntryPoint);
	bzero(buffer, size);
	macho_thread_command<x86::P>* cmd = (macho_thread_command<x86::P>*)buffer;
	cmd->set_cmd(LC_UNIXTHREAD);
	cmd->set_cmdsize(size);
	cmd->set_flavor(1);				// i386_THREAD_STATE
	cmd->set_count(16);				// i386_THREAD_STATE_COUNT;
	cmd->set_thread_register(10, start);
	if ( fWriter.fOptions.hasCustomStack() )
		cmd->set_thread_register(15, fWriter.fOptions.customStackAddr());	// uesp
}




template <typename A>
void LoadCommandsPaddingAtom<A>::copyRawContent(uint8_t buffer[]) const
{
	bzero(buffer, fSize);
}

template <typename A>
void LoadCommandsPaddingAtom<A>::setSize(uint64_t newSize) 
{ 
	fSize = newSize; 
	// this resizing by-passes the way fLargestAtomSize is set, so re-check here
	if ( fWriter.fLargestAtomSize < newSize )
		fWriter.fLargestAtomSize = newSize;
}

template <typename A>
uint64_t LinkEditAtom<A>::getFileOffset() const
{
	return ((SectionInfo*)this->getSection())->fFileOffset + this->getSectionOffset();
}


template <typename A>
uint64_t SectionRelocationsLinkEditAtom<A>::getSize() const
{
	return fWriter.fSectionRelocs.size() * sizeof(macho_relocation_info<P>);
}

template <typename A>
void SectionRelocationsLinkEditAtom<A>::copyRawContent(uint8_t buffer[]) const
{
	memcpy(buffer, &fWriter.fSectionRelocs[0], this->getSize());
}


template <typename A>
uint64_t LocalRelocationsLinkEditAtom<A>::getSize() const
{
	return fWriter.fInternalRelocs.size() * sizeof(macho_relocation_info<P>);
}

template <typename A>
void LocalRelocationsLinkEditAtom<A>::copyRawContent(uint8_t buffer[]) const
{
	memcpy(buffer, &fWriter.fInternalRelocs[0], this->getSize());
}



template <typename A>
uint64_t SymbolTableLinkEditAtom<A>::getSize() const
{
	return fWriter.fSymbolTableCount * sizeof(macho_nlist<P>);
}

template <typename A>
void SymbolTableLinkEditAtom<A>::copyRawContent(uint8_t buffer[]) const
{
	memcpy(buffer, fWriter.fSymbolTable, this->getSize());
}

template <typename A>
uint64_t ExternalRelocationsLinkEditAtom<A>::getSize() const
{
	return fWriter.fExternalRelocs.size() * sizeof(macho_relocation_info<P>);
}

template <typename A>
void ExternalRelocationsLinkEditAtom<A>::copyRawContent(uint8_t buffer[]) const
{
	memcpy(buffer, &fWriter.fExternalRelocs[0], this->getSize());
}



template <typename A>
uint64_t IndirectTableLinkEditAtom<A>::getSize() const
{
	return fTable.size() * sizeof(uint32_t);
}

template <typename A>
void IndirectTableLinkEditAtom<A>::copyRawContent(uint8_t buffer[]) const
{
	uint64_t size = this->getSize();
	bzero(buffer, size);
	const uint32_t indirectTableSize = fTable.size();
	uint32_t* indirectTable = (uint32_t*)buffer;
	for(std::vector<IndirectEntry>::const_iterator it = fTable.begin(); it != fTable.end(); ++it) {
		if ( it->indirectIndex < indirectTableSize ) {
			A::P::E::set32(indirectTable[it->indirectIndex], it->symbolIndex);
		}
		else {
			throwf("malformed indirect table. size=%d, index=%d", indirectTableSize, it->indirectIndex);
		}
	}
}



template <typename A>
StringsLinkEditAtom<A>::StringsLinkEditAtom(Writer<A>& writer)
	: LinkEditAtom<A>(writer), fCurrentBuffer(NULL), fCurrentBufferUsed(0)
{
	fCurrentBuffer = new char[kBufferSize];
	// burn first byte of string pool (so zero is never a valid string offset)
	fCurrentBuffer[fCurrentBufferUsed++] = ' ';
	// make offset 1 always point to an empty string
	fCurrentBuffer[fCurrentBufferUsed++] = '\0';
}

template <typename A>
uint64_t StringsLinkEditAtom<A>::getSize() const
{
	return kBufferSize * fFullBuffers.size() + fCurrentBufferUsed;
}

template <typename A>
void StringsLinkEditAtom<A>::copyRawContent(uint8_t buffer[]) const
{
	uint64_t offset = 0;
	for (unsigned int i=0; i < fFullBuffers.size(); ++i) {
		memcpy(&buffer[offset], fFullBuffers[i], kBufferSize);
		offset += kBufferSize;
	}
	memcpy(&buffer[offset], fCurrentBuffer, fCurrentBufferUsed);
}

template <typename A>
int32_t StringsLinkEditAtom<A>::add(const char* name)
{
	int32_t offset = kBufferSize * fFullBuffers.size() + fCurrentBufferUsed;
	int lenNeeded = strlcpy(&fCurrentBuffer[fCurrentBufferUsed], name, kBufferSize-fCurrentBufferUsed)+1;
	if ( (fCurrentBufferUsed+lenNeeded) < kBufferSize ) {
		fCurrentBufferUsed += lenNeeded;
	}
	else {
		int copied = kBufferSize-fCurrentBufferUsed-1;
		// change trailing '\0' that strlcpy added to real char
		fCurrentBuffer[kBufferSize-1] = name[copied];
		// alloc next buffer
		fFullBuffers.push_back(fCurrentBuffer);
		fCurrentBuffer = new char[kBufferSize];
		fCurrentBufferUsed = 0;
		// append rest of string
		this->add(&name[copied+1]);
	}
	return offset;
}


template <typename A>
int32_t StringsLinkEditAtom<A>::addUnique(const char* name)
{
	StringToOffset::iterator pos = fUniqueStrings.find(name);
	if ( pos != fUniqueStrings.end() ) {
		return pos->second;
	}
	else {
		int32_t offset = this->add(name);
		fUniqueStrings[name] = offset;
		return offset;
	}
}


template <typename A>
BranchIslandAtom<A>::BranchIslandAtom(Writer<A>& writer, const char* name, int islandRegion, ObjectFile::Atom& target, uint32_t targetOffset)
 : WriterAtom<A>(writer, Segment::fgTextSegment), fTarget(target), fTargetOffset(targetOffset)
{
	char* buf = new char[strlen(name)+32];
	if ( targetOffset == 0 ) {
		if ( islandRegion == 0 )
			sprintf(buf, "%s$island", name);
		else
			sprintf(buf, "%s$island_%d", name, islandRegion);
	}
	else {
		sprintf(buf, "%s_plus_%d$island_%d", name, targetOffset, islandRegion);
	}
	fName = buf;
}


template <>
void BranchIslandAtom<ppc>::copyRawContent(uint8_t buffer[]) const
{
	int64_t displacement = fTarget.getAddress() + fTargetOffset - this->getAddress();
	int32_t branchInstruction = 0x48000000 | ((uint32_t)displacement & 0x03FFFFFC);
	OSWriteBigInt32(buffer, 0, branchInstruction);
}

template <>
void BranchIslandAtom<ppc64>::copyRawContent(uint8_t buffer[]) const
{
	int64_t displacement = fTarget.getAddress() + fTargetOffset - this->getAddress();
	int32_t branchInstruction = 0x48000000 | ((uint32_t)displacement & 0x03FFFFFC);
	OSWriteBigInt32(buffer, 0, branchInstruction);
}

template <>
uint64_t BranchIslandAtom<ppc>::getSize() const
{
	return 4;
}

template <>
uint64_t BranchIslandAtom<ppc64>::getSize() const
{
	return 4;
}


template <>
bool StubAtom<ppc64>::pic() const
{
	// no-pic stubs for ppc64 don't work if lazy pointer is above low 2GB.
	// This usually only happens when a large zero-page is requested
	switch ( fWriter.fOptions.outputKind() ) {
		case Options::kDynamicExecutable:
			return (fWriter.fOptions.zeroPageSize() > 4096);
		case Options::kDynamicLibrary:
		case Options::kDynamicBundle:
			return true;
		case Options::kObjectFile:
		case Options::kDyld:
		case Options::kStaticExecutable:
			break;
	}
	throw "internal ld64 error: file type does not use stubs";
}

template <>
bool StubAtom<ppc>::pic() const
{
	return ( fWriter.fOptions.outputKind() != Options::kDynamicExecutable );
}


template <>
StubAtom<ppc>::StubAtom(Writer<ppc>& writer, ObjectFile::Atom& target)
 : WriterAtom<ppc>(writer, Segment::fgTextSegment), fName(stubName(target.getName())), fTarget(target)
{
	writer.fAllSynthesizedStubs.push_back(this);

	LazyPointerAtom<ppc>* lp = new LazyPointerAtom<ppc>(writer, target);
	if ( pic() ) {
		// picbase is 8 bytes into atom
		fReferences.push_back(new WriterReference<ppc>(12, ppc::kPICBaseHigh16, lp, 0, NULL, 8));
		fReferences.push_back(new WriterReference<ppc>(20, ppc::kPICBaseLow16, lp, 0, NULL, 8));
	}
	else {
		fReferences.push_back(new WriterReference<ppc>(0, ppc::kAbsHigh16AddLow, lp));
		fReferences.push_back(new WriterReference<ppc>(4, ppc::kAbsLow16, lp));
	}
}

template <>
StubAtom<ppc64>::StubAtom(Writer<ppc64>& writer, ObjectFile::Atom& target)
 : WriterAtom<ppc64>(writer, Segment::fgTextSegment), fName(stubName(target.getName())), fTarget(target)
{
	writer.fAllSynthesizedStubs.push_back(this);

	LazyPointerAtom<ppc64>* lp = new LazyPointerAtom<ppc64>(writer, target);
	if ( pic() ) {
		// picbase is 8 bytes into atom
		fReferences.push_back(new WriterReference<ppc64>(12, ppc64::kPICBaseHigh16, lp, 0, NULL, 8));
		fReferences.push_back(new WriterReference<ppc64>(20, ppc64::kPICBaseLow14, lp, 0, NULL, 8));
	}
	else {
		fReferences.push_back(new WriterReference<ppc64>(0, ppc64::kAbsHigh16AddLow, lp));
		fReferences.push_back(new WriterReference<ppc64>(4, ppc64::kAbsLow14, lp));
	}
}

// specialize to put x86 fast stub in __IMPORT segment with no lazy pointer
template <>
StubAtom<x86>::StubAtom(Writer<x86>& writer, ObjectFile::Atom& target)
 : WriterAtom<x86>(writer, Segment::fgImportSegment), fName(stubName(target.getName())), fTarget(target)
{
	writer.fAllSynthesizedStubs.push_back(this);
}


template <typename A>
const char* StubAtom<A>::stubName(const char* name)
{
	char* buf;
	asprintf(&buf, "%s$stub", name);
	return buf;
}

template <>
uint64_t StubAtom<ppc>::getSize() const
{
	return ( pic() ? 32 : 16 );
}

template <>
uint64_t StubAtom<ppc64>::getSize() const
{
	return ( pic() ? 32 : 16 );
}

template <>
uint64_t StubAtom<x86>::getSize() const
{
	return 5;
}


template <>
uint8_t StubAtom<x86>::getAlignment() const
{
	// special case x86 fast stubs to be byte aligned
	return 0;
}

template <>
void StubAtom<ppc64>::copyRawContent(uint8_t buffer[]) const
{
	if ( pic() ) {
		OSWriteBigInt32(&buffer [0], 0, 0x7c0802a6);	// 	mflr r0
		OSWriteBigInt32(&buffer[ 4], 0, 0x429f0005);	//  bcl 20,31,Lpicbase
		OSWriteBigInt32(&buffer[ 8], 0, 0x7d6802a6);	// Lpicbase: mflr r11
		OSWriteBigInt32(&buffer[12], 0, 0x3d6b0000);	// 	addis r11,r11,ha16(L_fwrite$lazy_ptr-Lpicbase)
		OSWriteBigInt32(&buffer[16], 0, 0x7c0803a6);	// 	mtlr r0
		OSWriteBigInt32(&buffer[20], 0, 0xe98b0001);	// 	ldu r12,lo16(L_fwrite$lazy_ptr-Lpicbase)(r11)
		OSWriteBigInt32(&buffer[24], 0, 0x7d8903a6);	//  mtctr r12
		OSWriteBigInt32(&buffer[28], 0, 0x4e800420);	//  bctr
	}
	else {
		OSWriteBigInt32(&buffer[ 0], 0, 0x3d600000);	// lis r11,ha16(L_fwrite$lazy_ptr)
		OSWriteBigInt32(&buffer[ 4], 0, 0xe98b0001);	// ldu r12,lo16(L_fwrite$lazy_ptr)(r11)
		OSWriteBigInt32(&buffer[ 8], 0, 0x7d8903a6);	// mtctr r12
		OSWriteBigInt32(&buffer[12], 0, 0x4e800420);	// bctr
	}
}

template <>
void StubAtom<ppc>::copyRawContent(uint8_t buffer[]) const
{
	if ( pic() ) {
		OSWriteBigInt32(&buffer[ 0], 0, 0x7c0802a6);	// 	mflr r0
		OSWriteBigInt32(&buffer[ 4], 0, 0x429f0005);	//  bcl 20,31,Lpicbase
		OSWriteBigInt32(&buffer[ 8], 0, 0x7d6802a6);	// Lpicbase: mflr r11
		OSWriteBigInt32(&buffer[12], 0, 0x3d6b0000);	// 	addis r11,r11,ha16(L_fwrite$lazy_ptr-Lpicbase)
		OSWriteBigInt32(&buffer[16], 0, 0x7c0803a6);	// 	mtlr r0
		OSWriteBigInt32(&buffer[20], 0, 0x858b0000);	// 	lwzu r12,lo16(L_fwrite$lazy_ptr-Lpicbase)(r11)
		OSWriteBigInt32(&buffer[24], 0, 0x7d8903a6);	//  mtctr r12
		OSWriteBigInt32(&buffer[28], 0, 0x4e800420);	//  bctr
	}
	else {
		OSWriteBigInt32(&buffer[ 0], 0, 0x3d600000);	// lis r11,ha16(L_fwrite$lazy_ptr)
		OSWriteBigInt32(&buffer[ 4], 0, 0x858b0000);	// lwzu r12,lo16(L_fwrite$lazy_ptr)(r11)
		OSWriteBigInt32(&buffer[ 8], 0, 0x7d8903a6);	// mtctr r12
		OSWriteBigInt32(&buffer[12], 0, 0x4e800420);	// bctr
	}
}

template <>
void StubAtom<x86>::copyRawContent(uint8_t buffer[]) const
{
	buffer[0] = 0xF4;
	buffer[1] = 0xF4;
	buffer[2] = 0xF4;
	buffer[3] = 0xF4;
	buffer[4] = 0xF4;
}


template <>
const char*	StubAtom<ppc>::getSectionName() const
{
	return ( pic() ? "__picsymbolstub1" : "__symbol_stub1");
}

template <>
const char*	StubAtom<ppc64>::getSectionName() const
{
	return ( pic() ? "__picsymbolstub1" : "__symbol_stub1");
}

template <>
const char*	StubAtom<x86>::getSectionName() const
{
	return "__jump_table";
}





template <typename A>
LazyPointerAtom<A>::LazyPointerAtom(Writer<A>& writer, ObjectFile::Atom& target)
 : WriterAtom<A>(writer, Segment::fgDataSegment), fName(lazyPointerName(target.getName())), fTarget(target)
{
	writer.fAllSynthesizedLazyPointers.push_back(this);

	fReferences.push_back(new WriterReference<A>(0, A::kPointer, &target));
}



template <typename A>
const char* LazyPointerAtom<A>::lazyPointerName(const char* name)
{
	char* buf;
	asprintf(&buf, "%s$lazy_pointer", name);
	return buf;
}

template <typename A>
void LazyPointerAtom<A>::copyRawContent(uint8_t buffer[]) const
{
	bzero(buffer, getSize());
}


template <typename A>
NonLazyPointerAtom<A>::NonLazyPointerAtom(Writer<A>& writer, ObjectFile::Atom& target)
 : WriterAtom<A>(writer, Segment::fgDataSegment), fName(nonlazyPointerName(target.getName())), fTarget(target)
{
	writer.fAllSynthesizedNonLazyPointers.push_back(this);

	fReferences.push_back(new WriterReference<A>(0, A::kPointer, &target));
}

template <typename A>
const char* NonLazyPointerAtom<A>::nonlazyPointerName(const char* name)
{
	char* buf;
	asprintf(&buf, "%s$non_lazy_pointer", name);
	return buf;
}

template <typename A>
void NonLazyPointerAtom<A>::copyRawContent(uint8_t buffer[]) const
{
	bzero(buffer, getSize());
}



}; // namespace executable
}; // namespace mach_o


#endif // __EXECUTABLE_MACH_O__

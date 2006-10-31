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

#ifndef __OBJECT_FILE_MACH_O__
#define __OBJECT_FILE_MACH_O__

#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include <sys/param.h>
#include <mach-o/ppc/reloc.h>
#include <mach-o/stab.h>
#include <mach-o/x86_64/reloc.h>
#ifndef S_ATTR_DEBUG
 #define S_ATTR_DEBUG 0x02000000
#endif

#include <vector>
#include <set>
#include <algorithm>

#include "MachOFileAbstraction.hpp"
#include "Architectures.hpp"
#include "ObjectFile.h"
#include "dwarf2.h"
#include "debugline.h"


//
//
//	To implement architecture xxx, you must write template specializations for the following six methods:
//			Reader<xxx>::validFile()
//			Reader<xxx>::validSectionType()
//			Reader<xxx>::addRelocReference()
//			Reference<xxx>::getDescription()
//
//



extern  __attribute__((noreturn)) void throwf(const char* format, ...);

namespace mach_o {
namespace relocatable {



// forward reference
template <typename A> class Reader;
template <typename A> class SymbolAtomSorter;

struct AtomAndOffset
{
						AtomAndOffset(ObjectFile::Atom* a=NULL) : atom(a), offset(0) {}
						AtomAndOffset(ObjectFile::Atom* a, uint32_t off) : atom(a), offset(off) {}
	ObjectFile::Atom*	atom;
	uint32_t			offset;
};


template <typename A>
class Reference : public ObjectFile::Reference
{
public:
	typedef typename A::P						P;
	typedef typename A::P::uint_t				pint_t;
	typedef typename A::ReferenceKinds			Kinds;

							Reference(Kinds kind, const AtomAndOffset& at, const AtomAndOffset& toTarget);
							Reference(Kinds kind, const AtomAndOffset& at, const AtomAndOffset& fromTarget, const AtomAndOffset& toTarget);
							Reference(Kinds kind, const AtomAndOffset& at, const char* toName, uint32_t toOffset);

	virtual					~Reference() {}


	virtual bool			isTargetUnbound() const							{ return ( fToTarget.atom == NULL ); }
	virtual bool			isFromTargetUnbound() const						{ return ( fFromTarget.atom == NULL ); }
	virtual uint8_t			getKind() const									{ return (uint8_t)fKind; }
	virtual uint64_t		getFixUpOffset() const							{ return fFixUpOffsetInSrc; }
	virtual const char*		getTargetName() const							{ return (fToTargetName != NULL) ? fToTargetName : fToTarget.atom->getName(); }
	virtual ObjectFile::Atom& getTarget() const								{ return *fToTarget.atom; }
	virtual uint64_t		getTargetOffset() const							{ return (int64_t)((int32_t)fToTarget.offset); }
	virtual bool			hasFromTarget() const							{ return ( (fFromTarget.atom != NULL) || (fFromTargetName != NULL) ); }
	virtual ObjectFile::Atom& getFromTarget() const							{ return *fFromTarget.atom; }
	virtual const char*		getFromTargetName() const						{ return (fFromTargetName != NULL) ? fFromTargetName : fFromTarget.atom->getName(); }
	virtual void			setTarget(ObjectFile::Atom& target, uint64_t offset)	{ fToTarget.atom = &target; fToTarget.offset = offset; }
	virtual void			setToTargetOffset(uint64_t offset)				{ fToTarget.offset = offset; }
	virtual void			setFromTarget(ObjectFile::Atom& target)			{ fFromTarget.atom = &target; }
	virtual void			setFromTargetName(const char* name)				{ fFromTargetName = name; }
	virtual void			setFromTargetOffset(uint64_t offset)			{ fFromTarget.offset = offset; }
	virtual const char*		getDescription() const;
	virtual uint64_t		getFromTargetOffset() const						{ return fFromTarget.offset; }


private:
	pint_t					fFixUpOffsetInSrc;
	AtomAndOffset			fToTarget;
	AtomAndOffset			fFromTarget;
	const char*				fToTargetName;
	const char*				fFromTargetName;
	Kinds					fKind;
};


template <typename A>
Reference<A>::Reference(Kinds kind, const AtomAndOffset& at, const AtomAndOffset& toTarget)
 : fFixUpOffsetInSrc(at.offset), fToTarget(toTarget), fToTargetName(NULL), fFromTargetName(NULL),
    fKind(kind)
{
	// make reference a by-name unless:
	// - the reference type is only used with direct references
	// - the target is translation unit scoped
	if ( (kind != A::kNoFixUp) && (kind != A::kFollowOn) 
		&& (toTarget.atom->getScope() != ObjectFile::Atom::scopeTranslationUnit) ) {
		//fprintf(stderr, "Reference(): changing to by-name %p %s, target scope=%d\n", toTarget.atom, fToTargetName, toTarget.atom->getScope());
		fToTargetName = toTarget.atom->getName();
		fToTarget.atom = NULL;
	}
	((class BaseAtom*)at.atom)->addReference(this);
	//fprintf(stderr, "Reference(): %p fToTarget<%s, %08X>\n", this, (fToTarget.atom != NULL) ? fToTarget.atom->getDisplayName() : fToTargetName , fToTarget.offset);
}

template <typename A>
Reference<A>::Reference(Kinds kind, const AtomAndOffset& at, const AtomAndOffset& fromTarget, const AtomAndOffset& toTarget)
 : fFixUpOffsetInSrc(at.offset), fToTarget(toTarget), fFromTarget(fromTarget),
   fToTargetName(NULL), fFromTargetName(NULL), fKind(kind)
{
	// make reference a by-name where needed
	if ( (kind != A::kNoFixUp) && (kind != A::kFollowOn) 
		&& (toTarget.atom->getScope() != ObjectFile::Atom::scopeTranslationUnit)
		&& (toTarget.atom != at.atom) ) {
			fToTargetName = toTarget.atom->getName();
			fToTarget.atom = NULL;
	}
	((class BaseAtom*)at.atom)->addReference(this);
	//fprintf(stderr, "Reference(): %p kind=%d, fToTarget<%s, %08X>, fromTarget<%s, %08X>\n", this, kind,
	//	 this->getTargetName(), fToTarget.offset, this->getFromTargetName(), fromTarget.offset);
}

template <typename A>
Reference<A>::Reference(Kinds kind, const AtomAndOffset& at, const char* toName, uint32_t toOffset)
 : fFixUpOffsetInSrc(at.offset),
   fToTargetName(toName), fFromTargetName(NULL), fKind(kind)
{
	fToTarget.offset = toOffset;
	((class BaseAtom*)at.atom)->addReference(this);
}


template <typename A>
class Segment : public ObjectFile::Segment
{
public:
								Segment(const macho_section<typename A::P>* sect);
	virtual const char*			getName() const						{ return fSection->segname(); }
	virtual bool				isContentReadable() const			{ return true; }
	virtual bool				isContentWritable() const			{ return fWritable; }
	virtual bool				isContentExecutable() const			{ return fExecutable; }
private:
	const macho_section<typename A::P>*		fSection;
	bool									fWritable;
	bool									fExecutable;
};

template <typename A>
Segment<A>::Segment(const macho_section<typename A::P>* sect) 
 :	fSection(sect), fWritable(true),  fExecutable(false) 
{
	if ( strcmp(fSection->segname(), "__TEXT") == 0 ) {
		fWritable = false;
		fExecutable = true;
	}
	else if ( strcmp(fSection->segname(), "__IMPORT") == 0 ) {
		fWritable = true;
		fExecutable = true;
	}
}


class DataSegment : public ObjectFile::Segment
{
public:
	virtual const char*			getName() const						{ return "__DATA"; }
	virtual bool				isContentReadable() const			{ return true; }
	virtual bool				isContentWritable() const			{ return true; }
	virtual bool				isContentExecutable() const			{ return false; }

	static DataSegment			fgSingleton;
};

DataSegment DataSegment::fgSingleton;


class BaseAtom : public ObjectFile::Atom
{
public:
												BaseAtom() : fStabsStartIndex(0), fStabsCount(0) {}

	virtual void								setSize(uint64_t size)	= 0;
	virtual void								addReference(ObjectFile::Reference* ref) = 0;
	virtual void								addLineInfo(const ObjectFile::LineInfo& info) = 0;
	virtual void								alignAtLeast(uint8_t align)	= 0;

	uint32_t									fStabsStartIndex;
	uint32_t									fStabsCount;
};


//
// A SymbolAtom represents a chunk of a mach-o object file that has a symbol table entry
// pointing to it.  A C function or global variable is represented by one of these atoms.
//
//
template <typename A>
class SymbolAtom : public BaseAtom
{
public:
	virtual ObjectFile::Reader*					getFile() const					{ return &fOwner; }
	virtual bool								getTranslationUnitSource(const char** dir, const char** name) const
																				{ return fOwner.getTranslationUnitSource(dir, name); }
	virtual const char*							getName() const					{ return &fOwner.fStrings[fSymbol->n_strx()]; }
	virtual const char*							getDisplayName() const			{ return getName(); }
	virtual ObjectFile::Atom::Scope				getScope() const				{ return fScope; }
	virtual ObjectFile::Atom::DefinitionKind	getDefinitionKind() const		{ return ((fSymbol->n_desc() & N_WEAK_DEF) != 0)
																						? ObjectFile::Atom::kWeakDefinition : ObjectFile::Atom::kRegularDefinition; }
	virtual SymbolTableInclusion				getSymbolTableInclusion() const	{ return fSymbolTableInclusion; }
	virtual	bool								dontDeadStrip() const			{ return ((fSymbol->n_desc() & (N_NO_DEAD_STRIP|REFERENCED_DYNAMICALLY)) != 0); }
	virtual bool								isZeroFill() const				{ return ((fSection->flags() & SECTION_TYPE) == S_ZEROFILL); }
	virtual uint64_t							getSize() const					{ return fSize; }
	virtual std::vector<ObjectFile::Reference*>&  getReferences() const			{ return (std::vector<ObjectFile::Reference*>&)(fReferences); }
	virtual bool								mustRemainInSection() const		{ return true; }
	virtual const char*							getSectionName() const;
	virtual Segment<A>&							getSegment() const				{ return *fSegment; }
	virtual bool								requiresFollowOnAtom() const;
	virtual ObjectFile::Atom&					getFollowOnAtom() const;
	virtual std::vector<ObjectFile::LineInfo>*	getLineInfo() const				{ return (std::vector<ObjectFile::LineInfo>*)&fLineInfo; }
	virtual uint8_t								getAlignment() const			{ return fAlignment; }
	virtual void								copyRawContent(uint8_t buffer[]) const;
	virtual void								setScope(ObjectFile::Atom::Scope newScope)		{ fScope = newScope; }
	virtual void								setSize(uint64_t size);
	virtual void								addReference(ObjectFile::Reference* ref) { fReferences.insert(fReferences.begin(), (Reference<A>*)ref); }
	virtual void								addLineInfo(const  ObjectFile::LineInfo& info)	{ fLineInfo.push_back(info); }
	virtual void								alignAtLeast(uint8_t align)		{ fAlignment = std::max(align, fAlignment); }

protected:
	typedef typename A::P						P;
	typedef typename A::P::E					E;
	typedef typename A::P::uint_t				pint_t;
	typedef typename A::ReferenceKinds			Kinds;
	typedef typename std::vector<Reference<A>*>			ReferenceVector;
	typedef typename ReferenceVector::iterator			ReferenceVectorIterator;		// seems to help C++ parser
	typedef typename ReferenceVector::const_iterator	ReferenceVectorConstIterator;	// seems to help C++ parser
	friend class Reader<A>;
	friend class SymbolAtomSorter<A>;

											SymbolAtom(Reader<A>&, const macho_nlist<P>*, const macho_section<P>*);
	virtual									~SymbolAtom() {}

	Reader<A>&									fOwner;
	const macho_nlist<P>*						fSymbol;
	pint_t										fAddress;
	pint_t										fSize;
	const macho_section<P>*						fSection;
	Segment<A>*									fSegment;
	ReferenceVector								fReferences;
	std::vector<ObjectFile::LineInfo>			fLineInfo;
	ObjectFile::Atom::Scope						fScope;
	SymbolTableInclusion						fSymbolTableInclusion;
	uint8_t										fAlignment;
};


template <typename A>
SymbolAtom<A>::SymbolAtom(Reader<A>& owner, const macho_nlist<P>* symbol, const macho_section<P>* section)
 : fOwner(owner), fSymbol(symbol), fAddress(0), fSize(0), fSection(section), fSegment(NULL), fAlignment(0)
{
	uint8_t type =  symbol->n_type();
	if ( (type & N_EXT) == 0 )
		fScope = ObjectFile::Atom::scopeTranslationUnit;
	else if ( (type & N_PEXT) != 0 )
		fScope = ObjectFile::Atom::scopeLinkageUnit;
	else
		fScope = ObjectFile::Atom::scopeGlobal;
	if ( (type & N_TYPE) == N_SECT ) {
		// real definition
 		fSegment = new Segment<A>(fSection);
		fAddress = fSymbol->n_value();
	}	
	else {
		printf("unknown symbol type: %d\n", type);
	}
	//fprintf(stderr, "SymbolAtom(%p) %s fAddress=0x%X\n", this, this->getDisplayName(), (uint32_t)fAddress);
	// support for .o files built with old ld64
	if ( (fSymbol->n_desc() & N_WEAK_DEF) && (strcmp(fSection->sectname(),"__picsymbolstub1__TEXT") == 0) ) {
		const char* name = this->getName();
		const int nameLen = strlen(name);
		if ( (nameLen > 6) && strcmp(&name[nameLen-5], "$stub") == 0 ) {
			// switch symbol to point at name that does not have trailing $stub
			char correctName[nameLen];
			strncpy(correctName, name, nameLen-5);
			correctName[nameLen-5] = '\0';
			const macho_nlist<P>* symbolsStart = fOwner.fSymbols;
			const macho_nlist<P>* symbolsEnd = &symbolsStart[fOwner.fSymbolCount];
			for(const macho_nlist<P>* s = symbolsStart; s < symbolsEnd; ++s) {
				if ( strcmp(&fOwner.fStrings[s->n_strx()], correctName) == 0 ) {
					fSymbol = s;
					break;
				}
			}
		}
	}
	// support for labeled stubs
	switch ( section->flags() & SECTION_TYPE ) {
		case S_SYMBOL_STUBS:
			setSize(section->reserved2());
			break;
		case S_LAZY_SYMBOL_POINTERS:
		case S_NON_LAZY_SYMBOL_POINTERS:
			setSize(sizeof(pint_t));
			break;
		case S_4BYTE_LITERALS:
			setSize(4);
			break;
		case S_8BYTE_LITERALS:
			setSize(8);
			break;
		case S_16BYTE_LITERALS:
			setSize(16);
			break;
		case S_CSTRING_LITERALS:
			setSize(strlen((char*)(fOwner.fHeader) + section->offset() + fAddress - section->addr()) + 1);
			break;
		case S_REGULAR:
		case S_ZEROFILL:
		case S_COALESCED:
			// size calculate later after next atom is found
			break;
	}
	
	// compute whether this atom needs to be in symbol table
	if ( (fSymbol->n_desc() & REFERENCED_DYNAMICALLY) != 0) {
		fSymbolTableInclusion = ObjectFile::Atom::kSymbolTableInAndNeverStrip;
	}
	else if (  fOwner.fOptions.fForFinalLinkedImage 
			&& ((section->flags() & SECTION_TYPE) == S_COALESCED) 
			&& ((section->flags() & S_ATTR_NO_TOC) == S_ATTR_NO_TOC) 
			&& ((section->flags() & S_ATTR_STRIP_STATIC_SYMS) == S_ATTR_STRIP_STATIC_SYMS) 
			&& (strcmp(section->sectname(), "__eh_frame") == 0) ) {
		// .eh symbols exist so the linker can associate them with functions
		// removing them from final linked images is a big space savings rdar://problem/4180168
		fSymbolTableInclusion = ObjectFile::Atom::kSymbolTableNotIn;
	}
	else if (  fOwner.fOptions.fForFinalLinkedImage 
			&& ((section->flags() & SECTION_TYPE) == S_REGULAR) 
			&& (strncmp(section->sectname(), "__gcc_except_tab", 16) == 0) 
			&& (strncmp(this->getName(),     "GCC_except_table", 16) == 0) ) {
		// GCC_except_table* symbols don't need to exist in final linked image
		fSymbolTableInclusion = ObjectFile::Atom::kSymbolTableNotIn;
	}
	else {
		fSymbolTableInclusion = ObjectFile::Atom::kSymbolTableIn;
	}
}


template <typename A>
void SymbolAtom<A>::setSize(uint64_t size)
{
	fSize = size;
	
	if ( fSection->flags() & S_ATTR_SOME_INSTRUCTIONS ) {
		// For code, the aligment is based just on the section alignment and code address
		if ( fAddress == 0 )
			fAlignment = fSection->align();
		else
			fAlignment = std::min((uint8_t)__builtin_ctz(fAddress), (uint8_t)fSection->align());
	}
	else {
		// For data, compute the alignment base on the address aligned at in object file and the size
		uint8_t sizeAlign = __builtin_ctz(fSize);
		uint8_t sizeAndSectAlign = std::min((uint8_t)fSection->align(), sizeAlign);
		// If address is zero, can't figure out better alignment than section alignment and size
		if ( fAddress == 0 )
			fAlignment = sizeAndSectAlign;
		else
			fAlignment = std::min((uint8_t)__builtin_ctz(fAddress), sizeAndSectAlign);
	}
}


template <typename A>
const char*	SymbolAtom<A>::getSectionName() const
{
	if ( strlen(fSection->sectname()) > 15 ) {
		static char temp[18];
		strncpy(temp, fSection->sectname(), 16);
		temp[17] = '\0';
		return temp;
	}
	return fSection->sectname();
}

template <typename A>
bool SymbolAtom<A>::requiresFollowOnAtom() const
{
	// requires follow-on if built with old compiler and not the last atom
	if ( (fOwner.fHeader->flags() & MH_SUBSECTIONS_VIA_SYMBOLS) == 0) {
		for (ReferenceVectorConstIterator it=fReferences.begin(); it != fReferences.end(); it++) {
			Reference<A>* ref = *it;
			if ( ref->getKind() == A::kFollowOn )
				return true;
		}
	}
	return false;
}

template <typename A>
ObjectFile::Atom& SymbolAtom<A>::getFollowOnAtom() const
{
	for (ReferenceVectorConstIterator it=fReferences.begin(); it != fReferences.end(); it++) {
		Reference<A>* ref = *it;
		if ( ref->getKind() == A::kFollowOn )
			return ref->getTarget();
	}
	return *((ObjectFile::Atom*)NULL);
}




template <typename A>
void SymbolAtom<A>::copyRawContent(uint8_t buffer[]) const
{
	// copy base bytes
	if ( isZeroFill() )
		bzero(buffer, fSize);
	else {
		uint32_t fileOffset = fSection->offset() - fSection->addr() + fAddress;
		memcpy(buffer, (char*)(fOwner.fHeader)+fileOffset, fSize);
	}
}


template <typename A>
class SymbolAtomSorter
{
public:
	SymbolAtomSorter(std::map<uint32_t, BaseAtom*>& map) : fMap(map) {}
	
	typedef typename A::P::uint_t pint_t;
	
	bool operator()(ObjectFile::Atom* left, ObjectFile::Atom* right)
	{
		pint_t leftAddr  = ((SymbolAtom<A>*)left)->fAddress;
		pint_t rightAddr = ((SymbolAtom<A>*)right)->fAddress;
		if ( leftAddr == rightAddr ) {
			// two atoms with same address, must have been a function with multiple labels
			// make sure we sort these so the one with real content (in map) is last
			std::map<uint32_t, BaseAtom*>::iterator pos = fMap.find(leftAddr);
			if ( pos != fMap.end() ) {
				return ( pos->second == right );
			}
			return false;
		}
		else {
			return ( leftAddr < rightAddr );
		}
	}
private:
	std::map<uint32_t, BaseAtom*>&	fMap;
};


//
// A TentativeAtom represents a C "common" or "tentative" defintion of data.
// For instance, "int foo;" is neither a declaration or a definition and
// is represented by a TentativeAtom.
//
template <typename A>
class TentativeAtom : public BaseAtom
{
public:
	virtual ObjectFile::Reader*					getFile() const					{ return &fOwner; }
	virtual bool								getTranslationUnitSource(const char** dir, const char** name) const
																				{ return fOwner.getTranslationUnitSource(dir, name); }
	virtual const char*							getName() const					{ return &fOwner.fStrings[fSymbol->n_strx()]; }
	virtual const char*							getDisplayName() const			{ return getName(); }
	virtual ObjectFile::Atom::Scope				getScope() const				{ return fScope; }
	virtual ObjectFile::Atom::DefinitionKind	getDefinitionKind() const		{ return ObjectFile::Atom::kTentativeDefinition; }
	virtual bool								isZeroFill() const				{ return true; }
	virtual SymbolTableInclusion				getSymbolTableInclusion() const	{ return ((fSymbol->n_desc() & REFERENCED_DYNAMICALLY) != 0)
																						? ObjectFile::Atom::kSymbolTableInAndNeverStrip : ObjectFile::Atom::kSymbolTableIn; }
	virtual	bool								dontDeadStrip() const			{ return ((fSymbol->n_desc() & (N_NO_DEAD_STRIP|REFERENCED_DYNAMICALLY)) != 0); }
	virtual uint64_t							getSize() const					{ return fSymbol->n_value(); }
	virtual std::vector<ObjectFile::Reference*>&  getReferences() const			{ return fgNoReferences; }
	virtual bool								mustRemainInSection() const		{ return true; }
	virtual const char*							getSectionName() const			{ return "__common"; }
	virtual ObjectFile::Segment&				getSegment() const				{ return DataSegment::fgSingleton; }
	virtual bool								requiresFollowOnAtom() const	{ return false; }
	virtual ObjectFile::Atom&					getFollowOnAtom() const			{ return *(ObjectFile::Atom*)NULL; }
	virtual std::vector<ObjectFile::LineInfo>*	getLineInfo() const				{ return NULL; }
	virtual uint8_t								getAlignment() const;
	virtual void								copyRawContent(uint8_t buffer[]) const;
	virtual void								setScope(ObjectFile::Atom::Scope newScope)		{ fScope = newScope; }
	virtual void								setSize(uint64_t size)			{ }
	virtual void								addReference(ObjectFile::Reference* ref) { throw "ld64: can't add references"; }
	virtual void								addLineInfo(const  ObjectFile::LineInfo& info)	{ throw "ld64: can't add line info to tentative definition"; }
	virtual void								alignAtLeast(uint8_t align)		{ }

protected:
	typedef typename A::P					P;
	typedef typename A::P::E				E;
	typedef typename A::P::uint_t			pint_t;
	typedef typename A::ReferenceKinds		Kinds;
	friend class Reader<A>;

											TentativeAtom(Reader<A>&, const macho_nlist<P>*);
	virtual									~TentativeAtom() {}

	Reader<A>&									fOwner;
	const macho_nlist<P>*						fSymbol;
	ObjectFile::Atom::Scope						fScope;
	static std::vector<ObjectFile::Reference*>	fgNoReferences;
};

template <typename A>
std::vector<ObjectFile::Reference*> TentativeAtom<A>::fgNoReferences;

template <typename A>
TentativeAtom<A>::TentativeAtom(Reader<A>& owner, const macho_nlist<P>* symbol)
 : fOwner(owner), fSymbol(symbol)
{
	uint8_t type =  symbol->n_type();
	if ( (type & N_EXT) == 0 )
		fScope = ObjectFile::Atom::scopeTranslationUnit;
	else if ( (type & N_PEXT) != 0 )
		fScope = ObjectFile::Atom::scopeLinkageUnit;
	else
		fScope = ObjectFile::Atom::scopeGlobal;
	if ( ((type & N_TYPE) == N_UNDF) && (symbol->n_value() != 0) ) {
		// tentative definition
	}
	else {
		printf("unknown symbol type: %d\n", type);
	}
	//fprintf(stderr, "TentativeAtom(%p) %s\n", this, this->getDisplayName());
}


template <typename A>
uint8_t	TentativeAtom<A>::getAlignment() const
{
	// common symbols align to their size
	// that is, a 4-byte common aligns to 4-bytes
	// to be safe, odd size commons align to the next power-of-2 size
	uint8_t alignment = (uint8_t)ceil(log2(this->getSize()));
	// limit alignment of extremely large commons to 2^15 bytes (8-page)
	if ( alignment < 15 )
		return alignment;
	else
		return 15;
}

template <typename A>
void TentativeAtom<A>::copyRawContent(uint8_t buffer[]) const
{
	bzero(buffer, getSize());
}


//
// An AnonymousAtom represents compiler generated data that has no name.
// For instance, a literal C-string or a 64-bit floating point constant
// is represented by an AnonymousAtom.
//
template <typename A>
class AnonymousAtom : public BaseAtom
{
public:
	virtual ObjectFile::Reader*					getFile() const					{ return &fOwner; }
	virtual bool								getTranslationUnitSource(const char** dir, const char** name) const { return false; }
	virtual const char*							getName() const					{ return fSynthesizedName; }
	virtual const char*							getDisplayName() const;
	virtual ObjectFile::Atom::Scope				getScope() const;
	virtual ObjectFile::Atom::DefinitionKind	getDefinitionKind() const;
	virtual ObjectFile::Atom::SymbolTableInclusion getSymbolTableInclusion() const	{ return fSymbolTableInclusion; }
	virtual	bool								dontDeadStrip() const			{ return fDontDeadStrip; }
	virtual bool								isZeroFill() const;
	virtual uint64_t							getSize() const					{ return fSize; }
	virtual std::vector<ObjectFile::Reference*>&  getReferences() const			{ return (std::vector<ObjectFile::Reference*>&)(fReferences); }
	virtual bool								mustRemainInSection() const		{ return true; }
	virtual const char*							getSectionName() const;
	virtual Segment<A>&							getSegment() const				{ return *fSegment; }
	virtual bool								requiresFollowOnAtom() const;
	virtual ObjectFile::Atom&					getFollowOnAtom() const;
	virtual std::vector<ObjectFile::LineInfo>*	getLineInfo() const				{ return NULL; }
	virtual uint8_t								getAlignment() const;
	virtual void								copyRawContent(uint8_t buffer[]) const;
	virtual void								setScope(ObjectFile::Atom::Scope newScope)	{ fScope = newScope; }
	virtual void								setSize(uint64_t size)			{ fSize = size; }
	virtual void								addReference(ObjectFile::Reference* ref) { fReferences.insert(fReferences.begin(), (Reference<A>*)ref); }
	virtual void								addLineInfo(const  ObjectFile::LineInfo& info) { fprintf(stderr, "ld64: can't add line info to anonymous symbol %s from %s\n", this->getDisplayName(), this->getFile()->getPath()); }
	virtual void								alignAtLeast(uint8_t align)		{ }
	BaseAtom*									redirectTo()					{ return fRedirect; }
	bool										isWeakImportStub()				{ return fWeakImportStub; }

protected:
	typedef typename A::P						P;
	typedef typename A::P::E					E;
	typedef typename A::P::uint_t				pint_t;
	typedef typename A::ReferenceKinds			Kinds;
	typedef typename std::vector<Reference<A>*>			ReferenceVector;
	typedef typename ReferenceVector::iterator			ReferenceVectorIterator;		// seems to help C++ parser
	typedef typename ReferenceVector::const_iterator	ReferenceVectorConstIterator;	// seems to help C++ parser
	friend class Reader<A>;

											AnonymousAtom(Reader<A>&, const macho_section<P>*, uint32_t addr, uint32_t size);
	virtual									~AnonymousAtom() {}

	Reader<A>&									fOwner;
	const char*									fSynthesizedName;
	const macho_section<P>*						fSection;
	uint32_t									fAddress;
	uint32_t									fSize;
	Segment<A>*									fSegment;
	ReferenceVector								fReferences;
	BaseAtom*									fRedirect;
	bool										fDontDeadStrip;
	bool										fWeakImportStub;
	bool										fReallyNonLazyPointer;	// HACK until compiler stops emitting anonymous non-lazy pointers
	ObjectFile::Atom::SymbolTableInclusion		fSymbolTableInclusion;
	ObjectFile::Atom::Scope						fScope;
};

template <typename A>
AnonymousAtom<A>::AnonymousAtom(Reader<A>& owner, const macho_section<P>* section, uint32_t addr, uint32_t size)
 : fOwner(owner), fSynthesizedName(NULL), fSection(section), fAddress(addr), fSize(size), fSegment(NULL), fDontDeadStrip(true),
	fWeakImportStub(false), fReallyNonLazyPointer(false), fSymbolTableInclusion(ObjectFile::Atom::kSymbolTableNotIn),
	fScope(ObjectFile::Atom::scopeTranslationUnit)
{
	fSegment = new Segment<A>(fSection);
	fRedirect = this;
	uint8_t type = fSection->flags() & SECTION_TYPE;
	switch ( type ) {
		case S_ZEROFILL:
			{
				asprintf((char**)&fSynthesizedName, "zero-fill-at-0x%08X", addr);
			}
			break;
		case S_REGULAR:
			if ( (strcmp(section->sectname(), "__class") == 0) && (strcmp(section->segname(), "__OBJC") == 0) && owner.fAppleObjc ) {
				// special case ObjC classes to synthesize .objc_class_name_* symbols, for Apple runtime only
				uint32_t classNameAddr =  P::getP(*(pint_t*)(((uint8_t*)owner.fHeader) + section->offset() + addr + 2*sizeof(pint_t) - section->addr()));
				const char* str = (char*)(owner.fHeader) + section->offset() + classNameAddr - section->addr();
				asprintf((char**)&fSynthesizedName, ".objc_class_name_%s", str);
				if ( fOwner.fOptions.fForFinalLinkedImage ) 
					fSymbolTableInclusion = ObjectFile::Atom::kSymbolTableIn;
				else
					fSymbolTableInclusion = ObjectFile::Atom::kSymbolTableInAsAbsolute;
				fScope = ObjectFile::Atom::scopeGlobal;
			}
			else if ( strcmp(fSection->sectname(), "__cstring") == 0 ) {
				// handle .o files created by old ld64 -r that are missing cstring section type
				const char* str = (char*)(owner.fHeader) + section->offset() + addr - section->addr();
				asprintf((char**)&fSynthesizedName, "cstring=%s", str);
			}
			break;
		case S_CSTRING_LITERALS:
			{
				const char* str = (char*)(owner.fHeader) + section->offset() + addr - section->addr();
				asprintf((char**)&fSynthesizedName, "cstring=%s", str);
				fScope = ObjectFile::Atom::scopeLinkageUnit;
				fDontDeadStrip = false;
			}
			break;
		case S_4BYTE_LITERALS:
			{
				uint32_t value =  E::get32(*(uint32_t*)(((uint8_t*)owner.fHeader) + section->offset() + addr - section->addr()));
				asprintf((char**)&fSynthesizedName, "4-byte-literal=0x%08X", value);
				fScope = ObjectFile::Atom::scopeLinkageUnit;
				fDontDeadStrip = false;
			}
			break;
		case S_8BYTE_LITERALS:
			{
				uint64_t value =  E::get64(*(uint64_t*)(((uint8_t*)owner.fHeader) + section->offset() + addr - section->addr()));
				asprintf((char**)&fSynthesizedName, "8-byte-literal=0x%016llX", value);
				fScope = ObjectFile::Atom::scopeLinkageUnit;
				fDontDeadStrip = false;
			}
			break;
		case S_16BYTE_LITERALS:
			{
				uint64_t value1 =  E::get64(*(uint64_t*)(((uint8_t*)owner.fHeader) + section->offset() + addr - section->addr()));
				uint64_t value2 =  E::get64(*(uint64_t*)(((uint8_t*)owner.fHeader) + section->offset() + addr + 8 - section->addr()));
				asprintf((char**)&fSynthesizedName, "16-byte-literal=0x%016llX,%016llX", value1, value2);
				fScope = ObjectFile::Atom::scopeLinkageUnit;
				fDontDeadStrip = false;
			}
			break;
		case S_LITERAL_POINTERS:
			{
				uint32_t literalNameAddr =  P::getP(*(pint_t*)(((uint8_t*)owner.fHeader) + section->offset() + addr - section->addr()));
				const char* str = (char*)(owner.fHeader) + section->offset() + literalNameAddr - section->addr();
				asprintf((char**)&fSynthesizedName, "literal-pointer@%s@%s@%s", section->segname(), section->sectname(), str);
				fScope = ObjectFile::Atom::scopeLinkageUnit;
			}
			break;
		case S_MOD_INIT_FUNC_POINTERS:
				asprintf((char**)&fSynthesizedName, "initializer$%d", (addr - (uint32_t)fSection->addr())/sizeof(pint_t));
				break;
		case S_MOD_TERM_FUNC_POINTERS:
				asprintf((char**)&fSynthesizedName, "terminator$%d", (addr - (uint32_t)fSection->addr())/sizeof(pint_t));
				break;
		case S_SYMBOL_STUBS:
			{
				uint32_t index = (fAddress - fSection->addr()) / fSection->reserved2();
				index += fSection->reserved1();
				uint32_t symbolIndex = E::get32(fOwner.fIndirectTable[index]);
				const macho_nlist<P>* sym = &fOwner.fSymbols[symbolIndex];
				uint32_t strOffset = sym->n_strx();
				// want name to not have $stub suffix, this is what automatic stub generation expects
				fSynthesizedName = &fOwner.fStrings[strOffset];
				// check for weak import
				fWeakImportStub = fOwner.isWeakImportSymbol(sym);
				// sometimes the compiler gets confused and generates a stub to a static function
				// if so, we should redirect any call to the stub to be calls to the real static function atom
				if ( ((sym->n_type() & N_TYPE) != N_UNDF) && ((sym->n_desc() & N_WEAK_DEF) == 0) ) {
					BaseAtom* staticAtom = fOwner.findAtomByName(fSynthesizedName);
					if ( staticAtom != NULL )
						fRedirect = staticAtom;
				}
				fScope = ObjectFile::Atom::scopeLinkageUnit;
			}
			break;
		case S_LAZY_SYMBOL_POINTERS:
		case S_NON_LAZY_SYMBOL_POINTERS:
			{
				fDontDeadStrip = false;
				fScope = ObjectFile::Atom::scopeLinkageUnit;
				uint32_t index = (fAddress - fSection->addr()) / sizeof(pint_t);
				index += fSection->reserved1();
				uint32_t symbolIndex = E::get32(fOwner.fIndirectTable[index]);
				if ( symbolIndex == INDIRECT_SYMBOL_LOCAL ) {
					// Silly codegen with non-lazy pointer to a local symbol
					uint32_t fileOffset = fSection->offset() - fSection->addr() + fAddress;
					pint_t nonLazyPtrValue = P::getP(*((pint_t*)((char*)(fOwner.fHeader)+fileOffset)));
					// All atoms not created yet, so we need to scan symbol table
					const macho_nlist<P>* end = &fOwner.fSymbols[fOwner.fSymbolCount];
					for (const macho_nlist<P>* sym =  fOwner.fSymbols; sym < end; ++sym) {
						if ( ((sym->n_type() & N_TYPE) == N_SECT) 
						 && ((sym->n_type() & N_STAB) == 0) 
						 && (sym->n_value() == nonLazyPtrValue) ) {
							const char* name = &fOwner.fStrings[sym->n_strx()];
							char* str = new char[strlen(name)+16];
							strcpy(str, name);
							strcat(str, "$non_lazy_ptr");
							fSynthesizedName = str;
							// add direct reference to target later, because its atom may not be constructed yet
							fOwner.fLocalNonLazys.push_back(this);
							fScope = ObjectFile::Atom::scopeTranslationUnit;
							return;
						}
					}
					throwf("malformed .o file: non-lazy-pointer at address 0x%08X with value 0x%0llX missing symbol", addr, (uint64_t)nonLazyPtrValue);
				}
				const macho_nlist<P>* targetSymbol = &fOwner.fSymbols[symbolIndex];
				const char* name = &fOwner.fStrings[targetSymbol->n_strx()];
				char* str = new char[strlen(name)+16];
				strcpy(str, name);
				if ( type == S_LAZY_SYMBOL_POINTERS )
					strcat(str, "$lazy_ptr");
				else
					strcat(str, "$non_lazy_ptr");
				fSynthesizedName = str;

				if ( (targetSymbol->n_type() & N_EXT) == 0 ) {
					// target is translation unit scoped, so add direct reference to target
					//fOwner.makeReference(A::kPointer, addr, targetSymbol->n_value());
					new Reference<A>(A::kPointer, AtomAndOffset(this), fOwner.findAtomAndOffset(targetSymbol->n_value()));
				}
				else {	
					if ( fOwner.isWeakImportSymbol(targetSymbol) )
						new Reference<A>(A::kPointerWeakImport, AtomAndOffset(this), name, 0);
					else
						new Reference<A>(A::kPointer, AtomAndOffset(this), name, 0);
				}
			}
			break;
		default:
			throwf("section type %d not supported with address=0x%08X", type, addr);
	}
	//fprintf(stderr, "AnonymousAtom(%p) %s \n", this, this->getDisplayName());
}


template <typename A>
const char* AnonymousAtom<A>::getDisplayName() const
{
	if ( fSynthesizedName != NULL )
		return fSynthesizedName;

	static char temp[512];
	if ( (fSection->flags() & SECTION_TYPE) == S_CSTRING_LITERALS ) {
		uint32_t fileOffset = fSection->offset() - fSection->addr() + fAddress;
		sprintf(temp, "atom string literal: \"%s\"", (char*)(fOwner.fHeader)+fileOffset);
	}
	else {
		sprintf(temp, "%s@%d", fSection->sectname(), fAddress - (uint32_t)fSection->addr() );
	}
	return temp;
}

template <typename A>
ObjectFile::Atom::Scope AnonymousAtom<A>::getScope() const
{
	if ( fReallyNonLazyPointer )
		return ObjectFile::Atom::scopeTranslationUnit;
	else	
		return fScope;
}

template <typename A>
ObjectFile::Atom::DefinitionKind AnonymousAtom<A>::getDefinitionKind() const
{
	if ( fReallyNonLazyPointer )
		return  ObjectFile::Atom::kRegularDefinition;
	// in order for literals to be coalesced they must be weak
	switch ( fSection->flags() & SECTION_TYPE ) {
		case S_CSTRING_LITERALS:
		case S_4BYTE_LITERALS:
		case S_8BYTE_LITERALS:
		case S_16BYTE_LITERALS:
		case S_NON_LAZY_SYMBOL_POINTERS:
		case S_LITERAL_POINTERS:
			return ObjectFile::Atom::kWeakDefinition;
		default:
			return ObjectFile::Atom::kRegularDefinition;
	}
}

template <typename A>
bool AnonymousAtom<A>::isZeroFill() const
{
	return ( (fSection->flags() & SECTION_TYPE) == S_ZEROFILL );
}


template <typename A>
const char*	AnonymousAtom<A>::getSectionName() const
{
	if ( strlen(fSection->sectname()) > 15 ) {
		static char temp[18];
		strncpy(temp, fSection->sectname(), 16);
		temp[17] = '\0';
		return temp;
	}
	return fSection->sectname();
}

template <typename A>
uint8_t AnonymousAtom<A>::getAlignment() const
{
	if ( fReallyNonLazyPointer )
		return (uint8_t)log2(sizeof(pint_t));
	switch ( fSection->flags() & SECTION_TYPE ) {
		case S_4BYTE_LITERALS:
			return 2;
		case S_8BYTE_LITERALS:
			return 3;
		case S_16BYTE_LITERALS:
			return 4;
		case S_NON_LAZY_SYMBOL_POINTERS:
			return (uint8_t)log2(sizeof(pint_t));
		default:
			return fSection->align();
	}
}

template <typename A>
bool AnonymousAtom<A>::requiresFollowOnAtom() const
{
	// requires follow-on if built with old compiler and not the last atom
	if ( (fOwner.fHeader->flags() & MH_SUBSECTIONS_VIA_SYMBOLS) == 0) {
		for (ReferenceVectorConstIterator it=fReferences.begin(); it != fReferences.end(); it++) {
			Reference<A>* ref = *it;
			if ( ref->getKind() == A::kFollowOn )
				return true;
		}
	}
	return false;
}

template <typename A>
ObjectFile::Atom& AnonymousAtom<A>::getFollowOnAtom() const
{
	for (ReferenceVectorConstIterator it=fReferences.begin(); it != fReferences.end(); it++) {
		Reference<A>* ref = *it;
		if ( ref->getKind() == A::kFollowOn )
			return ref->getTarget();
	}
	return *((ObjectFile::Atom*)NULL);
}

template <typename A>
void AnonymousAtom<A>::copyRawContent(uint8_t buffer[]) const
{
	// copy base bytes
	if ( isZeroFill() )
		bzero(buffer, fSize);
	else {
		uint32_t fileOffset = fSection->offset() - fSection->addr() + fAddress;
		memcpy(buffer, (char*)(fOwner.fHeader)+fileOffset, fSize);
	}
}




template <typename A>
class Reader : public ObjectFile::Reader
{
public:
	static bool										validFile(const uint8_t* fileContent);
	static Reader<A>*								make(const uint8_t* fileContent, const char* path, time_t modTime,
															const ObjectFile::ReaderOptions& options)
														{ return new Reader<A>(fileContent, path, modTime, options); }
	virtual											~Reader() {}

	virtual const char*								getPath()				{ return fPath; }
	virtual time_t									getModificationTime()	{ return fModTime; }
	virtual ObjectFile::Reader::DebugInfoKind		getDebugInfoKind()		{ return fDebugInfo; }
	virtual std::vector<class ObjectFile::Atom*>&	getAtoms()				{ return (std::vector<class ObjectFile::Atom*>&)(fAtoms); }
	virtual std::vector<class ObjectFile::Atom*>*	getJustInTimeAtomsFor(const char* name) { return NULL; }
	virtual std::vector<Stab>*						getStabs()				{ return &fStabs; }

	 bool											getTranslationUnitSource(const char** dir, const char** name) const;

private:
	typedef typename A::P						P;
	typedef typename A::P::E					E;
	typedef typename A::P::uint_t				pint_t;
	//typedef typename std::vector<Atom<A>*>		AtomVector;
	//typedef typename AtomVector::iterator		AtomVectorIterator;	// seems to help C++ parser
	typedef typename A::ReferenceKinds			Kinds;
	friend class AnonymousAtom<A>;
	friend class TentativeAtom<A>;
	friend class SymbolAtom<A>;
												Reader(const uint8_t* fileContent, const char* path, time_t modTime, const ObjectFile::ReaderOptions& options);
	bool										addRelocReference(const macho_section<P>* sect, const macho_relocation_info<P>* reloc);
	bool										addRelocReference_powerpc(const macho_section<P>* sect, const macho_relocation_info<P>* reloc);
	Kinds										pointerDiffKindForLength_powerpc(uint8_t r_length);
	bool										read_comp_unit(const char ** name, const char ** comp_dir, uint64_t *stmt_list);
	static bool									isWeakImportSymbol(const macho_nlist<P>* sym);
	static bool									skip_form(const uint8_t ** offset, const uint8_t * end, uint64_t form, uint8_t addr_size, bool dwarf64);
	static const char*							assureFullPath(const char* path);
	AtomAndOffset								findAtomAndOffset(uint32_t addr);
	AtomAndOffset								findAtomAndOffset(uint32_t baseAddr, uint32_t realAddr);
	Reference<A>*								makeReference(Kinds kind, uint32_t atAddr, uint32_t toAddr);
	Reference<A>*								makeReference(Kinds kind, uint32_t atAddr, uint32_t fromAddr, uint32_t toAddr);
	Reference<A>*								makeReferenceWithToBase(Kinds kind, uint32_t atAddr, uint32_t toAddr, uint32_t toBaseAddr);
	Reference<A>*								makeReferenceWithToBase(Kinds kind, uint32_t atAddr, uint32_t fromAddr, uint32_t toAddr, uint32_t toBaseAddr);
	Reference<A>*								makeByNameReference(Kinds kind, uint32_t atAddr, const char* toName, uint32_t toOffset);
	Reference<A>*								makeReferenceToEH(const char* ehName, pint_t ehAtomAddress, const macho_section<P>* ehSect);
	Reference<A>*								makeReferenceToSymbol(Kinds kind, uint32_t atAddr, const macho_nlist<P>* toSymbol, uint32_t toOffset);
	void										validSectionType(uint8_t type);
	void										handleAnonymousNonLazyPointers(const macho_section<P>* sect);

	BaseAtom*									findAtomByName(const char*);

	const char*									fPath;
	time_t										fModTime;
	const ObjectFile::ReaderOptions&			fOptions;
	const macho_header<P>*						fHeader;
	const char*									fStrings;
	const macho_nlist<P>*						fSymbols;
	uint32_t									fSymbolCount;
	const macho_segment_command<P>*				fSegment;
	const uint32_t*								fIndirectTable;
	std::vector<ObjectFile::Atom*>				fAtoms;
	std::map<uint32_t, BaseAtom*>				fAddrToAtom;
	std::vector<class AnonymousAtom<A>*>		fLocalNonLazys;
	ObjectFile::Reader::DebugInfoKind			fDebugInfo;
	bool										fHasUUID;
	const macho_section<P>*						fDwarfDebugInfoSect;
	const macho_section<P>*						fDwarfDebugAbbrevSect;
	const macho_section<P>*						fDwarfDebugLineSect;
	const char*									fDwarfTranslationUnitDir;
	const char*									fDwarfTranslationUnitFile;
	std::map<uint32_t,const char*>				fDwarfIndexToFile;
	std::vector<Stab>							fStabs;
	bool										fAppleObjc;
};

// usually do nothing
template <typename A> void Reader<A>::handleAnonymousNonLazyPointers(const macho_section<P>* sect) {  }

// HACK for ppc64, need to split of anonymous non-lazy-pointers because they must be 8-byte aligned to work with ld instruction
template <> void 
Reader<ppc64>::handleAnonymousNonLazyPointers(const macho_section<P>* dataSect) { 
	if ( (dataSect->size() >= sizeof(pint_t)) 
		&& (dataSect->align() >= log2(sizeof(pint_t)))
		&& (strcmp(dataSect->sectname(), "__data") == 0)
		&& (strcmp(dataSect->segname(), "__DATA") == 0) ) {
			std::set<uint32_t> lo14targets;
			const macho_section<P>* const sectionsStart = (macho_section<P>*)((char*)fSegment + sizeof(macho_segment_command<P>));
			const macho_section<P>* const sectionsEnd = &sectionsStart[fSegment->nsects()];
			for (const macho_section<P>* sect=sectionsStart; sect < sectionsEnd; ++sect) {
				if ( strncmp(sect->sectname(), "__text", 6) == 0 ) {
					const macho_relocation_info<P>* relocs = (macho_relocation_info<P>*)((char*)(fHeader) + sect->reloff());
					const macho_relocation_info<P>* relocsEnd = &relocs[sect->nreloc()];
					for (const macho_relocation_info<P>* r = relocs; r < relocsEnd; ++r) {	
						if ( (r->r_address() & R_SCATTERED) != 0 ) {
							const macho_scattered_relocation_info<P>* sreloc = (macho_scattered_relocation_info<P>*)r;
							if ( sreloc->r_type() == PPC_RELOC_LO14_SECTDIFF ) {
								lo14targets.insert(sreloc->r_value());
							}
						}
					}
				}
			}
			// walk backwards so that newly created anonymous atoms do not mask misalignmented
			for (std::set<uint32_t>::reverse_iterator it=lo14targets.rbegin(); it != lo14targets.rend(); it++) {
				uint32_t targetOfLO14 = *it;
				AtomAndOffset found = this->findAtomAndOffset(targetOfLO14);
				if ( (found.offset & 0x7) != 0 ) {
					AnonymousAtom<ppc64>* newAtom = new AnonymousAtom<ppc64>(*this, dataSect, targetOfLO14, sizeof(pint_t));
					newAtom->fReallyNonLazyPointer = true;
					fAtoms.push_back(newAtom);
					fAddrToAtom[targetOfLO14] = newAtom;
				}
			}
	}
}

template <typename A>
Reader<A>::Reader(const uint8_t* fileContent, const char* path, time_t modTime, const ObjectFile::ReaderOptions& options)
	: fPath(strdup(path)), fModTime(modTime), fOptions(options), fHeader((const macho_header<P>*)fileContent),
	 fStrings(NULL), fSymbols(NULL), fSymbolCount(0), fSegment(NULL), fIndirectTable(NULL),
	 fDebugInfo(kDebugInfoNone), fHasUUID(false), fDwarfDebugInfoSect(NULL), fDwarfDebugAbbrevSect(NULL),
	  fDwarfTranslationUnitDir(NULL), fDwarfTranslationUnitFile(NULL), fAppleObjc(false)
{
	// sanity check
	if ( ! validFile(fileContent) )
		throw "not a valid mach-o object file";

	// cache intersting pointers
	const macho_header<P>* header = (const macho_header<P>*)fileContent;
	const uint32_t cmd_count = header->ncmds();
	const macho_load_command<P>* const cmds = (macho_load_command<P>*)((char*)header + sizeof(macho_header<P>));
	const macho_load_command<P>* cmd = cmds;
	uint32_t undefinedStartIndex = 0;
	uint32_t undefinedEndIndex = 0;
	for (uint32_t i = 0; i < cmd_count; ++i) {
		switch (cmd->cmd()) {
		    case LC_SYMTAB:
				{
					const macho_symtab_command<P>* symtab = (macho_symtab_command<P>*)cmd;
					fSymbolCount = symtab->nsyms();
					fSymbols = (const macho_nlist<P>*)((char*)header + symtab->symoff());
					fStrings = (char*)header + symtab->stroff();
				}
				break;
			case LC_DYSYMTAB:
				{
					const macho_dysymtab_command<P>* dsymtab = (struct macho_dysymtab_command<P>*)cmd;
					fIndirectTable = (uint32_t*)((char*)fHeader + dsymtab->indirectsymoff());
					undefinedStartIndex = dsymtab->iundefsym();
					undefinedEndIndex = undefinedStartIndex + dsymtab->nundefsym();
				}
				break;
		    case LC_UUID:
				fHasUUID = true;
				break;

			default:
				if ( cmd->cmd() == macho_segment_command<P>::CMD ) {
					fSegment = (macho_segment_command<P>*)cmd;
				}
				break;
		}
		cmd = (const macho_load_command<P>*)(((char*)cmd)+cmd->cmdsize());
	}
	const macho_section<P>* const sectionsStart = (macho_section<P>*)((char*)fSegment + sizeof(macho_segment_command<P>));
	const macho_section<P>* const sectionsEnd = &sectionsStart[fSegment->nsects()];

	// inital guess for number of atoms
	fAtoms.reserve(fSymbolCount);

	// add all atoms that have entries in symbol table
	const macho_section<P>* sections = (macho_section<P>*)((char*)fSegment + sizeof(macho_segment_command<P>));
	for (uint32_t i=0; i < fSymbolCount; ++i) {
		const macho_nlist<P>& sym = fSymbols[i];
		if ( (sym.n_type() & N_STAB) == 0 ) {
			uint8_t type =  (sym.n_type() & N_TYPE);
			if ( type == N_SECT ) {
				const macho_section<P>* section	= &sections[sym.n_sect()-1];
				bool suppress = false;
				// ignore atoms in debugger sections
				if ( (section->flags() & S_ATTR_DEBUG) == 0 ) {
					// ignore labels for atoms in other sections
					switch ( section->flags() & SECTION_TYPE ) {
						case S_REGULAR:
							if ( (sym.n_desc() & N_WEAK_DEF) && strcmp(section->sectname(), "__picsymbolstub1__TEXT") == 0 )
								suppress = true; // ignore stubs in crt1.o built by old ld64 that was missing S_SYMBOL_STUBS
						case S_ZEROFILL:
						case S_COALESCED:
						case S_4BYTE_LITERALS:
						case S_8BYTE_LITERALS:
						case S_16BYTE_LITERALS:
						case S_CSTRING_LITERALS:
							{
								BaseAtom* newAtom = new SymbolAtom<A>(*this, &sym, section);
								std::map<uint32_t, BaseAtom*>::iterator pos = fAddrToAtom.find(sym.n_value());
								if ( pos != fAddrToAtom.end() ) {
									// another label to an existing address
									// make this one be the real one and followed by the previous 
									BaseAtom* existingAtom = pos->second;
									//fprintf(stderr, "new atom %s has same address as existing atom %s\n", newAtom->getDisplayName(), existingAtom->getDisplayName());
									new Reference<A>(A::kFollowOn, AtomAndOffset(newAtom), AtomAndOffset(existingAtom));
									newAtom->setSize(0);
								}
								else {
									fAddrToAtom[sym.n_value()] = newAtom;
								}
								if ( ! suppress )
									fAtoms.push_back(newAtom);
								}
							break;
						case S_SYMBOL_STUBS:
						case S_LAZY_SYMBOL_POINTERS:
						case S_NON_LAZY_SYMBOL_POINTERS:
							// ignore symboled stubs produces by old ld64
							break;
						default:
							fprintf(stderr, "ld64 warning: symbol %s found in unsupported section in %s\n",
								&fStrings[sym.n_strx()], this->getPath());
					}
				}
			}
			else if ( (type == N_UNDF) && (sym.n_value() != 0) ) {
				fAtoms.push_back(new TentativeAtom<A>(*this, &sym));
			}
			else if ( (type == N_ABS) && (strncmp(&fStrings[sym.n_strx()], ".objc_class_name_", 16) == 0) ) {
				fAppleObjc = true;
			}
		}
	}

	// sort SymbolAtoms by address
	std::sort(fAtoms.begin(), fAtoms.end(), SymbolAtomSorter<A>(fAddrToAtom));

	// add all fixed size anonymous atoms from special sections
	for (const macho_section<P>* sect=sectionsStart; sect < sectionsEnd; ++sect) {
		uint32_t atomSize = 0;
		uint8_t type (sect->flags() & SECTION_TYPE);
		validSectionType(type);
		bool suppress = false;
		switch ( type ) {
			case S_SYMBOL_STUBS:
				suppress = true;
				atomSize = sect->reserved2();
				break;
			case S_LAZY_SYMBOL_POINTERS:
				suppress = true;
				atomSize = sizeof(pint_t);
				break;
			case S_NON_LAZY_SYMBOL_POINTERS:
			case S_LITERAL_POINTERS:
			case S_MOD_INIT_FUNC_POINTERS:
			case S_MOD_TERM_FUNC_POINTERS:
				atomSize = sizeof(pint_t);
				break;
			case S_INTERPOSING:
				atomSize = sizeof(pint_t)*2;
				break;
			case S_4BYTE_LITERALS:
				atomSize = 4;
				break;
			case S_8BYTE_LITERALS:
				atomSize = 8;
				break;
			case S_16BYTE_LITERALS:
				atomSize = 16;
				break;
			case S_REGULAR:
				// special case ObjC classes to synthesize .objc_class_name_* symbols
				if ( (strcmp(sect->sectname(), "__class") == 0) && (strcmp(sect->segname(), "__OBJC") == 0) && fAppleObjc ) {
					// gcc sometimes over aligns class structure
					uint32_t align = 1 << sect->align();
					atomSize = ((12 * sizeof(pint_t)) + align-1) & (-align);
					}
				break;
		}
		if ( atomSize != 0 ) {
			for(uint32_t sectOffset=0; sectOffset < sect->size(); sectOffset += atomSize) {
				uint32_t atomAddr = sect->addr() + sectOffset;
				// add if not already an atom at that address
				if ( fAddrToAtom.find(atomAddr) == fAddrToAtom.end() ) {
					AnonymousAtom<A>* newAtom = new AnonymousAtom<A>(*this, sect, atomAddr, atomSize);
					if ( !suppress )
						fAtoms.push_back(newAtom);
					fAddrToAtom[atomAddr] = newAtom->redirectTo();
				}
			}
		}
	}

	// add all c-string anonymous atoms
	for (const macho_section<P>* sect=sectionsStart; sect < sectionsEnd; ++sect) {
		if ( ((sect->flags() & SECTION_TYPE) == S_CSTRING_LITERALS) || strcmp(sect->sectname(), "__cstring") == 0 ) {
			uint32_t stringLen;
			uint32_t stringAddr;
			BaseAtom* firstEmptyString = NULL;
			for(uint32_t sectOffset=0; sectOffset < sect->size(); sectOffset += stringLen) {
				stringAddr = sect->addr() + sectOffset;
				stringLen  = strlen((char*)(fHeader) + sect->offset() + sectOffset) + 1;
				// add if not already an atom at that address
				if ( fAddrToAtom.find(stringAddr) == fAddrToAtom.end() ) {
					BaseAtom* newAtom = new AnonymousAtom<A>(*this, sect, stringAddr, stringLen);
					if ( stringLen == 1 ) {
						// because of padding it may look like there are lots of empty strings
						// map them all to the first empty string
						if ( firstEmptyString == NULL ) {
							firstEmptyString = newAtom;
							fAtoms.push_back(firstEmptyString);
						}
						fAddrToAtom[stringAddr] = firstEmptyString;
					}
					else {
						fAtoms.push_back(newAtom);
						fAddrToAtom[stringAddr] = newAtom;
					}
				}
			}
		}
	}

	// create atoms to cover any non-debug ranges not handled above
	for (const macho_section<P>* sect=sectionsStart; sect < sectionsEnd; ++sect) {
		pint_t sectionStartAddr = sect->addr();
		pint_t sectionEndAddr   = sect->addr() + sect->size();
		const bool setFollowOnAtom = ((fHeader->flags() & MH_SUBSECTIONS_VIA_SYMBOLS) == 0);
		if ( sect->size() != 0 ) {
			// ignore dwarf sections.  If ld every supports processing dwarf, this logic will need to change
			if ( (sect->flags() & S_ATTR_DEBUG) != 0 ) {
				fDebugInfo = kDebugInfoDwarf;
				if ( strcmp(sect->sectname(), "__debug_info") == 0 )
					fDwarfDebugInfoSect = sect;
				else if ( strcmp(sect->sectname(), "__debug_abbrev") == 0 )
					fDwarfDebugAbbrevSect = sect;
				else if ( strcmp(sect->sectname(), "__debug_line") == 0 )
					fDwarfDebugLineSect = sect;
			}
			else {
				if ( strcmp(sect->segname(), "__DWARFA") == 0 ) {
					throw "object file contains old DWARF debug info - rebuild with newer compiler";
				}
				uint8_t type (sect->flags() & SECTION_TYPE);
				switch ( type ) {
					case S_REGULAR:
					case S_ZEROFILL:
					case S_COALESCED:
						// HACK until compiler stops generated anonymous non-lazy pointers rdar://problem/4513414
						handleAnonymousNonLazyPointers(sect); 
						// if there is not an atom already at the start of this section, add an anonymous one
						uint32_t previousAtomAddr = 0;
						BaseAtom* previousAtom = NULL;
						if ( fAddrToAtom.find(sectionStartAddr) == fAddrToAtom.end() ) {
							BaseAtom* newAtom = new AnonymousAtom<A>(*this, sect, sect->addr(), 0);
							fAtoms.push_back(newAtom);
							fAddrToAtom[sect->addr()] = newAtom;
							previousAtomAddr = sectionStartAddr;
							previousAtom = newAtom;
						}
						// calculate size of all atoms in this section and add follow-on references
						for (std::map<uint32_t, BaseAtom*>::iterator it=fAddrToAtom.begin(); it != fAddrToAtom.end(); it++) {
							// note: this algorithm depends on the map iterator returning entries in address order
							if ( (it->first >= sectionStartAddr) && (it->first < sectionEndAddr) ) {
								//fprintf(stderr, "  atom %s in section\n", it->second->getDisplayName());
								if ( previousAtom != NULL ) {
									previousAtom->setSize(it->first - previousAtomAddr);
									// FIX FIX: this setting of followOn atoms does not work when there are multiple
									// labels for the same atom
									if ( setFollowOnAtom && (it->second != previousAtom) )
										makeReference(A::kFollowOn, previousAtomAddr, it->first);
								}
								previousAtomAddr = it->first;
								previousAtom = it->second;
							}
						}
						if ( previousAtom != NULL ) {
							// set last atom in section
							previousAtom->setSize(sectionEndAddr - previousAtomAddr);
						}
						break;
				}
			}
		}
	}

	// add relocation based references
	for (const macho_section<P>* sect=sectionsStart; sect < sectionsEnd; ++sect) {
		// ignore dwarf sections.  If ld every supports processing dwarf, this logic will need to change
		if ( (sect->flags() & S_ATTR_DEBUG) == 0 ) {
			switch ( sect->flags() & SECTION_TYPE ) {
				case S_SYMBOL_STUBS:
				case S_LAZY_SYMBOL_POINTERS:
					// we ignore compiler generated stubs, so ignore those relocs too
					break;
				default:
					const macho_relocation_info<P>* relocs = (macho_relocation_info<P>*)((char*)(fHeader) + sect->reloff());
					const uint32_t relocCount = sect->nreloc();
					//fprintf(stderr, "relocCount = %d in section %s\n", relocCount, sect->sectname());
					for (uint32_t r = 0; r < relocCount; ++r) {
						try {
							if ( addRelocReference(sect, &relocs[r]) )
								++r; // skip next
						}
						catch (const char* msg) {
							throwf("in section %s,%s reloc %u: %s\n", sect->segname(), sect->sectname(), r, msg);
						}
					}
			}
		}
	}

	// check of object file that defines no classes, but uses classes
	if ( !fAppleObjc ) {
		for (uint32_t i=undefinedStartIndex; i < undefinedEndIndex; ++i) {
			const macho_nlist<P>& sym = fSymbols[i];
			if ( (sym.n_type() & N_STAB) == 0 ) {
				if ( ((sym.n_type() & N_TYPE) == N_UNDF) && (strncmp(&fStrings[sym.n_strx()], ".objc_class_name_", 16) == 0) ) {
					fAppleObjc = true;
					break;
				}
			}
		}
	}

	// add objective-c references
	if ( fAppleObjc ) {
		for (const macho_section<P>* sect=sectionsStart; sect < sectionsEnd; ++sect) {
			// ignore dwarf sections.  If ld every supports processing dwarf, this logic will need to change
			if ( (strcmp(sect->sectname(), "__class") == 0) && (strcmp(sect->segname(), "__OBJC") == 0) ) {
				// gcc sometimes over aligns class structure
				uint32_t align = 1 << sect->align();
				uint32_t classSize = ((12 * sizeof(pint_t)) + align-1) & (-align);
				for (uint32_t offset = 0; offset < sect->size(); offset += classSize) {
					// add by-name reference to super class
					uint32_t superClassNameAddr =  P::getP(*(pint_t*)(((uint8_t*)fHeader) + sect->offset() + offset + sizeof(pint_t)));
					const char* superStr = (char*)(fHeader) + sect->offset() + superClassNameAddr - sect->addr();
					const char* superClassName;
					asprintf((char**)&superClassName, ".objc_class_name_%s", superStr);
					makeByNameReference(A::kNoFixUp, sect->addr()+offset+sizeof(pint_t), superClassName, 0);
				}
			}
			else if ( (strcmp(sect->sectname(), "__cls_refs") == 0) && (strcmp(sect->segname(), "__OBJC") == 0) ) {
				for (uint32_t offset = 0; offset < sect->size(); offset += sizeof(pint_t)) {
					// scan through __cls_refs and add by-name reference for each required class
					uint32_t classNameAddr =  P::getP(*(pint_t*)(((uint8_t*)fHeader) + sect->offset() + offset));
					const char* classStr = (char*)(fHeader) + sect->offset() + classNameAddr - sect->addr();
					const char* className;
					asprintf((char**)&className, ".objc_class_name_%s", classStr);
					makeByNameReference(A::kNoFixUp, sect->addr()+offset, className, 0);
				}
			}
		}
	}

	// add direct references to local non-lazy-pointers, can do this now that all atoms are constructed
	for (typename std::vector<AnonymousAtom<A>*>::iterator it=fLocalNonLazys.begin(); it != fLocalNonLazys.end(); it++) {
		AnonymousAtom<A>* localNonLazy = *it;
		uint32_t fileOffset = localNonLazy->fSection->offset() - localNonLazy->fSection->addr() + localNonLazy->fAddress;
		pint_t nonLazyPtrValue = P::getP(*((pint_t*)((char*)(fHeader)+fileOffset)));
		makeReference(A::kPointer, localNonLazy->fAddress, nonLazyPtrValue);
	}

	// add implicit direct reference from each C++ function to its eh info
	for (const macho_section<P>* sect=sectionsStart; sect < sectionsEnd; ++sect) {
		if ( ((sect->flags() & SECTION_TYPE) == S_COALESCED) && (strcmp(sect->sectname(), "__eh_frame") == 0) ) {
			for (std::map<uint32_t, BaseAtom*>::iterator it=fAddrToAtom.begin(); it != fAddrToAtom.end(); it++) {
				// note: this algorithm depens on the map iterator returning entries in address order
				if ( (it->first >= sect->addr()) && (it->first < sect->addr()+sect->size()) ) {
					uint32_t ehAtomAddress = it->first;
					BaseAtom* ehAtom = it->second;
					const char* ehName = ehAtom->getName();
					if ( (ehName != NULL) && (strcmp(&ehName[strlen(ehName)-3], ".eh") == 0) ) 
						makeReferenceToEH(ehName, ehAtomAddress, sect);
				}
			}
		}
	}


	//for (std::map<uint32_t, BaseAtom*>::iterator it=fAddrToAtom.begin(); it != fAddrToAtom.end(); it++) {
	//	fprintf(stderr, "[0x%0X -> 0x%0llX) : %s\n", it->first, it->first+it->second->getSize(), it->second->getDisplayName());
	//}

	// add translation unit info from dwarf
	uint64_t stmtList;
	if ( (fDebugInfo == kDebugInfoDwarf) && (fOptions.fDebugInfoStripping != ObjectFile::ReaderOptions::kDebugInfoNone) ) {
		// compiler sometimes emits emtpty dwarf sections when there is no debug info, skip those
		if ( (fDwarfDebugInfoSect != NULL) && (fDwarfDebugInfoSect->size() != 0) ) {
			if ( !read_comp_unit(&fDwarfTranslationUnitFile, &fDwarfTranslationUnitDir, &stmtList) ) {
				// if can't parse dwarf, warn and give up
				fDwarfTranslationUnitFile = NULL;
				fDwarfTranslationUnitDir = NULL;
				fprintf(stderr, "ld64: warning can't parse dwarf compilation unit info in %s\n", this->getPath());
				fDebugInfo = kDebugInfoNone;
			}
		}
	}

	// add line number info to atoms from dwarf
	if ( (fDebugInfo == kDebugInfoDwarf) && (fOptions.fDebugInfoStripping != ObjectFile::ReaderOptions::kDebugInfoNone) ) {
		// file with just data will have no __debug_line info
		if ( (fDwarfDebugLineSect != NULL) && (fDwarfDebugLineSect->size() != 0) && (fAddrToAtom.size() != 0) ) {
			// validate stmt_list
			if ( (stmtList != (uint64_t)-1) && (stmtList < fDwarfDebugLineSect->size()) ) {
				const uint8_t* debug_line = (uint8_t*)(fHeader) + fDwarfDebugLineSect->offset();
				if ( debug_line != NULL ) {
					struct line_reader_data* lines = line_open(&debug_line[stmtList],
															fDwarfDebugLineSect->size() - stmtList, E::little_endian);
					struct line_info result;
					ObjectFile::Atom* curAtom = NULL;
					uint32_t curAtomOffset = 0;
					uint32_t curAtomAddress = 0;
					uint32_t curAtomSize = 0;
					while ( line_next (lines, &result, line_stop_pc) ) {
						// for performance, see if in next pc is in current atom
						if ( (curAtom != NULL) && (curAtomAddress <= result.pc) && (result.pc < (curAtomAddress+curAtomSize)) ) {
							curAtomOffset = result.pc - curAtomAddress;
						}
						// or pc at end of current atom
						else if ( result.end_of_sequence && (curAtom != NULL) && (result.pc == (curAtomAddress+curAtomSize)) ) {
							curAtomOffset = result.pc - curAtomAddress;
						}
						else {
							// do slow look up of atom by address
							AtomAndOffset ao = this->findAtomAndOffset(result.pc);
							curAtom			= ao.atom;
							if ( curAtom == NULL )
								break; // file has line info but no functions
							curAtomOffset	= ao.offset;
							curAtomAddress	= result.pc - ao.offset;
							curAtomSize		= curAtom->getSize();
						}
						const char* filename;
						std::map<uint32_t,const char*>::iterator pos = fDwarfIndexToFile.find(result.file);
						if ( pos == fDwarfIndexToFile.end() ) {
							filename = line_file(lines, result.file);
							fDwarfIndexToFile[result.file] = filename;
						}
						else {
							filename = pos->second;
						}
						ObjectFile::LineInfo info;
						info.atomOffset = curAtomOffset;
						info.fileName = filename;
						info.lineNumber = result.line;
						//fprintf(stderr, "addr=0x%08llX, line=%lld, file=%s, atom=%s, atom.size=0x%X, end=%d\n", 
						//		result.pc, result.line, filename, curAtom->getDisplayName(), curAtomSize, result.end_of_sequence);
						((BaseAtom*)curAtom)->addLineInfo(info);
						if ( result.end_of_sequence ) {
							curAtom = NULL;
						}
					}
					line_free(lines);
				}
				else {
					fprintf(stderr, "ld64: warning could not parse dwarf line number info in %s\n", this->getPath());
				}
			}
		}
	}

	// if no dwarf, try processing stabs debugging info
	if ( (fDebugInfo == kDebugInfoNone) && (fOptions.fDebugInfoStripping != ObjectFile::ReaderOptions::kDebugInfoNone) ) {
		// scan symbol table for stabs entries
		fStabs.reserve(fSymbolCount);  // reduce re-allocations
		BaseAtom* currentAtom = NULL;
		pint_t currentAtomAddress = 0;
		enum { start, inBeginEnd, inFun } state = start;
		for (uint32_t symbolIndex = 0; symbolIndex < fSymbolCount; ++symbolIndex ) {
			const macho_nlist<P>* sym = &fSymbols[symbolIndex];
			bool useStab = true;
			uint8_t type = sym->n_type();
			const char* symString = (sym->n_strx() != 0) ? &fStrings[sym->n_strx()] : NULL;
			if ( (type & N_STAB) != 0 ) {
				fDebugInfo =  (fHasUUID ? kDebugInfoStabsUUID : kDebugInfoStabs);
				Stab stab;
				stab.atom	= NULL;
				stab.type	= type;
				stab.other	= sym->n_sect();
				stab.desc	= sym->n_desc();
				stab.value	= sym->n_value();
				stab.string = NULL;
				switch (state) {
					case start:
						switch (type) {
							case N_BNSYM:
								// beginning of function block
								state = inBeginEnd;
								// fall into case to lookup atom by addresss
							case N_LCSYM:
							case N_STSYM:
								currentAtomAddress = sym->n_value();
								currentAtom = (BaseAtom*)this->findAtomAndOffset(currentAtomAddress).atom;
								if ( currentAtom != NULL ) {
									stab.atom = currentAtom;
									stab.string = symString;
								}
								else {
									fprintf(stderr, "can't find atom for stabs BNSYM at %08llX in %s\n",
										(uint64_t)sym->n_value(), path);
								}
								break;
							case N_SO:
							case N_OSO:
							case N_OPT:
							case N_LSYM:
								// not associated with an atom, just copy
								stab.string = symString;
								break;
							case N_GSYM:
								// n_value field is NOT atom address ;-(
								// need to find atom by name match
								const char* colon = strchr(symString, ':');
								if ( colon != NULL ) {
									// build underscore leading name
									int nameLen = colon - symString;
									char symName[nameLen+2];
									strlcpy(&symName[1], symString, nameLen+1);
									symName[0] = '_';
									symName[nameLen+1] = '\0';
									currentAtom = findAtomByName(symName);
									if ( currentAtom != NULL ) {
										stab.atom = currentAtom;
										stab.string = symString;
									}
								}
								if ( stab.atom == NULL ) {
									fprintf(stderr, "can't find atom for N_GSYM stabs %s in %s\n", symString, path);
									useStab = false;
								}
								break;
							case N_FUN:
								// old style stabs without BNSYM
								state = inFun;
								currentAtomAddress = sym->n_value();
								currentAtom = (BaseAtom*)this->findAtomAndOffset(currentAtomAddress).atom;
								if ( currentAtom != NULL ) {
									stab.atom = currentAtom;
									stab.string = symString;
								}
								else {
									fprintf(stderr, "can't find atom for stabs FUN at %08llX in %s\n",
										(uint64_t)currentAtomAddress, path);
								}
								break;
							case N_SOL:
							case N_SLINE:
								stab.string = symString;
								// old stabs
								break;
							case N_BINCL:
							case N_EINCL:
							case N_EXCL:
								stab.string = symString;
								// -gfull built .o file
								break;
							default:
								fprintf(stderr, "unknown stabs type 0x%X in %s\n", type, path);
						}
						break;
					case inBeginEnd:
						stab.atom = currentAtom;
						switch (type) {
							case N_ENSYM:
								state = start;
								currentAtom = NULL;
								break;
							case N_LCSYM:
							case N_STSYM:
								BaseAtom* nestedAtom = (BaseAtom*)this->findAtomAndOffset(sym->n_value()).atom;
								if ( nestedAtom != NULL ) {
									stab.atom = nestedAtom;
									stab.string = symString;
								}
								else {
									fprintf(stderr, "can't find atom for stabs 0x%X at %08llX in %s\n",
										type, (uint64_t)sym->n_value(), path);
								}
								break;
							case N_LBRAC:
							case N_RBRAC:
							case N_SLINE:
								// adjust value to be offset in atom
								stab.value -= currentAtomAddress;
							default:
								stab.string = symString;
								break;
						}
						break;
					case inFun:
						switch (type) {
							case N_FUN:
								if ( sym->n_sect() != 0 ) {
									// found another start stab, must be really old stabs...
									currentAtomAddress = sym->n_value();
									currentAtom = (BaseAtom*)this->findAtomAndOffset(currentAtomAddress).atom;
									if ( currentAtom != NULL ) {
										stab.atom = currentAtom;
										stab.string = symString;
									}
									else {
										fprintf(stderr, "can't find atom for stabs FUN at %08llX in %s\n",
											(uint64_t)currentAtomAddress, path);
									}
								}
								else {
									// found ending stab, switch back to start state
									stab.string = symString;
									stab.atom = currentAtom;
									state = start;
									currentAtom = NULL;
								}
								break;
							case N_LBRAC:
							case N_RBRAC:
							case N_SLINE:
								// adjust value to be offset in atom
								stab.value -= currentAtomAddress;
								stab.atom = currentAtom;
								break;
							case N_SO:
								stab.string = symString;
								state = start;
								break;
							default:
								stab.atom = currentAtom;
								stab.string = symString;
								break;
						}
						break;
				}
				// add to list of stabs for this .o file
				if ( useStab )
					fStabs.push_back(stab);
			}
		}
	}


#if 0
	// special case precompiled header .o file (which has no content) to have one empty atom
	if ( fAtoms.size() == 0 ) {
		int pathLen = strlen(path);
		if ( (pathLen > 6) && (strcmp(&path[pathLen-6], ".gch.o")==0) ) {
			ObjectFile::Atom* phony = new AnonymousAtom<A>(*this, (uint32_t)0);
			//phony->fSynthesizedName = ".gch.o";
			fAtoms.push_back(phony);
		}
	}
#endif
}

template <>
void Reader<x86_64>::validSectionType(uint8_t type)
{
	switch ( type ) {
		case S_SYMBOL_STUBS:
			throw "symbol_stub sections not valid in x86_64 object files";
		case S_LAZY_SYMBOL_POINTERS:
			throw "lazy pointer sections not valid in x86_64 object files";
		case S_NON_LAZY_SYMBOL_POINTERS:
			throw "non lazy pointer sections not valid in x86_64 object files";
	}
}

template <typename A>
void Reader<A>::validSectionType(uint8_t type)
{
}

template <typename A>
bool Reader<A>::getTranslationUnitSource(const char** dir, const char** name) const
{
	if ( fDebugInfo == kDebugInfoDwarf ) {
		*dir = fDwarfTranslationUnitDir;
		*name = fDwarfTranslationUnitFile;
		return true;
	}
	return false;
}

template <typename A>
BaseAtom* Reader<A>::findAtomByName(const char* name)
{
	// first search the more important atoms
	for (std::map<uint32_t, BaseAtom*>::iterator it=fAddrToAtom.begin(); it != fAddrToAtom.end(); it++) {
		const char* atomName = it->second->getName();
		if ( (atomName != NULL) && (strcmp(atomName, name) == 0) ) {
			return it->second;
		}
	}
	// try all atoms, because this might have been a tentative definition
	for (std::vector<ObjectFile::Atom*>::iterator it=fAtoms.begin(); it != fAtoms.end(); it++) {
		BaseAtom* atom = (BaseAtom*)(*it);
		const char* atomName = atom->getName();
		if ( (atomName != NULL) && (strcmp(atomName, name) == 0) ) {
			return atom;
		}
	}
	return NULL;
}

template <typename A>
Reference<A>* Reader<A>::makeReference(Kinds kind, uint32_t atAddr, uint32_t toAddr)
{
	return new Reference<A>(kind, findAtomAndOffset(atAddr), findAtomAndOffset(toAddr));
}

template <typename A>
Reference<A>* Reader<A>::makeReference(Kinds kind, uint32_t atAddr, uint32_t fromAddr, uint32_t toAddr)
{
	return new Reference<A>(kind, findAtomAndOffset(atAddr), findAtomAndOffset(fromAddr), findAtomAndOffset(toAddr));
}

template <typename A>
Reference<A>* Reader<A>::makeReferenceWithToBase(Kinds kind, uint32_t atAddr, uint32_t toAddr, uint32_t toBaseAddr)
{
	return new Reference<A>(kind, findAtomAndOffset(atAddr), findAtomAndOffset(toBaseAddr, toAddr));
}

template <typename A>
Reference<A>* Reader<A>::makeReferenceWithToBase(Kinds kind, uint32_t atAddr, uint32_t fromAddr, uint32_t toAddr, uint32_t toBaseAddr)
{
	return new Reference<A>(kind, findAtomAndOffset(atAddr), findAtomAndOffset(fromAddr), findAtomAndOffset(toBaseAddr, toAddr));
}

template <typename A>
Reference<A>* Reader<A>::makeByNameReference(Kinds kind, uint32_t atAddr, const char* toName, uint32_t toOffset)
{
	return new Reference<A>(kind, findAtomAndOffset(atAddr), toName, toOffset);
}

template <typename A>
Reference<A>* Reader<A>::makeReferenceToEH(const char* ehName, pint_t ehAtomAddress, const macho_section<P>* ehSect)
{
	// add a direct reference from function atom to its eh frame atom
	const uint8_t* ehContent = (const uint8_t*)(fHeader) + ehAtomAddress - ehSect->addr() + ehSect->offset();
	int32_t deltaMinus8 = P::getP(*(pint_t*)(&ehContent[8]));	// offset 8 in eh info is delta to function
	uint32_t funcAddr = ehAtomAddress + deltaMinus8 + 8;
	return makeReference(A::kNoFixUp, funcAddr, ehAtomAddress);
}


template <>
Reference<x86_64>* Reader<x86_64>::makeByNameReference(Kinds kind, uint32_t atAddr, const char* toName, uint32_t toOffset)
{
	// x86_64 uses external relocations everywhere, so external relocations do not imply by-name references
	// instead check scope of target
	BaseAtom* target = findAtomByName(toName);
	if ( (target != NULL) && (target->getScope() == ObjectFile::Atom::scopeTranslationUnit) )
		return new Reference<x86_64>(kind, findAtomAndOffset(atAddr), AtomAndOffset(target, toOffset));
	else
		return new Reference<x86_64>(kind, findAtomAndOffset(atAddr), toName, toOffset);
}

template <>
Reference<x86_64>* Reader<x86_64>::makeReferenceToSymbol(Kinds kind, uint32_t atAddr, const macho_nlist<P>* toSymbol, uint32_t toOffset)
{
	// x86_64 uses external relocations everywhere, so external relocations do not imply by-name references
	// instead check scope of target
	if ( ((toSymbol->n_type() & N_TYPE) == N_SECT) && ((toSymbol->n_type() & N_EXT) == 0) ) 
		return new Reference<x86_64>(kind, findAtomAndOffset(atAddr), findAtomAndOffset(toSymbol->n_value(), toSymbol->n_value()+toOffset));
	else
		return new Reference<x86_64>(kind, findAtomAndOffset(atAddr), &fStrings[toSymbol->n_strx()], toOffset);
}


template <>
Reference<x86_64>* Reader<x86_64>::makeReferenceToEH(const char* ehName, pint_t ehAtomAddress, const macho_section<P>* ehSect)
{
	// add a direct reference from function atom to its eh frame atom
	// for x86_64 the __eh_frame section contains the addends, so need to use relocs to find target
	uint32_t ehAtomDeltaSectionOffset = ehAtomAddress + 8 - ehSect->addr(); // offset 8 in eh info is delta to function
	const macho_relocation_info<P>* relocs = (macho_relocation_info<P>*)((char*)(fHeader) + ehSect->reloff());
	const macho_relocation_info<P>* relocsEnd = &relocs[ehSect->nreloc()];
	for (const macho_relocation_info<P>* reloc = relocs; reloc < relocsEnd; ++reloc) {
		if ( (reloc->r_address() == ehAtomDeltaSectionOffset) && (reloc->r_type() == X86_64_RELOC_UNSIGNED) ) {
			uint32_t funcAddr = fSymbols[reloc->r_symbolnum()].n_value();
			return makeReference(x86_64::kNoFixUp, funcAddr, ehAtomAddress);
		}
	}
	fprintf(stderr, "ld64: warning, can't find matching function for eh symbol %s\n", ehName);
	return NULL;
}


template <typename A>
AtomAndOffset Reader<A>::findAtomAndOffset(uint32_t addr)
{
	// STL has no built-in for "find largest key that is same or less than"
	std::map<uint32_t, BaseAtom*>::iterator it = fAddrToAtom.upper_bound(addr);
	--it; // upper_bound gets us next key, so we back up one
	AtomAndOffset result;
	result.atom = it->second;
	result.offset = addr - it->first;
	//fprintf(stderr, "findAtomAndOffset(0x%0X) ==> %s (0x%0X -> 0x%0llX)\n",
	//			addr, result.atom->getDisplayName(), it->first, it->first+result.atom->getSize());
	return result;
}

// "scattered" relocations enable you to offset into an atom past the end of it
// baseAddr is the address of the target atom,
// realAddr is the points into it
template <typename A>
AtomAndOffset Reader<A>::findAtomAndOffset(uint32_t baseAddr, uint32_t realAddr)
{
	std::map<uint32_t, BaseAtom*>::iterator it = fAddrToAtom.find(baseAddr);
	if ( it != fAddrToAtom.end() ) {
		AtomAndOffset result;
		result.atom = it->second;
		result.offset = realAddr - it->first;
		//fprintf(stderr, "findAtomAndOffset(0x%08X, 0x%08X) => %s + 0x%08X\n", baseAddr, realAddr, result.atom->getDisplayName(), result.offset);
		return result;
	}
	// getting here means we have a scattered relocation to an address without a label
	// we should never get here...
	// one case we do get here is because sometimes the compiler generates non-lazy pointers in the __data section
	return findAtomAndOffset(realAddr);
}


/* Skip over a LEB128 value (signed or unsigned).  */
static void
skip_leb128 (const uint8_t ** offset, const uint8_t * end)
{
  while (*offset != end && **offset >= 0x80)
    (*offset)++;
  if (*offset != end)
    (*offset)++;
}

/* Read a ULEB128 into a 64-bit word.  Return (uint64_t)-1 on overflow
   or error.  On overflow, skip past the rest of the uleb128.  */
static uint64_t
read_uleb128 (const uint8_t ** offset, const uint8_t * end)
{
  uint64_t result = 0;
  int bit = 0;

  do  {
    uint64_t b;

    if (*offset == end)
      return (uint64_t) -1;

    b = **offset & 0x7f;

    if (bit >= 64 || b << bit >> bit != b)
      result = (uint64_t) -1;
    else
      result |= b << bit, bit += 7;
  } while (*(*offset)++ >= 0x80);
  return result;
}


/* Skip over a DWARF attribute of form FORM.  */
template <typename A>
bool Reader<A>::skip_form(const uint8_t ** offset, const uint8_t * end, uint64_t form,
							uint8_t addr_size, bool dwarf64)
{
  int64_t sz=0;

  switch (form)
    {
    case DW_FORM_addr:
      sz = addr_size;
      break;

    case DW_FORM_block2:
      if (end - *offset < 2)
	return false;
      sz = 2 + A::P::E::get16(*(uint16_t*)offset);
      break;

    case DW_FORM_block4:
      if (end - *offset < 4)
	return false;
      sz = 2 + A::P::E::get32(*(uint32_t*)offset);
      break;

    case DW_FORM_data2:
    case DW_FORM_ref2:
      sz = 2;
      break;

    case DW_FORM_data4:
    case DW_FORM_ref4:
      sz = 4;
      break;

    case DW_FORM_data8:
    case DW_FORM_ref8:
      sz = 8;
      break;

    case DW_FORM_string:
      while (*offset != end && **offset)
	++*offset;
    case DW_FORM_data1:
    case DW_FORM_flag:
    case DW_FORM_ref1:
      sz = 1;
      break;

    case DW_FORM_block:
      sz = read_uleb128 (offset, end);
      break;

    case DW_FORM_block1:
      if (*offset == end)
	return false;
      sz = 1 + **offset;
      break;

    case DW_FORM_sdata:
    case DW_FORM_udata:
    case DW_FORM_ref_udata:
      skip_leb128 (offset, end);
      return true;

    case DW_FORM_strp:
    case DW_FORM_ref_addr:
      sz = dwarf64 ? 8 : 4;
      break;

    default:
      return false;
    }
  if (end - *offset < sz)
    return false;
  *offset += sz;
  return true;
}

// Look at the compilation unit DIE and determine
// its NAME, compilation directory (in COMP_DIR) and its
// line number information offset (in STMT_LIST).  NAME and COMP_DIR
// may be NULL (especially COMP_DIR) if they are not in the .o file;
// STMT_LIST will be (uint64_t) -1.
//
// At present this assumes that there's only one compilation unit DIE.
//
template <typename A>
bool Reader<A>::read_comp_unit(const char ** name, const char ** comp_dir,
							uint64_t *stmt_list)
{
	const uint8_t * debug_info;
	const uint8_t * debug_abbrev;
	const uint8_t * di;
	const uint8_t * da;
	const uint8_t * end;
	const uint8_t * enda;
	uint64_t sz;
	uint16_t vers;
	uint64_t abbrev_base;
	uint64_t abbrev;
	uint8_t address_size;
	bool dwarf64;

	*name = NULL;
	*comp_dir = NULL;
	*stmt_list = (uint64_t) -1;

	if ( (fDwarfDebugInfoSect == NULL) || (fDwarfDebugAbbrevSect == NULL) )
		return false;

	debug_info = (uint8_t*)(fHeader) + fDwarfDebugInfoSect->offset();
	debug_abbrev = (uint8_t*)(fHeader) + fDwarfDebugAbbrevSect->offset();
	di = debug_info;

	if (fDwarfDebugInfoSect->size() < 12)
		/* Too small to be a real debug_info section.  */
		return false;
	sz = A::P::E::get32(*(uint32_t*)di);
	di += 4;
	dwarf64 = sz == 0xffffffff;
	if (dwarf64)
		sz = A::P::E::get64(*(uint64_t*)di), di += 8;
	else if (sz > 0xffffff00)
		/* Unknown dwarf format.  */
		return false;

	/* Verify claimed size.  */
	if (sz + (di - debug_info) > fDwarfDebugInfoSect->size() || sz <= (dwarf64 ? 23 : 11))
		return false;

	vers = A::P::E::get16(*(uint16_t*)di);
	if (vers < 2 || vers > 3)
	/* DWARF version wrong for this code.
	   Chances are we could continue anyway, but we don't know for sure.  */
		return false;
	di += 2;

	/* Find the debug_abbrev section.  */
	abbrev_base = dwarf64 ? A::P::E::get64(*(uint64_t*)di) : A::P::E::get32(*(uint32_t*)di);
	di += dwarf64 ? 8 : 4;

	if (abbrev_base > fDwarfDebugAbbrevSect->size())
		return false;
	da = debug_abbrev + abbrev_base;
	enda = debug_abbrev + fDwarfDebugAbbrevSect->size();

	address_size = *di++;

	/* Find the abbrev number we're looking for.  */
	end = di + sz;
	abbrev = read_uleb128 (&di, end);
	if (abbrev == (uint64_t) -1)
		return false;

	/* Skip through the debug_abbrev section looking for that abbrev.  */
	for (;;)
	{
		uint64_t this_abbrev = read_uleb128 (&da, enda);
		uint64_t attr;

		if (this_abbrev == abbrev)
			/* This is almost always taken.  */
			break;
		skip_leb128 (&da, enda); /* Skip the tag.  */
		if (da == enda)
			return false;
		da++;  /* Skip the DW_CHILDREN_* value.  */

		do {
			attr = read_uleb128 (&da, enda);
			skip_leb128 (&da, enda);
		} while (attr != 0 && attr != (uint64_t) -1);
		if (attr != 0)
			return false;
	}

	/* Check that the abbrev is one for a DW_TAG_compile_unit.  */
	if (read_uleb128 (&da, enda) != DW_TAG_compile_unit)
	return false;
	if (da == enda)
	return false;
	da++;  /* Skip the DW_CHILDREN_* value.  */

	/* Now, go through the DIE looking for DW_AT_name,
	 DW_AT_comp_dir, and DW_AT_stmt_list.  */
	for (;;)
	{
		uint64_t attr = read_uleb128 (&da, enda);
		uint64_t form = read_uleb128 (&da, enda);

		if (attr == (uint64_t) -1)
			return false;
		else if (attr == 0)
			return true;

		if (form == DW_FORM_indirect)
			form = read_uleb128 (&di, end);

		if (attr == DW_AT_name && form == DW_FORM_string)
			*name = (const char *) di;
		else if (attr == DW_AT_comp_dir && form == DW_FORM_string)
			*comp_dir = (const char *) di;
		/* Really we should support DW_FORM_strp here, too, but
		there's usually no reason for the producer to use that form
		 for the DW_AT_name and DW_AT_comp_dir attributes.  */
		else if (attr == DW_AT_stmt_list && form == DW_FORM_data4)
			*stmt_list = A::P::E::get32(*(uint32_t*)di);
		else if (attr == DW_AT_stmt_list && form == DW_FORM_data8)
			*stmt_list = A::P::E::get64(*(uint64_t*)di);
		if (! skip_form (&di, end, form, address_size, dwarf64))
			return false;
	}
}

template <typename A>
const char* Reader<A>::assureFullPath(const char* path)
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
//
//	To implement architecture xxx, you must write template specializations for the following six methods:
//			Reader<xxx>::validFile()
//			Reader<xxx>::addRelocReference()
//			Reference<xxx>::getDescription()
//
//


template <>
bool Reader<ppc>::validFile(const uint8_t* fileContent)
{
	const macho_header<P>* header = (const macho_header<P>*)fileContent;
	if ( header->magic() != MH_MAGIC )
		return false;
	if ( header->cputype() != CPU_TYPE_POWERPC )
		return false;
	if ( header->filetype() != MH_OBJECT )
		return false;
	return true;
}

template <>
bool Reader<ppc64>::validFile(const uint8_t* fileContent)
{
	const macho_header<P>* header = (const macho_header<P>*)fileContent;
	if ( header->magic() != MH_MAGIC_64 )
		return false;
	if ( header->cputype() != CPU_TYPE_POWERPC64 )
		return false;
	if ( header->filetype() != MH_OBJECT )
		return false;
	return true;
}

template <>
bool Reader<x86>::validFile(const uint8_t* fileContent)
{
	const macho_header<P>* header = (const macho_header<P>*)fileContent;
	if ( header->magic() != MH_MAGIC )
		return false;
	if ( header->cputype() != CPU_TYPE_I386 )
		return false;
	if ( header->filetype() != MH_OBJECT )
		return false;
	return true;
}

template <>
bool Reader<x86_64>::validFile(const uint8_t* fileContent)
{
	const macho_header<P>* header = (const macho_header<P>*)fileContent;
	if ( header->magic() != MH_MAGIC_64 )
		return false;
	if ( header->cputype() != CPU_TYPE_X86_64 )
		return false;
	if ( header->filetype() != MH_OBJECT )
		return false;
	return true;
}


template <typename A>
bool Reader<A>::isWeakImportSymbol(const macho_nlist<P>* sym)
{
	return ( ((sym->n_type() & N_TYPE) == N_UNDF) && ((sym->n_desc() & N_WEAK_REF) != 0) );
}

template <>
bool Reader<ppc64>::addRelocReference(const macho_section<ppc64::P>* sect, const macho_relocation_info<ppc64::P>* reloc)
{
	return addRelocReference_powerpc(sect, reloc);
}

template <>
bool Reader<ppc>::addRelocReference(const macho_section<ppc::P>* sect, const macho_relocation_info<ppc::P>* reloc)
{
	return addRelocReference_powerpc(sect, reloc);
}


//
// ppc and ppc64 both use the same relocations, so process them in one common routine
//
template <typename A>
bool Reader<A>::addRelocReference_powerpc(const macho_section<typename A::P>* sect,
										  const macho_relocation_info<typename A::P>* reloc)
{
	uint32_t srcAddr;
	uint32_t dstAddr;
	uint32_t* fixUpPtr;
	int32_t displacement = 0;
	uint32_t instruction = 0;
	uint32_t offsetInTarget;
	int16_t lowBits;
	bool result = false;
	if ( (reloc->r_address() & R_SCATTERED) == 0 ) {
		const macho_relocation_info<P>* nextReloc = &reloc[1];
		const char* targetName = NULL;
		bool weakImport = false;
		fixUpPtr = (uint32_t*)((char*)(fHeader) + sect->offset() + reloc->r_address());
		if ( reloc->r_type() != PPC_RELOC_PAIR )
			instruction = BigEndian::get32(*fixUpPtr);
		srcAddr = sect->addr() + reloc->r_address();
		if ( reloc->r_extern() ) {
			const macho_nlist<P>* targetSymbol = &fSymbols[reloc->r_symbolnum()];
			targetName = &fStrings[targetSymbol->n_strx()];
			weakImport = this->isWeakImportSymbol(targetSymbol);
		}
		switch ( reloc->r_type() ) {
			case PPC_RELOC_BR24:
				{
					if ( (instruction & 0x4C000000) == 0x48000000 ) {
						displacement = (instruction & 0x03FFFFFC);
						if ( (displacement & 0x02000000) != 0 )
							displacement |= 0xFC000000;
					}
					else {
						printf("bad instruction for BR24 reloc");
					}
					if ( reloc->r_extern() ) {
						offsetInTarget = srcAddr + displacement;
						if ( weakImport )
							makeByNameReference(A::kBranch24WeakImport, srcAddr, targetName, offsetInTarget);
						else
							makeByNameReference(A::kBranch24, srcAddr, targetName, offsetInTarget);
					}
					else {
						dstAddr = srcAddr + displacement;
						// if this is a branch to a stub, we need to see if the stub is for a weak imported symbol
						ObjectFile::Atom* atom = findAtomAndOffset(dstAddr).atom;
						if ( (atom->getSymbolTableInclusion() == ObjectFile::Atom::kSymbolTableNotIn)
							&& ((AnonymousAtom<A>*)atom)->isWeakImportStub() )
							makeReference(A::kBranch24WeakImport, srcAddr, dstAddr);
						else
							makeReference(A::kBranch24, srcAddr, dstAddr);
					}
				}
				break;
			case PPC_RELOC_BR14:
				{
					displacement = (instruction & 0x0000FFFC);
					if ( (displacement & 0x00008000) != 0 )
						displacement |= 0xFFFF0000;
					if ( reloc->r_extern() ) {
						offsetInTarget = srcAddr + displacement;
						makeByNameReference(A::kBranch14, srcAddr, targetName, offsetInTarget);
					}
					else {
						dstAddr = srcAddr + displacement;
						makeReference(A::kBranch14, srcAddr, dstAddr);
					}
				}
				break;
			case PPC_RELOC_PAIR:
				// skip, processed by a previous look ahead
				break;
			case PPC_RELOC_LO16:
				{
					if ( nextReloc->r_type() != PPC_RELOC_PAIR ) {
						printf("PPC_RELOC_LO16 missing following pair\n");
						break;
					}
					result = true;
					lowBits = (instruction & 0xFFFF);
					if ( reloc->r_extern() ) {
						offsetInTarget = (nextReloc->r_address() << 16) | ((uint32_t)lowBits & 0x0000FFFF);
						makeByNameReference(A::kAbsLow16, srcAddr, targetName, offsetInTarget);
					}
					else {
						dstAddr = (nextReloc->r_address() << 16) + ((uint32_t)lowBits & 0x0000FFFF);
						makeReference(A::kAbsLow16, srcAddr, dstAddr);
					}
				}
				break;
			case PPC_RELOC_LO14:
				{
					if ( nextReloc->r_type() != PPC_RELOC_PAIR ) {
						printf("PPC_RELOC_LO14 missing following pair\n");
						break;
					}
					result = true;
					lowBits = (instruction & 0xFFFC);
					if ( reloc->r_extern() ) {
						offsetInTarget = (nextReloc->r_address() << 16) | ((uint32_t)lowBits & 0x0000FFFF);
						makeByNameReference(A::kAbsLow14, srcAddr, targetName, offsetInTarget);
					}
					else {
						dstAddr = (nextReloc->r_address() << 16) | ((uint32_t)lowBits & 0x0000FFFF);
						Reference<A>* ref = makeReference(A::kAbsLow14, srcAddr, dstAddr);
						BaseAtom* target = ((BaseAtom*)&(ref->getTarget()));
						if ( target != NULL )
							target->alignAtLeast(3);
					}
				}
				break;
			case PPC_RELOC_HI16:
				{
					if ( nextReloc->r_type() != PPC_RELOC_PAIR ) {
						printf("PPC_RELOC_HI16 missing following pair\n");
						break;
					}
					result = true;
					if ( reloc->r_extern() ) {
						offsetInTarget = ((instruction & 0x0000FFFF) << 16) | (nextReloc->r_address() & 0x0000FFFF);
						makeByNameReference(A::kAbsHigh16, srcAddr, targetName, offsetInTarget);
					}
					else {
						dstAddr = ((instruction & 0x0000FFFF) << 16) | (nextReloc->r_address() & 0x0000FFFF);
						makeReference(A::kAbsHigh16, srcAddr, dstAddr);
					}
				}
				break;
			case PPC_RELOC_HA16:
				{
					if ( nextReloc->r_type() != PPC_RELOC_PAIR ) {
						printf("PPC_RELOC_HA16 missing following pair\n");
						break;
					}
					result = true;
					lowBits = (nextReloc->r_address() & 0x0000FFFF);
					if ( reloc->r_extern() ) {
						offsetInTarget = ((instruction & 0x0000FFFF) << 16) + (int32_t)lowBits;
						makeByNameReference(A::kAbsHigh16AddLow, srcAddr, targetName, offsetInTarget);
					}
					else {
						dstAddr = ((instruction & 0x0000FFFF) << 16) + (int32_t)lowBits;
						makeReference(A::kAbsHigh16AddLow, srcAddr, dstAddr);
					}
				}
				break;
			case PPC_RELOC_VANILLA:
				{
					pint_t pointerValue = P::getP(*((pint_t*)fixUpPtr));
					if ( reloc->r_extern() ) {
						if ( weakImport )
							makeByNameReference(A::kPointerWeakImport, srcAddr, targetName, pointerValue);
						else
							makeByNameReference(A::kPointer, srcAddr, targetName, pointerValue);
					}
					else {
						makeReference(A::kPointer, srcAddr, pointerValue);
					}
				}
				break;
			case PPC_RELOC_JBSR:
				// this is from -mlong-branch codegen.  We ignore the jump island
				if ( nextReloc->r_type() != PPC_RELOC_PAIR ) {
					printf("PPC_RELOC_JBSR missing following pair\n");
					break;
				}
				result = true;
				makeReference(A::kBranch24, srcAddr, nextReloc->r_address());
				break;
			default:
				printf("unknown relocation type %d\n", reloc->r_type());
		}
	}
	else {
		const macho_scattered_relocation_info<P>* sreloc = (macho_scattered_relocation_info<P>*)reloc;
		srcAddr = sect->addr() + sreloc->r_address();
		dstAddr = sreloc->r_value();
		uint32_t betterDstAddr;
		fixUpPtr = (uint32_t*)((char*)(fHeader) + sect->offset() + sreloc->r_address());
		const macho_scattered_relocation_info<P>* nextSReloc = &sreloc[1];
		const macho_relocation_info<P>* nextReloc = &reloc[1];
		// file format allows pair to be scattered or not
		bool nextRelocIsPair = false;
		uint32_t nextRelocAddress = 0;
		uint32_t nextRelocValue = 0;
		if ( (nextReloc->r_address() & R_SCATTERED) == 0 ) {
			if ( nextReloc->r_type() == PPC_RELOC_PAIR ) {
				nextRelocIsPair = true;
				nextRelocAddress = nextReloc->r_address();
				result = true;
			}
		}
		else {
			if ( nextSReloc->r_type() == PPC_RELOC_PAIR ) {
				nextRelocIsPair = true;
				nextRelocAddress = nextSReloc->r_address();
				nextRelocValue = nextSReloc->r_value();
				result = true;
			}
		}
		switch (sreloc->r_type()) {
			case PPC_RELOC_VANILLA:
				{
					betterDstAddr = P::getP(*(pint_t*)fixUpPtr);
					//fprintf(stderr, "scattered pointer reloc: srcAddr=0x%08X, dstAddr=0x%08X, pointer=0x%08X\n", srcAddr, dstAddr, betterDstAddr);
					// with a scattered relocation we get both the target (sreloc->r_value()) and the target+offset (*fixUpPtr)
					makeReferenceWithToBase(A::kPointer, srcAddr, betterDstAddr, dstAddr);
				}
				break;
			case PPC_RELOC_BR14:
				{
					instruction = BigEndian::get32(*fixUpPtr);
					displacement = (instruction & 0x0000FFFC);
					if ( (displacement & 0x00008000) != 0 )
						displacement |= 0xFFFF0000;
					betterDstAddr = srcAddr+displacement;
					//fprintf(stderr, "betterDstAddr=0x%08X, srcAddr=0x%08X, displacement=0x%08X\n",  betterDstAddr, srcAddr, displacement);
					makeReferenceWithToBase(A::kBranch14, srcAddr, betterDstAddr, dstAddr);
				}
				break;
			case PPC_RELOC_BR24:
				{
					instruction = BigEndian::get32(*fixUpPtr);
					if ( (instruction & 0x4C000000) == 0x48000000 ) {
						displacement = (instruction & 0x03FFFFFC);
						if ( (displacement & 0x02000000) != 0 )
							displacement |= 0xFC000000;
						betterDstAddr = srcAddr+displacement;
						makeReferenceWithToBase(A::kBranch24, srcAddr, betterDstAddr, dstAddr);
					}
				}
				break;
			case PPC_RELOC_LO16_SECTDIFF:
				{
					if ( ! nextRelocIsPair ) {
						printf("PPC_RELOC_LO16_SECTDIFF missing following PAIR\n");
						break;
					}
					instruction = BigEndian::get32(*fixUpPtr);
					lowBits = (instruction & 0xFFFF);
					displacement = (nextRelocAddress << 16) | ((uint32_t)lowBits & 0x0000FFFF);
					makeReferenceWithToBase(A::kPICBaseLow16, srcAddr, nextRelocValue, nextRelocValue + displacement, dstAddr);
				}
				break;
			case PPC_RELOC_LO14_SECTDIFF:
				{
					if ( ! nextRelocIsPair ) {
						printf("PPC_RELOC_LO14_SECTDIFF missing following PAIR\n");
						break;
					}
					instruction = BigEndian::get32(*fixUpPtr);
					lowBits = (instruction & 0xFFFC);
					displacement = (nextRelocAddress << 16) | ((uint32_t)lowBits & 0x0000FFFF);
					Reference<A>* ref = makeReferenceWithToBase(A::kPICBaseLow14, srcAddr, nextRelocValue, nextRelocValue + displacement, dstAddr);
					BaseAtom* target = ((BaseAtom*)&(ref->getTarget()));
					if ( target != NULL ) // can be NULL if target is turned into by-name reference
						target->alignAtLeast(3);
				}
				break;
			case PPC_RELOC_HA16_SECTDIFF:
				{
					if ( ! nextRelocIsPair ) {
						printf("PPC_RELOC_HA16_SECTDIFF missing following PAIR\n");
						break;
					}
					instruction = BigEndian::get32(*fixUpPtr);
					lowBits = (nextRelocAddress & 0x0000FFFF);
					displacement = ((instruction & 0x0000FFFF) << 16) + (int32_t)lowBits;
					makeReferenceWithToBase(A::kPICBaseHigh16, srcAddr, nextRelocValue, nextRelocValue + displacement, dstAddr);
				}
				break;
			case PPC_RELOC_LO14:
				{
					if ( ! nextRelocIsPair ) {
						printf("PPC_RELOC_LO14 missing following PAIR\n");
						break;
					}
					instruction = BigEndian::get32(*fixUpPtr);
					lowBits = (instruction & 0xFFFC);
					betterDstAddr = (nextRelocAddress << 16) + ((uint32_t)lowBits & 0x0000FFFF);
					makeReferenceWithToBase(A::kAbsLow14, srcAddr, betterDstAddr, dstAddr);
				}
				break;
			case PPC_RELOC_LO16:
				{
					if ( ! nextRelocIsPair ) {
						printf("PPC_RELOC_LO16 missing following PAIR\n");
						break;
					}
					instruction = BigEndian::get32(*fixUpPtr);
					lowBits = (instruction & 0xFFFF);
					betterDstAddr = (nextRelocAddress << 16) + ((uint32_t)lowBits & 0x0000FFFF);
					makeReferenceWithToBase(A::kAbsLow16, srcAddr, betterDstAddr, dstAddr);
				}
				break;
			case PPC_RELOC_HA16:
				{
					if ( ! nextRelocIsPair ) {
						printf("PPC_RELOC_HA16 missing following PAIR\n");
						break;
					}
					instruction = BigEndian::get32(*fixUpPtr);
					lowBits = (nextRelocAddress & 0xFFFF);
					betterDstAddr = ((instruction & 0xFFFF) << 16) + (int32_t)lowBits;
					makeReferenceWithToBase(A::kAbsHigh16AddLow, srcAddr, betterDstAddr, dstAddr);
				}
				break;
			case PPC_RELOC_SECTDIFF:
			case PPC_RELOC_LOCAL_SECTDIFF:
				{
					if ( ! nextRelocIsPair ) {
						printf("PPC_RELOC_SECTDIFF missing following pair\n");
						break;
					}
					makeReference(pointerDiffKindForLength_powerpc(sreloc->r_length()), srcAddr, nextRelocValue, dstAddr);
				}
				break;
			case PPC_RELOC_PAIR:
				break;
			case PPC_RELOC_HI16_SECTDIFF:
				printf("unexpected scattered relocation type PPC_RELOC_HI16_SECTDIFF\n");
				break;
			default:
				printf("unknown scattered relocation type %d\n", sreloc->r_type());
		}
	}
	return result;
}

template <>
ppc::ReferenceKinds Reader<ppc>::pointerDiffKindForLength_powerpc(uint8_t r_length)
{
	if ( r_length == 2 )
		return ppc::kPointerDiff32;
	else
		throw "bad diff relocations r_length for ppc architecture";
 }

template <>
ppc64::ReferenceKinds Reader<ppc64>::pointerDiffKindForLength_powerpc(uint8_t r_length)
{
	if ( r_length == 2 )
		return ppc64::kPointerDiff32;
	else if ( r_length == 3 )
		return ppc64::kPointerDiff64;
	else
		throw "bad diff relocations r_length for ppc64 architecture";
 }

template <>
bool Reader<x86>::addRelocReference(const macho_section<x86::P>* sect, const macho_relocation_info<x86::P>* reloc)
{
	uint32_t srcAddr;
	uint32_t dstAddr;
	uint32_t* fixUpPtr;
	bool result = false;
	if ( (reloc->r_address() & R_SCATTERED) == 0 ) {
		srcAddr = sect->addr() + reloc->r_address();
		fixUpPtr = (uint32_t*)((char*)(fHeader) + sect->offset() + reloc->r_address());
		switch ( reloc->r_type() ) {
			case GENERIC_RELOC_VANILLA:
				{
					if ( reloc->r_length() != 2 )
						throw "bad vanilla relocation length";
					x86::ReferenceKinds kind;
					uint32_t pointerValue = E::get32(*fixUpPtr);
					if ( reloc->r_pcrel() ) {
						kind = x86::kPCRel32;
						pointerValue += srcAddr + sizeof(uint32_t);
					}
					else if ( strcmp(sect->segname(), "__TEXT") == 0 ) {
						kind = x86::kAbsolute32;
					}
					else {
						kind = x86::kPointer;
					}
					if ( reloc->r_extern() ) {
						const macho_nlist<P>* targetSymbol = &fSymbols[reloc->r_symbolnum()];
						if ( this->isWeakImportSymbol(targetSymbol) )
							kind = x86::kPointerWeakImport;
						const char* targetName = &fStrings[targetSymbol->n_strx()];
						makeByNameReference(kind, srcAddr, targetName, pointerValue);
					}
					else {
						// if this is a branch to a stub, we need to see if the stub is for a weak imported symbol
						ObjectFile::Atom* atom = findAtomAndOffset(pointerValue).atom;
						if ( reloc->r_pcrel() && (atom->getSymbolTableInclusion() == ObjectFile::Atom::kSymbolTableNotIn)
							&& ((AnonymousAtom<x86>*)atom)->isWeakImportStub() )
							makeReference(x86::kPCRel32WeakImport, srcAddr, pointerValue);
						else
							makeReference(kind, srcAddr, pointerValue);
					}
				}
				break;
			default:
				printf("unknown relocation type %d\n", reloc->r_type());
		}
	}
	else {
		const macho_scattered_relocation_info<P>* sreloc = (macho_scattered_relocation_info<P>*)reloc;
		srcAddr = sect->addr() + sreloc->r_address();
		dstAddr = sreloc->r_value();
		fixUpPtr = (uint32_t*)((char*)(fHeader) + sect->offset() + sreloc->r_address());
		const macho_scattered_relocation_info<P>* nextSReloc = &sreloc[1];
		const macho_relocation_info<P>* nextReloc = &reloc[1];
		pint_t betterDstAddr;
		// file format allows pair to be scattered or not
		bool nextRelocIsPair = false;
		uint32_t nextRelocAddress = 0;
		uint32_t nextRelocValue = 0;
		if ( (nextReloc->r_address() & R_SCATTERED) == 0 ) {
			if ( nextReloc->r_type() == PPC_RELOC_PAIR ) {
				nextRelocIsPair = true;
				nextRelocAddress = nextReloc->r_address();
				result = true;
			}
		}
		else {
			if ( nextSReloc->r_type() == PPC_RELOC_PAIR ) {
				nextRelocIsPair = true;
				nextRelocAddress = nextSReloc->r_address();
				nextRelocValue = nextSReloc->r_value();
			}
		}
		switch (sreloc->r_type()) {
			case GENERIC_RELOC_VANILLA:
					betterDstAddr = LittleEndian::get32(*fixUpPtr);
					//fprintf(stderr, "pointer reloc: srcAddr=0x%08X, dstAddr=0x%08X, pointer=0x%08lX\n", srcAddr, dstAddr, betterDstAddr);
					// with a scattered relocation we get both the target (sreloc->r_value()) and the target+offset (*fixUpPtr)
					if ( sreloc->r_pcrel() ) {
						betterDstAddr += srcAddr + 4;
						makeReferenceWithToBase(x86::kPCRel32, srcAddr, betterDstAddr, dstAddr);
					}
					else {
						if ( strcmp(sect->segname(), "__TEXT") == 0 )
							makeReferenceWithToBase(x86::kAbsolute32, srcAddr, betterDstAddr, dstAddr);
						else
							makeReferenceWithToBase(x86::kPointer, srcAddr, betterDstAddr, dstAddr);
					}
				break;
			case GENERIC_RELOC_SECTDIFF:
			case GENERIC_RELOC_LOCAL_SECTDIFF:
				{
					if ( !nextRelocIsPair ) {
						printf("GENERIC_RELOC_SECTDIFF missing following pair\n");
						break;
					}
					if ( sreloc->r_length() != 2 )
						throw "bad length for GENERIC_RELOC_SECTDIFF";
					betterDstAddr = LittleEndian::get32(*fixUpPtr);
					makeReferenceWithToBase(x86::kPointerDiff, srcAddr, nextRelocValue, betterDstAddr+nextRelocValue, dstAddr);
				}
				break;
			case GENERIC_RELOC_PAIR:
				// do nothing, already used via a look ahead
				break;
			default:
				printf("unknown scattered relocation type %d\n", sreloc->r_type());
		}
	}
	return result;
}

template <>
bool Reader<x86_64>::addRelocReference(const macho_section<x86_64::P>* sect, const macho_relocation_info<x86_64::P>* reloc)
{
	uint64_t srcAddr;
	uint64_t dstAddr = 0;
	uint64_t addend;
	uint32_t* fixUpPtr;
	x86_64::ReferenceKinds kind;
	bool result = false;
	const macho_nlist<P>* targetSymbol = NULL;
	const char* targetName = NULL;
	srcAddr = sect->addr() + reloc->r_address();
	fixUpPtr = (uint32_t*)((char*)(fHeader) + sect->offset() + reloc->r_address());
	//fprintf(stderr, "addReloc type=%d\n", reloc->r_type());
	if ( reloc->r_extern() ) {
		targetSymbol = &fSymbols[reloc->r_symbolnum()];
		targetName = &fStrings[targetSymbol->n_strx()];
	}
	switch ( reloc->r_type() ) {
		case X86_64_RELOC_UNSIGNED:
			if ( reloc->r_pcrel() )
				throw "pcrel and X86_64_RELOC_UNSIGNED not supported";
			if ( reloc->r_length() != 3 ) 
				throw "length < 3 and X86_64_RELOC_UNSIGNED not supported";
			dstAddr = E::get64(*((uint64_t*)fixUpPtr));
			if ( reloc->r_extern() ) 
				makeReferenceToSymbol(x86_64::kPointer, srcAddr, targetSymbol, dstAddr);
			else
				makeReference(x86_64::kPointer, srcAddr, dstAddr);
			break;
		case X86_64_RELOC_SIGNED:
		case X86_64_RELOC_SIGNED_1:
		case X86_64_RELOC_SIGNED_2:
		case X86_64_RELOC_SIGNED_4:
			if ( ! reloc->r_pcrel() )
				throw "not pcrel and X86_64_RELOC_SIGNED not supported";
			if ( reloc->r_length() != 2 ) 
				throw "length != 2 and X86_64_RELOC_SIGNED not supported";
			kind = x86_64::kPCRel32;
			dstAddr = (int64_t)((int32_t)(E::get32(*fixUpPtr)));
			switch ( reloc->r_type() ) {
				case X86_64_RELOC_SIGNED:
					if ( reloc->r_extern() ) {
						// Support older relocations
						if ( dstAddr == (uint64_t)(-1) ) {
							kind = x86_64::kPCRel32_1;
							dstAddr = 0;
						}
						else if ( dstAddr == (uint64_t)(-2) ) {
							kind = x86_64::kPCRel32_2;
							dstAddr = 0;
						}
						else if ( dstAddr == (uint64_t)(-4) ) {
							kind = x86_64::kPCRel32_4;
							dstAddr = 0;
						}
					}
					break;
				case X86_64_RELOC_SIGNED_1:
					if ( reloc->r_extern() ) {
						dstAddr = 0;
					} else {
						dstAddr += 1;
					}
					kind = x86_64::kPCRel32_1;
					break;
				case X86_64_RELOC_SIGNED_2:
					if ( reloc->r_extern() ) {
						dstAddr = 0;
					} else {
						dstAddr += 2;
					}
					kind = x86_64::kPCRel32_2;
					break;
				case X86_64_RELOC_SIGNED_4:
					if ( reloc->r_extern() ) {
						dstAddr = 0;
					} else {
						dstAddr += 4;
					}
					kind = x86_64::kPCRel32_4;
					break;
				default:
					break;
			}
			if ( reloc->r_extern() ) 
				makeReferenceToSymbol(kind, srcAddr, targetSymbol, dstAddr);
			else {
				makeReference(kind, srcAddr, srcAddr+4+dstAddr);
			}
			break;
		case X86_64_RELOC_BRANCH:
			if ( ! reloc->r_pcrel() )
				throw "not pcrel and X86_64_RELOC_BRANCH not supported";
			if ( reloc->r_length() != 2 ) 
				throw "length != 2 and X86_64_RELOC_BRANCH not supported";
			dstAddr = (int64_t)((int32_t)(E::get32(*fixUpPtr)));
			if ( reloc->r_extern() ) {
				if ( isWeakImportSymbol(targetSymbol) )
					makeReferenceToSymbol(x86_64::kBranchPCRel32WeakImport, srcAddr, targetSymbol, dstAddr);
				else
					makeReferenceToSymbol(x86_64::kBranchPCRel32, srcAddr, targetSymbol, dstAddr);
			}
			else {
				makeReference(x86_64::kBranchPCRel32, srcAddr, srcAddr+4+dstAddr);
			}
			break;
		case X86_64_RELOC_GOT:
			if ( ! reloc->r_extern() ) 
				throw "not extern and X86_64_RELOC_GOT not supported";
			if ( ! reloc->r_pcrel() )
				throw "not pcrel and X86_64_RELOC_GOT not supported";
			if ( reloc->r_length() != 2 ) 
				throw "length != 2 and X86_64_RELOC_GOT not supported";
			addend = (int64_t)((int32_t)(E::get32(*fixUpPtr)));
			if ( isWeakImportSymbol(targetSymbol) )
				makeReferenceToSymbol(x86_64::kPCRel32GOTWeakImport, srcAddr, targetSymbol, addend);
			else
				makeReferenceToSymbol(x86_64::kPCRel32GOT, srcAddr, targetSymbol, addend);
			break;
		case X86_64_RELOC_GOT_LOAD:
			if ( ! reloc->r_extern() ) 
				throw "not extern and X86_64_RELOC_GOT_LOAD not supported";
			if ( ! reloc->r_pcrel() )
				throw "not pcrel and X86_64_RELOC_GOT_LOAD not supported";
			if ( reloc->r_length() != 2 ) 
				throw "length != 2 and X86_64_RELOC_GOT_LOAD not supported";
			addend = (int64_t)((int32_t)(E::get32(*fixUpPtr)));
			if ( isWeakImportSymbol(targetSymbol) )
				makeReferenceToSymbol(x86_64::kPCRel32GOTLoadWeakImport, srcAddr, targetSymbol, addend);
			else
				makeReferenceToSymbol(x86_64::kPCRel32GOTLoad, srcAddr, targetSymbol, addend);
			break;
		case X86_64_RELOC_SUBTRACTOR:
			if ( reloc->r_pcrel() )
				throw "X86_64_RELOC_SUBTRACTOR cannot be pc-relative";
			if ( reloc->r_length() < 2 )
				throw "X86_64_RELOC_SUBTRACTOR must have r_length of 2 or 3";
			if ( !reloc->r_extern() )
				throw "X86_64_RELOC_SUBTRACTOR must have r_extern=1";
			const macho_relocation_info<x86_64::P>* nextReloc = &reloc[1];
			if ( nextReloc->r_type() != X86_64_RELOC_UNSIGNED )
				throw "X86_64_RELOC_SUBTRACTOR must be followed by X86_64_RELOC_UNSIGNED";
			result = true;
			if ( nextReloc->r_pcrel() )
				throw "X86_64_RELOC_UNSIGNED following a X86_64_RELOC_SUBTRACTOR cannot be pc-relative";
			if ( nextReloc->r_length() != reloc->r_length() )
				throw "X86_64_RELOC_UNSIGNED following a X86_64_RELOC_SUBTRACTOR must have same r_length";
			Reference<x86_64>* ref;
			bool negativeAddend;
			if ( reloc->r_length() == 2 ) {
				kind = x86_64::kPointerDiff32;
				dstAddr = E::get32(*fixUpPtr); // addend is in content
				negativeAddend = ((dstAddr & 0x80000000) != 0);
			}
			else {
				kind = x86_64::kPointerDiff;
				dstAddr = E::get64(*((uint64_t*)fixUpPtr)); // addend is in content
				negativeAddend = ((dstAddr & 0x8000000000000000ULL) != 0);
			}
			ObjectFile::Atom* inAtom = this->findAtomAndOffset(srcAddr).atom;
			// create reference with "to" target
			if ( nextReloc->r_extern() ) {
				const macho_nlist<P>* targetSymbol = &fSymbols[nextReloc->r_symbolnum()];
				const char* targetName = &fStrings[targetSymbol->n_strx()];
				ref = makeReferenceToSymbol(kind, srcAddr, targetSymbol, 0);
				// if "to" is in this atom, change by-name to a direct reference
				if ( strcmp(targetName, inAtom->getName()) == 0 )
					ref->setTarget(*inAtom, 0);
			}
			else {
				ref = makeReference(kind, srcAddr, dstAddr);
			}
			// add in "from" target
			if ( reloc->r_extern() ) {
				const macho_nlist<P>* targetFromSymbol = &fSymbols[reloc->r_symbolnum()];
				const char* fromTargetName = &fStrings[targetFromSymbol->n_strx()];
				if ( (targetFromSymbol->n_type() & N_EXT) == 0 ) {
					// from target is translation unit scoped, so use a direct reference
					ref->setFromTarget(*(findAtomAndOffset(targetSymbol->n_value()).atom));
				}
				else if ( strcmp(fromTargetName, inAtom->getName()) == 0 ) {
					// if "from" is in this atom, change by-name to a direct reference
					ref->setFromTarget(*inAtom);
				}
				else {
					// some non-static other atom
					ref->setFromTargetName(fromTargetName);
				}
			}
			// addend goes in from side iff negative
			if ( negativeAddend )
				ref->setFromTargetOffset(-dstAddr);
			else
				ref->setToTargetOffset(dstAddr);
			break;
		default:
			fprintf(stderr, "unknown relocation type %d\n", reloc->r_type());
	}
	return result;
}


template <>
const char* Reference<x86>::getDescription() const
{
	static char temp[2048];
	switch( fKind ) {
		case x86::kNoFixUp:
			sprintf(temp, "reference to ");
			break;
		case x86::kFollowOn:
			sprintf(temp, "followed by ");
			break;
		case x86::kPointerWeakImport:
			sprintf(temp, "offset 0x%04X, weak import pointer to ", fFixUpOffsetInSrc);
			break;
		case x86::kPointer:
			sprintf(temp, "offset 0x%04X, pointer to ", fFixUpOffsetInSrc);
			break;
		case x86::kPointerDiff:
			{
			// by-name references have quoted names
			const char* targetQuotes = (&(this->getTarget()) == NULL) ? "\"" : "";
			const char* fromQuotes = (&(this->getFromTarget()) == NULL) ? "\"" : "";
			sprintf(temp, "offset 0x%04X, 32-bit pointer difference: (&%s%s%s + 0x%08X) - (&%s%s%s + 0x%08X)",
				fFixUpOffsetInSrc, targetQuotes, this->getTargetName(), targetQuotes, fToTarget.offset,
							   fromQuotes, this->getFromTargetName(), fromQuotes, fFromTarget.offset );
			return temp;
			}
			break;
		case x86::kPCRel32WeakImport:
			sprintf(temp, "offset 0x%04X, rel32 reference to weak imported ", fFixUpOffsetInSrc);
			break;
		case x86::kPCRel32:
			sprintf(temp, "offset 0x%04X, rel32 reference to ", fFixUpOffsetInSrc);
			break;
		case x86::kAbsolute32:
			sprintf(temp, "offset 0x%04X, absolute32 reference to ", fFixUpOffsetInSrc);
			break;
	}
	// always quote by-name references
	if ( fToTargetName != NULL ) {
		strcat(temp, "\"");
		strcat(temp, fToTargetName);
		strcat(temp, "\"");
	}
	else if ( fToTarget.atom != NULL ) {
		strcat(temp, fToTarget.atom->getDisplayName());
	}
	else {
		strcat(temp, "NULL target");
	}
	if ( fToTarget.offset != 0 )
		sprintf(&temp[strlen(temp)], " plus 0x%08X", fToTarget.offset);

	return temp;
}


template <>
const char* Reference<ppc>::getDescription() const
{
	static char temp[2048];
	switch( fKind ) {
		case ppc::kNoFixUp:
			sprintf(temp, "reference to ");
			break;
		case ppc::kFollowOn:
			sprintf(temp, "followed by ");
			break;
		case ppc::kPointerWeakImport:
			sprintf(temp, "offset 0x%04X, weak import pointer to ", fFixUpOffsetInSrc);
			break;
		case ppc::kPointer:
			sprintf(temp, "offset 0x%04X, pointer to ", fFixUpOffsetInSrc);
			break;
		case ppc::kPointerDiff32:
			{
			// by-name references have quoted names
			const char* targetQuotes = (&(this->getTarget()) == NULL) ? "\"" : "";
			const char* fromQuotes = (&(this->getFromTarget()) == NULL) ? "\"" : "";
			sprintf(temp, "offset 0x%04X, 32-bit pointer difference: (&%s%s%s + %d) - (&%s%s%s + %d)",
				fFixUpOffsetInSrc, targetQuotes, this->getTargetName(), targetQuotes, fToTarget.offset,
							   fromQuotes, this->getFromTargetName(), fromQuotes, fFromTarget.offset );
			return temp;
			}
		case ppc::kPointerDiff64:
			throw "unsupported refrence kind";
			break;
		case ppc::kBranch24WeakImport:
			sprintf(temp, "offset 0x%04X, pc-rel branch fixup to weak imported ", fFixUpOffsetInSrc);
			break;
		case ppc::kBranch24:
		case ppc::kBranch14:
			sprintf(temp, "offset 0x%04X, pc-rel branch fixup to ", fFixUpOffsetInSrc);
			break;
		case ppc::kPICBaseLow16:
			sprintf(temp, "offset 0x%04X, low  16 fixup from pic-base offset 0x%04X to ", fFixUpOffsetInSrc, fFromTarget.offset);
			break;
		case ppc::kPICBaseLow14:
			sprintf(temp, "offset 0x%04X, low  14 fixup from pic-base offset 0x%04X to ", fFixUpOffsetInSrc, fFromTarget.offset);
			break;
		case ppc::kPICBaseHigh16:
			sprintf(temp, "offset 0x%04X, high 16 fixup from pic-base offset 0x%04X to ", fFixUpOffsetInSrc, fFromTarget.offset);
			break;
		case ppc::kAbsLow16:
			sprintf(temp, "offset 0x%04X, low  16 fixup to absolute address of ", fFixUpOffsetInSrc);
			break;
		case ppc::kAbsLow14:
			sprintf(temp, "offset 0x%04X, low  14 fixup to absolute address of ", fFixUpOffsetInSrc);
			break;
		case ppc::kAbsHigh16:
			sprintf(temp, "offset 0x%04X, high 16 fixup to absolute address of ", fFixUpOffsetInSrc);
			break;
		case ppc::kAbsHigh16AddLow:
			sprintf(temp, "offset 0x%04X, high 16 fixup to absolute address of ", fFixUpOffsetInSrc);
			break;
	}
	// always quote by-name references
	if ( fToTargetName != NULL ) {
		strcat(temp, "\"");
		strcat(temp, fToTargetName);
		strcat(temp, "\"");
	}
	else if ( fToTarget.atom != NULL ) {
		strcat(temp, fToTarget.atom->getDisplayName());
	}
	else {
		strcat(temp, "NULL target");
	}
	if ( fToTarget.offset != 0 )
		sprintf(&temp[strlen(temp)], " plus 0x%08X", fToTarget.offset);

	return temp;
}

template <>
const char* Reference<ppc64>::getDescription() const
{
	static char temp[2048];
	switch( fKind ) {
		case ppc64::kNoFixUp:
			sprintf(temp, "reference to ");
			break;
		case ppc64::kFollowOn:
			sprintf(temp, "followed by ");
			break;
		case ppc64::kPointerWeakImport:
			sprintf(temp, "offset 0x%04llX, weak import pointer to ", fFixUpOffsetInSrc);
			break;
		case ppc64::kPointer:
			sprintf(temp, "offset 0x%04llX, pointer to ", fFixUpOffsetInSrc);
			break;
		case ppc64::kPointerDiff64:
			{
			// by-name references have quoted names
			const char* targetQuotes = (&(this->getTarget()) == NULL) ? "\"" : "";
			const char* fromQuotes = (&(this->getFromTarget()) == NULL) ? "\"" : "";
			sprintf(temp, "offset 0x%04llX, 64-bit pointer difference: (&%s%s%s + %u) - (&%s%s%s + %u)",
				fFixUpOffsetInSrc, targetQuotes, this->getTargetName(), targetQuotes, fToTarget.offset,
							   fromQuotes, this->getFromTargetName(), fromQuotes, fFromTarget.offset );
			return temp;
			}
		case ppc64::kPointerDiff32:
			{
			// by-name references have quoted names
			const char* targetQuotes = (&(this->getTarget()) == NULL) ? "\"" : "";
			const char* fromQuotes = (&(this->getFromTarget()) == NULL) ? "\"" : "";
			sprintf(temp, "offset 0x%04llX, 32-bit pointer difference: (&%s%s%s + %u) - (&%s%s%s + %u)",
				fFixUpOffsetInSrc, targetQuotes, this->getTargetName(), targetQuotes, fToTarget.offset,
							   fromQuotes, this->getFromTargetName(), fromQuotes, fFromTarget.offset );
			return temp;
			}
		case ppc64::kBranch24WeakImport:
			sprintf(temp, "offset 0x%04llX, pc-rel branch fixup to weak imported ", fFixUpOffsetInSrc);
			break;
		case ppc64::kBranch24:
		case ppc64::kBranch14:
			sprintf(temp, "offset 0x%04llX, pc-rel branch fixup to ", fFixUpOffsetInSrc);
			break;
		case ppc64::kPICBaseLow16:
			sprintf(temp, "offset 0x%04llX, low  16 fixup from pic-base offset 0x%04X to ", fFixUpOffsetInSrc, fFromTarget.offset);
			break;
		case ppc64::kPICBaseLow14:
			sprintf(temp, "offset 0x%04llX, low  14 fixup from pic-base offset 0x%04X to ", fFixUpOffsetInSrc, fFromTarget.offset);
			break;
		case ppc64::kPICBaseHigh16:
			sprintf(temp, "offset 0x%04llX, high 16 fixup from pic-base offset 0x%04X to ", fFixUpOffsetInSrc, fFromTarget.offset);
			break;
		case ppc64::kAbsLow16:
			sprintf(temp, "offset 0x%04llX, low  16 fixup to absolute address of ", fFixUpOffsetInSrc);
			break;
		case ppc64::kAbsLow14:
			sprintf(temp, "offset 0x%04llX, low  14 fixup to absolute address of ", fFixUpOffsetInSrc);
			break;
		case ppc64::kAbsHigh16:
			sprintf(temp, "offset 0x%04llX, high 16 fixup to absolute address of ", fFixUpOffsetInSrc);
			break;
		case ppc64::kAbsHigh16AddLow:
			sprintf(temp, "offset 0x%04llX, high 16 fixup to absolute address of ", fFixUpOffsetInSrc);
			break;
	}
	// always quote by-name references
	if ( fToTargetName != NULL ) {
		strcat(temp, "\"");
		strcat(temp, fToTargetName);
		strcat(temp, "\"");
	}
	else if ( fToTarget.atom != NULL ) {
		strcat(temp, fToTarget.atom->getDisplayName());
	}
	else {
		strcat(temp, "NULL target");
	}
	if ( fToTarget.offset != 0 )
		sprintf(&temp[strlen(temp)], " plus 0x%llX", this->getTargetOffset());

	return temp;
}


template <>
const char* Reference<x86_64>::getDescription() const
{
	static char temp[2048];
	switch( fKind ) {
		case x86_64::kNoFixUp:
			sprintf(temp, "reference to ");
			break;
		case x86_64::kFollowOn:
			sprintf(temp, "followed by ");
			break;
		case x86_64::kPointerWeakImport:
			sprintf(temp, "offset 0x%04llX, weak import pointer to ", fFixUpOffsetInSrc);
			break;
		case x86_64::kPointer:
			sprintf(temp, "offset 0x%04llX, pointer to ", fFixUpOffsetInSrc);
			break;
		case x86_64::kPointerDiff32:
		case x86_64::kPointerDiff:
			{
			// by-name references have quoted names
			const char* targetQuotes = (&(this->getTarget()) == NULL) ? "\"" : "";
			const char* fromQuotes = (&(this->getFromTarget()) == NULL) ? "\"" : "";
			const char* size = (fKind == x86_64::kPointerDiff32) ? "32-bit" : "64-bit";
			sprintf(temp, "offset 0x%04llX, %s pointer difference: (&%s%s%s + 0x%08X) - (&%s%s%s + 0x%08X)",
				fFixUpOffsetInSrc, size, targetQuotes, this->getTargetName(), targetQuotes, fToTarget.offset,
							   fromQuotes, this->getFromTargetName(), fromQuotes, fFromTarget.offset );
			return temp;
			}
			break;
		case x86_64::kPCRel32:
			sprintf(temp, "offset 0x%04llX, rel32 reference to ", fFixUpOffsetInSrc);
			break;
		case x86_64::kPCRel32_1:
			sprintf(temp, "offset 0x%04llX, rel32-1 reference to ", fFixUpOffsetInSrc);
			break;
		case x86_64::kPCRel32_2:
			sprintf(temp, "offset 0x%04llX, rel32-2 reference to ", fFixUpOffsetInSrc);
			break;
		case x86_64::kPCRel32_4:
			sprintf(temp, "offset 0x%04llX, rel32-4 reference to ", fFixUpOffsetInSrc);
			break;
		case x86_64::kBranchPCRel32:
			sprintf(temp, "offset 0x%04llX, branch rel32 reference to ", fFixUpOffsetInSrc);
			break;
		case x86_64::kBranchPCRel32WeakImport:
			sprintf(temp, "offset 0x%04llX, branch rel32 reference to weak imported ", fFixUpOffsetInSrc);
			break;
		case x86_64::kPCRel32GOT:
			sprintf(temp, "offset 0x%04llX, rel32 reference to GOT entry for ", fFixUpOffsetInSrc);
			break;
		case x86_64::kPCRel32GOTWeakImport:
			sprintf(temp, "offset 0x%04llX, rel32 reference to GOT entry for weak imported ", fFixUpOffsetInSrc);
			break;
		case x86_64::kPCRel32GOTLoad:
			sprintf(temp, "offset 0x%04llX, rel32 reference to GOT entry for ", fFixUpOffsetInSrc);
			break;
		case x86_64::kPCRel32GOTLoadWeakImport:
			sprintf(temp, "offset 0x%04llX, rel32 reference to GOT entry for weak imported ", fFixUpOffsetInSrc);
			break;
	}
	// always quote by-name references
	if ( fToTargetName != NULL ) {
		strcat(temp, "\"");
		strcat(temp, fToTargetName);
		strcat(temp, "\"");
	}
	else if ( fToTarget.atom != NULL ) {
		strcat(temp, fToTarget.atom->getDisplayName());
	}
	else {
		strcat(temp, "NULL target");
	}
	if ( fToTarget.offset != 0 )
		sprintf(&temp[strlen(temp)], " plus 0x%llX", this->getTargetOffset());

	return temp;
}



}; // namespace relocatable
}; // namespace mach_o

#endif // __OBJECT_FILE_MACH_O__

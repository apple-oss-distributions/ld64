/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-*
 *
 * Copyright (c) 2009-2010 Apple Inc. All rights reserved.
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
#include <assert.h>

#include <string>
#include <map>
#include <set>
#include <vector>
#include <algorithm>
#include <ext/hash_map>
#include <ext/hash_set>

#include "Options.h"

#include "ld.hpp"
#include "InputFiles.h"
#include "SymbolTable.h"



namespace ld {
namespace tool {


// HACK, I can't find a way to pass values in the compare classes (e.g. ContentFuncs)
// so use global variable to pass info.
static ld::IndirectBindingTable*	_s_indirectBindingTable = NULL;
bool										SymbolTable::_s_doDemangle = false;


SymbolTable::SymbolTable(const Options& opts, std::vector<const ld::Atom*>& ibt) 
	: _options(opts), _cstringTable(6151), _indirectBindingTable(ibt), _hasExternalTentativeDefinitions(false)  
{  
	_s_indirectBindingTable = this;
	_s_doDemangle = _options.demangleSymbols();
}


size_t SymbolTable::ContentFuncs::operator()(const ld::Atom* atom) const
{
	return atom->contentHash(*_s_indirectBindingTable);
}

bool SymbolTable::ContentFuncs::operator()(const ld::Atom* left, const ld::Atom* right) const
{
	return (memcmp(left->rawContentPointer(), right->rawContentPointer(), left->size()) == 0);
}



size_t SymbolTable::CStringHashFuncs::operator()(const ld::Atom* atom) const
{
	return atom->contentHash(*_s_indirectBindingTable);
}

bool SymbolTable::CStringHashFuncs::operator()(const ld::Atom* left, const ld::Atom* right) const
{
	return (strcmp((char*)left->rawContentPointer(), (char*)right->rawContentPointer()) == 0);
}


size_t SymbolTable::UTF16StringHashFuncs::operator()(const ld::Atom* atom) const
{
	return atom->contentHash(*_s_indirectBindingTable);
}

bool SymbolTable::UTF16StringHashFuncs::operator()(const ld::Atom* left, const ld::Atom* right) const
{
	if ( left == right )
		return true;
	const void* leftContent = left->rawContentPointer();
	const void* rightContent = right->rawContentPointer();
	unsigned int amount = left->size()-2;
	bool result = (memcmp(leftContent, rightContent, amount) == 0);
	return result;
}


size_t SymbolTable::ReferencesHashFuncs::operator()(const ld::Atom* atom) const
{
	return atom->contentHash(*_s_indirectBindingTable);
}

bool SymbolTable::ReferencesHashFuncs::operator()(const ld::Atom* left, const ld::Atom* right) const
{
	return left->canCoalesceWith(*right, *_s_indirectBindingTable);
}



bool SymbolTable::addByName(const ld::Atom& newAtom, bool ignoreDuplicates)
{
	bool useNew = true;
	bool checkVisibilityMismatch = false;
	assert(newAtom.name() != NULL);
	const char* name = newAtom.name();
	IndirectBindingSlot slot = this->findSlotForName(name);
	const ld::Atom* existingAtom = _indirectBindingTable[slot];
	//fprintf(stderr, "addByName(%p) name=%s, slot=%u, existing=%p\n", &newAtom, newAtom.name(), slot, existingAtom);
	if ( existingAtom != NULL ) {
		assert(&newAtom != existingAtom);
		switch ( existingAtom->definition() ) {
			case ld::Atom::definitionRegular:
				switch ( newAtom.definition() ) {
					case ld::Atom::definitionRegular:
						if ( existingAtom->combine() == ld::Atom::combineByName ) {
							if ( newAtom.combine() == ld::Atom::combineByName ) {
								// <rdar://problem/9183821> always choose mach-o over llvm bit code, otherwise LTO may eliminate the llvm atom
								const bool existingIsLTO = (existingAtom->contentType() == ld::Atom::typeLTOtemporary);
								const bool newIsLTO = (newAtom.contentType() == ld::Atom::typeLTOtemporary);
								if ( existingIsLTO != newIsLTO ) {
									useNew = existingIsLTO;
								}
								else {
									// both weak, prefer non-auto-hide one
									if ( newAtom.autoHide() != existingAtom->autoHide() ) {
										// <rdar://problem/6783167> support auto hidden weak symbols: .weak_def_can_be_hidden
										useNew = existingAtom->autoHide();
										// don't check for visibility mismatch
									}
									else if ( newAtom.autoHide() && existingAtom->autoHide() ) {
										// both have auto-hide, so use one with greater alignment
										useNew = ( newAtom.alignment().trailingZeros() > existingAtom->alignment().trailingZeros() );
									}
									else {
										// neither auto-hide, check visibility
										if ( newAtom.scope() != existingAtom->scope() ) {
											// <rdar://problem/8304984> use more visible weak def symbol
											useNew = (newAtom.scope() == ld::Atom::scopeGlobal);
										}
										else {
											// both have same visibility, use one with greater alignment
											useNew = ( newAtom.alignment().trailingZeros() > existingAtom->alignment().trailingZeros() );
										}
									}
								}
							}
							else {
								// existing weak, new is not-weak
								useNew = true;
							}
						}
						else {
							if ( newAtom.combine() == ld::Atom::combineByName ) {
								// existing not-weak, new is weak
								useNew = false;
							}
							else {
								// existing not-weak, new is not-weak
								if ( newAtom.section().type() == ld::Section::typeMachHeader ) {
									warning("ignoring override of built-in symbol %s from %s", newAtom.name(), existingAtom->file()->path());
									useNew = true;
								} 
								else if ( existingAtom->section().type() == ld::Section::typeMachHeader ) {
									warning("ignoring override of built-in symbol %s from %s", newAtom.name(), newAtom.file()->path());
									useNew = false;
								} 
								else {
									if ( ignoreDuplicates ) {
										useNew = false;
										static bool fullWarning = false;
										if ( ! fullWarning ) {
											warning("-dead_strip with lazy loaded static (library) archives "
													"has resulted in a duplicate symbol.  You can change your "
													"source code to rename symbols to avoid the collision.  "
													"This will be an error in a future linker.");
											fullWarning = true;
										}
										warning("duplicate symbol %s originally in %s now lazily loaded from %s",
												SymbolTable::demangle(name), existingAtom->file()->path(), newAtom.file()->path());
									}
									else {
										throwf("duplicate symbol %s in %s and %s", 
												SymbolTable::demangle(name), newAtom.file()->path(), existingAtom->file()->path());
									}
								}
							}
						}
						break;
					case ld::Atom::definitionTentative:
						// ignore new tentative atom, because we already have a regular one
						useNew = false;
						checkVisibilityMismatch = true;
						if ( newAtom.size() > existingAtom->size() ) {
							warning("for symbol %s tentative definition of size %llu from %s is "
											"is smaller than the real definition of size %llu from %s",
											newAtom.name(), newAtom.size(), newAtom.file()->path(),
											existingAtom->size(), existingAtom->file()->path());
						}
						break;
					case ld::Atom::definitionAbsolute:
						throwf("duplicate symbol %s in %s and %s", name, newAtom.file()->path(), existingAtom->file()->path());
					case ld::Atom::definitionProxy:
						// ignore external atom, because we already have a one
						useNew = false;
						break;
				}
				break;
			case ld::Atom::definitionTentative:
				switch ( newAtom.definition() ) {
					case ld::Atom::definitionRegular:
						// replace existing tentative atom with regular one
						if ( newAtom.section().type() == ld::Section::typeMachHeader ) {
							// silently replace tentative __dso_handle with real linker created symbol
							useNew = true;
						}
						else if ( existingAtom->section().type() == ld::Section::typeMachHeader ) {
							// silently replace tentative __dso_handle with real linker created symbol
							useNew = false;
						}
						else {
							checkVisibilityMismatch = true;
							if ( newAtom.size() < existingAtom->size() ) {
								warning("for symbol %s tentative definition of size %llu from %s is "
												"being replaced by a real definition of size %llu from %s",
												newAtom.name(), existingAtom->size(), existingAtom->file()->path(),
												newAtom.size(), newAtom.file()->path());
							}
							if ( newAtom.section().type() == ld::Section::typeCode ) {
								warning("for symbol %s tentative (data) defintion from %s is "
										"being replaced by code from %s", newAtom.name(), existingAtom->file()->path(),
										newAtom.file()->path());
							}
						}
						break;
					case ld::Atom::definitionTentative:
						// new and existing are both tentative definitions, use largest
						checkVisibilityMismatch = true;
						if ( newAtom.size() < existingAtom->size() ) {
							useNew = false;
						} 
						else {
							if ( newAtom.alignment().trailingZeros() < existingAtom->alignment().trailingZeros() )
								warning("alignment lost in merging tentative definition %s", newAtom.name());
						}
						break;
					case ld::Atom::definitionAbsolute:
						// replace tentative with absolute
						useNew = true;
						break;
					case ld::Atom::definitionProxy:
						// a tentative definition and a dylib definition, so commons-mode decides how to handle
						switch ( _options.commonsMode() ) {
							case Options::kCommonsIgnoreDylibs:
								if ( _options.warnCommons() )
									warning("using common symbol %s from %s and ignoring defintion from dylib %s",
											existingAtom->name(), existingAtom->file()->path(), newAtom.file()->path());
								useNew = false;
								break;
							case Options::kCommonsOverriddenByDylibs:
								if ( _options.warnCommons() )
									warning("replacing common symbol %s from %s with true definition from dylib %s",
											existingAtom->name(), existingAtom->file()->path(), newAtom.file()->path());
								break;
							case Options::kCommonsConflictsDylibsError:
								throwf("common symbol %s from %s conflicts with defintion from dylib %s",
										existingAtom->name(), existingAtom->file()->path(), newAtom.file()->path());
						}
						break;
				}
				break;
			case ld::Atom::definitionAbsolute:
				switch ( newAtom.definition() ) {
					case ld::Atom::definitionRegular:
						throwf("duplicate symbol %s in %s and %s", name, newAtom.file()->path(), existingAtom->file()->path());
					case ld::Atom::definitionTentative:
						// ignore new tentative atom, because we already have a regular one
						useNew = false;
						break;
					case ld::Atom::definitionAbsolute:
						throwf("duplicate symbol %s in %s and %s", name, newAtom.file()->path(), existingAtom->file()->path());
					case ld::Atom::definitionProxy:
						// ignore external atom, because we already have a one
						useNew = false;
						break;
				}
				break;
			case ld::Atom::definitionProxy:
				switch ( newAtom.definition() ) {
					case ld::Atom::definitionRegular:
						// replace external atom with regular one
						useNew = true;
						break;
					case ld::Atom::definitionTentative:
						// a tentative definition and a dylib definition, so commons-mode decides how to handle
						switch ( _options.commonsMode() ) {
							case Options::kCommonsIgnoreDylibs:
								if ( _options.warnCommons() )
									warning("using common symbol %s from %s and ignoring defintion from dylib %s",
											newAtom.name(), newAtom.file()->path(), existingAtom->file()->path());
								break;
							case Options::kCommonsOverriddenByDylibs:
								if ( _options.warnCommons() )
									warning("replacing defintion of %s from dylib %s with common symbol from %s",
											newAtom.name(), existingAtom->file()->path(), newAtom.file()->path());
								useNew = false;
								break;
							case Options::kCommonsConflictsDylibsError:
								throwf("common symbol %s from %s conflicts with defintion from dylib %s",
											newAtom.name(), newAtom.file()->path(), existingAtom->file()->path());
						}
						break;
					case ld::Atom::definitionAbsolute:
						// replace external atom with absolute one
						useNew = true;
						break;
					case ld::Atom::definitionProxy:
						// <rdar://problem/5137732> ld should keep looking when it finds a weak definition in a dylib
						if ( newAtom.combine() == ld::Atom::combineByName ) {
							useNew = false;
						}
						else {
							if ( existingAtom->combine() == ld::Atom::combineByName )
								useNew = true;
							else
								throwf("symbol %s exported from both %s and %s\n", name, newAtom.file()->path(), existingAtom->file()->path());
						}
						break;
				}
				break;
		}	
	}
	if ( (existingAtom != NULL) && checkVisibilityMismatch && (newAtom.scope() != existingAtom->scope()) ) {
		warning("%s has different visibility (%s) in %s and (%s) in %s", 
			SymbolTable::demangle(newAtom.name()), (newAtom.scope() == 1 ? "hidden" : "default"), newAtom.file()->path(), (existingAtom->scope()  == 1 ? "hidden" : "default"), existingAtom->file()->path());
	}
	if ( useNew ) {
		_indirectBindingTable[slot] = &newAtom;
		if ( existingAtom != NULL ) {
			markCoalescedAway(existingAtom);
//			if ( fOwner.fInitialLoadsDone ) {
//				//fprintf(stderr, "existing %p %s overridden by %p\n", existingAtom, existingAtom->name(), &newAtom);
//				fOwner.fAtomsOverriddenByLateLoads.insert(existingAtom);
//			}
		}
		if ( newAtom.scope() == ld::Atom::scopeGlobal ) {
			if ( newAtom.definition() == ld::Atom::definitionTentative ) {
				_hasExternalTentativeDefinitions = true;
			}
		}
	}
	else {
		markCoalescedAway(&newAtom);
	}
	// return if existing atom in symbol table was replaced
	return useNew && (existingAtom != NULL);
}


bool SymbolTable::addByContent(const ld::Atom& newAtom)
{
	bool useNew = true;
	const ld::Atom* existingAtom;
	IndirectBindingSlot slot = this->findSlotForContent(&newAtom, &existingAtom);
	//fprintf(stderr, "addByContent(%p) name=%s, slot=%u, existing=%p\n", &newAtom, newAtom.name(), slot, existingAtom);
	if ( existingAtom != NULL ) {
		// use existing unless new one has greater alignment requirements
		useNew = ( newAtom.alignment().trailingZeros() > existingAtom->alignment().trailingZeros() );
	}
	if ( useNew ) {
		_indirectBindingTable[slot] = &newAtom;
		if ( existingAtom != NULL ) 
			markCoalescedAway(existingAtom);
	}
	else {
		_indirectBindingTable[slot] = existingAtom;
		if ( existingAtom != &newAtom )
			markCoalescedAway(&newAtom);
	}
	// return if existing atom in symbol table was replaced
	return useNew && (existingAtom != NULL);
}

bool SymbolTable::addByReferences(const ld::Atom& newAtom)
{
	bool useNew = true;
	const ld::Atom* existingAtom;
	IndirectBindingSlot slot = this->findSlotForReferences(&newAtom, &existingAtom);
	//fprintf(stderr, "addByReferences(%p) name=%s, slot=%u, existing=%p\n", &newAtom, newAtom.name(), slot, existingAtom);
	if ( existingAtom != NULL ) {
		// use existing unless new one has greater alignment requirements
		useNew = ( newAtom.alignment().trailingZeros() > existingAtom->alignment().trailingZeros() );
	}
	if ( useNew ) {
		_indirectBindingTable[slot] = &newAtom;
		if ( existingAtom != NULL ) 
			markCoalescedAway(existingAtom);
	}
	else {
		if ( existingAtom != &newAtom )
			markCoalescedAway(&newAtom);
	}
	// return if existing atom in symbol table was replaced
	return useNew && (existingAtom != NULL);
}


bool SymbolTable::add(const ld::Atom& atom, bool ignoreDuplicates)
{
	//fprintf(stderr, "SymbolTable::add(%p), name=%s\n", &atom, atom.name());
	assert(atom.scope() != ld::Atom::scopeTranslationUnit);
	switch ( atom.combine() ) {
		case ld::Atom::combineNever:
		case ld::Atom::combineByName:
			return this->addByName(atom, ignoreDuplicates);
			break;
		case ld::Atom::combineByNameAndContent:
			return this->addByContent(atom);
			break;
		case ld::Atom::combineByNameAndReferences:
			return this->addByReferences(atom);
			break;
	}

	return false;
}

void SymbolTable::markCoalescedAway(const ld::Atom* atom)
{
	// remove this from list of all atoms used
	//fprintf(stderr, "markCoalescedAway(%p) from %s\n", atom, atom->file()->path());
	(const_cast<ld::Atom*>(atom))->setCoalescedAway();
	
	//
	// The fixupNoneGroupSubordinate* fixup kind is used to model group comdat.  
	// The "signature" atom in the group has a fixupNoneGroupSubordinate* fixup to
	// all other members of the group.  So, if the signature atom is 
	// coalesced away, all other atoms in the group should also be removed.  
	//
	for (ld::Fixup::iterator fit=atom->fixupsBegin(), fend=atom->fixupsEnd(); fit != fend; ++fit) {	
		switch ( fit->kind ) {
			case ld::Fixup::kindNoneGroupSubordinate:
			case ld::Fixup::kindNoneGroupSubordinateFDE:
			case ld::Fixup::kindNoneGroupSubordinateLSDA:
				assert(fit->binding == ld::Fixup::bindingDirectlyBound);
				this->markCoalescedAway(fit->u.target);
				break;
			default:
				break;
		}
	}

}

void SymbolTable::undefines(std::vector<const char*>& undefs)
{
	// return all names in _byNameTable that have no associated atom
	for (NameToSlot::iterator it=_byNameTable.begin(); it != _byNameTable.end(); ++it) {
		//fprintf(stderr, "  _byNameTable[%s] = slot %d which has atom %p\n", it->first, it->second, _indirectBindingTable[it->second]);
		if ( _indirectBindingTable[it->second] == NULL )
			undefs.push_back(it->first);
	}
	// sort so that undefines are in a stable order (not dependent on hashing functions)
	std::sort(undefs.begin(), undefs.end());
}


void SymbolTable::tentativeDefs(std::vector<const char*>& tents)
{
	// return all names in _byNameTable that have no associated atom
	for (NameToSlot::iterator it=_byNameTable.begin(); it != _byNameTable.end(); ++it) {
		const char* name = it->first;
		const ld::Atom* atom = _indirectBindingTable[it->second];
		if ( (atom != NULL) && (atom->definition() == ld::Atom::definitionTentative) )
			tents.push_back(name);
	}
	std::sort(tents.begin(), tents.end());
}


bool SymbolTable::hasName(const char* name)			
{ 
	NameToSlot::iterator pos = _byNameTable.find(name);
	if ( pos == _byNameTable.end() ) 
		return false;
	return (_indirectBindingTable[pos->second] != NULL); 
}

// find existing or create new slot
SymbolTable::IndirectBindingSlot SymbolTable::findSlotForName(const char* name)
{
	NameToSlot::iterator pos = _byNameTable.find(name);
	if ( pos != _byNameTable.end() ) 
		return pos->second;
	// create new slot for this name
	SymbolTable::IndirectBindingSlot slot = _indirectBindingTable.size();
	_indirectBindingTable.push_back(NULL);
	_byNameTable[name] = slot;
	_byNameReverseTable[slot] = name;
	return slot;
}


// find existing or create new slot
SymbolTable::IndirectBindingSlot SymbolTable::findSlotForContent(const ld::Atom* atom, const ld::Atom** existingAtom)
{
	//fprintf(stderr, "findSlotForContent(%p)\n", atom);
	SymbolTable::IndirectBindingSlot slot = 0;
	UTF16StringToSlot::iterator upos;
	CStringToSlot::iterator cspos;
	ContentToSlot::iterator pos;
	switch ( atom->section().type() ) {
		case ld::Section::typeCString:
			cspos = _cstringTable.find(atom);
			if ( cspos != _cstringTable.end() ) {
				*existingAtom = _indirectBindingTable[cspos->second];
				return cspos->second;
			}
			slot = _indirectBindingTable.size();
			_cstringTable[atom] = slot;
			break;
		case ld::Section::typeNonStdCString:
			{
				// use seg/sect name is key to map to avoid coalescing across segments and sections
				char segsect[64];
				sprintf(segsect, "%s/%s", atom->section().segmentName(), atom->section().sectionName());
				NameToMap::iterator mpos = _nonStdCStringSectionToMap.find(segsect);
				CStringToSlot* map = NULL;
				if ( mpos == _nonStdCStringSectionToMap.end() ) {
					map = new CStringToSlot();
					_nonStdCStringSectionToMap[strdup(segsect)] = map;
				}
				else {
					map = mpos->second;
				}
				cspos = map->find(atom);
				if ( cspos != map->end() ) {
					*existingAtom = _indirectBindingTable[cspos->second];
					return cspos->second;
				}
				slot = _indirectBindingTable.size();
				map->operator[](atom) = slot;
			}
			break;
		case ld::Section::typeUTF16Strings:
			upos = _utf16Table.find(atom);
			if ( upos != _utf16Table.end() ) {
				*existingAtom = _indirectBindingTable[upos->second];
				return upos->second;
			}
			slot = _indirectBindingTable.size();
			_utf16Table[atom] = slot;
			break;
		case ld::Section::typeLiteral4:
			pos = _literal4Table.find(atom);
			if ( pos != _literal4Table.end() ) {
				*existingAtom = _indirectBindingTable[pos->second];
				return pos->second;
			}
			slot = _indirectBindingTable.size();
			_literal4Table[atom] = slot;
			break;
		case ld::Section::typeLiteral8:
			pos = _literal8Table.find(atom);
			if ( pos != _literal8Table.end() ) {
				*existingAtom = _indirectBindingTable[pos->second];
				return pos->second;
			}
			slot = _indirectBindingTable.size();
			_literal8Table[atom] = slot;
			break;
		case ld::Section::typeLiteral16:
			pos = _literal16Table.find(atom);
			if ( pos != _literal16Table.end() ) {
				*existingAtom = _indirectBindingTable[pos->second];
				return pos->second;
			}
			slot = _indirectBindingTable.size();
			_literal16Table[atom] = slot;
			break;
		default:
			assert(0 && "section type does not support coalescing by content");
	}
	_indirectBindingTable.push_back(atom); 
	*existingAtom = NULL;
	return slot;
}



// find existing or create new slot
SymbolTable::IndirectBindingSlot SymbolTable::findSlotForReferences(const ld::Atom* atom, const ld::Atom** existingAtom)
{
	//fprintf(stderr, "findSlotForReferences(%p)\n", atom);
	
	SymbolTable::IndirectBindingSlot slot = 0;
	ReferencesToSlot::iterator pos;
	switch ( atom->section().type() ) {
		case ld::Section::typeNonLazyPointer:		
			pos = _nonLazyPointerTable.find(atom);
			if ( pos != _nonLazyPointerTable.end() ) {
				*existingAtom = _indirectBindingTable[pos->second];
				return pos->second;
			}
			slot = _indirectBindingTable.size();
			_nonLazyPointerTable[atom] = slot;
			break;
		case ld::Section::typeCFString:
			pos = _cfStringTable.find(atom);
			if ( pos != _cfStringTable.end() ) {
				*existingAtom = _indirectBindingTable[pos->second];
				return pos->second;
			}
			slot = _indirectBindingTable.size();
			_cfStringTable[atom] = slot;
			break;
		case ld::Section::typeObjCClassRefs:
			pos = _objc2ClassRefTable.find(atom);
			if ( pos != _objc2ClassRefTable.end() ) {
				*existingAtom = _indirectBindingTable[pos->second];
				return pos->second;
			}
			slot = _indirectBindingTable.size();
			_objc2ClassRefTable[atom] = slot;
			break;
		case ld::Section::typeCStringPointer:
			pos = _pointerToCStringTable.find(atom);
			if ( pos != _pointerToCStringTable.end() ) {
				*existingAtom = _indirectBindingTable[pos->second];
				return pos->second;
			}
			slot = _indirectBindingTable.size();
			_pointerToCStringTable[atom] = slot;
			break;
		default:
			assert(0 && "section type does not support coalescing by references");
	}
	_indirectBindingTable.push_back(atom);
	*existingAtom = NULL;
	return slot;
}


const char*	SymbolTable::indirectName(IndirectBindingSlot slot) const
{
	assert(slot < _indirectBindingTable.size());
	const ld::Atom* target = _indirectBindingTable[slot];
	if ( target != NULL )  {
		return target->name();
	}
	// handle case when by-name reference is indirected and no atom yet in _byNameTable
	SlotToName::const_iterator pos = _byNameReverseTable.find(slot);
	if ( pos != _byNameReverseTable.end() )
		return pos->second;
	assert(0);
	return NULL;
}

const ld::Atom* SymbolTable::indirectAtom(IndirectBindingSlot slot) const
{
	assert(slot < _indirectBindingTable.size());
	return _indirectBindingTable[slot];
}

extern "C" char* __cxa_demangle (const char* mangled_name,
				   char* buf,
				   size_t* n,
				   int* status);

const char* SymbolTable::demangle(const char* sym)
{
	// only try to demangle symbols if -demangle on command line
	if ( !_s_doDemangle )
		return sym;

	// only try to demangle symbols that look like C++ symbols
	if ( strncmp(sym, "__Z", 3) != 0 )
		return sym;

	static size_t size = 1024;
	static char* buff = (char*)malloc(size);
	int status;
	
	char* result = __cxa_demangle(&sym[1], buff, &size, &status); 
	if ( result != NULL ) {
		// if demangling succesful, keep buffer for next demangle
		buff = result;
		return buff;
	}
	return sym;
}


void SymbolTable::printStatistics()
{
//	fprintf(stderr, "cstring table size: %lu, bucket count: %lu, hash func called %u times\n", 
//				_cstringTable.size(), _cstringTable.bucket_count(), cstringHashCount);
	int count[11];
	for(unsigned int b=0; b < 11; ++b) {
		count[b] = 0;
	}
	for(unsigned int i=0; i < _cstringTable.bucket_count(); ++i) {
		unsigned int n = _cstringTable.elems_in_bucket(i);
		if ( n < 10 ) 
			count[n] += 1;
		else
			count[10] += 1;
	}
	fprintf(stderr, "cstring table distribution\n");
	for(unsigned int b=0; b < 11; ++b) {
		fprintf(stderr, "%u buckets have %u elements\n", count[b], b);
	}
	fprintf(stderr, "indirect table size: %lu\n", _indirectBindingTable.size());
	fprintf(stderr, "by-name table size: %lu\n", _byNameTable.size());
//	fprintf(stderr, "by-content table size: %lu, hash count: %u, equals count: %u, lookup count: %u\n", 
//						_byContentTable.size(), contentHashCount, contentEqualCount, contentLookupCount);
//	fprintf(stderr, "by-ref table size: %lu, hashed count: %u, equals count: %u, lookup count: %u, insert count: %u\n", 
//						_byReferencesTable.size(), refHashCount, refEqualsCount, refLookupCount, refInsertCount);

	//ReferencesHash obj;
	//for(ReferencesHashToSlot::iterator it=_byReferencesTable.begin(); it != _byReferencesTable.end(); ++it) {
	//	if ( obj.operator()(it->first) == 0x2F3AC0EAC744EA70 ) {
	//		fprintf(stderr, "hash=0x2F3AC0EAC744EA70 for %p %s from %s\n", it->first, it->first->name(), it->first->file()->path());
	//	
	//	}
	//}
	
}

} // namespace tool 
} // namespace ld 


/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-
 *
 * Copyright (c) 2010 Apple Inc. All rights reserved.
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


#include <stdint.h>
#include <math.h>
#include <string.h>
#include <unistd.h>

#include <vector>
#include <map>

#include "MachOFileAbstraction.hpp"
#include "ld.hpp"
#include "branch_shim.h"

namespace ld {
namespace passes {
namespace branch_shim {



static bool _s_log = false;
static ld::Section _s_text_section("__TEXT", "__text", ld::Section::typeCode);



class Thumb2ToArmShimAtom : public ld::Atom {
public:
											Thumb2ToArmShimAtom(const ld::Atom* target)
				: ld::Atom(_s_text_section, ld::Atom::definitionRegular, ld::Atom::combineNever,
							ld::Atom::scopeLinkageUnit, ld::Atom::typeUnclassified, 
							ld::Atom::symbolTableIn, false, true, false, ld::Atom::Alignment(1)), 
				_name(NULL),
				_target(target),
				_fixup1(8, ld::Fixup::k1of4, ld::Fixup::kindSetTargetAddress, target),
				_fixup2(8, ld::Fixup::k2of4, ld::Fixup::kindSubtractTargetAddress, this),
				_fixup3(8, ld::Fixup::k3of4, ld::Fixup::kindSubtractAddend, 8),
				_fixup4(8, ld::Fixup::k4of4, ld::Fixup::kindStoreLittleEndian32)
				 { asprintf((char**)&_name, "%s$shim", target->name()); }

	virtual const ld::File*					file() const					{ return NULL; }
	virtual bool							translationUnitSource(const char** dir, const char**) const 
																			{ return false; }
	virtual const char*						name() const					{ return _name; }
	virtual uint64_t						size() const					{ return 12; }
	virtual uint64_t						objectAddress() const			{ return 0; }
	virtual void							copyRawContent(uint8_t buffer[]) const {
		// Use ARM instructions that can jump to thumb.
		assert( !  _target->isThumb() );
		if (_s_log) fprintf(stderr, "3 Thumb2 instruction shim to jump to %s\n", _target->name());
		OSWriteLittleInt32(&buffer[0], 0, 0xc004f8df);	// 	ldr  ip, pc + 4
		OSWriteLittleInt16(&buffer[4], 0, 0x44fc);		// 	add	 ip, pc, ip
		OSWriteLittleInt16(&buffer[6], 0, 0x4760);		// 	bx	 ip
		OSWriteLittleInt32(&buffer[8], 0, 0x00000000);	// 	.long target-this		
	}

	virtual void							setScope(Scope)					{ }
	virtual ld::Fixup::iterator				fixupsBegin() const				{ return (ld::Fixup*)&_fixup1; }
	virtual ld::Fixup::iterator				fixupsEnd()	const				{ return &((ld::Fixup*)&_fixup4)[1]; }

private:
	const char*								_name;
	const ld::Atom*							_target;
	ld::Fixup								_fixup1;
	ld::Fixup								_fixup2;
	ld::Fixup								_fixup3;
	ld::Fixup								_fixup4;
};



class Thumb1ToArmShimAtom : public ld::Atom {
public:
											Thumb1ToArmShimAtom(const ld::Atom* target)
				: ld::Atom(_s_text_section, ld::Atom::definitionRegular, ld::Atom::combineNever,
							ld::Atom::scopeLinkageUnit, ld::Atom::typeUnclassified, 
							ld::Atom::symbolTableIn, false, true, false, ld::Atom::Alignment(1)), 
				_name(NULL),
				_target(target),
				_fixup1(12, ld::Fixup::k1of4, ld::Fixup::kindSetTargetAddress, target),
				_fixup2(12, ld::Fixup::k2of4, ld::Fixup::kindSubtractTargetAddress, this),
				_fixup3(12, ld::Fixup::k3of4, ld::Fixup::kindSubtractAddend, 8),
				_fixup4(12, ld::Fixup::k4of4, ld::Fixup::kindStoreLittleEndian32)
				 { asprintf((char**)&_name, "%s$shim", target->name()); }

	virtual const ld::File*					file() const					{ return NULL; }
	virtual bool							translationUnitSource(const char** dir, const char**) const 
																			{ return false; }
	virtual const char*						name() const					{ return _name; }
	virtual uint64_t						size() const					{ return 16; }
	virtual uint64_t						objectAddress() const			{ return 0; }
	virtual void							copyRawContent(uint8_t buffer[]) const {
		// Use ARM instructions that can jump to thumb.
		assert( ! _target->isThumb() );
		if (_s_log) fprintf(stderr, "6 Thumb1 instruction shim to jump to %s\n", _target->name());
		OSWriteLittleInt16(&buffer[ 0], 0, 0xb402);		// 	push	{r1}
		OSWriteLittleInt16(&buffer[ 2], 0, 0x4902);		// 	ldr		r1, [pc, #8]
		OSWriteLittleInt16(&buffer[ 4], 0, 0x4479);		// 	add		r1, pc
		OSWriteLittleInt16(&buffer[ 6], 0, 0x468c);		// 	mov		ip, r1
		OSWriteLittleInt16(&buffer[ 8], 0, 0xbc02);		// 	pop		{r1}
		OSWriteLittleInt16(&buffer[10], 0, 0x4760);		// 	bx		ip
		OSWriteLittleInt32(&buffer[12], 0, 0x00000000);	// 	.long target-this		
	}
	virtual void							setScope(Scope)					{ }
	virtual ld::Fixup::iterator				fixupsBegin() const				{ return (ld::Fixup*)&_fixup1; }
	virtual ld::Fixup::iterator				fixupsEnd()	const				{ return &((ld::Fixup*)&_fixup4)[1]; }

private:
	const char*								_name;
	const ld::Atom*							_target;
	ld::Fixup								_fixup1;
	ld::Fixup								_fixup2;
	ld::Fixup								_fixup3;
	ld::Fixup								_fixup4;
};




class ARMtoThumbShimAtom : public ld::Atom {
public:
											ARMtoThumbShimAtom(const ld::Atom* target)
				: ld::Atom(_s_text_section, ld::Atom::definitionRegular, ld::Atom::combineNever,
							ld::Atom::scopeLinkageUnit, ld::Atom::typeUnclassified, 
							ld::Atom::symbolTableIn, false, false, false, ld::Atom::Alignment(2)), 
				_name(NULL),
				_target(target),
				_fixup1(12, ld::Fixup::k1of4, ld::Fixup::kindSetTargetAddress, target),
				_fixup2(12, ld::Fixup::k2of4, ld::Fixup::kindSubtractTargetAddress, this),
				_fixup3(12, ld::Fixup::k3of4, ld::Fixup::kindSubtractAddend, 12),
				_fixup4(12, ld::Fixup::k4of4, ld::Fixup::kindStoreLittleEndian32)
				 { asprintf((char**)&_name, "%s$shim", target->name()); }

	virtual const ld::File*					file() const					{ return NULL; }
	virtual bool							translationUnitSource(const char** dir, const char**) const 
																			{ return false; }
	virtual const char*						name() const					{ return _name; }
	virtual uint64_t						size() const					{ return 16; }
	virtual uint64_t						objectAddress() const			{ return 0; }
	virtual void							copyRawContent(uint8_t buffer[]) const {
		// Use ARM instructions that can jump to thumb.
		assert( _target->isThumb() );
		if (_s_log) fprintf(stderr, "4 ARM instruction shim to jump to %s\n", _target->name());
		OSWriteLittleInt32(&buffer[ 0], 0, 0xe59fc004);	// 	ldr  ip, pc + 4
		OSWriteLittleInt32(&buffer[ 4], 0, 0xe08fc00c);	// 	add	 ip, pc, ip
		OSWriteLittleInt32(&buffer[ 8], 0, 0xe12fff1c);	// 	bx	 ip
		OSWriteLittleInt32(&buffer[12], 0, 0);			// 	.long target-this		
	}
	virtual void							setScope(Scope)					{ }
	virtual ld::Fixup::iterator				fixupsBegin() const				{ return (ld::Fixup*)&_fixup1; }
	virtual ld::Fixup::iterator				fixupsEnd()	const				{ return &((ld::Fixup*)&_fixup4)[1]; }

private:
	const char*								_name;
	const ld::Atom*							_target;
	ld::Fixup								_fixup1;
	ld::Fixup								_fixup2;
	ld::Fixup								_fixup3;
	ld::Fixup								_fixup4;
};






static void extractTarget(ld::Fixup::iterator fixup, ld::Internal& state, const ld::Atom** target)
{
	switch ( fixup->binding ) {
		case ld::Fixup::bindingNone:
			throw "unexpected bindingNone";
		case ld::Fixup::bindingByNameUnbound:
			throw "unexpected bindingByNameUnbound";
		case ld::Fixup::bindingByContentBound:
		case ld::Fixup::bindingDirectlyBound:
			*target = fixup->u.target;
			break;
		case ld::Fixup::bindingsIndirectlyBound:
			*target = state.indirectBindingTable[fixup->u.bindingIndex];
			break;
	}
}



//
// The tail-call optimzation may result in a function ending in a jump (b) 
// to another functions.  At compile time the compiler does not know 
// if the target of the jump will be in the same mode (arm vs thumb).
// The arm/thumb instruction set has a way to change modes in a bl(x)
// insruction, but no instruction to change mode in a jump (b) instruction.
// In those rare cases, the linker needs to insert a shim of code to 
// make the mode switch.
//
void doPass(const Options& opts, ld::Internal& state)
{	
	std::map<const Atom*, const Atom*> atomToThumbMap;
	std::map<const Atom*, const Atom*> thumbToAtomMap;
	std::vector<const Atom*> shims;

	// only make branch shims in final linked images
	if ( opts.outputKind() == Options::kObjectFile )
		return;

	// only ARM need branch islands
	if ( opts.architecture() != CPU_TYPE_ARM )
		return;
	
	// scan to find __text section
	ld::Internal::FinalSection* textSection = NULL;
	for (std::vector<ld::Internal::FinalSection*>::iterator sit=state.sections.begin(); sit != state.sections.end(); ++sit) {
		ld::Internal::FinalSection* sect = *sit;
		if ( strcmp(sect->sectionName(), "__text") == 0 )
			textSection = sect;
	}
	if ( textSection == NULL )
		return;
	
	// scan __text section for branch instructions that need to switch mode
	for (std::vector<const ld::Atom*>::iterator ait=textSection->atoms.begin();  ait != textSection->atoms.end(); ++ait) {
		const ld::Atom* atom = *ait;
		const ld::Atom* target;
		for (ld::Fixup::iterator fit = atom->fixupsBegin(), end=atom->fixupsEnd(); fit != end; ++fit) {
			switch ( fit->kind ) {
				case ld::Fixup::kindStoreTargetAddressThumbBranch22:
					extractTarget(fit, state, &target);
					if ( ! target->isThumb() ) {
						const uint8_t* fixUpLocation = atom->rawContentPointer() + fit->offsetInAtom;
						uint32_t instruction = *((uint32_t*)fixUpLocation);
						bool is_b = ((instruction & 0xD000F800) == 0x9000F000);
						if ( is_b ) {
							fprintf(stderr, "need to add thumb->arm instr=0x%08X shim to %s for %s\n", instruction, target->name(), atom->name()); 
							const Atom* shim = NULL;
							std::map<const Atom*, const Atom*>::iterator pos = thumbToAtomMap.find(target);
							if ( pos == thumbToAtomMap.end() ) {
								if ( opts.subArchitecture() == CPU_SUBTYPE_ARM_V7 )
									shim = new Thumb2ToArmShimAtom(target);
								else
									shim = new Thumb1ToArmShimAtom(target);
								shims.push_back(shim);
								thumbToAtomMap[target] = shim;
							}
							else {
								shim = pos->second;
							}
							fit->binding = ld::Fixup::bindingDirectlyBound;
							fit->u.target = shim;
						}
					}
					break;
				case ld::Fixup::kindStoreTargetAddressARMBranch24:
					extractTarget(fit, state, &target);
					if ( target->isThumb() ) {
						const uint8_t* fixUpLocation = atom->rawContentPointer() + fit->offsetInAtom;
						uint32_t instruction = *((uint32_t*)fixUpLocation);
						bool is_b = ((instruction & 0x0F000000) == 0x0A000000);
						if ( is_b ) {
							fprintf(stderr, "need to add arm->thumb instr=0x%08X shim to %s for %s\n", instruction, target->name(), atom->name()); 
							const Atom* shim = NULL;
							std::map<const Atom*, const Atom*>::iterator pos = atomToThumbMap.find(target);
							if ( pos == atomToThumbMap.end() ) {
								shim = new ARMtoThumbShimAtom(target);
								shims.push_back(shim);
								atomToThumbMap[target] = shim;
							}
							else {
								shim = pos->second;
							}
							fit->binding = ld::Fixup::bindingDirectlyBound;
							fit->u.target = shim;
						}
					}
					break;
				
				case ld::Fixup::kindStoreARMBranch24:
				case ld::Fixup::kindStoreThumbBranch22:
					fprintf(stderr, "found branch-22 without store in %s\n", atom->name()); 
					break;
				default:
					break;
			}
		}
	}

	// append all new shims to end of __text
	textSection->atoms.insert(textSection->atoms.end(), shims.begin(), shims.end());
}


} // namespace branch_shim
} // namespace passes 
} // namespace ld 

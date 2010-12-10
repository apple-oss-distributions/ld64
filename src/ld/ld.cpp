/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-*
 *
 * Copyright (c) 2005-2010 Apple Inc. All rights reserved.
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
 
// start temp HACK for cross builds
extern "C" double log2 ( double );
//#define __MATH__
// end temp HACK for cross builds


#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <execinfo.h>
#include <mach/mach_time.h>
#include <mach/vm_statistics.h>
#include <mach/mach_init.h>
#include <mach/mach_host.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <dlfcn.h>
#include <AvailabilityMacros.h>

#include <string>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <list>
#include <algorithm>
#include <ext/hash_map>
#include <ext/hash_set>
#include <cxxabi.h>

#include "Options.h"

#include "MachOFileAbstraction.hpp"
#include "Architectures.hpp"
#include "ld.hpp"

#include "InputFiles.h"
#include "Resolver.h"
#include "OutputFile.h"

#include "passes/stubs/make_stubs.h"
#include "passes/dtrace_dof.h"
#include "passes/got.h"
#include "passes/tlvp.h"
#include "passes/huge.h"
#include "passes/compact_unwind.h"
#include "passes/order_file.h"
#include "passes/branch_island.h"
#include "passes/branch_shim.h"
#include "passes/objc.h"
#include "passes/dylibs.h"

#include "parsers/archive_file.h"
#include "parsers/macho_relocatable_file.h"
#include "parsers/macho_dylib_file.h"
#include "parsers/lto_file.h"
#include "parsers/opaque_section_file.h"


class InternalState : public ld::Internal
{
public:
											InternalState(const Options& opts) : _options(opts) { }
	virtual	ld::Internal::FinalSection*		addAtom(const ld::Atom& atom);
	virtual ld::Internal::FinalSection*		getFinalSection(const ld::Section&);
	
	void									sortSections();
	virtual									~InternalState() {}
private:

	class FinalSection : public ld::Internal::FinalSection 
	{
	public:
							FinalSection(const ld::Section& sect, uint32_t sectionsSeen, bool objFile);
		static int					sectionComparer(const void* l, const void* r);
		static const ld::Section&	outputSection(const ld::Section& sect);
		static const ld::Section&	objectOutputSection(const ld::Section& sect, bool makeTentativeDefsReal);
	private:
		friend class InternalState;
		static uint32_t		sectionOrder(const ld::Section& sect, uint32_t sectionsSeen);
		static uint32_t		segmentOrder(const ld::Section& sect, bool objFile);
		uint32_t			_segmentOrder;
		uint32_t			_sectionOrder;

		static std::vector<const char*> _s_segmentsSeen;
		static ld::Section		_s_DATA_data;
		static ld::Section		_s_DATA_const;
		static ld::Section		_s_TEXT_text;
		static ld::Section		_s_TEXT_const;
		static ld::Section		_s_DATA_nl_symbol_ptr;
		static ld::Section		_s_DATA_common;
	};
	
	
	struct SectionHash {
		size_t operator()(const ld::Section*) const;
	};
	struct SectionEquals {
		bool operator()(const ld::Section* left, const ld::Section* right) const;
	};
	typedef __gnu_cxx::hash_map<const ld::Section*, FinalSection*, SectionHash, SectionEquals> SectionInToOut;
	

	SectionInToOut			_sectionInToFinalMap;
	const Options&			_options;
};

ld::Section	InternalState::FinalSection::_s_DATA_data( "__DATA", "__data",  ld::Section::typeUnclassified);
ld::Section	InternalState::FinalSection::_s_DATA_const("__DATA", "__const", ld::Section::typeUnclassified);
ld::Section	InternalState::FinalSection::_s_TEXT_text( "__TEXT", "__text",  ld::Section::typeCode);
ld::Section	InternalState::FinalSection::_s_TEXT_const("__TEXT", "__const", ld::Section::typeUnclassified);
ld::Section	InternalState::FinalSection::_s_DATA_nl_symbol_ptr("__DATA", "__nl_symbol_ptr", ld::Section::typeNonLazyPointer);
ld::Section	InternalState::FinalSection::_s_DATA_common("__DATA", "__common", ld::Section::typeZeroFill);
std::vector<const char*> InternalState::FinalSection::_s_segmentsSeen;


size_t InternalState::SectionHash::operator()(const ld::Section* sect) const
{
	size_t hash = 0;	
	__gnu_cxx::hash<const char*> temp;
	hash += temp.operator()(sect->segmentName());
	hash += temp.operator()(sect->sectionName());
	return hash;
}

bool InternalState::SectionEquals::operator()(const ld::Section* left, const ld::Section* right) const
{
	return (*left == *right);
}


InternalState::FinalSection::FinalSection(const ld::Section& sect, uint32_t sectionsSeen, bool objFile)
	: ld::Internal::FinalSection(sect), 
	  _segmentOrder(segmentOrder(sect, objFile)),
	  _sectionOrder(sectionOrder(sect, sectionsSeen))
{
	//fprintf(stderr, "FinalSection(%s, %s) _segmentOrder=%d, _sectionOrder=%d\n", 
	//		this->segmentName(), this->sectionName(), _segmentOrder, _sectionOrder);
}

const ld::Section& InternalState::FinalSection::outputSection(const ld::Section& sect)
{
	// merge sections in final linked image
	switch ( sect.type() ) {
		case ld::Section::typeLiteral4:
		case ld::Section::typeLiteral8:
		case ld::Section::typeLiteral16:
			return _s_TEXT_const;
		case ld::Section::typeUnclassified:
			if ( strcmp(sect.segmentName(), "__DATA") == 0 ) {
				if ( strcmp(sect.sectionName(), "__datacoal_nt") == 0 )
					return _s_DATA_data;
				if ( strcmp(sect.sectionName(), "__const_coal") == 0 )
					return _s_DATA_const;
			}
			else if ( strcmp(sect.segmentName(), "__TEXT") == 0 ) {
				if ( strcmp(sect.sectionName(), "__const_coal") == 0 )
					return _s_TEXT_const;
			}
			break;
		case ld::Section::typeCode:
			if ( strcmp(sect.segmentName(), "__TEXT") == 0 ) {
				if ( strcmp(sect.sectionName(), "__textcoal_nt") == 0 )
					return _s_TEXT_text;
				else if ( strcmp(sect.sectionName(), "__StaticInit") == 0 )
					return _s_TEXT_text;
			}
			break;
		case ld::Section::typeNonLazyPointer:
			if ( strcmp(sect.segmentName(), "__DATA") == 0 ) {
				if ( strcmp(sect.sectionName(), "__nl_symbol_ptr") == 0 )
					return _s_DATA_nl_symbol_ptr;
			}
			else if ( strcmp(sect.segmentName(), "__IMPORT") == 0 ) {
				if ( strcmp(sect.sectionName(), "__pointers") == 0 )
					return _s_DATA_nl_symbol_ptr; 
			}
			break;
		case ld::Section::typeTentativeDefs:
			return _s_DATA_common;
			break;
			// FIX ME: more 
		default:
			break;
	}
	return sect;
}

const ld::Section& InternalState::FinalSection::objectOutputSection(const ld::Section& sect, bool makeTentativeDefsReal)
{
	// in -r mode the only section that ever changes is __tenative -> __common with -d option
	if ( (sect.type() == ld::Section::typeTentativeDefs) && makeTentativeDefsReal)
		return _s_DATA_common;
	return sect;
}

uint32_t InternalState::FinalSection::segmentOrder(const ld::Section& sect, bool objFile)
{
	if ( strcmp(sect.segmentName(), "__PAGEZERO") == 0 ) 
		return 0;
	if ( strcmp(sect.segmentName(), "__HEADER") == 0 ) // only used with -preload
		return 0;
	if ( strcmp(sect.segmentName(), "__TEXT") == 0 ) 
		return 1;
	// in -r mode, want __DATA  last so zerofill sections are at end
	if ( strcmp(sect.segmentName(), "__DATA") == 0 ) 
		return (objFile ? 5 : 2);
	if ( strcmp(sect.segmentName(), "__OBJC") == 0 ) 
		return 3;
	if ( strcmp(sect.segmentName(), "__IMPORT") == 0 ) 
		return 4;
	
	// layout non-standard segments in order seen (+10 to shift beyond standard segments)
	for (uint32_t i=0; i < _s_segmentsSeen.size(); ++i) {
		if ( strcmp(_s_segmentsSeen[i], sect.segmentName()) == 0 )
			return i+10;
	}
	_s_segmentsSeen.push_back(sect.segmentName());
	return _s_segmentsSeen.size()-1+10;
}

uint32_t InternalState::FinalSection::sectionOrder(const ld::Section& sect, uint32_t sectionsSeen)
{
	if ( sect.type() == ld::Section::typeFirstSection )
		return 0;
	if ( sect.type() == ld::Section::typeMachHeader )
		return 1;
	if ( sect.type() == ld::Section::typeLastSection )
		return INT_MAX;
	if ( strcmp(sect.segmentName(), "__TEXT") == 0 ) {
		switch ( sect.type() ) {
			case ld::Section::typeCode:
				// <rdar://problem/8346444> make __text always be first "code" section
				if ( strcmp(sect.sectionName(), "__text") == 0 )
					return 10;
				else
					return 11;
			case ld::Section::typeStub:
				return 12;
			case ld::Section::typeStubHelper:
				return 13;
			case ld::Section::typeLSDA:
				return INT_MAX-3;
			case ld::Section::typeUnwindInfo:
				return INT_MAX-2;
			case ld::Section::typeCFI:
				return INT_MAX-1;
			case ld::Section::typeStubClose:
				return INT_MAX;
			default:
				return sectionsSeen+20;
		}
	}
	else if ( strcmp(sect.segmentName(), "__DATA") == 0 ) {
		switch ( sect.type() ) {
			case ld::Section::typeLazyPointerClose:
				return 8;
			case ld::Section::typeDyldInfo:
				return 9;
			case ld::Section::typeNonLazyPointer:
				return 10;
			case ld::Section::typeLazyPointer:
				return 11;
			case ld::Section::typeInitializerPointers:
				return 12;
			case ld::Section::typeTerminatorPointers:
				return 13;
			case ld::Section::typeTLVInitialValues:
				return INT_MAX-4; // need TLV zero-fill to follow TLV init values
			case ld::Section::typeTLVZeroFill:
				return INT_MAX-3;
			case ld::Section::typeZeroFill:
				// make sure __huge is always last zerofill section
				if ( strcmp(sect.sectionName(), "__huge") == 0 )
					return INT_MAX-1;
				else
					return INT_MAX-2;
			default:
				// <rdar://problem/7435296> Reorder sections to reduce page faults in object files
				if ( strcmp(sect.sectionName(), "__objc_classlist") == 0 ) 
					return 20;
				else if ( strcmp(sect.sectionName(), "__objc_nlclslist") == 0 ) 
					return 21;
				else if ( strcmp(sect.sectionName(), "__objc_catlist") == 0 ) 
					return 22;
				else if ( strcmp(sect.sectionName(), "__objc_protolist") == 0 ) 
					return 23;
				else if ( strcmp(sect.sectionName(), "__objc_imageinfo") == 0 ) 
					return 24;
				else if ( strcmp(sect.sectionName(), "__objc_const") == 0 ) 
					return 25;
				else if ( strcmp(sect.sectionName(), "__objc_selrefs") == 0 ) 
					return 26;
				else if ( strcmp(sect.sectionName(), "__objc_msgrefs") == 0 ) 
					return 27;
				else if ( strcmp(sect.sectionName(), "__objc_protorefs") == 0 ) 
					return 28;
				else if ( strcmp(sect.sectionName(), "__objc_classrefs") == 0 ) 
					return 29;
				else if ( strcmp(sect.sectionName(), "__objc_superrefs") == 0 ) 
					return 30;
				else if ( strcmp(sect.sectionName(), "__objc_data") == 0 ) 
					return 31;
				else
					return sectionsSeen+40;
		}
	}
	// make sure zerofill in any other section is at end of segment
	if ( sect.type() == ld::Section::typeZeroFill )
		return INT_MAX-1;
	return sectionsSeen+20;
}

#ifndef NDEBUG
static void validateFixups(const ld::Atom& atom)
{
	//fprintf(stderr, "validateFixups %s\n", atom.name());
	bool lastWasClusterEnd = true;
	ld::Fixup::Cluster lastClusterSize = ld::Fixup::k1of1;
	uint32_t curClusterOffsetInAtom = 0;
	for (ld::Fixup::iterator fit=atom.fixupsBegin(); fit != atom.fixupsEnd(); ++fit) {
		//fprintf(stderr, "  fixup offset=%d, cluster=%d\n", fit->offsetInAtom, fit->clusterSize);
		assert((fit->offsetInAtom < atom.size()) || (fit->offsetInAtom == 0));
		if ( fit->firstInCluster() ) {
			assert(lastWasClusterEnd);
			curClusterOffsetInAtom = fit->offsetInAtom;
			lastWasClusterEnd = (fit->clusterSize == ld::Fixup::k1of1);
		}
		else {
			assert(!lastWasClusterEnd);
			assert(fit->offsetInAtom == curClusterOffsetInAtom);
			switch ((ld::Fixup::Cluster)fit->clusterSize) {
				case ld::Fixup::k1of1:
				case ld::Fixup::k1of2:
				case ld::Fixup::k1of3:
				case ld::Fixup::k1of4:
				case ld::Fixup::k1of5:
					lastWasClusterEnd = false;
					break;
				case ld::Fixup::k2of2:
					assert(lastClusterSize = ld::Fixup::k1of2);
					lastWasClusterEnd = true;
					break;
				case ld::Fixup::k2of3:
					assert(lastClusterSize = ld::Fixup::k1of3);
					lastWasClusterEnd = false;
					break;
				case ld::Fixup::k2of4:
					assert(lastClusterSize = ld::Fixup::k1of4);
					lastWasClusterEnd = false;
					break;
				case ld::Fixup::k2of5:
					assert(lastClusterSize = ld::Fixup::k1of5);
					lastWasClusterEnd = false;
					break;
				case ld::Fixup::k3of3:
					assert(lastClusterSize = ld::Fixup::k2of3);
					lastWasClusterEnd = true;
					break;
				case ld::Fixup::k3of4:
					assert(lastClusterSize = ld::Fixup::k2of4);
					lastWasClusterEnd = false;
					break;
				case ld::Fixup::k3of5:
					assert(lastClusterSize = ld::Fixup::k2of5);
					lastWasClusterEnd = false;
					break;
				case ld::Fixup::k4of4:
					assert(lastClusterSize = ld::Fixup::k3of4);
					lastWasClusterEnd = true;
					break;
				case ld::Fixup::k4of5:
					assert(lastClusterSize = ld::Fixup::k3of5);
					lastWasClusterEnd = false;
					break;
				case ld::Fixup::k5of5:
					assert(lastClusterSize = ld::Fixup::k4of5);
					lastWasClusterEnd = true;
					break;
			}
		}
		lastClusterSize = fit->clusterSize;
		if ( fit->binding == ld::Fixup::bindingDirectlyBound ) {
			assert(fit->u.target != NULL);
		}
	}
	switch (lastClusterSize) {
		case ld::Fixup::k1of1:
		case ld::Fixup::k2of2:
		case ld::Fixup::k3of3:
		case ld::Fixup::k4of4:
		case ld::Fixup::k5of5:
			break;
		default:
			assert(0 && "last fixup was not end of cluster");
			break;
	}
}
#endif

ld::Internal::FinalSection* InternalState::addAtom(const ld::Atom& atom)
{
	ld::Internal::FinalSection* fs = this->getFinalSection(atom.section());

	// <rdar://problem/8612550> When order file used on data, turn ordered zero fill symbols into zero data
	switch ( atom.section().type() ) {
		case ld::Section::typeZeroFill:
		case ld::Section::typeTentativeDefs:
			if ( (_options.outputKind() == Options::kDyld) && (atom.symbolTableInclusion() == ld::Atom::symbolTableIn) 
					&& (atom.size() <= 512) && (_options.orderedSymbolsCount() != 0) ) {
				for(Options::OrderedSymbolsIterator it = _options.orderedSymbolsBegin(); it != _options.orderedSymbolsEnd(); ++it) {
					if ( (it->objectFileName == NULL) && (strcmp(it->symbolName, atom.name()) == 0) ) {
						// found in order file, move to __data section
						fs = getFinalSection(InternalState::FinalSection::_s_DATA_data);\
						//fprintf(stderr, "moved %s to __data section\n", atom.name());
						break;
					}
				}
			}
			break;
		default:
			break;
	}
	
	//fprintf(stderr, "InternalState::doAtom(%p), name=%s, sect=%s, finalsect=%p\n", &atom, atom.name(), atom.section().sectionName(), fs);
#ifndef NDEBUG
	validateFixups(atom);
#endif
	fs->atoms.push_back(&atom);
	return fs;
}

ld::Internal::FinalSection* InternalState::getFinalSection(const ld::Section& inputSection)
{	
	const ld::Section* baseForFinalSection = &inputSection;
	
	// see if input section already has a FinalSection
	SectionInToOut::iterator pos = _sectionInToFinalMap.find(&inputSection);
	if ( pos != _sectionInToFinalMap.end() ) {
		return pos->second;
	}

	// otherwise, create a new final section
	bool objFile = false;
	switch ( _options.outputKind() ) {
		case Options::kStaticExecutable:
		case Options::kDynamicExecutable:
		case Options::kDynamicLibrary:
		case Options::kDynamicBundle:
		case Options::kDyld:
		case Options::kKextBundle:
		case Options::kPreload:
			{
				// coalesce some sections
				const ld::Section& outSect = FinalSection::outputSection(inputSection);
				pos = _sectionInToFinalMap.find(&outSect);
				if ( pos != _sectionInToFinalMap.end() ) {
					_sectionInToFinalMap[&inputSection] = pos->second;
					//fprintf(stderr, "_sectionInToFinalMap[%p] = %p\n", &inputSection, pos->second);
					return pos->second;
				}
				else if ( outSect != inputSection ) {
					// new output section created, but not in map
					baseForFinalSection = &outSect;
				}
			}
			break;
		case Options::kObjectFile:
			baseForFinalSection = &FinalSection::objectOutputSection(inputSection, _options.makeTentativeDefinitionsReal());
			pos = _sectionInToFinalMap.find(baseForFinalSection);
			if ( pos != _sectionInToFinalMap.end() ) {
				_sectionInToFinalMap[&inputSection] = pos->second;
				//fprintf(stderr, "_sectionInToFinalMap[%p] = %p\n", &inputSection, pos->second);
				return pos->second;
			}
			objFile = true;
			break;
	}

	InternalState::FinalSection* result = new InternalState::FinalSection(*baseForFinalSection, 
																	_sectionInToFinalMap.size(), objFile);
	_sectionInToFinalMap[baseForFinalSection] = result;
	//fprintf(stderr, "_sectionInToFinalMap[%p] = %p\n", baseForFinalSection, result);
	sections.push_back(result);
	return result;
}


int InternalState::FinalSection::sectionComparer(const void* l, const void* r)
{
	const FinalSection* left  = *(FinalSection**)l;
	const FinalSection* right = *(FinalSection**)r;
	if ( left->_segmentOrder != right->_segmentOrder )
		return (left->_segmentOrder - right->_segmentOrder);
	return (left->_sectionOrder - right->_sectionOrder);
}

void InternalState::sortSections()
{
	//fprintf(stderr, "UNSORTED final sections:\n");
	//for (std::vector<ld::Internal::FinalSection*>::iterator it = sections.begin(); it != sections.end(); ++it) {
	//	fprintf(stderr, "final section %p %s/%s\n", (*it), (*it)->segmentName(), (*it)->sectionName());
	//}
	qsort(&sections[0], sections.size(), sizeof(FinalSection*), &InternalState::FinalSection::sectionComparer);
	//fprintf(stderr, "SORTED final sections:\n");
	//for (std::vector<ld::Internal::FinalSection*>::iterator it = sections.begin(); it != sections.end(); ++it) {
	//	fprintf(stderr, "final section %p %s/%s\n", (*it), (*it)->segmentName(), (*it)->sectionName());
	//}
	assert((sections[0]->type() == ld::Section::typeMachHeader) 
		|| ((sections[0]->type() == ld::Section::typeFirstSection) && (sections[1]->type() == ld::Section::typeMachHeader))
		|| ((sections[0]->type() == ld::Section::typePageZero) && (sections[1]->type() == ld::Section::typeMachHeader))
		|| ((sections[0]->type() == ld::Section::typePageZero) && (sections[1]->type() == ld::Section::typeFirstSection) && (sections[2]->type() == ld::Section::typeMachHeader)) );
	
}

static void getVMInfo(vm_statistics_data_t& info)
{
	mach_msg_type_number_t count = sizeof(vm_statistics_data_t) / sizeof(natural_t);
	kern_return_t error = host_statistics(mach_host_self(), HOST_VM_INFO,
							(host_info_t)&info, &count);
	if (error != KERN_SUCCESS) {
		bzero(&info, sizeof(vm_statistics_data_t));
	}
}

int main(int argc, const char* argv[])
{
#if DEBUG
	usleep(1000000);
#endif
	const char* archName = NULL;
	bool showArch = false;
	bool archInferred = false;
	try {
		vm_statistics_data_t vmStart;
		vm_statistics_data_t vmEnd;
		getVMInfo(vmStart);
	
		// create object to track command line arguments
		Options options(argc, argv);
		
		// gather stats
		if ( options.printStatistics() )
			getVMInfo(vmStart);

		// update strings for error messages
		showArch = options.printArchPrefix();
		archName = options.architectureName();
		archInferred = (options.architecture() == 0);
		
		// open and parse input files
		ld::tool::InputFiles inputFiles(options, &archName);
		
		// load and resolve all references
		InternalState state(options);
		ld::tool::Resolver resolver(options, inputFiles, state);
		resolver.resolve();
				
		// add dylibs used
		inputFiles.dylibs(state);
	
		// do initial section sorting so passes have rough idea of the layout
		state.sortSections();

		// run passes
		ld::passes::objc::doPass(options, state);
		ld::passes::stubs::doPass(options, state);
		ld::passes::huge::doPass(options, state);
		ld::passes::got::doPass(options, state);
		ld::passes::tlvp::doPass(options, state);
		ld::passes::dylibs::doPass(options, state);	// must be after stubs and GOT passes
		ld::passes::order_file::doPass(options, state);
		ld::passes::branch_shim::doPass(options, state);	// must be after stubs 
		ld::passes::branch_island::doPass(options, state);	// must be after stubs and order_file pass
		ld::passes::dtrace::doPass(options, state);
		ld::passes::compact_unwind::doPass(options, state);  // must be after order-file pass
 		
		// sort final sections
		state.sortSections();

		// write output file
		ld::tool::OutputFile out(options);
		out.write(state);
		
		// print statistics
		//mach_o::relocatable::printCounts();
		if ( options.printStatistics() ) {
			getVMInfo(vmEnd);
			fprintf(stderr, "pageins=%u, pageouts=%u, faults=%u\n", vmEnd.pageins-vmStart.pageins,
										vmEnd.pageouts-vmStart.pageouts, vmEnd.faults-vmStart.faults);
			
		}
	}
	catch (const char* msg) {
		if ( archInferred )
			fprintf(stderr, "ld: %s for inferred architecture %s\n", msg, archName);
		else if ( showArch )
			fprintf(stderr, "ld: %s for architecture %s\n", msg, archName);
		else
			fprintf(stderr, "ld: %s\n", msg);
		return 1;
	}

	return 0;
}


#ifndef NDEBUG
// implement assert() function to print out a backtrace before aborting
void __assert_rtn(const char* func, const char* file, int line, const char* failedexpr)
{
	fprintf(stderr, "Assertion failed: (%s), function %s, file %s, line %d.\n", failedexpr, func, file, line);

	void* callStack[128];
	int depth = ::backtrace(callStack, 128);
	char* buffer = (char*)malloc(1024);
	for(int i=0; i < depth-1; ++i) {
		Dl_info info;
		dladdr(callStack[i], &info);
		const char* symboName = info.dli_sname;
		if ( (symboName != NULL) && (strncmp(symboName, "_Z", 2) == 0) ) {
			size_t bufLen = 1024;
			int result;
			char* unmangled = abi::__cxa_demangle(symboName, buffer, &bufLen, &result);
			if ( unmangled != NULL )
				symboName = unmangled;
		}
		long offset = (uintptr_t)callStack[i] - (uintptr_t)info.dli_saddr;
		fprintf(stderr, "%d  %p  %s + %ld\n", i, callStack[i], symboName, offset);
	}
	exit(1);
}
#endif



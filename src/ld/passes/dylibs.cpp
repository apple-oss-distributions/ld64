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
#include <unistd.h>
#include <dlfcn.h>
#include <mach/machine.h>

#include <vector>

#include "ld.hpp"
#include "dylibs.h"

namespace ld {
namespace passes {
namespace dylibs {


class WillBeUsed
{
public:
	bool operator()(ld::dylib::File* dylib) const {
		return dylib->willRemoved();
	}
};


void doPass(const Options& opts, ld::Internal& state)
{
//	const bool log = false;
	
	// only optimize dylibs in final linked images
	if ( opts.outputKind() == Options::kObjectFile )
		return;

	// clear "willRemoved" bit on all dylibs
	for (std::vector<ld::dylib::File*>::iterator it = state.dylibs.begin(); it != state.dylibs.end(); ++it) {
		ld::dylib::File* aDylib = *it;
		aDylib->setWillBeRemoved(false);
	}
	for (std::vector<ld::dylib::File*>::iterator it = state.dylibs.begin(); it != state.dylibs.end(); ++it) {
		ld::dylib::File* aDylib = *it;
		// set "willRemoved" bit on implicit dylibs that did not provide any exports
		if ( aDylib->implicitlyLinked() && !aDylib->explicitlyLinked() && !aDylib->providedExportAtom() )
			aDylib->setWillBeRemoved(true);
		// set "willRemoved" bit on dead strippable explicit dylibs that did not provide any exports
		if ( aDylib->explicitlyLinked() && aDylib->deadStrippable() && !aDylib->providedExportAtom() )
			aDylib->setWillBeRemoved(true);
		// set "willRemoved" bit on any unused explicit when -dead_strip_dylibs is used
		if ( opts.deadStripDylibs() && !aDylib->providedExportAtom() )
			aDylib->setWillBeRemoved(true);
	}
	
	
	// remove unused dylibs
	state.dylibs.erase(std::remove_if(state.dylibs.begin(), state.dylibs.end(), WillBeUsed()), state.dylibs.end());
	
}


} // namespace dylibs
} // namespace passes 
} // namespace ld 

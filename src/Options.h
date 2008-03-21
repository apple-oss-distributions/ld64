/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-
 *
 * Copyright (c) 2005 Apple Computer, Inc. All rights reserved.
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

#ifndef __OPTIONS__
#define __OPTIONS__


#include <stdint.h>
#include <mach/machine.h>

#include <vector>
#include <ext/hash_set>

#include "ObjectFile.h"

void throwf (const char* format, ...) __attribute__ ((noreturn));

class DynamicLibraryOptions
{
public:
	DynamicLibraryOptions() : fWeakImport(false), fReExport(false), fBundleLoader(false), fInstallPathOverride(NULL) {}

	bool		fWeakImport;
	bool		fReExport;
	bool		fBundleLoader;
	const char* fInstallPathOverride;
};

//
// The public interface to the Options class is the abstract representation of what work the linker
// should do.
//
// This abstraction layer will make it easier to support a future where the linker is a shared library
// invoked directly from Xcode.  The target settings in Xcode would be used to directly construct an Options
// object (without building a command line which is then parsed).
//
//
class Options
{
public:
	Options(int argc, const char* argv[]);
	~Options();

	enum OutputKind { kDynamicExecutable, kStaticExecutable, kDynamicLibrary, kDynamicBundle, kObjectFile, kDyld };
	enum NameSpace { kTwoLevelNameSpace, kFlatNameSpace, kForceFlatNameSpace };
	// Standard treatment for many options.
	enum Treatment { kError, kWarning, kSuppress, kNULL, kInvalid };
	enum UndefinedTreatment { kUndefinedError, kUndefinedWarning, kUndefinedSuppress, kUndefinedDynamicLookup };
	enum WeakReferenceMismatchTreatment { kWeakReferenceMismatchError, kWeakReferenceMismatchWeak,
										  kWeakReferenceMismatchNonWeak };
	enum CommonsMode { kCommonsIgnoreDylibs, kCommonsOverriddenByDylibs, kCommonsConflictsDylibsError };
	enum DeadStripMode { kDeadStripOff, kDeadStripOn, kDeadStripOnPlusUnusedInits };
	enum VersionMin { kMinUnset, k10_1, k10_2, k10_3, k10_4, k10_5 };

	struct FileInfo {
		const char*				path;
		uint64_t				fileLen;
		time_t					modTime;
		DynamicLibraryOptions	options;
	};

	struct ExtraSection {
		const char*				segmentName;
		const char*				sectionName;
		const char*				path;
		const uint8_t*			data;
		uint64_t				dataLen;
	};

	struct SectionAlignment {
		const char*				segmentName;
		const char*				sectionName;
		uint8_t					alignment;
	};

	const ObjectFile::ReaderOptions&	readerOptions();
	const char*							getOutputFilePath();
	std::vector<FileInfo>&				getInputFiles();

	cpu_type_t					architecture();
	OutputKind					outputKind();
	bool						stripLocalSymbols();
	bool						prebind();
	bool						bindAtLoad();
	bool						fullyLoadArchives();
	NameSpace					nameSpace();
	const char*					installPath();			// only for kDynamicLibrary
	uint32_t					currentVersion();		// only for kDynamicLibrary
	uint32_t					compatibilityVersion();	// only for kDynamicLibrary
	const char*					entryName();			// only for kDynamicExecutable or kStaticExecutable
	const char*					executablePath();
	uint64_t					baseAddress();
	bool						keepPrivateExterns();	// only for kObjectFile
	bool						interposable();			// only for kDynamicLibrary
	bool						hasExportRestrictList();
	bool						allGlobalsAreDeadStripRoots();
	bool						shouldExport(const char*);
	bool						ignoreOtherArchInputFiles();
	bool						forceCpuSubtypeAll();
	bool						traceDylibs();
	bool						traceArchives();
	DeadStripMode				deadStrip();
	UndefinedTreatment			undefinedTreatment();
	VersionMin					macosxVersionMin();
	bool						messagesPrefixedWithArchitecture();
	Treatment					picTreatment();
	WeakReferenceMismatchTreatment	weakReferenceMismatchTreatment();
	Treatment					multipleDefinitionsInDylibs();
	Treatment					overridingDefinitionInDependentDylib();
	bool						warnOnMultipleDefinitionsInObjectFiles();
	const char*					umbrellaName();
	std::vector<const char*>&	allowableClients();
	const char*					clientName();
	const char*					initFunctionName();			// only for kDynamicLibrary
	const char*					dotOutputFile();
	uint64_t					zeroPageSize();
	bool						hasCustomStack();
	uint64_t					customStackSize();
	uint64_t					customStackAddr();
	bool						hasExecutableStack();
	std::vector<const char*>&	initialUndefines();
	bool						printWhyLive(const char* name);
	uint32_t					minimumHeaderPad();
	std::vector<ExtraSection>&	extraSections();
	std::vector<SectionAlignment>&	sectionAlignments();
	CommonsMode					commonsMode();
	bool						warnCommons();
	bool						keepRelocations();
	std::vector<const char*>&   traceSymbols();
	FileInfo					findFile(const char* path);
	bool						emitUUID();
	bool						warnStabs();
	bool						pauseAtEnd() { return fPause; }
	bool						printStatistics() { return fStatistics; }
	bool						printArchPrefix() { return fMessagesPrefixedWithArchitecture; }
	bool						makeTentativeDefinitionsReal() { return fMakeTentativeDefinitionsReal; }

private:
	class CStringEquals
	{
	public:
		bool operator()(const char* left, const char* right) const { return (strcmp(left, right) == 0); }
	};

	typedef __gnu_cxx::hash_set<const char*, __gnu_cxx::hash<const char*>, CStringEquals>  NameSet;
	enum ExportMode { kExportDefault, kExportSome, kDontExportSome };
	enum LibrarySearchMode { kSearchDylibAndArchiveInEachDir, kSearchAllDirsForDylibsThenAllDirsForArchives };

	void						parse(int argc, const char* argv[]);
	void						checkIllegalOptionCombinations();
	void						buildSearchPaths(int argc, const char* argv[]);
	void						parseArch(const char* architecture);
	FileInfo					findLibrary(const char* rootName);
	FileInfo					findFramework(const char* frameworkName);
	FileInfo					findFramework(const char* rootName, const char* suffix);
	bool						checkForFile(const char* format, const char* dir, const char* rootName,
											 FileInfo& result);
	uint32_t					parseVersionNumber(const char*);
	void						parseSectionOrderFile(const char* segment, const char* section, const char* path);
	void						addSection(const char* segment, const char* section, const char* path);
	void						addSubLibrary(const char* name);
	void						loadFileList(const char* fileOfPaths);
	uint64_t					parseAddress(const char* addr);
	void						loadExportFile(const char* fileOfExports, const char* option, NameSet& set);
	void						parsePreCommandLineEnvironmentSettings();
	void						parsePostCommandLineEnvironmentSettings();
	void						setUndefinedTreatment(const char* treatment);
	void						setVersionMin(const char* version);
	void						setWeakReferenceMismatchTreatment(const char* treatment);
	void						setDylibInstallNameOverride(const char* paths);
	void						addSectionAlignment(const char* segment, const char* section, const char* alignment);
	CommonsMode					parseCommonsTreatment(const char* mode);
	Treatment					parseTreatment(const char* treatment);
	void						reconfigureDefaults();


	ObjectFile::ReaderOptions			fReaderOptions;
	const char*							fOutputFile;
	std::vector<Options::FileInfo>		fInputFiles;
	cpu_type_t							fArchitecture;
	OutputKind							fOutputKind;
	bool								fPrebind;
	bool								fBindAtLoad;
	bool								fStripLocalSymbols;
	bool								fKeepPrivateExterns;
	bool								fInterposable;
	bool								fIgnoreOtherArchFiles;
	bool								fForceSubtypeAll;
	DeadStripMode						fDeadStrip;
	VersionMin							fVersionMin;
	NameSpace							fNameSpace;
	uint32_t							fDylibCompatVersion;
	uint32_t							fDylibCurrentVersion;
	const char*							fDylibInstallName;
	const char*							fEntryName;
	uint64_t							fBaseAddress;
	NameSet								fExportSymbols;
	NameSet								fDontExportSymbols;
	ExportMode							fExportMode;
	LibrarySearchMode					fLibrarySearchMode;
	UndefinedTreatment					fUndefinedTreatment;
	bool								fMessagesPrefixedWithArchitecture;
	Treatment							fPICTreatment;
	WeakReferenceMismatchTreatment		fWeakReferenceMismatchTreatment;
	Treatment							fMultiplyDefinedDynamic;
	Treatment							fMultiplyDefinedUnused;
	bool								fWarnOnMultiplyDefined;
	std::vector<const char*>			fSubUmbellas;
	std::vector<const char*>			fSubLibraries;
	std::vector<const char*>			fAllowableClients;
	const char*							fClientName;
	const char*							fUmbrellaName;
	const char*							fInitFunctionName;
	const char*							fDotOutputFile;
	const char*							fExecutablePath;
	const char*							fBundleLoader;
	uint64_t							fZeroPageSize;
	uint64_t							fStackSize;
	uint64_t							fStackAddr;
	bool								fExecutableStack;
	uint32_t							fMinimumHeaderPad;
	CommonsMode							fCommonsMode;
	bool								fWarnCommons;
	bool								fVerbose;
	bool								fKeepRelocations;
	bool								fEmitUUID;
	bool								fWarnStabs;
	bool								fTraceDylibSearching;
	bool								fPause;
	bool								fStatistics;
	bool								fPrintOptions;
	bool								fMakeTentativeDefinitionsReal;
	std::vector<const char*>			fInitialUndefines;
	NameSet								fWhyLive;
	std::vector<const char*>			fTraceSymbols;
	unsigned long						fLimitUndefinedSymbols;
	std::vector<ExtraSection>			fExtraSections;
	std::vector<SectionAlignment>		fSectionAlignments;

	std::vector<const char*>			fLibrarySearchPaths;
	std::vector<const char*>			fFrameworkSearchPaths;
	std::vector<const char*>			fSDKPaths;
	bool								fAllowStackExecute;

};




#endif // __OPTIONS__

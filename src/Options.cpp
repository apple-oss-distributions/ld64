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


#include <sys/types.h>
#include <sys/stat.h>
#include <math.h>
#include <fcntl.h>
#include <vector>


#include "Options.h"

void throwf(const char* format, ...)
{
	va_list	list;
	char*	p;
	va_start(list, format);
	vasprintf(&p, format, list);
	va_end(list);

	const char*	t = p;
	throw t;
}

Options::Options(int argc, const char* argv[])
	: fOutputFile("a.out"), fArchitecture(0), fOutputKind(kDynamicExecutable), fBindAtLoad(false),
	  fStripLocalSymbols(false),  fKeepPrivateExterns(false),
	  fInterposable(false), fIgnoreOtherArchFiles(false), fForceSubtypeAll(false), fDeadStrip(kDeadStripOff),
	  fVersionMin(kMinUnset),fNameSpace(kTwoLevelNameSpace),
	  fDylibCompatVersion(0), fDylibCurrentVersion(0), fDylibInstallName(NULL), fEntryName("start"), fBaseAddress(0),
	  fExportMode(kExportDefault), fLibrarySearchMode(kSearchAllDirsForDylibsThenAllDirsForArchives),
	  fUndefinedTreatment(kUndefinedError), fMessagesPrefixedWithArchitecture(false), fPICTreatment(kError),
	  fWeakReferenceMismatchTreatment(kWeakReferenceMismatchError), fMultiplyDefinedDynamic(kWarning),
	  fMultiplyDefinedUnused(kSuppress), fWarnOnMultiplyDefined(false), fClientName(NULL),
	  fUmbrellaName(NULL), fInitFunctionName(NULL), fDotOutputFile(NULL), fExecutablePath(NULL), fBundleLoader(NULL),
	  fZeroPageSize(ULLONG_MAX), fStackSize(0), fStackAddr(0), fExecutableStack(false), fMinimumHeaderPad(0),
	  fCommonsMode(kCommonsIgnoreDylibs), fWarnCommons(false), fVerbose(false), fKeepRelocations(false),
	  fEmitUUID(true),fWarnStabs(false),
	  fTraceDylibSearching(false), fPause(false), fStatistics(false), fPrintOptions(false),
	  fMakeTentativeDefinitionsReal(false)
{
	this->parsePreCommandLineEnvironmentSettings();
	this->parse(argc, argv);
	this->parsePostCommandLineEnvironmentSettings();
	this->reconfigureDefaults();
	this->checkIllegalOptionCombinations();
}

Options::~Options()
{
}

const ObjectFile::ReaderOptions& Options::readerOptions()
{
	return fReaderOptions;
}

cpu_type_t Options::architecture()
{
	return fArchitecture;
}

const char*	Options::getOutputFilePath()
{
	return fOutputFile;
}

std::vector<Options::FileInfo>& Options::getInputFiles()
{
	return fInputFiles;
}

Options::OutputKind	Options::outputKind()
{
	return fOutputKind;
}

bool Options::stripLocalSymbols()
{
	return fStripLocalSymbols;
}

bool Options::bindAtLoad()
{
	return fBindAtLoad;
}

bool Options::prebind()
{
	return fPrebind;
}

bool Options::fullyLoadArchives()
{
	return fReaderOptions.fFullyLoadArchives;
}

Options::NameSpace Options::nameSpace()
{
	return fNameSpace;
}

const char*	Options::installPath()
{
	if ( fDylibInstallName != NULL )
		return fDylibInstallName;
	else
		return fOutputFile;
}

uint32_t Options::currentVersion()
{
	return fDylibCurrentVersion;
}

uint32_t Options::compatibilityVersion()
{
	return fDylibCompatVersion;
}

const char*	Options::entryName()
{
	return fEntryName;
}

uint64_t Options::baseAddress()
{
	return fBaseAddress;
}

bool Options::keepPrivateExterns()
{
	return fKeepPrivateExterns;
}

bool Options::interposable()
{
	return fInterposable;
}

bool Options::ignoreOtherArchInputFiles()
{
	return fIgnoreOtherArchFiles;
}

bool Options::forceCpuSubtypeAll()
{
	return fForceSubtypeAll;
}

bool Options::traceDylibs()
{
	return fReaderOptions.fTraceDylibs;
}

bool Options::traceArchives()
{
	return fReaderOptions.fTraceArchives;
}

Options::UndefinedTreatment Options::undefinedTreatment()
{
	return fUndefinedTreatment;
}

Options::VersionMin Options::macosxVersionMin()
{
	return fVersionMin;
}

Options::WeakReferenceMismatchTreatment	Options::weakReferenceMismatchTreatment()
{
	return fWeakReferenceMismatchTreatment;
}

Options::Treatment Options::multipleDefinitionsInDylibs()
{
	return fMultiplyDefinedDynamic;
}

Options::Treatment Options::overridingDefinitionInDependentDylib()
{
	return fMultiplyDefinedUnused;
}

bool Options::warnOnMultipleDefinitionsInObjectFiles()
{
	return fWarnOnMultiplyDefined;
}

const char* Options::umbrellaName()
{
	return fUmbrellaName;
}

std::vector<const char*>& Options::allowableClients()
{
	return fAllowableClients;
}

const char* Options::clientName()
{
	return fClientName;
}

uint64_t Options::zeroPageSize()
{
	return fZeroPageSize;
}

bool Options::hasCustomStack()
{
	return (fStackSize != 0);
}

uint64_t Options::customStackSize()
{
	return fStackSize;
}

uint64_t Options::customStackAddr()
{
	return fStackAddr;
}

bool Options::hasExecutableStack()
{
	return fExecutableStack;
}

std::vector<const char*>& Options::initialUndefines()
{
	return fInitialUndefines;
}

bool Options::printWhyLive(const char* symbolName)
{
	return ( fWhyLive.find(symbolName) != fWhyLive.end() );
}

std::vector<const char*>& Options::traceSymbols()
{
	return fTraceSymbols;
}

const char*	Options::initFunctionName()
{
	return fInitFunctionName;
}

const char*	Options::dotOutputFile()
{
	return fDotOutputFile;
}

bool Options::hasExportRestrictList()
{
	return (fExportMode != kExportDefault);
}

bool Options::allGlobalsAreDeadStripRoots()
{
	// -exported_symbols_list means globals are not exported by default
	if ( fExportMode == kExportSome ) 
		return false;
	//
	switch ( fOutputKind ) {
		case Options::kDynamicExecutable:
		case Options::kStaticExecutable:
			// by default unused globals in a main executable are stripped
			return false;
		case Options::kDynamicLibrary:
		case Options::kDynamicBundle:
		case Options::kObjectFile:
		case Options::kDyld:
			return true;
	}
	return false;
}

uint32_t Options::minimumHeaderPad()
{
	return fMinimumHeaderPad;
}

std::vector<Options::ExtraSection>&	Options::extraSections()
{
	return fExtraSections;
}

std::vector<Options::SectionAlignment>&	Options::sectionAlignments()
{
	return fSectionAlignments;
}

Options::CommonsMode Options::commonsMode()
{
	return fCommonsMode;
}

bool Options::warnCommons()
{
	return fWarnCommons;
}

bool Options::keepRelocations()
{
	return fKeepRelocations;
}

bool Options::emitUUID()
{
	return fEmitUUID;
}

bool Options::warnStabs()
{
	return fWarnStabs;
}

const char* Options::executablePath()
{
	return fExecutablePath;
}

Options::DeadStripMode Options::deadStrip()
{
	return fDeadStrip;
}

bool Options::shouldExport(const char* symbolName)
{
	switch (fExportMode) {
		case kExportSome:
			return ( fExportSymbols.find(symbolName) != fExportSymbols.end() );
		case kDontExportSome:
			return ( fDontExportSymbols.find(symbolName) == fDontExportSymbols.end() );
		case kExportDefault:
			return true;
	}
	throw "internal error";
}

void Options::parseArch(const char* architecture)
{
	if ( architecture == NULL )
		throw "-arch must be followed by an architecture string";
	if ( strcmp(architecture, "ppc") == 0 )
		fArchitecture = CPU_TYPE_POWERPC;
	else if ( strcmp(architecture, "ppc64") == 0 )
		fArchitecture = CPU_TYPE_POWERPC64;
	else if ( strcmp(architecture, "i386") == 0 )
		fArchitecture = CPU_TYPE_I386;
	else if ( strcmp(architecture, "x86_64") == 0 )
		fArchitecture = CPU_TYPE_X86_64;
	else
		throw "-arch followed by unknown architecture name";
}

bool Options::checkForFile(const char* format, const char* dir, const char* rootName, FileInfo& result)
{
	struct stat statBuffer;
	char possiblePath[strlen(dir)+strlen(rootName)+strlen(format)+8];
	sprintf(possiblePath, format,  dir, rootName);
	bool found = (stat(possiblePath, &statBuffer) == 0);
	if ( fTraceDylibSearching )
		printf("[Logging for XBS]%sfound library: '%s'\n", (found ? " " : " not "), possiblePath);
	if ( found ) {
		result.path = strdup(possiblePath);
		result.fileLen = statBuffer.st_size;
		result.modTime = statBuffer.st_mtime;
		return true;
	}
	return false;
}


Options::FileInfo Options::findLibrary(const char* rootName)
{
	FileInfo result;
	const int rootNameLen = strlen(rootName);
	// if rootName ends in .o there is no .a vs .dylib choice
	if ( (rootNameLen > 3) && (strcmp(&rootName[rootNameLen-2], ".o") == 0) ) {
		for (std::vector<const char*>::iterator it = fLibrarySearchPaths.begin();
			 it != fLibrarySearchPaths.end();
			 it++) {
			const char* dir = *it;
			if ( checkForFile("%s/%s", dir, rootName, result) )
				return result;
		}
	}
	else {
		bool lookForDylibs = ( fOutputKind != Options::kDyld);
		switch ( fLibrarySearchMode ) {
		case kSearchAllDirsForDylibsThenAllDirsForArchives:
				// first look in all directories for just for dylibs
				if ( lookForDylibs ) {
					for (std::vector<const char*>::iterator it = fLibrarySearchPaths.begin();
						 it != fLibrarySearchPaths.end();
						 it++) {
						const char* dir = *it;
						if ( checkForFile("%s/lib%s.dylib", dir, rootName, result) )
							return result;
					}
				}
				// next look in all directories for just for archives
				for (std::vector<const char*>::iterator it = fLibrarySearchPaths.begin();
					 it != fLibrarySearchPaths.end();
					 it++) {
					const char* dir = *it;
					if ( checkForFile("%s/lib%s.a", dir, rootName, result) )
						return result;
				}
				break;

			case kSearchDylibAndArchiveInEachDir:
				// look in each directory for just for a dylib then for an archive
				for (std::vector<const char*>::iterator it = fLibrarySearchPaths.begin();
					 it != fLibrarySearchPaths.end();
					 it++) {
					const char* dir = *it;
					if ( lookForDylibs && checkForFile("%s/lib%s.dylib", dir, rootName, result) )
						return result;
					if ( checkForFile("%s/lib%s.a", dir, rootName, result) )
						return result;
				}
				break;
		}
	}
	throwf("library not found for -l%s", rootName);
}


Options::FileInfo Options::findFramework(const char* rootName)
{
	struct stat statBuffer;
	const int rootNameLen = strlen(rootName);
	for (std::vector<const char*>::iterator it = fFrameworkSearchPaths.begin();
		 it != fFrameworkSearchPaths.end();
		 it++) {
		// ??? Shouldn't we be using String here and just initializing it?
		// ??? Use str.c_str () to pull out the string for the stat call.
		const char* dir = *it;
		char possiblePath[strlen(dir)+2*rootNameLen+20];
		strcpy(possiblePath, dir);
		strcat(possiblePath, "/");
		strcat(possiblePath, rootName);
		strcat(possiblePath, ".framework/");
		strcat(possiblePath, rootName);
		bool found = (stat(possiblePath, &statBuffer) == 0);
		if ( fTraceDylibSearching )
			printf("[Logging for XBS]%sfound framework: '%s'\n",
				   (found ? " " : " not "), possiblePath);
		if ( found ) {
			FileInfo result;
			result.path = strdup(possiblePath);
			result.fileLen = statBuffer.st_size;
			result.modTime = statBuffer.st_mtime;
			return result;
		}
	}
	throwf("framework not found %s", rootName);
}

Options::FileInfo Options::findFile(const char* path)
{
	FileInfo result;
	struct stat statBuffer;

	// if absolute path and not a .o file, the use SDK prefix
	if ( (path[0] == '/') && (strcmp(&path[strlen(path)-2], ".o") != 0) ) {
		const int pathLen = strlen(path);
		for (std::vector<const char*>::iterator it = fSDKPaths.begin(); it != fSDKPaths.end(); it++) {
			// ??? Shouldn't we be using String here?
			const char* sdkPathDir = *it;
			const int sdkPathDirLen = strlen(sdkPathDir);
			char possiblePath[sdkPathDirLen+pathLen+4];
			strcpy(possiblePath, sdkPathDir);
			if ( possiblePath[sdkPathDirLen-1] == '/' )
				possiblePath[sdkPathDirLen-1] = '\0';
			strcat(possiblePath, path);
			if ( stat(possiblePath, &statBuffer) == 0 ) {
				result.path = strdup(possiblePath);
				result.fileLen = statBuffer.st_size;
				result.modTime = statBuffer.st_mtime;
				return result;
			}
		}
	}
	// try raw path
	if ( stat(path, &statBuffer) == 0 ) {
		result.path = strdup(path);
		result.fileLen = statBuffer.st_size;
		result.modTime = statBuffer.st_mtime;
		return result;
	}

	// try @executable_path substitution
	if ( (strncmp(path, "@executable_path/", 17) == 0) && (fExecutablePath != NULL) ) {
		char newPath[strlen(fExecutablePath) + strlen(path)];
		strcpy(newPath, fExecutablePath);
		char* addPoint = strrchr(newPath,'/');
		if ( addPoint != NULL )
			strcpy(&addPoint[1], &path[17]);
		else
			strcpy(newPath, &path[17]);
		if ( stat(newPath, &statBuffer) == 0 ) {
			result.path = strdup(newPath);
			result.fileLen = statBuffer.st_size;
			result.modTime = statBuffer.st_mtime;
			return result;
		}
	}

	// not found
	throwf("file not found: %s", path);
}


void Options::loadFileList(const char* fileOfPaths)
{
	FILE* file;
	const char* comma = strrchr(fileOfPaths, ',');
	const char* prefix = NULL;
	if ( comma != NULL ) {
		prefix = comma+1;
		int realFileOfPathsLen = comma-fileOfPaths;
		char realFileOfPaths[realFileOfPathsLen+1];
		strncpy(realFileOfPaths,fileOfPaths, realFileOfPathsLen);
		realFileOfPaths[realFileOfPathsLen] = '\0';
		file = fopen(realFileOfPaths, "r");
		if ( file == NULL )
			throwf("-filelist file not found: %s\n", realFileOfPaths);
	}
	else {
		file = fopen(fileOfPaths, "r");
		if ( file == NULL )
			throwf("-filelist file not found: %s\n", fileOfPaths);
	}

	char path[PATH_MAX];
	while ( fgets(path, 1024, file) != NULL ) {
		path[PATH_MAX-1] = '\0';
		char* eol = strchr(path, '\n');
		if ( eol != NULL )
			*eol = '\0';
		if ( prefix != NULL ) {
			char builtPath[strlen(prefix)+strlen(path)+2];
			strcpy(builtPath, prefix);
			strcat(builtPath, "/");
			strcat(builtPath, path);
			fInputFiles.push_back(findFile(builtPath));
		}
		else {
			fInputFiles.push_back(findFile(path));
		}
	}
	fclose(file);
}


void Options::loadExportFile(const char* fileOfExports, const char* option, NameSet& set)
{
	// read in whole file
	int fd = ::open(fileOfExports, O_RDONLY, 0);
	if ( fd == -1 )
		throwf("can't open %s file: %s", option, fileOfExports);
	struct stat stat_buf;
	::fstat(fd, &stat_buf);
	char* p = (char*)malloc(stat_buf.st_size);
	if ( p == NULL )
		throwf("can't process %s file: %s", option, fileOfExports);

	if ( read(fd, p, stat_buf.st_size) != stat_buf.st_size )
		throwf("can't read %s file: %s", option, fileOfExports);

	::close(fd);

	// parse into symbols and add to hash_set
	char * const end = &p[stat_buf.st_size];
	enum { lineStart, inSymbol, inComment } state = lineStart;
	char* symbolStart = NULL;
	for (char* s = p; s < end; ++s ) {
		switch ( state ) {
		case lineStart:
			if ( *s =='#' ) {
				state = inComment;
			}
			else if ( !isspace(*s) ) {
				state = inSymbol;
				symbolStart = s;
			}
			break;
		case inSymbol:
			if ( *s == '\n' ) {
				*s = '\0';
				// removing any trailing spaces
				char* last = s-1;
				while ( isspace(*last) ) {
					*last = '\0';
					--last;
				}
				set.insert(symbolStart);
				symbolStart = NULL;
				state = lineStart;
			}
			break;
		case inComment:
			if ( *s == '\n' )
				state = lineStart;
			break;
		}
	}
	if ( state == inSymbol ) {
		fprintf(stderr, "ld64 warning: missing line-end at end of file \"%s\"\n", fileOfExports);
		int len = end-symbolStart+1;
		char* temp = new char[len];
		strlcpy(temp, symbolStart, len);

		// remove any trailing spaces
		char* last = &temp[len-2];
		while ( isspace(*last) ) {
			*last = '\0';
			--last;
		}
		set.insert(temp);
	}

	// Note: we do not free() the malloc buffer, because the strings are used by the export-set hash table
}

void Options::setUndefinedTreatment(const char* treatment)
{
	if ( treatment == NULL )
		throw "-undefined missing [ warning | error | suppress | dynamic_lookup ]";

	if ( strcmp(treatment, "warning") == 0 )
		fUndefinedTreatment = kUndefinedWarning;
	else if ( strcmp(treatment, "error") == 0 )
		fUndefinedTreatment = kUndefinedError;
	else if ( strcmp(treatment, "suppress") == 0 )
		fUndefinedTreatment = kUndefinedSuppress;
	else if ( strcmp(treatment, "dynamic_lookup") == 0 )
		fUndefinedTreatment = kUndefinedDynamicLookup;
	else
		throw "invalid option to -undefined [ warning | error | suppress | dynamic_lookup ]";
}

Options::Treatment Options::parseTreatment(const char* treatment)
{
	if ( treatment == NULL )
		return kNULL;

	if ( strcmp(treatment, "warning") == 0 )
		return kWarning;
	else if ( strcmp(treatment, "error") == 0 )
		return kError;
	else if ( strcmp(treatment, "suppress") == 0 )
		return kSuppress;
	else
		return kInvalid;
}

void Options::setVersionMin(const char* version)
{
	if ( version == NULL )
		throw "-macosx_version_min argument missing";

	if ( (strncmp(version, "10.", 3) == 0) && isdigit(version[3]) ) {
		int num = version[3] - '0';
		switch ( num ) {
			case 0:
			case 1:
				fVersionMin = k10_1;
				break;
			case 2:
				fVersionMin = k10_2;
				break;
			case 3:
				fVersionMin = k10_3;
				break;
			case 4:
				fVersionMin = k10_4;
				break;
			default:
				fVersionMin = k10_5;
				break;
			}
	}
	else {
		fprintf(stderr, "ld64: unknown option to -macosx_version_min not 10.x");
	}
}

void Options::setWeakReferenceMismatchTreatment(const char* treatment)
{
	if ( treatment == NULL )
		throw "-weak_reference_mismatches missing [ error | weak | non-weak ]";

	if ( strcmp(treatment, "error") == 0 )
		fWeakReferenceMismatchTreatment = kWeakReferenceMismatchError;
	else if ( strcmp(treatment, "weak") == 0 )
		fWeakReferenceMismatchTreatment = kWeakReferenceMismatchWeak;
	else if ( strcmp(treatment, "non-weak") == 0 )
		fWeakReferenceMismatchTreatment = kWeakReferenceMismatchNonWeak;
	else
		throw "invalid option to -weak_reference_mismatches [ error | weak | non-weak ]";
}

Options::CommonsMode Options::parseCommonsTreatment(const char* mode)
{
	if ( mode == NULL )
		throw "-commons missing [ ignore_dylibs | use_dylibs | error ]";

	if ( strcmp(mode, "ignore_dylibs") == 0 )
		return kCommonsIgnoreDylibs;
	else if ( strcmp(mode, "use_dylibs") == 0 )
		return kCommonsOverriddenByDylibs;
	else if ( strcmp(mode, "error") == 0 )
		return kCommonsConflictsDylibsError;
	else
		throw "invalid option to -commons [ ignore_dylibs | use_dylibs | error ]";
}

void Options::setDylibInstallNameOverride(const char* paths)
{


}

uint64_t Options::parseAddress(const char* addr)
{
	char* endptr;
	uint64_t result = strtoull(addr, &endptr, 16);
	return result;
}

//
// Parses number of form X[.Y[.Z]] into a uint32_t where the nibbles are xxxx.yy.zz
//
//
uint32_t Options::parseVersionNumber(const char* versionString)
{
	unsigned long x = 0;
	unsigned long y = 0;
	unsigned long z = 0;
	char* end;
	x = strtoul(versionString, &end, 10);
	if ( *end == '.' ) {
		y = strtoul(&end[1], &end, 10);
		if ( *end == '.' ) {
			z = strtoul(&end[1], &end, 10);
		}
	}
	if ( (*end != '\0') || (x > 0xffff) || (y > 0xff) || (z > 0xff) )
		throwf("malformed version number: %s", versionString);

	return (x << 16) | ( y << 8 ) | z;
}

void Options::parseSectionOrderFile(const char* segment, const char* section, const char* path)
{
	fprintf(stderr, "ld64: warning -sectorder not yet supported for 64-bit code\n");
}

void Options::addSection(const char* segment, const char* section, const char* path)
{
	if ( strlen(segment) > 16 )
		throw "-seccreate segment name max 16 chars";
	if ( strlen(section) > 16 ) {
		char* tmp = strdup(section);
		tmp[16] = '\0';
		fprintf(stderr, "ld64 warning: -seccreate section name (%s) truncated to 16 chars (%s)\n", section, tmp);
		section = tmp;
	}

	// read in whole file
	int fd = ::open(path, O_RDONLY, 0);
	if ( fd == -1 )
		throwf("can't open -sectcreate file: %s", path);
	struct stat stat_buf;
	::fstat(fd, &stat_buf);
	char* p = (char*)malloc(stat_buf.st_size);
	if ( p == NULL )
		throwf("can't process -sectcreate file: %s", path);
	if ( read(fd, p, stat_buf.st_size) != stat_buf.st_size )
		throwf("can't read -sectcreate file: %s", path);
	::close(fd);

	// record section to create
	ExtraSection info = { segment, section, path, (uint8_t*)p, stat_buf.st_size };
	fExtraSections.push_back(info);
}

void Options::addSectionAlignment(const char* segment, const char* section, const char* alignmentStr)
{
	if ( strlen(segment) > 16 )
		throw "-sectalign segment name max 16 chars";
	if ( strlen(section) > 16 )
		throw "-sectalign section name max 16 chars";

	// argument to -sectalign is a hexadecimal number
	char* endptr;
	unsigned long value = strtoul(alignmentStr, &endptr, 16);
	if ( *endptr != '\0')
		throw "argument for -sectalign is not a hexadecimal number";
	if ( value > 0x8000 )
		throw "argument for -sectalign must be less than or equal to 0x8000";
	if ( value == 0 ) {
		fprintf(stderr, "ld64 warning: zero is not a valid -sectalign\n");
		value = 1;
	}
	
	// alignment is power of 2 (e.g. page alignment = 12)
	uint8_t alignment = (uint8_t)log2(value);
	
	if ( (unsigned long)(1 << alignment) != value ) {
		fprintf(stderr, "ld64 warning: alignment for -sectalign %s %s is not a power of two, using 0x%X\n", 
			segment, section, 1 << alignment);
	}

	SectionAlignment info = { segment, section, alignment };
	fSectionAlignments.push_back(info);
}

//
// Process all command line arguments.
//
// The only error checking done here is that each option is valid and if it has arguments
// that they too are valid.
//
// The general rule is "last option wins", i.e. if both -bundle and -dylib are specified,
// whichever was last on the command line is used.
//
// Error check for invalid combinations of options is done in checkIllegalOptionCombinations()
//
void Options::parse(int argc, const char* argv[])
{
	// pass one builds search list from -L and -F options
	this->buildSearchPaths(argc, argv);

	// reduce re-allocations
	fInputFiles.reserve(32);

	// pass two parse all other options
	for(int i=1; i < argc; ++i) {
		const char* arg = argv[i];

		if ( arg[0] == '-' ) {

			// Since we don't care about the files passed, just the option names, we do this here.
			if (fPrintOptions)
				fprintf (stderr, "[Logging ld64 options]\t%s\n", arg);

			if ( (arg[1] == 'L') || (arg[1] == 'F') ) {
				// previously handled by buildSearchPaths()
			}
			else if ( strcmp(arg, "-arch") == 0 ) {
				parseArch(argv[++i]);
			}
			else if ( strcmp(arg, "-dynamic") == 0 ) {
				// default
			}
			else if ( strcmp(arg, "-static") == 0 ) {
				fOutputKind = kStaticExecutable;
			}
			else if ( strcmp(arg, "-dylib") == 0 ) {
				fOutputKind = kDynamicLibrary;
			}
			else if ( strcmp(arg, "-bundle") == 0 ) {
				fOutputKind = kDynamicBundle;
			}
			else if ( strcmp(arg, "-dylinker") == 0 ) {
				fOutputKind = kDyld;
			}
			else if ( strcmp(arg, "-execute") == 0 ) {
				if ( fOutputKind != kStaticExecutable )
					fOutputKind = kDynamicExecutable;
			}
			else if ( strcmp(arg, "-r") == 0 ) {
				fOutputKind = kObjectFile;
			}
			else if ( strcmp(arg, "-o") == 0 ) {
				fOutputFile = argv[++i];
			}
			else if ( arg[1] == 'l' ) {
				fInputFiles.push_back(findLibrary(&arg[2]));
			}
			// This causes a dylib to be weakly bound at
			// link time.  This corresponds to weak_import.
			else if ( strncmp(arg, "-weak-l", 7) == 0 ) {
				FileInfo info = findLibrary(&arg[7]);
				info.options.fWeakImport = true;
				fInputFiles.push_back(info);
			}
			// Avoid lazy binding.
			// ??? Deprecate.
			else if ( strcmp(arg, "-bind_at_load") == 0 ) {
				fBindAtLoad = true;
			}
			else if ( strcmp(arg, "-twolevel_namespace") == 0 ) {
				fNameSpace = kTwoLevelNameSpace;
			}
			else if ( strcmp(arg, "-flat_namespace") == 0 ) {
				fNameSpace = kFlatNameSpace;
			}
			// Also sets a bit to ensure dyld causes everything
			// in the namespace to be flat.
			// ??? Deprecate
			else if ( strcmp(arg, "-force_flat_namespace") == 0 ) {
				fNameSpace = kForceFlatNameSpace;
			}
			// Similar to --whole-archive.
			else if ( strcmp(arg, "-all_load") == 0 ) {
				fReaderOptions.fFullyLoadArchives = true;
			}
			// Similar to --whole-archive, but for all ObjC classes.
			else if ( strcmp(arg, "-ObjC") == 0 ) {
				fReaderOptions.fLoadObjcClassesInArchives = true;
			}
			// Library versioning.
			else if ( strcmp(arg, "-dylib_compatibility_version") == 0 ) {
				fDylibCompatVersion = parseVersionNumber(argv[++i]);
			}
			else if ( strcmp(arg, "-dylib_current_version") == 0 ) {
				fDylibCurrentVersion = parseVersionNumber(argv[++i]);
			}
			else if ( strcmp(arg, "-sectorder") == 0 ) {
				parseSectionOrderFile(argv[i+1], argv[i+2], argv[i+3]);
				i += 3;
			}
			// ??? Deprecate segcreate.
			// -sectcreate puts whole files into a section in the output.
			else if ( (strcmp(arg, "-sectcreate") == 0) || (strcmp(arg, "-segcreate") == 0) ) {
				addSection(argv[i+1], argv[i+2], argv[i+3]);
				i += 3;
			}
			// Since we have a full path in binary/library names we need to be able to override it.
			else if ( (strcmp(arg, "-dylib_install_name") == 0) || (strcmp(arg, "-dylinker_install_name") == 0) ) {
				fDylibInstallName = argv[++i];
			}
			// Sets the base address of the output.
			else if ( (strcmp(arg, "-seg1addr") == 0) || (strcmp(arg, "-image_base") == 0) ) {
				fBaseAddress = parseAddress(argv[++i]);
			}
			else if ( strcmp(arg, "-e") == 0 ) {
				fEntryName = argv[++i];
			}
			// Same as -@ from the FSF linker.
			else if ( strcmp(arg, "-filelist") == 0 ) {
				 loadFileList(argv[++i]);
			}
			else if ( strcmp(arg, "-keep_private_externs") == 0 ) {
				 fKeepPrivateExterns = true;
			}
			// ??? Deprecate
			else if ( strcmp(arg, "-final_output") == 0 ) {
				 ++i;
				// ignore for now
			}
			// Ensure that all calls to exported symbols go through lazy pointers.  Multi-module
			// just ensures that this happens for cross object file boundaries.
			else if ( (strcmp(arg, "-interposable") == 0) || (strcmp(arg, "-multi_module") == 0)) {
				 fInterposable = true;
			}
			// Default for -interposable/-multi_module/-single_module.
			else if ( strcmp(arg, "-single_module") == 0 ) {
				 fInterposable = false;
			}
			else if ( strcmp(arg, "-exported_symbols_list") == 0 ) {
				if ( fExportMode == kDontExportSome )
					throw "can't use -exported_symbols_list and -unexported_symbols_list";
				fExportMode = kExportSome;
				loadExportFile(argv[++i], "-exported_symbols_list", fExportSymbols);
			}
			else if ( strcmp(arg, "-unexported_symbols_list") == 0 ) {
				if ( fExportMode == kExportSome )
					throw "can't use -exported_symbols_list and -unexported_symbols_list";
				fExportMode = kDontExportSome;
				loadExportFile(argv[++i], "-unexported_symbols_list", fDontExportSymbols);
			}
			// ??? Deprecate
			else if ( strcmp(arg, "-no_arch_warnings") == 0 ) {
				 fIgnoreOtherArchFiles = true;
			}
			else if ( strcmp(arg, "-force_cpusubtype_ALL") == 0 ) {
				 fForceSubtypeAll = true;
			}
			// Similar to -weak-l but uses the absolute path name to the library.
			else if ( strcmp(arg, "-weak_library") == 0 ) {
				FileInfo info = findFile(argv[++i]);
				info.options.fWeakImport = true;
				fInputFiles.push_back(info);
			}
			else if ( strcmp(arg, "-framework") == 0 ) {
				fInputFiles.push_back(findFramework(argv[++i]));
			}
			else if ( strcmp(arg, "-weak_framework") == 0 ) {
				FileInfo info = findFramework(argv[++i]);
				info.options.fWeakImport = true;
				fInputFiles.push_back(info);
			}
			// ??? Deprecate when we get -Bstatic/-Bdynamic.
			else if ( strcmp(arg, "-search_paths_first") == 0 ) {
				fLibrarySearchMode = kSearchDylibAndArchiveInEachDir;
			}
			else if ( strcmp(arg, "-undefined") == 0 ) {
				 setUndefinedTreatment(argv[++i]);
			}
			// Debugging output flag.
			else if ( strcmp(arg, "-arch_multiple") == 0 ) {
				 fMessagesPrefixedWithArchitecture = true;
			}
			// Specify what to do with relocations in read only
			// sections like .text.  Could be errors, warnings,
			// or suppressed.  Currently we do nothing with the
			// flag.
			else if ( strcmp(arg, "-read_only_relocs") == 0 ) {
				Treatment temp = parseTreatment(argv[++i]);

				if ( temp == kNULL )
					throw "-read_only_relocs missing [ warning | error | suppress ]";
				else if ( temp == kInvalid )
					throw "invalid option to -read_only_relocs [ warning | error | suppress ]";
			}
			// Specifies whether or not there are intra section
			// relocations and what to do when found. Could be
			// errors, warnings, or suppressed.
			else if ( strcmp(arg, "-sect_diff_relocs") == 0 ) {
				fPICTreatment = parseTreatment(argv[++i]);

				if ( fPICTreatment == kNULL )
					throw "-sect_diff_relocs missing [ warning | error | suppress ]";
				else if ( fPICTreatment == kInvalid )
					throw "invalid option to -sect_diff_relocs [ warning | error | suppress ]";
			}
			// Warn, error or make strong a mismatch between weak
			// and non-weak references.
			else if ( strcmp(arg, "-weak_reference_mismatches") == 0 ) {
				 setWeakReferenceMismatchTreatment(argv[++i]);
			}
			// For a deployment target of 10.3 and earlier ld64 will
			// prebind an executable with 0s in all addresses that
			// are prebound.  This can then be fixed up by update_prebinding
			// later.  Prebinding is less useful on 10.4 and greater.
			else if ( strcmp(arg, "-prebind") == 0 ) {
				fPrebind = true;
			}
			else if ( strcmp(arg, "-noprebind") == 0 ) {
				fPrebind = false;
			}
			// ??? Deprecate
			else if ( strcmp(arg, "-prebind_allow_overlap") == 0 ) {
				  // Do not handle and suppress warnings always.
			}
			// ??? Deprecate
			else if ( strcmp(arg, "-prebind_all_twolevel_modules") == 0 ) {
				  // Ignore.
			}
			// ??? Deprecate
			else if ( strcmp(arg, "-noprebind_all_twolevel_modules") == 0 ) {
				  // Ignore.
			}
			// Sets a bit in the main executable only that causes fix_prebinding
			// not to run.  This is always set.
			else if ( strcmp(arg, "-nofixprebinding") == 0 ) {
				  // Ignore.
			}
			// This should probably be deprecated when we respect -L and -F
			// when searching for libraries.
			else if ( strcmp(arg, "-dylib_file") == 0 ) {
				 setDylibInstallNameOverride(argv[++i]);
			}
			// Allows us to rewrite full paths to be relocatable based on
			// the path name of the executable.
			else if ( strcmp(arg, "-executable_path") == 0 ) {
				 fExecutablePath = argv[++i];
				 if ( (fExecutablePath == NULL) || (fExecutablePath[0] == '-') )
					throw "-executable_path missing <path>";
			}
			// ??? Deprecate
			// Aligns all segments to the power of 2 boundary specified.
			else if ( strcmp(arg, "-segalign") == 0 ) {
				// Ignore.
				++i;
			}
			// Puts a specified segment at a particular address that must
			// be a multiple of the segment alignment.
			else if ( strcmp(arg, "-segaddr") == 0 ) {
				// FIX FIX
				i += 2;
			}
			// ??? Deprecate when we deprecate split-seg.
			else if ( strcmp(arg, "-segs_read_only_addr") == 0 ) {
				// Ignore.
				++i;
			}
			// ??? Deprecate when we deprecate split-seg.
			else if ( strcmp(arg, "-segs_read_write_addr") == 0 ) {
				// Ignore.
				++i;
			}
			// ??? Deprecate when we get rid of basing at build time.
			else if ( strcmp(arg, "-seg_addr_table") == 0 ) {
				// Ignore.
				++i;
			}
			// ??? Deprecate.
			else if ( strcmp(arg, "-seg_addr_table_filename") == 0 ) {
				// Ignore.
				++i;
			}
			else if ( strcmp(arg, "-segprot") == 0 ) {
				// FIX FIX
				i += 3;
			}
			else if ( strcmp(arg, "-pagezero_size") == 0 ) {
				fZeroPageSize = parseAddress(argv[++i]);
				uint64_t temp = fZeroPageSize & (-4096); // page align
				if ( fZeroPageSize != temp )
					fprintf(stderr, "ld64: warning, -pagezero_size not page aligned, rounding down\n");
				 fZeroPageSize = temp;
			}
			else if ( strcmp(arg, "-stack_addr") == 0 ) {
				fStackAddr = parseAddress(argv[++i]);
			}
			else if ( strcmp(arg, "-stack_size") == 0 ) {
				fStackSize = parseAddress(argv[++i]);
			}
			else if ( strcmp(arg, "-allow_stack_execute") == 0 ) {
				fExecutableStack = true;
			}
			else if ( strcmp(arg, "-sectalign") == 0 ) {
				addSectionAlignment(argv[i+1], argv[i+2], argv[i+3]);
				i += 3;
			}
			else if ( strcmp(arg, "-sectorder_detail") == 0 ) {
				 // FIX FIX
			}
			else if ( strcmp(arg, "-sectobjectsymbols") == 0 ) {
				// FIX FIX
				i += 2;
			}
			else if ( strcmp(arg, "-bundle_loader") == 0 ) {
				fBundleLoader = argv[++i];
				if ( (fBundleLoader == NULL) || (fBundleLoader[0] == '-') )
					throw "-bundle_loader missing <path>";
				FileInfo info = findFile(fBundleLoader);
				info.options.fBundleLoader = true;
				fInputFiles.push_back(info);
			}
			else if ( strcmp(arg, "-private_bundle") == 0 ) {
				// FIX FIX
			}
			else if ( strcmp(arg, "-twolevel_namespace_hints") == 0 ) {
				// FIX FIX
			}
			// Use this flag to set default behavior for deployement targets.
			else if ( strcmp(arg, "-macosx_version_min") == 0 ) {
				setVersionMin(argv[++i]);
			}
			// This option (unlike -m below) only affects how we warn
			// on multiple definitions inside dynamic libraries.
			else if ( strcmp(arg, "-multiply_defined") == 0 ) {
				fMultiplyDefinedDynamic = parseTreatment(argv[++i]);

				if ( fMultiplyDefinedDynamic == kNULL )
					throw "-multiply_defined missing [ warning | error | suppress ]";
				else if ( fMultiplyDefinedDynamic == kInvalid )
					throw "invalid option to -multiply_defined [ warning | error | suppress ]";
			}
			else if ( strcmp(arg, "-multiply_defined_unused") == 0 ) {
				fMultiplyDefinedUnused = parseTreatment(argv[++i]);

				if ( fMultiplyDefinedUnused == kNULL )
					throw "-multiply_defined_unused missing [ warning | error | suppress ]";
				else if ( fMultiplyDefinedUnused == kInvalid )
					throw "invalid option to -multiply_defined_unused [ warning | error | suppress ]";
			}
			else if ( strcmp(arg, "-nomultidefs") == 0 ) {
				 // FIX FIX
			}
			// Display each file in which the argument symbol appears and whether
			// the file defines or references it.  This option takes an argument
			// as -y<symbol> note that there is no space.
			else if ( strncmp(arg, "-y", 2) == 0 ) {
				const char* name = &arg[2];

				if ( name == NULL )
					throw "-y missing argument";

				fTraceSymbols.push_back(name);
			}
			// Same output as -y, but output <arg> number of undefined symbols only.
			else if ( strcmp(arg, "-Y") == 0 ) {
				char* endptr;
				fLimitUndefinedSymbols = strtoul (argv[++i], &endptr, 10);

				if(*endptr != '\0')
					throw "invalid argument for -Y [decimal number]";
			}
			// This option affects all objects linked into the final result.
			else if ( strcmp(arg, "-m") == 0 ) {
				fWarnOnMultiplyDefined = true;
			}
			else if ( (strcmp(arg, "-why_load") == 0) || (strcmp(arg, "-whyload") == 0) ) {
				 fReaderOptions.fWhyLoad = true;
			}
			else if ( strcmp(arg, "-why_live") == 0 ) {
				 const char* name = argv[++i];
				if ( name == NULL )
					throw "-why_live missing symbol name argument";
				fWhyLive.insert(name);
			}
			else if ( strcmp(arg, "-u") == 0 ) {
				const char* name = argv[++i];
				if ( name == NULL )
					throw "-u missing argument";
				fInitialUndefines.push_back(name);
			}
			else if ( strcmp(arg, "-U") == 0 ) {
				 // FIX FIX
				 ++i;
			}
			else if ( strcmp(arg, "-s") == 0 ) {
				fStripLocalSymbols = true;
				fReaderOptions.fDebugInfoStripping = ObjectFile::ReaderOptions::kDebugInfoNone;
			}
			else if ( strcmp(arg, "-x") == 0 ) {
				fStripLocalSymbols = true;
			}
			else if ( strcmp(arg, "-S") == 0 ) {
				fReaderOptions.fDebugInfoStripping = ObjectFile::ReaderOptions::kDebugInfoNone;
			}
			else if ( strcmp(arg, "-X") == 0 ) {
				// FIX FIX
			}
			else if ( strcmp(arg, "-Si") == 0 ) {
				fReaderOptions.fDebugInfoStripping = ObjectFile::ReaderOptions::kDebugInfoFull;
			}
			else if ( strcmp(arg, "-b") == 0 ) {
				// FIX FIX
			}
			else if ( strcmp(arg, "-Sn") == 0 ) {
				fReaderOptions.fDebugInfoStripping = ObjectFile::ReaderOptions::kDebugInfoFull;
			}
			else if ( strcmp(arg, "-Sp") == 0 ) {
				fReaderOptions.fDebugInfoStripping = ObjectFile::ReaderOptions::kDebugInfoMinimal;
			}
			else if ( strcmp(arg, "-dead_strip") == 0 ) {
				fDeadStrip = kDeadStripOnPlusUnusedInits;
			}
			else if ( strcmp(arg, "-no_dead_strip_inits_and_terms") == 0 ) {
				fDeadStrip = kDeadStripOn;
			}
			else if ( strcmp(arg, "-w") == 0 ) {
				// FIX FIX
			}
			else if ( strcmp(arg, "-arch_errors_fatal") == 0 ) {
				// FIX FIX
			}
			else if ( strcmp(arg, "-M") == 0 ) {
				// FIX FIX
			}
			else if ( strcmp(arg, "-whatsloaded") == 0 ) {
				// FIX FIX
			}
			else if ( strcmp(arg, "-headerpad") == 0 ) {
				const char* size = argv[++i];
				if ( size == NULL )
					throw "-headerpad missing argument";
				 fMinimumHeaderPad = parseAddress(size);
			}
			else if ( strcmp(arg, "-headerpad_max_install_names") == 0 ) {
				// FIX FIX
			}
			else if ( strcmp(arg, "-t") == 0 ) {
				// FIX FIX
			}
			else if ( strcmp(arg, "-A") == 0 ) {
				 // FIX FIX
				 ++i;
			}
			else if ( strcmp(arg, "-umbrella") == 0 ) {
				const char* name = argv[++i];
				if ( name == NULL )
					throw "-umbrella missing argument";
				fUmbrellaName = name;
			}
			else if ( strcmp(arg, "-allowable_client") == 0 ) {
				const char* name = argv[++i];

				if ( name == NULL )
					throw "-allowable_client missing argument";

				fAllowableClients.push_back(name);
			}
			else if ( strcmp(arg, "-client_name") == 0 ) {
				const char* name = argv[++i];

				if ( name == NULL )
					throw "-client_name missing argument";

				fClientName = name;
			}
			else if ( strcmp(arg, "-sub_umbrella") == 0 ) {
				const char* name = argv[++i];
				if ( name == NULL )
					throw "-sub_umbrella missing argument";
				 fSubUmbellas.push_back(name);
			}
			else if ( strcmp(arg, "-sub_library") == 0 ) {
				const char* name = argv[++i];
				if ( name == NULL )
					throw "-sub_library missing argument";
				 fSubLibraries.push_back(name);
			}
			else if ( strcmp(arg, "-init") == 0 ) {
				const char* name = argv[++i];
				if ( name == NULL )
					throw "-init missing argument";
				fInitFunctionName = name;
			}
			else if ( strcmp(arg, "-dot") == 0 ) {
				const char* name = argv[++i];
				if ( name == NULL )
					throw "-dot missing argument";
				fDotOutputFile = name;
			}
			else if ( strcmp(arg, "-warn_commons") == 0 ) {
				fWarnCommons = true;
			}
			else if ( strcmp(arg, "-commons") == 0 ) {
				fCommonsMode = parseCommonsTreatment(argv[++i]);
			}
			else if ( strcmp(arg, "-keep_relocs") == 0 ) {
				fKeepRelocations = true;
			}
			else if ( strcmp(arg, "-warn_stabs") == 0 ) {
				fWarnStabs = true;
			}
			else if ( strcmp(arg, "-pause") == 0 ) {
				fPause = true;
			}
			else if ( strcmp(arg, "-print_statistics") == 0 ) {
				fStatistics = true;
			}
			else if ( strcmp(arg, "-d") == 0 ) {
				fMakeTentativeDefinitionsReal = true;
			}
			else if ( strcmp(arg, "-v") == 0 ) {
				// previously handled by buildSearchPaths()
			}
			else if ( strcmp(arg, "-Z") == 0 ) {
				// previously handled by buildSearchPaths()
			}
			else if ( strcmp(arg, "-syslibroot") == 0 ) {
				++i;
				// previously handled by buildSearchPaths()
			}
			else if ( strcmp(arg, "-no_uuid") == 0 ) {
				fEmitUUID = false;
			}
			// put this last so that it does not interfer with other options starting with 'i'
			else if ( strncmp(arg, "-i", 2) == 0 ) {
				fprintf(stderr, "ld64: -i option (indirect symbols) not supported\n");
			}
			else {
				throwf("unknown option: %s", arg);
			}
		}
		else {
			fInputFiles.push_back(findFile(arg));
		}
	}
}



//
// -syslibroot <path> is used for SDK support.
// The rule is that all search paths (both explicit and default) are
// checked to see if they exist in the SDK.  If so, that path is
// replaced with the sdk prefixed path.  If not, that search path
// is used as is.  If multiple -syslibroot options are specified
// their directory structures are logically overlayed and files
// from sdks specified earlier on the command line used before later ones.

void Options::buildSearchPaths(int argc, const char* argv[])
{
	bool addStandardLibraryDirectories = true;
	std::vector<const char*> libraryPaths;
	std::vector<const char*> frameworkPaths;
	libraryPaths.reserve(10);
	frameworkPaths.reserve(10);
	// scan through argv looking for -L, -F, -Z, and -syslibroot options
	for(int i=0; i < argc; ++i) {
		if ( (argv[i][0] == '-') && (argv[i][1] == 'L') )
			libraryPaths.push_back(&argv[i][2]);
		else if ( (argv[i][0] == '-') && (argv[i][1] == 'F') )
			frameworkPaths.push_back(&argv[i][2]);
		else if ( strcmp(argv[i], "-Z") == 0 )
			addStandardLibraryDirectories = false;
		else if ( strcmp(argv[i], "-v") == 0 ) {
			fVerbose = true;
			extern const char ld64VersionString[];
			fprintf(stderr, "%s", ld64VersionString);
			 // if only -v specified, exit cleanly
			 if ( argc == 2 )
				exit(0);
		}
		else if ( strcmp(argv[i], "-syslibroot") == 0 ) {
			const char* path = argv[++i];
			if ( path == NULL )
				throw "-syslibroot missing argument";
			fSDKPaths.push_back(path);
		}
	}
	if ( addStandardLibraryDirectories ) {
		libraryPaths.push_back("/usr/lib");
		libraryPaths.push_back("/usr/local/lib");

		frameworkPaths.push_back("/Library/Frameworks/");
		frameworkPaths.push_back("/Network/Library/Frameworks/");
		frameworkPaths.push_back("/System/Library/Frameworks/");
	}

	// now merge sdk and library paths to make real search paths
	fLibrarySearchPaths.reserve(libraryPaths.size()*(fSDKPaths.size()+1));
	for (std::vector<const char*>::iterator it = libraryPaths.begin(); it != libraryPaths.end(); it++) {
		const char* libDir = *it;
		bool sdkOverride = false;
		if ( libDir[0] == '/' ) {
			char betterLibDir[PATH_MAX];
			if ( strstr(libDir, "/..") != NULL ) {
				if ( realpath(libDir, betterLibDir) != NULL )
					libDir = strdup(betterLibDir);
			}
			const int libDirLen = strlen(libDir);
			for (std::vector<const char*>::iterator sdkit = fSDKPaths.begin(); sdkit != fSDKPaths.end(); sdkit++) {
				// ??? Should be using string here.
				const char* sdkDir = *sdkit;
				const int sdkDirLen = strlen(sdkDir);
				char newPath[libDirLen + sdkDirLen+4];
				strcpy(newPath, sdkDir);
				if ( newPath[sdkDirLen-1] == '/' )
					newPath[sdkDirLen-1] = '\0';
				strcat(newPath, libDir);
				struct stat statBuffer;
				if ( stat(newPath, &statBuffer) == 0 ) {
					fLibrarySearchPaths.push_back(strdup(newPath));
					sdkOverride = true;
				}
			}
		}
		if ( !sdkOverride )
			fLibrarySearchPaths.push_back(libDir);
	}

	// now merge sdk and framework paths to make real search paths
	fFrameworkSearchPaths.reserve(frameworkPaths.size()*(fSDKPaths.size()+1));
	for (std::vector<const char*>::iterator it = frameworkPaths.begin(); it != frameworkPaths.end(); it++) {
		const char* frameworkDir = *it;
		bool sdkOverride = false;
		if ( frameworkDir[0] == '/' ) {
			char betterFrameworkDir[PATH_MAX];
			if ( strstr(frameworkDir, "/..") != NULL ) {
				if ( realpath(frameworkDir, betterFrameworkDir) != NULL )
					frameworkDir = strdup(betterFrameworkDir);
			}
			const int frameworkDirLen = strlen(frameworkDir);
			for (std::vector<const char*>::iterator sdkit = fSDKPaths.begin(); sdkit != fSDKPaths.end(); sdkit++) {
				// ??? Should be using string here
				const char* sdkDir = *sdkit;
				const int sdkDirLen = strlen(sdkDir);
				char newPath[frameworkDirLen + sdkDirLen+4];
				strcpy(newPath, sdkDir);
				if ( newPath[sdkDirLen-1] == '/' )
					newPath[sdkDirLen-1] = '\0';
				strcat(newPath, frameworkDir);
				struct stat statBuffer;
				if ( stat(newPath, &statBuffer) == 0 ) {
					fFrameworkSearchPaths.push_back(strdup(newPath));
					sdkOverride = true;
				}
			}
		}
		if ( !sdkOverride )
			fFrameworkSearchPaths.push_back(frameworkDir);
	}

	if ( fVerbose ) {
		fprintf(stderr,"Library search paths:\n");
		for (std::vector<const char*>::iterator it = fLibrarySearchPaths.begin();
			 it != fLibrarySearchPaths.end();
			 it++)
			fprintf(stderr,"\t%s\n", *it);
		fprintf(stderr,"Framework search paths:\n");
		for (std::vector<const char*>::iterator it = fFrameworkSearchPaths.begin();
			 it != fFrameworkSearchPaths.end();
			 it++)
			fprintf(stderr,"\t%s\n", *it);
	}
}

// this is run before the command line is parsed
void Options::parsePreCommandLineEnvironmentSettings()
{
	if ((getenv("LD_TRACE_ARCHIVES") != NULL)
		|| (getenv("RC_TRACE_ARCHIVES") != NULL))
	    fReaderOptions.fTraceArchives = true;

	if ((getenv("LD_TRACE_DYLIBS") != NULL)
		|| (getenv("RC_TRACE_DYLIBS") != NULL)) {
	    fReaderOptions.fTraceDylibs = true;
		fReaderOptions.fTraceIndirectDylibs = true;
	}

	if (getenv("RC_TRACE_DYLIB_SEARCHING") != NULL) {
	    fTraceDylibSearching = true;
	}

	if (getenv("LD_PRINT_OPTIONS") != NULL)
		fPrintOptions = true;

	if (fReaderOptions.fTraceDylibs || fReaderOptions.fTraceArchives)
		fReaderOptions.fTraceOutputFile = getenv("LD_TRACE_FILE");
}

// this is run after the command line is parsed
void Options::parsePostCommandLineEnvironmentSettings()
{
	// when building a dynamic main executable, default any use of @executable_path to output path
	if ( fExecutablePath == NULL && (fOutputKind == kDynamicExecutable) ) {
		fExecutablePath = fOutputFile;
	}
	
}

void Options::reconfigureDefaults()
{
	// sync reader options
	switch ( fOutputKind ) {
		case Options::kObjectFile:
			fReaderOptions.fForFinalLinkedImage = false;
			break;
		case Options::kDynamicExecutable:
		case Options::kStaticExecutable:
		case Options::kDynamicLibrary:
		case Options::kDynamicBundle:
		case Options::kDyld:
			fReaderOptions.fForFinalLinkedImage = true;
			break;
	}

	// set default min OS version
	if ( fVersionMin == kMinUnset ) {
		switch ( fArchitecture ) {
			case CPU_TYPE_POWERPC:
				fVersionMin = k10_2;
				break;
			case CPU_TYPE_I386:
			case CPU_TYPE_POWERPC64:
			case CPU_TYPE_X86_64:
				fVersionMin = k10_4;
			default:
				// architecture not specified
				fVersionMin = k10_4;
				break;
		}
	}

	// adjust min based on architecture
	switch ( fArchitecture ) {
		case CPU_TYPE_I386:
			if ( fVersionMin < k10_4 ) {
				//fprintf(stderr, "ld64 warning: -macosx_version_min should be 10.4 or later for i386\n");
				fVersionMin = k10_4;
			}
			break;
		case CPU_TYPE_POWERPC64:
			if ( fVersionMin < k10_4 ) {
				//fprintf(stderr, "ld64 warning: -macosx_version_min should be 10.4 or later for ppc64\n");
				fVersionMin = k10_4;
			}
			break;
		case CPU_TYPE_X86_64:
			if ( fVersionMin < k10_4 ) {
				//fprintf(stderr, "ld64 warning: -macosx_version_min should be 10.4 or later for x86_64\n");
				fVersionMin = k10_4;
			}
			break;
	}
	
}

void Options::checkIllegalOptionCombinations()
{
	// check -undefined setting
	switch ( fUndefinedTreatment ) {
		case kUndefinedError:
		case kUndefinedDynamicLookup:
			// always legal
			break;
		case kUndefinedWarning:
		case kUndefinedSuppress:
			// requires flat namespace
			if ( fNameSpace == kTwoLevelNameSpace )
				throw "can't use -undefined warning or suppress with -twolevel_namespace";
			break;
	}

	// unify -sub_umbrella with dylibs
	for (std::vector<const char*>::iterator it = fSubUmbellas.begin(); it != fSubUmbellas.end(); it++) {
		const char* subUmbrella = *it;
		bool found = false;
		for (std::vector<Options::FileInfo>::iterator fit = fInputFiles.begin(); fit != fInputFiles.end(); fit++) {
			Options::FileInfo& info = *fit;
			const char* lastSlash = strrchr(info.path, '/');
			if ( lastSlash == NULL )
				lastSlash = info.path - 1;
			if ( strcmp(&lastSlash[1], subUmbrella) == 0 ) {
				info.options.fReExport = true;
				found = true;
				break;
			}
		}
		if ( ! found )
			fprintf(stderr, "ld64 warning: -sub_umbrella %s does not match a supplied dylib\n", subUmbrella);
	}

	// unify -sub_library with dylibs
	for (std::vector<const char*>::iterator it = fSubLibraries.begin(); it != fSubLibraries.end(); it++) {
		const char* subLibrary = *it;
		bool found = false;
		for (std::vector<Options::FileInfo>::iterator fit = fInputFiles.begin(); fit != fInputFiles.end(); fit++) {
			Options::FileInfo& info = *fit;
			const char* lastSlash = strrchr(info.path, '/');
			if ( lastSlash == NULL )
				lastSlash = info.path - 1;
			const char* dot = strchr(&lastSlash[1], '.');
			if ( dot == NULL )
				dot = &lastSlash[strlen(lastSlash)];
			if ( strncmp(&lastSlash[1], subLibrary, dot-lastSlash-1) == 0 ) {
				info.options.fReExport = true;
				found = true;
				break;
			}
		}
		if ( ! found )
			fprintf(stderr, "ld64 warning: -sub_library %s does not match a supplied dylib\n", subLibrary);
	}

	// sync reader options
	if ( fNameSpace != kTwoLevelNameSpace )
		fReaderOptions.fFlatNamespace = true;

	// check -stack_addr
	if ( fStackAddr != 0 ) {
		switch (fArchitecture) {
			case CPU_TYPE_I386:
			case CPU_TYPE_POWERPC:
				if ( fStackAddr > 0xFFFFFFFF )
					throw "-stack_addr must be < 4G for 32-bit processes";
				break;
			case CPU_TYPE_POWERPC64:
			case CPU_TYPE_X86_64:
				break;
		}
		if ( (fStackAddr & -4096) != fStackAddr )
			throw "-stack_addr must be multiples of 4K";
		if ( fStackSize  == 0 )
			throw "-stack_addr must be used with -stack_size";
	}

	// check -stack_size
	if ( fStackSize != 0 ) {
		switch (fArchitecture) {
			case CPU_TYPE_I386:
			case CPU_TYPE_POWERPC:
				if ( fStackSize > 0xFFFFFFFF )
					throw "-stack_size must be < 4G for 32-bit processes";
				if ( fStackAddr == 0 ) {
					fStackAddr = 0xC0000000;
				}
				if ( (fStackAddr > 0xB0000000) && ((fStackAddr-fStackSize) < 0xB0000000) )
					fprintf(stderr, "ld64 warning: custom stack placement overlaps and will disable shared region\n");
				break;
			case CPU_TYPE_POWERPC64:
			case CPU_TYPE_X86_64:
				if ( fStackAddr == 0 ) {
					fStackAddr = 0x0007FFFF00000000LL;
				}
				break;
		}
		if ( (fStackSize & -4096) != fStackSize )
			throw "-stack_size must be multiples of 4K";
		switch ( fOutputKind ) {
			case Options::kDynamicExecutable:
			case Options::kStaticExecutable:
				// custom stack size only legal when building main executable
				break;
			case Options::kDynamicLibrary:
			case Options::kDynamicBundle:
			case Options::kObjectFile:
			case Options::kDyld:
				throw "-stack_size option can only be used when linking a main executable";
		}
	}

	// check that -allow_stack_execute is only used with main executables
	if ( fExecutableStack ) {
		switch ( fOutputKind ) {
			case Options::kDynamicExecutable:
			case Options::kStaticExecutable:
				// -allow_stack_execute size only legal when building main executable
				break;
			case Options::kDynamicLibrary:
			case Options::kDynamicBundle:
			case Options::kObjectFile:
			case Options::kDyld:
				throw "-allow_stack_execute option can only be used when linking a main executable";
		}
	}

	// check -client_name is only used when -bundle is specified
	if ( (fClientName != NULL) && (fOutputKind != Options::kDynamicBundle) )
		throw "-client_name can only be used with -bundle";

	// check -init is only used when building a dylib
	if ( (fInitFunctionName != NULL) && (fOutputKind != Options::kDynamicLibrary) )
		throw "-init can only be used with -dynamiclib";

	// check -bundle_loader only used with -bundle
	if ( (fBundleLoader != NULL) && (fOutputKind != Options::kDynamicBundle) )
		throw "-bundle_loader can only be used with -bundle";

	// check -d can only be used with -r
	if ( fMakeTentativeDefinitionsReal && (fOutputKind != Options::kObjectFile) )
		throw "-d can only be used with -r";
	
	// make sure all required exported symbols exist
	for (NameSet::iterator it=fExportSymbols.begin(); it != fExportSymbols.end(); it++) {
		const char* name = *it;
		// never export .eh symbols
		if ( strcmp(&name[strlen(name)-3], ".eh") != 0 )
			fInitialUndefines.push_back(name);
	}
	
	// make sure that -init symbol exist
	if ( fInitFunctionName != NULL )
		fInitialUndefines.push_back(fInitFunctionName);

	if ( fZeroPageSize == ULLONG_MAX ) {
		// zero page size not specified on command line, set default
		switch (fArchitecture) {
			case CPU_TYPE_I386:
			case CPU_TYPE_POWERPC:
				// first 4KB for 32-bit architectures
				fZeroPageSize = 0x1000;
				break;
			case CPU_TYPE_POWERPC64:
				// first 4GB for ppc64 on 10.5
				if ( fVersionMin >= k10_5 )
					fZeroPageSize = 0x100000000ULL;
				else
					fZeroPageSize = 0x1000;	// 10.4 dyld may not be able to handle >4GB zero page
				break;
			case CPU_TYPE_X86_64:
				// first 4GB for x86_64 on all OS's
				fZeroPageSize = 0x100000000ULL;
				break;
			default:
				// if -arch not used, default to 4K zero-page
				fZeroPageSize = 0x1000;
		}
	}
	else {
		switch ( fOutputKind ) {
			case Options::kDynamicExecutable:
			case Options::kStaticExecutable:
				// -pagezero_size size only legal when building main executable
				break;
			case Options::kDynamicLibrary:
			case Options::kDynamicBundle:
			case Options::kObjectFile:
			case Options::kDyld:
				throw "-pagezero_size option can only be used when linking a main executable";
		}	
	}

	// -dead_strip and -r are incompatible
	if ( (fDeadStrip != kDeadStripOff) && (fOutputKind == Options::kObjectFile) )
		throw "-r and -dead_strip cannot be used together\n";

}

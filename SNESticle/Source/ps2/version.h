
#ifndef _VERSION_H
#define _VERSION_H

struct VersionInfoT
{
	const char *ApplicationName;
	int Version[3];
	const char *BuildType;
	const char *ElfName;
	const char *CopyRight;
	const char *BuildDate;
	const char *BuildTime;
	const char *Compiler;
	int CompilerVersion[2];
};

const VersionInfoT *VersionGetInfo();
char *VersionGetElfName();

#endif

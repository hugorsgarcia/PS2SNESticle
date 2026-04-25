
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include "types.h"
#include "file.h"


Bool FileReadMem(Char *pFilePath, void *pMem, Uint32 nBytes)
{
	int hFile;
	int nReadBytes;

	hFile = open(pFilePath, O_RDONLY);
	if (hFile < 0)
	{
		return FALSE;
	}

	nReadBytes = read(hFile, pMem, nBytes);
	close(hFile);

	return ((Uint32)nReadBytes == nBytes);
}

Bool FileWriteMem(Char *pFilePath, void *pMem, Uint32 nBytes)
{
	int hFile;
	int nWriteBytes;

	hFile = open(pFilePath, O_CREAT | O_WRONLY | O_TRUNC, 0644);
	if (hFile < 0)
	{
		return FALSE;
	}

	nWriteBytes = write(hFile, pMem, nBytes);
	close(hFile);

	return ((Uint32)nWriteBytes == nBytes);
}

Bool FileExists(Char *pFilePath)
{
	int hFile;

	hFile = open(pFilePath, O_RDONLY);
	if (hFile < 0)
	{
		return FALSE;
	}

	close(hFile);
	return TRUE;
}


#include <tamtypes.h>
#include <kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#define NEWLIB_PORT_AWARE
#include <fileio.h>
#include <libmc.h>
#include "mcsave_ee.h"

static int _MCSave_bInitialized = 0;

int MCSave_IsInitialized()
{
	return _MCSave_bInitialized;
}

int MCSave_Init(int MaxSize)
{
	int ret = mcInit(MC_TYPE_MC);
	if (ret < 0) return -1;

	_MCSave_bInitialized = 1;
	return 0;
}

int MCSave_WriteSync(int bSync, int *pResult)
{
	int cmd, result;

	if (bSync)
	{
		mcSync(0, &cmd, &result);
		if (pResult) *pResult = result;
		return 0; // Async finished
	}
	else
	{
		int status = mcSync(1, &cmd, &result);
		if (status == 1) // Finished
		{
			if (pResult) *pResult = result;
			return 0; 
		}
		// status == 0 means executing, status == -1 means no function registered
		return (status == 0) ? 1 : 0; 
	}
}

int MCSave_Write(char *pFileName, char *pData, int nBytes)
{
	int port = 0;
	int slot = 0;
	char *name = pFileName;
	int isMemcard = 0;

	if (!_MCSave_bInitialized) return 0;

	if (strncmp(pFileName, "mc0:/", 5) == 0)
	{
		port = 0;
		name = pFileName + 5;
		isMemcard = 1;
	}
	else if (strncmp(pFileName, "mc1:/", 5) == 0)
	{
		port = 1;
		name = pFileName + 5;
		isMemcard = 1;
	}

	if (!isMemcard)
	{
		// Fallback to standard fio for mass:/, host:/, etc.
		int fd = fioOpen(pFileName, O_CREAT | O_TRUNC | O_WRONLY);
		if (fd >= 0)
		{
			fioWrite(fd, pData, nBytes);
			fioClose(fd);
		}
		// ensure mcSync isn't waiting on anything 
		mcSync(0, NULL, NULL); 
		return 1;
	}

	// Memory card operation via libmc
	int cmd, result, fd;

	mcOpen(port, slot, name, O_CREAT | O_TRUNC | O_WRONLY);
	mcSync(0, &cmd, &result);
	fd = result;

	if (fd >= 0)
	{
		mcWrite(fd, pData, nBytes);
		mcSync(0, &cmd, &result);

		mcClose(fd);
		mcSync(0, &cmd, &result);
	}

	return 1;
}

void MCSave_Shutdown()
{
	if(_MCSave_bInitialized)
	{
		mcSync(0, NULL, NULL);
		_MCSave_bInitialized = 0;
	}
}

int MCSave_Dread(int fd, io_dirent_t *dir)
{
	// Legacy bug workaround no longer needed on PS2SDK.
	// fioDread() safely handles mc0:/ directories.
	return fioDread(fd, dir);
}

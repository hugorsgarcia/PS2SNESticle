#include <tamtypes.h>
#include <kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#define NEWLIB_PORT_AWARE
#include <fileio.h>
#include <libmc.h>

// IOP file flags for mcOpen (different from newlib O_* flags!)
// newlib O_RDONLY=0, O_WRONLY=1, but IOP expects:
#define MC_O_RDONLY  0x0001
#define MC_O_WRONLY  0x0002
#define MC_O_CREAT   0x0200
#define MC_O_TRUNC   0x0400
#include "mcsave_ee.h"

static int _MCSave_bInitialized = 0;

int MCSave_IsInitialized()
{
	return _MCSave_bInitialized;
}

int MCSave_Init(int MaxSize)
{
	// Note: mcInit() is already called by MemCardInit() during module loading.
	// Calling mcInit() a second time returns -1 (by design in ps2sdk libmc).
	// We just mark ourselves as ready to use the already-initialized libmc.
	_MCSave_bInitialized = 1;
	printf("MCSave_Init: ready (mclib already initialized by MemCardInit)\n");
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
			int wrote = fioWrite(fd, pData, nBytes);
			fioClose(fd);
			if (wrote != nBytes)
			{
				printf("MCSave_Write: fio partial write %d/%d to %s\n", wrote, nBytes, pFileName);
				return 0;
			}
			printf("MCSave_Write: fio wrote %d bytes to %s\n", wrote, pFileName);
			return 1;
		}
		printf("MCSave_Write: fioOpen FAILED for %s\n", pFileName);
		return 0;
	}

	// Memory card operation via libmc
	int cmd, result, fd;

	mcOpen(port, slot, name, MC_O_WRONLY | MC_O_CREAT | MC_O_TRUNC);
	mcSync(0, &cmd, &result);
	fd = result;

	if (fd >= 0)
	{
		mcWrite(fd, pData, nBytes);
		mcSync(0, &cmd, &result);
		int wrote = result;

		mcClose(fd);
		mcSync(0, &cmd, &result);

		printf("MCSave_Write: mc wrote %d/%d to %s\n", wrote, nBytes, name);
		return (wrote == nBytes) ? 1 : 0;
	}

	printf("MCSave_Write: mcOpen FAILED for %s (fd=%d)\n", name, fd);
	return 0;
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

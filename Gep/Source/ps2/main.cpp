
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <kernel.h>
#include <iopheap.h>
#include <iopcontrol.h>
#include <sbv_patches.h>

#include "types.h"
#include "console.h"
#include "mainloop.h"

extern "C" {
#include "excepHandler.h"
#include "hw.h"
};



static char *_Main_pBootPath;
static char _Main_BootDir[256];


char *MainGetBootDir()
{
	return _Main_BootDir;
}

char *MainGetBootPath()
{
	return _Main_pBootPath;
}

void MainSetBootDir(const char *pPath)
{
	int i;
	strcpy(_Main_BootDir, pPath);

	i = strlen(_Main_BootDir);

	// search backward for start of filename
	while (i>0 
			&& _Main_BootDir[i]!='/'
			&& _Main_BootDir[i]!='\\'
			&& _Main_BootDir[i]!=':'
		) i--;

	i++;

	_Main_BootDir[i] = 0;
}

/* Reset the IOP and all of its subsystems.  */
int full_reset()
{
	SifExitIopHeap();
	SifLoadFileExit();
	SifExitRpc();

	SifIopReset("", 0);
	while (!SifIopSync()) ;

	SifInitRpc(0);
	SifLoadFileInit();
	SifInitIopHeap();
	FlushCache(0);

	/* Apply SBV patches to allow loading IRX modules from any path */
	sbv_patch_enable_lmb();
	sbv_patch_disable_prefix_check();

	return 0;
}








/* Your program's main entry point */
int main(int argc, char **argv) 
{
    int iArg;
//    init_scr();

	if (argc>=1)
	{
		_Main_pBootPath = argv[0];
	}

	MainSetBootDir(_Main_pBootPath);

	SifInitRpc(0);

	// Always reset IOP to clear BIOS modules and start fresh.
	// Required for embedded PS2SDK IOP modules (cdfs, usbd, etc.)
	full_reset();

    for (iArg=0; iArg < argc; iArg++)
    {
        printf("%d: %s\n", iArg, argv[iArg]);
    }

	DmaReset();

    install_VRstart_handler();

	ConInit();

	if (MainLoopInit())
	{
		// do stuff here
		while (MainLoopProcess())
		{
		}

		MainLoopShutdown();
	}

	ConShutdown();

	return 0;
}


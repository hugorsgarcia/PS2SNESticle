#ifndef _NES_H
#define _NES_H

#include "emusys.h"
#include "emurom.h"
#include "nesstate.h"

// NES IO bit definitions (standard NES controller)
#define NESIO_BIT_A      0
#define NESIO_BIT_B      1
#define NESIO_BIT_SELECT 2
#define NESIO_BIT_START  3
#define NESIO_BIT_UP     4
#define NESIO_BIT_DOWN   5
#define NESIO_BIT_LEFT   6
#define NESIO_BIT_RIGHT  7

// Forward declarations
class NesMMU;
class NesRom;
class NesFDSBios;
class NesDisk;

class NesMMU
{
public:
    void InsertDisk(int iDisk) {}
};

class NesSystem : public Emu::System
{
public:
    NesSystem() : m_MMU() {}
    ~NesSystem() {}

    void    SetRom(class Emu::Rom *pRom) {}
    void    SetNesDisk(NesDisk *pDisk) {}
    void    Reset() {}
    void    SoftReset() {}
    void    ExecuteFrame(Emu::SysInputT *pInput, class CRenderSurface *pTarget, class CMixBuffer *pSound, ModeE eMode) {}

    Int32   GetStateSize() { return sizeof(NesStateT); }
    void    SaveState(void *pState, Int32 nStateBytes) {}
    void    RestoreState(void *pState, Int32 nStateBytes) {}

    // Convenience overloads matching usage in mainloop
    void    SaveState(NesStateT *pState) {}
    Bool    RestoreState(NesStateT *pState) { return TRUE; }

    NesMMU *GetMMU() { return &m_MMU; }

    char *  GetString(StringE eString) { return ""; }
    Uint32  GetSampleRate() { return 0; }

private:
    NesMMU  m_MMU;
};

class NesRom : public Emu::Rom
{
public:
    NesRom() {}
    ~NesRom() {}

    LoadErrorE  LoadRom(CDataIO *pFileIO, Uint8 *pBuffer = NULL, Uint32 nBufferBytes = 0) { return LOADERROR_NONE; }
    void        Unload() { m_bLoaded = FALSE; }

    Uint32      GetNumExts() { return 1; }
    Char *      GetExtName(Uint32 uExt) { return ".nes"; }
    Uint32      GetNumRomRegions() { return 0; }
    Char *      GetRomRegionName(Uint32 uRegion) { return ""; }
    Uint32      GetRomRegionSize(Uint32 uRegion) { return 0; }
    Char *      GetMapperName() { return ""; }
};

class NesFDSBios : public Emu::Rom
{
public:
    NesFDSBios() {}
    ~NesFDSBios() {}

    LoadErrorE  LoadRom(CDataIO *pFileIO, Uint8 *pBuffer = NULL, Uint32 nBufferBytes = 0) { return LOADERROR_NONE; }
    void        Unload() { m_bLoaded = FALSE; }

    Uint32      GetNumExts() { return 1; }
    Char *      GetExtName(Uint32 uExt) { return ".bin"; }
    Uint32      GetNumRomRegions() { return 0; }
    Char *      GetRomRegionName(Uint32 uRegion) { return ""; }
    Uint32      GetRomRegionSize(Uint32 uRegion) { return 0; }
    Char *      GetMapperName() { return ""; }
};

class NesDisk : public Emu::Rom
{
public:
    NesDisk() {}
    ~NesDisk() {}

    LoadErrorE  LoadRom(CDataIO *pFileIO, Uint8 *pBuffer = NULL, Uint32 nBufferBytes = 0) { return LOADERROR_NONE; }
    void        Unload() { m_bLoaded = FALSE; }
    Int32       GetNumDisks() { return 0; }

    Uint32      GetNumExts() { return 1; }
    Char *      GetExtName(Uint32 uExt) { return ".fds"; }
    Uint32      GetNumRomRegions() { return 0; }
    Char *      GetRomRegionName(Uint32 uRegion) { return ""; }
    Uint32      GetRomRegionSize(Uint32 uRegion) { return 0; }
    Char *      GetMapperName() { return ""; }
};

#endif // _NES_H

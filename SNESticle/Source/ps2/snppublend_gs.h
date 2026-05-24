
#ifndef _SNPPUBLEND_GS_H
#define _SNPPUBLEND_GS_H


#include "snppublend.h"

struct SNPPUDmaListT
{
    Uint128     Data[128] _ALIGN(64);

    Uint64      *pFixedColor;
    Uint64      *pAddSub;
    Uint64      *pIntensity;
    Uint64      *pXYOffset;

    Uint32      uPalAddr;
    Uint32      uInputAddr;
    Uint32      uAttribMainPal;
    Uint32      uAttribSubPal;
    Uint32      uTempAddr;

	Uint32		uOutAddr;
	Uint32		uPadding[6]; // Pad structure to multiple of 64 bytes (2112 bytes total)
} _ALIGN(64);

struct SNPPUBlendColorCalibT
{
	Float32	y_mul,y_add;
	Float32	i_mul,i_add;
	Float32	q_mul,q_add;
};



class SNPPUBlendGS : public ISNPPUBlend
{
    SNPPUDmaListT m_DmaList _ALIGN(64);
    SNPPUBlendInfoT *m_pDmaBlendInfo _ALIGN(64);


public:
    SNPPUBlendGS(Uint32 uVramAddr, Uint32 uOutAddr);

    virtual void Begin(class CRenderSurface *pTarget);
    virtual void Exec(SNPPUBlendInfoT *pInfo, Int32 iLine, Uint32 uFixedColor32, SNMaskT *pColorMask, Bool bAddSub, Uint32 uIntensity);
    virtual void Clear(SNPPUBlendInfoT *pInfo, Int32 iLine);
    virtual void End();
    virtual void UpdatePalette(SNPPUBlendInfoT *pInfo, Uint16 *pCGRam, Uint32 uIntensity);
    virtual void UpdatePaletteEntry(SNPPUBlendInfoT *pInfo, Uint32 uAddr, Uint32 uData, Uint32 uIntensity);

	static void ColorCalibrate(SNPPUBlendColorCalibT *pCalib);
};



#endif

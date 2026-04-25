#ifndef _AUDSRVBUFFER_H
#define _AUDSRVBUFFER_H

#include "types.h"
#include "mixbuffer.h"

#define AUDSRVMIXBUFFER_MAXENQUEUE (16384)

class AudsrvMixBuffer : public CMixBuffer
{
    Int16   m_OutData[AUDSRVMIXBUFFER_MAXENQUEUE * 2] _ALIGN(16);
    Int32   m_nOutSamples;
    Uint32  m_uSampleRate;
    Int32   m_iPrevSample[2];
    Uint32  m_uLastOutput;

public:
    virtual void GetFormat(Uint32 *puSampleRate, Uint32 *pnSampleBits, Uint32 *pnChannels);
    virtual Int32 GetOutputSamples();
    virtual void OutputSamplesMono(Int16 *pSamples,Int32 nSamples);
    virtual void OutputSamplesStereo(Int16 *pLeftSamples, Int16 *pRightSamples, Int32 nSamples);
    virtual void Flush();
    virtual void SetSampleRate(Uint32 uSampleRate);

    AudsrvMixBuffer(Uint32 uSampleRate = 48000);
    ~AudsrvMixBuffer();

private:
    Int32 ConvertSamples2to3(Int16 *pOut, Int16 *pIn, Int32 nSamples, Int32 *pPrevSample);
    Int32 ConvertSamplesStereo_32000(Int16 *pLeftSamples, Int16 *pRightSamples, Int16 *pOutLeft, Int16 *pOutRight, Int32 nInSamples);
};

#endif

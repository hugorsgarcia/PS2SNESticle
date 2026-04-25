#include <stdio.h>
#include <string.h>
extern "C" {
#include <audsrv.h>
}
#include "audsrvbuffer.h"

AudsrvMixBuffer::AudsrvMixBuffer(Uint32 uSampleRate)
{
    m_uSampleRate = uSampleRate;
    m_nOutSamples = 0;
    m_iPrevSample[0] = 0;
    m_iPrevSample[1] = 0;
    m_uLastOutput = 0;
}

AudsrvMixBuffer::~AudsrvMixBuffer()
{
}

void AudsrvMixBuffer::GetFormat(Uint32 *puSampleRate, Uint32 *pnSampleBits, Uint32 *pnChannels)
{
    if (puSampleRate) *puSampleRate = m_uSampleRate;
    if (pnSampleBits) *pnSampleBits = 16;
    if (pnChannels)   *pnChannels = 2;
}

void AudsrvMixBuffer::SetSampleRate(Uint32 uSampleRate)
{
    m_uSampleRate = uSampleRate;
}

Int32 AudsrvMixBuffer::GetOutputSamples()
{
    // Fixed output: always generate exactly one frame's worth of audio.
    // At 32kHz NTSC (60fps): 32000/60 = 533 samples per frame.
    //
    // Do NOT use audsrv_queued() here — it returns >= 4096 even when
    // the IOP ring buffer is empty, causing nTotalSamples=0 (silence).
    // audsrv_play_audio() handles ring buffer management internally.
    Int32 nSamples = m_uSampleRate / 60;

    if (nSamples > AUDSRVMIXBUFFER_MAXENQUEUE)
        nSamples = AUDSRVMIXBUFFER_MAXENQUEUE;

    m_uLastOutput = nSamples;
    return nSamples;
}

void AudsrvMixBuffer::OutputSamplesMono(Int16 *pSamples, Int32 nSamples)
{
    // Not used by SNESticle (always stereo)
}

void AudsrvMixBuffer::OutputSamplesStereo(Int16 *pLeftSamples, Int16 *pRightSamples, Int32 nSamples)
{
    if (nSamples + m_nOutSamples > AUDSRVMIXBUFFER_MAXENQUEUE)
        nSamples = AUDSRVMIXBUFFER_MAXENQUEUE - m_nOutSamples;

    if (nSamples <= 0) return;

    Int16 *pOut = &m_OutData[m_nOutSamples * 2];

    // Interleave L/R samples for audsrv's expected format
    for (int i = 0; i < nSamples; i++)
    {
        *pOut++ = pLeftSamples[i];
        *pOut++ = pRightSamples[i];
    }

    m_nOutSamples += nSamples;
}

void AudsrvMixBuffer::Flush()
{
    if (m_nOutSamples > 0)
    {
        audsrv_play_audio((char *)m_OutData, m_nOutSamples * 4);
        m_nOutSamples = 0;
    }
}

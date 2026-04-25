#ifndef _NCPU_C_H
#define _NCPU_C_H

#include "gepdefs.h"

typedef void *NCpuT;

typedef Int32 (*N6502ExecuteFuncT)(NCpuT *pCpu);

static inline void N6502SetExecuteFunc(N6502ExecuteFuncT pFunc) { (void)pFunc; }

static inline Int32 NCPUExecute_C(NCpuT *pCpu) { (void)pCpu; return 0; }

#endif // _NCPU_C_H

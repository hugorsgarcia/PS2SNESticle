#ifndef _NESPAL_H
#define _NESPAL_H

#include "palette.h"

// Number of composed palettes (8 emphasis modes x 2 monochrome states)
#define NESPAL_NUMPALETTES	16

// Stock palette enumeration
enum NesPalE
{
	NESPAL_CCOVELL = 0,
	NESPAL_FCEU = 0,
	NESPAL_SHADY = 1,
	NESPAL_NUM
};

// Compose a palette with emphasis/monochrome applied
// iPal: bits 0-2 = emphasis, bit 3 = monochrome
void NesPalComposePalette(int iPal, Color32T* Palette, Color32T* BasePal, unsigned int brightness);

// Get pointer to a stock (built-in) NES palette
Color32T* NesPalGetStockPalette(NesPalE eNesPal);

// Generate a palette from hue/saturation (not implemented)
void NesPalGenerate(Color32T* pal, float hue, float saturation);

#endif

#include "ui_orientation.h"

uint8_t UI_OrientationToLcdCode(ui_orientation_t orientation)
{
	switch (orientation) {
	case UI_ORIENTATION_180:
		return 0u;
	case UI_ORIENTATION_90:
		return 2u;
	case UI_ORIENTATION_270:
		return 3u;
	case UI_ORIENTATION_0:
	default:
		return 1u;
	}
}

uint8_t UI_OrientationIsPortrait(ui_orientation_t orientation)
{
	return (orientation == UI_ORIENTATION_90 ||
	        orientation == UI_ORIENTATION_270) ? 1u : 0u;
}

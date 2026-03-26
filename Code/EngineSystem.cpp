/**
 * @file EngineSystem.cpp
 * @brief Shared system geometry and layout helper implementation.
 */
#include "EngineSystem.h"
#include <cstdint>

static int _GSystemScaleLayoutValue (int value, int numerator, int denominator) {
	if(denominator == 0)
		return 0;
	return (int)(((int64_t)value * (int64_t)numerator) / (int64_t)denominator);
}

void ESystem::GetSystemRects (
	const ERect& nativeRect,
	const ERect& nativeSafeRect,
	const ERect& designRect,
	ERect& screenRect,
	ERect& safeRect
) {
	ERect resolvedNative = nativeRect;
	ERect resolvedNativeSafe = nativeSafeRect;
	ERect resolvedDesign = designRect;

	if(resolvedNative.width <= 0 || resolvedNative.height <= 0)
		resolvedNative.Set(0, 0, resolvedDesign.width > 0 ? resolvedDesign.width : 0, resolvedDesign.height > 0 ? resolvedDesign.height : 0);
	if(resolvedNativeSafe.width <= 0 || resolvedNativeSafe.height <= 0)
		resolvedNativeSafe = resolvedNative;

	if(resolvedDesign.width <= 0 || resolvedDesign.height <= 0)
		resolvedDesign.Set(0, 0, resolvedNativeSafe.width, resolvedNativeSafe.height);
	if(resolvedDesign.width <= 0 || resolvedDesign.height <= 0) {
		screenRect = resolvedNative;
		safeRect = resolvedNativeSafe;
		return;
	}

	const bool fitToHeight = (int64_t)resolvedDesign.width * (int64_t)resolvedNativeSafe.height <= (int64_t)resolvedNativeSafe.width * (int64_t)resolvedDesign.height;
	const int scaleNumerator = fitToHeight ? resolvedDesign.height : resolvedDesign.width;
	const int scaleDenominator = fitToHeight ? resolvedNativeSafe.height : resolvedNativeSafe.width;

	ERect resolvedScreen;
	resolvedScreen.x = _GSystemScaleLayoutValue(resolvedNative.x, scaleNumerator, scaleDenominator);
	resolvedScreen.y = _GSystemScaleLayoutValue(resolvedNative.y, scaleNumerator, scaleDenominator);
	resolvedScreen.width = _GSystemScaleLayoutValue(resolvedNative.width, scaleNumerator, scaleDenominator);
	resolvedScreen.height = _GSystemScaleLayoutValue(resolvedNative.height, scaleNumerator, scaleDenominator);

	ERect resolvedSafe;
	resolvedSafe.x = _GSystemScaleLayoutValue(resolvedNativeSafe.x, scaleNumerator, scaleDenominator);
	resolvedSafe.y = _GSystemScaleLayoutValue(resolvedNativeSafe.y, scaleNumerator, scaleDenominator);
	resolvedSafe.width = fitToHeight ? _GSystemScaleLayoutValue(resolvedNativeSafe.width, scaleNumerator, scaleDenominator) : resolvedDesign.width;
	resolvedSafe.height = fitToHeight ? resolvedDesign.height : _GSystemScaleLayoutValue(resolvedNativeSafe.height, scaleNumerator, scaleDenominator);

	const int designOriginX = resolvedSafe.x + (resolvedSafe.width - resolvedDesign.width) / 2;
	const int designOriginY = resolvedSafe.y + (resolvedSafe.height - resolvedDesign.height) / 2;

	screenRect = resolvedScreen;
	screenRect.x -= designOriginX;
	screenRect.y -= designOriginY;

	safeRect = resolvedSafe;
	safeRect.x -= designOriginX;
	safeRect.y -= designOriginY;
}

/**
 * @file LayoutUtil.h
 * @brief Gameplay layout and input-normalization helper interfaces for preferred/safe view mapping.
 */
#ifndef DRAGON_ALPHA_LAYOUT_UTIL_H
#define DRAGON_ALPHA_LAYOUT_UTIL_H

#include "Global.h"
#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

struct LegacyCanvas {
	ERect frame;
	int baseWidth;
	int baseHeight;
	double scale;
};

inline LegacyCanvas MakeLegacyCanvas (const ERect& safe, int baseWidth = 800, int baseHeight = 600) {
	LegacyCanvas canvas = {
		ERect(safe.x, safe.y, std::max(1, safe.width), std::max(1, safe.height)),
		std::max(1, baseWidth),
		std::max(1, baseHeight),
		1.0
	};
	const double scaleX = (double)safe.width / (double)canvas.baseWidth;
	const double scaleY = (double)safe.height / (double)canvas.baseHeight;
	canvas.scale = std::max(0.0001, std::min(scaleX, scaleY));
	const int drawWidth = std::max(1, (int)(canvas.baseWidth * canvas.scale + 0.5));
	const int drawHeight = std::max(1, (int)(canvas.baseHeight * canvas.scale + 0.5));
	canvas.frame = ERect(
		safe.x + (safe.width - drawWidth) / 2,
		safe.y + (safe.height - drawHeight) / 2,
		drawWidth,
		drawHeight
	);
	return canvas;
}

inline ERect MakePreferredViewRect (const ERect& safe, int preferredWidth = DESIGN_PREFERRED_WIDTH, int preferredHeight = DESIGN_PREFERRED_HEIGHT) {
	if(safe.width <= 0 or safe.height <= 0)
		return safe;
	if(preferredWidth <= 0 or preferredHeight <= 0)
		return safe;
	return MakeLegacyCanvas(safe, preferredWidth, preferredHeight).frame;
}

inline LegacyCanvas MakeLegacyCanvasInPreferredView (const ENode& node, int baseWidth = 800, int baseHeight = 600, int preferredWidth = DESIGN_PREFERRED_WIDTH, int preferredHeight = DESIGN_PREFERRED_HEIGHT) {
	const ERect preferredView = MakePreferredViewRect(node.GetSafeRect(), preferredWidth, preferredHeight);
	return MakeLegacyCanvas(preferredView, baseWidth, baseHeight);
}

inline ERect LegacyRect (const LegacyCanvas& canvas, int x, int y, int width, int height) {
	const int left = canvas.frame.x + (int)(x * canvas.scale + 0.5);
	const int top = canvas.frame.y + (int)(y * canvas.scale + 0.5);
	const int drawWidth = std::max(1, (int)(width * canvas.scale + 0.5));
	const int drawHeight = std::max(1, (int)(height * canvas.scale + 0.5));
	return ERect(left, top, drawWidth, drawHeight);
}

inline EPoint NormalizeInputPoint (const ENode& node, int x, int y) {
	(void)node;
	// The engine touch/mouse callbacks are already delivered in node-local coordinates.
	return EPoint(x, y);
}

inline EPoint CenterInRect (const ERect& outer, int width, int height) {
	return EPoint(outer.x + (outer.width - width) / 2, outer.y + (outer.height - height) / 2);
}

inline EPoint CenterInSafeRect (ENode& node, int width, int height) {
	return CenterInRect(node.GetSafeRect(), width, height);
}

inline ERect InsetRect (const ERect& rect, int amount) {
	return ERect(rect.x + amount, rect.y + amount, std::max(1, rect.width - amount * 2), std::max(1, rect.height - amount * 2));
}

inline ERect ClipRectToBounds (const ERect& rect, const ERect& bounds) {
	const int left = std::max(rect.x, bounds.x);
	const int top = std::max(rect.y, bounds.y);
	const int right = std::min(rect.x + rect.width, bounds.x + bounds.width);
	const int bottom = std::min(rect.y + rect.height, bounds.y + bounds.height);
	if(right <= left or bottom <= top)
		return ERect(0, 0, 0, 0);
	return ERect(left, top, right - left, bottom - top);
}

inline ERect ExpandRectForTouch (const ERect& rect, int minWidth = 44, int minHeight = 44, int extraPadding = 0) {
	if(rect.width <= 0 or rect.height <= 0)
		return rect;

	const int targetWidth = std::max(rect.width, minWidth);
	const int targetHeight = std::max(rect.height, minHeight);
	const int expandX = (targetWidth - rect.width) / 2 + extraPadding;
	const int expandY = (targetHeight - rect.height) / 2 + extraPadding;
	return ERect(rect.x - expandX, rect.y - expandY, rect.width + expandX * 2, rect.height + expandY * 2);
}

inline bool HitTouchRect (const ERect& rect, int x, int y, int minWidth = 44, int minHeight = 44, int extraPadding = 0) {
	return ExpandRectForTouch(rect, minWidth, minHeight, extraPadding).IsPointInRect(x, y);
}

inline EString FitTextToWidth (EFont& font, const EString& text, int maxWidth, const char* ellipsis = "...") {
	if(maxWidth <= 0 or font.IsEmpty())
		return text;
	if(font.GetWidth(text) <= maxWidth)
		return text;
	if(ellipsis == NULL)
		ellipsis = "";

	const int ellipsisWidth = font.GetWidth(ellipsis);
	if(ellipsisWidth >= maxWidth)
		return "";

	std::string clipped = (const char*)text;
	while(clipped.empty() == false) {
		std::string candidate = clipped + ellipsis;
		if(font.GetWidth(candidate.c_str()) <= maxWidth)
			return candidate.c_str();
		clipped.pop_back();
	}
	return "";
}

inline void DrawImageContain (EImage& image, const ERect& bounds, const EColor& color = EColor::WHITE) {
	if(image.IsEmpty() or bounds.width <= 0 or bounds.height <= 0)
		return;

	const int imageWidth = image.GetWidth();
	const int imageHeight = image.GetHeight();
	if(imageWidth <= 0 or imageHeight <= 0)
		return;

	const double scaleX = (double)bounds.width / (double)imageWidth;
	const double scaleY = (double)bounds.height / (double)imageHeight;
	const double scale = std::min(scaleX, scaleY);
	const int drawWidth = std::max(1, (int)(imageWidth * scale));
	const int drawHeight = std::max(1, (int)(imageHeight * scale));
	const ERect drawRect(
		bounds.x + (bounds.width - drawWidth) / 2,
		bounds.y + (bounds.height - drawHeight) / 2,
		drawWidth,
		drawHeight
	);
	image.DrawRect(drawRect, color);
}

inline void DrawImageCover (EImage& image, const ERect& bounds, const EColor& color = EColor::WHITE) {
	if(image.IsEmpty() or bounds.width <= 0 or bounds.height <= 0)
		return;

	const int imageWidth = image.GetWidth();
	const int imageHeight = image.GetHeight();
	if(imageWidth <= 0 or imageHeight <= 0)
		return;

	const double scaleX = (double)bounds.width / (double)imageWidth;
	const double scaleY = (double)bounds.height / (double)imageHeight;
	const double scale = std::max(scaleX, scaleY);

	int srcWidth = std::max(1, (int)(bounds.width / scale));
	int srcHeight = std::max(1, (int)(bounds.height / scale));
	srcWidth = std::min(srcWidth, imageWidth);
	srcHeight = std::min(srcHeight, imageHeight);

	const ERect srcRect(
		std::max(0, (imageWidth - srcWidth) / 2),
		std::max(0, (imageHeight - srcHeight) / 2),
		srcWidth,
		srcHeight
	);
	image.Draw(srcRect, bounds, color);
}

inline void DrawCenteredLabel (EFont& font, const EString& text, const ERect& rect, int yNudge = 0) {
	if(font.IsEmpty() or rect.width <= 0 or rect.height <= 0)
		return;

	EString drawText = FitTextToWidth(font, text, rect.width);
	if(drawText.IsEmpty())
		return;
	ERect textRect = font.GetRect(drawText);
	int x = rect.x + (rect.width - textRect.width) / 2;
	int y = rect.y + (rect.height - font.GetLineHeight()) / 2 + yNudge;
	font.Draw(drawText, x, y);
}

inline void DrawLeftClampedLabel (EFont& font, const EString& text, const ERect& rect, int xPadding = 0, int yNudge = 0) {
	if(font.IsEmpty() or rect.width <= 0 or rect.height <= 0)
		return;
	const int safePadding = std::max(0, xPadding);
	const int availableWidth = std::max(1, rect.width - safePadding * 2);
	EString drawText = FitTextToWidth(font, text, availableWidth);
	if(drawText.IsEmpty())
		return;
	const int x = rect.x + safePadding;
	const int y = rect.y + (rect.height - font.GetLineHeight()) / 2 + yNudge;
	font.Draw(drawText, x, y);
}

inline void DrawRightClampedLabel (EFont& font, const EString& text, const ERect& rect, int xPadding = 0, int yNudge = 0) {
	if(font.IsEmpty() or rect.width <= 0 or rect.height <= 0)
		return;
	const int safePadding = std::max(0, xPadding);
	const int availableWidth = std::max(1, rect.width - safePadding * 2);
	EString drawText = FitTextToWidth(font, text, availableWidth);
	if(drawText.IsEmpty())
		return;
	const int drawWidth = font.GetWidth(drawText);
	const int x = rect.x + rect.width - safePadding - drawWidth;
	const int y = rect.y + (rect.height - font.GetLineHeight()) / 2 + yNudge;
	font.Draw(drawText, x, y);
}

inline int LayoutLineHeight (EFont& font, int minimum = 12, int maximum = 30) {
	const int minValue = std::max(1, minimum);
	const int maxValue = std::max(minValue, maximum);
	if(font.IsEmpty())
		return minValue;
	return std::max(minValue, std::min(maxValue, font.GetLineHeight()));
}

inline int LayoutLineStep (EFont& font, int minimum = 14, int maximum = 34, int extraSpacing = 2) {
	const int spacing = std::max(0, extraSpacing);
	const int baseMax = std::max(minimum, maximum - spacing);
	const int base = LayoutLineHeight(font, minimum, baseMax);
	return std::max(std::max(1, minimum), std::min(std::max(minimum, maximum), base + spacing));
}

inline std::vector<EString> WrapTextLines (EFont& font, const EString& text, int maxWidth) {
	std::vector<EString> lines;
	if(maxWidth <= 0)
		return lines;
	if(font.IsEmpty()) {
		lines.push_back(text);
		return lines;
	}

	std::istringstream stream((const char*)text);
	std::string word;
	std::string current;
	while(stream >> word) {
		std::string candidate = current.empty() ? word : current + " " + word;
		if(font.GetWidth(candidate.c_str()) <= maxWidth or current.empty()) {
			current = candidate;
		} else {
			lines.push_back(current.c_str());
			current = word;
		}
	}
	if(current.empty() == false)
		lines.push_back(current.c_str());
	return lines;
}

inline void DrawWrappedLabel (EFont& font, const EString& text, const ERect& rect, int maxLines = 0) {
	if(font.IsEmpty() or rect.width <= 0 or rect.height <= 0)
		return;

	std::vector<EString> lines = WrapTextLines(font, text, rect.width);
	if(lines.empty())
		return;

	int lineHeight = std::max(1, font.GetLineHeight());
	int allowedLines = maxLines > 0 ? maxLines : std::max(1, rect.height / lineHeight);
	if((int)lines.size() > allowedLines)
		lines.resize(allowedLines);

	for(int i = 0; i < (int)lines.size(); i++)
		font.Draw(lines[i], rect.x, rect.y + i * lineHeight);
}

inline void LoadImageOrFallback (EImage& image, const char* resource, const EColor& fallback) {
	if(resource != NULL and image.New(resource))
		return;
	image.New(fallback);
}

inline void LoadImageOrFallback (EImage& image, const char* primaryResource, const char* secondaryResource, const EColor& fallback) {
	if(primaryResource != NULL and image.New(primaryResource))
		return;
	if(secondaryResource != NULL and image.New(secondaryResource))
		return;
	image.New(fallback);
}

inline void LoadSoundIfPresent (ESound& sound, const char* resource) {
	if(resource == NULL)
		return;
	sound.New(resource);
}

inline void PlaySoundIfLoaded (ESound& sound) {
	sound.Stop();
	sound.Play();
}

#endif // DRAGON_ALPHA_LAYOUT_UTIL_H

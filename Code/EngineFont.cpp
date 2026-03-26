/**
 * @file EngineFont.cpp
 * @brief Font loading, glyph metrics, and text rendering implementation.
 */
#include "EngineFont.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace {
struct VisibleGlyphBounds {
	bool valid;
	int left;
	int top;
	int right;
	int bottom;
};

VisibleGlyphBounds FindVisibleGlyphBounds (
	const std::vector<uint8_t>& rgba,
	int imageWidth,
	int imageHeight,
	const ERect& rect
) {
	VisibleGlyphBounds bounds = { false, 0, 0, 0, 0 };
	if(rect.width <= 0
		|| rect.height <= 0
		|| imageWidth <= 0
		|| imageHeight <= 0
		|| rgba.size() < (size_t)imageWidth * (size_t)imageHeight * 4) {
		return bounds;
	}

	int minX = rect.width;
	int minY = rect.height;
	int maxX = -1;
	int maxY = -1;
	for(int y = 0; y < rect.height; y++) {
		const int srcY = rect.y + y;
		if(srcY < 0 || srcY >= imageHeight)
			continue;
		for(int x = 0; x < rect.width; x++) {
			const int srcX = rect.x + x;
			if(srcX < 0 || srcX >= imageWidth)
				continue;
			const size_t pixel = ((size_t)srcY * (size_t)imageWidth + (size_t)srcX) * 4;
			if(rgba[pixel + 3] == 0)
				continue;
			if(x < minX) minX = x;
			if(y < minY) minY = y;
			if(x > maxX) maxX = x;
			if(y > maxY) maxY = y;
			bounds.valid = true;
		}
	}

	if(!bounds.valid)
		return bounds;

	bounds.left = minX;
	bounds.top = minY;
	bounds.right = maxX;
	bounds.bottom = maxY;
	return bounds;
}

static bool SafeMulNonNegativeInt64 (int64_t a, int64_t b, int64_t& out) {
	if(a < 0 || b < 0)
		return false;
	if(a == 0 || b == 0) {
		out = 0;
		return true;
	}
	if(a > std::numeric_limits<int64_t>::max() / b)
		return false;
	out = a * b;
	return true;
}

static bool SafeAddNonNegativeInt64 (int64_t a, int64_t b, int64_t& out) {
	if(a < 0 || b < 0)
		return false;
	if(a > std::numeric_limits<int64_t>::max() - b)
		return false;
	out = a + b;
	return true;
}

static bool ComputeRgbaImageBufferSize (int32_t imageWidth, int32_t imageHeight, int64_t& outSize) {
	if(imageWidth <= 0 || imageHeight <= 0)
		return false;
	int64_t rowBytes = 0;
	if(!SafeMulNonNegativeInt64((int64_t)imageWidth, 4, rowBytes))
		return false;
	if(!SafeMulNonNegativeInt64(rowBytes, (int64_t)imageHeight, outSize))
		return false;
	return true;
}

static bool FitsSizeTFromInt64 (int64_t value) {
	return value >= 0 && (uint64_t)value <= (uint64_t)std::numeric_limits<size_t>::max();
}

}
bool EFont::New (const Resource& resource) {
_height = 0;
_base = 0;
_rects.clear();
_offsets.clear();
_advances.clear();
_has_kern.clear();
_hash.clear();
_kernings.clear();
_buffer.clear();
_imageWidth = 0;
_imageHeight = 0;

EImage::Resource imageResource;
imageResource.width = resource.imageWidth;
imageResource.height = resource.imageHeight;
imageResource.bufferSize = resource.bufferSize;
imageResource.buffer = resource.buffer;
if(!_image.New(imageResource)) {
	ESystem::Debug("Could not create image with font resource data!\n");
	imageResource.buffer = nullptr;
	return false;
}
imageResource.buffer = nullptr;

if(resource.buffer != nullptr && resource.bufferSize > 0) {
	_buffer.assign(resource.buffer, resource.buffer + resource.bufferSize);
	_imageWidth = resource.imageWidth;
	_imageHeight = resource.imageHeight;
}

_height = resource.height;
_base = resource.base;

if(resource.charCount > 0) {
	_rects.resize(resource.charCount);
	_offsets.resize(resource.charCount);
	_advances.resize(resource.charCount);
	_has_kern.resize(resource.charCount);
	for(int i = 0; i < resource.charCount; i++) {
		_rects[i].x = resource.chars[i].srcX;
		_rects[i].y = resource.chars[i].srcY;
		_rects[i].width = resource.chars[i].srcWidth;
		_rects[i].height = resource.chars[i].srcHeight;
		_offsets[i].x = resource.chars[i].xOffset;
		_offsets[i].y = resource.chars[i].yOffset;
		_advances[i] = resource.chars[i].xAdvance;
		_has_kern[i] = false;
	}
}

if(!_buffer.empty() && _imageWidth > 0 && _imageHeight > 0 && !_rects.empty()) {
	int minTop = std::numeric_limits<int>::max();
	int maxBottom = std::numeric_limits<int>::min();
	bool foundVisibleGlyph = false;
	for(int i = 0; i < (int)_rects.size(); i++) {
		if(_rects[i].width <= 0 || _rects[i].height <= 0)
			continue;

		const VisibleGlyphBounds bounds = FindVisibleGlyphBounds(_buffer, _imageWidth, _imageHeight, _rects[i]);
		if(!bounds.valid)
			continue;

		_rects[i].x += bounds.left;
		_rects[i].y += bounds.top;
		_rects[i].width = bounds.right - bounds.left + 1;
		_rects[i].height = bounds.bottom - bounds.top + 1;

		// Preserve authored draw placement while collapsing the visible glyph box.
		_offsets[i].x += bounds.left;
		_offsets[i].y += bounds.top;

		const int renderTop = _offsets[i].y;
		const int renderBottom = renderTop + _rects[i].height;
		if(renderTop < minTop)
			minTop = renderTop;
		if(renderBottom > maxBottom)
			maxBottom = renderBottom;
		foundVisibleGlyph = true;
	}

	if(foundVisibleGlyph && minTop < maxBottom) {
		_height = maxBottom - minTop;
		_base = maxBottom;
	}
}

	if(resource.hashCount > 0 && resource.hashCount + (1 << 7) - 1 <= resource.charCount) {
		for(int i = 0; i < resource.hashCount; i++)
			_hash[resource.hash[i]] = i + (1 << 7);
}

if(resource.kernCount > 0) {
	for(int i = 0; i < resource.kernCount; i++) {
		int first = (int)(resource.kernings[i] & 0x0000000000ffffff);
		int second = (int)((resource.kernings[i] & 0x0000ffffff000000) >> 24);
		int amount = (int)((resource.kernings[i] & 0xffff000000000000) >> 48);
		_kernings[std::make_pair(first, second)] = amount;
	}
}

return true;
}

void EFont::Delete () {
_height = 0;
_base = 0;
_rects.clear();
_offsets.clear();
_advances.clear();
_has_kern.clear();
_hash.clear();
_kernings.clear();
_buffer.clear();
_imageWidth = 0;
_imageHeight = 0;
_image.Delete();
}

int EFont::GetLineHeight () const {
	if(_height > 0)
		return _height;
	if(_base > 0)
		return _base;

	int glyphHeight = 0;
	for(const ERect& rect : _rects)
		if(rect.height > glyphHeight)
			glyphHeight = rect.height;
	if(glyphHeight > 0)
		return glyphHeight;

	return 1;
}



ERect EFont::GetRect (const EString& text) const {
if(text.IsEmpty() || _rects.size() < ((1 << 7) - 1))
	return {};

	int left = std::numeric_limits<int>::max();
	int top = std::numeric_limits<int>::max();
	int right = std::numeric_limits<int>::min();
	int bottom = std::numeric_limits<int>::min();
	bool hasGlyph = false;

	EPoint pos;
	int lineStep = GetLineHeight();
	int last = 0; // Last character rendered (used for kernings)

		for(int i = 0; i < text.GetLength(); i++) {
			if(text[i] != '\n') {
				int index = 0;
				// Find the index
				if((uint8_t)text[i] <= 0x7f) {
				// ASCII characters are always indexed exactly
				index = (int)(uint8_t)text[i];
		} else if((uint8_t)text[i] >= 0xc2 && (uint8_t)text[i] <= 0xdf && i + 1 < text.GetLength()) {
			// Find the index of two-byte non-ASCII characters
			index = GetIndexFromHash((0) | (0 << 8) | ((uint8_t)text[i + 1] << 16) | ((uint8_t)text[i] << 24));
			i += 1;
		} else if((uint8_t)text[i] >= 0xe0 && (uint8_t)text[i] <= 0xef && i + 2 < text.GetLength()) {
			// Find the index of three-byte non-ASCII characters
			index = GetIndexFromHash((0) | ((uint8_t)text[i + 2] << 8) | ((uint8_t)text[i + 1] << 16) | ((uint8_t)text[i] << 24));
			i += 2;
			} else if((uint8_t)text[i] >= 0xf0 && (uint8_t)text[i] <= 0xf4 && i + 3 < text.GetLength()) {
				// Find the index of four-byte non-ASCII characters
				index = GetIndexFromHash(((uint8_t)text[i + 3]) | ((uint8_t)text[i + 2] << 8) | ((uint8_t)text[i + 1] << 16) | ((uint8_t)text[i] << 24));
				i += 3;
			} else {
				ESystem::Debug("Unknown character (0x%x) found while drawing font!\n", (uint8_t)text[i]);
				last = 0;
				continue;
			}

			if(index < 0
				|| index >= (int)_rects.size()
				|| index >= (int)_offsets.size()
				|| index >= (int)_advances.size()
				|| last < 0
				|| last >= (int)_has_kern.size()) {
				last = 0;
				continue;
			}
			
			// Adjust for a possible kerning
			int kerning = 0;
			if(_has_kern[last]) {
				auto k = _kernings.find(std::make_pair(last, index));
			if(k != _kernings.end())
				kerning = k->second;
		}
		
		// Find the visible rendered area
		const int renderLeft = pos.x + _offsets[index].x + kerning;
		const int renderTop = pos.y + _offsets[index].y;
		const int renderRight = renderLeft + _rects[index].width;
		const int renderBottom = renderTop + _rects[index].height;
		if(_rects[index].width > 0 && _rects[index].height > 0) {
			left = std::min(left, renderLeft);
			top = std::min(top, renderTop);
			right = std::max(right, renderRight);
			bottom = std::max(bottom, renderBottom);
			hasGlyph = true;
		}
			
			// Advance the x position
			pos.x += _advances[index];
				last = index;
		} else {
			// If a new line, reset the x position to 0 and move down the height
			pos.x = 0;
			pos.y += lineStep;
		}
		}

	if(!hasGlyph)
		return {};

	return ERect(left, top, right - left, bottom - top);
}



bool EFont::NewImageFromText (const EString& text, EImage& image, const EColor& color) const {
	if(text.IsEmpty()
		|| _rects.size() < ((1 << 7) - 1)
		|| _buffer.empty()
		|| _imageWidth <= 0
		|| _imageHeight <= 0
		|| _buffer.size() < (size_t)_imageWidth * (size_t)_imageHeight * 4) {
		image.Delete();
		return false;
	}

	const ERect bounds = GetRect(text);
	if(bounds.width <= 0 || bounds.height <= 0) {
		image.Delete();
		return false;
	}

	EImage::Resource resource;
	resource.width = bounds.width;
	resource.height = bounds.height;
	resource.bufferSize = (int64_t)resource.width * (int64_t)resource.height * 4;
	resource.buffer = new uint8_t[(size_t)resource.bufferSize];
	memset(resource.buffer, 0, (size_t)resource.bufferSize);

	const uint8_t colorR = color.GetRed();
	const uint8_t colorG = color.GetGreen();
	const uint8_t colorB = color.GetBlue();
	const uint8_t colorA = color.GetAlpha();

	auto blendPixel = [&](int dstIndex, int srcIndex) {
		const uint8_t srcAlpha = (uint8_t)((int)_buffer[(size_t)srcIndex + 3] * (int)colorA / 255);
		if(srcAlpha == 0)
			return;

		const uint8_t srcRed = (uint8_t)((int)_buffer[(size_t)srcIndex + 0] * (int)colorR / 255);
		const uint8_t srcGreen = (uint8_t)((int)_buffer[(size_t)srcIndex + 1] * (int)colorG / 255);
		const uint8_t srcBlue = (uint8_t)((int)_buffer[(size_t)srcIndex + 2] * (int)colorB / 255);

		const uint8_t dstRed = resource.buffer[(size_t)dstIndex + 0];
		const uint8_t dstGreen = resource.buffer[(size_t)dstIndex + 1];
		const uint8_t dstBlue = resource.buffer[(size_t)dstIndex + 2];
		const uint8_t dstAlpha = resource.buffer[(size_t)dstIndex + 3];
		const int outAlpha = srcAlpha + ((int)dstAlpha * (255 - srcAlpha) / 255);
		if(outAlpha <= 0)
			return;

		resource.buffer[(size_t)dstIndex + 0] = (uint8_t)(((int)srcRed * srcAlpha + (int)dstRed * dstAlpha * (255 - srcAlpha) / 255) / outAlpha);
		resource.buffer[(size_t)dstIndex + 1] = (uint8_t)(((int)srcGreen * srcAlpha + (int)dstGreen * dstAlpha * (255 - srcAlpha) / 255) / outAlpha);
		resource.buffer[(size_t)dstIndex + 2] = (uint8_t)(((int)srcBlue * srcAlpha + (int)dstBlue * dstAlpha * (255 - srcAlpha) / 255) / outAlpha);
		resource.buffer[(size_t)dstIndex + 3] = (uint8_t)outAlpha;
	};

	EPoint pos;
	const int lineStep = GetLineHeight();
	int last = 0;

	for(int i = 0; i < text.GetLength(); i++) {
		if(text[i] == '\n') {
			pos.x = 0;
			pos.y += lineStep;
			continue;
		}

		int index = 0;
		if((uint8_t)text[i] <= 0x7f) {
			index = (int)(uint8_t)text[i];
		} else if((uint8_t)text[i] >= 0xc2 && (uint8_t)text[i] <= 0xdf && i + 1 < text.GetLength()) {
			index = GetIndexFromHash((0) | (0 << 8) | ((uint8_t)text[i + 1] << 16) | ((uint8_t)text[i] << 24));
			i += 1;
		} else if((uint8_t)text[i] >= 0xe0 && (uint8_t)text[i] <= 0xef && i + 2 < text.GetLength()) {
			index = GetIndexFromHash((0) | ((uint8_t)text[i + 2] << 8) | ((uint8_t)text[i + 1] << 16) | ((uint8_t)text[i] << 24));
			i += 2;
		} else if((uint8_t)text[i] >= 0xf0 && (uint8_t)text[i] <= 0xf4 && i + 3 < text.GetLength()) {
			index = GetIndexFromHash(((uint8_t)text[i + 3]) | ((uint8_t)text[i + 2] << 8) | ((uint8_t)text[i + 1] << 16) | ((uint8_t)text[i] << 24));
			i += 3;
		} else {
			last = 0;
			continue;
		}

		if(index < 0
			|| index >= (int)_rects.size()
			|| index >= (int)_offsets.size()
			|| index >= (int)_advances.size()
			|| last < 0
			|| last >= (int)_has_kern.size()) {
			last = 0;
			continue;
		}

		int kerning = 0;
		if(_has_kern[last]) {
			auto k = _kernings.find(std::make_pair(last, index));
			if(k != _kernings.end())
				kerning = k->second;
		}

		const int dstX = pos.x + _offsets[index].x + kerning - bounds.x;
		const int dstY = pos.y + _offsets[index].y - bounds.y;
		for(int y = 0; y < _rects[index].height; y++) {
			const int drawY = dstY + y;
			if(drawY < 0 || drawY >= resource.height)
				continue;
			const int srcY = _rects[index].y + y;
			if(srcY < 0 || srcY >= _imageHeight)
				continue;
			for(int x = 0; x < _rects[index].width; x++) {
				const int drawX = dstX + x;
				if(drawX < 0 || drawX >= resource.width)
					continue;
				const int srcX = _rects[index].x + x;
				if(srcX < 0 || srcX >= _imageWidth)
					continue;

				const int srcIndex = (srcY * _imageWidth + srcX) * 4;
				const int dstIndex = (drawY * resource.width + drawX) * 4;
				blendPixel(dstIndex, srcIndex);
			}
		}

		pos.x += _advances[index];
		last = index;
	}

	if(!image.New(resource)) {
		delete [] resource.buffer;
		resource.buffer = nullptr;
		return false;
	}

	delete [] resource.buffer;
	resource.buffer = nullptr;
	return true;
}



void EFont::Draw (const EString& text, int x, int y, float alpha) {
if(text.IsEmpty() || _rects.size() < ((1 << 7) - 1))
	return;

#if defined(DRAGON_TEST)
	// Validation reports can assert text placement, so record the final draw bounds here.
	const ERect drawRect = GetRect(text).Offset(x, y);
	if(drawRect.width > 0 && drawRect.height > 0)
		ESystem::ReportTextDraw(text, drawRect);
#endif

	EPoint pos(x, y);
	int lineStep = GetLineHeight();
	int last = 0; // Last character rendered (used for kernings)

		for(int i = 0; i < text.GetLength(); i++) {
			if(text[i] != '\n') {
				int index = 0;
				// Find the index
				if((uint8_t)text[i] <= 0x7f) {
				// ASCII characters are always indexed exactly
				index = (int)(uint8_t)text[i];
		} else if((uint8_t)text[i] >= 0xc2 && (uint8_t)text[i] <= 0xdf && i + 1 < text.GetLength()) {
			// Find the index of two-byte non-ASCII characters
			index = GetIndexFromHash((0) | (0 << 8) | ((uint8_t)text[i + 1] << 16) | ((uint8_t)text[i] << 24));
			i += 1;
		} else if((uint8_t)text[i] >= 0xe0 && (uint8_t)text[i] <= 0xef && i + 2 < text.GetLength()) {
			// Find the index of three-byte non-ASCII characters
			index = GetIndexFromHash((0) | ((uint8_t)text[i + 2] << 8) | ((uint8_t)text[i + 1] << 16) | ((uint8_t)text[i] << 24));
			i += 2;
			} else if((uint8_t)text[i] >= 0xf0 && (uint8_t)text[i] <= 0xf4 && i + 3 < text.GetLength()) {
				// Find the index of four-byte non-ASCII characters
				index = GetIndexFromHash(((uint8_t)text[i + 3]) | ((uint8_t)text[i + 2] << 8) | ((uint8_t)text[i + 1] << 16) | ((uint8_t)text[i] << 24));
				i += 3;
			} else {
				ESystem::Debug("Unknown character (0x%x) found while drawing font!\n", (uint8_t)text[i]);
				last = 0;
				continue;
			}

			if(index < 0
				|| index >= (int)_rects.size()
				|| index >= (int)_offsets.size()
				|| index >= (int)_advances.size()
				|| last < 0
				|| last >= (int)_has_kern.size()) {
				last = 0;
				continue;
			}
			
			// Draw the character adjusting for a possible kerning then advance the x position
			int kerning = 0;
		if(_has_kern[last]) {
			auto k = _kernings.find(std::make_pair(last, index));
			if(k != _kernings.end())
				kerning = k->second;
		}
		_image.Draw(_rects[index], pos.x + _offsets[index].x + kerning, pos.y + _offsets[index].y, alpha);
		pos.x += _advances[index];
			last = index;
		} else {
			// If a new line, reset the x position and move down the height
			pos.x = x;
			pos.y += lineStep;
		}
	}
}



bool EFont::Resource::New (const EString& name) {
if(chars) {
	delete [] chars;
	chars = nullptr;
}
if(hash) {
	delete [] hash;
	hash = nullptr;
}
if(kernings) {
	delete [] kernings;
	kernings = nullptr;
}
if(buffer) {
	delete [] buffer;
	buffer = nullptr;
}
height = 0;
base = 0;
charCount = 0;
hashCount = 0;
kernCount = 0;
imageWidth = 0;
	imageHeight = 0;
	bufferSize = 0;

const int64_t resourceSize = ESystem::ResourceSize(name + ".fnt");
const int64_t resourceHeaderSize = sizeof(height) + sizeof(base) + sizeof(charCount) + sizeof(hashCount) + sizeof(kernCount) + sizeof(imageWidth) + sizeof(imageHeight) + sizeof(bufferSize);
	if(resourceSize >= resourceHeaderSize && FitsSizeTFromInt64(resourceSize)) {
		std::unique_ptr<uint8_t[]> resourceBuffer(new uint8_t[(size_t)resourceSize]);
		if(ESystem::ResourceRead(name + ".fnt", resourceBuffer.get(), resourceSize)) {
		int64_t offset = 0;
		memcpy(&height, resourceBuffer.get() + offset, sizeof(height));
		offset += sizeof(height);
		memcpy(&base, resourceBuffer.get() + offset, sizeof(base));
		offset += sizeof(base);
		memcpy(&charCount, resourceBuffer.get() + offset, sizeof(charCount));
		offset += sizeof(charCount);
		memcpy(&hashCount, resourceBuffer.get() + offset, sizeof(hashCount));
		offset += sizeof(hashCount);
		memcpy(&kernCount, resourceBuffer.get() + offset, sizeof(kernCount));
		offset += sizeof(kernCount);
		memcpy(&imageWidth, resourceBuffer.get() + offset, sizeof(imageWidth));
		offset += sizeof(imageWidth);
		memcpy(&imageHeight, resourceBuffer.get() + offset, sizeof(imageHeight));
		offset += sizeof(imageHeight);
		memcpy(&bufferSize, resourceBuffer.get() + offset, sizeof(bufferSize));
		offset += sizeof(bufferSize);

		int64_t expectedImageBufferSize = 0;
		int64_t charBytes = 0;
		int64_t hashBytes = 0;
		int64_t kernBytes = 0;
		int64_t payloadSize = 0;
		if(charCount >= 0
			&& hashCount >= 0
				&& kernCount >= 0
				&& bufferSize > 0
				&& FitsSizeTFromInt64(bufferSize)
				&& ComputeRgbaImageBufferSize(imageWidth, imageHeight, expectedImageBufferSize)
				&& expectedImageBufferSize == bufferSize
			&& SafeMulNonNegativeInt64((int64_t)charCount, (int64_t)sizeof(Char), charBytes)
			&& SafeMulNonNegativeInt64((int64_t)hashCount, (int64_t)sizeof(uint32_t), hashBytes)
			&& SafeMulNonNegativeInt64((int64_t)kernCount, (int64_t)sizeof(uint64_t), kernBytes)
			&& SafeAddNonNegativeInt64(charBytes, hashBytes, payloadSize)
			&& SafeAddNonNegativeInt64(payloadSize, kernBytes, payloadSize)
			&& SafeAddNonNegativeInt64(payloadSize, bufferSize, payloadSize)
			&& payloadSize <= resourceSize - offset) {
			if(charCount > 0) {
				chars = new Char[(size_t)charCount];
				memcpy(chars, resourceBuffer.get() + offset, (size_t)charBytes);
				offset += charBytes;
			}

			if(hashCount > 0) {
				hash = new uint32_t[(size_t)hashCount];
				memcpy(hash, resourceBuffer.get() + offset, (size_t)hashBytes);
				offset += hashBytes;
			}

			if(kernCount > 0) {
				kernings = new uint64_t[(size_t)kernCount];
				memcpy(kernings, resourceBuffer.get() + offset, (size_t)kernBytes);
				offset += kernBytes;
			}

			if(bufferSize > 0) {
				buffer = new uint8_t[(size_t)bufferSize];
				memcpy(buffer, resourceBuffer.get() + offset, (size_t)bufferSize);
			}

			return true;
		}

		if(chars) { delete [] chars; chars = nullptr; }
		if(hash) { delete [] hash; hash = nullptr; }
		if(kernings) { delete [] kernings; kernings = nullptr; }
		if(buffer) { delete [] buffer; buffer = nullptr; }
		height = 0;
		base = 0;
		charCount = 0;
		hashCount = 0;
		kernCount = 0;
		imageWidth = 0;
		imageHeight = 0;
		bufferSize = 0;
	}
}


	EString fallbackPath;
	fallbackPath.Format("Fonts/%s.txt", (const char*)name);
	return NewFromFile(fallbackPath);
}



// This function converts a UTF-32 (or Unicode) character to UTF-8, then converts it into a hash value combining up to 4 bytes into a
// single 32-bit value.  This is used becuase the Font engine accepts UTF-8 strings, and to convert the UTF-8 strings to a normal unicode
// lookup would take additional math, so this removes one layer of calculations needed when non-ASCII characters are encountered.
static uint32_t ConvertUnicodeToHash (uint32_t c) {
if(c < (1 << 7)) { // 1-byte ASCII characters
	return c;										// 0xxxxxxx
} else if(c < (1 << 11)) { // 2-byte characters
	uint8_t utf8[2];
	utf8[0] = (uint8_t)((c >> 6) | 0xc0);			// 110xxxxx
	utf8[1] = (uint8_t)((c & 0x3f) | 0x80);			// 10xxxxxx
	return ((0) | (0 << 8) | ((uint8_t)utf8[1] << 16) | ((uint8_t)utf8[0] << 24));
} else if(c < (1 << 16)) { // 3-byte characters
	uint8_t utf8[3];
	utf8[0] = (uint8_t)((c >> 12) | 0xe0);			// 1110xxxx
	utf8[1] = (uint8_t)(((c >> 6) & 0x3f) | 0x80);	// 10xxxxxx
	utf8[2] = (uint8_t)((c & 0x3f) | 0x80);			// 10xxxxxx
	return ((0) | ((uint8_t)utf8[2] << 8) | ((uint8_t)utf8[1] << 16) | ((uint8_t)utf8[0] << 24));
} else if(c < (1 << 21)) { // 4-byte characters
	uint8_t utf8[4];
	utf8[0] = (uint8_t)(((c >> 18)) | 0xF0);		// 11110xxx
	utf8[1] = (uint8_t)(((c >> 12) & 0x3F) | 0x80);	// 10xxxxxx
	utf8[2] = (uint8_t)(((c >> 6) & 0x3F) | 0x80);	// 10xxxxxx
	utf8[3] = (uint8_t)((c & 0x3F) | 0x80);			// 10xxxxxx
	return (((uint8_t)utf8[3]) | ((uint8_t)utf8[2] << 8) | ((uint8_t)utf8[1] << 16) | ((uint8_t)utf8[0] << 24));
}
return 0;
}



// Parse BMFont-style text metadata plus its referenced page image into the
// runtime font resource layout used by Dragon Alpha.
bool EFont::Resource::NewFromFile (const EString& path) {
int64_t fileSize = ESystem::ResourceSizeFromFile(path);
if(fileSize <= 0)
	return false;

std::unique_ptr<uint8_t[]> fileBuffer(new uint8_t[fileSize + 2]);
if(!ESystem::ResourceReadFromFile(path, fileBuffer.get(), fileSize))
	return false;
//for(int64_t i = 0; i < fileSize; i++)
//	if(!EString::isprint((char)fileBuffer[i]))
//		fileBuffer[i] = '\0';
fileBuffer[fileSize + 0] = '\0';
fileBuffer[fileSize + 1] = '\0';

std::vector<Char> charsList((1 << 7) - 1);
std::vector<uint32_t> hashList;
std::vector<uint64_t> kerningsList;

for(char* line = (char*)fileBuffer.get(); line != nullptr && *line != '\0'; line++) {
	
	// This tag holds information common to all characters.
	if(EString::strnicmp("common ", line, 7) == 0) {
		
		// This is the distance in pixels from the absolute top of the line to the next line.
		if((line = EString::strinext(line, "lineHeight=")) != nullptr)
			height = (int16_t)EString::strtoi(line, &line, 10);
		
		// The number of pixels from the absolute top of the line to the base of the characters (minus the spacing between lines).
		if((line = EString::strinext(line, "base=")) != nullptr)
			base = (int16_t)EString::strtoi(line, &line, 10);
		
	// This tag gives the name of a texture file. There is one for each page in the font.
	} else if(EString::strnicmp("page ", line, 5) == 0) {
		
		// The texture file name.
		if((line = EString::strinext(line, "file=\"")) != nullptr) {
			char* end = EString::strstr(line, "\"");
			if(end != nullptr) {
				*end = '\0';
				EImage::Resource imageResource;
				if(imageResource.NewFromFile(EString(path).TrimToDirectory() + line) == false) {
					ESystem::Debug("Failed to read src image for font \"%s\"!\n", (const char*)path);
					return false;
				}
				imageWidth = imageResource.width;
				imageHeight = imageResource.height;
				bufferSize = imageResource.bufferSize;
				buffer = imageResource.buffer;
				imageResource.buffer = nullptr; // This prevents the resource from being deleted
				*end = '\"';
			}
		}
		
	// This tag describes one character in the font. There is one for each included character in the font.
	} else if(EString::strnicmp("char ", line, 5) == 0) {
		
		// The character id.
		int index;
		if((line = EString::strinext(line, "id=")) == nullptr || (index = EString::strtoi(line, &line, 10)) <= 0)
			continue;
		
		Char glyph;
		
		// The left position of the character image in the texture.
		if((line = EString::strinext(line, "x=")) != nullptr)
			glyph.srcX = (int16_t)EString::strtoi(line, &line, 10);
		
		// The top position of the character image in the texture.
		if((line = EString::strinext(line, "y=")) != nullptr)
			glyph.srcY = (int16_t)EString::strtoi(line, &line, 10);
		
		// The width of the character image in the texture (note that some characters have a width of 0).
		if((line = EString::strinext(line, "width=")) != nullptr)
		   glyph.srcWidth = (int16_t)EString::strtoi(line, &line, 10);
		
		// The height of the character image in the texture (note that some characters have a height of 0).
		if((line = EString::strinext(line, "height=")) != nullptr)
		   glyph.srcHeight = (int16_t)EString::strtoi(line, &line, 10);
		
		// How much the current x position should be offset when copying the image from the texture to the screen.
		if((line = EString::strinext(line, "xoffset=")) != nullptr)
			glyph.xOffset = (int16_t)EString::strtoi(line, &line, 10);
		
		// How much the current y position should be offset when copying the image from the texture to the screen.
		if((line = EString::strinext(line, "yoffset=")) != nullptr)
			glyph.yOffset = (int16_t)EString::strtoi(line, &line, 10);
		
		// How much the current position should be advanced after drawing the character.
		if((line = EString::strinext(line, "xadvance=")) != nullptr)
			glyph.xAdvance = (int16_t)EString::strtoi(line, &line, 10);
		
		// Add the character to the lists, the first 127 characters (ASCII) are always present
		if(index < (1 << 7)) { // ASCII characters
			charsList[index] = glyph;
		} else if (index < (1 << 21)) { // Multi-Byte UTF-8 character
			charsList.push_back(glyph);
			hashList.push_back(ConvertUnicodeToHash((uint32_t)index));
		}
		
	// The kerning information is used to adjust the distance between certain characters, e.g. some characters should be placed closer to each other than others.
	} else if(EString::strnicmp("kerning ", line, 8) == 0) {
		
		// The first character id.
		int first;
		if((line = EString::strinext(line, "first=")) == nullptr || (first = EString::strtoi(line, &line, 10)) <= 0)
			continue;
		
		// The second character id.
		int second;
		if((line = EString::strinext(line, "second=")) == nullptr || (second = EString::strtoi(line, &line, 10)) <= 0)
			continue;
		
		// How much the x position should be adjusted when drawing the second character immediately following the first.
		int amount;
		if((line = EString::strinext(line, "amount=")) == nullptr || (amount = EString::strtoi(line, &line, 10)) == 0)
			continue;
		
		// "first" and "second are converted into hash index values (which remain the same value for ASCII characters).
		// Since Unicode characters only use 21 bits of information, "first" and "second" are converted to 24-bit values.
		// This leaves 16-bits remaining for the actual kerning value.
		kerningsList.push_back(((uint64_t)ConvertUnicodeToHash((uint32_t)first) | ((uint64_t)ConvertUnicodeToHash((uint32_t)second) << 24) | ((uint64_t)amount << 48)));
	}
	
	// Advance to the end of the line
	while(line != nullptr && *line != '\0' && *line != '\n')
		line++;
}

// Copy the chars list to the resrouce chars
if(!charsList.empty()) {
	charCount = (int32_t)charsList.size();
	chars = new Char[charCount];
	for(int i = 0; i < charCount; i++)
		chars[i] = charsList[i];
}

// Copy the hash list to the resource hash
if(!hashList.empty()) {
	hashCount = (int32_t)hashList.size();
	hash = new uint32_t[hashCount];
	for(int i = 0; i < hashCount; i++)
		hash[i] = hashList[i];
}

// Copy the kerning list to the resource kernings
if(!kerningsList.empty()) {
	kernCount = (int32_t)kerningsList.size();
	kernings = new uint64_t[kernCount];
	for(int i = 0; i < kernCount; i++)
		kernings[i] = kerningsList[i];
}

return true;
}



bool EFont::Resource::Write (const EString& name) {
	if(charCount < 0 || hashCount < 0 || kernCount < 0 || bufferSize < 0) {
		ESystem::Debug("Invalid font resource counts for \"%s\".\n", (const char*)name);
		return false;
	}
	if((charCount > 0 && chars == nullptr)
		|| (hashCount > 0 && hash == nullptr)
		|| (kernCount > 0 && kernings == nullptr)
		|| (bufferSize > 0 && buffer == nullptr)) {
		ESystem::Debug("Invalid font resource buffers for \"%s\".\n", (const char*)name);
		return false;
	}

	int64_t charsBytes = 0;
	int64_t hashBytes = 0;
	int64_t kernBytes = 0;
	int64_t bitmapBytes = 0;
	if(!SafeMulNonNegativeInt64((int64_t)charCount, (int64_t)sizeof(Char), charsBytes)
		|| !SafeMulNonNegativeInt64((int64_t)hashCount, (int64_t)sizeof(uint32_t), hashBytes)
		|| !SafeMulNonNegativeInt64((int64_t)kernCount, (int64_t)sizeof(uint64_t), kernBytes)
		|| !SafeMulNonNegativeInt64((int64_t)bufferSize, (int64_t)sizeof(uint8_t), bitmapBytes)) {
		ESystem::Debug("Font resource size overflow for \"%s\".\n", (const char*)name);
		return false;
	}

	int64_t resourceSize = sizeof(height) + sizeof(base) + sizeof(charCount) + sizeof(hashCount) + sizeof(kernCount) + sizeof(imageWidth) + sizeof(imageHeight) + sizeof(bufferSize);
	if(!SafeAddNonNegativeInt64(resourceSize, charsBytes, resourceSize)
		|| !SafeAddNonNegativeInt64(resourceSize, hashBytes, resourceSize)
		|| !SafeAddNonNegativeInt64(resourceSize, kernBytes, resourceSize)
		|| !SafeAddNonNegativeInt64(resourceSize, bitmapBytes, resourceSize)
		|| resourceSize <= 0
		|| FitsSizeTFromInt64(resourceSize) == false) {
		ESystem::Debug("Font resource total size overflow for \"%s\".\n", (const char*)name);
		return false;
	}

	std::unique_ptr<uint8_t[]> resourceBuffer(new (std::nothrow) uint8_t[(size_t)resourceSize]);
	if(resourceBuffer.get() == nullptr) {
		ESystem::Debug("Failed to allocate font resource buffer for \"%s\".\n", (const char*)name);
		return false;
	}

	size_t offset = 0;
	std::memcpy(resourceBuffer.get() + offset, &height, sizeof(height));
	offset += sizeof(height);
	std::memcpy(resourceBuffer.get() + offset, &base, sizeof(base));
	offset += sizeof(base);
	std::memcpy(resourceBuffer.get() + offset, &charCount, sizeof(charCount));
	offset += sizeof(charCount);
	std::memcpy(resourceBuffer.get() + offset, &hashCount, sizeof(hashCount));
	offset += sizeof(hashCount);
	std::memcpy(resourceBuffer.get() + offset, &kernCount, sizeof(kernCount));
	offset += sizeof(kernCount);
	std::memcpy(resourceBuffer.get() + offset, &imageWidth, sizeof(imageWidth));
	offset += sizeof(imageWidth);
	std::memcpy(resourceBuffer.get() + offset, &imageHeight, sizeof(imageHeight));
	offset += sizeof(imageHeight);
	std::memcpy(resourceBuffer.get() + offset, &bufferSize, sizeof(bufferSize));
	offset += sizeof(bufferSize);

	if(charsBytes > 0) {
		std::memcpy(resourceBuffer.get() + offset, chars, (size_t)charsBytes);
		offset += (size_t)charsBytes;
	}
	if(hashBytes > 0) {
		std::memcpy(resourceBuffer.get() + offset, hash, (size_t)hashBytes);
		offset += (size_t)hashBytes;
	}
	if(kernBytes > 0) {
		std::memcpy(resourceBuffer.get() + offset, kernings, (size_t)kernBytes);
		offset += (size_t)kernBytes;
	}
	if(bitmapBytes > 0) {
		std::memcpy(resourceBuffer.get() + offset, buffer, (size_t)bitmapBytes);
		offset += (size_t)bitmapBytes;
	}
	if(offset != (size_t)resourceSize) {
		ESystem::Debug("Font resource write size mismatch for \"%s\".\n", (const char*)name);
		return false;
	}
	return ESystem::ResourceWrite(name + ".fnt", resourceBuffer.get(), resourceSize);
}

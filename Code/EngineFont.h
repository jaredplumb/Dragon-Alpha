/**
 * @file EngineFont.h
 * @brief Font loading, glyph metrics, and text rendering interfaces.
 */
#ifndef ENGINE_FONT_H_
#define ENGINE_FONT_H_

#include "EngineTypes.h"
#include "EngineImage.h"
#include <map>
#include <unordered_map>
#include <vector>

class EFont {
public:
	class Resource;

	inline EFont (): _height(0), _base(0), _imageWidth(0), _imageHeight(0) {}
	inline EFont (const Resource& resource): _height(0), _base(0), _imageWidth(0), _imageHeight(0) { New(resource); }
	inline EFont (const EString& resource): _height(0), _base(0), _imageWidth(0), _imageHeight(0) { New(Resource(resource)); }

	inline bool New (const EString& resource) { return New(Resource(resource)); }
	bool New (const Resource& resource);
	void Delete (); // Legacy API alias.

	/// Returns a rect containing the actual rendered pixels, x and y may not be 0 depending on how the text renders with offsets and kernings.
	ERect GetRect (const EString& text) const;

	inline int GetWidth (const EString& text) const { return GetRect(text).width; }
	inline int GetWidth (const EString& text, uint32_t count) const {
		if(count == 0)
			return GetWidth(text);
		const uint32_t length = text.GetLength();
		if(count >= length)
			return GetWidth(text);
		EString partial = text;
		partial[(int)count] = '\0';
		return GetWidth(partial);
	}
	inline int GetHeight (const EString& text) const { return GetRect(text).height; }
	inline int GetHeight (const EString& text, uint32_t count) const {
		if(count == 0)
			return GetHeight(text);
		const uint32_t length = text.GetLength();
		if(count >= length)
			return GetHeight(text);
		EString partial = text;
		partial[(int)count] = '\0';
		return GetHeight(partial);
	}
	inline int GetHeight () const { return _height; } // Legacy API alias.
	inline int GetOffset () const { return 0; } // Modern font pipeline does not expose global offset.
	inline void SetOffset (int) {} // Kept for legacy-callsite compatibility.

	/// This is the distance in pixels from the absolute top of the line to the next line.
	int GetLineHeight () const;

	/// This is the number of pixels from the absolute top of the line to the base of the characters.
	inline int GetBaseHeight () const { return _base; }

	inline bool IsEmpty () const { return _image.IsEmpty(); }

	/// Builds a standalone image containing the rendered text using this font's current glyph data.
	bool NewImageFromText (const EString& text, EImage& image, const EColor& color = EColor::WHITE) const;
	void Draw (const EString& text, int x, int y, float alpha = 1.0f);
	inline void Draw (const EString& text, int x, int y, uint8_t alpha) { Draw(text, x, y, (float)alpha / 255.0f); }
	inline void Draw (const EString& text, const EPoint& loc, float alpha = 1.0f) { Draw(text, loc.x, loc.y, alpha); }

	struct Resource {
		struct Char {
			int16_t srcX;
			int16_t srcY;
			int16_t srcWidth;
			int16_t srcHeight;
			int16_t xOffset;
			int16_t yOffset;
			int16_t xAdvance;
			inline Char (): srcX(0), srcY(0), srcWidth(0), srcHeight(0), xOffset(0), yOffset(0), xAdvance(0) {}
		};
		int16_t height;
		int16_t base;
		int32_t charCount;
		int32_t hashCount;
		int32_t kernCount;
		int32_t imageWidth;
		int32_t imageHeight;
		int64_t bufferSize;
		Char* chars;
		uint32_t* hash;
		uint64_t* kernings;
		uint8_t* buffer;
		inline Resource (): height(0), base(0), charCount(0), hashCount(0), kernCount(0), imageWidth(0), imageHeight(0), bufferSize(0), chars(nullptr), hash(nullptr), kernings(nullptr), buffer(nullptr) {}
		inline Resource (const EString& name): height(0), base(0), charCount(0), hashCount(0), kernCount(0), imageWidth(0), imageHeight(0), bufferSize(0), chars(nullptr), hash(nullptr), kernings(nullptr), buffer(nullptr) { New(name); }
		inline ~Resource () {
			height = 0;
			base = 0;
			charCount = 0;
			hashCount = 0;
			kernCount = 0;
			imageWidth = 0;
			imageHeight = 0;
			bufferSize = 0;
			if(chars) delete [] chars;
			if(hash) delete [] hash;
			if(kernings) delete [] kernings;
			if(buffer) delete [] buffer;
			chars = nullptr;
			hash = nullptr;
			kernings = nullptr;
			buffer = nullptr;
		}
		bool New (const EString& name);
		bool NewFromFile (const EString& path);
		bool Write (const EString& name);
	};

private:
	int _height;
	int _base;
	std::vector<ERect> _rects;
	std::vector<EPoint> _offsets;
	std::vector<int> _advances;
	std::vector<bool> _has_kern;
	std::unordered_map<uint32_t, int> _hash;
	std::map<std::pair<int, int>, int> _kernings;
	std::vector<uint8_t> _buffer;
	int _imageWidth;
	int _imageHeight;
	EImage _image;

	inline int GetIndexFromHash (uint32_t hash) const {
		std::unordered_map<uint32_t, int>::const_iterator i = _hash.find(hash);
		return i != _hash.end() ? i->second : -1;
	}
};

#endif // ENGINE_FONT_H_

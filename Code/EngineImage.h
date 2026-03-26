/**
 * @file EngineImage.h
 * @brief Image abstraction and draw-surface interfaces for runtime rendering.
 */
#ifndef ENGINE_IMAGE_H_
#define ENGINE_IMAGE_H_

#include "EngineTypes.h"
#include "EngineSystem.h"
#include <cstdint>
#include <memory>

class EImage {
public:
	struct Vertex;
	struct Resource;
	
	EImage ();
	EImage (const Resource& resource);
	EImage (const EString& resource);
	EImage (const EColor& color);
	~EImage ();
	
	bool New (const Resource& resource);
	inline bool New (const EString& resource) { return New(Resource(resource)); }
	bool New (const EColor& color = EColor::WHITE);
	void Delete ();
	
	int GetWidth () const;
	int GetHeight () const;
	ERect GetRect () const; // Returns a rect using a 0,0 location and GetWdith and GetHeight (utility)
	bool IsEmpty () const;
	
	void Draw ();
	void Draw (const ERect& src, const ERect& dst, const EColor& color = EColor::WHITE);
	void DrawLine (const EPoint& a, const EPoint& b, int width, const EColor& color = EColor::WHITE);
	void DrawEllipse (const ERect& dst, const EColor& color = EColor::WHITE, const int sides = 45);
	void DrawVertices (const std::vector<Vertex>& vertices, const std::vector<uint16_t>& indices);
	
	// These are inline overload functions to allow for more drawing options
	inline void Draw (int x, int y, float alpha = 1.0f)								{ Draw(GetRect(), GetRect().Offset(x, y), EColor(0xff, 0xff, 0xff, _NormalizeAlpha(alpha))); }
	inline void Draw (const EPoint& loc, float alpha = 1.0f)						{ Draw(loc.x, loc.y, alpha); }
	inline void Draw (const ERect& src, int x, int y, float alpha = 1.0f)			{ Draw(src, ERect(x, y, src.width, src.height), EColor(0xff, 0xff, 0xff, _NormalizeAlpha(alpha))); }
	inline void Draw (const ERect& src, int x, int y, uint8_t alpha, bool fliphorz = false, bool flipvert = false) {
		const ERect dst(x, y, src.width, src.height);
		const EColor color(0xff, 0xff, 0xff, alpha);
		if(!fliphorz && !flipvert) {
			Draw(src, dst, color);
			return;
		}
		if(IsEmpty() || GetWidth() <= 0 || GetHeight() <= 0)
			return;
		float u0 = (float)src.x / (float)GetWidth();
		float v0 = (float)src.y / (float)GetHeight();
		float u1 = (float)(src.x + src.width) / (float)GetWidth();
		float v1 = (float)(src.y + src.height) / (float)GetHeight();
		if(fliphorz)
			E_SWAP(u0, u1);
		if(flipvert)
			E_SWAP(v0, v1);
		std::vector<Vertex> vertices = {
			{{(float)dst.x, (float)dst.y}, {(uint8_t)color.GetRed(), (uint8_t)color.GetGreen(), (uint8_t)color.GetBlue(), (uint8_t)color.GetAlpha()}, {u0, v0}},
			{{(float)(dst.x + dst.width), (float)dst.y}, {(uint8_t)color.GetRed(), (uint8_t)color.GetGreen(), (uint8_t)color.GetBlue(), (uint8_t)color.GetAlpha()}, {u1, v0}},
			{{(float)(dst.x + dst.width), (float)(dst.y + dst.height)}, {(uint8_t)color.GetRed(), (uint8_t)color.GetGreen(), (uint8_t)color.GetBlue(), (uint8_t)color.GetAlpha()}, {u1, v1}},
			{{(float)dst.x, (float)(dst.y + dst.height)}, {(uint8_t)color.GetRed(), (uint8_t)color.GetGreen(), (uint8_t)color.GetBlue(), (uint8_t)color.GetAlpha()}, {u0, v1}},
		};
		std::vector<uint16_t> indices = {0, 1, 2, 0, 2, 3};
		DrawVertices(vertices, indices);
	}
	inline void Draw (const ERect& src, const EPoint& loc, uint8_t alpha = 0xff, bool fliphorz = false, bool flipvert = false) { Draw(src, loc.x, loc.y, alpha, fliphorz, flipvert); }
	inline void Draw (const ERect& dst, float alpha = 1.0f)							{ Draw(GetRect(), dst, EColor(0xff, 0xff, 0xff, (uint8_t)(alpha * 255.0f))); }
	inline void DrawRect (const ERect& dst, const EColor& color = EColor::WHITE)	{ Draw(GetRect(), dst, color); }
	
	struct Vertex {
		float xy[2];
		uint8_t rgba[4];
		float uv[2];
	};
	
	struct Resource {
		int32_t width;
		int32_t height;
		int64_t bufferSize;
		uint8_t* buffer;
		inline Resource (): width(0), height(0), bufferSize(0), buffer(nullptr) {}
		inline Resource (const EString& name): width(0), height(0), bufferSize(0), buffer(nullptr) { New(name); }
		inline ~Resource () { width = 0; height = 0; bufferSize = 0; if(buffer) delete [] buffer; buffer = nullptr; }
		bool New (const EString& name);
		bool NewFromFile (const EString& path);
		bool Write (const EString& name);
	};
	
private:
	static inline uint8_t _NormalizeAlpha (float alpha) {
		if(alpha <= 0.0f)
			return 0;
		if(alpha <= 1.0f)
			return (uint8_t)(alpha * 255.0f);
		if(alpha >= 255.0f)
			return 0xff;
		return (uint8_t)alpha;
	}

	void UploadStagedTextureIfReady ();

	struct Private;
	std::unique_ptr<Private> _data;
};

#endif // ENGINE_IMAGE_H_

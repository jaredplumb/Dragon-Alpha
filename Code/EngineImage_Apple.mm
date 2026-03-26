/**
 * @file EngineImage_Apple.mm
 * @brief Apple-specific image decoding and texture upload implementation.
 */
#include "EngineImage.h"
#include "EngineSystem.h"
#ifdef __APPLE__
#include <cmath>
#include <limits>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>



// These are defined in EngineSystem_Apple.mm
extern id<MTLDevice> DEVICE;
extern id<MTLRenderCommandEncoder> RENDER;


struct EImage::Private {
	int width, height;
	ERect src, dst; // Temp data used by the last draw call
	EColor color; // Temp data used by the last draw call
	id<MTLTexture> texture;
	std::vector<uint8_t> stagedPixels;
	std::vector<Vertex> vertices;
	id<MTLBuffer> vertexObject;
	std::vector<uint16_t> indices;
	id<MTLBuffer> indexObject;
	inline Private (): width(0), height(0) {}
	inline ~Private () {
#if !__has_feature(objc_arc)
		if(indexObject != nil)
			[indexObject release];
		if(vertexObject != nil)
			[vertexObject release];
		if(texture != nil)
			[texture release];
#endif
		indexObject = nil;
		vertexObject = nil;
		texture = nil;
	}
};

void EImage::UploadStagedTextureIfReady () {
	if(_data == nullptr || _data->texture != nil || DEVICE == nil)
		return;
	if(_data->width <= 0 || _data->height <= 0 || _data->stagedPixels.empty())
		return;
	if((int64_t)_data->width > std::numeric_limits<int64_t>::max() / 4)
		return;

	const int64_t bytesPerRow64 = (int64_t)_data->width * 4;
	if((int64_t)_data->height > std::numeric_limits<int64_t>::max() / bytesPerRow64)
		return;
	const int64_t expectedBufferSize = bytesPerRow64 * (int64_t)_data->height;
	if(expectedBufferSize <= 0 || expectedBufferSize != (int64_t)_data->stagedPixels.size())
		return;

	const NSUInteger textureWidth = (NSUInteger)_data->width;
	const NSUInteger textureHeight = (NSUInteger)_data->height;
	const NSUInteger bytesPerRow = (NSUInteger)bytesPerRow64;

	MTLTextureDescriptor* textureDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:textureWidth height:textureHeight mipmapped:NO];
	_data->texture = [DEVICE newTextureWithDescriptor:textureDescriptor];
	if(_data->texture == nil)
		return;

	[_data->texture replaceRegion:MTLRegionMake2D(0, 0, textureWidth, textureHeight) mipmapLevel:0 withBytes:_data->stagedPixels.data() bytesPerRow:bytesPerRow];
	_data->stagedPixels.clear();
	_data->stagedPixels.shrink_to_fit();
}



// These are required to be here to satisfy the hidden struct that is pointed to by the unique_ptr
EImage::EImage (): _data(new Private) {}
EImage::EImage (const Resource& resource): _data(new Private) { New(resource); }
EImage::EImage (const EString& resource): _data(new Private) { New(resource); }
EImage::EImage (const EColor& color): _data(new Private) { New(color); }
EImage::~EImage () {}



void EImage::Delete () {
	_data.reset(new Private);
}



bool EImage::New (const Resource& resource) {
	Delete();
	
	if(resource.width <= 0 || resource.height <= 0 || resource.bufferSize <= 0 || resource.buffer == nullptr)
		return false;
	if((int64_t)resource.width > std::numeric_limits<int64_t>::max() / 4)
		return false;
	const int64_t bytesPerRow64 = (int64_t)resource.width * 4;
	if((int64_t)resource.height > std::numeric_limits<int64_t>::max() / bytesPerRow64)
		return false;
	const int64_t expectedBufferSize = bytesPerRow64 * (int64_t)resource.height;
	if(resource.bufferSize != expectedBufferSize)
		return false;
	if(resource.bufferSize < 0 || (uint64_t)resource.bufferSize > (uint64_t)std::numeric_limits<size_t>::max())
		return false;
	
	_data->width = resource.width;
	_data->height = resource.height;
	_data->stagedPixels.assign(resource.buffer, resource.buffer + (size_t)resource.bufferSize);

	UploadStagedTextureIfReady();
	return _data->texture != nil || !_data->stagedPixels.empty();
}



int EImage::GetWidth () const {
	return _data->width;
}



int EImage::GetHeight () const {
	return _data->height;
}



ERect EImage::GetRect () const {
	return (ERect){0, 0, _data->width, _data->height};
}



bool EImage::IsEmpty () const {
	return _data->width == 0 || _data->height == 0;
}



void EImage::Draw () {
	Draw(ERect(0, 0, _data->width, _data->height), ERect(0, 0, _data->width, _data->height), EColor::WHITE);
}



void EImage::Draw (const ERect& src, const ERect& dst, const EColor& color) {
	UploadStagedTextureIfReady();
	if(RENDER == nil || _data->texture == nil)
		return;
	
	if(_data->indices.size() != 6) {
		_data->indices = {0, 1, 2, 1, 2, 3};
#if !__has_feature(objc_arc)
		if(_data->indexObject != nil)
			[_data->indexObject release];
#endif
		_data->indexObject = [DEVICE newBufferWithBytes:_data->indices.data() length:(sizeof(uint16_t) * _data->indices.size()) options:MTLResourceStorageModeShared];
	}
	
	if(_data->vertices.size() != 4 || _data->src != src || _data->dst != dst || _data->color != color) {
		_data->src = src;
		_data->dst = dst;
		_data->color = color;
		_data->vertices.resize(4);
		_data->vertices[0].xy[0] = (float)_data->dst.x;
		_data->vertices[0].xy[1] = (float)_data->dst.y;
		_data->vertices[0].uv[0] = (float)_data->src.x / (float)_data->width;
		_data->vertices[0].uv[1] = (float)_data->src.y / (float)_data->height;
		_data->vertices[0].rgba[0] = color.GetRed();
		_data->vertices[0].rgba[1] = color.GetGreen();
		_data->vertices[0].rgba[2] = color.GetBlue();
		_data->vertices[0].rgba[3] = color.GetAlpha();
		_data->vertices[1].xy[0] = (float)(_data->dst.x + _data->dst.width);
		_data->vertices[1].xy[1] = (float)_data->dst.y;
		_data->vertices[1].uv[0] = (float)(_data->src.x + _data->src.width) / (float)_data->width;
		_data->vertices[1].uv[1] = (float)_data->src.y / (float)_data->height;
		_data->vertices[1].rgba[0] = color.GetRed();
		_data->vertices[1].rgba[1] = color.GetGreen();
		_data->vertices[1].rgba[2] = color.GetBlue();
		_data->vertices[1].rgba[3] = color.GetAlpha();
		_data->vertices[2].xy[0] = (float)_data->dst.x;
		_data->vertices[2].xy[1] = (float)(_data->dst.y + _data->dst.height);
		_data->vertices[2].uv[0] = (float)_data->src.x / (float)_data->width;
		_data->vertices[2].uv[1] = (float)(_data->src.y + _data->src.height) / (float)_data->height;
		_data->vertices[2].rgba[0] = color.GetRed();
		_data->vertices[2].rgba[1] = color.GetGreen();
		_data->vertices[2].rgba[2] = color.GetBlue();
		_data->vertices[2].rgba[3] = color.GetAlpha();
		_data->vertices[3].xy[0] = (float)(_data->dst.x + _data->dst.width);
		_data->vertices[3].xy[1] = (float)(_data->dst.y + _data->dst.height);
		_data->vertices[3].uv[0] = (float)(_data->src.x + _data->src.width) / (float)_data->width;
		_data->vertices[3].uv[1] = (float)(_data->src.y + _data->src.height) / (float)_data->height;
		_data->vertices[3].rgba[0] = color.GetRed();
			_data->vertices[3].rgba[1] = color.GetGreen();
			_data->vertices[3].rgba[2] = color.GetBlue();
			_data->vertices[3].rgba[3] = color.GetAlpha();
#if !__has_feature(objc_arc)
			if(_data->vertexObject != nil)
				[_data->vertexObject release];
#endif
			_data->vertexObject = [DEVICE newBufferWithBytes:_data->vertices.data() length:(sizeof(Vertex) * _data->vertices.size()) options:MTLResourceStorageModeShared];
	}
	
	[RENDER setFragmentTexture:_data->texture atIndex:0];
	[RENDER setVertexBuffer:_data->vertexObject offset:0 atIndex:0];
	[RENDER drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:_data->indices.size() indexType:MTLIndexTypeUInt16 indexBuffer:_data->indexObject indexBufferOffset:0];
}



void EImage::DrawLine (const EPoint& a, const EPoint& b, int width, const EColor& color) {
	UploadStagedTextureIfReady();
	if(RENDER == nil || _data->texture == nil)
		return;

	const EPoint drawA = a;
	const EPoint drawB = b;
	
	if(_data->indices.size() != 6) {
		_data->indices = {0, 1, 2, 1, 2, 3};
#if !__has_feature(objc_arc)
		if(_data->indexObject != nil)
			[_data->indexObject release];
#endif
		_data->indexObject = [DEVICE newBufferWithBytes:_data->indices.data() length:(sizeof(uint16_t) * _data->indices.size()) options:MTLResourceStorageModeShared];
	}
	
	ERect src(drawA.x, drawA.y, width, 0);
	ERect dst(drawB.x, drawB.y, width, 0);
	if(_data->vertices.size() != 4 || _data->src != src || _data->dst != dst || _data->color != color) {
		_data->src = src;
		_data->dst = dst;
		_data->color = color;
		_data->vertices.resize(4);
		float theta = std::atan2f((float)(drawB.y - drawA.y), (float)(drawB.x - drawA.x));
		float tsin = (float)width * 0.5f * std::sinf(theta);
		float tcos = (float)width * 0.5f * std::cosf(theta);
		_data->vertices[0].xy[0] = (float)drawA.x + tsin;
		_data->vertices[0].xy[1] = (float)drawA.y - tcos;
		_data->vertices[0].uv[0] = 0.0f;
		_data->vertices[0].uv[1] = 0.0f;
		_data->vertices[0].rgba[0] = color.GetRed();
		_data->vertices[0].rgba[1] = color.GetGreen();
		_data->vertices[0].rgba[2] = color.GetBlue();
		_data->vertices[0].rgba[3] = color.GetAlpha();
		_data->vertices[1].xy[0] = (float)drawA.x - tsin;
		_data->vertices[1].xy[1] = (float)drawA.y + tcos;
		_data->vertices[1].uv[0] = 1.0f;
		_data->vertices[1].uv[1] = 0.0f;
		_data->vertices[1].rgba[0] = color.GetRed();
		_data->vertices[1].rgba[1] = color.GetGreen();
		_data->vertices[1].rgba[2] = color.GetBlue();
		_data->vertices[1].rgba[3] = color.GetAlpha();
		_data->vertices[2].xy[0] = (float)drawB.x + tsin;
		_data->vertices[2].xy[1] = (float)drawB.y - tcos;
		_data->vertices[2].uv[0] = 0.0f;
		_data->vertices[2].uv[1] = 1.0f;
		_data->vertices[2].rgba[0] = color.GetRed();
		_data->vertices[2].rgba[1] = color.GetGreen();
		_data->vertices[2].rgba[2] = color.GetBlue();
		_data->vertices[2].rgba[3] = color.GetAlpha();
		_data->vertices[3].xy[0] = (float)drawB.x - tsin;
		_data->vertices[3].xy[1] = (float)drawB.y + tcos;
		_data->vertices[3].uv[0] = 1.0f;
		_data->vertices[3].uv[1] = 1.0f;
		_data->vertices[3].rgba[0] = color.GetRed();
			_data->vertices[3].rgba[1] = color.GetGreen();
			_data->vertices[3].rgba[2] = color.GetBlue();
			_data->vertices[3].rgba[3] = color.GetAlpha();
#if !__has_feature(objc_arc)
			if(_data->vertexObject != nil)
				[_data->vertexObject release];
#endif
			_data->vertexObject = [DEVICE newBufferWithBytes:_data->vertices.data() length:(sizeof(Vertex) * _data->vertices.size()) options:MTLResourceStorageModeShared];
	}
	
	[RENDER setFragmentTexture:_data->texture atIndex:0];
	[RENDER setVertexBuffer:_data->vertexObject offset:0 atIndex:0];
	[RENDER drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:_data->indices.size() indexType:MTLIndexTypeUInt16 indexBuffer:_data->indexObject indexBufferOffset:0];
}



void EImage::DrawEllipse (const ERect& dst, const EColor& color, const int sides) {
	UploadStagedTextureIfReady();
	if(RENDER == nil || _data->texture == nil)
		return;

	const ERect drawDst = dst;
	
	if(_data->indices.size() != sides * 3 - 2) {
		_data->indices.resize(sides * 3 - 2);
		for(int i = 2; i < sides; i++) {
			_data->indices[(i - 2) * 3 + 0] = 0;
			_data->indices[(i - 2) * 3 + 1] = i - 1;
			_data->indices[(i - 2) * 3 + 2] = i;
		}
#if !__has_feature(objc_arc)
		if(_data->indexObject != nil)
			[_data->indexObject release];
#endif
		_data->indexObject = [DEVICE newBufferWithBytes:_data->indices.data() length:(sizeof(uint16_t) * _data->indices.size()) options:MTLResourceStorageModeShared];
	}
	
	ERect src(0, 0, _data->width, _data->height);
	if(_data->vertices.size() != sides || _data->src != src || _data->dst != drawDst || _data->color != color) {
		_data->src = src;
		_data->dst = drawDst;
		_data->color = color;
		_data->vertices.resize(sides);
		for(int i = 0; i < sides; i++) {
			float theta = 2.0f * (float)M_PI * (float)i / (float)sides;
			float x = cosf(theta) * (float)drawDst.width * 0.5f;
			float y = sinf(theta) * (float)drawDst.height * 0.5f;
			_data->vertices[i].xy[0] = (float)drawDst.x + (float)drawDst.width * 0.5f + x;
			_data->vertices[i].xy[1] = (float)drawDst.y + (float)drawDst.height * 0.5f + y;
			_data->vertices[i].uv[0] = ((float)drawDst.width * 0.5f + x) / (float)drawDst.width;
			_data->vertices[i].uv[1] = ((float)drawDst.height * 0.5f + y) / (float)drawDst.height;
			_data->vertices[i].rgba[0] = color.GetRed();
			_data->vertices[i].rgba[1] = color.GetGreen();
			_data->vertices[i].rgba[2] = color.GetBlue();
			_data->vertices[i].rgba[3] = color.GetAlpha();
		}
#if !__has_feature(objc_arc)
		if(_data->vertexObject != nil)
			[_data->vertexObject release];
#endif
		_data->vertexObject = [DEVICE newBufferWithBytes:_data->vertices.data() length:(sizeof(Vertex) * _data->vertices.size()) options:MTLResourceStorageModeShared];
	}
	
	[RENDER setFragmentTexture:_data->texture atIndex:0];
	[RENDER setVertexBuffer:_data->vertexObject offset:0 atIndex:0];
	[RENDER drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:_data->indices.size() indexType:MTLIndexTypeUInt16 indexBuffer:_data->indexObject indexBufferOffset:0];
}



void EImage::DrawVertices (const std::vector<Vertex>& vertices_, const std::vector<uint16_t>& indices_) {
	UploadStagedTextureIfReady();
	if(RENDER != nil && _data->texture != nil) {
#if !__has_feature(objc_arc)
		if(_data->indexObject != nil)
			[_data->indexObject release];
		if(_data->vertexObject != nil)
			[_data->vertexObject release];
#endif
		_data->indexObject = [DEVICE newBufferWithBytes:indices_.data() length:(sizeof(uint16_t) * indices_.size()) options:MTLResourceStorageModeShared];
		_data->vertexObject = [DEVICE newBufferWithBytes:vertices_.data() length:(sizeof(Vertex) * vertices_.size()) options:MTLResourceStorageModeShared];
		[RENDER setFragmentTexture:_data->texture atIndex:0];
		[RENDER setVertexBuffer:_data->vertexObject offset:0 atIndex:0];
		[RENDER drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:indices_.size() indexType:MTLIndexTypeUInt16 indexBuffer:_data->indexObject indexBufferOffset:0];
	}
}



bool EImage::Resource::NewFromFile (const EString& path) {
	int64_t fileSize = ESystem::ResourceSizeFromFile(path);
	if(fileSize <= 0)
		return false;
	
	std::unique_ptr<uint8_t[]> fileBuffer(new uint8_t[fileSize]);
	if(!ESystem::ResourceReadFromFile(path, fileBuffer.get(), fileSize))
		return false;
	
	CFDataRef data = CFDataCreateWithBytesNoCopy(kCFAllocatorDefault, (const UInt8*)fileBuffer.get(), (CFIndex)fileSize, kCFAllocatorNull);
	if(data == nullptr)
		return false;
	CGImageSourceRef imageSource = CGImageSourceCreateWithData(data, nullptr);
	CFRelease(data);
	if(imageSource == nullptr)
		return false;
	
	CGImageRef image = CGImageSourceCreateImageAtIndex(imageSource, 0, nullptr);
	CFRelease(imageSource);
	if(image == nullptr)
		return false;
	
	width = (int32_t)CGImageGetWidth(image);
	height = (int32_t)CGImageGetHeight(image);
	bufferSize = width * height * 4;
	buffer = new uint8_t[bufferSize];
	CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
	CGContextRef context = CGBitmapContextCreate(buffer, width, height, 8, width * 4, colorSpace, kCGImageAlphaPremultipliedLast);
	if(context == nullptr) {
		CGColorSpaceRelease(colorSpace);
		CGImageRelease(image);
		delete[] buffer;
		buffer = nullptr;
		bufferSize = 0;
		width = 0;
		height = 0;
		return false;
	}
	CGContextSetBlendMode(context, kCGBlendModeCopy);
	CGContextDrawImage(context, CGRectMake((CGFloat)0, (CGFloat)0, (CGFloat)width, (CGFloat)height), image);
	CGContextRelease(context);
	CGColorSpaceRelease(colorSpace);
	CGImageRelease(image);
	return true;
}



#endif // __APPLE__

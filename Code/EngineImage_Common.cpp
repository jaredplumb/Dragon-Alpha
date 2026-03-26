/**
 * @file EngineImage_Common.cpp
 * @brief Shared image serialization and runtime image utility implementation.
 */
#include "EngineImage.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

namespace {
constexpr int64_t IMAGE_CHANNEL_COUNT = 4;
constexpr int64_t IMAGE_RESOURCE_HEADER_SIZE = sizeof(int32_t) + sizeof(int32_t) + sizeof(int64_t);

static bool ComputeImageBufferSize (int32_t width, int32_t height, int64_t& outSize) {
	if(width <= 0 || height <= 0)
		return false;
	const int64_t rowPixels = (int64_t)width;
	if(rowPixels > std::numeric_limits<int64_t>::max() / IMAGE_CHANNEL_COUNT)
		return false;
	const int64_t rowBytes = rowPixels * IMAGE_CHANNEL_COUNT;
	if((int64_t)height > std::numeric_limits<int64_t>::max() / rowBytes)
		return false;
	outSize = rowBytes * (int64_t)height;
	return true;
}

static bool FitsSizeTFromInt64 (int64_t value) {
	return value >= 0 && (uint64_t)value <= (uint64_t)std::numeric_limits<size_t>::max();
}
}


bool EImage::New (const EColor& color) {
	Resource resource;
	resource.width = 4;
	resource.height = 4;
	resource.bufferSize = resource.width * resource.height * 4;
	resource.buffer = new uint8_t[resource.bufferSize];
	for(int i = 0; i < resource.bufferSize; i += 4) {
		resource.buffer[i + 0] = color.GetRed();
		resource.buffer[i + 1] = color.GetGreen();
		resource.buffer[i + 2] = color.GetBlue();
		resource.buffer[i + 3] = color.GetAlpha();
	}
	return New(resource);
}



bool EImage::Resource::New (const EString& name) {
	if(buffer) {
		delete [] buffer;
		buffer = nullptr;
	}
	width = 0;
	height = 0;
	bufferSize = 0;

	int64_t resourceSize = ESystem::ResourceSize(name + ".img");
	if(resourceSize >= IMAGE_RESOURCE_HEADER_SIZE && FitsSizeTFromInt64(resourceSize)) {
		std::unique_ptr<uint8_t[]> resourceBuffer(new uint8_t[(size_t)resourceSize]);
		if(ESystem::ResourceRead(name + ".img", resourceBuffer.get(), resourceSize)) {
			int64_t offset = 0;
			memcpy(&width, resourceBuffer.get() + offset, sizeof(width));
			offset += sizeof(width);
			memcpy(&height, resourceBuffer.get() + offset, sizeof(height));
			offset += sizeof(height);
			memcpy(&bufferSize, resourceBuffer.get() + offset, sizeof(bufferSize));
			offset += sizeof(bufferSize);
			int64_t expectedBufferSize = 0;
				if(bufferSize > 0
					&& bufferSize <= resourceSize - offset
					&& FitsSizeTFromInt64(bufferSize)
					&& ComputeImageBufferSize(width, height, expectedBufferSize)
					&& expectedBufferSize == bufferSize) {
				buffer = new uint8_t[(size_t)bufferSize];
				memcpy(buffer, resourceBuffer.get() + offset, (size_t)bufferSize);
				return true;
			}
		}
	}

	EString fallbackPath;
	fallbackPath.Format("Images/%s.png", (const char*)name);
	return NewFromFile(fallbackPath);

}



bool EImage::Resource::Write (const EString& name) {
	int64_t expectedBufferSize = 0;
	if(buffer == nullptr
		|| bufferSize <= 0
		|| ComputeImageBufferSize(width, height, expectedBufferSize) == false
		|| expectedBufferSize != bufferSize
		|| bufferSize > std::numeric_limits<int64_t>::max() - IMAGE_RESOURCE_HEADER_SIZE
		|| FitsSizeTFromInt64(bufferSize) == false) {
		return false;
	}

	const int64_t resourceSize = IMAGE_RESOURCE_HEADER_SIZE + bufferSize;
	if(resourceSize <= 0 || FitsSizeTFromInt64(resourceSize) == false)
		return false;
	std::unique_ptr<uint8_t[]> resourceBuffer(new uint8_t[(size_t)resourceSize]);
	int64_t offset = 0;
	memcpy(resourceBuffer.get() + offset, &width, sizeof(width));
	offset += sizeof(width);
	memcpy(resourceBuffer.get() + offset, &height, sizeof(height));
	offset += sizeof(height);
	memcpy(resourceBuffer.get() + offset, &bufferSize, sizeof(bufferSize));
	offset += sizeof(bufferSize);
	memcpy(resourceBuffer.get() + offset, buffer, (size_t)bufferSize);
	return ESystem::ResourceWrite(name + ".img", resourceBuffer.get(), resourceSize);
}

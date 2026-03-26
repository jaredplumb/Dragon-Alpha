/**
 * @file EngineSound.cpp
 * @brief Sound loading, playback helpers, and audio resource utility implementation.
 */
#include "EngineSound.h"
#include "EngineSystem.h"
#include <cstring>
#include <vector>

namespace {

constexpr uint8_t GSND_MAGIC[4] = {'G', 'S', 'N', 'D'};
constexpr uint16_t GSND_VERSION = 1;
constexpr uint16_t GSND_FLAGS_PCM_LITTLE_ENDIAN = 1;
constexpr uint64_t GSND_HEADER_SIZE = 24;

static EString SoundResourceName (const EString& name) {
	EString result;
	result.Format("%s.snd", (const char*)name);
	return result;
}

static EString SoundLooseFilePath (const EString& name) {
	EString result;
	result.Format("Sounds/%s.wav", (const char*)name);
	return result;
}

static void ClearSoundResource (ESound::Resource& resource) {
	if(resource.buffer != nullptr) {
		delete [] resource.buffer;
		resource.buffer = nullptr;
	}
	resource.bufferSize = 0;
	resource.sampleRate = 0;
	resource.channels = 0;
	resource.bitsPerSample = 0;
}

static bool SetSoundPCM (
	ESound::Resource& resource,
	const uint8_t* pcm,
	uint64_t pcmSize,
	uint16_t channels,
	uint32_t sampleRate,
	uint16_t bitsPerSample
) {
	ClearSoundResource(resource);
	if(pcm == nullptr || pcmSize == 0 || sampleRate == 0)
		return false;
	if(channels != 1 && channels != 2)
		return false;
	if(bitsPerSample != 8 && bitsPerSample != 16)
		return false;
	const uint64_t bytesPerFrame = ((uint64_t)bitsPerSample / 8u) * (uint64_t)channels;
	if(bytesPerFrame == 0 || (pcmSize % bytesPerFrame) != 0)
		return false;

	resource.buffer = new uint8_t[(size_t)pcmSize];
	memcpy(resource.buffer, pcm, (size_t)pcmSize);
	resource.bufferSize = pcmSize;
	resource.sampleRate = sampleRate;
	resource.channels = channels;
	resource.bitsPerSample = bitsPerSample;
	return true;
}

static bool ReadResourceBytes (const EString& name, std::vector<uint8_t>& bytes) {
	bytes.clear();
	const int64_t size = ESystem::ResourceSize(name);
	if(size <= 0)
		return false;
	bytes.resize((size_t)size);
	if(!ESystem::ResourceRead(name, bytes.data(), size)) {
		bytes.clear();
		return false;
	}
	return true;
}

static bool ReadFileBytes (const EString& path, std::vector<uint8_t>& bytes) {
	bytes.clear();
	const int64_t size = ESystem::ResourceSizeFromFile(path);
	if(size <= 0)
		return false;
	bytes.resize((size_t)size);
	if(!ESystem::ResourceReadFromFile(path, bytes.data(), size)) {
		bytes.clear();
		return false;
	}
	return true;
}

static bool ParseGSND (ESound::Resource& resource, const uint8_t* bytes, uint64_t size) {
	if(bytes == nullptr || size < GSND_HEADER_SIZE)
		return false;
	if(memcmp(bytes, GSND_MAGIC, sizeof(GSND_MAGIC)) != 0)
		return false;

	uint16_t version = 0;
	memcpy(&version, bytes + 4, sizeof(version));
	if(version != GSND_VERSION)
		return false;

	uint16_t flags = 0;
	memcpy(&flags, bytes + 6, sizeof(flags));
	if((flags & GSND_FLAGS_PCM_LITTLE_ENDIAN) == 0)
		return false;

	uint16_t channels = 0;
	uint16_t bitsPerSample = 0;
	uint32_t sampleRate = 0;
	uint64_t pcmSize = 0;
	memcpy(&channels, bytes + 8, sizeof(channels));
	memcpy(&bitsPerSample, bytes + 10, sizeof(bitsPerSample));
	memcpy(&sampleRate, bytes + 12, sizeof(sampleRate));
	memcpy(&pcmSize, bytes + 16, sizeof(pcmSize));
	if(GSND_HEADER_SIZE + pcmSize > size)
		return false;
	return SetSoundPCM(resource, bytes + GSND_HEADER_SIZE, pcmSize, channels, sampleRate, bitsPerSample);
}

static bool ParseWavPCM (ESound::Resource& resource, const uint8_t* bytes, uint64_t size) {
	if(bytes == nullptr || size < 44)
		return false;
	if(memcmp(bytes, "RIFF", 4) != 0 || memcmp(bytes + 8, "WAVE", 4) != 0)
		return false;

	auto Read16 = [bytes](uint64_t offset) -> uint16_t {
		return (uint16_t)(
			((uint16_t)bytes[offset + 1] << 8)
			| (uint16_t)bytes[offset]
		);
	};
	auto Read32 = [bytes](uint64_t offset) -> uint32_t {
		return (uint32_t)(
			((uint32_t)bytes[offset + 3] << 24)
			| ((uint32_t)bytes[offset + 2] << 16)
			| ((uint32_t)bytes[offset + 1] << 8)
			| (uint32_t)bytes[offset]
		);
	};

	uint16_t audioFormat = 0;
	uint16_t channels = 0;
	uint16_t bitsPerSample = 0;
	uint32_t sampleRate = 0;
	const uint8_t* pcm = nullptr;
	uint32_t pcmSize = 0;

	for(uint64_t offset = 12; offset + 8 <= size; ) {
		const uint8_t* chunkType = bytes + offset;
		const uint32_t chunkSize = Read32(offset + 4);
		offset += 8;
		if(offset + (uint64_t)chunkSize > size)
			return false;

		if(memcmp(chunkType, "fmt ", 4) == 0) {
			if(chunkSize < 16)
				return false;
			audioFormat = Read16(offset);
			channels = Read16(offset + 2);
			sampleRate = Read32(offset + 4);
			bitsPerSample = Read16(offset + 14);
		} else if(memcmp(chunkType, "data", 4) == 0) {
			pcm = bytes + offset;
			pcmSize = chunkSize;
		}

		offset += chunkSize;
		if((chunkSize & 1u) != 0u)
			offset++;
	}

	if(audioFormat != 1 || pcm == nullptr || pcmSize == 0)
		return false;
	return SetSoundPCM(resource, pcm, pcmSize, channels, sampleRate, bitsPerSample);
}

static bool ExtractWrappedPayload (const uint8_t* bytes, uint64_t size, const uint8_t*& payload, uint64_t& payloadSize) {
	payload = nullptr;
	payloadSize = 0;
	if(bytes == nullptr || size <= sizeof(uint64_t))
		return false;
	memcpy(&payloadSize, bytes, sizeof(payloadSize));
	if(payloadSize == 0 || payloadSize > (size - sizeof(payloadSize)))
		return false;
	payload = bytes + sizeof(payloadSize);
	return true;
}

static bool ExtractLegacyPayload (const uint8_t* bytes, uint64_t size, const uint8_t*& payload, uint64_t& payloadSize) {
	payload = nullptr;
	payloadSize = 0;
	if(bytes == nullptr || size <= (uint64_t)(sizeof(uint8_t) + sizeof(uint32_t)))
		return false;
	uint32_t payloadSize32 = 0;
	memcpy(&payloadSize32, bytes + sizeof(uint8_t), sizeof(payloadSize32));
	const uint64_t payloadOffset = (uint64_t)sizeof(uint8_t) + (uint64_t)sizeof(payloadSize32);
	if(payloadSize32 == 0 || payloadOffset + (uint64_t)payloadSize32 > size)
		return false;
	payload = bytes + payloadOffset;
	payloadSize = payloadSize32;
	return true;
}

static bool ParseKnownSoundBytes (ESound::Resource& resource, const uint8_t* bytes, uint64_t size) {
	return ParseGSND(resource, bytes, size) || ParseWavPCM(resource, bytes, size);
}

} // namespace

bool ESound::Resource::New (const EString& name) {
	ClearSoundResource(*this);

	std::vector<uint8_t> bytes;
	if(ReadResourceBytes(SoundResourceName(name), bytes)) {
		if(ParseKnownSoundBytes(*this, bytes.data(), (uint64_t)bytes.size()))
			return true;

		const uint8_t* payload = nullptr;
		uint64_t payloadSize = 0;
		if(ExtractWrappedPayload(bytes.data(), (uint64_t)bytes.size(), payload, payloadSize)
			&& ParseKnownSoundBytes(*this, payload, payloadSize))
			return true;
	}

	if(ReadResourceBytes(name, bytes)) {
		if(ParseKnownSoundBytes(*this, bytes.data(), (uint64_t)bytes.size()))
			return true;

		const uint8_t* payload = nullptr;
		uint64_t payloadSize = 0;
		if(ExtractWrappedPayload(bytes.data(), (uint64_t)bytes.size(), payload, payloadSize)
			&& ParseKnownSoundBytes(*this, payload, payloadSize))
			return true;
		if(ExtractLegacyPayload(bytes.data(), (uint64_t)bytes.size(), payload, payloadSize)
			&& ParseKnownSoundBytes(*this, payload, payloadSize))
			return true;
	}

	return NewFromFile(SoundLooseFilePath(name));
}

bool ESound::Resource::NewFromFile (const EString& path) {
	ClearSoundResource(*this);

	std::vector<uint8_t> bytes;
	if(!ReadFileBytes(path, bytes))
		return false;
	return ParseKnownSoundBytes(*this, bytes.data(), (uint64_t)bytes.size());
}

bool ESound::Resource::Write (const EString& name) {
	if(buffer == nullptr || bufferSize == 0 || sampleRate == 0)
		return false;
	if(channels != 1 && channels != 2)
		return false;
	if(bitsPerSample != 8 && bitsPerSample != 16)
		return false;
	const uint64_t bytesPerFrame = ((uint64_t)bitsPerSample / 8u) * (uint64_t)channels;
	if(bytesPerFrame == 0 || (bufferSize % bytesPerFrame) != 0)
		return false;

	std::vector<uint8_t> bytes((size_t)(GSND_HEADER_SIZE + bufferSize), 0);
	memcpy(bytes.data(), GSND_MAGIC, sizeof(GSND_MAGIC));
	memcpy(bytes.data() + 4, &GSND_VERSION, sizeof(GSND_VERSION));
	const uint16_t flags = GSND_FLAGS_PCM_LITTLE_ENDIAN;
	memcpy(bytes.data() + 6, &flags, sizeof(flags));
	memcpy(bytes.data() + 8, &channels, sizeof(channels));
	memcpy(bytes.data() + 10, &bitsPerSample, sizeof(bitsPerSample));
	memcpy(bytes.data() + 12, &sampleRate, sizeof(sampleRate));
	memcpy(bytes.data() + 16, &bufferSize, sizeof(bufferSize));
	memcpy(bytes.data() + GSND_HEADER_SIZE, buffer, (size_t)bufferSize);
	return ESystem::ResourceWrite(SoundResourceName(name), bytes.data(), (int64_t)bytes.size());
}

/**
 * @file EngineSound.h
 * @brief Sound loading, playback helpers, and audio resource utility interfaces.
 */
#ifndef ENGINE_SOUND_H_
#define ENGINE_SOUND_H_

#include "EngineTypes.h"
#include <memory>

// NOTE: To self contain ESound, the audio engine is turned on when accessed and shuts down 
// automatically upon exit of the application.  If there is a long pause accessing the first 
// audio, just create a dummy audio to turn on the engine.

// ESound is the short-lived sound-effect surface.  Longer-running music/streaming playback
// should use a dedicated backend when that runtime surface is reintroduced.
//
// Sound resource contract:
// - Optional tooling can normalize <Sounds> content into the canonical GSND short-SFX container.
// - GSND currently stores PCM clip data plus explicit playback metadata (channels, sample rate,
//   bits/sample) so runtime backends can play short sounds quickly without format-specific import
//   logic at playback time.
// - Source files in Resources/Sounds should still prefer .wav for the most predictable portable
//   authoring path.
// - Longer-running music/streaming content should use a future dedicated runtime surface rather
//   than the short-lived ESound path.

class ESound {
public:
	static void Startup ();
	static void Shutdown ();
	static void SetMasterVolume (float volume);
	static bool PlayMusicTrack (const EString& resourcePath);
	static void StopMusic ();
	static void SetMusicEnabled (bool enabled);
	static void SetLifecyclePaused (bool paused);
	
	class Resource;
	
	ESound ();
	ESound (const Resource& resource);
	ESound (const EString& resource);
	~ESound ();
	
	bool New (const Resource& resource);
	bool New (const EString& resource);
	void Delete (); // Legacy API alias.
	
	void Play ();
	void Stop ();
	void Pause ();
	bool IsPlaying ();
	
	class Resource {
	public:
		uint64_t bufferSize;
		uint8_t* buffer;
		uint32_t sampleRate;
		uint16_t channels;
		uint16_t bitsPerSample;
		inline Resource (): bufferSize(0), buffer(nullptr), sampleRate(0), channels(0), bitsPerSample(0) {}
		inline Resource (const EString& name): bufferSize(0), buffer(nullptr), sampleRate(0), channels(0), bitsPerSample(0) { New(name); }
		inline ~Resource () { if(buffer) delete [] buffer; buffer = nullptr; bufferSize = 0; sampleRate = 0; channels = 0; bitsPerSample = 0; }
		bool New (const EString& name);
		bool NewFromFile (const EString& path);
		bool Write (const EString& name);
	};
	
private:
	struct Private;
	std::unique_ptr<Private> _data;
};

#endif // ENGINE_SOUND_H_

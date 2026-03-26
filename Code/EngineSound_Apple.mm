/**
 * @file EngineSound_Apple.mm
 * @brief Apple-specific audio backend integration for runtime playback.
 */
#include "EngineSound.h"

#if __APPLE__

#import <TargetConditionals.h>
#import <AVFoundation/AVFoundation.h>
#include "EngineSystem.h"
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

static AVAudioEngine* APPLE_AUDIO_ENGINE = nil;
static bool APPLE_AUDIO_SHUTDOWN_CALLBACK_REGISTERED = false;
static float APPLE_AUDIO_MASTER_VOLUME = 1.0f;
static bool APPLE_AUDIO_DISABLED = false;
static AVMIDIPlayer* APPLE_MIDI_PLAYER = nil;
static std::string APPLE_MIDI_TRACK;
static bool APPLE_MIDI_ENABLED = true;
static int APPLE_MIDI_LOOP_TOKEN = 0;
static bool APPLE_AUDIO_LIFECYCLE_PAUSED = false;
static bool APPLE_MIDI_RESUME_ON_FOCUS = false;

static void StopAppleMidiPlayback (bool releasePlayer) {
	APPLE_MIDI_LOOP_TOKEN++;
	if(APPLE_MIDI_PLAYER != nil) {
		[APPLE_MIDI_PLAYER stop];
		[APPLE_MIDI_PLAYER setCurrentPosition:0.0];
	}
	if(releasePlayer) {
#if !__has_feature(objc_arc)
		if(APPLE_MIDI_PLAYER != nil)
			[APPLE_MIDI_PLAYER release];
#endif
		APPLE_MIDI_PLAYER = nil;
		APPLE_MIDI_TRACK.clear();
	}
}

static bool HasLaunchArg (const char* flag) {
	if(flag == nullptr || flag[0] == '\0')
		return false;
	for(int i = 1; i < ESystem::GetArgCount(); i++) {
		const char* value = ESystem::GetArgValue(i);
		if(value != nullptr && std::strcmp(value, flag) == 0)
			return true;
	}
	return false;
}

static bool ShouldDisableAudioStartup () {
#if defined(DRAGON_TEST) && TARGET_OS_OSX
	return HasLaunchArg("--automation");
#else
	return false;
#endif
}

static bool StartAppleAudioEngine () {
	ESound::Startup();
	if(APPLE_AUDIO_ENGINE == nil)
		return false;

#if TARGET_OS_IOS
	AVAudioSession* session = [AVAudioSession sharedInstance];
	NSError* sessionError = nil;
	if([session setCategory:AVAudioSessionCategoryAmbient error:&sessionError] == NO) {
		ESystem::Debug("Could not set AVAudioSession category! (%s)\n", sessionError ? sessionError.localizedDescription.UTF8String : "unknown error");
	}
	sessionError = nil;
	if([session setActive:YES error:&sessionError] == NO) {
		ESystem::Debug("Could not activate AVAudioSession! (%s)\n", sessionError ? sessionError.localizedDescription.UTF8String : "unknown error");
	}
#endif

	@try {
		[[APPLE_AUDIO_ENGINE mainMixerNode] setOutputVolume:APPLE_AUDIO_MASTER_VOLUME];
		if([APPLE_AUDIO_ENGINE isRunning] == NO) {
			NSError* startError = nil;
			[APPLE_AUDIO_ENGINE prepare];
			if([APPLE_AUDIO_ENGINE startAndReturnError:&startError] == NO) {
				ESystem::Debug("Could not start Apple audio engine! (%s)\n", startError ? startError.localizedDescription.UTF8String : "unknown error");
				return false;
			}
		}
	}
	@catch (NSException* exception) {
		ESystem::Debug("Could not activate Apple audio output! (%s)\n", exception.reason ? exception.reason.UTF8String : "unknown exception");
		return false;
	}
	return true;
}

static AVMIDIPlayer* CreateAppleMidiPlayerForResource (const EString& resourcePath) {
	const int64_t size = ESystem::ResourceSizeFromFile(resourcePath);
	if(size <= 0) {
		ESystem::Debug("Could not load MIDI resource \"%s\".\n", (const char*)resourcePath);
		return nil;
	}

	std::vector<uint8_t> bytes((size_t)size);
	if(ESystem::ResourceReadFromFile(resourcePath, bytes.data(), size) == false) {
		ESystem::Debug("Could not read MIDI resource \"%s\".\n", (const char*)resourcePath);
		return nil;
	}

	NSData* data = [NSData dataWithBytes:bytes.data() length:(NSUInteger)bytes.size()];
	if(data == nil || [data length] == 0) {
		ESystem::Debug("Could not map MIDI data \"%s\".\n", (const char*)resourcePath);
		return nil;
	}

	NSError* error = nil;
	AVMIDIPlayer* player = [[AVMIDIPlayer alloc] initWithData:data soundBankURL:nil error:&error];
	if(player == nil) {
		ESystem::Debug(
			"Could not create MIDI player for \"%s\" (%s)\n",
			(const char*)resourcePath,
			error != nil && error.localizedDescription != nil ? error.localizedDescription.UTF8String : "unknown error"
		);
		return nil;
	}
	[player prepareToPlay];
	return player;
}

static void StartAppleMidiPlaybackLoop () {
	if(APPLE_MIDI_PLAYER == nil || APPLE_MIDI_ENABLED == false || APPLE_AUDIO_LIFECYCLE_PAUSED)
		return;
	if(StartAppleAudioEngine() == false)
		return;

	APPLE_MIDI_LOOP_TOKEN++;
	const int playToken = APPLE_MIDI_LOOP_TOKEN;
	AVMIDIPlayer* player = APPLE_MIDI_PLAYER;
	[player stop];
	[player setCurrentPosition:0.0];
	[player play:^(void) {
		dispatch_async(dispatch_get_main_queue(), ^{
			if(playToken != APPLE_MIDI_LOOP_TOKEN || APPLE_MIDI_PLAYER == nil || APPLE_MIDI_ENABLED == false)
				return;
			StartAppleMidiPlaybackLoop();
		});
	}];
}

struct ESound::Private {
	AVAudioPlayerNode* player;
	AVAudioPCMBuffer* buffer;
	bool playing;

	inline Private ()
	:	player(nullptr)
	,	buffer(nullptr)
	,	playing(false)
	{
	}

	inline ~Private () {
		AVAudioPlayerNode* ownedPlayer = player;
		if(ownedPlayer != nil) {
			[ownedPlayer stop];
			if(APPLE_AUDIO_ENGINE != nil && ownedPlayer.engine == APPLE_AUDIO_ENGINE)
				[APPLE_AUDIO_ENGINE detachNode:ownedPlayer];
#if !__has_feature(objc_arc)
			[ownedPlayer release];
#endif
		}

		AVAudioPCMBuffer* ownedBuffer = buffer;
		if(ownedBuffer != nil) {
#if !__has_feature(objc_arc)
			[ownedBuffer release];
#endif
		}

		player = nullptr;
		buffer = nullptr;
		playing = false;
	}
};

void ESound::Startup () {
	if(ShouldDisableAudioStartup()) {
		if(APPLE_AUDIO_DISABLED == false) {
			ESystem::Print("[DRAGON_TEST] Audio startup disabled for automation run.\n");
			APPLE_AUDIO_DISABLED = true;
		}
		return;
	}

	if(APPLE_AUDIO_ENGINE == nil) {
		@try {
			APPLE_AUDIO_ENGINE = [[AVAudioEngine alloc] init];
		}
		@catch (NSException* exception) {
			ESystem::Debug("Could not create Apple audio engine! (%s)\n", exception.reason ? exception.reason.UTF8String : "unknown exception");
			APPLE_AUDIO_ENGINE = nil;
		}
		if(APPLE_AUDIO_ENGINE == nil) {
			return;
		}
	}

	@try {
		[[APPLE_AUDIO_ENGINE mainMixerNode] setOutputVolume:APPLE_AUDIO_MASTER_VOLUME];
	}
	@catch (NSException* exception) {
		ESystem::Debug("Could not access Apple audio mixer! (%s)\n", exception.reason ? exception.reason.UTF8String : "unknown exception");
		Shutdown();
		return;
	}
	if(APPLE_AUDIO_SHUTDOWN_CALLBACK_REGISTERED == false) {
		ESystem::NewShutdownCallback(Shutdown);
		APPLE_AUDIO_SHUTDOWN_CALLBACK_REGISTERED = true;
	}
}

void ESound::Shutdown () {
	StopAppleMidiPlayback(true);
	if(APPLE_AUDIO_ENGINE != nil) {
		[APPLE_AUDIO_ENGINE stop];
#if !__has_feature(objc_arc)
		[APPLE_AUDIO_ENGINE release];
#endif
		APPLE_AUDIO_ENGINE = nil;
	}
	APPLE_AUDIO_DISABLED = false;
	APPLE_AUDIO_LIFECYCLE_PAUSED = false;
	APPLE_MIDI_RESUME_ON_FOCUS = false;

#if TARGET_OS_IOS
	AVAudioSession* session = [AVAudioSession sharedInstance];
	[session setActive:NO error:nil];
#endif
}

void ESound::SetMasterVolume (float volume) {
	if(volume < 0.0f)
		volume = 0.0f;
	if(volume > 1.0f)
		volume = 1.0f;
	APPLE_AUDIO_MASTER_VOLUME = volume;
	if(APPLE_AUDIO_ENGINE != nil) {
		@try {
			[[APPLE_AUDIO_ENGINE mainMixerNode] setOutputVolume:APPLE_AUDIO_MASTER_VOLUME];
		}
		@catch (NSException* exception) {
			ESystem::Debug("Could not update Apple audio volume! (%s)\n", exception.reason ? exception.reason.UTF8String : "unknown exception");
		}
	}
}

bool ESound::PlayMusicTrack (const EString& resourcePath) {
	if(resourcePath.IsEmpty()) {
		StopMusic();
		return true;
	}
	if(ShouldDisableAudioStartup())
		return false;

	if(APPLE_MIDI_PLAYER != nil && APPLE_MIDI_TRACK == resourcePath.String()) {
		if(APPLE_MIDI_ENABLED)
			StartAppleMidiPlaybackLoop();
		return true;
	}

	AVMIDIPlayer* nextPlayer = CreateAppleMidiPlayerForResource(resourcePath);
	if(nextPlayer == nil)
		return false;

	StopAppleMidiPlayback(true);
	APPLE_MIDI_PLAYER = nextPlayer;
	APPLE_MIDI_TRACK = resourcePath.String();
	if(APPLE_MIDI_ENABLED)
		StartAppleMidiPlaybackLoop();
	return true;
}

void ESound::StopMusic () {
	StopAppleMidiPlayback(false);
}

void ESound::SetMusicEnabled (bool enabled) {
	APPLE_MIDI_ENABLED = enabled;
	if(APPLE_MIDI_ENABLED == false) {
		StopAppleMidiPlayback(false);
		return;
	}
	if(APPLE_MIDI_PLAYER != nil && APPLE_AUDIO_LIFECYCLE_PAUSED == false)
		StartAppleMidiPlaybackLoop();
}

void ESound::SetLifecyclePaused (bool paused) {
	if(paused) {
		if(APPLE_AUDIO_LIFECYCLE_PAUSED)
			return;
		APPLE_AUDIO_LIFECYCLE_PAUSED = true;
		APPLE_MIDI_RESUME_ON_FOCUS = APPLE_MIDI_PLAYER != nil
			&& APPLE_MIDI_ENABLED
			&& [(AVMIDIPlayer*)APPLE_MIDI_PLAYER isPlaying];
		StopAppleMidiPlayback(false);
		if(APPLE_AUDIO_ENGINE != nil && [APPLE_AUDIO_ENGINE isRunning])
			[APPLE_AUDIO_ENGINE pause];
		return;
	}

	if(APPLE_AUDIO_LIFECYCLE_PAUSED == false)
		return;
	APPLE_AUDIO_LIFECYCLE_PAUSED = false;
	if(ShouldDisableAudioStartup()) {
		APPLE_MIDI_RESUME_ON_FOCUS = false;
		return;
	}
	if(APPLE_MIDI_RESUME_ON_FOCUS && APPLE_MIDI_PLAYER != nil && APPLE_MIDI_ENABLED)
		StartAppleMidiPlaybackLoop();
	APPLE_MIDI_RESUME_ON_FOCUS = false;
}

ESound::ESound ()
:	_data(new Private)
{
}

ESound::ESound (const Resource& resource)
:	_data(new Private)
{
	New(resource);
}

ESound::ESound (const EString& resource)
:	_data(new Private)
{
	New(resource);
}

ESound::~ESound () {
}

void ESound::Delete () {
	_data.reset(new Private);
}

bool ESound::New (const EString& resource) {
	return New(Resource(resource));
}

bool ESound::New (const Resource& resource) {
	Delete();
	Startup();
	if(APPLE_AUDIO_ENGINE == nil)
		return false;
	if(resource.buffer == nullptr || resource.bufferSize == 0 || resource.sampleRate == 0)
		return false;
	if((resource.channels != 1 && resource.channels != 2) || (resource.bitsPerSample != 8 && resource.bitsPerSample != 16))
		return false;

	const uint64_t bytesPerFrame = ((uint64_t)resource.bitsPerSample / 8u) * (uint64_t)resource.channels;
	if(bytesPerFrame == 0 || (resource.bufferSize % bytesPerFrame) != 0)
		return false;

	const uint64_t frameCount64 = resource.bufferSize / bytesPerFrame;
	if(frameCount64 == 0 || frameCount64 > (uint64_t)UINT32_MAX)
		return false;

	AVAudioFormat* format = [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatFloat32 sampleRate:(double)resource.sampleRate channels:(AVAudioChannelCount)resource.channels interleaved:NO];
	if(format == nil)
		return false;

	AVAudioPCMBuffer* newBuffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:format frameCapacity:(AVAudioFrameCount)frameCount64];
	if(newBuffer == nil) {
#if !__has_feature(objc_arc)
		[format release];
#endif
		return false;
	}

	newBuffer.frameLength = (AVAudioFrameCount)frameCount64;
	float* const* channelData = [newBuffer floatChannelData];
	if(channelData == nullptr) {
#if !__has_feature(objc_arc)
		[newBuffer release];
		[format release];
#endif
		return false;
	}

	const uint32_t frameCount = (uint32_t)frameCount64;
	if(resource.bitsPerSample == 8) {
		const uint8_t* source = resource.buffer;
		for(uint32_t frame = 0; frame < frameCount; frame++)
			for(uint16_t channel = 0; channel < resource.channels; channel++) {
				const uint8_t sample = source[(size_t)frame * (size_t)resource.channels + (size_t)channel];
				channelData[channel][frame] = (float)((int)sample - 128) / 128.0f;
			}
	} else {
		const uint8_t* source = resource.buffer;
		for(uint32_t frame = 0; frame < frameCount; frame++)
			for(uint16_t channel = 0; channel < resource.channels; channel++) {
				const size_t sampleOffset = (((size_t)frame * (size_t)resource.channels) + (size_t)channel) * sizeof(int16_t);
				const int16_t sample = (int16_t)(
					((uint16_t)source[sampleOffset + 1] << 8)
					| (uint16_t)source[sampleOffset]
				);
				channelData[channel][frame] = (float)sample / 32768.0f;
			}
	}

	AVAudioPlayerNode* newPlayer = [[AVAudioPlayerNode alloc] init];
	if(newPlayer == nil) {
#if !__has_feature(objc_arc)
		[newBuffer release];
		[format release];
#endif
		return false;
	}

	[APPLE_AUDIO_ENGINE attachNode:newPlayer];
	[APPLE_AUDIO_ENGINE connect:newPlayer to:[APPLE_AUDIO_ENGINE mainMixerNode] format:format];
	if(StartAppleAudioEngine() == false) {
		[APPLE_AUDIO_ENGINE detachNode:newPlayer];
#if !__has_feature(objc_arc)
		[newPlayer release];
		[newBuffer release];
		[format release];
#endif
		return false;
	}

	_data->player = newPlayer;
	_data->buffer = newBuffer;
	_data->playing = false;

#if !__has_feature(objc_arc)
	[format release];
#endif
	return true;
}

void ESound::Play () {
	if(_data == nullptr || _data->player == nullptr || _data->buffer == nullptr)
		return;
	if(StartAppleAudioEngine() == false)
		return;

	AVAudioPlayerNode* player = (AVAudioPlayerNode*)_data->player;
	AVAudioPCMBuffer* buffer = (AVAudioPCMBuffer*)_data->buffer;
	[player stop];
	[player scheduleBuffer:buffer atTime:nil options:0 completionHandler:nil];
	[player play];
	_data->playing = true;
}

void ESound::Stop () {
	if(_data == nullptr || _data->player == nullptr)
		return;
	[(AVAudioPlayerNode*)_data->player stop];
	_data->playing = false;
}

void ESound::Pause () {
	if(_data == nullptr || _data->player == nullptr)
		return;
	[(AVAudioPlayerNode*)_data->player pause];
	_data->playing = false;
}

bool ESound::IsPlaying () {
	if(_data == nullptr || _data->player == nullptr)
		return false;
	return [(AVAudioPlayerNode*)_data->player isPlaying];
}

#endif // __APPLE__

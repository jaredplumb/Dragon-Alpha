/**
 * @file EngineServices.h
 * @brief Engine-level service declarations exposed to game/runtime code.
 */
#ifndef ENGINE_SERVICES_H
#define ENGINE_SERVICES_H

// Lightweight runtime service shim used by Dragon Alpha bootstrap scenes.
// This mirrors the Epic Saga service entry points while keeping behavior minimal
// until full platform service integrations are migrated.
class EServices {
public:
	static inline void StartupRuntime (bool analyticsEnabled) {
		(void)analyticsEnabled;
	}

	static inline bool IsCompromisedEnvironment (void) {
		return false;
	}

	static inline bool HasInstalledBundle (const char * bundleID) {
		(void)bundleID;
		return false;
	}
};

#endif // ENGINE_SERVICES_H

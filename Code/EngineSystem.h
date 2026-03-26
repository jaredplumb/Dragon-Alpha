/**
 * @file EngineSystem.h
 * @brief Cross-platform system facade for timing, IO, callbacks, and runtime services.
 */
#ifndef ENGINE_SYSTEM_H_
#define ENGINE_SYSTEM_H_

#include "EngineTypes.h"
#include <chrono>
#include <map>
#include <vector>

/// This class is used to access global data internally created to run a core game shell.  This
/// class internally contains everything needed to automatically launches and create a game
/// including hardware accelerated graphics and audio.  Main has deliberately been
/// moved from this class to allow for a full inclusion of this library when creating tools.
/// Several functions and all the callbacks will only work after calling Run(), which can be
/// ignored when working with tools.
/// Keep this surface lightweight and avoid introducing higher-level engine dependencies here.

class ESystem {
public:
	/// Returns the logical screen rect in design-space units.
	/// The screen rect represents the full drawable area after the platform's
	/// native drawable has been adapted into design space. This can include
	/// regions that are visually obscured or interaction-unsafe on modern devices.
	/// Typical use: fill ambient or background content to this rect.
	static ERect GetScreenRect ();
	
	/// Returns the logical safe rect in design-space units.
	/// The safe rect is the largest interaction-safe subset of the logical screen
	/// rect after applying platform-defined hardware and system insets.
	/// Typical use: keep critical gameplay and interface elements inside this rect.
	static ERect GetSafeRect ();
	
	/// Returns the requested design rect in design-space units.
	/// The design rect is always rooted at 0,0 and keeps the exact width and
	/// height requested by SetLaunchDesignSize(...). Screen and safe rects shift
	/// around this origin as needed to reflect the real platform shape.
	/// Typical use: author the core layout against this rect.
	static ERect GetDesignRect ();

	/// Derives logical screen and safe rects from native platform bounds.
	/// `nativeRect` is the full native drawable rect for the current platform host.
	/// `nativeSafeRect` is the native safe subset inside that drawable rect.
	/// `designRect` provides the fixed launch-time design width and height.
	/// Only the design width and height are used; the design origin remains 0,0.
	/// `screenRect` receives the logical full-screen area in design-space units.
	/// `safeRect` receives the logical safe area in design-space units.
	/// Platform backends should measure native geometry, convert any platform
	/// safe-area data into that same native space, then call this helper to
	/// derive the public rect contract.
	static void GetSystemRects (
		const ERect& nativeRect,
		const ERect& nativeSafeRect,
		const ERect& designRect,
		ERect& screenRect,
		ERect& safeRect
	);

	/// Returns the the current FPS.
	static int GetFPS ();

	static void Paint (const EColor& color, const ERect& area);
	
	/// Returns a monotonic time value in milliseconds suitable for gameplay/runtime timing.
	static inline int64_t GetMilliseconds () { return (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
	
	/// Returns a monotonic time value in microseconds suitable for gameplay/runtime timing.
	static inline int64_t GetMicroseconds () { return (int64_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
	
	/// Returns a monotonic time value in nanoseconds suitable for gameplay/runtime timing.
	static inline int64_t GetNanoseconds () { return (int64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
	
	static int64_t ResourceSize (const EString& name);
	
	static int64_t ResourceSizeFromFile (const EString& path);
	
	static bool ResourceRead (const EString& name, void* data, int64_t size);
	
	static bool ResourceReadFromFile (const EString& path, void* data, int64_t size);
	
	static bool ResourceWrite (const EString& name, void* data, int64_t size);
	
	/// Read save data from the appropriate place, falling back to the previous committed copy when present
	static bool SaveRead (const EString& name, void* data, int64_t size);

	/// Write save data atomically to the appropriate place and keep one previous committed copy when present
	static bool SaveWrite (const EString& name, const void* data, int64_t size);

	/// Delete save data (current, previous, temporary, and legacy extension variants when present)
	static bool SaveDelete (const EString& name);
	
	/// Returns a list of file names in the directory provided by the path including all sub directories
	static std::vector<EString> GetFileNamesInDirectory (const EString& path);
	
	/// Sets the launch-time design size before Run().
	/// This does not resize the native drawable directly. It defines the logical
	/// design target that platform backends fit inside the safe native area.
	/// Calling this after Run() is not supported.
	static void SetLaunchDesignSize (int width, int height);

	/// Sets the launch-time target FPS before Run().
	/// Calling this after Run() is not supported.
	static void SetLaunchTargetFPS (int fps);

	/// Stores launch arguments before Run().
	/// Calling this after Run() is not supported.
	static void SetLaunchArgs (int argc, char* argv[]);
	static int GetArgCount ();
	static const char* GetArgValue (int index);
	
	/// Runs the core game system until finished (or in some cases indefinitely) returning the exit code
	static int Run ();
	
	// Optional runtime bootstrap can call this before first resource read.
	static void SetDefaultWD ();
	
	// Typical usage is to set the matrix to default, then scale, then rotate, then translate, then update the matrix
	static void MatrixSetModelDefault (); // Set model to identity
	static void MatrixSetProjectionDefault (); // Set projection to an ortho 2D view
	static void MatrixTranslateModel (float x, float y);
	static void MatrixTranslateProjection (float x, float y);
	static void MatrixScaleModel (float x, float y);
	static void MatrixScaleProjection (float x, float y);
	static void MatrixRotateModel (float degrees);
	static void MatrixRotateProjection (float degrees);
	static void MatrixUpdate ();
	
	/// Returns a new unique integer, per application session.
	static inline int GetUniqueRef () { static int REF = 1; return REF++; }
	
	/// Prints a formatted string to the console.
	static void Print (const char* message, ...);
	
	/// Prints a formatted string to the console in debug builds only.
	static void Debug (const char* message, ...);

	/// Reports structured text layout telemetry for machine-readable runtime artifacts.
	static void ReportTextDraw (const EString& text, const ERect& rect);
	static void ClearReportedTextDraws ();
	static int GetReportedTextDrawCount ();
	static bool GetReportedTextDraw (int index, EString& text, ERect& rect);

	// Optional runtime environment probes used by host games.
	static bool HasInstalledBundle (const char* bundleIdentifier);
	static bool IsCompromisedEnvironment ();
	static bool CanCaptureFramePNG ();
	static bool RequestFrameCapturePNG (
		const EString& path,
		void (* callback) (bool success, int width, int height, const EString& path, const EString& error)
	);
	static bool SetFullscreenEnabled (bool enabled);
	static bool IsFullscreenEnabled ();
	static void RequestExit (int exitCode = 0);
	
	// Touch locations are relative to the design rect. Callback passes use snapshot iteration
	// so callbacks can safely add or remove other callbacks for future frames.
	static inline int NewPreRunCallback (void (* callback) ()) { int ref = GetUniqueRef(); PRERUN_CALLBACKS.insert(std::make_pair(ref, callback)); return ref; }
	static inline int NewStartupCallback (void (* callback) ()) { int ref = GetUniqueRef(); STARTUP_CALLBACKS.insert(std::make_pair(ref, callback)); return ref; }
	static inline int NewPauseCallback (void (* callback) ()) { int ref = GetUniqueRef(); PAUSE_CALLBACKS.insert(std::make_pair(ref, callback)); return ref; }
	static inline int NewShutdownCallback (void (* callback) ()) { int ref = GetUniqueRef(); SHUTDOWN_CALLBACKS.insert(std::make_pair(ref, callback)); return ref; }
	static inline int NewDrawCallback (void (* callback) ()) { int ref = GetUniqueRef(); DRAW_CALLBACKS.insert(std::make_pair(ref, callback)); return ref; }
	static inline int NewTimerCallback (void (* callback) ()) { return NewDrawCallback(callback); }
	static inline int NewTouchCallback (void (* callback) (int x, int y)) { int ref = GetUniqueRef(); TOUCH_CALLBACKS.insert(std::make_pair(ref, callback)); return ref; }
	static inline int NewTouchUpCallback (void (* callback) (int x, int y)) { int ref = GetUniqueRef(); TOUCHUP_CALLBACKS.insert(std::make_pair(ref, callback)); return ref; }
	static inline int NewTouchMoveCallback (void (* callback) (int x, int y)) { int ref = GetUniqueRef(); TOUCHMOVE_CALLBACKS.insert(std::make_pair(ref, callback)); return ref; }
	static inline int NewTouchEndedCallback (void (* callback) (int x, int y)) { return NewTouchUpCallback(callback); }
	static inline int NewTouchMovedCallback (void (* callback) (int x, int y)) { return NewTouchMoveCallback(callback); }
	static inline void DeletePreRunCallback (int ref) { PRERUN_CALLBACKS.erase(ref); }
	static inline void DeleteStartupCallback (int ref) { STARTUP_CALLBACKS.erase(ref); }
	static inline void DeletePauseCallback (int ref) { PAUSE_CALLBACKS.erase(ref); }
	static inline void DeleteShutdownCallback (int ref) { SHUTDOWN_CALLBACKS.erase(ref); }
	static inline void DeleteDrawCallback (int ref) { DRAW_CALLBACKS.erase(ref); }
	static inline void DeleteTimerCallback (int ref) { DeleteDrawCallback(ref); }
	static inline void DeleteTouchCallback (int ref) { TOUCH_CALLBACKS.erase(ref); }
	static inline void DeleteTouchUpCallback (int ref) { TOUCHUP_CALLBACKS.erase(ref); }
	static inline void DeleteTouchMoveCallback (int ref) { TOUCHMOVE_CALLBACKS.erase(ref); }
	static inline void DeleteEndedCallback (int ref) { DeleteTouchUpCallback(ref); }
	static inline void DeleteMovedCallback (int ref) { DeleteTouchMoveCallback(ref); }
	static inline void RunPreRunCallbacks () { RunVoidCallbackMap(PRERUN_CALLBACKS); }
	static inline void RunStartupCallbacks () { RunVoidCallbackMap(STARTUP_CALLBACKS); }
	static inline void RunPauseCallbacks () { RunVoidCallbackMap(PAUSE_CALLBACKS); }
	static inline void RunShutdownCallbacks () { RunVoidCallbackMap(SHUTDOWN_CALLBACKS); }
	static inline void RunDrawCallbacks () { RunVoidCallbackMap(DRAW_CALLBACKS); }
	static inline void RunTouchCallbacks (int x, int y) { RunTouchCallbackMap(TOUCH_CALLBACKS, x, y); }
	static inline void RunTouchUpCallbacks (int x, int y) { RunTouchCallbackMap(TOUCHUP_CALLBACKS, x, y); }
	static inline void RunTouchMoveCallbacks (int x, int y) { RunTouchCallbackMap(TOUCHMOVE_CALLBACKS, x, y); }
	
private:
	using VoidCallback = void (*) ();
	using TouchCallback = void (*) (int x, int y);
	using VoidCallbackMap = std::map<int, VoidCallback>;
	using TouchCallbackMap = std::map<int, TouchCallback>;

	template <typename CallbackMap>
	static inline std::vector<typename CallbackMap::mapped_type> CopyCallbacks (const CallbackMap& callbacks) {
		std::vector<typename CallbackMap::mapped_type> snapshot;
		snapshot.reserve(callbacks.size());
		for(const auto& entry : callbacks)
			if(entry.second)
				snapshot.push_back(entry.second);
		return snapshot;
	}

	static inline void RunVoidCallbackMap (const VoidCallbackMap& callbacks) {
		for(VoidCallback callback : CopyCallbacks(callbacks))
			callback();
	}

	static inline void RunTouchCallbackMap (const TouchCallbackMap& callbacks, int x, int y) {
		for(TouchCallback callback : CopyCallbacks(callbacks))
			callback(x, y);
	}

	static inline VoidCallbackMap	PRERUN_CALLBACKS;
	static inline VoidCallbackMap	STARTUP_CALLBACKS;
	static inline VoidCallbackMap	PAUSE_CALLBACKS;
	static inline VoidCallbackMap	SHUTDOWN_CALLBACKS;
	static inline VoidCallbackMap	DRAW_CALLBACKS;
	static inline TouchCallbackMap	TOUCH_CALLBACKS;
	static inline TouchCallbackMap	TOUCHUP_CALLBACKS;
	static inline TouchCallbackMap	TOUCHMOVE_CALLBACKS;
};

#endif // ENGINE_SYSTEM_H_

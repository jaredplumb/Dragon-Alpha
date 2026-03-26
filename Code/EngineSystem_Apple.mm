/**
 * @file EngineSystem_Apple.mm
 * @brief Apple platform runtime host implementation for system and IO services.
 */
#import "EngineSystem.h"
#include "EngineImage.h"
#include "EngineSound.h"
#ifdef __APPLE__
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <CoreFoundation/CoreFoundation.h>
#if TARGET_OS_IOS
#import <UIKit/UIKit.h>
#else
#import <AppKit/AppKit.h>
#endif
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>
#if TARGET_OS_OSX
#import <CoreServices/CoreServices.h>
#endif
#include <string>
#include <memory>
	#include <cstdlib>
	#include <cstring>
	#include <algorithm>
	#include <cctype>
	#include <cmath>
	#include <limits>
	#include <dirent.h>
	#include <errno.h>
	#include <fcntl.h>
	#include <sys/stat.h>
	#include <unistd.h>
	#include <limits.h>
	#include <mach-o/dyld.h>
	#include <zlib.h>



static ERect					NATIVE_RECT;
static ERect					NATIVE_SAFE_RECT;
static ERect					SCREEN_RECT;
static ERect					SAFE_RECT;
static ERect					DESIGN_RECT;
static int						FPS = 60;
static int						ARG_C = 0;
static char**					ARG_V = nullptr;
id<MTLDevice>					DEVICE = nil; // Non-static to allow for extern access from EImage
id<MTLRenderCommandEncoder>		RENDER = nil; // Non-static to allow for extern access from EImage
static EMatrix32_4x4			MODEL_MATRIX;
static EMatrix32_4x4			PROJECTION_MATRIX;
#if TARGET_OS_IOS
static bool						STARTUP_CALLBACKS_RAN = false;
#endif
static bool						APPLICATION_IS_PAUSED = false;
static bool						PROCESS_EXIT_REQUESTED = false;
static int						PROCESS_EXIT_CODE = EXIT_SUCCESS;
#if TARGET_OS_OSX
static NSWindow*					ACTIVE_WINDOW = nil;
#endif
using FrameCaptureCallback = void (*) (bool success, int width, int height, const EString& path, const EString& error);
static bool						FRAME_CAPTURE_PENDING = false;
static bool						FRAME_CAPTURE_IN_FLIGHT = false;
static std::string					FRAME_CAPTURE_PATH;
static FrameCaptureCallback		FRAME_CAPTURE_CALLBACK = nullptr;
struct ReportedTextDrawEntry {
	EString text;
	ERect rect;
};
static std::vector<ReportedTextDrawEntry>	REPORTED_TEXT_DRAWS;

static void _RequestProcessTermination ();
static bool _HasValue (const char* text);
#if TARGET_OS_OSX
static bool _CaptureWindowPNG (NSWindow* window, const std::string& path, int& width, int& height, std::string& error);
static int _RunHeadlessValidation ();
#endif
static void _SetLifecyclePausedState (bool paused);

static inline bool _FitsSizeT (int64_t value) {
	return value >= 0 && (uint64_t)value <= (uint64_t)std::numeric_limits<size_t>::max();
}

static void _SetLifecyclePausedState (bool paused) {
	if(APPLICATION_IS_PAUSED == paused)
		return;
	APPLICATION_IS_PAUSED = paused;
	if(paused)
		ESystem::RunPauseCallbacks();
#if TARGET_OS_OSX
	if(ACTIVE_WINDOW != nil) {
		NSView* contentView = [ACTIVE_WINDOW contentView];
		if(contentView != nil && [contentView isKindOfClass:[MTKView class]])
			[(MTKView*)contentView setPaused:paused];
	}
#endif
	ESound::SetLifecyclePaused(paused);
}

#if TARGET_OS_IOS
@interface _MyViewController : UIViewController
@end

@interface _MyAppDelegate : UIResponder <UIApplicationDelegate>
@property (strong, nonatomic) UIWindow* _window;
@property (strong, nonatomic) _MyViewController* _controller;
@end

#else // TARGET_OS_MAC
@interface _MyViewController : NSViewController
@end

@interface _MyAppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@property (strong, nonatomic) NSWindow* _window;
@end

#endif // TARGET_OS_IOS // TARGET_OS_MAC



@interface _MyMetalView : MTKView <MTKViewDelegate>
@end



#if TARGET_OS_IOS
@implementation _MyViewController

- (void) loadView {
	_MyMetalView* view = [[_MyMetalView alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
	self.view = view;
#if !__has_feature(objc_arc)
	[view release];
#endif
}

- (BOOL) shouldAutorotate {
	return YES;
}

- (UIInterfaceOrientationMask) supportedInterfaceOrientations {
	return UIInterfaceOrientationMaskLandscape;
}

- (BOOL) prefersStatusBarHidden {
	return YES;
}

- (BOOL) prefersHomeIndicatorAutoHidden {
	return YES;
}

@end // _MyViewController

#endif // TARGET_OS_IOS



@implementation _MyAppDelegate

- (void) dealloc {
#if !__has_feature(objc_arc)
#if TARGET_OS_IOS
	self._controller = nil;
#endif
#if TARGET_OS_OSX
	if(ACTIVE_WINDOW == self._window)
		ACTIVE_WINDOW = nil;
#endif
	self._window = nil;
	[super dealloc];
#endif
}

#if TARGET_OS_IOS
- (BOOL) application: (UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {
	UIWindow* window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
	self._window = window;
	[self._window setBackgroundColor:[UIColor blackColor]];
	_MyViewController* controller = [[_MyViewController alloc] init];
	self._controller = controller;
	[self._window setRootViewController:self._controller];
	[self._window makeKeyAndVisible];
#if !__has_feature(objc_arc)
	[controller release];
	[window release];
#endif

	if(!STARTUP_CALLBACKS_RAN) {
		ESystem::RunStartupCallbacks();
		STARTUP_CALLBACKS_RAN = true;
	}
	return YES;
}

- (void) applicationWillResignActive: (UIApplication*)application {
	// Sent when the application is about to move from active to inactive state. This can occur for certain types of temporary interruptions (such as an incoming phone call or SMS message) or when the user quits the application and it begins the transition to the background state.
	// Use this method to pause ongoing tasks, disable timers, and throttle down OpenGL ES frame rates. Games should use this method to pause the game.
	_SetLifecyclePausedState(true);
}

- (void) applicationDidEnterBackground: (UIApplication*)application {
	// Use this method to release shared resources, save user data, invalidate timers, and store enough application state information to restore your application to its current state in case it is terminated later.
	// If your application supports background execution, this method is called instead of applicationWillTerminate: when the user quits.
	_SetLifecyclePausedState(true);
}

- (void) applicationWillEnterForeground: (UIApplication*)application {
	// Called as part of the transition from the background to the active state; here you can undo many of the changes made on entering the background.
}

- (void) applicationDidBecomeActive: (UIApplication*)application {
	// Restart any tasks that were paused (or not yet started) while the application was inactive. If the application was previously in the background, optionally refresh the user interface.
	_SetLifecyclePausedState(false);
}

- (void) applicationWillTerminate: (UIApplication*)application {
	// Run the shutdown callbacks before everything is turned off
	ESystem::RunShutdownCallbacks();
}

#else // TARGET_OS_MAC
	- (void) applicationDidFinishLaunching: (NSNotification*)aNotification {

	// Setup the menus
	NSString* appName = [[NSProcessInfo processInfo] processName];
	NSMenu* mainMenu = [NSMenu new];
	NSMenuItem* appMenuItem = [NSMenuItem new];
	[mainMenu addItem:appMenuItem];
	NSMenu* appMenu = [NSMenu new];
	[appMenuItem setSubmenu:appMenu];
	NSMenuItem* quitMenuItem = [[NSMenuItem alloc] initWithTitle:[@"Quit " stringByAppendingString:appName] action:@selector(terminate:) keyEquivalent:@"q"];
	[appMenu addItem:quitMenuItem];
	[NSApp setMainMenu:mainMenu];
#if !__has_feature(objc_arc)
	[quitMenuItem release];
	[appMenu release];
	[appMenuItem release];
	[mainMenu release];
#endif

		// Keep gameplay design size separate from desktop launch window size.
		// Legacy games still run in their original design coordinate space
		// (for example 480x320), while macOS launches in a modern baseline window.
		int windowWidth = DESIGN_RECT.width;
		int windowHeight = DESIGN_RECT.height;
		if(windowWidth <= 0 || windowHeight <= 0) {
			windowWidth = 1920;
			windowHeight = 1080;
		} else {
			if(windowWidth < 1920)
				windowWidth = 1920;
			if(windowHeight < 1080)
				windowHeight = 1080;
		}

		// Setup the window
		NSRect windowRect = NSMakeRect(0, 0, windowWidth, windowHeight);
		self._window = [[NSWindow alloc] initWithContentRect:windowRect styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable) backing:NSBackingStoreBuffered defer:YES];
		[self._window setDelegate:self];
		[self._window setContentAspectRatio:NSMakeSize((CGFloat)windowWidth / (CGFloat)windowHeight, ((CGFloat)1))];
		[self._window setTitle:[[NSProcessInfo processInfo] processName]];
		[self._window setContentView:[[_MyMetalView alloc] initWithFrame:windowRect]];
		[self._window makeKeyAndOrderFront:self];
	[self._window center];
	ACTIVE_WINDOW = self._window;

	// Run the startup callbacks after everything is turned on
	ESystem::RunStartupCallbacks();
}

- (void) applicationDidResignActive: (NSNotification*)notification {
	_SetLifecyclePausedState(true);
}

- (void) applicationDidBecomeActive: (NSNotification*)notification {
	_SetLifecyclePausedState(false);
}

- (void) windowDidMiniaturize: (NSNotification*)notification {
	_SetLifecyclePausedState(true);
}

- (void) windowDidDeminiaturize: (NSNotification*)notification {
	if(NSApp != nil && [NSApp isActive])
		_SetLifecyclePausedState(false);
}

- (void) applicationWillTerminate: (NSNotification*)notification {
	// Run the shutdown callbacks before everything is turned off
	ESystem::RunShutdownCallbacks();
	if(ACTIVE_WINDOW == self._window)
		ACTIVE_WINDOW = nil;
}

- (BOOL) applicationShouldTerminateAfterLastWindowClosed: (NSApplication *)sender {
	return YES;
}

- (BOOL) applicationSupportsSecureRestorableState: (NSApplication *)app {
    return YES;
}

#endif // TARGET_OS_IOS // TARGET_OS_MAC

@end // _MyAppDelegate



@implementation _MyMetalView {
	id<MTLCommandQueue> _commandQueue;
	id<MTLRenderPipelineState> _pipelineState;
}

- (id) initWithFrame: (CGRect)frameRect {

#if TARGET_OS_IOS
	frameRect = [[UIScreen mainScreen] bounds];
#else
	// AppKit view frames are point-based. Converting to backing pixels here can
	// double the logical view size on Retina displays and desync input mapping.
	// Keep the view frame in points and let drawableSize track native pixels.
#endif // TARGET_OS_IOS // TARGET_OS_OSX

	DEVICE = MTLCreateSystemDefaultDevice();
	self = [super initWithFrame:frameRect device:DEVICE];
	[self setDelegate:self];
#if TARGET_OS_OSX
	// Keep the metal view matched to resizable window content bounds.
	[self setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
#endif
	[self setColorPixelFormat:MTLPixelFormatBGRA8Unorm];
	[self setClearColor:MTLClearColorMake(0.0, 0.0, 0.0, 0.0)];
	[self setPreferredFramesPerSecond:(NSInteger)FPS];
	CGSize initialDrawableSize = self.drawableSize;
	if(initialDrawableSize.width <= 0.0 || initialDrawableSize.height <= 0.0)
		initialDrawableSize = frameRect.size;
	[self mtkView:self drawableSizeWillChange:initialDrawableSize]; // Initialize layout metrics before first draw.

	// Added this as a string for simplicity for multiple platforms.  This can be added to a .metal file instead
	// and the id<MTLLibrary> defaultLibrary code below can be changed to newDefaultLibrary.
	constexpr NSString* SHADER = @""
	"#include <metal_stdlib>\n"
	"#include <simd/simd.h>\n"
	"using namespace metal;\n"
	"\n"
	"struct _CustomVertex {\n"
	"	packed_float2 xy;\n"
	"	packed_uchar4 rgba;\n"
	"	packed_float2 uv;\n"
	"};\n"
	"\n"
	"struct _VertexOutput {\n"
	"	float4 xyzw [[position]];\n"
	"	float4 rgba;\n"
	"	float2 uv;\n"
	"};\n"
	"\n"
	"vertex _VertexOutput _VertexShader (constant _CustomVertex* vertexArray [[buffer(0)]],\n"
	"									constant float4x4* matrix  [[buffer(1)]],\n"
	"									ushort vertexID [[vertex_id]]) {\n"
	"	_VertexOutput out;\n"
	"	out.xyzw = *matrix * float4(vertexArray[vertexID].xy, 0.0, 1.0);\n"
	"	out.rgba = float4(vertexArray[vertexID].rgba) / 255.0;\n"
	"	out.uv = float2(vertexArray[vertexID].uv);\n"
	"	return out;\n"
	"}\n"
	"\n"
	"fragment float4 _FragmentShader (_VertexOutput in [[stage_in]],\n"
	"								 texture2d<float> texture [[texture(0)]]) {\n"
	"	return in.rgba * float4(texture.sample(sampler(mag_filter::linear, min_filter::linear), in.uv));\n"
	"}\n"
	"";

	NSError* error = NULL;
	id<MTLLibrary> defaultLibrary = [self.device newLibraryWithSource:SHADER options:nil error:&error];
	id<MTLFunction> vertexFunction = [defaultLibrary newFunctionWithName:@"_VertexShader"];
	id<MTLFunction> fragmentFunction = [defaultLibrary newFunctionWithName:@"_FragmentShader"];

	MTLRenderPipelineDescriptor* pipelineStateDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
	pipelineStateDescriptor.vertexFunction = vertexFunction;
	pipelineStateDescriptor.fragmentFunction = fragmentFunction;
	pipelineStateDescriptor.colorAttachments[0].pixelFormat = self.colorPixelFormat;
	pipelineStateDescriptor.colorAttachments[0].blendingEnabled = YES;
	pipelineStateDescriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
	pipelineStateDescriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
	pipelineStateDescriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
	pipelineStateDescriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

	_pipelineState = [self.device newRenderPipelineStateWithDescriptor:pipelineStateDescriptor error:&error];
	_commandQueue = [self.device newCommandQueue];
#if !__has_feature(objc_arc)
	[pipelineStateDescriptor release];
	[fragmentFunction release];
	[vertexFunction release];
	[defaultLibrary release];
#endif

	return self;
}

#if TARGET_OS_IOS
- (UIWindow*) _resolvedIOSWindow {
	if(self.window != nil)
		return self.window;

	UIApplication* application = UIApplication.sharedApplication;
	if(@available(iOS 13.0, *)) {
		for(UIScene* scene in application.connectedScenes) {
			if(scene.activationState != UISceneActivationStateForegroundActive &&
			   scene.activationState != UISceneActivationStateForegroundInactive)
				continue;
			if(![scene isKindOfClass:[UIWindowScene class]])
				continue;

			UIWindowScene* windowScene = (UIWindowScene*)scene;
			for(UIWindow* window in windowScene.windows)
				if(window.isKeyWindow)
					return window;
			for(UIWindow* window in windowScene.windows)
				if(!window.hidden)
					return window;
		}
	}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
	return application.windows.firstObject;
#pragma clang diagnostic pop
}
#endif

- (CGSize) _resolvedNativeDrawableSize {
	CGSize size = self.drawableSize;
#if TARGET_OS_IOS
	CGRect bounds = self.bounds;
	if(bounds.size.width <= 0.0 || bounds.size.height <= 0.0)
		bounds = [[UIScreen mainScreen] bounds];

	UIWindow* window = [self _resolvedIOSWindow];
	UIScreen* screen = window != nil ? window.screen : [UIScreen mainScreen];
	CGFloat scale = self.contentScaleFactor;
	if(scale <= 0.0)
		scale = screen.scale;
	if(scale <= 0.0)
		scale = 1.0;

	size.width = bounds.size.width * scale;
	size.height = bounds.size.height * scale;

	if(std::fabs(self.contentScaleFactor - scale) > 0.01)
		self.contentScaleFactor = scale;
	if(std::fabs(self.drawableSize.width - size.width) > 0.5 || std::fabs(self.drawableSize.height - size.height) > 0.5)
		self.drawableSize = size;
#else
	// Always resolve macOS drawable size from backing pixels so Retina/display-scale
	// changes cannot leave us with stale logical-size metrics.
	const CGSize backingSize = [self convertRectToBacking:self.bounds].size;
	if(backingSize.width > 0.0 && backingSize.height > 0.0) {
		size = backingSize;
		if(std::fabs(self.drawableSize.width - backingSize.width) > 0.5 || std::fabs(self.drawableSize.height - backingSize.height) > 0.5)
			self.drawableSize = backingSize;
	} else if(size.width <= 0.0 || size.height <= 0.0) {
		size = self.bounds.size;
	}
#endif
	return size;
}

- (void) _syncLayoutRectsWithDrawableSize: (CGSize)size {
	int nativeWidth = (int)std::lround(size.width);
	int nativeHeight = (int)std::lround(size.height);
	if(nativeWidth <= 0)
		nativeWidth = DESIGN_RECT.width > 0 ? DESIGN_RECT.width : 0;
	if(nativeHeight <= 0)
		nativeHeight = DESIGN_RECT.height > 0 ? DESIGN_RECT.height : 0;

	NATIVE_RECT.Set(0, 0, nativeWidth, nativeHeight);

#if TARGET_OS_IOS
	CGRect bounds = self.bounds;
	if(bounds.size.width <= 0.0 || bounds.size.height <= 0.0)
		bounds = [[UIScreen mainScreen] bounds];

	UIEdgeInsets safeInsets = self.safeAreaInsets;
	CGRect safeBounds = UIEdgeInsetsInsetRect(bounds, safeInsets);
	const CGFloat scaleX = bounds.size.width > 0.0 ? (CGFloat)nativeWidth / bounds.size.width : 1.0;
	const CGFloat scaleY = bounds.size.height > 0.0 ? (CGFloat)nativeHeight / bounds.size.height : 1.0;
	NATIVE_SAFE_RECT.Set(
		(int)std::lround((safeBounds.origin.x - bounds.origin.x) * scaleX),
		(int)std::lround((safeBounds.origin.y - bounds.origin.y) * scaleY),
		(int)std::lround(safeBounds.size.width * scaleX),
		(int)std::lround(safeBounds.size.height * scaleY)
	);
#else
	NATIVE_SAFE_RECT = NATIVE_RECT;
#endif

	if(NATIVE_SAFE_RECT.width < 0)
		NATIVE_SAFE_RECT.width = 0;
	if(NATIVE_SAFE_RECT.height < 0)
		NATIVE_SAFE_RECT.height = 0;

	ESystem::GetSystemRects(NATIVE_RECT, NATIVE_SAFE_RECT, DESIGN_RECT, SCREEN_RECT, SAFE_RECT);
}

- (CGPoint) _screenSpaceLocationFromViewPoint: (CGPoint)location {
	const CGRect bounds = self.bounds;
	const CGFloat width = bounds.size.width;
	const CGFloat height = bounds.size.height;
	CGFloat mappedX = (CGFloat)SCREEN_RECT.x;
	CGFloat mappedY = (CGFloat)SCREEN_RECT.y;
	if(width > 0.0)
		mappedX += location.x * (CGFloat)SCREEN_RECT.width / width;
	if(height > 0.0)
		mappedY += location.y * (CGFloat)SCREEN_RECT.height / height;

	// Round-to-nearest removes floor bias when view-space points are scaled into
	// screen-space pixels, and clamp guards against right/bottom edge overflow.
	mappedX = (CGFloat)std::lround(mappedX);
	mappedY = (CGFloat)std::lround(mappedY);
	if(SCREEN_RECT.width > 0) {
		const CGFloat minX = (CGFloat)SCREEN_RECT.x;
		const CGFloat maxX = (CGFloat)(SCREEN_RECT.x + SCREEN_RECT.width - 1);
		mappedX = std::min(maxX, std::max(minX, mappedX));
	}
	if(SCREEN_RECT.height > 0) {
		const CGFloat minY = (CGFloat)SCREEN_RECT.y;
		const CGFloat maxY = (CGFloat)(SCREEN_RECT.y + SCREEN_RECT.height - 1);
		mappedY = std::min(maxY, std::max(minY, mappedY));
	}
	location.x = mappedX;
	location.y = mappedY;
	return location;
}

#if TARGET_OS_IOS
- (CGPoint) _screenSpaceTouchLocation: (UITouch*)touch {
	[self _syncLayoutRectsWithDrawableSize:[self _resolvedNativeDrawableSize]];
	return [self _screenSpaceLocationFromViewPoint:[touch locationInView:self]];
}

- (void) layoutSubviews {
	[super layoutSubviews];
	[self mtkView:self drawableSizeWillChange:[self _resolvedNativeDrawableSize]];
}

- (void) safeAreaInsetsDidChange {
	[super safeAreaInsetsDidChange];
	[self mtkView:self drawableSizeWillChange:[self _resolvedNativeDrawableSize]];
}
#endif

- (void) drawInMTKView:(nonnull MTKView*)view {
	[self _syncLayoutRectsWithDrawableSize:[self _resolvedNativeDrawableSize]];
	id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
	MTLRenderPassDescriptor* renderPassDescriptor = view.currentRenderPassDescriptor;
	if(renderPassDescriptor != nil) {
		RENDER = [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
		[RENDER setViewport:(MTLViewport){(double)0, (double)0, (double)E_MAX(1, NATIVE_RECT.width), (double)E_MAX(1, NATIVE_RECT.height), (double)-1, (double)1}];
		[RENDER setRenderPipelineState:_pipelineState];
		ESystem::MatrixSetModelDefault();
		ESystem::MatrixSetProjectionDefault();
		ESystem::MatrixUpdate();
		ESystem::RunDrawCallbacks();
		[RENDER endEncoding];

		id<CAMetalDrawable> drawable = view.currentDrawable;
		if(drawable != nil)
			[commandBuffer presentDrawable:drawable];
	}
#if TARGET_OS_OSX
	if(FRAME_CAPTURE_PENDING && !FRAME_CAPTURE_IN_FLIGHT) {
		FRAME_CAPTURE_PENDING = false;
		FRAME_CAPTURE_IN_FLIGHT = true;
		const std::string capturePath = FRAME_CAPTURE_PATH;
		const FrameCaptureCallback captureCallback = FRAME_CAPTURE_CALLBACK;
		[commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
			(void)buffer;
			dispatch_async(dispatch_get_main_queue(), ^{
				int captureWidth = 0;
				int captureHeight = 0;
				std::string captureError;
				const bool captureOk = _CaptureWindowPNG(ACTIVE_WINDOW, capturePath, captureWidth, captureHeight, captureError);
				if(captureCallback != nullptr) {
					captureCallback(
						captureOk,
						captureWidth,
						captureHeight,
						EString(capturePath.c_str()),
						EString(captureError.c_str())
					);
				}
				FRAME_CAPTURE_IN_FLIGHT = false;
			});
		}];
	}
#endif
	[commandBuffer commit];
	if(PROCESS_EXIT_REQUESTED)
		_RequestProcessTermination();
}

- (void) mtkView:(nonnull MTKView*)view drawableSizeWillChange:(CGSize)size {
	[self _syncLayoutRectsWithDrawableSize:size];

#if _DEBUG
	ESystem::Debug(
		"Layout update native=(%d,%d,%d,%d) nativeSafe=(%d,%d,%d,%d) screen=(%d,%d,%d,%d) safe=(%d,%d,%d,%d) design=(%d,%d,%d,%d)\n",
		NATIVE_RECT.x, NATIVE_RECT.y, NATIVE_RECT.width, NATIVE_RECT.height,
		NATIVE_SAFE_RECT.x, NATIVE_SAFE_RECT.y, NATIVE_SAFE_RECT.width, NATIVE_SAFE_RECT.height,
		SCREEN_RECT.x, SCREEN_RECT.y, SCREEN_RECT.width, SCREEN_RECT.height,
		SAFE_RECT.x, SAFE_RECT.y, SAFE_RECT.width, SAFE_RECT.height,
		DESIGN_RECT.x, DESIGN_RECT.y, DESIGN_RECT.width, DESIGN_RECT.height
	);
#endif
}

- (void) encodeWithCoder:(nonnull NSCoder*)aCoder {
}

- (BOOL) acceptsFirstResponder {
	return YES;
}

- (BOOL) becomeFirstResponder {
	[super becomeFirstResponder];
	return YES;
}

- (BOOL) resignFirstResponder {
	return [super resignFirstResponder];
}

#if TARGET_OS_IOS
- (void) touchesBegan: (NSSet*)touches withEvent:(UIEvent*)event {
	for(UITouch* touch in touches) {
		CGPoint location = [self _screenSpaceTouchLocation:touch];
		ESystem::RunTouchCallbacks((int)location.x, (int)location.y);
	}
}

- (void) touchesMoved: (NSSet*)touches withEvent:(UIEvent*)event {
	for(UITouch* touch in touches) {
		CGPoint location = [self _screenSpaceTouchLocation:touch];
		ESystem::RunTouchMoveCallbacks((int)location.x, (int)location.y);
	}
}

- (void) touchesEnded: (NSSet*)touches withEvent:(UIEvent*)event {
	for(UITouch* touch in touches) {
		CGPoint location = [self _screenSpaceTouchLocation:touch];
		ESystem::RunTouchUpCallbacks((int)location.x, (int)location.y);
	}
}

- (void) touchesCancelled: (NSSet*)touches withEvent:(UIEvent*)event {
	for(UITouch* touch in touches) {
		CGPoint location = [self _screenSpaceTouchLocation:touch];
		ESystem::RunTouchUpCallbacks((int)location.x, (int)location.y);
	}
}

#else // TARGET_OS_MAC
- (void) mouseDown: (NSEvent*)theEvent {
	NSPoint location = [self convertPoint:[theEvent locationInWindow] fromView:nil];
	[self _syncLayoutRectsWithDrawableSize:[self _resolvedNativeDrawableSize]];
	location.y = self.bounds.size.height - location.y;
	location = [self _screenSpaceLocationFromViewPoint:location];
	ESystem::RunTouchCallbacks((int)location.x, (int)location.y); // [theEvent buttonNumber]
}

- (void) rightMouseDown: (NSEvent*)theEvent {
	NSPoint location = [self convertPoint:[theEvent locationInWindow] fromView:nil];
	[self _syncLayoutRectsWithDrawableSize:[self _resolvedNativeDrawableSize]];
	location.y = self.bounds.size.height - location.y;
	location = [self _screenSpaceLocationFromViewPoint:location];
	ESystem::RunTouchCallbacks((int)location.x, (int)location.y);
}

- (void) otherMouseDown: (NSEvent*)theEvent {
	NSPoint location = [self convertPoint:[theEvent locationInWindow] fromView:nil];
	[self _syncLayoutRectsWithDrawableSize:[self _resolvedNativeDrawableSize]];
	location.y = self.bounds.size.height - location.y;
	location = [self _screenSpaceLocationFromViewPoint:location];
	ESystem::RunTouchCallbacks((int)location.x, (int)location.y);
}

- (void) mouseUp: (NSEvent*)theEvent {
	NSPoint location = [self convertPoint:[theEvent locationInWindow] fromView:nil];
	[self _syncLayoutRectsWithDrawableSize:[self _resolvedNativeDrawableSize]];
	location.y = self.bounds.size.height - location.y;
	location = [self _screenSpaceLocationFromViewPoint:location];
	ESystem::RunTouchUpCallbacks((int)location.x, (int)location.y);
}

- (void) rightMouseUp: (NSEvent*)theEvent {
	NSPoint location = [self convertPoint:[theEvent locationInWindow] fromView:nil];
	[self _syncLayoutRectsWithDrawableSize:[self _resolvedNativeDrawableSize]];
	location.y = self.bounds.size.height - location.y;
	location = [self _screenSpaceLocationFromViewPoint:location];
	ESystem::RunTouchUpCallbacks((int)location.x, (int)location.y);
}

- (void) otherMouseUp: (NSEvent*)theEvent {
	NSPoint location = [self convertPoint:[theEvent locationInWindow] fromView:nil];
	[self _syncLayoutRectsWithDrawableSize:[self _resolvedNativeDrawableSize]];
	location.y = self.bounds.size.height - location.y;
	location = [self _screenSpaceLocationFromViewPoint:location];
	ESystem::RunTouchUpCallbacks((int)location.x, (int)location.y);
}

- (void) mouseDragged: (NSEvent*)theEvent {
	NSPoint location = [self convertPoint:[theEvent locationInWindow] fromView:nil];
	[self _syncLayoutRectsWithDrawableSize:[self _resolvedNativeDrawableSize]];
	location.y = self.bounds.size.height - location.y;
	location = [self _screenSpaceLocationFromViewPoint:location];
	ESystem::RunTouchMoveCallbacks((int)location.x, (int)location.y);
}

- (void) rightMouseDragged: (NSEvent*)theEvent {
	NSPoint location = [self convertPoint:[theEvent locationInWindow] fromView:nil];
	[self _syncLayoutRectsWithDrawableSize:[self _resolvedNativeDrawableSize]];
	location.y = self.bounds.size.height - location.y;
	location = [self _screenSpaceLocationFromViewPoint:location];
	ESystem::RunTouchMoveCallbacks((int)location.x, (int)location.y);
}

- (void) otherMouseDragged: (NSEvent*)theEvent {
	NSPoint location = [self convertPoint:[theEvent locationInWindow] fromView:nil];
	[self _syncLayoutRectsWithDrawableSize:[self _resolvedNativeDrawableSize]];
	location.y = self.bounds.size.height - location.y;
	location = [self _screenSpaceLocationFromViewPoint:location];
	ESystem::RunTouchMoveCallbacks((int)location.x, (int)location.y);
}

#endif // TARGET_OS_IOS // TARGET_OS_MAC

@end // _MyMetalView



ERect ESystem::GetScreenRect () {
	return SCREEN_RECT;
}

ERect ESystem::GetSafeRect () {
	return SAFE_RECT;
}

ERect ESystem::GetDesignRect () {
	return DESIGN_RECT;
}

int ESystem::GetFPS () {
	return FPS;
}

void ESystem::Paint (const EColor& color, const ERect& area) {
	static EImage solid(EColor::WHITE);
	solid.DrawRect(area, color);
}

static bool _HasValue (const char* text) {
	return text != nullptr && text[0] != '\0';
}

static bool _HasLaunchArg (const char* flag) {
	if(!_HasValue(flag))
		return false;
	for(int i = 1; i < ARG_C; i++) {
		const char* value = ARG_V != nullptr ? ARG_V[i] : nullptr;
		if(value != nullptr && std::strcmp(value, flag) == 0)
			return true;
	}
	return false;
}

static bool _MatchesMainBundleIdentifier (const char* bundleIdentifier) {
	if(!_HasValue(bundleIdentifier))
		return false;

	CFBundleRef mainBundle = CFBundleGetMainBundle();
	if(mainBundle == nullptr)
		return false;

	CFStringRef mainBundleId = (CFStringRef)CFBundleGetValueForInfoDictionaryKey(mainBundle, kCFBundleIdentifierKey);
	if(mainBundleId == nullptr)
		return false;

	CFStringRef target = CFStringCreateWithCString(kCFAllocatorDefault, bundleIdentifier, kCFStringEncodingUTF8);
	if(target == nullptr)
		return false;

	const bool matches = CFStringCompare(mainBundleId, target, 0) == kCFCompareEqualTo;
	CFRelease(target);
	return matches;
}

static bool _FileExists (const char* path) {
	return path != nullptr && access(path, F_OK) == 0;
}

static bool _EnsureDirectoryPath (const char* path) {
	if(!_HasValue(path))
		return false;

	NSString* directory = [NSString stringWithUTF8String:path];
	if(directory == nil)
		return false;

	NSFileManager* manager = [NSFileManager defaultManager];
	BOOL isDirectory = NO;
	if([manager fileExistsAtPath:directory isDirectory:&isDirectory])
		return isDirectory == YES;

	NSError* error = nil;
	return [manager createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:&error] == YES;
}

static bool _EnsureParentDirectoryForFile (const char* path) {
	if(!_HasValue(path))
		return false;

	NSString* filePath = [NSString stringWithUTF8String:path];
	if(filePath == nil)
		return false;

	NSString* directory = [filePath stringByDeletingLastPathComponent];
	if(directory == nil || [directory length] == 0)
		return false;

	return _EnsureDirectoryPath([directory fileSystemRepresentation]);
}

#if TARGET_OS_OSX
static void _ConfigureHeadlessLayoutRects (int nativeWidth, int nativeHeight) {
	if(nativeWidth <= 0)
		nativeWidth = 1920;
	if(nativeHeight <= 0)
		nativeHeight = 1080;

	NATIVE_RECT.Set(0, 0, nativeWidth, nativeHeight);
	NATIVE_SAFE_RECT = NATIVE_RECT;
	ESystem::GetSystemRects(NATIVE_RECT, NATIVE_SAFE_RECT, DESIGN_RECT, SCREEN_RECT, SAFE_RECT);
}

static bool _CreateHeadlessRenderPipeline (
	id<MTLDevice> device,
	id<MTLRenderPipelineState>* outPipelineState,
	std::string& error
) {
	if(device == nil || outPipelineState == nullptr) {
		error = "Headless validation could not initialize the Metal pipeline.";
		return false;
	}

	constexpr NSString* SHADER = @""
	"#include <metal_stdlib>\n"
	"#include <simd/simd.h>\n"
	"using namespace metal;\n"
	"\n"
	"struct _CustomVertex {\n"
	"	packed_float2 xy;\n"
	"	packed_uchar4 rgba;\n"
	"	packed_float2 uv;\n"
	"};\n"
	"\n"
	"struct _VertexOutput {\n"
	"	float4 xyzw [[position]];\n"
	"	float4 rgba;\n"
	"	float2 uv;\n"
	"};\n"
	"\n"
	"vertex _VertexOutput _VertexShader (constant _CustomVertex* vertexArray [[buffer(0)]],\n"
	"									constant float4x4* matrix  [[buffer(1)]],\n"
	"									ushort vertexID [[vertex_id]]) {\n"
	"	_VertexOutput out;\n"
	"	out.xyzw = *matrix * float4(vertexArray[vertexID].xy, 0.0, 1.0);\n"
	"	out.rgba = float4(vertexArray[vertexID].rgba) / 255.0;\n"
	"	out.uv = float2(vertexArray[vertexID].uv);\n"
	"	return out;\n"
	"}\n"
	"\n"
	"fragment float4 _FragmentShader (_VertexOutput in [[stage_in]],\n"
	"								 texture2d<float> texture [[texture(0)]]) {\n"
	"	return in.rgba * float4(texture.sample(sampler(mag_filter::linear, min_filter::linear), in.uv));\n"
	"}\n"
	"";

	NSError* nsError = nil;
	id<MTLLibrary> library = [device newLibraryWithSource:SHADER options:nil error:&nsError];
	if(library == nil) {
		error = (nsError != nil && nsError.localizedDescription != nil) ? std::string([nsError.localizedDescription UTF8String]) : std::string("Failed to compile the headless validation shader library.");
		return false;
	}

	id<MTLFunction> vertexFunction = [library newFunctionWithName:@"_VertexShader"];
	id<MTLFunction> fragmentFunction = [library newFunctionWithName:@"_FragmentShader"];
	if(vertexFunction == nil || fragmentFunction == nil) {
		error = "Failed to load the default headless validation shader entry points.";
#if !__has_feature(objc_arc)
		if(fragmentFunction != nil)
			[fragmentFunction release];
		if(vertexFunction != nil)
			[vertexFunction release];
		[library release];
#endif
		return false;
	}

	MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
	descriptor.vertexFunction = vertexFunction;
	descriptor.fragmentFunction = fragmentFunction;
	descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
	descriptor.colorAttachments[0].blendingEnabled = YES;
	descriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
	descriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
	descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
	descriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

	id<MTLRenderPipelineState> pipelineState = [device newRenderPipelineStateWithDescriptor:descriptor error:&nsError];
#if !__has_feature(objc_arc)
	[descriptor release];
	[fragmentFunction release];
	[vertexFunction release];
	[library release];
#endif
	if(pipelineState == nil) {
		error = (nsError != nil && nsError.localizedDescription != nil) ? std::string([nsError.localizedDescription UTF8String]) : std::string("Failed to create the headless validation render pipeline.");
		return false;
	}

	*outPipelineState = pipelineState;
	return true;
}

static bool _CaptureTexturePNG (
	id<MTLTexture> texture,
	const std::string& path,
	int& width,
	int& height,
	std::string& error
) {
	if(texture == nil) {
		error = "No headless validation texture is available for capture.";
		return false;
	}
	if(path.empty()) {
		error = "Frame capture path is empty.";
		return false;
	}
	if(_EnsureParentDirectoryForFile(path.c_str()) == false) {
		error = "Failed to create the capture directory.";
		return false;
	}

	width = (int)texture.width;
	height = (int)texture.height;
	if(width <= 0 || height <= 0) {
		error = "Headless validation texture has invalid dimensions.";
		return false;
	}

	const size_t bytesPerRow = (size_t)width * 4;
	std::vector<uint8_t> pixels(bytesPerRow * (size_t)height);
	[texture getBytes:pixels.data() bytesPerRow:bytesPerRow fromRegion:MTLRegionMake2D(0, 0, (NSUInteger)width, (NSUInteger)height) mipmapLevel:0];

	CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
	if(colorSpace == nullptr) {
		error = "Failed to create the PNG color space.";
		return false;
	}

	const CGBitmapInfo bitmapInfo =
		static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedFirst) |
		kCGBitmapByteOrder32Little;

	CGContextRef context = CGBitmapContextCreate(
		pixels.data(),
		(size_t)width,
		(size_t)height,
		8,
		bytesPerRow,
		colorSpace,
		bitmapInfo
	);
	CGColorSpaceRelease(colorSpace);
	if(context == nullptr) {
		error = "Failed to create a bitmap context for the PNG capture.";
		return false;
	}

	CGImageRef image = CGBitmapContextCreateImage(context);
	CGContextRelease(context);
	if(image == nullptr) {
		error = "Failed to create a CGImage from the headless validation frame.";
		return false;
	}

	CFURLRef fileURL = CFURLCreateFromFileSystemRepresentation(
		kCFAllocatorDefault,
		(const UInt8*)path.c_str(),
		(CFIndex)path.length(),
		false
	);
	if(fileURL == nullptr) {
		CGImageRelease(image);
		error = "Failed to create the capture file URL.";
		return false;
	}

	CGImageDestinationRef destination = CGImageDestinationCreateWithURL(fileURL, CFSTR("public.png"), 1, nullptr);
	CFRelease(fileURL);
	if(destination == nullptr) {
		CGImageRelease(image);
		error = "Failed to create the PNG destination.";
		return false;
	}

	CGImageDestinationAddImage(destination, image, nullptr);
	const bool finalized = CGImageDestinationFinalize(destination) == YES;
	CFRelease(destination);
	CGImageRelease(image);
	if(finalized == false) {
		error = "Failed to finalize the PNG capture.";
		return false;
	}

	return true;
}

static bool _CaptureWindowPNG (NSWindow* window, const std::string& path, int& width, int& height, std::string& error) {
	if(window == nil) {
		error = "No active macOS window is available for capture.";
		return false;
	}
	if(path.empty()) {
		error = "Frame capture path is empty.";
		return false;
	}
	if(_EnsureParentDirectoryForFile(path.c_str()) == false) {
		error = "Failed to create the capture directory.";
		return false;
	}

	const NSInteger windowNumber = [window windowNumber];
	if(windowNumber <= 0) {
		error = "The app window does not have a valid window number.";
		return false;
	}

	CGImageRef image = CGWindowListCreateImage(
		CGRectNull,
		kCGWindowListOptionIncludingWindow,
		(CGWindowID)windowNumber,
		kCGWindowImageBoundsIgnoreFraming
	);
	if(image == nullptr) {
		error = "Quartz failed to capture the app window.";
		return false;
	}

	width = (int)CGImageGetWidth(image);
	height = (int)CGImageGetHeight(image);
	NSBitmapImageRep* bitmap = [[NSBitmapImageRep alloc] initWithCGImage:image];
	CGImageRelease(image);
	if(bitmap == nil) {
		error = "Failed to create a bitmap image for the capture.";
		return false;
	}

	NSData* pngData = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
#if !__has_feature(objc_arc)
	[bitmap release];
#endif
	if(pngData == nil) {
		error = "Failed to encode the captured window as PNG.";
		return false;
	}

	NSString* filePath = [NSString stringWithUTF8String:path.c_str()];
	if(filePath == nil || [pngData writeToFile:filePath atomically:YES] == NO) {
		error = "Failed to write capture.png to disk.";
		return false;
	}

	return true;
}

static int _RunHeadlessValidation () {
	int nativeWidth = DESIGN_RECT.width;
	int nativeHeight = DESIGN_RECT.height;
	if(nativeWidth <= 0 || nativeHeight <= 0) {
		nativeWidth = 1920;
		nativeHeight = 1080;
	} else {
		if(nativeWidth < 1920)
			nativeWidth = 1920;
		if(nativeHeight < 1080)
			nativeHeight = 1080;
	}

	DEVICE = MTLCreateSystemDefaultDevice();
	id<MTLCommandQueue> commandQueue = nil;
	id<MTLRenderPipelineState> pipelineState = nil;
	id<MTLTexture> renderTexture = nil;
	if(DEVICE == nil) {
		ESystem::Print("[DRAGON_TEST] No Metal device available; continuing with telemetry-only headless validation.\n");
	}
	else {
		commandQueue = [DEVICE newCommandQueue];
		if(commandQueue == nil) {
			ESystem::Print("[DRAGON_TEST] Failed to create a Metal command queue for headless validation.\n");
#if !__has_feature(objc_arc)
			[DEVICE release];
#endif
			DEVICE = nil;
			return EXIT_FAILURE;
		}

		std::string pipelineError;
		if(_CreateHeadlessRenderPipeline(DEVICE, &pipelineState, pipelineError) == false) {
			ESystem::Print("[DRAGON_TEST] %s\n", pipelineError.c_str());
#if !__has_feature(objc_arc)
			[commandQueue release];
			[DEVICE release];
#endif
			DEVICE = nil;
			return EXIT_FAILURE;
		}

		MTLTextureDescriptor* textureDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:(NSUInteger)nativeWidth height:(NSUInteger)nativeHeight mipmapped:NO];
		textureDescriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
		textureDescriptor.storageMode = MTLStorageModeShared;
		renderTexture = [DEVICE newTextureWithDescriptor:textureDescriptor];
		if(renderTexture == nil) {
			ESystem::Print("[DRAGON_TEST] Failed to create the headless validation render texture.\n");
#if !__has_feature(objc_arc)
			[pipelineState release];
			[commandQueue release];
			[DEVICE release];
#endif
			DEVICE = nil;
			return EXIT_FAILURE;
		}
	}

	_ConfigureHeadlessLayoutRects(nativeWidth, nativeHeight);
	ESystem::RunStartupCallbacks();

	while(PROCESS_EXIT_REQUESTED == false) {
		@autoreleasepool {
			ESystem::MatrixSetModelDefault();
			ESystem::MatrixSetProjectionDefault();
			if(commandQueue != nil && pipelineState != nil && renderTexture != nil) {
				id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
				MTLRenderPassDescriptor* renderPassDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
				renderPassDescriptor.colorAttachments[0].texture = renderTexture;
				renderPassDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
				renderPassDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
				renderPassDescriptor.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);

				RENDER = [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
				[RENDER setViewport:(MTLViewport){(double)0, (double)0, (double)E_MAX(1, NATIVE_RECT.width), (double)E_MAX(1, NATIVE_RECT.height), (double)-1, (double)1}];
				[RENDER setRenderPipelineState:pipelineState];
				ESystem::MatrixUpdate();
				ESystem::RunDrawCallbacks();
				[RENDER endEncoding];
				RENDER = nil;

				bool shouldCapture = FRAME_CAPTURE_PENDING && !FRAME_CAPTURE_IN_FLIGHT;
				std::string capturePath;
				FrameCaptureCallback captureCallback = nullptr;
				if(shouldCapture) {
					FRAME_CAPTURE_PENDING = false;
					FRAME_CAPTURE_IN_FLIGHT = true;
					capturePath = FRAME_CAPTURE_PATH;
					captureCallback = FRAME_CAPTURE_CALLBACK;
				}

				[commandBuffer commit];
				[commandBuffer waitUntilCompleted];

				if(shouldCapture) {
					int captureWidth = 0;
					int captureHeight = 0;
					std::string captureError;
					const bool captureOk = _CaptureTexturePNG(renderTexture, capturePath, captureWidth, captureHeight, captureError);
					if(captureCallback != nullptr) {
						captureCallback(
							captureOk,
							captureWidth,
							captureHeight,
							EString(capturePath.c_str()),
							EString(captureError.c_str())
						);
					}
					FRAME_CAPTURE_IN_FLIGHT = false;
				}
			}
			else {
				ESystem::MatrixUpdate();
				ESystem::RunDrawCallbacks();
			}
		}

		if(PROCESS_EXIT_REQUESTED)
			break;
		usleep((useconds_t)std::max(1, 1000000 / E_MAX(1, FPS)));
	}

	ESystem::RunShutdownCallbacks();
	RENDER = nil;
#if !__has_feature(objc_arc)
	[renderTexture release];
	[pipelineState release];
	[commandQueue release];
	[DEVICE release];
#endif
	DEVICE = nil;
	return PROCESS_EXIT_CODE;
}
#endif

#if TARGET_OS_IOS
static bool _CanWriteProbePath (const char* path) {
	if(path == nullptr)
		return false;

	FILE* file = fopen(path, "w");
	if(file == nullptr)
		return false;

	fclose(file);
	remove(path);
	return true;
}
#endif

bool ESystem::HasInstalledBundle (const char* bundleIdentifier) {
	if(!_HasValue(bundleIdentifier))
		return false;

	if(_MatchesMainBundleIdentifier(bundleIdentifier))
		return true;

#if TARGET_OS_OSX
	CFStringRef target = CFStringCreateWithCString(kCFAllocatorDefault, bundleIdentifier, kCFStringEncodingUTF8);
	if(target == nullptr)
		return false;

	CFArrayRef urls = LSCopyApplicationURLsForBundleIdentifier(target, nullptr);
	const bool found = (urls != nullptr && CFArrayGetCount(urls) > 0);
	if(urls != nullptr)
		CFRelease(urls);
	CFRelease(target);
	return found;
#else
	(void)bundleIdentifier;
	return false;
#endif
}

bool ESystem::IsCompromisedEnvironment () {
#if TARGET_OS_IOS
	static const char* jailbreakPaths[] = {
		"/Applications/Cydia.app",
		"/Library/MobileSubstrate/MobileSubstrate.dylib",
		"/bin/bash",
		"/usr/sbin/sshd",
		"/private/var/lib/apt"
	};

	for(size_t i = 0; i < sizeof(jailbreakPaths) / sizeof(jailbreakPaths[0]); ++i) {
		if(_FileExists(jailbreakPaths[i]))
			return true;
	}

	return _CanWriteProbePath("/private/genesis_jailbreak_probe.txt");
#else
	return _FileExists("Pirate.txt");
#endif
}

static void _RequestProcessTermination () {
#if TARGET_OS_IOS
	ESystem::RunShutdownCallbacks();
	fflush(stdout);
	fflush(stderr);
	exit(PROCESS_EXIT_CODE);
#else
	if(NSApp != nil)
		[NSApp terminate:nil];
#endif
}

static bool ResolveExecutableResourceDirectory (std::string& directory) {
	char executablePath[PATH_MAX];
	uint32_t executablePathSize = (uint32_t)sizeof(executablePath);
	if(_NSGetExecutablePath(executablePath, &executablePathSize) != 0 || executablePath[0] == '\0')
		return false;

	char resolvedExecutablePath[PATH_MAX];
	const char* executable = realpath(executablePath, resolvedExecutablePath);
	if(executable == nullptr)
		executable = executablePath;

	std::string executableDirectory(executable);
	const std::string::size_type slash = executableDirectory.find_last_of('/');
	if(slash == std::string::npos)
		return false;
	executableDirectory.resize(slash);

	std::string resourcesDirectory = executableDirectory + "/../Resources";
	char resolvedResourcesPath[PATH_MAX];
	if(realpath(resourcesDirectory.c_str(), resolvedResourcesPath) != nullptr)
		resourcesDirectory = resolvedResourcesPath;

	if(access(resourcesDirectory.c_str(), R_OK) != 0)
		return false;

	directory = resourcesDirectory;
	return true;
}



static const char* GetResourceDirectory () {
	static std::string RESOURCE_DIRECTORY;
	if(RESOURCE_DIRECTORY.empty() || RESOURCE_DIRECTORY == "./") {
		if([NSBundle mainBundle] != nil && [[NSBundle mainBundle] resourceURL] != nil) {
			const char* bundlePath = [[[NSBundle mainBundle] resourceURL] fileSystemRepresentation];
			if(bundlePath != nullptr && bundlePath[0] != '\0')
				RESOURCE_DIRECTORY = std::string(bundlePath);
		}
		if(RESOURCE_DIRECTORY.empty())
			ResolveExecutableResourceDirectory(RESOURCE_DIRECTORY);
		if(RESOURCE_DIRECTORY.empty())
			RESOURCE_DIRECTORY = "./";
		ESystem::Debug("Resources: %s ... ", RESOURCE_DIRECTORY.c_str());
	}
	return RESOURCE_DIRECTORY.c_str();
}

static bool ResolveResourceFilePath (const EString& path, EString& resolvedPath) {
	if(path.IsEmpty())
		return false;

	const EString bundledPath = EString().Format("%s/%s", GetResourceDirectory(), (const char*)path);
	if(access((const char*)bundledPath, R_OK) == 0) {
		resolvedPath = bundledPath;
		return true;
	}

	const char* rawPath = (const char*)path;
	const char* slash = std::strrchr(rawPath, '/');
	if(slash != nullptr && slash[1] != '\0') {
		const EString flattenedPath = EString().Format("%s/%s", GetResourceDirectory(), slash + 1);
		if(access((const char*)flattenedPath, R_OK) == 0) {
			resolvedPath = flattenedPath;
			return true;
		}
	}

	return false;
}



void ESystem::SetDefaultWD () {
	static std::string DEFAULT_WD;

	const char* resourceDirectory = GetResourceDirectory();
	if(resourceDirectory == nullptr || resourceDirectory[0] == '\0')
		return;
	if(DEFAULT_WD == resourceDirectory)
		return;

	if(chdir(resourceDirectory) == 0)
		DEFAULT_WD = resourceDirectory;

#if _DEBUG
	char cwd[PATH_MAX];
	if(getcwd(cwd, sizeof(cwd)) != nullptr)
		ESystem::Debug("WD: %s\n", cwd);
#endif
}

int64_t ESystem::ResourceSize (const EString& name) {
	return ResourceSizeFromFile(name);
}



int64_t ESystem::ResourceSizeFromFile (const EString& path) {
	EString resolvedPath;
	if(ResolveResourceFilePath(path, resolvedPath) == false) {
		ESystem::Debug("Failed to find resource \"%s\"!\n", (const char*)path);
		return 0;
	}

	FILE* file = fopen((const char*)resolvedPath, "rb");
	if(file == nullptr) {
		ESystem::Debug("Failed to find resource \"%s\"!\n", (const char*)path);
		return 0;
	}
	fseeko(file, 0, SEEK_END);
	int64_t size = ftello(file);
	fclose(file);
	return size;
}



bool ESystem::ResourceRead (const EString& name, void* data, int64_t size) {
	if(size <= 0)
		size = ResourceSizeFromFile(name);
	if(size <= 0)
		return false;
	return ResourceReadFromFile(name, data, size);
}



bool ESystem::ResourceReadFromFile (const EString& path, void* data, int64_t size) {
	if(size < 0 || (size > 0 && data == nullptr)) {
		ESystem::Debug("Invalid resource read arguments for \"%s\"!\n", (const char*)path);
		return false;
	}
	if(_FitsSizeT(size) == false) {
		ESystem::Debug("Resource \"%s\" is too large to read on this platform!\n", (const char*)path);
		return false;
	}

	EString resolvedPath;
	if(ResolveResourceFilePath(path, resolvedPath) == false) {
		ESystem::Debug("Failed to open resource \"%s\"!\n", (const char*)path);
		return false;
	}

	FILE* file = fopen((const char*)resolvedPath, "rb");
	if(file == nullptr) {
		ESystem::Debug("Failed to open resource \"%s\"!\n", (const char*)path);
		return false;
	}

	if(fseeko(file, 0, SEEK_END) != 0) {
		ESystem::Debug("Failed to seek resource \"%s\"!\n", (const char*)path);
		fclose(file);
		return false;
	}
	const off_t fileSize = ftello(file);
	if(fileSize < 0 || (int64_t)fileSize < size) {
		ESystem::Debug("Resource \"%s\" is larger than data buffer!\n", (const char*)path);
		fclose(file);
		return false;
	}
	if(fseeko(file, 0, SEEK_SET) != 0) {
		ESystem::Debug("Failed to rewind resource \"%s\"!\n", (const char*)path);
		fclose(file);
		return false;
	}
	if(size == 0) {
		fclose(file);
		return true;
	}

	if(fread(data, 1, (size_t)size, file) != (size_t)size) {
		ESystem::Debug("Failed to read resource \"%s\"!\n", (const char*)path);
		fclose(file);
		return false;
	}

	fclose(file);
	return true;
}



bool ESystem::ResourceWrite (const EString& name, void* data, int64_t size) {
	if(size < 0 || (size > 0 && data == nullptr)) {
		ESystem::Debug("Invalid resource write arguments for \"%s\"!\n", (const char*)name);
		return false;
	}
	if(_FitsSizeT(size) == false) {
		ESystem::Debug("Resource \"%s\" write size is too large!\n", (const char*)name);
		return false;
	}
	const size_t byteCount = (size_t)size;

	const EString fullPath = EString().Format("%s/%s", GetResourceDirectory(), (const char*)name);
	if(_EnsureParentDirectoryForFile((const char*)fullPath) == false) {
		ESystem::Debug("Failed to create parent directory for resource \"%s\"!\n", (const char*)name);
		return false;
	}

	FILE* file = fopen((const char*)fullPath, "wb");
	if(file == nullptr) {
		ESystem::Debug("Failed to open resource \"%s\" for writing!\n", (const char*)name);
		return false;
	}

	if(byteCount > 0 && fwrite(data, 1, byteCount, file) != byteCount) {
		ESystem::Debug("Failed to write resource \"%s\"!\n", (const char*)name);
		fclose(file);
		return false;
	}

	if(fflush(file) != 0) {
		ESystem::Debug("Failed to flush resource \"%s\"!\n", (const char*)name);
		fclose(file);
		return false;
	}
	fclose(file);
	return true;
}



static const char* GetSaveDirectory () {
	static std::string SAVE_DIRECTORY;
	if(SAVE_DIRECTORY.empty()) {
		const char* overrideDirectory = getenv("DRAGON_SAVE_ROOT");
		if(!_HasValue(overrideDirectory))
			overrideDirectory = getenv("DRAGON_TEST_SAVE_ROOT");
		if(_HasValue(overrideDirectory) && _EnsureDirectoryPath(overrideDirectory))
			SAVE_DIRECTORY = std::string(overrideDirectory);
		if(SAVE_DIRECTORY.empty()) {
			NSURL* support = [[[NSFileManager defaultManager] URLForDirectory:NSApplicationSupportDirectory inDomain:NSUserDomainMask appropriateForURL:nil create:YES error:nil] URLByAppendingPathComponent:[[NSBundle mainBundle] bundleIdentifier]];
			if([[NSFileManager defaultManager] fileExistsAtPath:[support path]] == NO)
				[[NSFileManager defaultManager] createDirectoryAtPath:[support path] withIntermediateDirectories:YES attributes:nil error:nil];
			SAVE_DIRECTORY = std::string([support fileSystemRepresentation]);
		}
		ESystem::Debug("Saves: \"%s\"\n", SAVE_DIRECTORY.c_str());
	}
	return SAVE_DIRECTORY.c_str();
}

static bool DeleteSavePath (const EString& path);

static EString GetSavePath (const EString& name) {
	return EString().Format("%s/%s.sav", GetSaveDirectory(), (const char*)name);
}

static EString GetSavePreviousPath (const EString& name) {
	return EString().Format("%s/%s.sav.prev", GetSaveDirectory(), (const char*)name);
}

static EString GetSaveTemporaryPath (const EString& name) {
	return EString().Format("%s/%s.sav.tmp", GetSaveDirectory(), (const char*)name);
}

static EString GetLegacySavePath (const EString& name) {
	return EString().Format("%s/%s.save", GetSaveDirectory(), (const char*)name);
}

static bool SyncFileForSave (FILE* file) {
	if(file == nullptr)
		return false;
	if(fflush(file) != 0)
		return false;
	const int fd = fileno(file);
	return fd < 0 || fsync(fd) == 0;
}

static void SyncSaveDirectoryBestEffort () {
	const int fd = open(GetSaveDirectory(), O_RDONLY);
	if(fd < 0)
		return;
	(void)fsync(fd);
	close(fd);
}

static bool ReadSavePath (const EString& path, void* data, int64_t size) {
	FILE* file = fopen((const char*)path, "rb");
	if(file == nullptr)
		return false;

	int64_t archiveSize = 0;
	if(fread(&archiveSize, sizeof(archiveSize), 1, file) != 1 || archiveSize <= 0) {
		fclose(file);
		return false;
	}

	const off_t current = ftello(file);
	if(current < 0 || fseeko(file, 0, SEEK_END) != 0 || ftello(file) - current < archiveSize) {
		fclose(file);
		return false;
	}
	fseeko(file, current, SEEK_SET);

	std::unique_ptr<uint8_t[]> archive(new uint8_t[archiveSize]);
	const bool ok = fread(archive.get(), archiveSize, 1, file) == 1
		&& EArchive::Decompress(archive.get(), archiveSize, data, size) == size;
	fclose(file);
	return ok;
}

bool ESystem::SaveRead (const EString& name, void* data, int64_t size) {

#if DEBUG
	GetSaveDirectory(); // This is to make the debug output in GetSaveDirectory happen before the debug output in this function
	ESystem::Debug("Reading save \"%s\" ... ", (const char*)name);
	int64_t elapse = ESystem::GetMilliseconds();
#endif

	const EString savePath = GetSavePath(name);
	const EString previousPath = GetSavePreviousPath(name);
	const bool primaryOk = ReadSavePath(savePath, data, size);
	const bool ok = primaryOk || ReadSavePath(previousPath, data, size);
	if(!ok) {
		ESystem::Debug("Failed to read save data!\n");
		return false;
	}
	if(!primaryOk)
		ESystem::Debug("Recovered from previous save copy.\n");

#if DEBUG
	elapse = ESystem::GetMilliseconds() - elapse;
	ESystem::Debug("Done (%d ms)\n", (int)elapse);
#endif

	return true;
}



bool ESystem::SaveWrite (const EString& name, const void* data, int64_t size) {

#if DEBUG
	ESystem::Debug("Writing save \"%s\" ... ", (const char*)name);
	int64_t elapse = ESystem::GetMilliseconds();
#endif

	int64_t archiveSize = EArchive::GetBufferBounds(size);
	std::unique_ptr<uint8_t[]> archive(new uint8_t[archiveSize]);
	archiveSize = EArchive::Compress(data, size, archive.get(), archiveSize);
	if(archiveSize == 0) {
		ESystem::Debug("Failed to compress save data!\n");
		return false;
	}

	const EString savePath = GetSavePath(name);
	const EString previousPath = GetSavePreviousPath(name);
	const EString temporaryPath = GetSaveTemporaryPath(name);
	DeleteSavePath(temporaryPath);

	FILE* file = fopen((const char*)temporaryPath, "wb+");
	if(file == nullptr) {
		ESystem::Debug("Failed to open save for writing!\n");
		return false;
	}

	if(fwrite(&archiveSize, sizeof(archiveSize), 1, file) != 1) {
		ESystem::Debug("Failed to write save data archive size!\n");
		fclose(file);
		return false;
	}

	if(fwrite(archive.get(), archiveSize, 1, file) != 1) {
		ESystem::Debug("Failed to write save data archive!\n");
		fclose(file);
		return false;
	}

	if(!SyncFileForSave(file)) {
		ESystem::Debug("Failed to flush save data!\n");
		fclose(file);
		DeleteSavePath(temporaryPath);
		return false;
	}
	if(fclose(file) != 0) {
		ESystem::Debug("Failed to close save file cleanly!\n");
		DeleteSavePath(temporaryPath);
		return false;
	}
	
	bool rotatedCurrentToPrevious = false;
	if(size > 0) {
		std::unique_ptr<uint8_t[]> buffer(new uint8_t[size]);
		if(ReadSavePath(savePath, buffer.get(), size)) {
		if(!DeleteSavePath(previousPath)) {
			ESystem::Debug("Failed to clear previous save copy!\n");
			DeleteSavePath(temporaryPath);
			return false;
		}
		errno = 0;
		if(rename((const char*)savePath, (const char*)previousPath) != 0) {
			ESystem::Debug("Failed to rotate previous save copy!\n");
			DeleteSavePath(temporaryPath);
			return false;
		}
		rotatedCurrentToPrevious = true;
		}
	}
	if(rename((const char*)temporaryPath, (const char*)savePath) != 0) {
		ESystem::Debug("Failed to install new save data!\n");
		DeleteSavePath(temporaryPath);
		if(rotatedCurrentToPrevious) {
			errno = 0;
			(void)rename((const char*)previousPath, (const char*)savePath);
		}
		return false;
	}
	SyncSaveDirectoryBestEffort();

#if DEBUG
	elapse = ESystem::GetMilliseconds() - elapse;
	ESystem::Debug("Done (%d bytes compressed to %d bytes in %d ms)\n", (int)size, (int)archiveSize, (int)elapse);
#endif

	return true;
}


static bool DeleteSavePath (const EString& path) {
	if(path.IsEmpty())
		return false;

	NSString* filePath = [NSString stringWithUTF8String:path];
	if(filePath == nil)
		return false;

	NSFileManager* manager = [NSFileManager defaultManager];
	if([manager fileExistsAtPath:filePath] == NO)
		return true;

	NSError* error = nil;
	if([manager removeItemAtPath:filePath error:&error] == NO) {
		if(error != nil) ESystem::Debug("Failed to delete save \"%s\": %s\n", (const char*)path, [[error localizedDescription] UTF8String]);
		return false;
	}
	return true;
}

bool ESystem::SaveDelete (const EString& name) {
	if(name.IsEmpty())
		return false;

	const EString savePath = GetSavePath(name);
	const EString previousPath = GetSavePreviousPath(name);
	const EString temporaryPath = GetSaveTemporaryPath(name);
	const EString legacyPath = GetLegacySavePath(name);
	return DeleteSavePath(savePath)
		&& DeleteSavePath(previousPath)
		&& DeleteSavePath(temporaryPath)
		&& DeleteSavePath(legacyPath);
}



std::vector<EString> ESystem::GetFileNamesInDirectory (const EString& path) {
	std::vector<EString> files;

	EString directory = path;
	if(directory.IsEmpty())
		directory = "./";

	if(directory[directory.GetLength() - 1] != '/')
		directory += "/";

	DIR* dir = opendir(directory);
	if(dir == nullptr) {
		FILE* file = fopen(path, "rb");
		if(file != nullptr) {
			files.push_back(path);
			fclose(file);
			return files;
		}
		return files;
	}

	for(dirent* info = readdir(dir); info != nullptr; info = readdir(dir)) {
		if(EString::strcmp(info->d_name, ".") == 0 || EString::strcmp(info->d_name, "..") == 0)
			continue;

		const EString entryPath = directory + info->d_name;
		bool isDirectory = info->d_type == DT_DIR;
		if(info->d_type == DT_UNKNOWN) {
			struct stat entryStat;
			if(stat((const char*)entryPath, &entryStat) == 0)
				isDirectory = S_ISDIR(entryStat.st_mode);
		}

		if(isDirectory) {
			std::vector<EString> sub = GetFileNamesInDirectory(entryPath);
			files.reserve(files.size() + sub.size());
			files.insert(files.end(), sub.begin(), sub.end());
			continue;
		}
		files.push_back(entryPath);
	}

	closedir(dir);
	return files;
}



void ESystem::SetLaunchDesignSize (int width, int height) {
	DESIGN_RECT.Set(0, 0, width, height);
	ESystem::GetSystemRects(NATIVE_RECT, NATIVE_SAFE_RECT, DESIGN_RECT, SCREEN_RECT, SAFE_RECT);
}



void ESystem::SetLaunchTargetFPS (int fps) {
	FPS = fps;
}



void ESystem::SetLaunchArgs (int argc, char* argv[]) {
	ARG_C = argc;
	ARG_V = argv;
}



int ESystem::GetArgCount () {
	return ARG_C;
}



const char* ESystem::GetArgValue (int index) {
	if(index < 0 || index >= ARG_C || ARG_V == nullptr)
		return nullptr;
	return ARG_V[index];
}



#if defined(DRAGON_ENGINE_ENABLE_MAIN)
int main (int argc, char* argv[]) {
	ESystem::SetLaunchArgs(argc, argv);
	return ESystem::Run();
}
#endif



int ESystem::Run () {
	ESystem::RunPreRunCallbacks();
	if(PROCESS_EXIT_REQUESTED)
		return PROCESS_EXIT_CODE;
#if TARGET_OS_IOS
	ESystem::Debug("Running iOS Application...\n");
	@autoreleasepool {
		return UIApplicationMain(ARG_C, ARG_V, nil, NSStringFromClass([_MyAppDelegate class]));
	}
#else // TARGET_OS_MAC
#if defined(DRAGON_TEST)
	if(_HasLaunchArg("--automation"))
		return _RunHeadlessValidation();
#endif
	ESystem::Debug("Running MacOS Application...\n");
	@autoreleasepool {
		_MyAppDelegate* appDelegate = [[_MyAppDelegate alloc] init];
		[[NSApplication sharedApplication] setDelegate:appDelegate];
		[[NSApplication sharedApplication] run];
#if !__has_feature(objc_arc)
		[appDelegate release];
#endif
	}
#endif // TARGET_OS_IOS // TARGET_OS_MAC
	return PROCESS_EXIT_CODE;
}



void ESystem::MatrixSetModelDefault () {
	MODEL_MATRIX.SetIdentity();
}

void ESystem::MatrixSetProjectionDefault () {
	PROJECTION_MATRIX.SetOrtho2D(
		(float)SCREEN_RECT.x,
		(float)(SCREEN_RECT.x + SCREEN_RECT.width),
		(float)(SCREEN_RECT.y + SCREEN_RECT.height),
		(float)SCREEN_RECT.y,
		(float)-1,
		(float)1
	);
}

void ESystem::MatrixTranslateModel (float x, float y) {
	MODEL_MATRIX.SetTranslation((float)x, (float)y, (float)0);
}

void ESystem::MatrixTranslateProjection (float x, float y) {
	PROJECTION_MATRIX.SetTranslation((float)x, (float)y, (float)0);
}

void ESystem::MatrixScaleModel (float x, float y) {
	MODEL_MATRIX.SetScale((float)x, (float)y, (float)1);
}

void ESystem::MatrixScaleProjection (float x, float y) {
	PROJECTION_MATRIX.SetScale((float)x, (float)y, (float)1);
}

void ESystem::MatrixRotateModel (float degrees) {
	MODEL_MATRIX.SetRotation((float)degrees * ((float)M_PI / (float)180));
}

void ESystem::MatrixRotateProjection (float degrees) {
	PROJECTION_MATRIX.SetRotation((float)degrees * ((float)M_PI / (float)180));
}

void ESystem::MatrixUpdate () {
	if(RENDER != nil) {
		static EMatrix32_4x4 MATRIX;
		MATRIX = PROJECTION_MATRIX * MODEL_MATRIX;
		[RENDER setVertexBytes:&MATRIX length:sizeof(MATRIX) atIndex:1];
	}
}



void ESystem::Print (const char* message, ...) {
	if (message) {
		va_list args;
		va_start(args, message);
		vprintf(message, args);
		va_end(args);
	}
}



void ESystem::Debug (const char* message, ...) {
#if DEBUG
	if (message) {
		va_list args;
		va_start(args, message);
		vprintf(message, args);
		va_end(args);
	}
#endif
}

void ESystem::ReportTextDraw (const EString& text, const ERect& rect) {
	if(text.IsEmpty() || rect.width <= 0 || rect.height <= 0)
		return;
	REPORTED_TEXT_DRAWS.push_back({text, rect});
	if(REPORTED_TEXT_DRAWS.size() > 1024)
		REPORTED_TEXT_DRAWS.erase(REPORTED_TEXT_DRAWS.begin(), REPORTED_TEXT_DRAWS.begin() + 512);
}

void ESystem::ClearReportedTextDraws () {
	REPORTED_TEXT_DRAWS.clear();
}

int ESystem::GetReportedTextDrawCount () {
	return (int)REPORTED_TEXT_DRAWS.size();
}

bool ESystem::GetReportedTextDraw (int index, EString& text, ERect& rect) {
	if(index < 0 || index >= (int)REPORTED_TEXT_DRAWS.size())
		return false;
	text = REPORTED_TEXT_DRAWS[(size_t)index].text;
	rect = REPORTED_TEXT_DRAWS[(size_t)index].rect;
	return true;
}

bool ESystem::CanCaptureFramePNG () {
#if TARGET_OS_OSX
	return ACTIVE_WINDOW != nil || DEVICE != nil;
#else
	return false;
#endif
}

bool ESystem::RequestFrameCapturePNG (
	const EString& path,
	void (* callback) (bool success, int width, int height, const EString& path, const EString& error)
) {
	if(path.IsEmpty()) {
		if(callback != nullptr)
			callback(false, 0, 0, path, EString("Frame capture path is empty."));
		return false;
	}

#if TARGET_OS_OSX
	if(CanCaptureFramePNG() == false) {
		if(callback != nullptr)
			callback(false, 0, 0, path, EString("Frame capture is unavailable because no macOS capture device is active."));
		return false;
	}
	if(FRAME_CAPTURE_PENDING || FRAME_CAPTURE_IN_FLIGHT) {
		if(callback != nullptr)
			callback(false, 0, 0, path, EString("A frame capture request is already in flight."));
		return false;
	}
	FRAME_CAPTURE_PATH = path.String();
	FRAME_CAPTURE_CALLBACK = callback;
	FRAME_CAPTURE_PENDING = true;
	return true;
#else
	(void)callback;
	ESystem::Debug("Frame capture is only supported by the standalone macOS DRAGON_TEST runtime.\n");
	return false;
#endif
}

bool ESystem::SetFullscreenEnabled (bool enabled) {
#if TARGET_OS_OSX
	if(ACTIVE_WINDOW == nil)
		return false;
	const bool fullscreen = (([ACTIVE_WINDOW styleMask] & NSWindowStyleMaskFullScreen) != 0);
	if(fullscreen != enabled)
		[ACTIVE_WINDOW toggleFullScreen:nil];
	return true;
#else
	(void)enabled;
	return false;
#endif
}

bool ESystem::IsFullscreenEnabled () {
#if TARGET_OS_OSX
	if(ACTIVE_WINDOW == nil)
		return false;
	return ([ACTIVE_WINDOW styleMask] & NSWindowStyleMaskFullScreen) != 0;
#else
	return false;
#endif
}

void ESystem::RequestExit (int exitCode) {
	PROCESS_EXIT_CODE = exitCode;
	PROCESS_EXIT_REQUESTED = true;
	if(RENDER == nil) {
#if TARGET_OS_IOS
		_RequestProcessTermination();
#else
		if(NSApp != nil)
			_RequestProcessTermination();
#endif
	}
}



#endif // __APPLE__

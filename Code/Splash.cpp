/**
 * @file Splash.cpp
 * @brief Splash/menu scene rendering, interaction flow, and startup gating implementation.
 */
#include "Splash.h"
#include "LayoutUtil.h"

#include <cstdlib>
#include <climits>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace {
constexpr int kSplashMenuRowCount = 7;
// Row tops measured from baked SplashBackground button chrome (legacy front-door art).
constexpr int kSplashMenuRowY[kSplashMenuRowCount] = {230, 329, 370, 428, 469, 510, 568};
constexpr int kSplashMenuMarkerX = 11;
constexpr int kSplashMenuMarkerWidth = 22;
constexpr int kSplashMenuMarkerHeight = 22;
constexpr int kSplashMenuMarkerYOffset = 0;
constexpr int kSplashMenuHitWidth = 296;
constexpr int kSplashMenuPanelExtraRight = 92;
constexpr int64_t kSplashMenuHighlightDurationMs = 450;

#if defined(DRAGON_TEST)
void ReportValidationLabel (const EString& text, const ERect& rect) {
	if(text.IsEmpty() == false && rect.width > 0 && rect.height > 0)
		ESystem::ReportTextDraw(text, rect);
}
#endif
}

Splash::Splash ()
:	background(EColor(0x101722ff))
,	panel(EColor(0x1d2b42ff))
,	banner(EColor(0x365780ff))
,	progressBackground(EColor(0x0c1320ff))
,	progressFill(EColor(0x74a8e6ff))
,	accent(EColor(0x9ec2ed55))
,	markerOn(EColor(0xffffffdf))
,	markerOff(EColor(0xffffff7f))
,	buttonPrimary(EColor(0x355a82ff))
,	buttonSecondary(EColor(0x6a4b30ff))
,	fontHeader("FontMissionHeader")
,	fontBody("FontMissionText")
,	transition(UITransition::FADE_OUT_BLACK, this)
,	legacySummary(BuildLegacyAssetSummary())
,	continuePreview({})
,	aboutRect()
,	continueRect()
,	slotMenuRect()
,	soundRect()
,	musicRect()
,	fullscreenRect()
,	quitRect()
,	infoRect()
,	infoOkRect()
,	quitConfirmRect()
,	quitYesRect()
,	quitNoRect()
,	infoTitle("")
,	infoBody("")
,	startupMilliseconds(ESystem::GetMilliseconds())
,	messageTimestamp(0)
,	continueSlot(0)
,	hasContinueSlot(false)
,	hasContinuePreview(false)
,	startupReady(false)
,	transitioned(false)
	,	infoActive(false)
	,	quitConfirmActive(false)
	,	menuHoverIndex(-1)
	,	menuHoverTimestamp(0)
	,	touchHandledThisSequence(false)
	,	touchMovedThisSequence(false)
	,	touchDownX(0)
	,	touchDownY(0)
,	messageText("")
{
	DragonEnsureSaveInfo();
	ApplySoundPreference();
	ESystem::SetFullscreenEnabled(DragonIsFullscreenEnabled());
	uint8_t candidateSlot = 0;
	if(gSaveInfo.selectedSlot >= 1 and gSaveInfo.selectedSlot <= DRAGON_ALPHA_SLOT_COUNT and DragonSlotExists(gSaveInfo.selectedSlot))
		candidateSlot = gSaveInfo.selectedSlot;
	else {
		for(int slot = 1; slot <= DRAGON_ALPHA_SLOT_COUNT; slot++) {
			if(DragonSlotExists(slot) == false)
				continue;
			candidateSlot = (uint8_t)slot;
			break;
		}
	}
	if(candidateSlot != 0) {
		continueSlot = candidateSlot;
		hasContinueSlot = true;
		hasContinuePreview = DragonReadSlotPreview(continueSlot, continuePreview);
	}
	gLegacyAssets = legacySummary;

	LoadImageOrFallback(background, "SplashBackground", EColor(0x101722ff));
	LoadImageOrFallback(panel, "SplashPanel", EColor(0x1d2b42ff));
	LoadImageOrFallback(banner, "SplashBanner", EColor(0x365780ff));
	LoadImageOrFallback(progressBackground, "SplashProgressBack", EColor(0x0c1320ff));
	LoadImageOrFallback(progressFill, "SplashProgressFill", EColor(0x74a8e6ff));
	LoadImageOrFallback(accent, "SplashAccent", EColor(0x9ec2ed55));
	LoadImageOrFallback(markerOn, "SplashMenuMarkerOn", EColor(0xffffffdf));
	LoadImageOrFallback(markerOff, "SplashMenuMarkerOff", EColor(0xffffff7f));
	LoadImageOrFallback(buttonPrimary, "NewAvatarButton", EColor(0x355a82ff));
	LoadImageOrFallback(buttonSecondary, "NewAvatarButtonAlt", EColor(0x6a4b30ff));
	LoadSoundIfPresent(soundContinue, "Click");
	DragonPlayMenuMusic();
}

void Splash::SetMessage (const EString& text) {
	messageText = text;
	messageTimestamp = GetMilliseconds();
}

void Splash::UpdateLayoutRects (const LegacyCanvas& canvas) {
	const ERect safe = canvas.frame;
	aboutRect = LegacyRect(canvas, 8, kSplashMenuRowY[0], kSplashMenuHitWidth, 30);
	slotMenuRect = LegacyRect(canvas, 8, kSplashMenuRowY[1], kSplashMenuHitWidth, 30); // New Game
	continueRect = LegacyRect(canvas, 8, kSplashMenuRowY[2], kSplashMenuHitWidth, 30); // Open Game...
	soundRect = LegacyRect(canvas, 8, kSplashMenuRowY[3], kSplashMenuHitWidth, 30);
	musicRect = LegacyRect(canvas, 8, kSplashMenuRowY[4], kSplashMenuHitWidth, 30);
	fullscreenRect = LegacyRect(canvas, 8, kSplashMenuRowY[5], kSplashMenuHitWidth, 30);
	quitRect = LegacyRect(canvas, 8, kSplashMenuRowY[6], kSplashMenuHitWidth, 30);

	infoRect = ERect(safe.x + (safe.width - 420) / 2, safe.y + (safe.height - 216) / 2, 420, 216);
	infoOkRect = ERect(infoRect.x + infoRect.width / 2 - 70, infoRect.y + infoRect.height - 46, 140, 30);
	quitConfirmRect = ERect(safe.x + (safe.width - 420) / 2, safe.y + (safe.height - 188) / 2, 420, 188);
	quitYesRect = ERect(quitConfirmRect.x + 16, quitConfirmRect.y + quitConfirmRect.height - 44, (quitConfirmRect.width - 40) / 2, 30);
	quitNoRect = ERect(quitYesRect.x + quitYesRect.width + 8, quitYesRect.y, quitYesRect.width, quitYesRect.height);
}

void Splash::OpenInfo (const EString& title, const EString& body) {
	infoActive = true;
	quitConfirmActive = false;
	infoTitle = title;
	infoBody = body;
}

void Splash::OpenQuitConfirm () {
	quitConfirmActive = true;
	infoActive = false;
}

void Splash::ApplySoundPreference () {
	DragonApplyAudioPreferences();
}

void Splash::RefreshStartupReadyState () {
	const int splashTarget = std::max(400, DESIGN_SPLASH_MIN_MILLISECONDS);
	const int64_t elapsed = GetMilliseconds() - startupMilliseconds;
	startupReady = elapsed >= splashTarget;
}

int Splash::MenuRowIndexForPoint (int x, int y) const {
	const ERect rows[kSplashMenuRowCount] = {
		aboutRect,
		slotMenuRect,
		continueRect,
		soundRect,
		musicRect,
		fullscreenRect,
		quitRect
	};
	for(int i = 0; i < kSplashMenuRowCount; i++) {
		if(HitTouchRect(rows[i], x, y, 44, 44, 2))
			return i;
	}

	int minLeft = rows[0].x;
	int maxRight = rows[0].x + rows[0].width;
	int minTop = rows[0].y;
	int maxBottom = rows[kSplashMenuRowCount - 1].y + rows[kSplashMenuRowCount - 1].height;
	for(int i = 1; i < kSplashMenuRowCount; i++) {
		minLeft = std::min(minLeft, rows[i].x);
		maxRight = std::max(maxRight, rows[i].x + rows[i].width);
		minTop = std::min(minTop, rows[i].y);
		maxBottom = std::max(maxBottom, rows[i].y + rows[i].height);
	}

	const int panelPaddingX = std::max(10, rows[0].height / 3);
	const int panelPaddingY = std::max(10, rows[0].height / 2);
	const int panelExtraRight = std::max(kSplashMenuPanelExtraRight, rows[0].width / 3);
	if(x < minLeft - panelPaddingX
		or x > maxRight + panelPaddingX + panelExtraRight
		or y < minTop - panelPaddingY
		or y > maxBottom + panelPaddingY)
		return -1;

	int bestIndex = -1;
	int bestDistance = INT_MAX;
	for(int i = 0; i < kSplashMenuRowCount; i++) {
		const int centerY = rows[i].y + rows[i].height / 2;
		const int distance = std::abs(y - centerY);
		const int tolerance = std::max(18, rows[i].height / 2 + 6);
		if(distance > tolerance)
			continue;
		if(distance < bestDistance) {
			bestDistance = distance;
			bestIndex = i;
		}
	}
	if(bestIndex >= 0)
		return bestIndex;
	return -1;
}

void Splash::SetMenuHoverIndex (int index) {
	if(index < 0 or index >= kSplashMenuRowCount) {
		menuHoverIndex = -1;
		menuHoverTimestamp = 0;
		return;
	}
	menuHoverIndex = index;
	menuHoverTimestamp = GetMilliseconds();
}

void Splash::OnDraw () {
	const LegacyCanvas canvas = MakeLegacyCanvasInPreferredView(*this, 800, 600);
	const ERect safe = canvas.frame;
	DrawImageContain(background, canvas.frame);
	UpdateLayoutRects(canvas);
	RefreshStartupReadyState();
	int64_t elapsed = GetMilliseconds() - startupMilliseconds;
	int splashTarget = std::max(400, DESIGN_SPLASH_MIN_MILLISECONDS);
	int progress = (int)(elapsed * 100 / splashTarget);
	progress = std::max(0, std::min(100, progress));
	startupReady = startupReady or progress >= 100;

	const ERect statusRect = LegacyRect(canvas, 170, 28, 610, 30);
	if(not fontBody.IsEmpty() and startupReady == false) {
#if defined(DRAGON_TEST)
		ReportValidationLabel("Loading...", statusRect);
#endif
		DrawCenteredLabel(fontBody, "Loading...", statusRect);
	}

	if(startupReady == false) {
		ERect splashPanelRect = LegacyRect(canvas, 0, 0, 800, 600);
		panel.DrawRect(splashPanelRect);
		ERect progressRect = LegacyRect(canvas, 236, 476, 328, 76);
		DrawImageContain(banner, progressRect);
		ERect fillRect = LegacyRect(canvas, 290, 509, 217, 12);
		progressBackground.DrawRect(fillRect);
		const int fillWidth = std::max(1, fillRect.width * progress / 100);
		progressFill.DrawRect(ERect(fillRect.x, fillRect.y, fillWidth, fillRect.height));
	} else {
		const bool markerHighlightActive = menuHoverIndex >= 0
			and menuHoverIndex < kSplashMenuRowCount
			and (GetMilliseconds() - menuHoverTimestamp) <= kSplashMenuHighlightDurationMs;
		for(int i = 0; i < kSplashMenuRowCount; i++) {
			ERect markerRect = LegacyRect(
				canvas,
				kSplashMenuMarkerX,
				kSplashMenuRowY[i] + kSplashMenuMarkerYOffset,
				kSplashMenuMarkerWidth,
				kSplashMenuMarkerHeight
			);
			// Only the OPEN GAME row depends on having a valid continue slot.
			const bool rowEnabled = (i != 2) or hasContinueSlot;
			const bool rowHighlighted = markerHighlightActive and i == menuHoverIndex and rowEnabled;
			if(rowHighlighted) {
				DrawImageContain(markerOn, markerRect, EColor::WHITE);
			}
			else {
				DrawImageContain(markerOff, markerRect, rowEnabled ? EColor(0xffffffd0) : EColor(0x7f7f7fbf));
			}
		}
#if defined(DRAGON_TEST)
		ReportValidationLabel("ABOUT DRAGON ALPHA", aboutRect);
		ReportValidationLabel("NEW GAME", slotMenuRect);
		ReportValidationLabel("OPEN GAME...", continueRect);
		ReportValidationLabel("SOUND", soundRect);
		ReportValidationLabel("MUSIC", musicRect);
		ReportValidationLabel("FULLSCREEN", fullscreenRect);
		ReportValidationLabel("QUIT", quitRect);
#endif
		if(not fontBody.IsEmpty() and hasContinuePreview and continuePreview.name[0] != '\0') {
			EString heroLine = EString().Format("Continue: %s  Lv.%d", continuePreview.name, (int)continuePreview.level);
			const ERect heroLineRect = ClipRectToBounds(LegacyRect(canvas, 170, 494, 610, 20), safe);
			DrawCenteredLabel(fontBody, heroLine, heroLineRect);
		}
	}

	if(messageText.IsEmpty() == false and GetMilliseconds() - messageTimestamp < 2200 and not fontBody.IsEmpty()) {
		ERect messageRect = LegacyRect(canvas, 188, 506, 570, 40);
		panel.DrawRect(messageRect);
		DrawWrappedLabel(fontBody, messageText, InsetRect(messageRect, 6), 2);
	}

	if(infoActive) {
		accent.DrawRect(safe, EColor(0x000000a2));
		panel.DrawRect(infoRect);
		buttonPrimary.DrawRect(infoOkRect);
		const ERect infoTitleRect(infoRect.x + 12, infoRect.y + 14, infoRect.width - 24, 22);
		const ERect infoBodyRect(infoRect.x + 14, infoRect.y + 46, infoRect.width - 28, infoRect.height - 96);
#if defined(DRAGON_TEST)
		ReportValidationLabel(infoTitle.IsEmpty() ? "DRAGON ALPHA" : infoTitle, infoTitleRect);
		ReportValidationLabel(infoBody, infoBodyRect);
		ReportValidationLabel("OK", infoOkRect);
#endif
		if(not fontHeader.IsEmpty())
			DrawCenteredLabel(fontHeader, infoTitle.IsEmpty() ? "DRAGON ALPHA" : infoTitle, infoTitleRect);
		if(not fontBody.IsEmpty()) {
			DrawWrappedLabel(fontBody, infoBody, infoBodyRect, 5);
			DrawCenteredLabel(fontBody, "OK", infoOkRect);
		}
	}

	if(quitConfirmActive) {
		accent.DrawRect(safe, EColor(0x000000a2));
		panel.DrawRect(quitConfirmRect);
		buttonSecondary.DrawRect(quitYesRect);
		buttonPrimary.DrawRect(quitNoRect);
		const ERect quitTitleRect(quitConfirmRect.x + 12, quitConfirmRect.y + 14, quitConfirmRect.width - 24, 22);
		const ERect quitBodyRect(quitConfirmRect.x + 14, quitConfirmRect.y + 46, quitConfirmRect.width - 28, 58);
#if defined(DRAGON_TEST)
		ReportValidationLabel("QUIT DRAGON ALPHA", quitTitleRect);
		ReportValidationLabel("Are you sure you want to quit Dragon Alpha?", quitBodyRect);
		ReportValidationLabel("QUIT", quitYesRect);
		ReportValidationLabel("CANCEL", quitNoRect);
#endif
		if(not fontHeader.IsEmpty())
			DrawCenteredLabel(fontHeader, "QUIT DRAGON ALPHA", quitTitleRect);
		if(not fontBody.IsEmpty()) {
			DrawWrappedLabel(fontBody, "Are you sure you want to quit Dragon Alpha?", quitBodyRect, 3);
			DrawCenteredLabel(fontBody, "QUIT", quitYesRect);
			DrawCenteredLabel(fontBody, "CANCEL", quitNoRect);
		}
	}
}

void Splash::OnTouch (int x, int y) {
	EPoint input = NormalizeInputPoint(*this, x, y);
	x = input.x;
	y = input.y;
	const ERect inputBounds = MakePreferredViewRect(GetSafeRect(), DESIGN_PREFERRED_WIDTH, DESIGN_PREFERRED_HEIGHT);
	const ERect safeBounds = GetSafeRect();
	if(inputBounds.IsPointInRect(x, y) == false or transitioned) {
		if(transitioned) {
			touchHandledThisSequence = false;
			touchMovedThisSequence = false;
			return;
		}
		if(inputBounds.IsPointInRect(x, y) == false) {
			if(safeBounds.IsPointInRect(x, y) == false) {
				touchHandledThisSequence = false;
				touchMovedThisSequence = false;
				return;
			}
		}
	}
	touchHandledThisSequence = true;
	touchMovedThisSequence = false;
	touchDownX = x;
	touchDownY = y;
	const LegacyCanvas canvas = MakeLegacyCanvasInPreferredView(*this, 800, 600);
	UpdateLayoutRects(canvas);
	RefreshStartupReadyState();

	if(startupReady == false) {
		PlaySoundIfLoaded(soundContinue);
		int splashTarget = std::max(400, DESIGN_SPLASH_MIN_MILLISECONDS);
		startupMilliseconds = GetMilliseconds() - splashTarget;
		RefreshStartupReadyState();
		if(startupReady == false)
			return;
	}
	const int menuRowIndex = MenuRowIndexForPoint(x, y);
	SetMenuHoverIndex(menuRowIndex);

	if(infoActive) {
		if(HitTouchRect(infoOkRect, x, y, 44, 44, 2) or infoRect.IsPointInRect(x, y) == false) {
			PlaySoundIfLoaded(soundContinue);
			infoActive = false;
		}
		return;
	}

	if(quitConfirmActive) {
		if(HitTouchRect(quitNoRect, x, y, 44, 44, 2) or quitConfirmRect.IsPointInRect(x, y) == false) {
			PlaySoundIfLoaded(soundContinue);
			quitConfirmActive = false;
			return;
		}
		if(HitTouchRect(quitYesRect, x, y, 44, 44, 2)) {
			PlaySoundIfLoaded(soundContinue);
			quitConfirmActive = false;
#if defined(__APPLE__) && TARGET_OS_IOS
			SetMessage("Use the Home gesture to close on iOS.");
#else
			std::exit(0);
#endif
			return;
		}
		return;
	}

	if(menuRowIndex == 0 or HitTouchRect(aboutRect, x, y, 44, 44, 2)) {
		PlaySoundIfLoaded(soundContinue);
		OpenInfo(
			"ABOUT DRAGON ALPHA",
			"Original release by Dracosoft (1997-2003).\nThis modern build keeps the original game flow while restoring a standalone Apple runtime."
		);
		return;
	}

	if(menuRowIndex == 3 or HitTouchRect(soundRect, x, y, 44, 44, 2)) {
		PlaySoundIfLoaded(soundContinue);
		DragonSetSoundEnabled(not DragonIsSoundEnabled());
		ApplySoundPreference();
		SetMessage(DragonIsSoundEnabled() ? "Sound enabled." : "Sound disabled.");
		return;
	}

	if(menuRowIndex == 4 or HitTouchRect(musicRect, x, y, 44, 44, 2)) {
		PlaySoundIfLoaded(soundContinue);
		DragonSetMusicEnabled(not DragonIsMusicEnabled());
		SetMessage(DragonIsMusicEnabled() ? "Music enabled." : "Music disabled.");
		return;
	}

	if(menuRowIndex == 5 or HitTouchRect(fullscreenRect, x, y, 44, 44, 2)) {
		PlaySoundIfLoaded(soundContinue);
		DragonSetFullscreenEnabled(not DragonIsFullscreenEnabled());
		SetMessage(DragonIsFullscreenEnabled() ? "Full screen enabled." : "Full screen disabled.");
		return;
	}

	if(menuRowIndex == 6 or HitTouchRect(quitRect, x, y, 44, 44, 2)) {
		PlaySoundIfLoaded(soundContinue);
		OpenQuitConfirm();
		return;
	}

	// New Game hotspot (legacy menu row)
	if(menuRowIndex == 1 or HitTouchRect(slotMenuRect, x, y, 44, 44, 2)) {
		PlaySoundIfLoaded(soundContinue);
		transitioned = true;
		RunNewNode("NewGame");
		return;
	}

	// Open Game hotspot (legacy menu row)
	if(menuRowIndex == 2 or HitTouchRect(continueRect, x, y, 44, 44, 2)) {
		PlaySoundIfLoaded(soundContinue);
		if(hasContinueSlot)
			gSaveInfo.selectedSlot = continueSlot;
		// Route OPEN GAME through slot manager so load/delete handling stays consistent.
		transitioned = true;
		RunNewNode("NewGame");
		return;
	}
}

void Splash::OnTouchMove (int x, int y) {
	EPoint input = NormalizeInputPoint(*this, x, y);
	x = input.x;
	y = input.y;
	if(touchHandledThisSequence
		and (std::abs(x - touchDownX) > 3 or std::abs(y - touchDownY) > 3))
		touchMovedThisSequence = true;
	const ERect inputBounds = MakePreferredViewRect(GetSafeRect(), DESIGN_PREFERRED_WIDTH, DESIGN_PREFERRED_HEIGHT);
	const ERect safeBounds = GetSafeRect();
	if(inputBounds.IsPointInRect(x, y) == false) {
		if(safeBounds.IsPointInRect(x, y) == false) {
			SetMenuHoverIndex(-1);
			return;
		}
	}
	if(transitioned or startupReady == false) {
		SetMenuHoverIndex(-1);
		return;
	}
	const LegacyCanvas canvas = MakeLegacyCanvasInPreferredView(*this, 800, 600);
	UpdateLayoutRects(canvas);
	if(infoActive or quitConfirmActive) {
		SetMenuHoverIndex(-1);
		return;
	}
	SetMenuHoverIndex(MenuRowIndexForPoint(x, y));
}

void Splash::OnTouchUp (int x, int y) {
	EPoint input = NormalizeInputPoint(*this, x, y);
	x = input.x;
	y = input.y;
	const ERect inputBounds = MakePreferredViewRect(GetSafeRect(), DESIGN_PREFERRED_WIDTH, DESIGN_PREFERRED_HEIGHT);
	const ERect safeBounds = GetSafeRect();
	if((inputBounds.IsPointInRect(x, y) or safeBounds.IsPointInRect(x, y))
		and touchHandledThisSequence == false
		and touchMovedThisSequence == false)
		OnTouch(x, y);
	touchHandledThisSequence = false;
	touchMovedThisSequence = false;
	if(menuHoverIndex >= 0 and GetMilliseconds() - menuHoverTimestamp > kSplashMenuHighlightDurationMs)
		SetMenuHoverIndex(-1);
}

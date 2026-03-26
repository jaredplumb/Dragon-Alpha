/**
 * @file Splash.h
 * @brief Splash/menu scene interface and interaction state declarations.
 */
#ifndef DRAGON_ALPHA_SPLASH_H
#define DRAGON_ALPHA_SPLASH_H

#include "Global.h"

struct LegacyCanvas;

class Splash : public ENodeAutoRun<Splash, "Splash"> {
public:
	Splash ();
	void OnDraw () override;
	void OnTouch (int x, int y) override;
	void OnTouchMove (int x, int y) override;
	void OnTouchUp (int x, int y) override;

private:
	void SetMessage (const EString& text);
	void OpenInfo (const EString& title, const EString& body);
	void OpenQuitConfirm ();
	void ApplySoundPreference ();
	void RefreshStartupReadyState ();
	void UpdateLayoutRects (const LegacyCanvas& canvas);
	int MenuRowIndexForPoint (int x, int y) const;
	void SetMenuHoverIndex (int index);

	EImage background;
	EImage panel;
	EImage banner;
	EImage progressBackground;
	EImage progressFill;
	EImage accent;
	EImage markerOn;
	EImage markerOff;
	EImage buttonPrimary;
	EImage buttonSecondary;
	ESound soundContinue;
	EFont fontHeader;
	EFont fontBody;
	UITransition transition;
	LegacyAssetSummary legacySummary;
	DragonSaveSlot continuePreview;
	ERect aboutRect;
	ERect continueRect;
	ERect slotMenuRect;
	ERect soundRect;
	ERect musicRect;
	ERect fullscreenRect;
	ERect quitRect;
	ERect infoRect;
	ERect infoOkRect;
	ERect quitConfirmRect;
	ERect quitYesRect;
	ERect quitNoRect;
	EString infoTitle;
	EString infoBody;
	int64_t startupMilliseconds;
	int64_t messageTimestamp;
	uint8_t continueSlot;
	bool hasContinueSlot;
	bool hasContinuePreview;
	bool startupReady;
	bool transitioned;
	bool infoActive;
	bool quitConfirmActive;
	int menuHoverIndex;
	int64_t menuHoverTimestamp;
	bool touchHandledThisSequence;
	bool touchMovedThisSequence;
	int touchDownX;
	int touchDownY;
	EString messageText;
};

#endif // DRAGON_ALPHA_SPLASH_H

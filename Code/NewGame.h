/**
 * @file NewGame.h
 * @brief New game slot-management scene interface and state declarations.
 */
#ifndef DRAGON_ALPHA_NEWGAME_H
#define DRAGON_ALPHA_NEWGAME_H

#include "Global.h"

struct LegacyCanvas;

class NewGame : public ENodeWithName<NewGame, "NewGame"> {
public:
	NewGame ();
	void OnDraw () override;
	void OnTouch (int x, int y) override;
	void OnTouchMove (int x, int y) override;
	void OnTouchUp (int x, int y) override;

private:
	void RefreshSlots ();
	void UpdateLayoutRects (const LegacyCanvas& canvas);
	bool DrawSlotCard (int slot, const ERect& rect);
	bool HasAnyFilledSlot () const;
	void SetMessage (const EString& text);
	void OpenDeleteConfirm (int slot);
	void CloseDeleteConfirm (void);

	EImage imageBackground;
	EImage imagePanel;
	EImage imageSlotFilled;
	EImage imageSlotEmpty;
	EImage imageSlotHighlight;
	EImage imageDelete;
	EImage imageDeleteActive;
	EImage slotAvatar[DRAGON_AVATAR_COUNT];
	ESound soundClick;
	ESound soundDelete;
	EFont fontHeader;
	EFont fontMain;
	UITransition transition;

	ERect slotRect[DRAGON_ALPHA_SLOT_COUNT];
	ERect deleteRect[DRAGON_ALPHA_SLOT_COUNT];
	ERect backRect;
	ERect deleteConfirmRect;
	ERect deleteConfirmYesRect;
	ERect deleteConfirmNoRect;
	bool slotFilled[DRAGON_ALPHA_SLOT_COUNT];
	DragonSaveSlot slotPreview[DRAGON_ALPHA_SLOT_COUNT];
	bool deleteConfirmActive;
	int deleteConfirmSlot;
	bool touchHandledThisSequence;
	bool touchMovedThisSequence;
	int touchDownX;
	int touchDownY;
	int64_t touchUpGuardUntil;
	int64_t refreshTimer;
	int64_t messageTimestamp;
	EString messageText;
};

#endif // DRAGON_ALPHA_NEWGAME_H

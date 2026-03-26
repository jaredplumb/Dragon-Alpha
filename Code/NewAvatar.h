/**
 * @file NewAvatar.h
 * @brief Avatar creation scene interface and selection state declarations.
 */
#ifndef DRAGON_ALPHA_NEWAVATAR_H
#define DRAGON_ALPHA_NEWAVATAR_H

#include "Global.h"

struct LegacyCanvas;

class NewAvatar : public ENodeWithName<NewAvatar, "NewAvatar"> {
public:
	NewAvatar ();
	void OnDraw () override;
	void OnTouch (int x, int y) override;
	void OnTouchMove (int x, int y) override;
	void OnTouchUp (int x, int y) override;

private:
	const char* GetSelectedName () const;
	void UpdateLayoutRects (const LegacyCanvas& canvas);

	EImage imageBackground;
	EImage imagePanel;
	EImage imageAvatarCard;
	EImage imageAvatarSelected;
	EImage avatarPortrait[DRAGON_AVATAR_COUNT];
	EImage imageButton;
	EImage imageButtonAlt;
	ESound soundSelect;
	ESound soundClick;
	ESound soundCreate;
	EFont fontHeader;
	EFont fontMain;
	UITransition transition;

	ERect avatarRect[DRAGON_AVATAR_COUNT];
	ERect nextNameRect;
	ERect createRect;
	ERect backRect;
	int selectedAvatar;
	int selectedName;
	bool touchHandledThisSequence;
	bool touchMovedThisSequence;
	int touchDownX;
	int touchDownY;
};

#endif // DRAGON_ALPHA_NEWAVATAR_H

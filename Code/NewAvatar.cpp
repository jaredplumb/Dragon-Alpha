/**
 * @file NewAvatar.cpp
 * @brief Avatar creation scene rendering and selection flow implementation.
 */
#include "NewAvatar.h"
#include "LayoutUtil.h"

#include <cstdlib>

namespace {
static const bool kNewAvatarNodeFactoryRegistered = DragonEnsureNodeFactoryRegistered<NewAvatar>();

#if defined(DRAGON_TEST)
void ReportValidationLabel (const EString& text, const ERect& rect) {
	if(text.IsEmpty() == false && rect.width > 0 && rect.height > 0)
		ESystem::ReportTextDraw(text, rect);
}
#endif
}

static const char* kAvatarNames[] = {"ARIN", "LUNA", "KANE", "MIRA", "ZALE", "RHEA", "NOVA", "TARIN"};
static const char* kAvatarClassName[DRAGON_AVATAR_COUNT] = {"Soldier", "Mage", "Thief"};

NewAvatar::NewAvatar ()
:	imageBackground(EColor(0x0f1828ff))
,	imagePanel(EColor(0x1b2b43ff))
,	imageAvatarCard(EColor(0x30567fff))
,	imageAvatarSelected(EColor(0x8ec0ff66))
,	imageButton(EColor(0x355a82ff))
,	imageButtonAlt(EColor(0x6a4b30ff))
,	fontHeader("FontMissionHeader")
,	fontMain("FontMissionText")
	,	transition(UITransition::FADE_BLACK, this)
	,	selectedAvatar(DRAGON_AVATAR_WARRIOR)
	,	selectedName((int)ENode::GetRandom(sizeof(kAvatarNames) / sizeof(kAvatarNames[0])))
	,	touchHandledThisSequence(false)
	,	touchMovedThisSequence(false)
	,	touchDownX(0)
	,	touchDownY(0)
{
	DragonPlayMenuMusic();
	if(gPendingSlot < 1 or gPendingSlot > DRAGON_ALPHA_SLOT_COUNT)
		gPendingSlot = 1;

	LoadImageOrFallback(imageBackground, "NewAvatarBackground", EColor(0x0f1828ff));
	LoadImageOrFallback(imagePanel, "NewAvatarPanel", EColor(0x1b2b43ff));
	LoadImageOrFallback(imageAvatarCard, "NewAvatarCard", EColor(0x30567fff));
	LoadImageOrFallback(imageAvatarSelected, "NewAvatarSelected", EColor(0x8ec0ff66));
	LoadImageOrFallback(imageButton, "NewAvatarButton", EColor(0x355a82ff));
	LoadImageOrFallback(imageButtonAlt, "NewAvatarButtonAlt", EColor(0x6a4b30ff));
	LoadImageOrFallback(avatarPortrait[DRAGON_AVATAR_WARRIOR], "AvatarWarrior", EColor(0x6b7f98ff));
	LoadImageOrFallback(avatarPortrait[DRAGON_AVATAR_SORCERER], "AvatarSorcerer", EColor(0x8f5d94ff));
	LoadImageOrFallback(avatarPortrait[DRAGON_AVATAR_RANGER], "AvatarRanger", EColor(0x5d8058ff));
	if(imageBackground.GetWidth() < 120 or imageBackground.GetHeight() < 48)
		imageBackground.New(EColor(0x0f1828ff));
	if(imagePanel.GetWidth() < 120 or imagePanel.GetHeight() < 48)
		imagePanel.New(EColor(0x1b2b43f0));
	if(imageAvatarCard.GetWidth() < 32 or imageAvatarCard.GetHeight() < 24)
		imageAvatarCard.New(EColor(0x30567fff));
	if(imageAvatarSelected.GetWidth() < 16 or imageAvatarSelected.GetHeight() < 16)
		imageAvatarSelected.New(EColor(0x8ec0ff66));
	if((imageButton.GetWidth() <= 96 and imageButton.GetHeight() <= 96) or imageButton.GetWidth() < 80 or imageButton.GetHeight() < 24)
		imageButton.New(EColor(0x355a82ff));
	if((imageButtonAlt.GetWidth() <= 96 and imageButtonAlt.GetHeight() <= 96) or imageButtonAlt.GetWidth() < 80 or imageButtonAlt.GetHeight() < 24)
		imageButtonAlt.New(EColor(0x6a4b30ff));
	LoadSoundIfPresent(soundSelect, "Select");
	LoadSoundIfPresent(soundClick, "Click");
	LoadSoundIfPresent(soundCreate, "LevelUp");
}

const char* NewAvatar::GetSelectedName () const {
	return kAvatarNames[selectedName % (int)(sizeof(kAvatarNames) / sizeof(kAvatarNames[0]))];
}

void NewAvatar::UpdateLayoutRects (const LegacyCanvas& canvas) {
	const ERect safe = canvas.frame;
	const int gap = std::max(10, LegacyRect(canvas, 0, 0, 12, 0).width);
	const int cardsX = LegacyRect(canvas, 194, 0, 0, 0).x;
	const int cardsY = LegacyRect(canvas, 140, 0, 0, 0).y;
	const int cardsWidth = LegacyRect(canvas, 194, 0, 570, 0).width;
	const int cardWidth = (cardsWidth - gap * 2) / 3;
	const int cardHeight = std::max(132, LegacyRect(canvas, 0, 0, 0, 188).height);
	for(int i = 0; i < DRAGON_AVATAR_COUNT; i++)
		avatarRect[i] = ERect(cardsX + i * (cardWidth + gap), cardsY, cardWidth, cardHeight);

	nextNameRect = ClipRectToBounds(LegacyRect(canvas, 194, 430, 570, 34), safe);
	const int buttonWidth = (nextNameRect.width - gap) / 2;
	createRect = ClipRectToBounds(LegacyRect(canvas, 194, 472, 0, 34), safe);
	createRect.width = buttonWidth;
	backRect = ERect(createRect.x + createRect.width + gap, createRect.y, buttonWidth, createRect.height);
}

void NewAvatar::OnDraw () {
	const LegacyCanvas canvas = MakeLegacyCanvasInPreferredView(*this, 800, 600);
	const ERect safe = canvas.frame;
	UpdateLayoutRects(canvas);
	DrawImageContain(imageBackground, safe);
	DrawImageContain(imagePanel, LegacyRect(canvas, 182, 88, 594, 512), EColor(0xffffff24));

	const ERect headerRect = ClipRectToBounds(LegacyRect(canvas, 196, 94, 570, 24), safe);
#if defined(DRAGON_TEST)
	ReportValidationLabel("NEW GAME - SELECT CLASS", headerRect);
#endif

	if(not fontHeader.IsEmpty()) {
		DrawLeftClampedLabel(fontHeader, "NEW GAME - SELECT CLASS", headerRect);
	}
	const ERect slotRect = ClipRectToBounds(LegacyRect(canvas, 196, 116, 570, 20), safe);
#if defined(DRAGON_TEST)
	ReportValidationLabel(EString().Format("Save Slot %d", gPendingSlot), slotRect);
#endif
	if(not fontMain.IsEmpty()) {
		DrawLeftClampedLabel(fontMain, EString().Format("Save Slot %d", gPendingSlot), slotRect);
	}

	for(int i = 0; i < DRAGON_AVATAR_COUNT; i++) {
		imageAvatarCard.DrawRect(avatarRect[i]);
		if(i == selectedAvatar)
			imageAvatarSelected.DrawRect(InsetRect(avatarRect[i], 4));

		ERect portraitRect = InsetRect(avatarRect[i], 10);
			portraitRect.height = std::max(40, avatarRect[i].height / 2);
			DrawImageContain(avatarPortrait[i], portraitRect);
#if defined(DRAGON_TEST)
			ReportValidationLabel(kAvatarClassName[i], ERect(avatarRect[i].x + 10, avatarRect[i].y + 8, avatarRect[i].width - 20, 24));
#endif
			if(not fontHeader.IsEmpty()) {
				fontHeader.Draw(kAvatarClassName[i], avatarRect[i].x + 10, avatarRect[i].y + 12);
			}
	}

	imageButton.DrawRect(nextNameRect);
	imageButton.DrawRect(createRect);
	imageButtonAlt.DrawRect(backRect);

#if defined(DRAGON_TEST)
	ReportValidationLabel(EString().Format("Name: %s  (tap to reroll)", GetSelectedName()), nextNameRect);
	ReportValidationLabel("BEGIN GAME", createRect);
	ReportValidationLabel("BACK", backRect);
	ReportValidationLabel(EString().Format("Selected class: %s", kAvatarClassName[selectedAvatar]), avatarRect[selectedAvatar]);
#endif
	if(not fontMain.IsEmpty()) {
		DrawLeftClampedLabel(fontMain, EString().Format("Name: %s  (tap to reroll)", GetSelectedName()), nextNameRect, 12, 0);
		DrawCenteredLabel(fontMain, "BEGIN GAME", createRect);
		DrawCenteredLabel(fontMain, "BACK", backRect);
	}
}

void NewAvatar::OnTouch (int x, int y) {
	EPoint input = NormalizeInputPoint(*this, x, y);
	x = input.x;
	y = input.y;
	const ERect inputBounds = MakePreferredViewRect(GetSafeRect(), DESIGN_PREFERRED_WIDTH, DESIGN_PREFERRED_HEIGHT);
	const ERect safeBounds = GetSafeRect();
	if(inputBounds.IsPointInRect(x, y) == false) {
		if(safeBounds.IsPointInRect(x, y) == false) {
			touchHandledThisSequence = false;
			touchMovedThisSequence = false;
			return;
		}
	}
	touchHandledThisSequence = true;
	touchMovedThisSequence = false;
	touchDownX = x;
	touchDownY = y;
	const LegacyCanvas canvas = MakeLegacyCanvasInPreferredView(*this, 800, 600);
	UpdateLayoutRects(canvas);

	for(int i = 0; i < DRAGON_AVATAR_COUNT; i++) {
		if(HitTouchRect(avatarRect[i], x, y, 44, 44, 6)) {
			selectedAvatar = i;
			PlaySoundIfLoaded(soundSelect);
			return;
		}
	}

	if(HitTouchRect(nextNameRect, x, y, 44, 44, 2)) {
		selectedName++;
		if(selectedName >= (int)(sizeof(kAvatarNames) / sizeof(kAvatarNames[0])))
			selectedName = 0;
		PlaySoundIfLoaded(soundClick);
		return;
	}

	if(HitTouchRect(backRect, x, y, 44, 44, 2)) {
		PlaySoundIfLoaded(soundClick);
		RunNewNode("NewGame");
		return;
	}

	if(HitTouchRect(createRect, x, y, 44, 44, 2)) {
		PlaySoundIfLoaded(soundCreate);
		if(DragonCreateSlot(gPendingSlot, selectedAvatar, GetSelectedName()))
			RunNewNode("WorldMap");
		else
			RunNewNode("NewGame");
	}
}

void NewAvatar::OnTouchMove (int x, int y) {
	EPoint input = NormalizeInputPoint(*this, x, y);
	x = input.x;
	y = input.y;
	if(touchHandledThisSequence
		and (std::abs(x - touchDownX) > 3 or std::abs(y - touchDownY) > 3))
		touchMovedThisSequence = true;
}

void NewAvatar::OnTouchUp (int x, int y) {
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
}

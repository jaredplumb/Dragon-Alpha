/**
 * @file NewGame.cpp
 * @brief New game slot-management scene behavior and input handling implementation.
 */
#include "NewGame.h"
#include "LayoutUtil.h"

#include <cstdlib>

namespace {
static const bool kNewGameNodeFactoryRegistered = DragonEnsureNodeFactoryRegistered<NewGame>();

#if defined(DRAGON_TEST)
void ReportValidationLabel (const EString& text, const ERect& rect) {
	if(text.IsEmpty() == false && rect.width > 0 && rect.height > 0)
		ESystem::ReportTextDraw(text, rect);
}
#endif
}

NewGame::NewGame ()
:	imageBackground(EColor(0x0d1624ff))
,	imagePanel(EColor(0x1a2a43ff))
,	imageSlotFilled(EColor(0x2f527eff))
,	imageSlotEmpty(EColor(0x243449ff))
,	imageSlotHighlight(EColor(0x7cb0f055))
,	imageDelete(EColor(0x933737ff))
,	imageDeleteActive(EColor(0xd45a5aff))
	,	fontHeader("FontMissionHeader")
	,	fontMain("FontMissionText")
	,	transition(UITransition::FADE_BLACK, this)
	,	deleteConfirmActive(false)
	,	deleteConfirmSlot(-1)
	,	touchHandledThisSequence(false)
	,	touchMovedThisSequence(false)
	,	touchDownX(0)
	,	touchDownY(0)
	,	touchUpGuardUntil(0)
	,	refreshTimer(0)
	,	messageTimestamp(0)
	,	messageText("")
{
	// Ignore orphan touch-up events right after scene transitions.
	touchUpGuardUntil = GetMilliseconds() + 220;
	DragonPlayMenuMusic();
	for(int i = 0; i < DRAGON_ALPHA_SLOT_COUNT; i++) {
		slotFilled[i] = false;
		slotPreview[i] = {};
		slotRect[i] = ERect();
		deleteRect[i] = ERect();
	}
	backRect = ERect();
	deleteConfirmRect = ERect();
	deleteConfirmYesRect = ERect();
	deleteConfirmNoRect = ERect();

	LoadImageOrFallback(imageBackground, "NewGameBackground", EColor(0x0d1624ff));
	LoadImageOrFallback(imagePanel, "NewGamePanel", EColor(0x1a2a43ff));
	LoadImageOrFallback(imageSlotFilled, "NewGameSlotFilled", EColor(0x2f527eff));
	LoadImageOrFallback(imageSlotEmpty, "NewGameSlotEmpty", EColor(0x243449ff));
	LoadImageOrFallback(imageSlotHighlight, "NewGameSlotHighlight", EColor(0x7cb0f055));
	LoadImageOrFallback(imageDelete, "NewGameDelete", EColor(0x933737ff));
	LoadImageOrFallback(imageDeleteActive, "NewGameDeleteActive", EColor(0xd45a5aff));
	LoadSoundIfPresent(soundClick, "Click");
	LoadSoundIfPresent(soundDelete, "Select");
	LoadImageOrFallback(slotAvatar[DRAGON_AVATAR_WARRIOR], "AvatarWarrior", EColor(0x6b7f98ff));
	LoadImageOrFallback(slotAvatar[DRAGON_AVATAR_SORCERER], "AvatarSorcerer", EColor(0x8f5d94ff));
	LoadImageOrFallback(slotAvatar[DRAGON_AVATAR_RANGER], "AvatarRanger", EColor(0x5d8058ff));

	// Legacy alias safety for tiny fragments.
	if(imageBackground.GetWidth() < 120 or imageBackground.GetHeight() < 48)
		imageBackground.New(EColor(0x0d1624ff));
	if(imagePanel.GetWidth() < 120 or imagePanel.GetHeight() < 48)
		imagePanel.New(EColor(0x1a2a43f0));
	if(imageSlotFilled.GetWidth() < 32 or imageSlotFilled.GetHeight() < 20)
		imageSlotFilled.New(EColor(0x2f527eff));
	if(imageSlotEmpty.GetWidth() < 32 or imageSlotEmpty.GetHeight() < 20)
		imageSlotEmpty.New(EColor(0x243449ff));
	if((imageSlotHighlight.GetWidth() <= 96 and imageSlotHighlight.GetHeight() <= 96) or imageSlotHighlight.GetWidth() < 96 or imageSlotHighlight.GetHeight() < 28)
		imageSlotHighlight.New(EColor(0x7cb0f055));
	if(imageDelete.GetWidth() < 48 or imageDelete.GetHeight() < 24)
		imageDelete.New(EColor(0x933737ff));
	if(imageDeleteActive.GetWidth() < 48 or imageDeleteActive.GetHeight() < 24)
		imageDeleteActive.New(EColor(0xd45a5aff));

	RefreshSlots();
	if(HasAnyFilledSlot() == false)
		SetMessage("No saves found. Tap any slot to start NEW GAME.");
}

void NewGame::SetMessage (const EString& text) {
	messageText = text;
	messageTimestamp = GetMilliseconds();
}

void NewGame::OpenDeleteConfirm (int slot) {
	if(slot < 0 or slot >= DRAGON_ALPHA_SLOT_COUNT)
		return;
	if(slotFilled[slot] == false)
		return;
	deleteConfirmSlot = slot;
	deleteConfirmActive = true;
}

void NewGame::CloseDeleteConfirm () {
	deleteConfirmActive = false;
	deleteConfirmSlot = -1;
}

void NewGame::RefreshSlots () {
	int recoveredSlots[DRAGON_ALPHA_SLOT_COUNT] = {};
	int recoveredCount = 0;
	for(int i = 0; i < DRAGON_ALPHA_SLOT_COUNT; i++) {
		const int slot = i + 1;
		if(DragonSlotHasCorruption(slot) and DragonRecoverCorruptedSlot(slot))
			recoveredSlots[recoveredCount++] = slot;
	}

	for(int i = 0; i < DRAGON_ALPHA_SLOT_COUNT; i++)
		slotFilled[i] = DragonReadSlotPreview(i + 1, slotPreview[i]);

	if(recoveredCount > 0) {
		if(recoveredCount == 1) {
			SetMessage(EString().Format("Recovered corrupted save in slot %d.", recoveredSlots[0]));
		} else {
			EString slotList;
			for(int i = 0; i < recoveredCount; i++) {
				if(i > 0)
					slotList += (i + 1 == recoveredCount) ? " and " : ", ";
				slotList += EString().Format("%d", recoveredSlots[i]);
			}
			SetMessage(EString().Format("Recovered corrupted saves in slots %s.", (const char*)slotList));
		}
	}
	refreshTimer = GetMilliseconds();
}

bool NewGame::HasAnyFilledSlot () const {
	for(int i = 0; i < DRAGON_ALPHA_SLOT_COUNT; i++) {
		if(slotFilled[i])
			return true;
	}
	return false;
}

void NewGame::UpdateLayoutRects (const LegacyCanvas& canvas) {
	const int gap = std::max(10, LegacyRect(canvas, 0, 0, 12, 0).width);
	const int cardsX = LegacyRect(canvas, 194, 0, 0, 0).x;
	const int cardsY = LegacyRect(canvas, 136, 0, 0, 0).y;
	const int cardsWidth = LegacyRect(canvas, 194, 0, 570, 0).width;
	const int cardWidth = (cardsWidth - gap * 2) / 3;
	const int cardHeight = std::max(108, LegacyRect(canvas, 0, 0, 0, 138).height);
	for(int i = 0; i < DRAGON_ALPHA_SLOT_COUNT; i++) {
		slotRect[i] = ERect(cardsX + i * (cardWidth + gap), cardsY, cardWidth, cardHeight);
		const int deleteWidth = std::max(42, std::min(64, slotRect[i].width / 4));
		const int deleteHeight = std::max(24, std::min(30, slotRect[i].height / 3));
		deleteRect[i] = ERect(slotRect[i].x + slotRect[i].width - deleteWidth - 8, slotRect[i].y + 8, deleteWidth, deleteHeight);
	}
	backRect = LegacyRect(canvas, 194, 94, 118, 28);

	if(deleteConfirmActive) {
		deleteConfirmRect = LegacyRect(canvas, 220, 224, 360, 152);
		deleteConfirmYesRect = ERect(deleteConfirmRect.x + LegacyRect(canvas, 0, 0, 16, 0).width, deleteConfirmRect.y + deleteConfirmRect.height - LegacyRect(canvas, 0, 0, 0, 46).height, (deleteConfirmRect.width - LegacyRect(canvas, 0, 0, 40, 0).width) / 2, std::max(24, LegacyRect(canvas, 0, 0, 0, 30).height));
		deleteConfirmNoRect = ERect(deleteConfirmYesRect.x + deleteConfirmYesRect.width + std::max(6, LegacyRect(canvas, 0, 0, 8, 0).width), deleteConfirmYesRect.y, deleteConfirmYesRect.width, deleteConfirmYesRect.height);
	}
}

bool NewGame::DrawSlotCard (int slot, const ERect& rect) {
	const bool isActiveSlot = gSaveInfo.selectedSlot == slot + 1;
	const EString header = EString().Format("SLOT %d", slot + 1);

	if(slotFilled[slot]) {
		imageSlotFilled.DrawRect(rect);
		imageSlotHighlight.DrawRect(InsetRect(rect, 4), isActiveSlot ? EColor(0xffffffe2) : EColor(0xffffffa8));
	} else {
		imageSlotEmpty.DrawRect(rect);
	}

#if defined(DRAGON_TEST)
	ReportValidationLabel(header, ERect(rect.x + 12, rect.y + 8, rect.width - 24, 24));
	if(slotFilled[slot]) {
		const EString name = slotPreview[slot].name[0] == '\0' ? "HERO" : slotPreview[slot].name;
		const EString level = EString().Format("Lv: %d", slotPreview[slot].level);
		const int areaIndex = std::max(0, std::min((int)slotPreview[slot].areaIndex, DRAGON_ALPHA_AREA_COUNT - 1));
		const DragonAreaInfo* area = DragonAreaByIndex(areaIndex);
		const EString progress = EString().Format("%s  Gold %d  W%d/L%d",
			area ? area->name : "Unknown",
			(int)slotPreview[slot].gold,
			(int)slotPreview[slot].battlesWon,
			(int)slotPreview[slot].battlesLost);
		const ERect portraitRect(
			rect.x + 12,
			rect.y + 30,
			std::max(24, std::min(92, rect.width / 3)),
			std::max(24, std::min(84, rect.height - 54))
		);
		const int textX = portraitRect.x + portraitRect.width + 10;
		const int textWidth = std::max(1, rect.x + rect.width - textX - 10);
		const int lineHeight = LayoutLineHeight(fontMain, 12, 24);
		const int lineStep = LayoutLineStep(fontMain, 14, 28, 2);
		const int textTop = rect.y + 34;
		const ERect nameRect(textX, textTop - 2, textWidth, lineHeight + 4);
		const ERect levelRect(textX, textTop - 2 + lineStep, textWidth, lineHeight + 4);
		const ERect progressRect(textX, textTop - 2 + lineStep * 2, textWidth, lineHeight + 4);
		ReportValidationLabel(name, nameRect);
		ReportValidationLabel(level, levelRect);
		ReportValidationLabel(progress, progressRect);
		ReportValidationLabel("DEL", deleteRect[slot]);
	}
#endif
	if(not fontHeader.IsEmpty()) {
		fontHeader.Draw(header, rect.x + 14, rect.y + 10);
	}

		if(not fontMain.IsEmpty()) {
			if(slotFilled[slot]) {
				EString name = slotPreview[slot].name[0] == '\0' ? "HERO" : slotPreview[slot].name;
				EString level = EString().Format("Lv: %d", slotPreview[slot].level);
				const int areaIndex = std::max(0, std::min((int)slotPreview[slot].areaIndex, DRAGON_ALPHA_AREA_COUNT - 1));
				const DragonAreaInfo* area = DragonAreaByIndex(areaIndex);
				EString progress = EString().Format("%s  Gold %d  W%d/L%d",
					area ? area->name : "Unknown",
					(int)slotPreview[slot].gold,
					(int)slotPreview[slot].battlesWon,
					(int)slotPreview[slot].battlesLost);

			const int avatarType = std::max((int)DRAGON_AVATAR_WARRIOR, std::min((int)slotPreview[slot].avatarType, (int)DRAGON_AVATAR_COUNT - 1));
			ERect portraitRect(
				rect.x + 12,
				rect.y + 30,
				std::max(24, std::min(92, rect.width / 3)),
				std::max(24, std::min(84, rect.height - 54))
			);
			DrawImageContain(slotAvatar[avatarType], portraitRect);

				const int textX = portraitRect.x + portraitRect.width + 10;
				const int textWidth = std::max(1, rect.x + rect.width - textX - 10);
				const int lineHeight = LayoutLineHeight(fontMain, 12, 24);
				const int lineStep = LayoutLineStep(fontMain, 14, 28, 2);
				const int textTop = rect.y + 34;
					const ERect nameRect(textX, textTop - 2, textWidth, lineHeight + 4);
					const ERect levelRect(textX, textTop - 2 + lineStep, textWidth, lineHeight + 4);
					const ERect progressRect(textX, textTop - 2 + lineStep * 2, textWidth, lineHeight + 4);
#if defined(DRAGON_TEST)
					ReportValidationLabel(name, nameRect);
					ReportValidationLabel(level, levelRect);
					ReportValidationLabel(progress, progressRect);
#endif
					DrawLeftClampedLabel(fontMain, name, nameRect, 0, 0);
					DrawLeftClampedLabel(fontMain, level, levelRect, 0, 0);
					DrawLeftClampedLabel(fontMain, progress, progressRect, 0, 0);
				ERect actionRect(rect.x + rect.width - 88, rect.y + 8, 76, 20);
#if defined(DRAGON_TEST)
				ReportValidationLabel("OPEN GAME...", actionRect);
#endif
				DrawCenteredLabel(fontMain, "OPEN GAME...", actionRect);
			} else {
				ERect emptyRect(rect.x + 14, rect.y + rect.height / 2 - 10, rect.width - 28, 20);
#if defined(DRAGON_TEST)
				ReportValidationLabel("NEW GAME...", emptyRect);
#endif
				DrawCenteredLabel(fontMain, "NEW GAME...", emptyRect);
			}
		} else if(slotFilled[slot]) {
			ERect actionRect(rect.x + rect.width - 88, rect.y + 8, 76, 20);
#if defined(DRAGON_TEST)
			ReportValidationLabel("OPEN GAME...", actionRect);
#endif
		} else {
			ERect emptyRect(rect.x + 14, rect.y + rect.height / 2 - 10, rect.width - 28, 20);
#if defined(DRAGON_TEST)
			ReportValidationLabel("NEW GAME...", emptyRect);
#endif
		}

	if(slotFilled[slot]) {
		imageDelete.DrawRect(deleteRect[slot]);
		if(not fontMain.IsEmpty()) {
			DrawCenteredLabel(fontMain, "DEL", deleteRect[slot], -1);
		}
	}

	return slotFilled[slot];
}

void NewGame::OnDraw () {
	const LegacyCanvas canvas = MakeLegacyCanvasInPreferredView(*this, 800, 600);
	const ERect safe = canvas.frame;
	UpdateLayoutRects(canvas);

	DrawImageContain(imageBackground, safe);
	DrawImageContain(imagePanel, LegacyRect(canvas, 182, 88, 594, 512), EColor(0xffffff24));
	imageDelete.DrawRect(backRect);
#if defined(DRAGON_TEST)
	ReportValidationLabel("BACK", backRect);
#endif
	if(not fontMain.IsEmpty()) {
		DrawCenteredLabel(fontMain, "BACK", backRect);
	}

	for(int i = 0; i < DRAGON_ALPHA_SLOT_COUNT; i++)
		DrawSlotCard(i, slotRect[i]);

	if(not fontMain.IsEmpty() and deleteConfirmActive == false) {
		ERect hintRect = ClipRectToBounds(LegacyRect(canvas, 194, 488, 570, 22), safe);
		if(HasAnyFilledSlot()) {
#if defined(DRAGON_TEST)
			ReportValidationLabel("Tap a slot to load. Tap DEL to erase a save.", hintRect);
#endif
			DrawCenteredLabel(fontMain, "Tap a slot to load. Tap DEL to erase a save.", hintRect);
		}
		else {
#if defined(DRAGON_TEST)
			ReportValidationLabel("No saves found. Tap any slot to start NEW GAME.", hintRect);
#endif
			DrawCenteredLabel(fontMain, "No saves found. Tap any slot to start NEW GAME.", hintRect);
		}
	} else if(deleteConfirmActive == false) {
#if defined(DRAGON_TEST)
		ERect hintRect = ClipRectToBounds(LegacyRect(canvas, 194, 488, 570, 22), safe);
		if(HasAnyFilledSlot())
			ReportValidationLabel("Tap a slot to load. Tap DEL to erase a save.", hintRect);
		else
			ReportValidationLabel("No saves found. Tap any slot to start NEW GAME.", hintRect);
#endif
	}

	if(messageText.IsEmpty() == false and GetMilliseconds() - messageTimestamp < 2800) {
		ERect messageRect = ClipRectToBounds(LegacyRect(canvas, 194, 514, 570, 34), safe);
		imagePanel.DrawRect(messageRect);
#if defined(DRAGON_TEST)
		ReportValidationLabel(messageText, InsetRect(messageRect, 6));
#endif
		if(not fontMain.IsEmpty())
			DrawWrappedLabel(fontMain, messageText, InsetRect(messageRect, 6), 2);
	}

	if(deleteConfirmActive) {
		imagePanel.DrawRect(deleteConfirmRect);
		imageDeleteActive.DrawRect(deleteConfirmYesRect);
		imageSlotFilled.DrawRect(deleteConfirmNoRect);

		const int slotNumber = deleteConfirmSlot + 1;
		ERect textRect(deleteConfirmRect.x + 14, deleteConfirmRect.y + 14, deleteConfirmRect.width - 28, 72);
#if defined(DRAGON_TEST)
		ReportValidationLabel(EString().Format("Delete slot %d? This cannot be undone.", slotNumber), textRect);
		ReportValidationLabel("DELETE", deleteConfirmYesRect);
		ReportValidationLabel("CANCEL", deleteConfirmNoRect);
#endif
		if(not fontMain.IsEmpty()) {
			DrawWrappedLabel(fontMain, EString().Format("Delete slot %d? This cannot be undone.", slotNumber), textRect, 3);
			DrawCenteredLabel(fontMain, "DELETE", deleteConfirmYesRect);
			DrawCenteredLabel(fontMain, "CANCEL", deleteConfirmNoRect);
		}
	}
}

void NewGame::OnTouch (int x, int y) {
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

	if(deleteConfirmActive) {
		if(HitTouchRect(deleteConfirmYesRect, x, y, 44, 44, 2)) {
			PlaySoundIfLoaded(soundDelete);
			if(deleteConfirmSlot >= 0 and DragonDeleteSlot(deleteConfirmSlot + 1))
				SetMessage("Slot deleted.");
			else
				SetMessage("Unable to delete slot.");
			RefreshSlots();
			CloseDeleteConfirm();
			return;
		}
		if(HitTouchRect(deleteConfirmNoRect, x, y, 44, 44, 2) or deleteConfirmRect.IsPointInRect(x, y) == false) {
			PlaySoundIfLoaded(soundClick);
			CloseDeleteConfirm();
			SetMessage("Delete canceled.");
		}
		return;
	}

	if(HitTouchRect(backRect, x, y, 44, 44, 2)) {
		PlaySoundIfLoaded(soundClick);
		RunNewNode("Splash");
		return;
	}

	for(int i = 0; i < DRAGON_ALPHA_SLOT_COUNT; i++) {
		if(HitTouchRect(slotRect[i], x, y, 44, 44, 6) == false)
			continue;

		if(slotFilled[i]) {
			if(HitTouchRect(deleteRect[i], x, y, 56, 44, 4)) {
				PlaySoundIfLoaded(soundClick);
				OpenDeleteConfirm(i);
				return;
			}
			PlaySoundIfLoaded(soundClick);
			if(DragonLoadSlot(i + 1))
				RunNewNode("WorldMap");
			else {
				if(DragonSlotHasCorruption(i + 1) and DragonRecoverCorruptedSlot(i + 1))
					SetMessage(EString().Format("Slot %d was corrupted and has been reset.", i + 1));
				else
					SetMessage("Unable to load slot.");
				RefreshSlots();
			}
			return;
		}

		PlaySoundIfLoaded(soundClick);
		gPendingSlot = i + 1;
		RunNewNode("NewAvatar");
		return;
	}

	if(GetMilliseconds() - refreshTimer > 2000)
		RefreshSlots();
}

void NewGame::OnTouchMove (int x, int y) {
	EPoint input = NormalizeInputPoint(*this, x, y);
	x = input.x;
	y = input.y;
	if(touchHandledThisSequence
		and (std::abs(x - touchDownX) > 3 or std::abs(y - touchDownY) > 3))
		touchMovedThisSequence = true;
}

void NewGame::OnTouchUp (int x, int y) {
	EPoint input = NormalizeInputPoint(*this, x, y);
	x = input.x;
	y = input.y;
	const ERect inputBounds = MakePreferredViewRect(GetSafeRect(), DESIGN_PREFERRED_WIDTH, DESIGN_PREFERRED_HEIGHT);
	const ERect safeBounds = GetSafeRect();
	const bool touchUpFallbackAllowed = GetMilliseconds() >= touchUpGuardUntil;
	if((inputBounds.IsPointInRect(x, y) or safeBounds.IsPointInRect(x, y))
		and touchUpFallbackAllowed
		and touchHandledThisSequence == false
		and touchMovedThisSequence == false)
		OnTouch(x, y);
	touchHandledThisSequence = false;
	touchMovedThisSequence = false;
}

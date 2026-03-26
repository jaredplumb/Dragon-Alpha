/**
 * @file Status.h
 * @brief Status/inventory scene interface and equipment/inventory state declarations.
 */
#ifndef DRAGON_ALPHA_STATUS_H
#define DRAGON_ALPHA_STATUS_H

#include "Global.h"

struct LegacyCanvas;

class Status : public ENodeWithName<Status, "Status"> {
public:
	Status ();
	void OnDraw () override;
	void OnTouch (int x, int y) override;
	void OnTouchMove (int x, int y) override;
	void OnTouchUp (int x, int y) override;

private:
	void ClampSelection ();
	void ClampFieldCommandSelection ();
	void BuildFieldCommandList ();
	bool IsFieldCommandMode () const;
	void TryEquipSelected ();
	void TryRunFieldCommand ();
	void TrySellSelected ();
	void TryUnequipSlot (int slotType);
	void SetMessage (const EString& text);
	void UpdateLayoutRects (const LegacyCanvas& canvas);
	void RefreshItemPreview ();
	EString BuildFieldCommandDetail () const;
	const char* HeaderLabel () const;
	const char* ActionLabel () const;
	const char* ItemImageName (int itemType) const;

	EImage imageBackground;
	EImage imagePanel;
	EImage imageStats;
	EImage imageList;
	EImage imageSelection;
	EImage imageButton;
	EImage imageButtonAlt;
	EImage imageItemPreview;
	EImage itemIcon[DRAGON_ITEM_COUNT];
	int previewItemType;
	ESound soundClick;
	ESound soundEquip;
	ESound soundUse;
	EFont fontHeader;
	EFont fontMain;
	UITransition transition;

	ERect listRect[8];
	ERect weaponRect;
	ERect armorRect;
	ERect relicRect;
	ERect previousRect;
	ERect nextRect;
	ERect actionRect;
	ERect sellRect;
	ERect backRect;
	DragonStatusEntryMode entryMode;
	DragonLegacyCommandAction fieldCommands[8];
	int fieldCommandCount;
	int selectedFieldCommand;
	int scrollOffset;
	int selectedInventoryIndex;
	bool listDragActive;
	bool touchHandledThisSequence;
	bool touchMovedThisSequence;
	int listDragLastY;
	int64_t messageTimestamp;
	EString messageText;
};

#endif // DRAGON_ALPHA_STATUS_H

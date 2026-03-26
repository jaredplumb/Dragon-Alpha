/**
 * @file Status.cpp
 * @brief Status/inventory scene behavior, equipment actions, and UI handling implementation.
 */
#include "Status.h"
#include "LayoutUtil.h"

#include <cstdlib>
#include <cstring>

namespace {
static const bool kStatusNodeFactoryRegistered = DragonEnsureNodeFactoryRegistered<Status>();

#if defined(DRAGON_TEST)
void ReportValidationLabel (const EString& text, const ERect& rect) {
	if(text.IsEmpty() == false && rect.width > 0 && rect.height > 0)
		ESystem::ReportTextDraw(text, rect);
}
#endif

const char* FieldElementLabel (int element) {
	switch(element) {
		case 1: return "LIGHT";
		case 2: return "DARK";
		case 3: return "FIRE";
		case 4: return "WATER";
		case 5: return "EARTH";
		case 6: return "AIR";
		default: return "NONE";
	}
}
}

static constexpr int kStatusVisibleRows = 7;

Status::Status ()
:	imageBackground(EColor(0x101826ff))
,	imagePanel(EColor(0x1d3049ff))
,	imageStats(EColor(0x2d4b70ff))
,	imageList(EColor(0x273549ff))
,	imageSelection(EColor(0x86bafc55))
,	imageButton(EColor(0x36587fff))
,	imageButtonAlt(EColor(0x6b4b30ff))
,	fontHeader("FontMissionHeader")
,	fontMain("FontMissionText")
,	transition(UITransition::FADE_BLACK, this)
,	previewItemType(-1)
,	entryMode(DRAGON_STATUS_ENTRY_EQUIPMENT)
,	fieldCommands{}
,	fieldCommandCount(0)
,	selectedFieldCommand(0)
,	scrollOffset(0)
,	selectedInventoryIndex(0)
,	listDragActive(false)
,	touchHandledThisSequence(false)
,	touchMovedThisSequence(false)
,	listDragLastY(0)
,	messageTimestamp(0)
,	messageText("")
{
	LoadImageOrFallback(imageBackground, "StatusBackground", EColor(0x101826ff));
	LoadImageOrFallback(imagePanel, "StatusPanel", EColor(0x1d3049ff));
	LoadImageOrFallback(imageStats, "StatusStats", EColor(0x2d4b70ff));
	LoadImageOrFallback(imageList, "StatusList", EColor(0x273549ff));
	LoadImageOrFallback(imageSelection, "StatusSelection", EColor(0x86bafc55));
	LoadImageOrFallback(imageButton, "StatusButton", EColor(0x36587fff));
	LoadImageOrFallback(imageButtonAlt, "StatusButtonAlt", EColor(0x6b4b30ff));
	LoadImageOrFallback(imageItemPreview, "ItemHealthPotion", EColor(0x3f5c80ff));
	for(int i = 0; i < DRAGON_ITEM_COUNT; i++) {
		const char* icon = ItemImageName(i);
		LoadImageOrFallback(itemIcon[i], icon, EColor(0x3f5c80ff));
	}

	// Legacy alias safety: these IDs can be tiny resource fragments in recovered builds.
	if(imageBackground.GetWidth() < 120 or imageBackground.GetHeight() < 48)
		imageBackground.New(EColor(0x101826ff));
	if(imagePanel.GetWidth() < 120 or imagePanel.GetHeight() < 48)
		imagePanel.New(EColor(0x1d3049f0));
	if(imageStats.GetWidth() < 120 or imageStats.GetHeight() < 32)
		imageStats.New(EColor(0x2d4b70f0));
	if(imageList.GetWidth() < 120 or imageList.GetHeight() < 120)
		imageList.New(EColor(0x273549f0));
	if(imageSelection.GetWidth() < 24 or imageSelection.GetHeight() < 24)
		imageSelection.New(EColor(0x86bafc55));
	if((imageButton.GetWidth() <= 96 and imageButton.GetHeight() <= 96) or imageButton.GetWidth() < 80 or imageButton.GetHeight() < 24)
		imageButton.New(EColor(0x36587fff));
	if((imageButtonAlt.GetWidth() <= 96 and imageButtonAlt.GetHeight() <= 96) or imageButtonAlt.GetWidth() < 80 or imageButtonAlt.GetHeight() < 24)
		imageButtonAlt.New(EColor(0x6b4b30ff));

	LoadSoundIfPresent(soundClick, "Click");
	LoadSoundIfPresent(soundEquip, "Select");
	LoadSoundIfPresent(soundUse, "Heal");
	entryMode = DragonConsumeStatusEntryMode();
	if(IsFieldCommandMode()) {
		BuildFieldCommandList();
		ClampFieldCommandSelection();
		if(fieldCommandCount > 0) {
			SetMessage(entryMode == DRAGON_STATUS_ENTRY_MAGIC
				? "Magic commands ready."
				: "Technique commands ready.");
		}
		else {
			entryMode = DRAGON_STATUS_ENTRY_EQUIPMENT;
		}
	}
	ClampSelection();
}

bool Status::IsFieldCommandMode () const {
	return entryMode == DRAGON_STATUS_ENTRY_MAGIC or entryMode == DRAGON_STATUS_ENTRY_TECH;
}

void Status::ClampSelection () {
	if(gSave.inventoryCount == 0) {
		selectedInventoryIndex = -1;
		scrollOffset = 0;
		RefreshItemPreview();
		return;
	}

	if(selectedInventoryIndex < 0)
		selectedInventoryIndex = 0;
	if(selectedInventoryIndex >= gSave.inventoryCount)
		selectedInventoryIndex = gSave.inventoryCount - 1;

	if(selectedInventoryIndex < scrollOffset)
		scrollOffset = selectedInventoryIndex;
	if(selectedInventoryIndex >= scrollOffset + kStatusVisibleRows)
		scrollOffset = selectedInventoryIndex - (kStatusVisibleRows - 1);
	if(scrollOffset < 0)
		scrollOffset = 0;
	RefreshItemPreview();
}

void Status::BuildFieldCommandList () {
	memset(fieldCommands, 0, sizeof(fieldCommands));
	const DragonLegacyCommandMenuMode mode = entryMode == DRAGON_STATUS_ENTRY_TECH
		? DRAGON_LEGACY_COMMAND_MENU_TECH
		: DRAGON_LEGACY_COMMAND_MENU_MAGIC;
	fieldCommandCount = DragonBuildLegacyCommandList(mode, fieldCommands, (int)(sizeof(fieldCommands) / sizeof(fieldCommands[0])));
	selectedFieldCommand = 0;
	scrollOffset = 0;
}

void Status::ClampFieldCommandSelection () {
	if(fieldCommandCount <= 0) {
		selectedFieldCommand = -1;
		scrollOffset = 0;
		return;
	}

	if(selectedFieldCommand < 0)
		selectedFieldCommand = 0;
	if(selectedFieldCommand >= fieldCommandCount)
		selectedFieldCommand = fieldCommandCount - 1;

	if(selectedFieldCommand < scrollOffset)
		scrollOffset = selectedFieldCommand;
	if(selectedFieldCommand >= scrollOffset + kStatusVisibleRows)
		scrollOffset = selectedFieldCommand - (kStatusVisibleRows - 1);
	if(scrollOffset < 0)
		scrollOffset = 0;
}

EString Status::BuildFieldCommandDetail () const {
	if(fieldCommandCount <= 0 or selectedFieldCommand < 0 or selectedFieldCommand >= fieldCommandCount)
		return "No legacy commands available for this class.";

	const DragonLegacyCommandAction& action = fieldCommands[selectedFieldCommand];
	const char* actionName = (action.name != nullptr and action.name[0] != '\0') ? action.name : "Action";
	if(action.isHeal) {
		return EString().Format(
			"%s  -  Field heal  -  Restores about %d HP",
			actionName,
			DragonComputeLegacyCommandFieldHeal(action)
		);
	}
	if(action.isAll)
		return EString().Format("%s  -  All-target battle command  -  Element %s", actionName, FieldElementLabel(action.effectElement));
	return EString().Format("%s  -  Single-target battle command  -  Element %s", actionName, FieldElementLabel(action.effectElement));
}

const char* Status::HeaderLabel () const {
	switch(entryMode) {
		case DRAGON_STATUS_ENTRY_MAGIC:
			return "MAGIC / COMMANDS";
		case DRAGON_STATUS_ENTRY_TECH:
			return "TECH / COMMANDS";
		case DRAGON_STATUS_ENTRY_EQUIPMENT:
		default:
			return "STATUS / INVENTORY";
	}
}

const char* Status::ActionLabel () const {
	if(IsFieldCommandMode())
		return entryMode == DRAGON_STATUS_ENTRY_MAGIC ? "CAST" : "USE";
	if(selectedInventoryIndex >= 0 and selectedInventoryIndex < gSave.inventoryCount) {
		const DragonItemInfo* item = DragonItemByType(gSave.inventory[selectedInventoryIndex].type);
		if(item != nullptr and item->slot == DRAGON_SLOT_CONSUMABLE)
			return "USE";
	}
	return "EQUIP";
}

const char* Status::ItemImageName (int itemType) const {
	switch(itemType) {
		case DRAGON_ITEM_IRON_BLADE: return "ItemIronBlade";
		case DRAGON_ITEM_ADEPT_WAND: return "ItemAdeptWand";
		case DRAGON_ITEM_HUNTER_BOW: return "ItemHunterBow";
		case DRAGON_ITEM_STEEL_SABER: return "ItemSteelSaber";
		case DRAGON_ITEM_WAR_HAMMER: return "ItemWarHammer";
		case DRAGON_ITEM_MYSTIC_TOME: return "ItemMysticTome";
		case DRAGON_ITEM_LEATHER_ARMOR: return "ItemLeatherArmor";
		case DRAGON_ITEM_TRAVELER_CLOAK: return "ItemTravelerCloak";
		case DRAGON_ITEM_TOWER_SHIELD: return "ItemTowerShield";
		case DRAGON_ITEM_SCOUT_CHARM: return "ItemScoutCharm";
		case DRAGON_ITEM_SEER_CHARM: return "ItemSeerCharm";
		case DRAGON_ITEM_DRAGON_TALISMAN: return "ItemDragonTalisman";
		case DRAGON_ITEM_GUARDIAN_RING: return "ItemGuardianRing";
		case DRAGON_ITEM_HEALTH_POTION: return "ItemHealthPotion";
		case DRAGON_ITEM_FIRE_SCROLL: return "ItemFireScroll";
		default: return NULL;
	}
}

void Status::RefreshItemPreview () {
	int itemType = DRAGON_ITEM_NONE;
	if(selectedInventoryIndex >= 0 and selectedInventoryIndex < gSave.inventoryCount)
		itemType = gSave.inventory[selectedInventoryIndex].type;
	else if(gSave.equippedWeapon != DRAGON_ITEM_NONE)
		itemType = gSave.equippedWeapon;
	else if(gSave.equippedArmor != DRAGON_ITEM_NONE)
		itemType = gSave.equippedArmor;
	else if(gSave.equippedRelic != DRAGON_ITEM_NONE)
		itemType = gSave.equippedRelic;
	if(previewItemType == itemType)
		return;

	previewItemType = itemType;
	const char* imageName = ItemImageName(itemType);
	LoadImageOrFallback(imageItemPreview, imageName, EColor(0x3f5c80ff));
}

void Status::TryEquipSelected () {
	if(selectedInventoryIndex < 0 or selectedInventoryIndex >= gSave.inventoryCount)
		return;

	const int itemType = gSave.inventory[selectedInventoryIndex].type;
	const DragonItemInfo* item = DragonItemByType(itemType);
	if(item == NULL) {
		SetMessage("Unknown item.");
		return;
	}

	if(item->slot == DRAGON_SLOT_CONSUMABLE) {
		int beforeHealth = gSave.health;
		if(DragonInventoryUse(selectedInventoryIndex) == false) {
			if(item->heal <= 0)
				SetMessage("That item is battle-only.");
			else if(gSave.health >= DragonGetMaxHealth())
				SetMessage("Health is already full.");
			else
				SetMessage("Cannot use this item now.");
			return;
		}

		int healed = std::max(0, (int)gSave.health - beforeHealth);
		ClampSelection();
		DragonSaveCurrentSlot();
		SetMessage(EString().Format("Used %s (+%d HP).", item->name, healed));
		PlaySoundIfLoaded(soundUse);
		return;
	}

	if(DragonEquipFromInventory(selectedInventoryIndex) == false) {
		SetMessage("Cannot equip this item.");
		return;
	}

	ClampSelection();
	DragonSaveCurrentSlot();
	SetMessage(EString().Format("Equipped %s.", item->name));
	PlaySoundIfLoaded(soundEquip);
}

void Status::TryRunFieldCommand () {
	if(IsFieldCommandMode() == false) {
		TryEquipSelected();
		return;
	}
	if(fieldCommandCount <= 0 or selectedFieldCommand < 0 or selectedFieldCommand >= fieldCommandCount) {
		SetMessage("No legacy commands available.");
		return;
	}

	const DragonLegacyCommandAction& action = fieldCommands[selectedFieldCommand];
	const char* actionName = (action.name != nullptr and action.name[0] != '\0') ? action.name : "Action";
	if(action.isHeal) {
		const int maxHealth = DragonGetMaxHealth();
		const int beforeHealth = gSave.health;
		const int heal = DragonComputeLegacyCommandFieldHeal(action);
		gSave.health = (int16_t)std::min(maxHealth, (int)gSave.health + heal);
		const int gained = std::max(0, (int)gSave.health - beforeHealth);
		if(gained > 0) {
			DragonSaveCurrentSlot();
			SetMessage(EString().Format("%s restores %d HP.", actionName, gained));
			PlaySoundIfLoaded(soundUse);
		}
		else {
			SetMessage(EString().Format("%s has no effect.", actionName));
			PlaySoundIfLoaded(soundClick);
		}
		return;
	}

	SetMessage(EString().Format("%s can only be used in battle.", actionName));
	PlaySoundIfLoaded(soundClick);
}

void Status::TrySellSelected () {
	if(selectedInventoryIndex < 0 or selectedInventoryIndex >= gSave.inventoryCount)
		return;

	const int itemType = gSave.inventory[selectedInventoryIndex].type;
	const DragonItemInfo* item = DragonItemByType(itemType);
	if(item == NULL) {
		SetMessage("Unknown item.");
		return;
	}

	int sellValue = std::max(1, (int)item->value / 4);
	if(DragonInventoryConsumeType(itemType, 1) == false) {
		SetMessage("Unable to sell item.");
		return;
	}

	gSave.gold = std::min(DESIGN_MAX_GOLD, gSave.gold + sellValue);
	ClampSelection();
	DragonSaveCurrentSlot();
	SetMessage(EString().Format("Sold %s for %d gold.", item->name, sellValue));
	PlaySoundIfLoaded(soundClick);
}

void Status::TryUnequipSlot (int slotType) {
	int equippedType = DRAGON_ITEM_NONE;
	const char* slotLabel = NULL;
	switch(slotType) {
		case DRAGON_SLOT_WEAPON:
			equippedType = gSave.equippedWeapon;
			slotLabel = "weapon";
			break;
		case DRAGON_SLOT_ARMOR:
			equippedType = gSave.equippedArmor;
			slotLabel = "armor";
			break;
		case DRAGON_SLOT_RELIC:
			equippedType = gSave.equippedRelic;
			slotLabel = "relic";
			break;
		default:
			return;
	}

	if(equippedType == DRAGON_ITEM_NONE) {
		SetMessage(EString().Format("No %s equipped.", slotLabel));
		return;
	}

	const DragonItemInfo* info = DragonItemByType(equippedType);
	if(DragonUnequipSlot(slotType) == false) {
		SetMessage("Inventory is full.");
		return;
	}

	ClampSelection();
	DragonSaveCurrentSlot();
	SetMessage(EString().Format("Unequipped %s.", info ? info->name : "item"));
	PlaySoundIfLoaded(soundEquip);
}

void Status::SetMessage (const EString& text) {
	messageText = text;
	messageTimestamp = GetMilliseconds();
}

void Status::UpdateLayoutRects (const LegacyCanvas& canvas) {
	const ERect safe = canvas.frame;
	const auto scaledW = [&canvas] (int value) {
		return std::max(1, LegacyRect(canvas, 0, 0, value, 0).width);
	};
	const auto scaledH = [&canvas] (int value) {
		return std::max(1, LegacyRect(canvas, 0, 0, 0, value).height);
	};
	const int mainLineHeight = fontMain.IsEmpty() ? 16 : LayoutLineHeight(fontMain, 12, 28);
	const int mainLineStep = fontMain.IsEmpty() ? 18 : LayoutLineStep(fontMain, 14, 30, 2);
	ERect statsRect = ClipRectToBounds(LegacyRect(canvas, 194, 120, 570, 118), safe);
	const int gearRight = statsRect.x + statsRect.width - scaledW(84);
	const int gearWidth = std::max(200, gearRight - (statsRect.x + 8));
	const int gearLabelHeight = std::max(18, mainLineHeight + 4);
	weaponRect = ERect(statsRect.x + 8, statsRect.y + 66, gearWidth, gearLabelHeight);
	armorRect = ERect(statsRect.x + 8, weaponRect.y + weaponRect.height + 2, (gearWidth - 8) / 2, gearLabelHeight);
	relicRect = ERect(armorRect.x + armorRect.width + 8, armorRect.y, armorRect.width, gearLabelHeight);

	ERect inventoryRect = ClipRectToBounds(LegacyRect(canvas, 194, 292, 570, 194), safe);
	const int detailHeight = mainLineStep * 2 + scaledH(6);
	const int listTop = inventoryRect.y + 6 + detailHeight;
	const int rowHeight = std::max(scaledH(21), (inventoryRect.height - 12 - detailHeight) / kStatusVisibleRows);
	for(int i = 0; i < kStatusVisibleRows; i++)
		listRect[i] = ERect(inventoryRect.x + 6, listTop + i * rowHeight, inventoryRect.width - 12, rowHeight - 3);

	const int buttonY = std::min(
		LegacyRect(canvas, 0, 500, 0, 0).y,
		safe.y + safe.height - std::max(scaledH(28), LegacyRect(canvas, 0, 0, 0, 34).height) - scaledH(2)
	);
	const int buttonGap = std::max(scaledW(8), LegacyRect(canvas, 0, 0, 8, 0).width);
	const int buttonHeight = std::max(28, LegacyRect(canvas, 0, 0, 0, 34).height);
	const int buttonWidth = (LegacyRect(canvas, 194, 0, 570, 0).width - buttonGap * 4) / 5;
	const int buttonStartX = LegacyRect(canvas, 194, 0, 0, 0).x;
	previousRect = ERect(buttonStartX, buttonY, buttonWidth, buttonHeight);
	nextRect = ERect(previousRect.x + previousRect.width + buttonGap, buttonY, buttonWidth, buttonHeight);
	actionRect = ERect(nextRect.x + nextRect.width + buttonGap, buttonY, buttonWidth, buttonHeight);
	sellRect = ERect(actionRect.x + actionRect.width + buttonGap, buttonY, buttonWidth, buttonHeight);
	backRect = ERect(sellRect.x + sellRect.width + buttonGap, buttonY, buttonWidth, buttonHeight);
}

void Status::OnDraw () {
	const LegacyCanvas canvas = MakeLegacyCanvasInPreferredView(*this, 800, 600);
	const ERect safe = canvas.frame;
	const auto scaledW = [&canvas] (int value) {
		return std::max(1, LegacyRect(canvas, 0, 0, value, 0).width);
	};
	const auto scaledH = [&canvas] (int value) {
		return std::max(1, LegacyRect(canvas, 0, 0, 0, value).height);
	};
	UpdateLayoutRects(canvas);
	DrawImageContain(imageBackground, safe);

	const ERect headerRect = ERect(
		LegacyRect(canvas, 196, 94, 0, 0).x,
		LegacyRect(canvas, 196, 94, 0, 0).y,
		scaledW(280),
		scaledH(24)
	);
	const char* headerLabel = HeaderLabel();
#if defined(DRAGON_TEST)
	ReportValidationLabel(headerLabel, headerRect);
#endif
	if(not fontHeader.IsEmpty()) {
		fontHeader.Draw(headerLabel, headerRect.x, headerRect.y);
	}

	ERect statsRect = ClipRectToBounds(LegacyRect(canvas, 194, 120, 570, 118), safe);
	imageStats.DrawRect(statsRect);
	const int mainLineHeight = fontMain.IsEmpty() ? 16 : LayoutLineHeight(fontMain, 12, 28);
	const int mainLineStep = fontMain.IsEmpty() ? 18 : LayoutLineStep(fontMain, 14, 30, 2);

	const DragonItemInfo* weapon = DragonItemByType(gSave.equippedWeapon);
	const DragonItemInfo* armor = DragonItemByType(gSave.equippedArmor);
	const DragonItemInfo* relic = DragonItemByType(gSave.equippedRelic);
	const int xpNeeded = std::max(1, DragonGetLevelXPRequirement(gSave.level));
	const DragonAreaInfo* area = DragonAreaByIndex(std::max(0, std::min((int)gSave.areaIndex, DRAGON_ALPHA_AREA_COUNT - 1)));
	const int statsTextY = statsRect.y + scaledH(10);
	const ERect summaryRectA(statsRect.x + scaledW(10), statsTextY - scaledH(2), statsRect.width - scaledW(20), mainLineHeight + scaledH(4));
	const ERect summaryRectB(statsRect.x + scaledW(10), statsTextY - scaledH(2) + mainLineStep, statsRect.width - scaledW(20), mainLineHeight + scaledH(4));
	const ERect summaryRectC(statsRect.x + scaledW(10), statsTextY - scaledH(2) + mainLineStep * 2, statsRect.width - scaledW(20), mainLineHeight + scaledH(4));
	const EString summaryA = EString().Format("%s  Lv.%d  XP %u/%d", gSave.name[0] == '\0' ? "HERO" : gSave.name, gSave.level, gSave.xp, xpNeeded);
	const EString summaryB = EString().Format("HP %d/%d  Gold %d  Area %s", (int)gSave.health, DragonGetMaxHealth(), (int)gSave.gold, area ? area->name : "Unknown");
	const EString summaryC = EString().Format("STR %d  DEF %d  SPD %d  MAG %d", DragonGetAttack(), DragonGetDefense(), DragonGetSpeed(), DragonGetMagic());
	const int gearRight = statsRect.x + statsRect.width - scaledW(84);
	const int gearWidth = std::max(200, gearRight - (statsRect.x + 8));
	const int gearLabelHeight = std::max(18, mainLineHeight + 4);
	weaponRect = ERect(statsRect.x + 8, statsRect.y + 66, gearWidth, gearLabelHeight);
	armorRect = ERect(statsRect.x + 8, weaponRect.y + weaponRect.height + 2, (gearWidth - 8) / 2, gearLabelHeight);
	relicRect = ERect(armorRect.x + armorRect.width + 8, armorRect.y, armorRect.width, gearLabelHeight);
	const EString weaponLabel = EString().Format("Weapon: %s", weapon ? weapon->name : "None");
	const EString armorLabel = EString().Format("Armor: %s", armor ? armor->name : "None");
	const EString relicLabel = EString().Format("Relic: %s", relic ? relic->name : "None");
#if defined(DRAGON_TEST)
	ReportValidationLabel(summaryA, summaryRectA);
	ReportValidationLabel(summaryB, summaryRectB);
	ReportValidationLabel(summaryC, summaryRectC);
	ReportValidationLabel(weaponLabel, weaponRect);
	ReportValidationLabel(armorLabel, armorRect);
	ReportValidationLabel(relicLabel, relicRect);
#endif
	if(not fontMain.IsEmpty()) {
		DrawLeftClampedLabel(fontMain, summaryA, summaryRectA);
		DrawLeftClampedLabel(fontMain, summaryB, summaryRectB);
		DrawLeftClampedLabel(fontMain, summaryC, summaryRectC);
		DrawLeftClampedLabel(fontMain, weaponLabel, weaponRect, 2, 0);
		DrawLeftClampedLabel(fontMain, armorLabel, armorRect, 2, 0);
		DrawLeftClampedLabel(fontMain, relicLabel, relicRect, 2, 0);
	}

	const ERect itemPreviewRect(statsRect.x + statsRect.width - scaledW(74), statsRect.y + scaledH(20), scaledW(56), scaledH(56));
	DrawImageContain(imageItemPreview, itemPreviewRect);
#if defined(DRAGON_TEST)
	ReportValidationLabel("Status item preview", itemPreviewRect);
#endif

	const bool showMessage = messageText.IsEmpty() == false and GetMilliseconds() - messageTimestamp < 2300;
	const ERect infoRect = ClipRectToBounds(LegacyRect(canvas, 194, 244, 570, 42), safe);
	if(showMessage) {
		imageStats.DrawRect(infoRect);
		const ERect messageRect = InsetRect(infoRect, 6);
#if defined(DRAGON_TEST)
		ReportValidationLabel(messageText, messageRect);
#endif
		if(not fontMain.IsEmpty())
			DrawWrappedLabel(fontMain, messageText, messageRect, 2);
	}

	ERect inventoryRect = ClipRectToBounds(LegacyRect(canvas, 194, 292, 570, 194), safe);
	imageList.DrawRect(inventoryRect);

	const int detailHeight = mainLineStep * 2 + scaledH(6);
	EString detail = IsFieldCommandMode()
		? BuildFieldCommandDetail()
		: EString("Inventory empty. Visit merchants or find treasure.");
	if(IsFieldCommandMode() == false and selectedInventoryIndex >= 0 and selectedInventoryIndex < gSave.inventoryCount) {
		const DragonInventoryEntry& entry = gSave.inventory[selectedInventoryIndex];
		const DragonItemInfo* info = DragonItemByType(entry.type);
		if(info != NULL) {
			if(info->slot == DRAGON_SLOT_CONSUMABLE) {
				if(info->heal > 0)
					detail = EString().Format("%s x%d  -  Restores %d HP  -  Sell %dg", info->name, (int)entry.count, (int)info->heal, std::max(1, (int)info->value / 4));
				else
					detail = EString().Format("%s x%d  -  Battle item  -  Sell %dg", info->name, (int)entry.count, std::max(1, (int)info->value / 4));
			} else {
				int equippedType = DRAGON_ITEM_NONE;
				switch(info->slot) {
					case DRAGON_SLOT_WEAPON: equippedType = gSave.equippedWeapon; break;
					case DRAGON_SLOT_ARMOR: equippedType = gSave.equippedArmor; break;
					case DRAGON_SLOT_RELIC: equippedType = gSave.equippedRelic; break;
					default: break;
				}
				const DragonItemInfo* equippedInfo = DragonItemByType(equippedType);
				int atkDelta = (int)info->attack - (equippedInfo ? (int)equippedInfo->attack : 0);
				int defDelta = (int)info->defense - (equippedInfo ? (int)equippedInfo->defense : 0);
				int magDelta = (int)info->magic - (equippedInfo ? (int)equippedInfo->magic : 0);
				int spdDelta = (int)info->speed - (equippedInfo ? (int)equippedInfo->speed : 0);
				detail = EString().Format("%s x%d  -  dATK%+d dDEF%+d dMAG%+d dSPD%+d  -  Sell %dg",
					info->name,
					(int)entry.count,
					atkDelta,
					defDelta,
					magDelta,
					spdDelta,
					std::max(1, (int)info->value / 4));
			}
		}
	}
	const ERect detailRect(inventoryRect.x + 8, inventoryRect.y + 6, inventoryRect.width - 16, detailHeight);
#if defined(DRAGON_TEST)
	ReportValidationLabel(detail, detailRect);
#endif
	if(not fontMain.IsEmpty())
		DrawWrappedLabel(fontMain, detail, detailRect, 2);

	const int listTop = inventoryRect.y + 6 + detailHeight;
	const int rowHeight = std::max(scaledH(21), (inventoryRect.height - 12 - detailHeight) / kStatusVisibleRows);
	for(int i = 0; i < kStatusVisibleRows; i++) {
		listRect[i] = ERect(inventoryRect.x + 6, listTop + i * rowHeight, inventoryRect.width - 12, rowHeight - 3);
		if(IsFieldCommandMode()) {
			const int commandIndex = scrollOffset + i;
			if(commandIndex >= fieldCommandCount)
				continue;
			if(commandIndex == selectedFieldCommand)
				imageSelection.DrawRect(listRect[i]);
			const DragonLegacyCommandAction& command = fieldCommands[commandIndex];
			const char* commandName = (command.name != nullptr and command.name[0] != '\0') ? command.name : "Action";
			const char* tag = command.isHeal ? "[HEAL]" : (command.isAll ? "[ALL]" : "[BATTLE]");
			const EString rowLabel = EString().Format("%s  %s", commandName, tag);
			const ERect rowTextRect(listRect[i].x + 10, listRect[i].y, listRect[i].width - 16, listRect[i].height);
#if defined(DRAGON_TEST)
			ReportValidationLabel(rowLabel, rowTextRect);
#endif
			if(not fontMain.IsEmpty())
				fontMain.Draw(rowLabel, rowTextRect.x, listRect[i].y + std::max(2, (listRect[i].height - mainLineHeight) / 2));
			continue;
		}

		const int inventoryIndex = scrollOffset + i;
		if(inventoryIndex >= gSave.inventoryCount)
			continue;

		if(inventoryIndex == selectedInventoryIndex)
			imageSelection.DrawRect(listRect[i]);

		const DragonInventoryEntry& entry = gSave.inventory[inventoryIndex];
		const DragonItemInfo* info = DragonItemByType(entry.type);
		const ERect iconRect(listRect[i].x + 4, listRect[i].y + 3, listRect[i].height - 6, listRect[i].height - 6);
		if(entry.type >= 0 and entry.type < DRAGON_ITEM_COUNT)
			DrawImageContain(itemIcon[entry.type], iconRect);
		if(info != NULL) {
			const bool equipped = entry.type == gSave.equippedWeapon or entry.type == gSave.equippedArmor or entry.type == gSave.equippedRelic;
			const EString rowLabel = EString().Format("%s  x%d%s", info->name, (int)entry.count, equipped ? "  [E]" : "");
			const ERect rowTextRect(iconRect.x + iconRect.width + 8, listRect[i].y, listRect[i].width - iconRect.width - 12, listRect[i].height);
#if defined(DRAGON_TEST)
			ReportValidationLabel(rowLabel, rowTextRect);
#endif
			if(not fontMain.IsEmpty())
				fontMain.Draw(rowLabel, iconRect.x + iconRect.width + 8, listRect[i].y + std::max(2, (listRect[i].height - mainLineHeight) / 2));
		}
	}

	imageButtonAlt.DrawRect(previousRect);
	imageButtonAlt.DrawRect(nextRect);
	imageButton.DrawRect(actionRect);
	imageButtonAlt.DrawRect(sellRect);
	imageButton.DrawRect(backRect);

	const EString actionLabel = ActionLabel();
	const EString sellLabel = IsFieldCommandMode() ? "INFO" : "SELL";

#if defined(DRAGON_TEST)
	ReportValidationLabel("PREV", previousRect);
	ReportValidationLabel("NEXT", nextRect);
	ReportValidationLabel(actionLabel, actionRect);
	ReportValidationLabel(sellLabel, sellRect);
	ReportValidationLabel("BACK", backRect);
#endif
	if(not fontMain.IsEmpty()) {
		DrawCenteredLabel(fontMain, "PREV", previousRect);
		DrawCenteredLabel(fontMain, "NEXT", nextRect);
		DrawCenteredLabel(fontMain, actionLabel, actionRect);
		DrawCenteredLabel(fontMain, sellLabel, sellRect);
		DrawCenteredLabel(fontMain, "BACK", backRect);
	}
}

void Status::OnTouch (int x, int y) {
	EPoint input = NormalizeInputPoint(*this, x, y);
	x = input.x;
	y = input.y;
	const ERect inputBounds = MakePreferredViewRect(GetSafeRect(), DESIGN_PREFERRED_WIDTH, DESIGN_PREFERRED_HEIGHT);
	const ERect safeBounds = GetSafeRect();
	if(inputBounds.IsPointInRect(x, y) == false) {
		if(safeBounds.IsPointInRect(x, y) == false) {
			listDragActive = false;
			touchHandledThisSequence = false;
			touchMovedThisSequence = false;
			return;
		}
	}
	const LegacyCanvas canvas = MakeLegacyCanvasInPreferredView(*this, 800, 600);
	UpdateLayoutRects(canvas);

	touchHandledThisSequence = true;
	touchMovedThisSequence = false;
	listDragActive = false;

	if(IsFieldCommandMode()) {
		for(int i = 0; i < kStatusVisibleRows; i++) {
			if(HitTouchRect(listRect[i], x, y, 44, 44, 3) == false)
				continue;
			const int commandIndex = scrollOffset + i;
			if(commandIndex < fieldCommandCount) {
				selectedFieldCommand = commandIndex;
				ClampFieldCommandSelection();
				PlaySoundIfLoaded(soundClick);
				listDragActive = true;
				listDragLastY = y;
				touchMovedThisSequence = false;
			}
			return;
		}

		if(HitTouchRect(previousRect, x, y, 44, 44, 2)) {
			if(fieldCommandCount <= 0) {
				SetMessage("No legacy commands available.");
				PlaySoundIfLoaded(soundClick);
				return;
			}
			selectedFieldCommand = std::max(0, selectedFieldCommand - 1);
			ClampFieldCommandSelection();
			PlaySoundIfLoaded(soundClick);
			return;
		}

		if(HitTouchRect(nextRect, x, y, 44, 44, 2)) {
			if(fieldCommandCount <= 0) {
				SetMessage("No legacy commands available.");
				PlaySoundIfLoaded(soundClick);
				return;
			}
			selectedFieldCommand++;
			ClampFieldCommandSelection();
			PlaySoundIfLoaded(soundClick);
			return;
		}

		if(HitTouchRect(actionRect, x, y, 44, 44, 2)) {
			PlaySoundIfLoaded(soundClick);
			TryRunFieldCommand();
			return;
		}

		if(HitTouchRect(sellRect, x, y, 44, 44, 2)) {
			PlaySoundIfLoaded(soundClick);
			SetMessage(BuildFieldCommandDetail());
			return;
		}

		if(HitTouchRect(backRect, x, y, 44, 44, 2)) {
			PlaySoundIfLoaded(soundClick);
			DragonSaveCurrentSlot();
			RunNewNode("WorldMap");
			return;
		}
		return;
	}

	if(HitTouchRect(weaponRect, x, y, 44, 44, 2)) {
		PlaySoundIfLoaded(soundClick);
		TryUnequipSlot(DRAGON_SLOT_WEAPON);
		return;
	}

	if(HitTouchRect(armorRect, x, y, 44, 44, 2)) {
		PlaySoundIfLoaded(soundClick);
		TryUnequipSlot(DRAGON_SLOT_ARMOR);
		return;
	}

	if(HitTouchRect(relicRect, x, y, 44, 44, 2)) {
		PlaySoundIfLoaded(soundClick);
		TryUnequipSlot(DRAGON_SLOT_RELIC);
		return;
	}

	for(int i = 0; i < kStatusVisibleRows; i++) {
		if(HitTouchRect(listRect[i], x, y, 44, 44, 3) == false)
			continue;
		int index = scrollOffset + i;
		if(index < gSave.inventoryCount) {
			selectedInventoryIndex = index;
			ClampSelection();
			PlaySoundIfLoaded(soundClick);
			listDragActive = true;
			listDragLastY = y;
			touchMovedThisSequence = false;
		}
		return;
	}

	if(HitTouchRect(previousRect, x, y, 44, 44, 2)) {
		if(gSave.inventoryCount == 0) {
			SetMessage("Inventory is empty.");
			PlaySoundIfLoaded(soundClick);
			return;
		}
		scrollOffset = std::max(0, scrollOffset - 1);
		selectedInventoryIndex = std::max(0, selectedInventoryIndex - 1);
		ClampSelection();
		PlaySoundIfLoaded(soundClick);
		return;
	}

	if(HitTouchRect(nextRect, x, y, 44, 44, 2)) {
		if(gSave.inventoryCount == 0) {
			SetMessage("Inventory is empty.");
			PlaySoundIfLoaded(soundClick);
			return;
		}
		scrollOffset++;
		selectedInventoryIndex++;
		ClampSelection();
		PlaySoundIfLoaded(soundClick);
		return;
	}

	if(HitTouchRect(actionRect, x, y, 44, 44, 2)) {
		if(gSave.inventoryCount == 0) {
			SetMessage("Inventory is empty.");
			PlaySoundIfLoaded(soundClick);
			return;
		}
		PlaySoundIfLoaded(soundClick);
		TryEquipSelected();
		return;
	}

	if(HitTouchRect(sellRect, x, y, 44, 44, 2)) {
		if(gSave.inventoryCount == 0) {
			SetMessage("Inventory is empty.");
			PlaySoundIfLoaded(soundClick);
			return;
		}
		PlaySoundIfLoaded(soundClick);
		TrySellSelected();
		return;
	}

	if(HitTouchRect(backRect, x, y, 44, 44, 2)) {
		PlaySoundIfLoaded(soundClick);
		DragonSaveCurrentSlot();
		RunNewNode("WorldMap");
	}
}

void Status::OnTouchMove (int x, int y) {
	EPoint input = NormalizeInputPoint(*this, x, y);
	x = input.x;
	y = input.y;
	const ERect inputBounds = MakePreferredViewRect(GetSafeRect(), DESIGN_PREFERRED_WIDTH, DESIGN_PREFERRED_HEIGHT);
	const ERect safeBounds = GetSafeRect();
	if(inputBounds.IsPointInRect(x, y) == false) {
		if(safeBounds.IsPointInRect(x, y) == false)
			return;
	}
	const LegacyCanvas canvas = MakeLegacyCanvasInPreferredView(*this, 800, 600);
	UpdateLayoutRects(canvas);

	if(listDragActive == false)
		return;

	const int rowHeight = (kStatusVisibleRows > 1) ? std::max(16, listRect[1].y - listRect[0].y) : 20;
	const int step = std::max(10, rowHeight / 2);
	const int delta = y - listDragLastY;
	if(std::abs(delta) < step)
		return;

	const int direction = (delta > 0) ? 1 : -1;
	if(IsFieldCommandMode()) {
		if(fieldCommandCount <= 0)
			return;
		selectedFieldCommand += direction;
		ClampFieldCommandSelection();
		listDragLastY = y;
		touchMovedThisSequence = true;
		return;
	}
	if(gSave.inventoryCount <= 0)
		return;
	selectedInventoryIndex += direction;
	ClampSelection();
	listDragLastY = y;
	touchMovedThisSequence = true;
}

void Status::OnTouchUp (int x, int y) {
	EPoint input = NormalizeInputPoint(*this, x, y);
	x = input.x;
	y = input.y;
	const ERect inputBounds = MakePreferredViewRect(GetSafeRect(), DESIGN_PREFERRED_WIDTH, DESIGN_PREFERRED_HEIGHT);
	const ERect safeBounds = GetSafeRect();
	const LegacyCanvas canvas = MakeLegacyCanvasInPreferredView(*this, 800, 600);
	UpdateLayoutRects(canvas);
	if((inputBounds.IsPointInRect(x, y) or safeBounds.IsPointInRect(x, y)) and touchHandledThisSequence == false and touchMovedThisSequence == false)
		OnTouch(x, y);
	listDragActive = false;
	touchHandledThisSequence = false;
	touchMovedThisSequence = false;
}

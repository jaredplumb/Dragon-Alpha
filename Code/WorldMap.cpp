/**
 * @file WorldMap.cpp
 * @brief World map traversal, event routing, and exploration flow implementation.
 */
#include "WorldMap.h"
#include "LayoutUtil.h"
#include "LegacyItemMap.h"
#include "LegacyWarpData.generated.h"
#include "LegacyScriptData.generated.h"
#include "LegacyBattleData.generated.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cmath>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <queue>
#include <unordered_map>
#include <vector>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace {
static const bool kWorldMapNodeFactoryRegistered = DragonEnsureNodeFactoryRegistered<WorldMap>();

#if defined(DRAGON_TEST)
void ReportValidationLabel (const EString& text, const ERect& rect) {
	if(text.IsEmpty() == false && rect.width > 0 && rect.height > 0)
		ESystem::ReportTextDraw(text, rect);
}

const char* ValidationEventTypeLabel (uint8_t eventType) {
	switch(eventType) {
		case 1: return "Talk";
		case 2: return "ItemGate";
		case 3: return "Treasure";
		case 4: return "Heal";
		case 5: return "Train";
		case 6: return "Shop";
		case 7: return "Warp";
		case 8: return "Gate";
		case 9: return "Challenge";
		default: return "Event";
	}
}
#endif
}

static constexpr uint16_t DRAGON_EVENT_FLAG_TREASURE = 0x0001;
static constexpr uint16_t DRAGON_EVENT_FLAG_GATE_CLEARED = 0x0002;
static constexpr uint16_t DRAGON_EVENT_FLAG_WARP_OPENED = 0x0004;
static constexpr uint16_t DRAGON_EVENT_FLAG_SUMMIT_CLAIMED = 0x0008;
static constexpr uint16_t DRAGON_EVENT_FLAG_CHALLENGE_ONE = 0x0020;
static constexpr uint16_t DRAGON_EVENT_FLAG_CHALLENGE_TWO = 0x0040;
static constexpr uint16_t DRAGON_EVENT_FLAG_LEGACY_COMPLETION_MARKER = 0x8000;
static constexpr uint8_t MAP_SCRIPT_ACTION_YES_NO = 100;
static constexpr uint8_t MAP_SCRIPT_ACTION_REFRESH = 101;
static constexpr int MAP_TOUCH_PATH_STEP_MS = 72;
static constexpr int MAP_TOUCH_DRAG_REPEAT_MS = 120;
static constexpr int MAP_TOUCH_TAP_DEBOUNCE_MS = 80;
static constexpr int MAP_TOUCH_DRAG_DEADZONE_PX = 10;
static constexpr int MAP_TOUCH_EVENT_MAGNET_DISTANCE = 2;
static constexpr int MAP_TOUCH_RETARGET_COOLDOWN_MS = 72;
static constexpr int MAP_INTERACTION_HINT_PERSIST_MS = 120000;
// Keep traversal SFX explicitly design-driven; default remains disabled.
static constexpr bool MAP_ENABLE_TRAVERSAL_SFX = (DESIGN_TRAVERSAL_SFX_ENABLED != 0);
static constexpr int MAP_TRAVERSAL_STEP_VARIANTS = (DESIGN_TRAVERSAL_SFX_STEP_VARIANTS < 1 ? 1 : (DESIGN_TRAVERSAL_SFX_STEP_VARIANTS > 3 ? 3 : DESIGN_TRAVERSAL_SFX_STEP_VARIANTS));
static constexpr int MAP_TRAVERSAL_BUMP_COOLDOWN_MS = (DESIGN_TRAVERSAL_SFX_BUMP_COOLDOWN_MS < 0 ? 0 : DESIGN_TRAVERSAL_SFX_BUMP_COOLDOWN_MS);
static constexpr bool MAP_TOUCH_ROUTE_BREADCRUMB_OVERLAY = false;
static constexpr int MAP_VIEW_COLS = 21;
static constexpr int MAP_VIEW_ROWS = 16;
// Legacy camera focus in DrawMap used src origin offsets (-320, -224),
// which maps to player anchor (10, 7) in a 21x16 tile viewport.
static constexpr int MAP_VIEW_FOCUS_COL = 10;
static constexpr int MAP_VIEW_FOCUS_ROW = 7;
static constexpr int MAP_ENCOUNTER_CHANCE_PERCENT = 7;
static constexpr int MAP_ENCOUNTER_NORMAL_WEIGHT = 5;
static constexpr int MAP_ENCOUNTER_RARE_WEIGHT = 1;
static constexpr uint16_t LEGACY_PLUGIN_CORE = 1;
static constexpr uint16_t LEGACY_PLUGIN_CAVES = 2;
static constexpr uint16_t LEGACY_PLUGIN_MOUNTAIN = 3;
static constexpr uint16_t LEGACY_PLUGIN_FOREST = 4;
static constexpr uint16_t LEGACY_PLUGIN_DESERT = 5;
static constexpr uint16_t LEGACY_PLUGIN_TITANIUM = 100;
static constexpr uint16_t LEGACY_PLUGIN_GOBLIN = 101;
static constexpr uint16_t LEGACY_PLUGIN_MEADOWS = 1000;
static int gLegacyPendingBattleArea = -1;
static int gLegacyPendingBattleEventIndex = -1;
static int gLegacyPendingBattleNextAction = -1;

static const char* kAreaShortName[DRAGON_ALPHA_AREA_COUNT] = {
	"Eleusis",
	"Meadows",
	"Forests",
	"Caves",
	"Mountains",
	"Peak",
};

static const char* kAreaMapImageName[DRAGON_ALPHA_AREA_COUNT] = {
	"AreaEleusisMap",
	"AreaMeadowsMap",
	"AreaForestsMap",
	"AreaCavesMap",
	"AreaMountainsMap",
	"AreaPeakMap",
};

static const char* kAreaOverlayImageName[DRAGON_ALPHA_AREA_COUNT] = {
	"AreaEleusisOverlay",
	"AreaMeadowsOverlay",
	"AreaForestsOverlay",
	"AreaCavesOverlay",
	"AreaMountainsOverlay",
	"AreaPeakOverlay",
};

static const char* kAreaMaskImageName[DRAGON_ALPHA_AREA_COUNT] = {
	"AreaEleusisMask",
	"AreaMeadowsMask",
	"AreaForestsMask",
	"AreaCavesMask",
	"AreaMountainsMask",
	"AreaPeakMask",
};

static const char* kAreaEleusisStrongholdMapImageName = "AreaEleusisStrongholdMap";
static const char* kAreaEleusisStrongholdOverlayImageName = "AreaEleusisStrongholdOverlay";
static const char* kAreaEleusisStrongholdMaskImageName = "AreaEleusisStrongholdMask";

struct ResourceVisualProbe {
	bool valid;
	int width;
	int height;
	double opaqueRatio;
	double brightOpaqueRatio;
	int lumaRange;
};

static ResourceVisualProbe ProbeResourceVisual (const char* resourceName) {
	ResourceVisualProbe probe = {false, 0, 0, 0.0, 0.0, 0};
	if(resourceName == NULL or resourceName[0] == '\0')
		return probe;

	EImage::Resource resource;
	if(resource.New(resourceName) == false)
		return probe;
	if(resource.buffer == NULL or resource.width <= 0 or resource.height <= 0)
		return probe;
	if(resource.bufferSize < (int64_t)resource.width * (int64_t)resource.height * 4)
		return probe;

	const int totalPixels = resource.width * resource.height;
	const int sampleStride = std::max(1, totalPixels / 8192);
	int sampledPixels = 0;
	int opaquePixels = 0;
	int brightOpaquePixels = 0;
	int minLuma = 255;
	int maxLuma = 0;
	for(int pixelIndex = 0; pixelIndex < totalPixels; pixelIndex += sampleStride) {
		const int offset = pixelIndex * 4;
		const uint8_t red = resource.buffer[offset + 0];
		const uint8_t green = resource.buffer[offset + 1];
		const uint8_t blue = resource.buffer[offset + 2];
		const uint8_t alpha = resource.buffer[offset + 3];
		const int luma = ((int)red + (int)green + (int)blue) / 3;
		minLuma = std::min(minLuma, luma);
		maxLuma = std::max(maxLuma, luma);
		if(alpha >= 16) {
			opaquePixels++;
			if(luma >= 245)
				brightOpaquePixels++;
		}
		sampledPixels++;
	}
	if(sampledPixels <= 0)
		return probe;

	probe.valid = true;
	probe.width = resource.width;
	probe.height = resource.height;
	probe.opaqueRatio = (double)opaquePixels / (double)sampledPixels;
	probe.brightOpaqueRatio = opaquePixels > 0 ? (double)brightOpaquePixels / (double)opaquePixels : 0.0;
	probe.lumaRange = maxLuma - minLuma;
	return probe;
}

static bool IsLikelyInvalidAreaMapProbe (const ResourceVisualProbe& probe) {
	if(probe.valid == false)
		return true;
	if(probe.width < 256 or probe.height < 192)
		return true;
	// Guard against accidental white flood placeholders after legacy import mismatch.
	if(probe.opaqueRatio > 0.70 and probe.brightOpaqueRatio > 0.97 and probe.lumaRange < 24)
		return true;
	return false;
}

static bool IsLikelyInvalidAreaOverlayProbe (const ResourceVisualProbe& probe) {
	if(probe.valid == false)
		return true;
	if(probe.width < 256 or probe.height < 192)
		return true;
	if(probe.opaqueRatio > 0.90 and probe.brightOpaqueRatio > 0.95 and probe.lumaRange < 18)
		return true;
	return false;
}

static ERect ComputeOpaqueSourceRect (const char* resourceName, uint8_t alphaThreshold = 24) {
	if(resourceName == NULL or resourceName[0] == '\0')
		return ERect();

	EImage::Resource resource;
	if(resource.New(resourceName) == false)
		return ERect();
	if(resource.buffer == NULL or resource.width <= 0 or resource.height <= 0)
		return ERect();
	if(resource.bufferSize < (int64_t)resource.width * (int64_t)resource.height * 4)
		return ERect();

	int minX = resource.width;
	int minY = resource.height;
	int maxX = -1;
	int maxY = -1;
	int opaquePixels = 0;
	for(int y = 0; y < resource.height; y++) {
		for(int x = 0; x < resource.width; x++) {
			const int offset = (y * resource.width + x) * 4;
			const uint8_t alpha = resource.buffer[offset + 3];
			if(alpha < alphaThreshold)
				continue;
			minX = std::min(minX, x);
			minY = std::min(minY, y);
			maxX = std::max(maxX, x);
			maxY = std::max(maxY, y);
			opaquePixels++;
		}
	}

	if(opaquePixels < 8 or maxX < minX or maxY < minY)
		return ERect(0, 0, resource.width, resource.height);
	return ERect(minX, minY, maxX - minX + 1, maxY - minY + 1);
}

static uint16_t LegacyDefaultPluginForArea (int area) {
	switch(area) {
		case DRAGON_AREA_ELEUSIS: return LEGACY_PLUGIN_CORE;
		case DRAGON_AREA_MEADOWS: return LEGACY_PLUGIN_MEADOWS;
		case DRAGON_AREA_FORESTS: return LEGACY_PLUGIN_FOREST;
		case DRAGON_AREA_CAVES: return LEGACY_PLUGIN_CAVES;
		case DRAGON_AREA_MOUNTAINS: return LEGACY_PLUGIN_MOUNTAIN;
		case DRAGON_AREA_PEAK: return LEGACY_PLUGIN_MOUNTAIN;
		default: return LEGACY_PLUGIN_CORE;
	}
}

static uint16_t LegacyDefaultMapForArea (int area) {
	return area == DRAGON_AREA_PEAK ? 133 : 128;
}

static const char* LegacyPluginDisplayName (uint16_t pluginId) {
	switch(pluginId) {
		case LEGACY_PLUGIN_CORE: return "Core";
		case LEGACY_PLUGIN_CAVES: return "Caves";
		case LEGACY_PLUGIN_MOUNTAIN: return "Mountain";
		case LEGACY_PLUGIN_FOREST: return "Forest";
		case LEGACY_PLUGIN_DESERT: return "Desert";
		case LEGACY_PLUGIN_TITANIUM: return "Titanium";
		case LEGACY_PLUGIN_GOBLIN: return "Goblin";
		case LEGACY_PLUGIN_MEADOWS: return "The Meadows";
		default: return NULL;
	}
}

static bool IsOptionalLegacyPlugin (uint16_t pluginId) {
	return pluginId == LEGACY_PLUGIN_DESERT
		or pluginId == LEGACY_PLUGIN_TITANIUM
		or pluginId == LEGACY_PLUGIN_GOBLIN;
}

static bool CellInsideLegacyRect (int x, int y, int x1, int y1, int x2, int y2) {
	const int minX = std::min(x1, x2);
	const int maxX = std::max(x1, x2);
	const int minY = std::min(y1, y2);
	const int maxY = std::max(y1, y2);
	return x >= minX and x <= maxX and y >= minY and y <= maxY;
}

static bool HasItemTypeEquippedOrInBag (int itemType) {
	if(itemType <= DRAGON_ITEM_NONE or itemType >= DRAGON_ITEM_COUNT)
		return false;
	if(gSave.equippedWeapon == itemType or gSave.equippedArmor == itemType or gSave.equippedRelic == itemType)
		return true;
	for(int i = 0; i < gSave.inventoryCount; i++)
		if(gSave.inventory[i].type == itemType and gSave.inventory[i].count > 0)
			return true;
	return false;
}

static uint16_t ChooseLegacyBattleGroupId (const uint16_t* groups, int count) {
	if(groups == NULL or count <= 0)
		return 0;
	uint16_t valid[16] = {};
	int validCount = 0;
	for(int i = 0; i < count and i < 16; i++) {
		if(groups[i] == 0)
			continue;
		valid[validCount++] = groups[i];
	}
	if(validCount <= 0)
		return 0;
	const int index = (int)ENode::GetRandom((uint32_t)validCount);
	return valid[index];
}

static bool LegacyBattleGroupExists (uint16_t pluginId, uint16_t groupId) {
	if(pluginId == 0 or groupId == 0)
		return false;
	return DragonFindLegacyGroupData(pluginId, groupId) != NULL;
}

static void ScriptTrace (const char* format, ...) {
#if defined(DRAGON_ALPHA_SCRIPT_TRACE)
	va_list args;
	va_start(args, format);
	std::fprintf(stderr, "[WorldMapScript] ");
	std::vfprintf(stderr, format, args);
	std::fprintf(stderr, "\n");
	va_end(args);
#else
	(void)format;
#endif
}

WorldMap::WorldMap ()
:	imageBackground(EColor(0x0f1b2cff))
,	imageUiLegacy(EColor(0x00000000))
,	imageUiOverlay1Legacy(EColor(0x00000000))
,	imageUiOverlay2Legacy(EColor(0x00000000))
,	imageHelpLegacy(EColor(0x00000000))
,	imageAreaMap(EColor(0x0f1b2cff))
,	imageAreaOverlay(EColor(0x00000000))
,	imagePanel(EColor(0x1a2f49ff))
,	imageAreaOpen(EColor(0x2f6288ff))
,	imageAreaLocked(EColor(0x2a3240ff))
,	imageAreaSelected(EColor(0x7eb8ff66))
	,	imageGridOpen(EColor(0x26425fff))
	,	imageGridBlocked(EColor(0x172435ff))
	,	imageGridAccent(EColor(0x9bd0ff40))
	,	imageGridPlayer(EColor(0xe1b94bff))
	,	imageMapAvatar(EColor(0xe1b94bff))
	,	imageBattleButton(EColor(0x7a4d2bff))
	,	imageStatusButton(EColor(0x36536fff))
	,	imageSaveButton(EColor(0x366741ff))
,	imageMenuButton(EColor(0x5b3c70ff))
,	fontHeader("FontMissionHeader")
,	fontMain("FontMissionText")
,	transition(UITransition::FADE_BLACK, this)
,	selectedArea(0)
,	mapX(1)
,	mapY(1)
,	moveAnimating(false)
,	moveFromX(0)
,	moveFromY(0)
,	moveToX(0)
,	moveToY(0)
,	moveAnimationStartMs(0)
,	moveAnimationEndMs(0)
,	visibleStartX(0)
,	visibleStartY(0)
,	visibleCols(0)
,	visibleRows(0)
,	mapAvatarSourceRect()
,	loadedAreaVisual(-1)
,	saveBannerTimer(0)
	,	messageTimestamp(0)
	,	messageText("")
	,	promptActive(false)
	,	promptEventArea(-1)
	,	promptEventIndex(-1)
,	promptNextAction(-1)
,	promptNextNoAction(-1)
	,	promptFromMove(false)
	,	promptLegacyScript(false)
	,	promptTrainingPending(false)
	,	promptTrainingCost(0)
	,	promptText("")
	,	promptRect()
	,	promptYesRect()
	,	promptNoRect()
	,	talkActive(false)
	,	talkEventArea(-1)
	,	talkEventIndex(-1)
,	talkNextAction(-1)
,	talkFromMove(false)
,	talkLegacyScript(false)
,	talkText("")
	,	talkRect()
	,	talkOkRect()
	,	tutorialRect()
	,	tutorialOkRect()
,	endingRect()
,	endingContinueRect()
,	endingMenuRect()
,	shopOffers{}
,	shopOfferCount(0)
,	shopScrollOffset(0)
,	legacyShopContextActive(false)
,	legacyShopPluginId(0)
,	legacyShopId(0)
,	shopActive(false)
,	shopHasPendingScript(false)
,	shopEventArea(-1)
,	shopEventIndex(-1)
,	shopNextAction(-1)
,	shopFromMove(false)
,	tutorialActive(false)
,	endingActive(false)
,	stepsSinceBattle(0)
,	stepsSinceRecovery(0)
,	facingDX(0)
,	facingDY(1)
,	legacyScriptActions()
,	legacyScriptEventArea(-1)
,	legacyScriptEventIndex(-1)
,	autoPathCells()
,	autoPathIndex(0)
,	autoPathTargetX(0)
,	autoPathTargetY(0)
,	autoPathRequestedX(0)
,	autoPathRequestedY(0)
,	autoPathAdjusted(false)
,	autoPathNextMoveTime(0)
,	lastAutoPathRetargetAt(0)
,	interactHintX(-1)
,	interactHintY(-1)
,	interactHintUntil(0)
,	lastTapAt(0)
,	lastBumpSoundAt(0)
,	lastTapMapX(-1)
,	lastTapMapY(-1)
,	swipeTracking(false)
,	swipeStartX(0)
,	swipeStartY(0)
	,	swipeLastX(0)
	,	swipeLastY(0)
	,	swipeLastMoveTime(0)
	,	touchUpGuardUntil(0)
	,	touchHandledThisSequence(false)
	,	touchMovedThisSequence(false)
{
	// Ignore orphan touch-up events right after scene transitions.
	touchUpGuardUntil = GetMilliseconds() + 220;
	memset(areaMaskBlocked, 0, sizeof(areaMaskBlocked));
	memset(areaBlocked, 0, sizeof(areaBlocked));
	memset(eventPositionValid, 0, sizeof(eventPositionValid));
	memset(eventPositionX, 0, sizeof(eventPositionX));
	memset(eventPositionY, 0, sizeof(eventPositionY));
	memset(legacyEventCacheValid, 0, sizeof(legacyEventCacheValid));
	memset(legacyEventCachePluginId, 0, sizeof(legacyEventCachePluginId));
	memset(legacyEventCacheMapId, 0, sizeof(legacyEventCacheMapId));
	memset(legacyEventCacheCount, 0, sizeof(legacyEventCacheCount));
	memset(legacyEventCache, 0, sizeof(legacyEventCache));
	for(int area = 0; area < DRAGON_ALPHA_AREA_COUNT; area++) {
		legacyPluginIdByArea[area] = LegacyDefaultPluginForArea(area);
		legacyMapIdByArea[area] = LegacyDefaultMapForArea(area);
	}
	activeMapSourceRect = ERect();
	activeOverlaySourceRect = ERect();
	mapTileFrameRect = ERect();
	mapTileSize = 0;

	if(gSave.slotIndex == 0) {
		DragonEnsureSaveInfo();
		if(gSaveInfo.selectedSlot != 0)
			DragonLoadSlot(gSaveInfo.selectedSlot);
	}

	if(gSave.slotIndex == 0) {
		for(int slot = 1; slot <= DRAGON_ALPHA_SLOT_COUNT; slot++) {
			if(DragonSlotExists(slot) == false)
				continue;
			DragonLoadSlot(slot);
			break;
		}
	}

	if(gSave.slotIndex != 0)
		DragonLoadWorldState(gSave.slotIndex);
	NormalizeProgressionState();

	LoadAreaMaskCollisions();
	SyncSelectedAreaFromSave();
	SyncMapPositionFromWorldState();
	autoPathTargetX = mapX;
	autoPathTargetY = mapY;
	autoPathRequestedX = mapX;
	autoPathRequestedY = mapY;
	autoPathAdjusted = false;
	ApplyPendingBattleResult();
	NormalizeProgressionState();
	tutorialActive = false;
	if(tutorialActive == false)
		SetMessage(ObjectiveText());

	LoadImageOrFallback(imageBackground, "WorldMapBackground", EColor(0x0f1b2cff));
	LoadImageOrFallback(imageUiLegacy, "WorldMapUiLegacy", EColor(0x00000000));
	LoadImageOrFallback(imageUiOverlay1Legacy, "WorldMapUiOverlay1Legacy", EColor(0x00000000));
	LoadImageOrFallback(imageUiOverlay2Legacy, "WorldMapUiOverlay2Legacy", EColor(0x00000000));
	LoadImageOrFallback(imageHelpLegacy, "WorldMapHelpLegacy", "WorldMapTutorialLegacy", EColor(0x00000000));
	LoadImageOrFallback(imagePanel, "WorldMapPanel", EColor(0x1a2f49ff));
	LoadImageOrFallback(imageAreaOpen, "WorldMapAreaOpen", EColor(0x2f6288ff));
	LoadImageOrFallback(imageAreaLocked, "WorldMapAreaLocked", EColor(0x2a3240ff));
	LoadImageOrFallback(imageAreaSelected, "WorldMapAreaSelected", EColor(0x7eb8ff66));
	LoadImageOrFallback(imageGridOpen, "WorldMapGridOpen", EColor(0x26425fff));
	LoadImageOrFallback(imageGridBlocked, "WorldMapGridBlocked", EColor(0x172435ff));
	LoadImageOrFallback(imageGridAccent, "WorldMapGridAccent", EColor(0x9bd0ff40));
	LoadImageOrFallback(imageGridPlayer, "WorldMapGridPlayer", EColor(0xe1b94bff));
	LoadImageOrFallback(imageBattleButton, "WorldMapBattleButton", EColor(0x7a4d2bff));
	LoadImageOrFallback(imageStatusButton, "WorldMapStatusButton", EColor(0x36536fff));
	LoadImageOrFallback(imageSaveButton, "WorldMapSaveButton", EColor(0x366741ff));
	LoadImageOrFallback(imageMenuButton, "WorldMapMenuButton", EColor(0x5b3c70ff));
	LoadImageOrFallback(imageEventTalk, "AvatarWarrior", EColor(0x85b2dfff));
	LoadImageOrFallback(imageEventTreasure, "ItemScoutCharm", EColor(0xd5b96cff));
	LoadImageOrFallback(imageEventHeal, "ItemHealthPotion", EColor(0x66bb75ff));
	LoadImageOrFallback(imageEventTrain, "AvatarRanger", EColor(0x7a98c4ff));
	LoadImageOrFallback(imageEventShop, "ItemIronBlade", EColor(0xd6c2a1ff));
	LoadImageOrFallback(imageEventWarp, "ItemSeerCharm", EColor(0xa889d8ff));
	LoadImageOrFallback(imageEventGate, "BattleEnemyEleusis", EColor(0xd5715eff));
	LoadImageOrFallback(imageTutorialLegacy, "WorldMapTutorialLegacy", EColor(0x00000000));
	int avatarIndex = std::max((int)DRAGON_AVATAR_WARRIOR, std::min((int)gSave.avatarType, (int)DRAGON_AVATAR_COUNT - 1));
	const char* avatarResourceName = "AvatarWarrior";
	EColor avatarFallback(0xe1b94bff);
	if(avatarIndex == DRAGON_AVATAR_SORCERER) {
		avatarResourceName = "AvatarSorcerer";
		avatarFallback = EColor(0xb58ce8ff);
	}
	else if(avatarIndex == DRAGON_AVATAR_RANGER) {
		avatarResourceName = "AvatarRanger";
		avatarFallback = EColor(0x8ec07dff);
	}
	LoadImageOrFallback(imageMapAvatar, avatarResourceName, avatarFallback);
	mapAvatarSourceRect = ComputeOpaqueSourceRect(avatarResourceName, 20);
	for(int i = 0; i < DRAGON_ITEM_COUNT; i++)
		LoadImageOrFallback(itemIcon[i], ItemImageName(i), EColor(0x3f5c80ff));

	// Preserve visual stability when legacy sprite fragments are mapped to UI IDs.
	if(imagePanel.GetWidth() < 120 or imagePanel.GetHeight() < 48)
		imagePanel.New(EColor(0x1a2f49f0));
	if((imageSaveButton.GetWidth() <= 96 and imageSaveButton.GetHeight() <= 96) or imageSaveButton.GetWidth() < 80 or imageSaveButton.GetHeight() < 24)
		imageSaveButton.New(EColor(0x366741ff));
	if((imageMenuButton.GetWidth() <= 96 and imageMenuButton.GetHeight() <= 96) or imageMenuButton.GetWidth() < 80 or imageMenuButton.GetHeight() < 24)
		imageMenuButton.New(EColor(0x5b3c70ff));
	if((imageBattleButton.GetWidth() <= 96 and imageBattleButton.GetHeight() <= 96) or imageBattleButton.GetWidth() < 80 or imageBattleButton.GetHeight() < 24)
		imageBattleButton.New(EColor(0x7a4d2bff));
	if((imageStatusButton.GetWidth() <= 96 and imageStatusButton.GetHeight() <= 96) or imageStatusButton.GetWidth() < 80 or imageStatusButton.GetHeight() < 24)
		imageStatusButton.New(EColor(0x36536fff));
	if(imageGridBlocked.GetWidth() < 32 or imageGridBlocked.GetHeight() < 32)
		imageGridBlocked.New(EColor(0x172435ff));
	if(imageGridAccent.GetWidth() < 24 or imageGridAccent.GetHeight() < 24)
		imageGridAccent.New(EColor(0x9bd0ff40));
	if(imageAreaSelected.GetWidth() < 24 or imageAreaSelected.GetHeight() < 24)
		imageAreaSelected.New(EColor(0x7eb8ff88));
	if(imageGridPlayer.GetWidth() < 16 or imageGridPlayer.GetHeight() < 16 or (imageGridPlayer.GetWidth() <= 32 and imageGridPlayer.GetHeight() <= 32))
		imageGridPlayer.New(EColor(0xe1b94bff));
	if(imageMapAvatar.GetWidth() < 24 or imageMapAvatar.GetHeight() < 24)
		imageMapAvatar.New(EColor(0xe1b94bff));
	if(mapAvatarSourceRect.width <= 0 or mapAvatarSourceRect.height <= 0
		or mapAvatarSourceRect.x < 0 or mapAvatarSourceRect.y < 0
		or mapAvatarSourceRect.x + mapAvatarSourceRect.width > imageMapAvatar.GetWidth()
		or mapAvatarSourceRect.y + mapAvatarSourceRect.height > imageMapAvatar.GetHeight()) {
		mapAvatarSourceRect = imageMapAvatar.GetRect();
	}

	LoadSoundIfPresent(soundClick, "Click");
	LoadSoundIfPresent(soundBattleEntry, "Select");
	LoadSoundIfPresent(soundTreasure, "Treasure");
	LoadSoundIfPresent(soundHeal, "Heal");
	LoadSoundIfPresent(soundSave, "Click");
	if(MAP_ENABLE_TRAVERSAL_SFX) {
		LoadSoundIfPresent(soundStep[0], "MovePathA");
		LoadSoundIfPresent(soundStep[1], "MovePathB");
		LoadSoundIfPresent(soundStep[2], "MovePathC");
		LoadSoundIfPresent(soundBump[0], "MoveBumpA");
		LoadSoundIfPresent(soundBump[1], "MoveBumpB");
		LoadSoundIfPresent(soundBump[2], "MoveBumpC");
		LoadSoundIfPresent(soundBump[3], "MoveBumpD");
	}

#if defined(DRAGON_TEST)
	if(DragonAutomationCaseIs("Validation_Shop_Default")
		|| DragonAutomationCaseIs("Validation_Shop_ReturnToWorldMap")
		|| DragonAutomationCaseIs("Validation_Shop_Purchase")
		|| DragonAutomationCaseIs("Validation_Shop_InsufficientGold")
		|| DragonAutomationCaseIs("Validation_Shop_DuplicateOwned")
		|| DragonAutomationCaseIs("Validation_Shop_InventoryFull")
		|| DragonAutomationCaseIs("Validation_Shop_MultiPurchase"))
		OpenLegacyShop(1, 128);
#endif

	EnsureAreaVisualLoaded();
}

void WorldMap::SyncSelectedAreaFromSave () {
	selectedArea = std::max(0, std::min((int)gSave.areaIndex, DRAGON_ALPHA_AREA_COUNT - 1));
	RestoreLegacyMapStateFromWorldState();
	if(IsAreaUnlocked(selectedArea) == false) {
		for(int i = 0; i < DRAGON_ALPHA_AREA_COUNT; i++) {
			if(IsAreaUnlocked(i)) {
				selectedArea = i;
				break;
			}
		}
	}
	gSave.areaIndex = (uint8_t)selectedArea;
	gSelectedArea = selectedArea;
	SetInteractionHint(-1, -1);
	SyncLegacyMapStateFromSelection();
	EnsureAreaVisualLoaded();
}

void WorldMap::SyncLegacyMapStateFromSelection () {
	for(int area = 0; area < DRAGON_ALPHA_AREA_COUNT; area++) {
		if(legacyPluginIdByArea[area] == 0)
			legacyPluginIdByArea[area] = LegacyDefaultPluginForArea(area);
		if(legacyMapIdByArea[area] == 0)
			legacyMapIdByArea[area] = LegacyDefaultMapForArea(area);
	}

	const uint16_t selectedPlugin = legacyPluginIdByArea[selectedArea];
	const uint16_t selectedMap = legacyMapIdByArea[selectedArea];
	if(DragonFindLegacyMapWarpData(selectedPlugin, selectedMap) != NULL) {
		StoreLegacyMapStateToWorldState();
		return;
	}

	legacyPluginIdByArea[selectedArea] = LegacyDefaultPluginForArea(selectedArea);
	legacyMapIdByArea[selectedArea] = LegacyDefaultMapForArea(selectedArea);
	StoreLegacyMapStateToWorldState();
}

void WorldMap::RestoreLegacyMapStateFromWorldState () {
	uint32_t packed = (uint32_t)gWorldState.reserved[0];
	packed |= ((uint32_t)gWorldState.reserved[1] << 8);
	packed |= ((uint32_t)gWorldState.reserved[2] << 16);

	const uint16_t pluginId = (uint16_t)(packed & 0x07FFu);
	const uint16_t mapId = (uint16_t)((packed >> 11) & 0x07FFu);
	if(pluginId == 0 or mapId == 0)
		return;
	if(DragonFindLegacyMapWarpData(pluginId, mapId) == NULL)
		return;

	const int restoredArea = AreaIndexForLegacyPluginMap(pluginId, mapId);
	if(restoredArea < 0 or restoredArea >= DRAGON_ALPHA_AREA_COUNT)
		return;

	legacyPluginIdByArea[restoredArea] = pluginId;
	legacyMapIdByArea[restoredArea] = mapId;
	selectedArea = restoredArea;
	gSelectedArea = selectedArea;
	gSave.areaIndex = (uint8_t)selectedArea;
	gSave.discoveredAreaMask |= (uint8_t)(1u << selectedArea);
}

void WorldMap::StoreLegacyMapStateToWorldState () {
	if(selectedArea < 0 or selectedArea >= DRAGON_ALPHA_AREA_COUNT)
		return;
	uint16_t pluginId = legacyPluginIdByArea[selectedArea];
	uint16_t mapId = legacyMapIdByArea[selectedArea];
	if(pluginId == 0)
		pluginId = LegacyDefaultPluginForArea(selectedArea);
	if(mapId == 0)
		mapId = LegacyDefaultMapForArea(selectedArea);
	pluginId &= 0x07FFu;
	mapId &= 0x07FFu;

	const uint32_t packed = (uint32_t)pluginId | ((uint32_t)mapId << 11);
	gWorldState.reserved[0] = (uint8_t)(packed & 0xFFu);
	gWorldState.reserved[1] = (uint8_t)((packed >> 8) & 0xFFu);
	gWorldState.reserved[2] = (uint8_t)((packed >> 16) & 0xFFu);
}

const char* WorldMap::LegacyAreaMapImageName (int area) const {
	if(area < 0 or area >= DRAGON_ALPHA_AREA_COUNT)
		return "WorldMapBackground";
	if(area == DRAGON_AREA_ELEUSIS and legacyPluginIdByArea[area] == LEGACY_PLUGIN_CORE and legacyMapIdByArea[area] == 129)
		return kAreaEleusisStrongholdMapImageName;
	return kAreaMapImageName[area];
}

const char* WorldMap::LegacyAreaOverlayImageName (int area) const {
	if(area < 0 or area >= DRAGON_ALPHA_AREA_COUNT)
		return NULL;
	if(area == DRAGON_AREA_ELEUSIS and legacyPluginIdByArea[area] == LEGACY_PLUGIN_CORE and legacyMapIdByArea[area] == 129)
		return kAreaEleusisStrongholdOverlayImageName;
	return kAreaOverlayImageName[area];
}

const char* WorldMap::LegacyAreaMaskImageName (int area) const {
	if(area < 0 or area >= DRAGON_ALPHA_AREA_COUNT)
		return NULL;
	if(area == DRAGON_AREA_ELEUSIS and legacyPluginIdByArea[area] == LEGACY_PLUGIN_CORE and legacyMapIdByArea[area] == 129)
		return kAreaEleusisStrongholdMaskImageName;
	return kAreaMaskImageName[area];
}

void WorldMap::EnsureAreaVisualLoaded () {
	if(selectedArea < 0 or selectedArea >= DRAGON_ALPHA_AREA_COUNT)
		return;
	const int visualKey = ((selectedArea & 0x07) << 22)
		| (((int)legacyPluginIdByArea[selectedArea] & 0x07FF) << 11)
		| ((int)legacyMapIdByArea[selectedArea] & 0x07FF);
	if(loadedAreaVisual == visualKey)
		return;

	const char* requestedMapName = LegacyAreaMapImageName(selectedArea);
	const char* canonicalMapName = kAreaMapImageName[selectedArea];
	const char* resolvedMapName = requestedMapName;
	ResourceVisualProbe mapProbe = ProbeResourceVisual(resolvedMapName);
	if(IsLikelyInvalidAreaMapProbe(mapProbe) and canonicalMapName != NULL and std::strcmp(canonicalMapName, resolvedMapName) != 0) {
		ResourceVisualProbe canonicalProbe = ProbeResourceVisual(canonicalMapName);
		if(IsLikelyInvalidAreaMapProbe(canonicalProbe) == false) {
			resolvedMapName = canonicalMapName;
			mapProbe = canonicalProbe;
		}
	}
	if(IsLikelyInvalidAreaMapProbe(mapProbe))
		resolvedMapName = "WorldMapBackground";
	LoadImageOrFallback(imageAreaMap, resolvedMapName, "WorldMapBackground", EColor(0x0f1b2cff));

	const char* overlayName = LegacyAreaOverlayImageName(selectedArea);
	const ResourceVisualProbe overlayProbe = ProbeResourceVisual(overlayName);
	const bool overlayLoaded = (IsLikelyInvalidAreaOverlayProbe(overlayProbe) == false) and imageAreaOverlay.New(overlayName);
	if(overlayLoaded == false)
		imageAreaOverlay.New(EColor(0x00000000));

	const int mapWidth = std::max(1, imageAreaMap.GetWidth());
	const int mapHeight = std::max(1, imageAreaMap.GetHeight());
	activeMapSourceRect = ERect(0, 0, mapWidth, mapHeight);
	if(imageAreaOverlay.IsEmpty() == false)
		activeOverlaySourceRect = ERect(0, 0, std::max(1, imageAreaOverlay.GetWidth()), std::max(1, imageAreaOverlay.GetHeight()));
	else
		activeOverlaySourceRect = ERect();
	// Keep full-surface legacy map/overlay framing and avoid mask-projected crops
	// that can shift authored coordinates versus classic object placement.
	loadedAreaVisual = visualKey;
	DragonPlayWorldMapMusic(selectedArea);
}

int WorldMap::AreaIndexForLegacyPluginMap (uint16_t pluginId, uint16_t mapId) const {
	switch(pluginId) {
		case LEGACY_PLUGIN_CORE: return DRAGON_AREA_ELEUSIS;
		case LEGACY_PLUGIN_MEADOWS: return DRAGON_AREA_MEADOWS;
		case LEGACY_PLUGIN_FOREST: return DRAGON_AREA_FORESTS;
		case LEGACY_PLUGIN_CAVES: return DRAGON_AREA_CAVES;
		case LEGACY_PLUGIN_MOUNTAIN:
			if(mapId == 133)
				return DRAGON_AREA_PEAK;
			return DRAGON_AREA_MOUNTAINS;
		default:
			return -1;
	}
}

void WorldMap::ApplyLegacyWarpState (int targetArea, uint16_t targetPluginId, uint16_t targetMapId, int targetX, int targetY) {
	const int sourceArea = selectedArea;
	gWorldState.areaMapX[sourceArea] = (uint8_t)std::max(0, std::min(mapX, DRAGON_ALPHA_MAP_WIDTH - 1));
	gWorldState.areaMapY[sourceArea] = (uint8_t)std::max(0, std::min(mapY, DRAGON_ALPHA_MAP_HEIGHT - 1));

	legacyPluginIdByArea[targetArea] = targetPluginId;
	legacyMapIdByArea[targetArea] = targetMapId;
	selectedArea = targetArea;
	gSelectedArea = selectedArea;
	gSave.areaIndex = (uint8_t)selectedArea;
	gSave.discoveredAreaMask |= (uint8_t)(1 << selectedArea);

	mapX = std::max(0, std::min(targetX, DRAGON_ALPHA_MAP_WIDTH - 1));
	mapY = std::max(0, std::min(targetY, DRAGON_ALPHA_MAP_HEIGHT - 1));
	SyncLegacyMapStateFromSelection();
	LoadAreaMaskCollisions();
	int openX = mapX;
	int openY = mapY;
	if(FindNearestOpenCell(selectedArea, mapX, mapY, openX, openY)) {
		mapX = openX;
		mapY = openY;
	}
	gWorldState.areaMapX[selectedArea] = (uint8_t)mapX;
	gWorldState.areaMapY[selectedArea] = (uint8_t)mapY;
	loadedAreaVisual = -1;
	EnsureAreaVisualLoaded();
	SetInteractionHint(-1, -1);
}

void WorldMap::SetUnavailableWarpMessage (uint16_t pluginId) {
	const char* plugName = LegacyPluginDisplayName(pluginId);
	if(IsOptionalLegacyPlugin(pluginId)) {
		if(plugName != NULL)
			SetMessageThrottled(EString().Format("%s plug-in content is not included in this build.", plugName), MAP_ROUTE_MESSAGE_THROTTLE_MS);
		else
			SetMessageThrottled("Optional plug-in content is not included in this build.", MAP_ROUTE_MESSAGE_THROTTLE_MS);
	}
	else if(plugName != NULL)
		SetMessageThrottled(EString().Format("%s plug-in destination is unavailable.", plugName), MAP_ROUTE_MESSAGE_THROTTLE_MS);
	else
		SetMessageThrottled("This destination requires an unavailable plug-in.", MAP_ROUTE_MESSAGE_THROTTLE_MS);
}

bool WorldMap::TryApplyLegacyWarpAtCell (int x, int y, bool fromMove) {
	if(selectedArea < 0 or selectedArea >= DRAGON_ALPHA_AREA_COUNT)
		return false;

	const uint16_t sourcePlugin = legacyPluginIdByArea[selectedArea];
	const uint16_t sourceMap = legacyMapIdByArea[selectedArea];
	const DragonLegacyMapWarpData* sourceData = DragonFindLegacyMapWarpData(sourcePlugin, sourceMap);
	if(sourceData == NULL or sourceData->warpCount <= 0 or sourceData->warps == NULL)
		return false;

	bool unsupportedTarget = false;
	uint16_t unsupportedPluginId = 0;

	for(int i = 0; i < sourceData->warpCount; i++) {
		const DragonLegacyWarpObject& warp = sourceData->warps[i];
		if(CellInsideLegacyRect(x, y, (int)warp.x1, (int)warp.y1, (int)warp.x2, (int)warp.y2) == false)
			continue;

		const int targetArea = AreaIndexForLegacyPluginMap(warp.targetPluginId, warp.targetMapId);
		if(targetArea < 0 or targetArea >= DRAGON_ALPHA_AREA_COUNT) {
			unsupportedTarget = true;
			unsupportedPluginId = warp.targetPluginId;
			continue;
		}

		const bool areaChanged = targetArea != selectedArea;
		ApplyLegacyWarpState(targetArea, warp.targetPluginId, warp.targetMapId, (int)warp.targetX, (int)warp.targetY);

		const DragonLegacyMapWarpData* targetData = DragonFindLegacyMapWarpData(warp.targetPluginId, warp.targetMapId);
		const char* targetMapName = (targetData != NULL and targetData->mapName != NULL and targetData->mapName[0] != '\0')
			? targetData->mapName
			: kAreaShortName[selectedArea];
		if(areaChanged)
			SetMessage(EString().Format("Warp: %s.", targetMapName));
		else
			SetMessage(EString().Format("Route: %s.", targetMapName));
		return true;
	}

	if(unsupportedTarget) {
		SetUnavailableWarpMessage(unsupportedPluginId);
		if(fromMove)
			SetInteractionHint(x, y);
		return true;
	}

	return false;
}

bool WorldMap::TryApplyLegacyBoundaryWarp (int nextX, int nextY, bool fromMove) {
	if(selectedArea < 0 or selectedArea >= DRAGON_ALPHA_AREA_COUNT)
		return false;

	const uint16_t sourcePlugin = legacyPluginIdByArea[selectedArea];
	const uint16_t sourceMap = legacyMapIdByArea[selectedArea];
	const DragonLegacyMapWarpData* sourceData = DragonFindLegacyMapWarpData(sourcePlugin, sourceMap);
	if(sourceData == NULL)
		return false;

	const DragonLegacyDirectionalWarp* edgeWarp = NULL;
	if(nextX < 0)
		edgeWarp = &sourceData->westWarp;
	else if(nextX >= DRAGON_ALPHA_MAP_WIDTH)
		edgeWarp = &sourceData->eastWarp;
	else if(nextY < 0)
		edgeWarp = &sourceData->northWarp;
	else if(nextY >= DRAGON_ALPHA_MAP_HEIGHT)
		edgeWarp = &sourceData->southWarp;

	if(edgeWarp == NULL or edgeWarp->active == 0 or edgeWarp->targetMapId == 0)
		return false;

	const int targetArea = AreaIndexForLegacyPluginMap(edgeWarp->targetPluginId, edgeWarp->targetMapId);
	if(targetArea < 0 or targetArea >= DRAGON_ALPHA_AREA_COUNT) {
		SetUnavailableWarpMessage(edgeWarp->targetPluginId);
		(void)fromMove;
		return true;
	}

	int targetX = (int)edgeWarp->targetX;
	int targetY = (int)edgeWarp->targetY;
	if(targetX < 0)
		targetX = mapX;
	if(targetY < 0)
		targetY = mapY;

	const bool areaChanged = targetArea != selectedArea;
	ApplyLegacyWarpState(targetArea, edgeWarp->targetPluginId, edgeWarp->targetMapId, targetX, targetY);

	const DragonLegacyMapWarpData* targetData = DragonFindLegacyMapWarpData(edgeWarp->targetPluginId, edgeWarp->targetMapId);
	const char* targetMapName = (targetData != NULL and targetData->mapName != NULL and targetData->mapName[0] != '\0')
		? targetData->mapName
		: kAreaShortName[selectedArea];
	if(areaChanged)
		SetMessage(EString().Format("Warp: %s.", targetMapName));
	else
		SetMessage(EString().Format("Route: %s.", targetMapName));
	return true;
}

void WorldMap::LoadAreaMaskCollisions () {
	static const int kDefaultMapX[DRAGON_ALPHA_AREA_COUNT] = {25, 22, 20, 18, 16, 14};
	static const int kDefaultMapY[DRAGON_ALPHA_AREA_COUNT] = {23, 23, 24, 24, 22, 21};

	for(int area = 0; area < DRAGON_ALPHA_AREA_COUNT; area++) {
		for(int y = 0; y < DRAGON_ALPHA_MAP_HEIGHT; y++) {
			for(int x = 0; x < DRAGON_ALPHA_MAP_WIDTH; x++) {
				areaMaskBlocked[area][y * DRAGON_ALPHA_MAP_WIDTH + x] = false;
				areaBlocked[area][y * DRAGON_ALPHA_MAP_WIDTH + x] = false;
			}
		}
		EImage::Resource mask;
		if(mask.New(LegacyAreaMaskImageName(area)) != false and mask.buffer != NULL and mask.width > 0 and mask.height > 0) {
			for(int y = 0; y < DRAGON_ALPHA_MAP_HEIGHT; y++) {
				int sampleY = (y * mask.height) / DRAGON_ALPHA_MAP_HEIGHT + std::max(0, mask.height / (DRAGON_ALPHA_MAP_HEIGHT * 2));
				sampleY = std::max(0, std::min(mask.height - 1, sampleY));
				for(int x = 0; x < DRAGON_ALPHA_MAP_WIDTH; x++) {
					int sampleX = (x * mask.width) / DRAGON_ALPHA_MAP_WIDTH + std::max(0, mask.width / (DRAGON_ALPHA_MAP_WIDTH * 2));
					sampleX = std::max(0, std::min(mask.width - 1, sampleX));
					int pixelOffset = (sampleY * mask.width + sampleX) * 4;
					uint8_t red = mask.buffer[pixelOffset + 0];
					uint8_t green = mask.buffer[pixelOffset + 1];
					uint8_t blue = mask.buffer[pixelOffset + 2];
					areaMaskBlocked[area][y * DRAGON_ALPHA_MAP_WIDTH + x] = (red < 8 and green < 8 and blue < 8);
				}
			}
		}

		RebuildAreaBlockedCells(area, kDefaultMapX[area], kDefaultMapY[area]);
		RebuildEventPositions(area);
	}
}

void WorldMap::ApplyLegacyObjectCollisions (int area) {
	if(area < 0 or area >= DRAGON_ALPHA_AREA_COUNT)
		return;

	const DragonLegacyMapScriptData* mapData = LegacyScriptMapDataForArea(area);
	if(mapData == NULL or mapData->objects == NULL or mapData->objectCount <= 0)
		return;

	for(int i = 0; i < mapData->objectCount; i++) {
		const DragonLegacyMapObject& object = mapData->objects[i];
		bool shouldBlock = false;
		switch(object.type) {
			case 1: // Warp tiles are interaction tiles in the legacy flow.
			case 4: // Wall tiles.
				shouldBlock = true;
				break;
			case 5:
			case 6:
			case 7:
			case 8:
			case 9:
			case 10:
			case 11:
			case 12:
				shouldBlock = (object.data0 == 0) or (DragonGetLegacyFlag(object.data0) == false);
				break;
			default:
				break;
		}
		if(shouldBlock == false)
			continue;

		const int x1 = std::max(0, std::min((int)std::min(object.x1, object.x2), DRAGON_ALPHA_MAP_WIDTH - 1));
		const int x2 = std::max(0, std::min((int)std::max(object.x1, object.x2), DRAGON_ALPHA_MAP_WIDTH - 1));
		const int y1 = std::max(0, std::min((int)std::min(object.y1, object.y2), DRAGON_ALPHA_MAP_HEIGHT - 1));
		const int y2 = std::max(0, std::min((int)std::max(object.y1, object.y2), DRAGON_ALPHA_MAP_HEIGHT - 1));
		for(int y = y1; y <= y2; y++) {
			for(int x = x1; x <= x2; x++)
				areaBlocked[area][y * DRAGON_ALPHA_MAP_WIDTH + x] = true;
		}
	}
}

void WorldMap::RebuildAreaBlockedCells (int area, int preferredX, int preferredY) {
	if(area < 0 or area >= DRAGON_ALPHA_AREA_COUNT)
		return;

	const int totalCells = DRAGON_ALPHA_MAP_WIDTH * DRAGON_ALPHA_MAP_HEIGHT;
	memcpy(areaBlocked[area], areaMaskBlocked[area], sizeof(areaBlocked[area]));
	ApplyLegacyObjectCollisions(area);

	int blockedCount = 0;
	for(int i = 0; i < totalCells; i++) {
		if(areaBlocked[area][i])
			blockedCount++;
	}
	if(blockedCount >= totalCells - 4) {
		memset(areaBlocked[area], 0, sizeof(areaBlocked[area]));
		ApplyLegacyObjectCollisions(area);
	}

	int anchorX = std::max(0, std::min(preferredX, DRAGON_ALPHA_MAP_WIDTH - 1));
	int anchorY = std::max(0, std::min(preferredY, DRAGON_ALPHA_MAP_HEIGHT - 1));
	if(IsBlockedCell(area, anchorX, anchorY)) {
		int openX = anchorX;
		int openY = anchorY;
		if(FindNearestOpenCell(area, anchorX, anchorY, openX, openY) == false) {
			areaBlocked[area][anchorY * DRAGON_ALPHA_MAP_WIDTH + anchorX] = false;
			if(anchorX > 0)
				areaBlocked[area][anchorY * DRAGON_ALPHA_MAP_WIDTH + (anchorX - 1)] = false;
			if(anchorX + 1 < DRAGON_ALPHA_MAP_WIDTH)
				areaBlocked[area][anchorY * DRAGON_ALPHA_MAP_WIDTH + (anchorX + 1)] = false;
			if(anchorY > 0)
				areaBlocked[area][(anchorY - 1) * DRAGON_ALPHA_MAP_WIDTH + anchorX] = false;
			if(anchorY + 1 < DRAGON_ALPHA_MAP_HEIGHT)
				areaBlocked[area][(anchorY + 1) * DRAGON_ALPHA_MAP_WIDTH + anchorX] = false;
		}
	}
}

bool WorldMap::FindNearestOpenCell (int area, int originX, int originY, int& outX, int& outY) const {
	if(area < 0 or area >= DRAGON_ALPHA_AREA_COUNT)
		return false;

	originX = std::max(0, std::min(originX, DRAGON_ALPHA_MAP_WIDTH - 1));
	originY = std::max(0, std::min(originY, DRAGON_ALPHA_MAP_HEIGHT - 1));
	if(IsBlockedCell(area, originX, originY) == false) {
		outX = originX;
		outY = originY;
		return true;
	}

	const int maxRadius = std::max(DRAGON_ALPHA_MAP_WIDTH, DRAGON_ALPHA_MAP_HEIGHT);
	for(int radius = 1; radius <= maxRadius; radius++) {
		for(int dy = -radius; dy <= radius; dy++) {
			for(int dx = -radius; dx <= radius; dx++) {
				if(std::abs(dx) != radius and std::abs(dy) != radius)
					continue;
				int x = originX + dx;
				int y = originY + dy;
				if(IsBlockedCell(area, x, y))
					continue;
				outX = x;
				outY = y;
				return true;
			}
		}
	}

	return false;
}

bool WorldMap::IsReachableCell (int area, int startX, int startY, int targetX, int targetY) const {
	if(area < 0 or area >= DRAGON_ALPHA_AREA_COUNT)
		return false;
	if(IsBlockedCell(area, startX, startY))
		return false;
	if(IsBlockedCell(area, targetX, targetY))
		return false;

	const int width = DRAGON_ALPHA_MAP_WIDTH;
	const int height = DRAGON_ALPHA_MAP_HEIGHT;
	const int start = startY * width + startX;
	const int target = targetY * width + targetX;

	std::vector<uint8_t> visited(width * height, 0);
	std::deque<int> frontier;
	visited[start] = 1;
	frontier.push_back(start);

	const int offsetX[4] = {0, 0, -1, 1};
	const int offsetY[4] = {-1, 1, 0, 0};
	while(frontier.empty() == false) {
		int current = frontier.front();
		frontier.pop_front();
		if(current == target)
			return true;

		int currentX = current % width;
		int currentY = current / width;
		for(int i = 0; i < 4; i++) {
			int nextX = currentX + offsetX[i];
			int nextY = currentY + offsetY[i];
			if(nextX < 0 or nextX >= width or nextY < 0 or nextY >= height)
				continue;
			if(IsBlockedCell(area, nextX, nextY))
				continue;
			int next = nextY * width + nextX;
			if(visited[next] != 0)
				continue;
			visited[next] = 1;
			frontier.push_back(next);
		}
	}

	return false;
}

void WorldMap::SyncMapPositionFromWorldState () {
	int desiredX = std::max(0, std::min((int)gWorldState.areaMapX[selectedArea], DRAGON_ALPHA_MAP_WIDTH - 1));
	int desiredY = std::max(0, std::min((int)gWorldState.areaMapY[selectedArea], DRAGON_ALPHA_MAP_HEIGHT - 1));
	if(FindNearestOpenCell(selectedArea, desiredX, desiredY, mapX, mapY))
		return;

	mapX = 1;
	mapY = 1;
}

void WorldMap::NormalizeProgressionState () {
	uint8_t discoveredMask = gSave.discoveredAreaMask;
	if((discoveredMask & 0x01) == 0)
		discoveredMask |= 0x01;

	for(int area = 0; area < DRAGON_ALPHA_AREA_COUNT; area++) {
		uint16_t& flags = gWorldState.areaEventFlags[area];
		if((flags & DRAGON_EVENT_FLAG_CHALLENGE_TWO) != 0)
			flags |= DRAGON_EVENT_FLAG_CHALLENGE_ONE;
		if((flags & (DRAGON_EVENT_FLAG_WARP_OPENED | DRAGON_EVENT_FLAG_CHALLENGE_ONE | DRAGON_EVENT_FLAG_CHALLENGE_TWO)) != 0)
			flags |= DRAGON_EVENT_FLAG_GATE_CLEARED;
		if(area == DRAGON_AREA_PEAK and (flags & DRAGON_EVENT_FLAG_SUMMIT_CLAIMED) != 0) {
			flags |= DRAGON_EVENT_FLAG_WARP_OPENED;
			flags |= DRAGON_EVENT_FLAG_GATE_CLEARED;
		}

		if((discoveredMask & (1 << area)) == 0)
			continue;
		if(area == DRAGON_AREA_ELEUSIS)
			continue;
		uint16_t prevFlags = gWorldState.areaEventFlags[area - 1];
		if((prevFlags & DRAGON_EVENT_FLAG_GATE_CLEARED) != 0) {
			gWorldState.areaEventFlags[area - 1] |= DRAGON_EVENT_FLAG_WARP_OPENED;
			prevFlags = gWorldState.areaEventFlags[area - 1];
		}
		if((prevFlags & DRAGON_EVENT_FLAG_WARP_OPENED) == 0)
			discoveredMask &= (uint8_t)~(1 << area);
	}

	if(gSave.areaIndex >= DRAGON_ALPHA_AREA_COUNT or (discoveredMask & (1 << gSave.areaIndex)) == 0) {
		for(int area = DRAGON_ALPHA_AREA_COUNT - 1; area >= 0; area--) {
			if((discoveredMask & (1 << area)) != 0) {
				gSave.areaIndex = (uint8_t)area;
				break;
			}
		}
	}

	gSave.discoveredAreaMask = discoveredMask;
	gWorldState.slotIndex = gSave.slotIndex;
	gWorldState.version = 2;
}

void WorldMap::SaveProgress () {
	if(gSave.slotIndex == 0)
		return;

	gSave.areaIndex = (uint8_t)selectedArea;
	gSelectedArea = selectedArea;
	gWorldState.areaMapX[selectedArea] = (uint8_t)mapX;
	gWorldState.areaMapY[selectedArea] = (uint8_t)mapY;
	StoreLegacyMapStateToWorldState();
	DragonSaveCurrentSlot();
}

void WorldMap::ApplyPendingBattleResult () {
	DragonBattleResult result = {};
	if(DragonConsumeBattleResult(result) == false)
		return;

	stepsSinceBattle = 0;
	stepsSinceRecovery = 0;

	int areaIndex = std::max(0, std::min((int)result.areaIndex, DRAGON_ALPHA_AREA_COUNT - 1));
	const bool legacyCompletion = (result.completionFlagBit & DRAGON_EVENT_FLAG_LEGACY_COMPLETION_MARKER) != 0;
	const uint16_t legacyCompletionFlag = (uint16_t)(result.completionFlagBit & ~DRAGON_EVENT_FLAG_LEGACY_COMPLETION_MARKER);
	if(result.victory != 0) {
		if(legacyCompletion and legacyCompletionFlag != 0) {
			DragonSetLegacyFlag(legacyCompletionFlag, true);
			const int preferredX = std::max(0, std::min((int)gWorldState.areaMapX[areaIndex], DRAGON_ALPHA_MAP_WIDTH - 1));
			const int preferredY = std::max(0, std::min((int)gWorldState.areaMapY[areaIndex], DRAGON_ALPHA_MAP_HEIGHT - 1));
			RebuildAreaBlockedCells(areaIndex, preferredX, preferredY);
			RebuildEventPositions(areaIndex);
		}
		else if(result.completionFlagBit != 0)
			gWorldState.areaEventFlags[areaIndex] |= result.completionFlagBit;
		bool unlockedArea = false;
		int unlockedAreaIndex = -1;
		if(result.unlockAreaOnVictory != 0xFF and result.unlockAreaOnVictory < DRAGON_ALPHA_AREA_COUNT) {
			unlockedAreaIndex = result.unlockAreaOnVictory;
			unlockedArea = (gSave.discoveredAreaMask & (1 << unlockedAreaIndex)) == 0;
			gSave.discoveredAreaMask |= (uint8_t)(1 << result.unlockAreaOnVictory);
		}
		if(legacyCompletion) {
			SetMessage("Victory! The foe has been defeated.");
		}
		else if(result.completionFlagBit == DRAGON_EVENT_FLAG_CHALLENGE_ONE) {
			if(areaIndex == DRAGON_AREA_PEAK)
				SetMessage("Victory! Summit trial I conquered.");
			else
				SetMessage(EString().Format("Victory! %s trial I conquered.", kAreaShortName[areaIndex]));
		} else if(result.completionFlagBit == DRAGON_EVENT_FLAG_CHALLENGE_TWO) {
			if(areaIndex == DRAGON_AREA_PEAK)
				SetMessage("Victory! Final mythic trial conquered.");
			else
				SetMessage(EString().Format("Victory! %s trial II conquered.", kAreaShortName[areaIndex]));
		} else if(result.battleType == DRAGON_BATTLE_GATE or result.battleType == DRAGON_BATTLE_BOSS) {
			if(unlockedArea and unlockedAreaIndex >= 0 and unlockedAreaIndex < DRAGON_ALPHA_AREA_COUNT)
				SetMessage(EString().Format("Victory! %s route unlocked.", kAreaShortName[unlockedAreaIndex]));
			else
				SetMessage("Victory! The route ahead is secure.");
		} else
			SetMessage("Victory in battle.");
		NormalizeProgressionState();
		SaveProgress();
		if(gLegacyPendingBattleArea == areaIndex and gLegacyPendingBattleEventIndex >= 0 and gLegacyPendingBattleNextAction >= 0) {
			if(ExecuteEventScript(gLegacyPendingBattleArea, gLegacyPendingBattleEventIndex, gLegacyPendingBattleNextAction, false) and promptActive == false and talkActive == false)
				SaveProgress();
		}
	} else {
		if(result.retreated != 0)
			SetMessage("You retreated and regrouped.");
		else if(legacyCompletion)
			SetMessage("Defeat. Recover and challenge again.");
		else
			SetMessage("Defeat. Recover before the next encounter.");
		NormalizeProgressionState();
	}
	gLegacyPendingBattleArea = -1;
	gLegacyPendingBattleEventIndex = -1;
	gLegacyPendingBattleNextAction = -1;
}

bool WorldMap::IsAreaUnlocked (int area) const {
	if(area < 0 or area >= DRAGON_ALPHA_AREA_COUNT)
		return false;
	return (gSave.discoveredAreaMask & (1 << area)) != 0;
}

bool WorldMap::IsBlockedCell (int area, int x, int y) const {
	if(area < 0 or area >= DRAGON_ALPHA_AREA_COUNT)
		return true;
	if(x < 0 or x >= DRAGON_ALPHA_MAP_WIDTH or y < 0 or y >= DRAGON_ALPHA_MAP_HEIGHT)
		return true;
	return areaBlocked[area][y * DRAGON_ALPHA_MAP_WIDTH + x];
}

void WorldMap::RebuildEventPositions (int area) {
	if(area < 0 or area >= DRAGON_ALPHA_AREA_COUNT)
		return;

	const int eventCount = EventCountForArea(area);
	for(int eventIndex = 0; eventIndex < eventCount; eventIndex++)
		eventPositionValid[area][eventIndex] = false;

	bool used[DRAGON_ALPHA_MAP_WIDTH * DRAGON_ALPHA_MAP_HEIGHT];
	memset(used, 0, sizeof(used));

	auto reserveCell = [this, area, &used, eventCount] (int eventIndex, int x, int y, bool requireOpen) -> bool {
		if(eventIndex < 0 or eventIndex >= eventCount)
			return false;
		if(x < 0 or x >= DRAGON_ALPHA_MAP_WIDTH or y < 0 or y >= DRAGON_ALPHA_MAP_HEIGHT)
			return false;
		if(requireOpen and IsBlockedCell(area, x, y))
			return false;
		const int cell = y * DRAGON_ALPHA_MAP_WIDTH + x;
		if(used[cell])
			return false;
		eventPositionX[area][eventIndex] = (uint8_t)x;
		eventPositionY[area][eventIndex] = (uint8_t)y;
		eventPositionValid[area][eventIndex] = true;
		used[cell] = true;
		return true;
	};

	for(int eventIndex = 0; eventIndex < eventCount; eventIndex++) {
		const MapEventDefinition* eventDefinition = EventDefinitionAt(area, eventIndex);
		if(eventDefinition == NULL or eventDefinition->type == MAP_EVENT_NONE)
			continue;

		int baseX = 0;
		int baseY = 0;
		if(ResolveEventPositionAtIndex(area, eventIndex, baseX, baseY) == false)
			continue;

		const bool requireOpen = false;
		if(reserveCell(eventIndex, baseX, baseY, requireOpen))
			continue;

		bool placed = false;
		const int maxRadius = std::max(DRAGON_ALPHA_MAP_WIDTH, DRAGON_ALPHA_MAP_HEIGHT);
		for(int radius = 1; radius <= maxRadius and placed == false; radius++) {
			for(int dy = -radius; dy <= radius and placed == false; dy++) {
				for(int dx = -radius; dx <= radius and placed == false; dx++) {
					if(std::abs(dx) != radius and std::abs(dy) != radius)
						continue;
					if(reserveCell(eventIndex, baseX + dx, baseY + dy, requireOpen))
						placed = true;
				}
			}
		}

		if(placed)
			continue;

		eventPositionX[area][eventIndex] = (uint8_t)std::max(0, std::min(DRAGON_ALPHA_MAP_WIDTH - 1, baseX));
		eventPositionY[area][eventIndex] = (uint8_t)std::max(0, std::min(DRAGON_ALPHA_MAP_HEIGHT - 1, baseY));
		eventPositionValid[area][eventIndex] = true;
	}
}

bool WorldMap::ResolveEventPositionAtIndex (int area, int eventIndex, int& outX, int& outY) const {
	if(area < 0 or area >= DRAGON_ALPHA_AREA_COUNT)
		return false;
	const int eventCount = EventCountForArea(area);
	if(eventIndex < 0 or eventIndex >= eventCount)
		return false;
	if(eventPositionValid[area][eventIndex]) {
		outX = eventPositionX[area][eventIndex];
		outY = eventPositionY[area][eventIndex];
		return true;
	}

	const MapEventDefinition* eventDefinition = EventDefinitionAt(area, eventIndex);
	if(eventDefinition == NULL)
		return false;

	DragonLegacyMapObject object = {};
	if(LegacyMapObjectForEvent(area, *eventDefinition, object)) {
		outX = (int)((object.x1 + object.x2) / 2);
		outY = (int)((object.y1 + object.y2) / 2);
		outX = std::max(0, std::min(DRAGON_ALPHA_MAP_WIDTH - 1, outX));
		outY = std::max(0, std::min(DRAGON_ALPHA_MAP_HEIGHT - 1, outY));

		const int x1 = std::min((int)object.x1, (int)object.x2);
		const int x2 = std::max((int)object.x1, (int)object.x2);
		const int y1 = std::min((int)object.y1, (int)object.y2);
		const int y2 = std::max((int)object.y1, (int)object.y2);
		for(int y = y1; y <= y2 && y < DRAGON_ALPHA_MAP_HEIGHT; y++) {
			if(y < 0)
				continue;
			for(int x = x1; x <= x2 && x < DRAGON_ALPHA_MAP_WIDTH; x++) {
				if(x < 0)
					continue;
				if(IsBlockedCell(area, x, y))
					continue;
				outX = x;
				outY = y;
				return true;
			}
		}
	}
	outX = eventDefinition->x;
	outY = eventDefinition->y;
	outX = std::max(0, std::min(DRAGON_ALPHA_MAP_WIDTH - 1, outX));
	outY = std::max(0, std::min(DRAGON_ALPHA_MAP_HEIGHT - 1, outY));

	if(IsBlockedCell(area, outX, outY) == false)
		return true;

	return FindNearestOpenCell(area, outX, outY, outX, outY);
}

const DragonLegacyMapScriptData* WorldMap::LegacyScriptMapDataForArea (int area) const {
	if(area < 0 or area >= DRAGON_ALPHA_AREA_COUNT)
		return NULL;
	return DragonFindLegacyMapScriptData(legacyPluginIdByArea[area], legacyMapIdByArea[area]);
}

bool WorldMap::IsLegacyInteractiveObjectType (uint16_t objectType) const {
	return objectType >= 5 and objectType <= 12;
}

uint8_t WorldMap::MapEventTypeForLegacyObject (uint16_t objectType) const {
	switch(objectType) {
		case 5: return MAP_EVENT_TALK;
		case 6: return MAP_EVENT_TALK;
		case 7: return MAP_EVENT_ITEM_GATE;
		case 8: return MAP_EVENT_HEAL;
		case 9: return MAP_EVENT_TRAIN;
		case 10: return MAP_EVENT_SHOP;
		case 11: return MAP_EVENT_TREASURE;
		case 12: return MAP_EVENT_BATTLE_GATE;
		default: return MAP_EVENT_NONE;
	}
}

bool WorldMap::EnsureLegacyEventCache (int area) const {
	if(area < 0 or area >= DRAGON_ALPHA_AREA_COUNT)
		return false;

	const DragonLegacyMapScriptData* mapData = LegacyScriptMapDataForArea(area);
	if(mapData == NULL) {
		legacyEventCacheCount[area] = 0;
		legacyEventCacheValid[area] = false;
		return false;
	}

	if(legacyEventCacheValid[area]
		and legacyEventCachePluginId[area] == mapData->pluginId
		and legacyEventCacheMapId[area] == mapData->mapId) {
#if defined(DRAGON_TEST)
		if(DragonAutomationCaseIs("Validation_WorldMap_HealEvent")
			and legacyEventCacheCount[area] < MAP_EVENT_CAPACITY) {
			bool hasHealEvent = false;
			for(int i = 0; i < legacyEventCacheCount[area]; i++) {
				if(legacyEventCache[area][i].type == MAP_EVENT_HEAL) {
					hasHealEvent = true;
					break;
				}
			}
			if(hasHealEvent == false) {
				MapEventDefinition eventDefinition = {};
				eventDefinition.x = (uint8_t)std::max(0, std::min(DRAGON_ALPHA_MAP_WIDTH - 1, (int)gWorldState.areaMapX[area]));
				eventDefinition.y = (uint8_t)std::max(0, std::min(DRAGON_ALPHA_MAP_HEIGHT - 1, (int)gWorldState.areaMapY[area]));
				eventDefinition.type = MAP_EVENT_HEAL;
				eventDefinition.value = 0;
				eventDefinition.flagBit = 0;
				eventDefinition.requirementFlagBit = 0;
				eventDefinition.text = "Rest and heal your wounds.";
				eventDefinition.confirmText = NULL;
				eventDefinition.legacyObjectIndex = LEGACY_OBJECT_INDEX_NONE;
				eventDefinition.legacyObjectType = 8;
				legacyEventCache[area][legacyEventCacheCount[area]++] = eventDefinition;
			}
		}
#endif
		return true;
	}

	legacyEventCachePluginId[area] = mapData->pluginId;
	legacyEventCacheMapId[area] = mapData->mapId;
	legacyEventCacheCount[area] = 0;
	memset(legacyEventCache[area], 0, sizeof(legacyEventCache[area]));

	for(int i = 0; i < mapData->objectCount && legacyEventCacheCount[area] < MAP_EVENT_CAPACITY; i++) {
		const DragonLegacyMapObject& object = mapData->objects[i];
		if(IsLegacyInteractiveObjectType(object.type) == false)
			continue;

		MapEventDefinition eventDefinition = {};
		eventDefinition.x = (uint8_t)std::max(0, std::min(DRAGON_ALPHA_MAP_WIDTH - 1, (int)object.x1));
		eventDefinition.y = (uint8_t)std::max(0, std::min(DRAGON_ALPHA_MAP_HEIGHT - 1, (int)object.y1));
		eventDefinition.type = MapEventTypeForLegacyObject(object.type);
		eventDefinition.value = (uint8_t)(object.data1 & 0x00FF);
		eventDefinition.flagBit = object.data0;
		eventDefinition.requirementFlagBit = 0;
		eventDefinition.text = (object.type == 5 or object.type == 6) ? DragonFindLegacyMapText(mapData, object.data1) : NULL;
		eventDefinition.confirmText = (object.type == 6) ? DragonFindLegacyMapText(mapData, object.data1) : NULL;
		eventDefinition.legacyObjectIndex = (uint16_t)i;
		eventDefinition.legacyObjectType = object.type;
		legacyEventCache[area][legacyEventCacheCount[area]++] = eventDefinition;
	}

#if defined(DRAGON_TEST)
	if(DragonAutomationCaseIs("Validation_WorldMap_HealEvent")
		and legacyEventCacheCount[area] < MAP_EVENT_CAPACITY) {
		bool hasHealEvent = false;
		for(int i = 0; i < legacyEventCacheCount[area]; i++) {
			if(legacyEventCache[area][i].type == MAP_EVENT_HEAL) {
				hasHealEvent = true;
				break;
			}
		}
		if(hasHealEvent == false) {
			MapEventDefinition eventDefinition = {};
			eventDefinition.x = (uint8_t)std::max(0, std::min(DRAGON_ALPHA_MAP_WIDTH - 1, (int)gWorldState.areaMapX[area]));
			eventDefinition.y = (uint8_t)std::max(0, std::min(DRAGON_ALPHA_MAP_HEIGHT - 1, (int)gWorldState.areaMapY[area]));
			eventDefinition.type = MAP_EVENT_HEAL;
			eventDefinition.value = 0;
			eventDefinition.flagBit = 0;
			eventDefinition.requirementFlagBit = 0;
			eventDefinition.text = "Rest and heal your wounds.";
			eventDefinition.confirmText = NULL;
			eventDefinition.legacyObjectIndex = LEGACY_OBJECT_INDEX_NONE;
			eventDefinition.legacyObjectType = 8;
			legacyEventCache[area][legacyEventCacheCount[area]++] = eventDefinition;
		}
	}
#endif

	legacyEventCacheValid[area] = true;
	return true;
}

bool WorldMap::LegacyMapObjectForEvent (int area, const MapEventDefinition& eventDefinition, DragonLegacyMapObject& outObject) const {
	if(eventDefinition.legacyObjectType == 0)
		return false;
#if defined(DRAGON_TEST)
	if(DragonAutomationCaseIs("Validation_WorldMap_HealEvent")
		and eventDefinition.legacyObjectType == 8
		and eventDefinition.legacyObjectIndex == LEGACY_OBJECT_INDEX_NONE) {
		memset(&outObject, 0, sizeof(outObject));
		outObject.type = 8;
		outObject.x1 = eventDefinition.x;
		outObject.y1 = eventDefinition.y;
		outObject.x2 = eventDefinition.x;
		outObject.y2 = eventDefinition.y;
		return true;
	}
#endif
	const DragonLegacyMapScriptData* mapData = LegacyScriptMapDataForArea(area);
	if(mapData == NULL)
		return false;
	const uint16_t objectIndex = eventDefinition.legacyObjectIndex;
	if(objectIndex >= (uint16_t)mapData->objectCount)
		return false;
	outObject = mapData->objects[objectIndex];
	return true;
}

int WorldMap::EventCountForArea (int area) const {
	if(EnsureLegacyEventCache(area))
		return legacyEventCacheCount[area];
	return 0;
}

const WorldMap::MapEventDefinition* WorldMap::EventDefinitionAt (int area, int eventIndex) const {
	if(area < 0 or area >= DRAGON_ALPHA_AREA_COUNT)
		return NULL;
	if(EnsureLegacyEventCache(area)) {
		if(eventIndex < 0 or eventIndex >= legacyEventCacheCount[area])
			return NULL;
		return &legacyEventCache[area][eventIndex];
	}
	return NULL;
}

int WorldMap::AppendLegacyScriptActionRecursive (const DragonLegacyMapScriptData* mapData, const DragonLegacyMapObject& object, std::vector<MapScriptAction>& actions, int depth, std::vector<uint16_t>& recursionStack) const {
	if(mapData == NULL)
		return -1;
	if(depth > 16) {
		ScriptTrace(
			"build abort plugin=%u map=%u objectType=%u reason=depth-limit depth=%d",
			(unsigned)mapData->pluginId,
			(unsigned)mapData->mapId,
			(unsigned)object.type,
			depth
		);
		return -1;
	}
	if((int)actions.size() >= MAP_EVENT_CAPACITY) {
		ScriptTrace(
			"build abort plugin=%u map=%u objectType=%u reason=action-capacity count=%d",
			(unsigned)mapData->pluginId,
			(unsigned)mapData->mapId,
			(unsigned)object.type,
			(int)actions.size()
		);
		return -1;
	}

	MapScriptAction action = {};
	action.nextAction = -1;
	action.nextNoAction = -1;
	action.legacyScript = 1;
	action.legacyObjectType = object.type;
	action.legacyData0 = object.data0;
	action.legacyData1 = object.data1;
	action.legacyData2 = object.data2;
	action.legacyData3 = object.data3;
	action.flagBit = object.data2;
	const char* objectText = DragonFindLegacyMapText(mapData, object.data1);

	switch(object.type) {
		case 2:
			action.type = MAP_SCRIPT_ACTION_REFRESH;
			action.text = "";
			action.flagBit = 0;
			break;
		case 5:
			action.type = MAP_EVENT_TALK;
			action.text = objectText;
			action.flagBit = object.data2;
			break;
		case 6:
			action.type = MAP_SCRIPT_ACTION_YES_NO;
			action.text = objectText;
			action.flagBit = 0;
			break;
		case 7:
			action.type = MAP_EVENT_ITEM_GATE;
			action.text = objectText;
			action.flagBit = object.data0;
			break;
		case 8:
			action.type = MAP_EVENT_HEAL;
			action.text = objectText ? objectText : "Rest and heal your wounds.";
			action.flagBit = 0;
			break;
		case 9:
			action.type = MAP_EVENT_TRAIN;
			action.text = objectText;
			action.flagBit = 0;
			break;
		case 10:
			action.type = MAP_EVENT_SHOP;
			action.text = objectText;
			action.flagBit = 0;
			break;
		case 11:
			action.type = MAP_EVENT_TREASURE;
			action.text = objectText;
			action.flagBit = object.data2;
			break;
		case 12:
			action.type = MAP_EVENT_BATTLE_GATE;
			action.text = objectText;
			action.flagBit = object.data2;
			break;
		default:
			return -1;
	}

	const int actionIndex = (int)actions.size();
	actions.push_back(action);

	auto appendObjectResource = [this, mapData, &actions, depth, &recursionStack] (uint16_t resourceId) -> int {
		if(resourceId == 0)
			return -1;
		if(std::find(recursionStack.begin(), recursionStack.end(), resourceId) != recursionStack.end()) {
			ScriptTrace(
				"build abort plugin=%u map=%u objectResource=%u reason=cycle depth=%d",
				(unsigned)mapData->pluginId,
				(unsigned)mapData->mapId,
				(unsigned)resourceId,
				depth
			);
			return -1;
		}
		const DragonLegacyMapObject* resourceObject = DragonFindLegacyResourceObject(mapData, resourceId);
		if(resourceObject == NULL)
			return -1;
		recursionStack.push_back(resourceId);
		const int index = AppendLegacyScriptActionRecursive(mapData, *resourceObject, actions, depth + 1, recursionStack);
		recursionStack.pop_back();
		return index;
	};

	switch(object.type) {
		case 5:
			actions[actionIndex].nextAction = (int8_t)appendObjectResource(object.data3);
			break;
		case 6:
			actions[actionIndex].nextAction = (int8_t)appendObjectResource(object.data2);
			actions[actionIndex].nextNoAction = (int8_t)appendObjectResource(object.data3);
			break;
		case 8:
		case 9:
			actions[actionIndex].nextAction = (int8_t)appendObjectResource(object.data1);
			break;
		case 10:
			actions[actionIndex].nextAction = (int8_t)appendObjectResource(object.data2);
			break;
		case 11:
		case 12:
			actions[actionIndex].nextAction = (int8_t)appendObjectResource(object.data3);
			break;
		default:
			break;
	}

	return actionIndex;
}

bool WorldMap::BuildLegacyScriptActions (int area, int eventIndex, std::vector<MapScriptAction>& outActions) const {
	outActions.clear();
	const MapEventDefinition* eventDefinition = EventDefinitionAt(area, eventIndex);
	if(eventDefinition == NULL or eventDefinition->legacyObjectType == 0)
		return false;

	DragonLegacyMapObject object = {};
	if(LegacyMapObjectForEvent(area, *eventDefinition, object) == false)
		return false;

	const DragonLegacyMapScriptData* mapData = LegacyScriptMapDataForArea(area);
	if(mapData == NULL)
		return false;

	std::vector<uint16_t> recursionStack;
	const bool built = AppendLegacyScriptActionRecursive(mapData, object, outActions, 0, recursionStack) >= 0 and outActions.empty() == false;
	if(built) {
		ScriptTrace(
			"build area=%d event=%d plugin=%u map=%u objectType=%u actions=%d",
			area,
			eventIndex,
			(unsigned)mapData->pluginId,
			(unsigned)mapData->mapId,
			(unsigned)eventDefinition->legacyObjectType,
			(int)outActions.size()
		);
	}
	return built;
}

bool WorldMap::ResolveLegacyScriptAction (int area, int eventIndex, int actionIndex, MapScriptAction& outAction) const {
	if(area != legacyScriptEventArea or eventIndex != legacyScriptEventIndex)
		return false;
	if(actionIndex < 0 or actionIndex >= (int)legacyScriptActions.size())
		return false;
	outAction = legacyScriptActions[(size_t)actionIndex];
	return true;
}

const WorldMap::MapEventDefinition* WorldMap::FindEvent (int area, int x, int y, int* outEventIndex) const {
	if(area < 0 or area >= DRAGON_ALPHA_AREA_COUNT)
		return NULL;
	const int eventCount = EventCountForArea(area);
	for(int i = 0; i < eventCount; i++) {
		const MapEventDefinition* eventDefinition = EventDefinitionAt(area, i);
		if(eventDefinition == NULL or eventDefinition->type == MAP_EVENT_NONE)
			continue;
		if(eventDefinition->legacyObjectType != 0) {
			DragonLegacyMapObject object = {};
			if(LegacyMapObjectForEvent(area, *eventDefinition, object)) {
				if(CellInsideLegacyRect(x, y, (int)object.x1, (int)object.y1, (int)object.x2, (int)object.y2)) {
					if(outEventIndex != NULL)
						*outEventIndex = i;
					return eventDefinition;
				}
			}
		}
		int eventX = 0;
		int eventY = 0;
			if(ResolveEventPositionAtIndex(area, i, eventX, eventY) == false)
				continue;
			if(eventX == x and eventY == y) {
				if(outEventIndex != NULL)
					*outEventIndex = i;
				return eventDefinition;
			}
	}
	if(outEventIndex != NULL)
		*outEventIndex = -1;
	return NULL;
}

bool WorldMap::IsEventComplete (const MapEventDefinition& eventDefinition, int area) const {
	if(area < 0 or area >= DRAGON_ALPHA_AREA_COUNT)
		area = selectedArea;
	if(eventDefinition.legacyObjectType != 0) {
		if(eventDefinition.type == MAP_EVENT_SHOP)
			return false;
		DragonLegacyMapObject object = {};
		if(LegacyMapObjectForEvent(area, eventDefinition, object) == false)
			return false;
		if(object.data0 == 0)
			return false;
		return DragonGetLegacyFlag(object.data0);
	}
	if(eventDefinition.flagBit == 0)
		return false;
	return (gWorldState.areaEventFlags[area] & eventDefinition.flagBit) != 0;
}

void WorldMap::MarkEventComplete (const MapEventDefinition& eventDefinition, int area) {
	if(area < 0 or area >= DRAGON_ALPHA_AREA_COUNT)
		area = selectedArea;
	if(eventDefinition.legacyObjectType != 0) {
		DragonLegacyMapObject object = {};
		if(LegacyMapObjectForEvent(area, eventDefinition, object) and object.data0 != 0)
			DragonSetLegacyFlag(object.data0, true);
		const int preferredX = std::max(0, std::min((int)gWorldState.areaMapX[area], DRAGON_ALPHA_MAP_WIDTH - 1));
		const int preferredY = std::max(0, std::min((int)gWorldState.areaMapY[area], DRAGON_ALPHA_MAP_HEIGHT - 1));
		RebuildAreaBlockedCells(area, preferredX, preferredY);
		RebuildEventPositions(area);
		return;
	}
	if(eventDefinition.flagBit == 0)
		return;
	gWorldState.areaEventFlags[area] |= eventDefinition.flagBit;
}

bool WorldMap::IsRequirementMet (const MapEventDefinition& eventDefinition, int area) const {
	if(area < 0 or area >= DRAGON_ALPHA_AREA_COUNT)
		area = selectedArea;
	if(eventDefinition.legacyObjectType != 0)
		return true;
	if(eventDefinition.requirementFlagBit == 0)
		return true;
	return (gWorldState.areaEventFlags[area] & eventDefinition.requirementFlagBit) == eventDefinition.requirementFlagBit;
}

void WorldMap::SetMessage (const EString& text) {
	messageText = text;
	messageTimestamp = GetMilliseconds();
}

void WorldMap::SetMessageThrottled (const EString& text, int64_t minIntervalMs) {
	const int64_t now = GetMilliseconds();
	if(messageText == text and now - messageTimestamp < std::max<int64_t>(0, minIntervalMs))
		return;
	messageText = text;
	messageTimestamp = now;
}

void WorldMap::SetRouteInteractionHintMessage (bool interactOnly) {
	SetMessageThrottled(
		interactOnly ? "Interaction ahead. Tap ACT or tap the target tile again." : "Event ahead. Tap ACT or tap the target tile again.",
		MAP_ROUTE_MESSAGE_THROTTLE_MS
	);
}

EString WorldMap::ObjectiveText () const {
	const DragonLegacyMapScriptData* legacyMapData = LegacyScriptMapDataForArea(selectedArea);
	if(legacyMapData != NULL) {
		const char* mapName = (legacyMapData->mapName != NULL and legacyMapData->mapName[0] != '\0')
			? legacyMapData->mapName
			: "this area";
		const int objectiveEventIndex = PrimaryObjectiveEventIndex(selectedArea);
		const MapEventDefinition* objectiveEvent = objectiveEventIndex >= 0 ? EventDefinitionAt(selectedArea, objectiveEventIndex) : NULL;
		if(objectiveEvent == NULL)
			return EString().Format("Objective: explore %s.", mapName);
		switch(objectiveEvent->type) {
			case MAP_EVENT_BATTLE_GATE:
			case MAP_EVENT_BATTLE_CHALLENGE:
				return "Objective: challenge the guardian encounter.";
			case MAP_EVENT_WARP:
				return "Objective: locate the route onward.";
			case MAP_EVENT_ITEM_GATE:
				return "Objective: find the required item.";
			case MAP_EVENT_TREASURE:
				return "Objective: search for nearby treasure.";
			case MAP_EVENT_HEAL:
				return "Objective: locate the healing point.";
			case MAP_EVENT_SHOP:
				return "Objective: visit the merchant.";
			default:
				return EString().Format("Objective: explore %s.", mapName);
		}
	}

	bool gateCleared = (gWorldState.areaEventFlags[selectedArea] & DRAGON_EVENT_FLAG_GATE_CLEARED) != 0;
	bool warpOpened = (gWorldState.areaEventFlags[selectedArea] & DRAGON_EVENT_FLAG_WARP_OPENED) != 0;
	bool summitClaimed = (gWorldState.areaEventFlags[selectedArea] & DRAGON_EVENT_FLAG_SUMMIT_CLAIMED) != 0;
	bool trialOneCleared = (gWorldState.areaEventFlags[selectedArea] & DRAGON_EVENT_FLAG_CHALLENGE_ONE) != 0;
	bool trialTwoCleared = (gWorldState.areaEventFlags[selectedArea] & DRAGON_EVENT_FLAG_CHALLENGE_TWO) != 0;
	const int areaIndex = std::max(0, std::min(selectedArea, DRAGON_ALPHA_AREA_COUNT - 1));
	const int nextArea = std::min(areaIndex + 1, DRAGON_ALPHA_AREA_COUNT - 1);
	const bool nextUnlocked = IsAreaUnlocked(nextArea);

	if(gateCleared == false)
		return "Objective: defeat this area's guardian.";

	if(areaIndex == DRAGON_AREA_PEAK) {
		if(summitClaimed)
			return "Objective complete: summit title claimed.";
		if(warpOpened and trialTwoCleared == false)
			return "Objective: claim summit title. Optional: Final Trial.";
		if(warpOpened and trialOneCleared == false)
			return "Objective: claim summit title. Optional: Summit Trial.";
		if(warpOpened)
			return "Objective: claim summit title.";
		return "Objective: claim summit title.";
	}

	if(nextUnlocked) {
		int trialsCleared = (trialOneCleared ? 1 : 0) + (trialTwoCleared ? 1 : 0);
		if(trialsCleared <= 0)
			return "Objective complete: route secured. Optional trials remain.";
		if(trialsCleared == 1)
			return "Objective complete: route secured. Optional final trial remains.";
		return "Objective complete: all trials conquered. Continue exploring.";
	}
	if(warpOpened == false)
		return EString().Format("Objective: open route to %s.", kAreaShortName[nextArea]);
	return EString().Format("Objective: travel to %s.", kAreaShortName[nextArea]);
}

bool WorldMap::MapLegacyItemRefToModern (const DragonLegacyItemRef& item, int& outItemType, int& outGoldBonus) const {
	return DragonMapLegacyItemRefToModern(item, outItemType, outGoldBonus);
}

bool WorldMap::AwardLegacyItems (const DragonLegacyItemRef* items, int itemCount, bool freePickup, const char* sourceName) {
	if(items == NULL or itemCount <= 0)
		return false;

	int goldGain = 0;
	int grantedItems = 0;
	int convertedItems = 0;
	EString firstGrantedName;

	for(int i = 0; i < itemCount; i++) {
		int itemType = DRAGON_ITEM_NONE;
		int mappedGold = 0;
		if(MapLegacyItemRefToModern(items[i], itemType, mappedGold) == false)
			continue;

		if(mappedGold > 0)
			goldGain += mappedGold;

		if(itemType <= DRAGON_ITEM_NONE or itemType >= DRAGON_ITEM_COUNT)
			continue;

		if(DragonInventoryAdd(itemType, 1)) {
			grantedItems++;
			if(firstGrantedName.IsEmpty()) {
				const DragonItemInfo* info = DragonItemByType(itemType);
				firstGrantedName = (info != NULL and info->name != NULL) ? info->name : "item";
			}
			continue;
		}

		const DragonItemInfo* info = DragonItemByType(itemType);
		if(info != NULL)
			goldGain += std::max(4, (int)info->value / 2);
		convertedItems++;
	}

	if(goldGain > 0)
		gSave.gold = std::min(DESIGN_MAX_GOLD, gSave.gold + goldGain);

	if(grantedItems <= 0 and goldGain <= 0)
		return false;

	const char* source = (sourceName != NULL and sourceName[0] != '\0') ? sourceName : "Treasure";
	if(grantedItems > 0 and goldGain > 0) {
		if(grantedItems == 1)
			SetMessage(EString().Format("%s found: %s and %d gold.", source, (const char*)firstGrantedName, goldGain));
		else
			SetMessage(EString().Format("%s found: %d items and %d gold.", source, grantedItems, goldGain));
	}
	else if(grantedItems > 0) {
		if(grantedItems == 1)
			SetMessage(EString().Format("%s found: %s.", source, (const char*)firstGrantedName));
		else
			SetMessage(EString().Format("%s found: %d items.", source, grantedItems));
	}
	else {
		SetMessage(EString().Format("%s found: %d gold.", source, goldGain));
	}

	(void)freePickup;
	(void)convertedItems;
	return true;
}

void WorldMap::AwardLegacyTreasure (uint16_t pluginId, uint16_t treasureId) {
	const DragonLegacyTreasureData* treasureData = DragonFindLegacyTreasureData(pluginId, treasureId);
	if(treasureData == NULL) {
		SetMessage("Legacy treasure data unavailable.");
		return;
	}
	if(AwardLegacyItems(treasureData->items, treasureData->itemCount, true, "Treasure"))
		return;
	SetMessage("Treasure chest was empty.");
}

const char* WorldMap::ItemImageName (int itemType) const {
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

void WorldMap::BuildShopOffers () {
	shopOfferCount = 0;
	shopScrollOffset = 0;
	if(legacyShopContextActive == false)
		return;
	const DragonLegacyShopData* legacyShop = DragonFindLegacyShopData(legacyShopPluginId, legacyShopId);
	BuildShopOffersFromLegacy(legacyShop);
}

void WorldMap::BuildShopOffersFromLegacy (const DragonLegacyShopData* shopData) {
	shopOfferCount = 0;
	shopScrollOffset = 0;
	if(shopData == NULL or shopData->items == NULL or shopData->itemCount <= 0)
		return;

	const int buyPercent = shopData->buyPercent == 0 ? 100 : std::max(25, std::min((int)shopData->buyPercent, 400));
	for(int i = 0; i < shopData->itemCount and shopOfferCount < SHOP_MAX_OFFERS; i++) {
		int itemType = DRAGON_ITEM_NONE;
		int itemGold = 0;
		if(MapLegacyItemRefToModern(shopData->items[i], itemType, itemGold) == false)
			continue;
		if(itemType <= DRAGON_ITEM_NONE or itemType >= DRAGON_ITEM_COUNT)
			continue;

		const DragonItemInfo* info = DragonItemByType(itemType);
		if(info == NULL)
			continue;
		// Preserve legacy ordering and allow repeated entries from shop resources.
		const int basePrice = itemGold > 0 ? itemGold : (int)info->value;
		int price = std::max(4, (basePrice * buyPercent) / 100);
		shopOffers[shopOfferCount++] = {itemType, price};
	}
}

void WorldMap::OpenShop () {
	CancelAutoPath();
	if(legacyShopContextActive == false) {
		shopActive = false;
		SetMessage("Legacy shop data unavailable.");
		return;
	}
	BuildShopOffers();
	if(shopOfferCount <= 0) {
		shopActive = false;
		SetMessage("Legacy shop data unavailable.");
		return;
	}
	shopActive = true;
	SetMessage("Merchant open.");
}

void WorldMap::OpenLegacyShop (uint16_t pluginId, uint16_t shopId) {
	legacyShopContextActive = true;
	legacyShopPluginId = pluginId;
	legacyShopId = shopId;
	OpenShop();
}

bool WorldMap::TryBuyShopOffer (int offerIndex) {
	if(offerIndex < 0 or offerIndex >= shopOfferCount)
		return false;

	const ShopOffer& offer = shopOffers[offerIndex];
	const DragonItemInfo* info = DragonItemByType(offer.itemType);
	if(info == NULL)
		return false;
	if(gSave.gold < offer.price) {
		SetMessage(EString().Format("Need %d gold to purchase.", offer.price));
		return false;
	}
	if(info->slot != DRAGON_SLOT_CONSUMABLE and HasItemTypeEquippedOrInBag(offer.itemType)) {
		SetMessage("You already have this gear.");
		return false;
	}
	if(DragonInventoryAdd(offer.itemType, 1) == false) {
		SetMessage("Inventory is full.");
		return false;
	}

	gSave.gold -= offer.price;
	if(info->slot != DRAGON_SLOT_CONSUMABLE)
		SetMessage(EString().Format("Purchased %s for %d gold. %d left.", info->name, offer.price, (int)gSave.gold));
	else
		SetMessage(EString().Format("Purchased %s for %d gold. %d left.", info->name, offer.price, (int)gSave.gold));
	SaveProgress();
	return true;
}

int WorldMap::EncounterChancePercentForSteps (int stepsSinceLastBattle) const {
	(void)stepsSinceLastBattle;
#if defined(DRAGON_TEST)
	if(DragonAutomationCaseIs("Validation_WorldMap_RandomBattleEntry")
		|| DragonAutomationCaseIs("Validation_WorldMap_RandomBattleRetreat"))
		return 100;
#endif
	// Legacy map-mode behavior used a fixed 7% step encounter chance.
	return MAP_ENCOUNTER_CHANCE_PERCENT;
}

bool WorldMap::TryRandomBattle () {
#if defined(DRAGON_TEST)
	if(DragonAutomationCaseIs("Validation_WorldMap_AutoPathTreasure")
		|| DragonAutomationCaseIs("Validation_WorldMap_CurrentTileRouteCancel"))
		return false;
#endif
	stepsSinceBattle++;
	int chance = EncounterChancePercentForSteps(stepsSinceBattle);
	if(chance <= 0)
		return false;
	if((int)ENode::GetRandom(100) >= chance)
		return false;

	const DragonLegacyMapScriptData* mapData = LegacyScriptMapDataForArea(selectedArea);
	if(mapData != NULL) {
		const int rarityRoll = (int)ENode::GetRandom((uint32_t)(MAP_ENCOUNTER_NORMAL_WEIGHT + MAP_ENCOUNTER_RARE_WEIGHT));
		const bool rareEncounter = rarityRoll >= MAP_ENCOUNTER_NORMAL_WEIGHT;
		bool useRareEncounter = rareEncounter;
		uint16_t groupId = useRareEncounter
			? ChooseLegacyBattleGroupId(mapData->rareBattles, 4)
			: ChooseLegacyBattleGroupId(mapData->randomBattles, 16);
		if(groupId != 0 and LegacyBattleGroupExists(mapData->pluginId, groupId) == false) {
			if(useRareEncounter) {
				useRareEncounter = false;
				groupId = ChooseLegacyBattleGroupId(mapData->randomBattles, 16);
			}
			if(groupId != 0 and LegacyBattleGroupExists(mapData->pluginId, groupId) == false)
				groupId = 0;
		}
		if(groupId == 0 and useRareEncounter == true) {
			useRareEncounter = false;
			groupId = ChooseLegacyBattleGroupId(mapData->randomBattles, 16);
			if(groupId != 0 and LegacyBattleGroupExists(mapData->pluginId, groupId) == false)
				groupId = 0;
		}
		if(groupId != 0) {
			DragonQueueLegacyGroupBattle(
				selectedArea,
				DRAGON_BATTLE_RANDOM,
				mapData->pluginId,
				mapData->mapId,
				groupId,
				useRareEncounter,
				false,
				0,
				-1,
				0,
				0,
				NULL
			);
			stepsSinceBattle = 0;
			stepsSinceRecovery = 0;
				BeginBattleTransition();
				return true;
		}

			// Legacy-authored map data can intentionally omit roaming groups.
			// Preserve that behavior instead of injecting synthetic fallback battles.
			return false;
	}

	// Preserve strict legacy parity: if map script payload is unavailable,
	// do not synthesize roaming encounters.
	return false;
}

EString WorldMap::ResolveScriptActionText (const MapEventDefinition& eventDefinition, const MapScriptAction& action) const {
	auto hasVisibleText = [] (const char* text) -> bool {
		if(text == NULL)
			return false;
		for(const unsigned char* c = (const unsigned char*)text; *c != '\0'; c++) {
			if(std::isspace(*c) == 0)
				return true;
		}
		return false;
	};

	if(hasVisibleText(action.text))
		return action.text;

	if(action.type == MAP_SCRIPT_ACTION_YES_NO) {
		if(hasVisibleText(eventDefinition.confirmText))
			return eventDefinition.confirmText;
		if(hasVisibleText(eventDefinition.text))
			return eventDefinition.text;
		return "Proceed?";
	}
	if(action.type == MAP_SCRIPT_ACTION_REFRESH)
		return "";

	if(hasVisibleText(eventDefinition.text))
		return eventDefinition.text;

	switch(action.type) {
		case MAP_EVENT_ITEM_GATE: return "You need a specific item.";
		case MAP_EVENT_TREASURE: return "You found treasure.";
		case MAP_EVENT_HEAL: return "You feel restored.";
		case MAP_EVENT_TRAIN: return "Train here?";
		case MAP_EVENT_SHOP: return "Browse the shop?";
		case MAP_EVENT_WARP: return "Take this route?";
		case MAP_EVENT_BATTLE_GATE:
		case MAP_EVENT_BATTLE_CHALLENGE:
			return "A battle awaits. Proceed?";
		default:
			return "...";
	}
}

EString WorldMap::BuildConfirmPromptText (const MapEventDefinition& eventDefinition, const char* promptFallback) const {
	(void)eventDefinition;
	const char* basePrompt = promptFallback ? promptFallback : "Proceed?";
	return basePrompt;
}

EString WorldMap::ResolveDeclineMessage (const MapEventDefinition* eventDefinition, const EString& promptText, bool trainingPending) const {
	if(trainingPending
		or (eventDefinition != NULL and eventDefinition->type == MAP_EVENT_TRAIN)
		or promptText.Contains("train you")) {
		return "You decline the training.";
	}
	if((eventDefinition != NULL and eventDefinition->type == MAP_EVENT_SHOP)
		or promptText.Contains("take a look at items")) {
		return "You leave the merchant.";
	}
	if((eventDefinition != NULL and eventDefinition->type == MAP_EVENT_BATTLE_CHALLENGE)
		or promptText.Contains("test your skills")
		or promptText.Contains("How about a duel")) {
		return "You decline the challenge for now.";
	}
	if((eventDefinition != NULL and eventDefinition->type == MAP_EVENT_WARP)
		or promptText.Contains("Take this route")) {
		return "You stay on the current path.";
	}
	if((eventDefinition != NULL and eventDefinition->type == MAP_EVENT_BATTLE_GATE)
		or promptText.Contains("A battle awaits")) {
		return "You hold off on the battle.";
	}
	return "Maybe later.";
}

int WorldMap::LegacyTrainingCostForLevel (int level) const {
	const int safeLevel = std::max(1, std::min(level, DRAGON_RUNTIME_LEVEL_CAP));
	const int baseCost = std::max(1, DESIGN_TRAINING_BASE_COST);
	const int64_t raw = (int64_t)(safeLevel + 1) * (int64_t)baseCost;
	return (int)std::max<int64_t>(1, std::min<int64_t>(raw, DESIGN_MAX_GOLD));
}

bool WorldMap::ApplyLegacyTrainingPurchase (int cost) {
	const int safeCost = std::max(1, cost);
	if(gSave.level >= DRAGON_RUNTIME_LEVEL_CAP) {
		SetMessage("You are already at max level.");
		return false;
	}
	if(gSave.gold < safeCost) {
		SetMessage(EString().Format("Need %d gold for training.", safeCost));
		return false;
	}

	const int oldLevel = std::max(1, (int)gSave.level);
	gSave.gold -= safeCost;
	gSave.level = (int16_t)std::min(DRAGON_RUNTIME_LEVEL_CAP, oldLevel + 1);
	DragonApplyCurrentLevelGrowth();
	DragonHealToFull();
	EString milestoneRewards = DragonGrantLevelMilestoneRewards(oldLevel, gSave.level);
	if(not milestoneRewards.IsEmpty())
		SetMessage(EString().Format("Level up! Reached level %d. New gear: %s.", gSave.level, (const char*)milestoneRewards));
	else
		SetMessage(EString().Format("Level up! Reached level %d.", gSave.level));
	return true;
}

void WorldMap::ShowConfirmPrompt (const MapEventDefinition& eventDefinition, int area, int eventIndex, const MapScriptAction& action, bool fromMove) {
	promptActive = true;
	promptEventArea = area;
	promptEventIndex = eventIndex;
	promptNextAction = action.nextAction;
	promptNextNoAction = action.nextNoAction;
	promptFromMove = fromMove;
	promptLegacyScript = action.legacyScript != 0;
	promptTrainingPending = false;
	promptTrainingCost = 0;
	const EString promptSource = ResolveScriptActionText(eventDefinition, action);
	promptText = BuildConfirmPromptText(eventDefinition, (const char*)promptSource);
}

void WorldMap::ShowTalkPrompt (const EString& text, int area, int eventIndex, int nextAction, bool fromMove) {
	talkActive = true;
	talkEventArea = area;
	talkEventIndex = eventIndex;
	talkNextAction = nextAction;
	talkFromMove = fromMove;
	talkText = text.IsEmpty() ? "..." : text;
}

bool WorldMap::ExecuteEventAction (const MapEventDefinition& eventDefinition, int area, int eventIndex, const MapScriptAction& action, bool fromMove) {
	(void)eventDefinition;
	if(action.legacyScript == 0) {
		ScriptTrace("exec stop area=%d event=%d action-type=%u reason=no-legacy-script-action", area, eventIndex, (unsigned)action.type);
		return false;
	}
	return ExecuteLegacyEventAction(area, eventIndex, action, fromMove);
}

bool WorldMap::ExecuteLegacyEventAction (int area, int eventIndex, const MapScriptAction& action, bool fromMove) {
	(void)fromMove;
	const DragonLegacyMapScriptData* areaMapData = LegacyScriptMapDataForArea(area);
	const uint16_t mapPluginId = areaMapData != NULL ? areaMapData->pluginId : legacyPluginIdByArea[area];
	const uint16_t mapId = areaMapData != NULL ? areaMapData->mapId : legacyMapIdByArea[area];
	auto setLegacyFlagAndRefresh = [this, area] (uint16_t flagId) {
		if(flagId == 0)
			return;
		DragonSetLegacyFlag(flagId, true);
		const int preferredX = std::max(0, std::min((int)gWorldState.areaMapX[area], DRAGON_ALPHA_MAP_WIDTH - 1));
		const int preferredY = std::max(0, std::min((int)gWorldState.areaMapY[area], DRAGON_ALPHA_MAP_HEIGHT - 1));
		RebuildAreaBlockedCells(area, preferredX, preferredY);
		RebuildEventPositions(area);
	};

	switch(action.legacyObjectType) {
		case 2: {
			const int preferredX = std::max(0, std::min((int)gWorldState.areaMapX[area], DRAGON_ALPHA_MAP_WIDTH - 1));
			const int preferredY = std::max(0, std::min((int)gWorldState.areaMapY[area], DRAGON_ALPHA_MAP_HEIGHT - 1));
			RebuildAreaBlockedCells(area, preferredX, preferredY);
			RebuildEventPositions(area);
			loadedAreaVisual = -1;
			EnsureAreaVisualLoaded();
			return true;
		}
		case 7: {
			DragonLegacyItemRef required = {
				action.legacyData1,
				action.legacyData2 == 0 ? mapPluginId : action.legacyData2,
				action.legacyData3,
			};
			int requiredItemType = DRAGON_ITEM_NONE;
			int requiredGold = 0;
			if(MapLegacyItemRefToModern(required, requiredItemType, requiredGold) == false or requiredItemType <= DRAGON_ITEM_NONE) {
				SetMessage("You need a specific item.");
				return true;
			}
			(void)requiredGold;
			if(DragonInventoryConsumeType(requiredItemType, 1)) {
				setLegacyFlagAndRefresh(action.legacyData0);
				SetMessage("Required item consumed.");
				PlaySoundIfLoaded(soundTreasure);
				return true;
			}
			const DragonItemInfo* info = DragonItemByType(requiredItemType);
			SetMessage(EString().Format("You need %s.", info != NULL ? info->name : "the required item"));
			return true;
		}
		case 8:
			DragonHealToFull();
			SetMessage(action.text ? action.text : "Rest and heal your wounds.");
			PlaySoundIfLoaded(soundHeal);
			return true;
		case 9: {
			const int cost = LegacyTrainingCostForLevel((int)gSave.level);
			promptActive = true;
			promptEventArea = area;
			promptEventIndex = eventIndex;
			// Legacy `CharLvUp` always returns to script flow regardless of yes/no.
			promptNextAction = action.nextAction;
			promptNextNoAction = action.nextAction;
			promptFromMove = fromMove;
			promptLegacyScript = action.legacyScript != 0;
			promptTrainingPending = true;
			promptTrainingCost = cost;
			promptText = EString().Format("For %d gold, I can train you to level %d. Proceed?", cost, std::min(DRAGON_RUNTIME_LEVEL_CAP, (int)gSave.level + 1));
			return true;
		}
		case 10:
			shopHasPendingScript = false;
			shopEventArea = -1;
			shopEventIndex = -1;
			shopNextAction = -1;
			shopFromMove = false;
			if(action.legacyData1 != 0) {
				OpenLegacyShop(mapPluginId, action.legacyData1);
				if(shopActive and action.nextAction >= 0) {
					shopHasPendingScript = true;
					shopEventArea = area;
					shopEventIndex = eventIndex;
					shopNextAction = action.nextAction;
					shopFromMove = fromMove;
				}
			}
			else
				SetMessage("Legacy shop data unavailable.");
			return true;
		case 11:
			AwardLegacyTreasure(mapPluginId, action.legacyData1);
			setLegacyFlagAndRefresh(action.legacyData2);
			PlaySoundIfLoaded(soundTreasure);
			return true;
		case 12: {
			if(action.legacyData0 != 0 and DragonGetLegacyFlag(action.legacyData0)) {
				SetMessage("This battle has already been won.");
				return true;
			}
			if(action.legacyData1 == 0) {
				SetMessage("Legacy battle data unavailable.");
				return true;
			}
			if(LegacyBattleGroupExists(mapPluginId, action.legacyData1) == false) {
				SetMessage("Legacy battle group missing.");
				return true;
			}

			const uint16_t completionFlag = action.legacyData2 != 0
				? (uint16_t)(DRAGON_EVENT_FLAG_LEGACY_COMPLETION_MARKER | (action.legacyData2 & 0x7FFF))
				: 0;
			const bool finalBoss = (area == DRAGON_AREA_PEAK) and (action.legacyData2 >= 100);
				DragonQueueLegacyGroupBattle(
					area,
					finalBoss ? DRAGON_BATTLE_BOSS : DRAGON_BATTLE_GATE,
					mapPluginId,
					mapId,
					action.legacyData1,
					false,
					false,
					completionFlag,
					-1,
					0,
					0,
					NULL
				);
			gLegacyPendingBattleArea = area;
			gLegacyPendingBattleEventIndex = eventIndex;
			gLegacyPendingBattleNextAction = action.nextAction;
			stepsSinceBattle = 0;
			stepsSinceRecovery = 0;
				BeginBattleTransition();
				return true;
		}
		default:
			return false;
	}
}

bool WorldMap::ExecuteEventScript (int area, int eventIndex, int actionIndex, bool fromMove) {
	const MapEventDefinition* eventDefinition = EventDefinitionAt(area, eventIndex);
	if(eventDefinition == NULL)
		return false;
	if(actionIndex >= MAP_EVENT_CAPACITY) {
		ScriptTrace(
			"exec stop area=%d event=%d action=%d reason=invalid-start-index",
			area,
			eventIndex,
			actionIndex
		);
		return false;
	}
	ScriptTrace(
		"exec begin area=%d event=%d action=%d legacy=%d fromMove=%d",
		area,
		eventIndex,
		actionIndex,
		eventDefinition->legacyObjectType != 0 ? 1 : 0,
		fromMove ? 1 : 0
	);

	if(eventDefinition->legacyObjectType == 0) {
		ScriptTrace("exec stop area=%d event=%d action=%d reason=no-legacy-object", area, eventIndex, actionIndex);
		return false;
	}

	if(legacyScriptEventArea != area or legacyScriptEventIndex != eventIndex or actionIndex == 0) {
		legacyScriptActions.clear();
		if(BuildLegacyScriptActions(area, eventIndex, legacyScriptActions) == false) {
			legacyScriptEventArea = -1;
			legacyScriptEventIndex = -1;
			return false;
		}
		legacyScriptEventArea = area;
		legacyScriptEventIndex = eventIndex;
	}

	int currentAction = actionIndex;
	bool executedAnyAction = false;
	for(int guard = 0; guard < MAP_EVENT_CAPACITY and currentAction >= 0; guard++) {
		MapScriptAction action = {};
		const bool resolvedAction = ResolveLegacyScriptAction(area, eventIndex, currentAction, action);
		if(resolvedAction == false) {
			ScriptTrace("exec stop area=%d event=%d unresolved action=%d", area, eventIndex, currentAction);
			break;
		}
		ScriptTrace(
			"exec step area=%d event=%d action=%d type=%u next=%d nextNo=%d legacy=%u flags=%u data=%u/%u/%u/%u text=%s",
			area,
			eventIndex,
			currentAction,
			(unsigned)action.type,
			action.nextAction,
			action.nextNoAction,
			(unsigned)action.legacyScript,
			(unsigned)action.flagBit,
			(unsigned)action.legacyData0,
			(unsigned)action.legacyData1,
			(unsigned)action.legacyData2,
			(unsigned)action.legacyData3,
			action.text != NULL ? action.text : "<null>"
		);

		if(action.type == MAP_SCRIPT_ACTION_YES_NO) {
			ShowConfirmPrompt(*eventDefinition, area, eventIndex, action, fromMove);
			ScriptTrace("exec prompt area=%d event=%d action=%d", area, eventIndex, currentAction);
			return true;
		}

			if(action.type == MAP_EVENT_TALK) {
				if(action.legacyScript != 0 and action.legacyData2 != 0) {
					DragonSetLegacyFlag(action.legacyData2, true);
					const int preferredX = std::max(0, std::min((int)gWorldState.areaMapX[area], DRAGON_ALPHA_MAP_WIDTH - 1));
					const int preferredY = std::max(0, std::min((int)gWorldState.areaMapY[area], DRAGON_ALPHA_MAP_HEIGHT - 1));
					RebuildAreaBlockedCells(area, preferredX, preferredY);
					RebuildEventPositions(area);
				}
				talkLegacyScript = action.legacyScript != 0;
				const EString talkLine = ResolveScriptActionText(*eventDefinition, action);
				ShowTalkPrompt(talkLine, area, eventIndex, action.nextAction, fromMove);
				ScriptTrace("exec talk area=%d event=%d action=%d", area, eventIndex, currentAction);
				return true;
		}

		if(ExecuteEventAction(*eventDefinition, area, eventIndex, action, fromMove) == false)
			return executedAnyAction;
		executedAnyAction = true;

		if(action.type == MAP_EVENT_BATTLE_GATE or action.type == MAP_EVENT_BATTLE_CHALLENGE)
			return true;

			currentAction = action.nextAction;
		}
	if(currentAction >= 0) {
		ScriptTrace(
			"exec stop area=%d event=%d action=%d reason=step-guard-limit limit=%d",
			area,
			eventIndex,
			currentAction,
			MAP_EVENT_CAPACITY
		);
	}
	ScriptTrace("exec end area=%d event=%d executed=%d", area, eventIndex, executedAnyAction ? 1 : 0);
	return executedAnyAction;
}

void WorldMap::ResolveConfirmPrompt (bool accepted) {
	const int area = promptEventArea;
	const int eventIndex = promptEventIndex;
	const int nextAction = promptNextAction;
	const int nextNoAction = promptNextNoAction;
	const bool fromMove = promptFromMove;
	const bool trainingPending = promptTrainingPending;
	const int trainingCost = promptTrainingCost;
	const EString promptLine = promptText;

	promptActive = false;
	promptText = "";
	promptEventArea = -1;
	promptEventIndex = -1;
	promptNextAction = -1;
	promptNextNoAction = -1;
	promptFromMove = false;
	promptLegacyScript = false;
	promptTrainingPending = false;
	promptTrainingCost = 0;

	if(area < 0 or area >= DRAGON_ALPHA_AREA_COUNT or eventIndex < 0)
		return;

	const MapEventDefinition* eventDefinition = EventDefinitionAt(area, eventIndex);

	if(trainingPending) {
		if(accepted)
			ApplyLegacyTrainingPurchase(trainingCost);
		else
			SetMessage(ResolveDeclineMessage(eventDefinition, promptLine, true));
		if(nextAction >= 0 and ExecuteEventScript(area, eventIndex, nextAction, fromMove) and promptActive == false and talkActive == false)
			SaveProgress();
		return;
	}

	if(accepted) {
		if(nextAction >= 0 and ExecuteEventScript(area, eventIndex, nextAction, fromMove) and promptActive == false and talkActive == false)
			SaveProgress();
		return;
	}

	if(nextNoAction >= 0) {
		if(ExecuteEventScript(area, eventIndex, nextNoAction, fromMove) and promptActive == false and talkActive == false)
			SaveProgress();
	}
	else
		SetMessage(ResolveDeclineMessage(eventDefinition, promptLine, false));
}

void WorldMap::ResolveTalkPrompt () {
	const int area = talkEventArea;
	const int eventIndex = talkEventIndex;
	const int nextAction = talkNextAction;
	const bool fromMove = talkFromMove;

	talkActive = false;
	talkEventArea = -1;
	talkEventIndex = -1;
	talkNextAction = -1;
	talkFromMove = false;
	talkLegacyScript = false;
	talkText = "";

	if(area < 0 or area >= DRAGON_ALPHA_AREA_COUNT or eventIndex < 0)
		return;

	if(nextAction >= 0) {
		if(ExecuteEventScript(area, eventIndex, nextAction, fromMove) and promptActive == false and talkActive == false)
			SaveProgress();
	}
}

bool WorldMap::ResolveShopPrompt () {
	const int area = shopEventArea;
	const int eventIndex = shopEventIndex;
	const int nextAction = shopNextAction;
	const bool fromMove = shopFromMove;
	const bool hasPending = shopHasPendingScript;

	shopActive = false;
	legacyShopContextActive = false;
	legacyShopPluginId = 0;
	legacyShopId = 0;
	shopHasPendingScript = false;
	shopEventArea = -1;
	shopEventIndex = -1;
	shopNextAction = -1;
	shopFromMove = false;

	if(hasPending == false)
		return false;
	if(area < 0 or area >= DRAGON_ALPHA_AREA_COUNT or eventIndex < 0 or nextAction < 0)
		return false;

	if(ExecuteEventScript(area, eventIndex, nextAction, fromMove)) {
		if(promptActive == false and talkActive == false and shopActive == false)
			SaveProgress();
		return true;
	}
	return false;
}

EImage& WorldMap::EventIcon (const MapEventDefinition& eventDefinition) {
	switch(eventDefinition.type) {
		case MAP_EVENT_TALK: return imageEventTalk;
		case MAP_EVENT_ITEM_GATE: return imageEventTalk;
		case MAP_EVENT_TREASURE: return imageEventTreasure;
		case MAP_EVENT_HEAL: return imageEventHeal;
		case MAP_EVENT_TRAIN: return imageEventTrain;
		case MAP_EVENT_SHOP: return imageEventShop;
		case MAP_EVENT_WARP: return imageEventWarp;
		case MAP_EVENT_BATTLE_GATE: return imageEventGate;
		case MAP_EVENT_BATTLE_CHALLENGE: return imageEventGate;
		default: return imageGridAccent;
	}
}

bool WorldMap::IsInteractOnlyEventType (uint8_t eventType) const {
	return eventType == MAP_EVENT_TALK
		or eventType == MAP_EVENT_ITEM_GATE
		or eventType == MAP_EVENT_TREASURE
		or eventType == MAP_EVENT_HEAL
		or eventType == MAP_EVENT_TRAIN
		or eventType == MAP_EVENT_SHOP;
}

bool WorldMap::ShouldAutoInteractAtAdjacentEvent (const MapEventDefinition& eventDefinition, int eventX, int eventY) const {
	if(IsInteractOnlyEventType(eventDefinition.type))
		return true;
	if(eventDefinition.type == MAP_EVENT_WARP
		or eventDefinition.type == MAP_EVENT_BATTLE_GATE
		or eventDefinition.type == MAP_EVENT_BATTLE_CHALLENGE)
		return true;
	return IsBlockedCell(selectedArea, eventX, eventY);
}

void WorldMap::PlayStepSound () {
	if(MAP_ENABLE_TRAVERSAL_SFX == false)
		return;
	const int variantIndex = (int)ENode::GetRandom((uint32_t)MAP_TRAVERSAL_STEP_VARIANTS);
	PlaySoundIfLoaded(soundStep[variantIndex]);
}

void WorldMap::PlayBumpSound () {
	if(MAP_ENABLE_TRAVERSAL_SFX == false)
		return;
	const int64_t now = GetMilliseconds();
	if(MAP_TRAVERSAL_BUMP_COOLDOWN_MS > 0 and now - lastBumpSoundAt < MAP_TRAVERSAL_BUMP_COOLDOWN_MS)
		return;
	lastBumpSoundAt = now;
	switch((int)ENode::GetRandom(4)) {
		case 0: PlaySoundIfLoaded(soundBump[0]); break;
		case 1: PlaySoundIfLoaded(soundBump[1]); break;
		case 2: PlaySoundIfLoaded(soundBump[2]); break;
		default: PlaySoundIfLoaded(soundBump[3]); break;
	}
}

void WorldMap::BeginBattleTransition () {
	CancelAutoPath();
	SaveProgress();
	PlaySoundIfLoaded(soundBattleEntry);
	RunNewNode("Battle");
}

void WorldMap::ResetSwipeTracking () {
	swipeTracking = false;
	swipeStartX = 0;
	swipeStartY = 0;
	swipeLastX = 0;
	swipeLastY = 0;
	swipeLastMoveTime = 0;
}

int WorldMap::PathAffinityScore (int area, int x, int y) const {
	if(IsPathBlockedCell(area, x, y))
		return INT_MIN / 4;

	static const int cardinalDX[4] = {0, 0, -1, 1};
	static const int cardinalDY[4] = {-1, 1, 0, 0};
	static const int diagonalDX[4] = {-1, 1, -1, 1};
	static const int diagonalDY[4] = {-1, -1, 1, 1};

	int score = 0;
	for(int i = 0; i < 4; i++) {
		if(IsPathBlockedCell(area, x + cardinalDX[i], y + cardinalDY[i]))
			score += 3;
	}
	for(int i = 0; i < 4; i++) {
		if(IsPathBlockedCell(area, x + diagonalDX[i], y + diagonalDY[i]))
			score += 1;
	}

	if(x < 2 or y < 2 or x > DRAGON_ALPHA_MAP_WIDTH - 3 or y > DRAGON_ALPHA_MAP_HEIGHT - 3)
		score -= 3;
	return score;
}

bool WorldMap::IsLegacyWarpCell (int area, int x, int y) const {
	if(area < 0 or area >= DRAGON_ALPHA_AREA_COUNT)
		return false;
	if(x < 0 or x >= DRAGON_ALPHA_MAP_WIDTH or y < 0 or y >= DRAGON_ALPHA_MAP_HEIGHT)
		return false;

	const uint16_t pluginId = legacyPluginIdByArea[area];
	const uint16_t mapId = legacyMapIdByArea[area];
	const DragonLegacyMapWarpData* mapData = DragonFindLegacyMapWarpData(pluginId, mapId);
	if(mapData == NULL or mapData->warps == NULL or mapData->warpCount <= 0)
		return false;

	for(int i = 0; i < mapData->warpCount; i++) {
		const DragonLegacyWarpObject& warp = mapData->warps[i];
		if(CellInsideLegacyRect(x, y, (int)warp.x1, (int)warp.y1, (int)warp.x2, (int)warp.y2))
			return true;
	}
	return false;
}

bool WorldMap::IsUnsupportedLegacyWarpCell (int area, int x, int y, uint16_t* outPluginId) const {
	if(outPluginId != NULL)
		*outPluginId = 0;
	if(area < 0 or area >= DRAGON_ALPHA_AREA_COUNT)
		return false;
	if(x < 0 or x >= DRAGON_ALPHA_MAP_WIDTH or y < 0 or y >= DRAGON_ALPHA_MAP_HEIGHT)
		return false;

	const uint16_t pluginId = legacyPluginIdByArea[area];
	const uint16_t mapId = legacyMapIdByArea[area];
	const DragonLegacyMapWarpData* mapData = DragonFindLegacyMapWarpData(pluginId, mapId);
	if(mapData == NULL or mapData->warps == NULL or mapData->warpCount <= 0)
		return false;

	for(int i = 0; i < mapData->warpCount; i++) {
		const DragonLegacyWarpObject& warp = mapData->warps[i];
		if(CellInsideLegacyRect(x, y, (int)warp.x1, (int)warp.y1, (int)warp.x2, (int)warp.y2) == false)
			continue;
		const int targetArea = AreaIndexForLegacyPluginMap(warp.targetPluginId, warp.targetMapId);
		if(targetArea >= 0 and targetArea < DRAGON_ALPHA_AREA_COUNT)
			continue;
		if(outPluginId != NULL)
			*outPluginId = warp.targetPluginId;
		return true;
	}
	return false;
}

bool WorldMap::IsPathBlockedCell (int area, int x, int y) const {
	if(IsUnsupportedLegacyWarpCell(area, x, y, NULL))
		return true;
	if(IsBlockedCell(area, x, y) == false)
		return false;
	// Warp tiles are interactive transition triggers in the legacy map flow.
	return IsLegacyWarpCell(area, x, y) == false;
}

bool WorldMap::IsEventReachableForObjective (int area, int eventIndex, const MapEventDefinition& eventDefinition) const {
	if(area < 0 or area >= DRAGON_ALPHA_AREA_COUNT)
		return false;

	int startX = area == selectedArea
		? mapX
		: std::max(0, std::min((int)gWorldState.areaMapX[area], DRAGON_ALPHA_MAP_WIDTH - 1));
	int startY = area == selectedArea
		? mapY
		: std::max(0, std::min((int)gWorldState.areaMapY[area], DRAGON_ALPHA_MAP_HEIGHT - 1));
	if(IsPathBlockedCell(area, startX, startY)) {
		if(FindNearestOpenCell(area, startX, startY, startX, startY) == false)
			return false;
	}

	int eventX = 0;
	int eventY = 0;
	if(ResolveEventPositionAtIndex(area, eventIndex, eventX, eventY) == false)
		return false;

	if(IsPathBlockedCell(area, eventX, eventY) == false)
		return IsReachableCell(area, startX, startY, eventX, eventY);

	if(IsUnsupportedLegacyWarpCell(area, eventX, eventY, NULL))
		return false;

	if(ShouldAutoInteractAtAdjacentEvent(eventDefinition, eventX, eventY) == false)
		return false;

	static const int offsetX[4] = {0, 0, -1, 1};
	static const int offsetY[4] = {-1, 1, 0, 0};
	for(int i = 0; i < 4; i++) {
		const int adjacentX = eventX + offsetX[i];
		const int adjacentY = eventY + offsetY[i];
		if(IsPathBlockedCell(area, adjacentX, adjacentY))
			continue;
		if(IsReachableCell(area, startX, startY, adjacentX, adjacentY))
			return true;
	}

	return false;
}

int WorldMap::PrimaryObjectiveEventIndex (int area) const {
	if(area < 0 or area >= DRAGON_ALPHA_AREA_COUNT)
		return -1;

	int bestIndex = -1;
	int bestPriority = INT_MAX;
	const int eventCount = EventCountForArea(area);
	for(int eventIndex = 0; eventIndex < eventCount; eventIndex++) {
		const MapEventDefinition* eventDefinition = EventDefinitionAt(area, eventIndex);
		if(eventDefinition == NULL or eventDefinition->type == MAP_EVENT_NONE)
			continue;
		if(IsRequirementMet(*eventDefinition, area) == false or IsEventComplete(*eventDefinition, area))
			continue;
		if(IsEventReachableForObjective(area, eventIndex, *eventDefinition) == false)
			continue;

		int priority = 8;
		switch(eventDefinition->type) {
			case MAP_EVENT_BATTLE_GATE: priority = 0; break;
			case MAP_EVENT_WARP: priority = 1; break;
			case MAP_EVENT_BATTLE_CHALLENGE: priority = 2; break;
			case MAP_EVENT_ITEM_GATE: priority = 3; break;
			case MAP_EVENT_TREASURE:
			case MAP_EVENT_HEAL: priority = 4; break;
			case MAP_EVENT_SHOP:
			case MAP_EVENT_TRAIN: priority = 5; break;
			case MAP_EVENT_TALK: priority = 6; break;
			default: priority = 7; break;
		}

		if(bestIndex < 0 or priority < bestPriority or (priority == bestPriority and eventIndex < bestIndex)) {
			bestIndex = eventIndex;
			bestPriority = priority;
		}
	}
	return bestIndex;
}

bool WorldMap::ScreenToMapCell (int screenX, int screenY, int& outMapX, int& outMapY) const {
	const ERect touchFrame = (mapTileFrameRect.width > 0 and mapTileFrameRect.height > 0) ? mapTileFrameRect : gridFrameRect;
	if(touchFrame.IsPointInRect(screenX, screenY) == false)
		return false;
	if(visibleCols <= 0 or visibleRows <= 0 or touchFrame.width <= 0 or touchFrame.height <= 0)
		return false;

	const int localX = std::max(0, std::min(touchFrame.width - 1, screenX - touchFrame.x));
	const int localY = std::max(0, std::min(touchFrame.height - 1, screenY - touchFrame.y));
	int col = 0;
	int row = 0;
	if(mapTileSize > 0 and touchFrame.width == mapTileSize * visibleCols and touchFrame.height == mapTileSize * visibleRows) {
		// Use containment-based tile quantization so each pixel maps to the tile
		// it is physically inside and doesn't shift half a tile at boundaries.
		col = std::max(0, std::min(visibleCols - 1, localX / std::max(1, mapTileSize)));
		row = std::max(0, std::min(visibleRows - 1, localY / std::max(1, mapTileSize)));
	}
	else {
		col = (localX * visibleCols) / std::max(1, touchFrame.width);
		row = (localY * visibleRows) / std::max(1, touchFrame.height);
	}

	outMapX = std::max(0, std::min(DRAGON_ALPHA_MAP_WIDTH - 1, visibleStartX + col));
	outMapY = std::max(0, std::min(DRAGON_ALPHA_MAP_HEIGHT - 1, visibleStartY + row));
	return true;
}

bool WorldMap::ResolvePreferredTapTarget (int desiredX, int desiredY, int& outX, int& outY) const {
	outX = std::max(0, std::min(DRAGON_ALPHA_MAP_WIDTH - 1, desiredX));
	outY = std::max(0, std::min(DRAGON_ALPHA_MAP_HEIGHT - 1, desiredY));
	if(selectedArea < 0 or selectedArea >= DRAGON_ALPHA_AREA_COUNT)
		return false;
	// Preserve direct-touch intent whenever the tapped tile is already a valid
	// movement target. Legacy event magneting only applies to blocked taps.
	if(IsPathBlockedCell(selectedArea, outX, outY) == false)
		return true;
	const MapEventDefinition* tappedEvent = FindEvent(selectedArea, outX, outY, NULL);
	if(tappedEvent != NULL and IsRequirementMet(*tappedEvent, selectedArea) and IsEventComplete(*tappedEvent, selectedArea) == false)
		return true;
	if(IsUnsupportedLegacyWarpCell(selectedArea, outX, outY, NULL))
		return true;

	const int objectiveEventIndex = PrimaryObjectiveEventIndex(selectedArea);

	bool found = false;
	int bestScore = INT_MAX;
	int bestX = outX;
	int bestY = outY;
	const int selectedEventCount = EventCountForArea(selectedArea);
	for(int eventIndex = 0; eventIndex < selectedEventCount; eventIndex++) {
		const MapEventDefinition* eventDefinition = EventDefinitionAt(selectedArea, eventIndex);
		if(eventDefinition == NULL)
			continue;
		if(IsEventComplete(*eventDefinition, selectedArea))
			continue;
		if(IsRequirementMet(*eventDefinition, selectedArea) == false)
			continue;
		if(IsEventReachableForObjective(selectedArea, eventIndex, *eventDefinition) == false)
			continue;

		int eventX = 0;
		int eventY = 0;
		if(ResolveEventPositionAtIndex(selectedArea, eventIndex, eventX, eventY) == false)
			continue;

		const int distance = std::abs(eventX - outX) + std::abs(eventY - outY);
		int magnetDistance = 1;
		if(eventDefinition->type == MAP_EVENT_BATTLE_GATE or eventDefinition->type == MAP_EVENT_WARP or eventDefinition->type == MAP_EVENT_BATTLE_CHALLENGE)
			magnetDistance = MAP_TOUCH_EVENT_MAGNET_DISTANCE;
		if(distance > magnetDistance)
			continue;

		int progressionPriority = 5;
		if(eventDefinition->type == MAP_EVENT_BATTLE_GATE)
			progressionPriority = 0;
		else if(eventDefinition->type == MAP_EVENT_WARP)
			progressionPriority = 1;
		else if(eventDefinition->type == MAP_EVENT_BATTLE_CHALLENGE)
			progressionPriority = 2;
		else if(eventDefinition->type == MAP_EVENT_ITEM_GATE)
			progressionPriority = 3;
		else if(eventDefinition->type == MAP_EVENT_TREASURE or eventDefinition->type == MAP_EVENT_HEAL)
			progressionPriority = 4;
		else if(eventDefinition->type == MAP_EVENT_TALK)
			progressionPriority = 5;

		const bool isObjectiveEvent = objectiveEventIndex >= 0 and eventIndex == objectiveEventIndex;

		int score = distance * 16 + progressionPriority * 4 + eventIndex;
		if(isObjectiveEvent)
			score -= 10;
		if(found == false or score < bestScore) {
			found = true;
			bestScore = score;
			bestX = eventX;
			bestY = eventY;
		}
	}

	if(found) {
		outX = bestX;
		outY = bestY;
	}
	return true;
}

bool WorldMap::FindReachableDestination (int area, int startX, int startY, int desiredX, int desiredY, int& outX, int& outY) const {
	if(area < 0 or area >= DRAGON_ALPHA_AREA_COUNT)
		return false;
	if(IsPathBlockedCell(area, startX, startY))
		return false;

	desiredX = std::max(0, std::min(DRAGON_ALPHA_MAP_WIDTH - 1, desiredX));
	desiredY = std::max(0, std::min(DRAGON_ALPHA_MAP_HEIGHT - 1, desiredY));

	const int width = DRAGON_ALPHA_MAP_WIDTH;
	const int height = DRAGON_ALPHA_MAP_HEIGHT;
	const int total = width * height;
	const int start = startY * width + startX;

	std::vector<uint8_t> visited(total, 0);
	std::deque<int> frontier;
	visited[start] = 1;
	frontier.push_back(start);

	auto eventAdjacencyBoost = [this, area] (int cellX, int cellY) -> int {
		int boost = 0;
		const int eventCount = EventCountForArea(area);
		for(int eventIndex = 0; eventIndex < eventCount; eventIndex++) {
			const MapEventDefinition* eventDefinition = EventDefinitionAt(area, eventIndex);
			if(eventDefinition == NULL or eventDefinition->type == MAP_EVENT_NONE)
				continue;
			if(IsEventComplete(*eventDefinition, area))
				continue;
			if(IsRequirementMet(*eventDefinition, area) == false)
				continue;

			int eventX = 0;
			int eventY = 0;
			if(ResolveEventPositionAtIndex(area, eventIndex, eventX, eventY) == false)
				continue;

			const int distance = std::abs(eventX - cellX) + std::abs(eventY - cellY);
			if(distance == 0)
				boost += 24;
			else if(distance == 1)
				boost += 18;
			else if(distance == 2 and (eventDefinition->type == MAP_EVENT_BATTLE_GATE or eventDefinition->type == MAP_EVENT_WARP or eventDefinition->type == MAP_EVENT_BATTLE_CHALLENGE))
				boost += 6;
		}
		return boost;
	};

	int bestIndex = start;
	int bestDistance = std::abs(startX - desiredX) + std::abs(startY - desiredY);
	int bestAffinity = PathAffinityScore(area, startX, startY);
	int bestEventBoost = eventAdjacencyBoost(startX, startY);

	static const int offsetX[4] = {0, 0, -1, 1};
	static const int offsetY[4] = {-1, 1, 0, 0};

	while(frontier.empty() == false) {
		int current = frontier.front();
		frontier.pop_front();

		int currentX = current % width;
		int currentY = current / width;
		int distance = std::abs(currentX - desiredX) + std::abs(currentY - desiredY);
		int affinity = PathAffinityScore(area, currentX, currentY);
		int eventBoost = eventAdjacencyBoost(currentX, currentY);
		if(distance < bestDistance
			or (distance == bestDistance and eventBoost > bestEventBoost)
			or (distance == bestDistance and eventBoost == bestEventBoost and affinity > bestAffinity)) {
			bestIndex = current;
			bestDistance = distance;
			bestAffinity = affinity;
			bestEventBoost = eventBoost;
		}
		if(distance == 0) {
			bestIndex = current;
			break;
		}

		for(int i = 0; i < 4; i++) {
			int nextX = currentX + offsetX[i];
			int nextY = currentY + offsetY[i];
			if(nextX < 0 or nextX >= width or nextY < 0 or nextY >= height)
				continue;
			if(IsPathBlockedCell(area, nextX, nextY))
				continue;
			int next = nextY * width + nextX;
			if(visited[next] != 0)
				continue;
			visited[next] = 1;
			frontier.push_back(next);
		}
	}

	if(bestIndex < 0)
		return false;
	outX = bestIndex % width;
	outY = bestIndex / width;
	return true;
}

bool WorldMap::FindPathAStar (int area, int startX, int startY, int targetX, int targetY, std::vector<EPoint>& outPath) const {
	outPath.clear();
	if(area < 0 or area >= DRAGON_ALPHA_AREA_COUNT)
		return false;
	if(IsPathBlockedCell(area, startX, startY) or IsPathBlockedCell(area, targetX, targetY))
		return false;

	const int width = DRAGON_ALPHA_MAP_WIDTH;
	const int height = DRAGON_ALPHA_MAP_HEIGHT;
	const int total = width * height;
	const int startIndex = startY * width + startX;
	const int targetIndex = targetY * width + targetX;
	if(startIndex == targetIndex) {
		outPath.push_back(EPoint(startX, startY));
		return true;
	}

	std::vector<int> parent(total, -1);
	std::vector<int> gScore(total, INT_MAX / 4);
	std::vector<uint8_t> closedSet(total, 0);

	auto heuristic = [targetX, targetY](int x, int y) -> int {
		return std::abs(x - targetX) + std::abs(y - targetY);
	};

	struct OpenNode {
		int index;
		int fScore;
		int hScore;
		int serial;
	};
	struct OpenNodeGreater {
		bool operator () (const OpenNode& a, const OpenNode& b) const {
			if(a.fScore != b.fScore)
				return a.fScore > b.fScore;
			if(a.hScore != b.hScore)
				return a.hScore > b.hScore;
			return a.serial > b.serial;
		}
	};
	std::priority_queue<OpenNode, std::vector<OpenNode>, OpenNodeGreater> openQueue;
	int openSerial = 0;
	gScore[startIndex] = 0;
	const int startHeuristic = heuristic(startX, startY);
	openQueue.push(OpenNode{startIndex, startHeuristic, startHeuristic, openSerial++});

	static const int offsetX[4] = {0, 0, -1, 1};
	static const int offsetY[4] = {-1, 1, 0, 0};
	static const int kPathExpansionLimit = DRAGON_ALPHA_MAP_WIDTH * DRAGON_ALPHA_MAP_HEIGHT * 8;
	int expansions = 0;

	while(openQueue.empty() == false) {
		const OpenNode currentNode = openQueue.top();
		openQueue.pop();

		const int current = currentNode.index;
		if(current < 0 or current >= total)
			continue;
		if(closedSet[current] != 0)
			continue;
		const int currentX = current % width;
		const int currentY = current / width;
		const int expectedFScore = gScore[current] + heuristic(currentX, currentY);
		if(currentNode.fScore != expectedFScore)
			continue;

		closedSet[current] = 1;
		expansions++;
		if(current == targetIndex) {
			std::vector<EPoint> reversed;
			int cursor = current;
			while(cursor >= 0) {
				int cursorX = cursor % width;
				int cursorY = cursor / width;
				reversed.push_back(EPoint(cursorX, cursorY));
				if(cursor == startIndex)
					break;
				cursor = parent[cursor];
			}
			if(reversed.empty() or reversed.back().x != startX or reversed.back().y != startY)
				return false;
			outPath.assign(reversed.rbegin(), reversed.rend());
			return true;
		}
		if(expansions > kPathExpansionLimit)
			break;

		struct NeighborCandidate {
			int index;
			int x;
			int y;
			int heuristicScore;
			int affinity;
		};
		std::vector<NeighborCandidate> neighbors;
		neighbors.reserve(4);

		for(int i = 0; i < 4; i++) {
			const int nextX = currentX + offsetX[i];
			const int nextY = currentY + offsetY[i];
			if(nextX < 0 or nextX >= width or nextY < 0 or nextY >= height)
				continue;
			if(IsPathBlockedCell(area, nextX, nextY))
				continue;
			const int next = nextY * width + nextX;
			if(closedSet[next] != 0)
				continue;
			neighbors.push_back(NeighborCandidate{
				next,
				nextX,
				nextY,
				heuristic(nextX, nextY),
				PathAffinityScore(area, nextX, nextY)
			});
		}

		std::sort(neighbors.begin(), neighbors.end(), [] (const NeighborCandidate& a, const NeighborCandidate& b) {
			if(a.heuristicScore != b.heuristicScore)
				return a.heuristicScore < b.heuristicScore;
			if(a.affinity != b.affinity)
				return a.affinity > b.affinity;
			return a.index < b.index;
		});

		for(const NeighborCandidate& neighbor : neighbors) {
			const int next = neighbor.index;
			const int tentativeG = gScore[current] + 1;
			if(tentativeG >= gScore[next])
				continue;

			parent[next] = current;
			gScore[next] = tentativeG;
			openQueue.push(OpenNode{
				next,
				tentativeG + neighbor.heuristicScore,
				neighbor.heuristicScore,
				openSerial++
			});
		}
	}

	return false;
}

bool WorldMap::StartAutoPathTo (int targetX, int targetY) {
	if(selectedArea < 0 or selectedArea >= DRAGON_ALPHA_AREA_COUNT)
		return false;
	SetInteractionHint(-1, -1);
	autoPathRequestedX = std::max(0, std::min(DRAGON_ALPHA_MAP_WIDTH - 1, targetX));
	autoPathRequestedY = std::max(0, std::min(DRAGON_ALPHA_MAP_HEIGHT - 1, targetY));

	int resolvedX = 0;
	int resolvedY = 0;
	if(FindReachableDestination(selectedArea, mapX, mapY, targetX, targetY, resolvedX, resolvedY) == false)
		return false;

	std::vector<EPoint> path;
	if(FindPathAStar(selectedArea, mapX, mapY, resolvedX, resolvedY, path) == false)
		return false;

	autoPathTargetX = resolvedX;
	autoPathTargetY = resolvedY;
	autoPathAdjusted = (autoPathTargetX != autoPathRequestedX or autoPathTargetY != autoPathRequestedY);
	if(path.size() <= 1) {
		CancelAutoPath();
		return false;
	}

	autoPathCells = path;
	autoPathIndex = 1;
	autoPathNextMoveTime = GetMilliseconds();
	lastAutoPathRetargetAt = autoPathNextMoveTime;
	return true;
}

void WorldMap::AdvanceAutoPath () {
	if(autoPathIndex < 1 or autoPathIndex >= (int)autoPathCells.size())
		return;
	if(tutorialActive or endingActive or talkActive or promptActive or shopActive)
		return;

	int64_t now = GetMilliseconds();
	if(IsMoveAnimationActive(now))
		return;
	moveAnimating = false;
	if(now < autoPathNextMoveTime)
		return;

	if(mapX == autoPathTargetX and mapY == autoPathTargetY) {
		HandleAdjustedInteractionAtRouteEnd();
		CancelAutoPath();
		return;
	}

	const EPoint& nextCell = autoPathCells[(size_t)autoPathIndex];
	const int dx = nextCell.x - mapX;
	const int dy = nextCell.y - mapY;
	if(std::abs(dx) + std::abs(dy) != 1) {
		CancelAutoPath();
		return;
	}
	if(autoPathIndex == (int)autoPathCells.size() - 1) {
		const MapEventDefinition* destinationEvent = FindEvent(selectedArea, nextCell.x, nextCell.y, NULL);
		if(destinationEvent != NULL and IsRequirementMet(*destinationEvent, selectedArea) and IsEventComplete(*destinationEvent, selectedArea) == false) {
			if(ShouldAutoInteractAtAdjacentEvent(*destinationEvent, nextCell.x, nextCell.y)) {
				facingDX = dx < 0 ? -1 : (dx > 0 ? 1 : 0);
				facingDY = dy < 0 ? -1 : (dy > 0 ? 1 : 0);
				CancelAutoPath();
				if(TryInteractFacing())
					return;
				SetInteractionHint(nextCell.x, nextCell.y);
				SetRouteInteractionHintMessage(IsInteractOnlyEventType(destinationEvent->type));
				return;
			}
		}
	}

	const int beforeX = mapX;
	const int beforeY = mapY;
	MoveBy(dx, dy);
	if(mapX == beforeX and mapY == beforeY) {
		CancelAutoPath();
		return;
	}

	if(promptActive or talkActive or shopActive or tutorialActive or endingActive) {
		CancelAutoPath();
		return;
	}

	if(mapX == nextCell.x and mapY == nextCell.y) {
		autoPathIndex++;
		if(autoPathIndex >= (int)autoPathCells.size()) {
			HandleAdjustedInteractionAtRouteEnd();
			CancelAutoPath();
			return;
		}
	}

	autoPathNextMoveTime = now + MAP_TOUCH_PATH_STEP_MS;
}

bool WorldMap::HandleAdjustedInteractionAtRouteEnd () {
	if(autoPathRequestedX < 0 or autoPathRequestedX >= DRAGON_ALPHA_MAP_WIDTH or autoPathRequestedY < 0 or autoPathRequestedY >= DRAGON_ALPHA_MAP_HEIGHT)
		return false;

	const MapEventDefinition* requestedEvent = FindEvent(selectedArea, autoPathRequestedX, autoPathRequestedY, NULL);
	if(requestedEvent == NULL)
		return false;
	if(IsRequirementMet(*requestedEvent, selectedArea) == false or IsEventComplete(*requestedEvent, selectedArea))
		return false;

	const int distance = std::abs(autoPathRequestedX - mapX) + std::abs(autoPathRequestedY - mapY);
	if(distance != 1)
		return false;

	facingDX = autoPathRequestedX < mapX ? -1 : (autoPathRequestedX > mapX ? 1 : 0);
	facingDY = autoPathRequestedY < mapY ? -1 : (autoPathRequestedY > mapY ? 1 : 0);
	if(TryInteractFacing())
		return true;
	SetInteractionHint(autoPathRequestedX, autoPathRequestedY);
	SetRouteInteractionHintMessage(IsInteractOnlyEventType(requestedEvent->type));
	return true;
}

void WorldMap::CancelAutoPath () {
	autoPathCells.clear();
	autoPathIndex = 0;
	autoPathTargetX = mapX;
	autoPathTargetY = mapY;
	autoPathRequestedX = mapX;
	autoPathRequestedY = mapY;
	autoPathAdjusted = false;
	autoPathNextMoveTime = 0;
}

bool WorldMap::IsMoveAnimationActive (int64_t nowMs) const {
	return moveAnimating and nowMs < moveAnimationEndMs;
}

void WorldMap::StartMoveAnimation (int fromX, int fromY, int toX, int toY) {
	const int dx = toX - fromX;
	const int dy = toY - fromY;
	if(std::abs(dx) + std::abs(dy) != 1) {
		moveAnimating = false;
		return;
	}

	moveAnimating = true;
	moveFromX = fromX;
	moveFromY = fromY;
	moveToX = toX;
	moveToY = toY;
	moveAnimationStartMs = GetMilliseconds();
	moveAnimationEndMs = moveAnimationStartMs + MAP_MOVE_ANIMATION_MS;
}

void WorldMap::SetInteractionHint (int hintMapX, int hintMapY) {
	if(hintMapX < 0 or hintMapX >= DRAGON_ALPHA_MAP_WIDTH or hintMapY < 0 or hintMapY >= DRAGON_ALPHA_MAP_HEIGHT) {
		interactHintX = -1;
		interactHintY = -1;
		interactHintUntil = 0;
		return;
	}
	interactHintX = hintMapX;
	interactHintY = hintMapY;
	interactHintUntil = GetMilliseconds() + MAP_INTERACTION_HINT_PERSIST_MS;
}

bool WorldMap::TriggerEventAt (int x, int y, bool fromMove) {
	int eventIndex = -1;
	const MapEventDefinition* eventDefinition = FindEvent(selectedArea, x, y, &eventIndex);
	if(eventDefinition == NULL)
		return false;

	if(IsEventComplete(*eventDefinition, selectedArea)) {
		if(eventDefinition->type == MAP_EVENT_ITEM_GATE)
			SetMessage("This passage is already unlocked.");
		else if(eventDefinition->type == MAP_EVENT_TREASURE)
			SetMessage("The cache is empty.");
		else if(eventDefinition->type == MAP_EVENT_BATTLE_GATE)
			SetMessage("The gate guardian is already defeated.");
		else if(eventDefinition->type == MAP_EVENT_BATTLE_CHALLENGE)
			SetMessage("This trial has already been conquered.");
		else if(eventDefinition->type == MAP_EVENT_WARP) {
			if(selectedArea == DRAGON_AREA_PEAK)
				SetMessage("The summit title is already yours.");
			else
				SetMessage("This path has already been secured.");
		}
		return true;
	}

	if(IsRequirementMet(*eventDefinition, selectedArea) == false) {
		if(eventDefinition->type == MAP_EVENT_WARP)
			SetMessage("Route blocked. Defeat this area's guardian first.");
		else if(eventDefinition->type == MAP_EVENT_BATTLE_CHALLENGE) {
			if(eventDefinition->requirementFlagBit == DRAGON_EVENT_FLAG_CHALLENGE_ONE)
				SetMessage("Trial II is sealed until Trial I is conquered.");
			else
				SetMessage("This trial is sealed until the gate guardian falls.");
		}
		else
			SetMessage("The gate guardian still blocks this route.");
		return true;
	}

	return ExecuteEventScript(selectedArea, eventIndex, 0, fromMove);
}

void WorldMap::MoveBy (int dx, int dy) {
	if(dx != 0 or dy != 0) {
		facingDX = dx < 0 ? -1 : (dx > 0 ? 1 : 0);
		facingDY = dy < 0 ? -1 : (dy > 0 ? 1 : 0);
	}

	int nextX = mapX + dx;
	int nextY = mapY + dy;
	const int beforeArea = selectedArea;
	const int beforeX = mapX;
	const int beforeY = mapY;
	if(nextX < 0 or nextX >= DRAGON_ALPHA_MAP_WIDTH or nextY < 0 or nextY >= DRAGON_ALPHA_MAP_HEIGHT) {
		CancelAutoPath();
		moveAnimating = false;
		if(TryApplyLegacyBoundaryWarp(nextX, nextY, true)) {
			if(selectedArea != beforeArea or mapX != beforeX or mapY != beforeY) {
				SaveProgress();
			}
			else
				PlayBumpSound();
			return;
		}
		SetMessageThrottled("The terrain blocks your path.", MAP_INTERACTION_MISS_MESSAGE_THROTTLE_MS);
		PlayBumpSound();
		return;
	}
	if(TryApplyLegacyWarpAtCell(nextX, nextY, true)) {
		CancelAutoPath();
		moveAnimating = false;
		if(selectedArea != beforeArea or mapX != beforeX or mapY != beforeY) {
			SaveProgress();
		}
		else
			PlayBumpSound();
		return;
	}
	if(IsBlockedCell(selectedArea, nextX, nextY)) {
		CancelAutoPath();
		moveAnimating = false;
		if(TriggerEventAt(nextX, nextY, true)) {
			if(promptActive == false and talkActive == false)
				SaveProgress();
			return;
		}
		SetMessageThrottled("You cannot pass there.", MAP_INTERACTION_MISS_MESSAGE_THROTTLE_MS);
		PlayBumpSound();
		return;
	}

	mapX = nextX;
	mapY = nextY;
	StartMoveAnimation(beforeX, beforeY, mapX, mapY);
	SetInteractionHint(-1, -1);
	stepsSinceRecovery++;
	if(stepsSinceRecovery >= 4 and gSave.health < DragonGetMaxHealth()) {
		gSave.health = (int16_t)std::min(DragonGetMaxHealth(), (int)gSave.health + 1);
		stepsSinceRecovery = 0;
	}
	PlayStepSound();
	SaveProgress();
	TryRandomBattle();
}

bool WorldMap::TryInteractFacing () {
	const int hintedX = interactHintX;
	const int hintedY = interactHintY;
	SetInteractionHint(-1, -1);
	int frontX = mapX + facingDX;
	int frontY = mapY + facingDY;
	if(frontX >= 0 and frontX < DRAGON_ALPHA_MAP_WIDTH and frontY >= 0 and frontY < DRAGON_ALPHA_MAP_HEIGHT) {
		const int beforeArea = selectedArea;
		const int beforeX = mapX;
		const int beforeY = mapY;
		if(TryApplyLegacyWarpAtCell(frontX, frontY, false)) {
			if((selectedArea != beforeArea or mapX != beforeX or mapY != beforeY) and promptActive == false and talkActive == false and shopActive == false)
				SaveProgress();
			return true;
		}
		if(TriggerEventAt(frontX, frontY, false)) {
			if(promptActive == false and talkActive == false and shopActive == false)
				SaveProgress();
			return true;
		}
	}
	const int beforeArea = selectedArea;
	const int beforeX = mapX;
	const int beforeY = mapY;
	if(TryApplyLegacyWarpAtCell(mapX, mapY, false)) {
		if((selectedArea != beforeArea or mapX != beforeX or mapY != beforeY) and promptActive == false and talkActive == false and shopActive == false)
			SaveProgress();
		return true;
	}

	if(TriggerEventAt(mapX, mapY, false)) {
		if(promptActive == false and talkActive == false and shopActive == false)
			SaveProgress();
		return true;
	}

	if(hintedX >= 0 and hintedX < DRAGON_ALPHA_MAP_WIDTH and hintedY >= 0 and hintedY < DRAGON_ALPHA_MAP_HEIGHT) {
		const int hintDistance = std::abs(hintedX - mapX) + std::abs(hintedY - mapY);
		if(hintDistance <= 1 and (hintedX != frontX or hintedY != frontY) and (hintedX != mapX or hintedY != mapY)) {
			if(TriggerEventAt(hintedX, hintedY, false)) {
				if(promptActive == false and talkActive == false and shopActive == false)
					SaveProgress();
				return true;
			}
		}
	}

	// If facing/current/hint misses, still allow ACT to talk to nearby NPCs and
	// other interact-only events adjacent to the player.
	static const int fallbackDX[4] = {0, 0, -1, 1};
	static const int fallbackDY[4] = {-1, 1, 0, 0};
	for(int i = 0; i < 4; i++) {
		const int adjacentX = mapX + fallbackDX[i];
		const int adjacentY = mapY + fallbackDY[i];
		if(adjacentX < 0 or adjacentX >= DRAGON_ALPHA_MAP_WIDTH or adjacentY < 0 or adjacentY >= DRAGON_ALPHA_MAP_HEIGHT)
			continue;

		int adjacentEventIndex = -1;
		const MapEventDefinition* adjacentEvent = FindEvent(selectedArea, adjacentX, adjacentY, &adjacentEventIndex);
		if(adjacentEvent == NULL)
			continue;
		if(IsRequirementMet(*adjacentEvent, selectedArea) == false or IsEventComplete(*adjacentEvent, selectedArea))
			continue;
		if(IsInteractOnlyEventType(adjacentEvent->type) == false)
			continue;

		facingDX = fallbackDX[i];
		facingDY = fallbackDY[i];
		if(TriggerEventAt(adjacentX, adjacentY, false)) {
			if(promptActive == false and talkActive == false and shopActive == false)
				SaveProgress();
			return true;
		}
	}

	SetMessageThrottled("There is nothing here.", MAP_INTERACTION_MISS_MESSAGE_THROTTLE_MS);
	return false;
}

void WorldMap::DrawLegacyMapObjectOverlays () {
	const DragonLegacyMapScriptData* mapData = LegacyScriptMapDataForArea(selectedArea);
	if(mapData == NULL or mapData->objects == NULL or mapData->objectCount <= 0)
		return;

	static std::unordered_map<int, std::unique_ptr<EImage>> sLegacyDrawCache;
	auto resolveLegacyDrawImage = [&] (uint16_t pluginId, uint16_t drawId) -> EImage* {
		if(drawId < 128 or drawId > 4096)
			return NULL;

		const int cacheKey = (((int)pluginId & 0xFFFF) << 16) | ((int)drawId & 0xFFFF);
		auto cached = sLegacyDrawCache.find(cacheKey);
		if(cached != sLegacyDrawCache.end()) {
			if(cached->second == NULL)
				return NULL;
			return cached->second.get();
		}

		std::unique_ptr<EImage> image(new EImage());
		bool loaded = false;
		if(pluginId != 0) {
			EString pluginDrawName = EString().Format("LegacyMapDrawP%dD%d", (int)pluginId, (int)drawId);
			loaded = image->New((const char*)pluginDrawName);
		}
		if(loaded == false) {
			EString genericDrawName = EString().Format("LegacyMapDraw%d", (int)drawId);
			loaded = image->New((const char*)genericDrawName);
		}

		// Preserve legacy parity: skip unknown draw IDs instead of rendering synthetic placeholders.
		if(loaded == false or image->GetWidth() < 8 or image->GetHeight() < 8) {
			sLegacyDrawCache.emplace(cacheKey, nullptr);
			return NULL;
		}

		auto inserted = sLegacyDrawCache.emplace(cacheKey, std::move(image));
		return inserted.first->second.get();
	};

	for(int i = 0; i < mapData->objectCount; i++) {
		const DragonLegacyMapObject& object = mapData->objects[i];
		if(object.type != 3)
			continue;
		if(object.data0 != 0 and DragonGetLegacyFlag(object.data0))
			continue;

		const int mapDrawX = std::max(0, std::min((int)object.data2, DRAGON_ALPHA_MAP_WIDTH - 1));
		const int mapDrawY = std::max(0, std::min((int)object.data3, DRAGON_ALPHA_MAP_HEIGHT - 1));
		EImage* image = resolveLegacyDrawImage(mapData->pluginId, object.data1);
		if(image == NULL)
			continue;

		// Legacy parity: map draw objects are placed at (tile * 32) with their
		// authored sprite pixel dimensions, not constrained to a single tile cell.
		const int drawX = mapTileFrameRect.x + (mapDrawX - visibleStartX) * mapTileSize;
		const int drawY = mapTileFrameRect.y + (mapDrawY - visibleStartY) * mapTileSize;
		const int drawW = std::max(1, (image->GetWidth() * mapTileSize + 16) / 32);
		const int drawH = std::max(1, (image->GetHeight() * mapTileSize + 16) / 32);
		ERect drawRect(drawX, drawY, drawW, drawH);

		// Skip fully offscreen draws to keep parity behavior without extra overdraw.
		if(drawRect.x + drawRect.width <= mapTileFrameRect.x
			or drawRect.y + drawRect.height <= mapTileFrameRect.y
			or drawRect.x >= mapTileFrameRect.x + mapTileFrameRect.width
			or drawRect.y >= mapTileFrameRect.y + mapTileFrameRect.height) {
			continue;
		}
		image->DrawRect(drawRect);
	}
}

void WorldMap::UpdateLayoutRects () {
	const LegacyCanvas canvas = MakeLegacyCanvasInPreferredView(*this, 800, 600);
	for(int i = 0; i < DRAGON_ALPHA_AREA_COUNT; i++)
		areaRect[i] = ERect();

	// Legacy left-column hotspots.
	menuRect = LegacyRect(canvas, 8, 230, 148, 30);      // Open Game...
	saveRect = LegacyRect(canvas, 8, 271, 148, 30);      // Save Game
	statusRect = LegacyRect(canvas, 8, 329, 148, 30);    // Equipment
	actionRect = LegacyRect(canvas, 8, 370, 148, 30);    // Magic (interact first, then command menu)
	battleRect = LegacyRect(canvas, 8, 411, 148, 30);    // Techniques
	dpadUpRect = LegacyRect(canvas, 8, 510, 148, 30);    // Help
	dpadDownRect = LegacyRect(canvas, 8, 568, 148, 30);  // Quit
	dpadLeftRect = ERect();
	dpadRightRect = ERect();

	gridFrameRect = LegacyRect(canvas, 128, 88, 672, 512);
	mapX = std::max(0, std::min(DRAGON_ALPHA_MAP_WIDTH - 1, mapX));
	mapY = std::max(0, std::min(DRAGON_ALPHA_MAP_HEIGHT - 1, mapY));
	visibleCols = std::min(DRAGON_ALPHA_MAP_WIDTH, MAP_VIEW_COLS);
	visibleRows = std::min(DRAGON_ALPHA_MAP_HEIGHT, MAP_VIEW_ROWS);
	const int maxVisibleStartX = std::max(0, DRAGON_ALPHA_MAP_WIDTH - visibleCols);
	const int maxVisibleStartY = std::max(0, DRAGON_ALPHA_MAP_HEIGHT - visibleRows);
	const int focusCol = std::max(0, std::min(MAP_VIEW_FOCUS_COL, visibleCols - 1));
	const int focusRow = std::max(0, std::min(MAP_VIEW_FOCUS_ROW, visibleRows - 1));
	visibleStartX = std::max(0, std::min(maxVisibleStartX, mapX - focusCol));
	visibleStartY = std::max(0, std::min(maxVisibleStartY, mapY - focusRow));

	const int tileWidth = std::max(1, gridFrameRect.width / visibleCols);
	const int tileHeight = std::max(1, gridFrameRect.height / visibleRows);
	const int tileSize = std::max(1, std::min(tileWidth, tileHeight));
	const int mapDrawWidth = tileSize * visibleCols;
	const int mapDrawHeight = tileSize * visibleRows;
	const ERect tileFrameRectBase(
		gridFrameRect.x + (gridFrameRect.width - mapDrawWidth) / 2,
		gridFrameRect.y + (gridFrameRect.height - mapDrawHeight) / 2,
		mapDrawWidth,
		mapDrawHeight
	);

	int64_t nowMs = GetMilliseconds();
	int mapAnimationOffsetX = 0;
	int mapAnimationOffsetY = 0;
	if(IsMoveAnimationActive(nowMs)) {
		const int64_t duration = std::max<int64_t>(1, moveAnimationEndMs - moveAnimationStartMs);
		const double t = std::min(1.0, std::max(0.0, (double)(nowMs - moveAnimationStartMs) / (double)duration));
		const double remaining = 1.0 - t;
		mapAnimationOffsetX = (int)std::lround((double)(moveToX - moveFromX) * (double)tileSize * remaining);
		mapAnimationOffsetY = (int)std::lround((double)(moveToY - moveFromY) * (double)tileSize * remaining);
	}
	else {
		moveAnimating = false;
	}
	mapTileFrameRect = ERect(
		tileFrameRectBase.x + mapAnimationOffsetX,
		tileFrameRectBase.y + mapAnimationOffsetY,
		tileFrameRectBase.width,
		tileFrameRectBase.height
	);
	mapTileSize = tileSize;
}

void WorldMap::OnDraw () {
	const LegacyCanvas canvas = MakeLegacyCanvasInPreferredView(*this, 800, 600);
	const ERect safe = canvas.frame;
	const auto scaledW = [&canvas] (int value) {
		return std::max(1, LegacyRect(canvas, 0, 0, value, 0).width);
	};
	const auto scaledH = [&canvas] (int value) {
		return std::max(1, LegacyRect(canvas, 0, 0, 0, value).height);
	};
	DrawImageContain(imageBackground, safe);
	if(imageUiLegacy.IsEmpty() == false)
		DrawImageContain(imageUiLegacy, safe);
	AdvanceAutoPath();
	UpdateLayoutRects();
	EnsureAreaVisualLoaded();
	const int tileSize = mapTileSize;
	const ERect tileFrameRectAnimated = mapTileFrameRect;
	mapTileFrameRect = tileFrameRectAnimated;
	const int64_t nowMs = GetMilliseconds();

	ERect mapSourceRect = activeMapSourceRect;
	if(mapSourceRect.width <= 0 or mapSourceRect.height <= 0)
		mapSourceRect = ERect(0, 0, std::max(1, imageAreaMap.GetWidth()), std::max(1, imageAreaMap.GetHeight()));
	const int mapSourceX = std::max(0, std::min(mapSourceRect.x, std::max(0, imageAreaMap.GetWidth() - 1)));
	const int mapSourceY = std::max(0, std::min(mapSourceRect.y, std::max(0, imageAreaMap.GetHeight() - 1)));
	const int mapSourceW = std::max(1, std::min(mapSourceRect.width, imageAreaMap.GetWidth() - mapSourceX));
	const int mapSourceH = std::max(1, std::min(mapSourceRect.height, imageAreaMap.GetHeight() - mapSourceY));
	const int srcX = mapSourceX + (visibleStartX * mapSourceW) / DRAGON_ALPHA_MAP_WIDTH;
	const int srcY = mapSourceY + (visibleStartY * mapSourceH) / DRAGON_ALPHA_MAP_HEIGHT;
	int srcW = (visibleCols * mapSourceW) / DRAGON_ALPHA_MAP_WIDTH;
	int srcH = (visibleRows * mapSourceH) / DRAGON_ALPHA_MAP_HEIGHT;
	srcW = std::max(1, std::min(srcW, mapSourceX + mapSourceW - srcX));
	srcH = std::max(1, std::min(srcH, mapSourceY + mapSourceH - srcY));
	const ERect mapSrcRect(srcX, srcY, srcW, srcH);
	imageAreaMap.Draw(mapSrcRect, tileFrameRectAnimated);

	// Legacy parity: draw-object sprites are composited on map pixels before map2 overlay.
	DrawLegacyMapObjectOverlays();

	for(int row = 0; row < visibleRows; row++) {
		int mapRow = visibleStartY + row;
		for(int col = 0; col < visibleCols; col++) {
			int mapCol = visibleStartX + col;
			tileRect[mapCol][mapRow] = ERect(tileFrameRectAnimated.x + col * tileSize, tileFrameRectAnimated.y + row * tileSize, tileSize, tileSize);
		}
	}

	const int playerInset = std::max(1, tileSize / 10);
	const int avatarInset = std::max(0, tileSize / 24);
	ERect playerTile = InsetRect(tileRect[mapX][mapY], playerInset);
	ERect mapAvatarRect = InsetRect(playerTile, avatarInset);
	if(imageMapAvatar.IsEmpty() == false and mapAvatarSourceRect.width > 0 and mapAvatarSourceRect.height > 0)
		imageMapAvatar.Draw(mapAvatarSourceRect, mapAvatarRect);
	else
		DrawImageContain(imageMapAvatar, mapAvatarRect);

	if(imageAreaOverlay.IsEmpty() == false) {
		ERect overlaySourceRect = activeOverlaySourceRect;
		if(overlaySourceRect.width <= 0 or overlaySourceRect.height <= 0)
			overlaySourceRect = ERect(0, 0, std::max(1, imageAreaOverlay.GetWidth()), std::max(1, imageAreaOverlay.GetHeight()));
		const int overlaySourceX = std::max(0, std::min(overlaySourceRect.x, std::max(0, imageAreaOverlay.GetWidth() - 1)));
		const int overlaySourceY = std::max(0, std::min(overlaySourceRect.y, std::max(0, imageAreaOverlay.GetHeight() - 1)));
		const int overlaySourceW = std::max(1, std::min(overlaySourceRect.width, imageAreaOverlay.GetWidth() - overlaySourceX));
		const int overlaySourceH = std::max(1, std::min(overlaySourceRect.height, imageAreaOverlay.GetHeight() - overlaySourceY));
		const int overlaySrcX = overlaySourceX + (visibleStartX * overlaySourceW) / DRAGON_ALPHA_MAP_WIDTH;
		const int overlaySrcY = overlaySourceY + (visibleStartY * overlaySourceH) / DRAGON_ALPHA_MAP_HEIGHT;
		int overlaySrcW = (visibleCols * overlaySourceW) / DRAGON_ALPHA_MAP_WIDTH;
		int overlaySrcH = (visibleRows * overlaySourceH) / DRAGON_ALPHA_MAP_HEIGHT;
		overlaySrcW = std::max(1, std::min(overlaySrcW, overlaySourceX + overlaySourceW - overlaySrcX));
		overlaySrcH = std::max(1, std::min(overlaySrcH, overlaySourceY + overlaySourceH - overlaySrcY));
		imageAreaOverlay.Draw(ERect(overlaySrcX, overlaySrcY, overlaySrcW, overlaySrcH), tileFrameRectAnimated, EColor::WHITE);
	}

		if(autoPathIndex >= 1 and autoPathIndex < (int)autoPathCells.size()) {
			const EColor targetColor = autoPathAdjusted ? EColor(0xffffd8ba) : EColor(0xffffffe6);
			if(MAP_TOUCH_ROUTE_BREADCRUMB_OVERLAY) {
				const EColor breadcrumbColor = autoPathAdjusted ? EColor(0xffffe0c8) : EColor(0xffffffe6);
				const int breadcrumbLimit = std::min((int)autoPathCells.size(), autoPathIndex + 64);
				const int breadcrumbInset = std::max(1, tileSize / 4);
				for(int i = autoPathIndex; i < breadcrumbLimit; i++) {
					const EPoint& step = autoPathCells[(size_t)i];
				if(step.x < 0 or step.x >= DRAGON_ALPHA_MAP_WIDTH or step.y < 0 or step.y >= DRAGON_ALPHA_MAP_HEIGHT)
					continue;
				if(step.x < visibleStartX or step.x >= visibleStartX + visibleCols or step.y < visibleStartY or step.y >= visibleStartY + visibleRows)
					continue;
				ERect breadcrumbRect = InsetRect(tileRect[step.x][step.y], breadcrumbInset);
				DrawImageContain(imageGridPlayer, breadcrumbRect, breadcrumbColor);
			}
		}

		if(autoPathTargetX >= 0 and autoPathTargetX < DRAGON_ALPHA_MAP_WIDTH and autoPathTargetY >= 0 and autoPathTargetY < DRAGON_ALPHA_MAP_HEIGHT) {
			if(autoPathTargetX >= visibleStartX and autoPathTargetX < visibleStartX + visibleCols and autoPathTargetY >= visibleStartY and autoPathTargetY < visibleStartY + visibleRows) {
				const int targetInset = std::max(0, tileSize / 8);
				ERect targetRect = InsetRect(tileRect[autoPathTargetX][autoPathTargetY], targetInset);
				DrawImageContain(imageAreaSelected, targetRect, targetColor);
			}
		}
	}
	if(interactHintUntil > nowMs and interactHintX >= 0 and interactHintX < DRAGON_ALPHA_MAP_WIDTH and interactHintY >= 0 and interactHintY < DRAGON_ALPHA_MAP_HEIGHT) {
		if(interactHintX >= visibleStartX and interactHintX < visibleStartX + visibleCols and interactHintY >= visibleStartY and interactHintY < visibleStartY + visibleRows) {
			const int hintInset = std::max(0, tileSize / 8);
			ERect hintRect = InsetRect(tileRect[interactHintX][interactHintY], hintInset);
			DrawImageContain(imageAreaSelected, hintRect, EColor(0xfffff4f2));
		}
	}

	// Legacy map-mode UI overlays clamp map art at authored boundaries.
	if(imageUiOverlay1Legacy.IsEmpty() == false) {
		ERect leftOverlayRect = LegacyRect(canvas, 128, 88, 32, 512);
		DrawImageContain(imageUiOverlay1Legacy, leftOverlayRect);
	}
	if(imageUiOverlay2Legacy.IsEmpty() == false) {
		ERect topOverlayRect = LegacyRect(canvas, 160, 88, 640, 16);
		DrawImageContain(imageUiOverlay2Legacy, topOverlayRect);
	}

	bool showMessage = messageText.IsEmpty() == false and nowMs - messageTimestamp < 3200;
	const DragonLegacyMapWarpData* mapData = DragonFindLegacyMapWarpData(legacyPluginIdByArea[selectedArea], legacyMapIdByArea[selectedArea]);
	const char* mapName = (mapData != NULL and mapData->mapName != NULL and mapData->mapName[0] != '\0')
		? mapData->mapName
		: kAreaShortName[selectedArea];
	const char* heroName = gSave.name[0] == '\0' ? "HERO" : gSave.name;
	const int maxHealth = DragonGetMaxHealth();

	ERect mapNameRect = ClipRectToBounds(LegacyRect(canvas, 315, 42, 250, 20), safe);
	ERect heroNameRect = ClipRectToBounds(LegacyRect(canvas, 18, 140, 101, 15), safe);
	ERect heroLevelRect = ClipRectToBounds(LegacyRect(canvas, 18, 155, 101, 15), safe);
	ERect heroHealthRect = ClipRectToBounds(LegacyRect(canvas, 18, 170, 101, 15), safe);
	const EString mapLabel = EString().Format("Map: %s", mapName);
	const EString positionLabel = EString().Format("Position: %d,%d", mapX, mapY);
	const EString heroLabel = EString().Format("Hero: %s", heroName);
	const EString levelLabel = EString().Format("Lv %d", std::max(1, (int)gSave.level));
	const EString healthLabel = EString().Format("HP %d/%d", (int)gSave.health, maxHealth);
	const EString goldLabel = EString().Format("Gold %d", gSave.gold);
	const EString playerTileLabel = EString().Format("Player tile: %d,%d", mapX, mapY);

#if defined(DRAGON_TEST)
	ReportValidationLabel(mapLabel, mapNameRect);
	ReportValidationLabel(positionLabel, mapNameRect);
	ReportValidationLabel(heroLabel, heroNameRect);
	ReportValidationLabel(levelLabel, heroLevelRect);
	ReportValidationLabel(healthLabel, heroHealthRect);
	ReportValidationLabel(goldLabel, heroHealthRect);
	ReportValidationLabel("Map avatar bounds", mapAvatarRect);
	ReportValidationLabel("OPEN GAME...", menuRect);
	ReportValidationLabel("SAVE GAME", saveRect);
	ReportValidationLabel("EQUIPMENT", statusRect);
	ReportValidationLabel("INTERACT", actionRect);
	ReportValidationLabel("BATTLE", battleRect);
	ReportValidationLabel(playerTileLabel, playerTile);
	const int eventCount = EventCountForArea(selectedArea);
	int pendingEventReportCount = 0;
	for(int eventIndex = 0; eventIndex < eventCount; eventIndex++) {
		const MapEventDefinition* eventDefinition = EventDefinitionAt(selectedArea, eventIndex);
		if(eventDefinition == NULL or eventDefinition->type == MAP_EVENT_NONE)
			continue;
		if(IsRequirementMet(*eventDefinition, selectedArea) == false or IsEventComplete(*eventDefinition, selectedArea))
			continue;
		int eventX = 0;
		int eventY = 0;
		if(ResolveEventPositionAtIndex(selectedArea, eventIndex, eventX, eventY) == false)
			continue;
		if(pendingEventReportCount < 20) {
			ReportValidationLabel(
				EString().Format("Pending event %d: %s %d,%d", eventIndex, ValidationEventTypeLabel(eventDefinition->type), eventX, eventY),
				ERect(mapNameRect.x, mapNameRect.y + pendingEventReportCount * 14, std::max(160, mapNameRect.width), 14)
			);
			pendingEventReportCount++;
		}
		if(eventX < visibleStartX or eventX >= visibleStartX + visibleCols or eventY < visibleStartY or eventY >= visibleStartY + visibleRows)
			continue;
		ReportValidationLabel(
			EString().Format("Event %d: %s %d,%d", eventIndex, ValidationEventTypeLabel(eventDefinition->type), eventX, eventY),
			InsetRect(tileRect[eventX][eventY], std::max(1, tileSize / 6))
		);
	}
	if(autoPathIndex >= 1 and autoPathIndex < (int)autoPathCells.size()) {
		ReportValidationLabel(EString().Format("Route target: %d,%d", autoPathTargetX, autoPathTargetY), mapNameRect);
		ReportValidationLabel(EString().Format("Route requested: %d,%d", autoPathRequestedX, autoPathRequestedY), heroNameRect);
	}
	if(interactHintUntil > nowMs and interactHintX >= 0 and interactHintX < DRAGON_ALPHA_MAP_WIDTH and interactHintY >= 0 and interactHintY < DRAGON_ALPHA_MAP_HEIGHT)
		ReportValidationLabel(EString().Format("Interaction hint: %d,%d", interactHintX, interactHintY), playerTile);
	if(DragonAutomationCaseIs("Validation_SaveLoad_ProgressionState")
		|| DragonAutomationCaseIs("Validation_SaveLoad_LaterGameLoadoutState")) {
		ReportValidationLabel(EString().Format("Discovered mask: 0x%02X", (unsigned int)gSave.discoveredAreaMask), mapNameRect);
		ReportValidationLabel(EString().Format("Area %d flags: 0x%04X", selectedArea, (unsigned int)gWorldState.areaEventFlags[selectedArea]), heroNameRect);
		ReportValidationLabel(EString().Format("Area %d flags: 0x%04X", DRAGON_AREA_PEAK, (unsigned int)gWorldState.areaEventFlags[DRAGON_AREA_PEAK]), heroLevelRect);
	}
#endif
	if(not fontMain.IsEmpty()) {
		DrawCenteredLabel(fontMain, mapName, mapNameRect);
		DrawCenteredLabel(fontMain, heroName, heroNameRect);
		DrawCenteredLabel(fontMain, levelLabel, heroLevelRect);
		DrawCenteredLabel(fontMain, healthLabel, heroHealthRect);
	}

	if(saveBannerTimer != 0 and nowMs - saveBannerTimer < 1000 and not fontMain.IsEmpty())
		fontMain.Draw("Saved.", LegacyRect(canvas, 735, 38, 60, 16).x, LegacyRect(canvas, 735, 38, 60, 16).y);
#if defined(DRAGON_TEST)
	if(saveBannerTimer != 0 and nowMs - saveBannerTimer < 1000)
		ReportValidationLabel("Saved.", LegacyRect(canvas, 735, 38, 60, 16));
#endif

	if(showMessage) {
		ERect messageRect = ClipRectToBounds(LegacyRect(canvas, 186, 504, 570, 46), safe);
		imagePanel.DrawRect(messageRect);
#if defined(DRAGON_TEST)
		ReportValidationLabel(messageText, InsetRect(messageRect, 6));
#endif
		if(not fontMain.IsEmpty())
			DrawWrappedLabel(fontMain, messageText, InsetRect(messageRect, 6), 2);
	}

	if(promptActive) {
		const int panelMarginX = scaledW(20);
		const int panelMarginY = scaledH(20);
		const int panelWidth = std::max(scaledW(240), std::min(safe.width - panelMarginX * 2, scaledW(360)));
		const int panelHeight = std::max(scaledH(108), std::min(safe.height - panelMarginY * 2, scaledH(148)));
		promptRect = ClipRectToBounds(ERect(safe.x + (safe.width - panelWidth) / 2, safe.y + (safe.height - panelHeight) / 2, panelWidth, panelHeight), safe);
		const int buttonInsetX = scaledW(16);
		const int buttonGap = scaledW(8);
		const int buttonHeight = scaledH(34);
		const int buttonBottomInset = scaledH(16);
		const int buttonWidth = std::max(scaledW(80), (promptRect.width - buttonInsetX * 2 - buttonGap) / 2);
		promptYesRect = ERect(promptRect.x + buttonInsetX, promptRect.y + promptRect.height - buttonBottomInset - buttonHeight, buttonWidth, buttonHeight);
		promptNoRect = ERect(promptYesRect.x + promptYesRect.width + buttonGap, promptYesRect.y, buttonWidth, buttonHeight);

		imagePanel.DrawRect(promptRect);
		imageSaveButton.DrawRect(promptYesRect);
		imageMenuButton.DrawRect(promptNoRect);
		const int textInsetX = scaledW(14);
		const int textInsetY = scaledH(14);
		ERect textRect(promptRect.x + textInsetX, promptRect.y + textInsetY, std::max(1, promptRect.width - textInsetX * 2), std::max(1, promptRect.height - textInsetY - buttonHeight - buttonBottomInset - scaledH(10)));
#if defined(DRAGON_TEST)
		ReportValidationLabel(EString().Format("Prompt: %s", (const char*)promptText), textRect);
		ReportValidationLabel("YES", promptYesRect);
		ReportValidationLabel("NO", promptNoRect);
#endif
		if(not fontMain.IsEmpty()) {
			DrawWrappedLabel(fontMain, promptText, textRect);
			DrawCenteredLabel(fontMain, "YES", promptYesRect);
			DrawCenteredLabel(fontMain, "NO", promptNoRect);
		}
	}

	if(talkActive) {
		const int panelMarginX = scaledW(20);
		const int panelMarginY = scaledH(20);
		const int panelWidth = std::max(scaledW(280), std::min(safe.width - panelMarginX * 2, scaledW(420)));
		const int panelHeight = std::max(scaledH(124), std::min(safe.height - panelMarginY * 2, scaledH(158)));
		talkRect = ClipRectToBounds(ERect(safe.x + (safe.width - panelWidth) / 2, safe.y + (safe.height - panelHeight) / 2, panelWidth, panelHeight), safe);
		const int talkButtonWidth = scaledW(128);
		const int talkButtonHeight = scaledH(34);
		const int talkButtonBottomInset = scaledH(12);
		talkOkRect = ERect(talkRect.x + (talkRect.width - talkButtonWidth) / 2, talkRect.y + talkRect.height - talkButtonBottomInset - talkButtonHeight, talkButtonWidth, talkButtonHeight);
		imagePanel.DrawRect(talkRect);
		imageSaveButton.DrawRect(talkOkRect);
		const int textInsetX = scaledW(14);
		const int textInsetY = scaledH(14);
		ERect textRect(talkRect.x + textInsetX, talkRect.y + textInsetY, std::max(1, talkRect.width - textInsetX * 2), std::max(1, talkRect.height - textInsetY - talkButtonHeight - talkButtonBottomInset - scaledH(8)));
#if defined(DRAGON_TEST)
		ReportValidationLabel(EString().Format("Talk: %s", (const char*)talkText), textRect);
		ReportValidationLabel("CONTINUE", talkOkRect);
#endif
		if(not fontMain.IsEmpty()) {
			DrawWrappedLabel(fontMain, talkText, textRect, 3);
			DrawCenteredLabel(fontMain, "CONTINUE", talkOkRect);
		}
	}

	if(shopActive) {
		const int panelMarginX = scaledW(18);
		const int panelMarginY = scaledH(18);
		int panelWidth = std::max(scaledW(320), std::min(safe.width - panelMarginX * 2, scaledW(500)));
		int panelHeight = std::max(scaledH(256), std::min(safe.height - panelMarginY * 2, scaledH(396)));
		shopRect = ClipRectToBounds(ERect(safe.x + (safe.width - panelWidth) / 2, safe.y + (safe.height - panelHeight) / 2, panelWidth, panelHeight), safe);
		imagePanel.DrawRect(shopRect);
#if defined(DRAGON_TEST)
		ReportValidationLabel("Shop panel bounds", shopRect);
#endif

		const int headerLineHeight = not fontHeader.IsEmpty() ? LayoutLineHeight(fontHeader, 14, 32) : 20;
		const int titleInsetX = scaledW(12);
		const int titleInsetY = scaledH(12);
		ERect titleRect(shopRect.x + titleInsetX, shopRect.y + titleInsetY, std::max(1, shopRect.width - titleInsetX * 2), headerLineHeight + scaledH(2));
#if defined(DRAGON_TEST)
		ReportValidationLabel("MERCHANT", titleRect);
#endif
		if(not fontHeader.IsEmpty()) {
			DrawCenteredLabel(fontHeader, "MERCHANT", titleRect);
		}

		const int maxScroll = std::max(0, shopOfferCount - SHOP_VISIBLE_ROWS);
		shopScrollOffset = std::max(0, std::min(shopScrollOffset, maxScroll));

		const int rowGap = scaledH(6);
		const int footerTop = shopRect.y + shopRect.height - scaledH(54);
		const int rowY = shopRect.y + scaledH(44);
		const int rowsHeight = std::max(1, footerTop - rowY - rowGap * (SHOP_VISIBLE_ROWS - 1));
		const int rowHeight = std::max(scaledH(34), rowsHeight / SHOP_VISIBLE_ROWS);
		const int rowInsetX = scaledW(14);
		const int rowIconInsetX = scaledW(5);
		const int rowIconInsetY = scaledH(4);
		const int rowTextGapX = scaledW(8);
		const int rowTextRightInset = scaledW(6);
#if defined(DRAGON_TEST)
		ReportValidationLabel("Shop row sample", ERect(shopRect.x + rowInsetX, rowY, std::max(1, shopRect.width - rowInsetX * 2), rowHeight));
#endif

		for(int i = 0; i < SHOP_VISIBLE_ROWS; i++) {
			shopOfferRect[i] = ERect(shopRect.x + rowInsetX, rowY + i * (rowHeight + rowGap), std::max(1, shopRect.width - rowInsetX * 2), rowHeight);
			const int offerIndex = shopScrollOffset + i;
			if(offerIndex >= shopOfferCount) {
				imageGridBlocked.DrawRect(shopOfferRect[i], EColor(0xffffff8f));
				continue;
			}

			const ShopOffer& offer = shopOffers[offerIndex];
			const DragonItemInfo* info = DragonItemByType(offer.itemType);
			bool canAfford = gSave.gold >= offer.price;
			(canAfford ? imageSaveButton : imageMenuButton).DrawRect(shopOfferRect[i], canAfford ? EColor(0xffffffef) : EColor(0xffffffb4));

			const int iconSize = std::max(scaledW(20), shopOfferRect[i].height - rowIconInsetY * 2);
			ERect iconRect(shopOfferRect[i].x + rowIconInsetX, shopOfferRect[i].y + rowIconInsetY, iconSize, iconSize);
			if(offer.itemType > DRAGON_ITEM_NONE and offer.itemType < DRAGON_ITEM_COUNT)
				DrawImageContain(itemIcon[offer.itemType], iconRect);

			const char* name = info != NULL ? info->name : "Unknown";
			const char* note = (info != NULL and info->slot != DRAGON_SLOT_CONSUMABLE and HasItemTypeEquippedOrInBag(offer.itemType)) ? " (OWNED)" : "";
			const int textX = iconRect.x + iconRect.width + rowTextGapX;
			const int textWidth = std::max(1, shopOfferRect[i].x + shopOfferRect[i].width - textX - rowTextRightInset);
			const int lineHeight = not fontMain.IsEmpty() ? LayoutLineHeight(fontMain, 12, 24) : 14;
			const ERect nameRect(textX, shopOfferRect[i].y + rowIconInsetY, textWidth, lineHeight + scaledH(4));
			const ERect priceRect(textX, shopOfferRect[i].y + std::max(scaledH(12), shopOfferRect[i].height - lineHeight - scaledH(6)), textWidth, lineHeight + scaledH(4));
			const EString offerName = EString().Format("%s%s", name, note);
			const EString offerPrice = EString().Format("%d gold", offer.price);
#if defined(DRAGON_TEST)
			ReportValidationLabel(offerName, nameRect);
			ReportValidationLabel(offerPrice, priceRect);
#endif
			if(not fontMain.IsEmpty()) {
				DrawLeftClampedLabel(fontMain, offerName, nameRect, 0, 0);
				DrawLeftClampedLabel(fontMain, offerPrice, priceRect, 0, 0);
			}
		}

		const int footerButtonWidth = std::max(scaledW(96), std::min(scaledW(132), (shopRect.width - scaledW(52)) / 3));
		const int footerButtonHeight = scaledH(30);
		const int footerY = shopRect.y + shopRect.height - scaledH(40);
		shopPrevRect = ERect(shopRect.x + rowInsetX, footerY, footerButtonWidth, footerButtonHeight);
		shopCloseRect = ERect(shopRect.x + (shopRect.width - footerButtonWidth) / 2, footerY, footerButtonWidth, footerButtonHeight);
		shopNextRect = ERect(shopRect.x + shopRect.width - rowInsetX - footerButtonWidth, footerY, footerButtonWidth, footerButtonHeight);
		const bool canPrevPage = shopScrollOffset > 0;
		const bool canNextPage = (shopScrollOffset + SHOP_VISIBLE_ROWS) < shopOfferCount;
		(canPrevPage ? imageSaveButton : imageMenuButton).DrawRect(shopPrevRect, canPrevPage ? EColor(0xffffffef) : EColor(0xffffffb4));
		imageMenuButton.DrawRect(shopCloseRect);
		(canNextPage ? imageSaveButton : imageMenuButton).DrawRect(shopNextRect, canNextPage ? EColor(0xffffffef) : EColor(0xffffffb4));
		const int firstVisible = shopOfferCount <= 0 ? 0 : (shopScrollOffset + 1);
		const int lastVisible = std::min(shopOfferCount, shopScrollOffset + SHOP_VISIBLE_ROWS);
		ERect pageRect(shopCloseRect.x - scaledW(90), shopCloseRect.y - scaledH(18), shopCloseRect.width + scaledW(180), scaledH(14));
#if defined(DRAGON_TEST)
		ReportValidationLabel("PREV", shopPrevRect);
		ReportValidationLabel("DONE", shopCloseRect);
		ReportValidationLabel("NEXT", shopNextRect);
		ReportValidationLabel(EString().Format("Shop total offers: %d", shopOfferCount), ERect(pageRect.x, pageRect.y - scaledH(16), pageRect.width, scaledH(14)));
		ReportValidationLabel(EString().Format("Shop page offset: %d", shopScrollOffset), pageRect);
#endif
		if(not fontMain.IsEmpty()) {
			DrawCenteredLabel(fontMain, "PREV", shopPrevRect);
			DrawCenteredLabel(fontMain, "DONE", shopCloseRect);
			DrawCenteredLabel(fontMain, "NEXT", shopNextRect);
			DrawCenteredLabel(fontMain, EString().Format("%d-%d / %d", firstVisible, lastVisible, shopOfferCount), pageRect);
		}
	}

	if(endingActive) {
		imageGridBlocked.DrawRect(safe, EColor(0x000000cc));
		const int panelMarginX = scaledW(18);
		const int panelMarginY = scaledH(18);
		int panelWidth = std::max(scaledW(320), std::min(safe.width - panelMarginX * 2, scaledW(420)));
		int panelHeight = std::max(scaledH(176), std::min(safe.height - panelMarginY * 2, scaledH(232)));
		endingRect = ClipRectToBounds(ERect(safe.x + (safe.width - panelWidth) / 2, safe.y + (safe.height - panelHeight) / 2, panelWidth, panelHeight), safe);
		const int buttonInsetX = scaledW(18);
		const int buttonGap = scaledW(10);
		const int buttonHeight = scaledH(34);
		const int buttonBottomInset = scaledH(14);
		const int buttonWidth = std::max(scaledW(96), (endingRect.width - buttonInsetX * 2 - buttonGap) / 2);
		endingContinueRect = ERect(endingRect.x + buttonInsetX, endingRect.y + endingRect.height - buttonBottomInset - buttonHeight, buttonWidth, buttonHeight);
		endingMenuRect = ERect(endingContinueRect.x + endingContinueRect.width + buttonGap, endingContinueRect.y, buttonWidth, buttonHeight);
		imagePanel.DrawRect(endingRect);
		imageSaveButton.DrawRect(endingContinueRect);
		imageMenuButton.DrawRect(endingMenuRect);

		if(not fontHeader.IsEmpty()) {
			const int headerLineHeight = LayoutLineHeight(fontHeader, 14, 32);
			ERect titleRect(endingRect.x + scaledW(12), endingRect.y + scaledH(14), std::max(1, endingRect.width - scaledW(24)), headerLineHeight + scaledH(2));
			DrawCenteredLabel(fontHeader, "SUMMIT CLAIMED", titleRect);
		}
		if(not fontMain.IsEmpty()) {
			ERect textRect(endingRect.x + scaledW(16), endingRect.y + scaledH(48), std::max(1, endingRect.width - scaledW(32)), std::max(1, endingRect.height - scaledH(104)));
			DrawWrappedLabel(fontMain, EString().Format("Dragon Alpha complete. Lv %d, Gold %d, Wins %d.", (int)gSave.level, (int)gSave.gold, (int)gSave.battlesWon), textRect, 3);
			DrawCenteredLabel(fontMain, "CONTINUE", endingContinueRect);
			DrawCenteredLabel(fontMain, "MENU", endingMenuRect);
		}
	}

	if(tutorialActive) {
		imageGridBlocked.DrawRect(safe, EColor(0x000000cc));
		const int panelMarginX = scaledW(12);
		const int panelMarginY = scaledH(12);
		const int panelWidth = std::max(scaledW(420), std::min(safe.width - panelMarginX * 2, scaledW(672)));
		const int panelHeight = std::max(scaledH(320), std::min(safe.height - panelMarginY * 2, scaledH(512)));
		tutorialRect = ClipRectToBounds(ERect(safe.x + (safe.width - panelWidth) / 2, safe.y + (safe.height - panelHeight) / 2, panelWidth, panelHeight), safe);
		const int okButtonWidth = scaledW(140);
		const int okButtonHeight = scaledH(34);
		const int okButtonBottomInset = scaledH(12);
		tutorialOkRect = ERect(tutorialRect.x + (tutorialRect.width - okButtonWidth) / 2, tutorialRect.y + tutorialRect.height - okButtonBottomInset - okButtonHeight, okButtonWidth, okButtonHeight);
		imagePanel.DrawRect(tutorialRect);
		EImage* tutorialArt = imageHelpLegacy.IsEmpty() ? (imageTutorialLegacy.IsEmpty() ? NULL : &imageTutorialLegacy) : &imageHelpLegacy;
		if(tutorialArt != NULL) {
			ERect tutorialArtRect = InsetRect(tutorialRect, scaledW(8));
			DrawImageContain(*tutorialArt, tutorialArtRect, EColor(0xffffff66));
		}
		imageSaveButton.DrawRect(tutorialOkRect);
		if(not fontHeader.IsEmpty()) {
			const int headerLineHeight = LayoutLineHeight(fontHeader, 14, 32);
			ERect titleRect(tutorialRect.x + scaledW(14), tutorialRect.y + scaledH(14), std::max(1, tutorialRect.width - scaledW(28)), headerLineHeight + scaledH(2));
			DrawCenteredLabel(fontHeader, "MAP HELP", titleRect);
		}
		if(not fontMain.IsEmpty()) {
			ERect bodyRect(tutorialRect.x + scaledW(16), tutorialRect.y + scaledH(48), std::max(1, tutorialRect.width - scaledW(32)), std::max(1, tutorialRect.height - scaledH(100)));
			(void)bodyRect;
			DrawCenteredLabel(fontMain, "CONTINUE", tutorialOkRect);
		}
	}
}

void WorldMap::OnTouch (int x, int y) {
	EPoint input = NormalizeInputPoint(*this, x, y);
	x = input.x;
	y = input.y;
	const ERect inputBounds = MakePreferredViewRect(GetSafeRect(), DESIGN_PREFERRED_WIDTH, DESIGN_PREFERRED_HEIGHT);
	const ERect safeBounds = GetSafeRect();
	if(inputBounds.IsPointInRect(x, y) == false) {
		if(safeBounds.IsPointInRect(x, y) == false) {
			ResetSwipeTracking();
			touchHandledThisSequence = false;
			touchMovedThisSequence = false;
			return;
		}
	}
	touchHandledThisSequence = true;
	touchMovedThisSequence = false;
	UpdateLayoutRects();

	if(tutorialActive) {
		if(HitTouchRect(tutorialOkRect, x, y, 44, 44, 2)) {
			tutorialActive = false;
			PlaySoundIfLoaded(soundClick);
			SetMessage("Map help closed.");
		}
		return;
	}

	if(endingActive) {
		if(HitTouchRect(endingContinueRect, x, y, 44, 44, 2)) {
			endingActive = false;
			PlaySoundIfLoaded(soundClick);
			SetMessage("Continue your journey as summit champion.");
			return;
		}
		if(HitTouchRect(endingMenuRect, x, y, 44, 44, 2)) {
			endingActive = false;
			PlaySoundIfLoaded(soundClick);
			SaveProgress();
			RunNewNode("NewGame");
			return;
		}
		return;
	}

	if(talkActive) {
		if(HitTouchRect(talkOkRect, x, y, 44, 44, 2)) {
			PlaySoundIfLoaded(soundClick);
			ResolveTalkPrompt();
			return;
		}
		if(talkRect.IsPointInRect(x, y) == false) {
			PlaySoundIfLoaded(soundClick);
			ResolveTalkPrompt();
		}
		return;
	}

	if(promptActive) {
		if(HitTouchRect(promptYesRect, x, y, 44, 44, 2)) {
			PlaySoundIfLoaded(soundClick);
			ResolveConfirmPrompt(true);
			return;
		}
		if(HitTouchRect(promptNoRect, x, y, 44, 44, 2)) {
			PlaySoundIfLoaded(soundClick);
			ResolveConfirmPrompt(false);
			return;
		}
		if(promptRect.IsPointInRect(x, y) == false) {
			PlaySoundIfLoaded(soundClick);
			ResolveConfirmPrompt(false);
		}
		return;
	}

	if(shopActive) {
		const int maxScroll = std::max(0, shopOfferCount - SHOP_VISIBLE_ROWS);
		shopScrollOffset = std::max(0, std::min(shopScrollOffset, maxScroll));

			if(HitTouchRect(shopPrevRect, x, y, 44, 44, 2)) {
				if(shopScrollOffset > 0) {
					PlaySoundIfLoaded(soundClick);
					shopScrollOffset = std::max(0, shopScrollOffset - SHOP_VISIBLE_ROWS);
				}
				else
					PlayBumpSound();
				return;
			}
			if(HitTouchRect(shopNextRect, x, y, 44, 44, 2)) {
				if(shopScrollOffset + SHOP_VISIBLE_ROWS < shopOfferCount) {
					PlaySoundIfLoaded(soundClick);
					shopScrollOffset = std::min(maxScroll, shopScrollOffset + SHOP_VISIBLE_ROWS);
				}
				else
					PlayBumpSound();
				return;
			}
		if(HitTouchRect(shopCloseRect, x, y, 44, 44, 2)) {
			PlaySoundIfLoaded(soundClick);
			const bool resumedScript = ResolveShopPrompt();
			if(resumedScript == false)
				SetMessage("You leave the merchant.");
			return;
		}
		for(int i = 0; i < SHOP_VISIBLE_ROWS; i++) {
			const int offerIndex = shopScrollOffset + i;
			if(offerIndex >= shopOfferCount)
				break;
			if(HitTouchRect(shopOfferRect[i], x, y, 44, 44, 2) == false)
				continue;
			PlaySoundIfLoaded(soundClick);
			TryBuyShopOffer(offerIndex);
			return;
		}
		if(shopRect.IsPointInRect(x, y) == false) {
			PlaySoundIfLoaded(soundClick);
			const bool resumedScript = ResolveShopPrompt();
			if(resumedScript == false)
				SetMessage("You leave the merchant.");
		}
		return;
	}

	ResetSwipeTracking();

	if(HitTouchRect(menuRect, x, y, 44, 44, 2)) {
		CancelAutoPath();
		PlaySoundIfLoaded(soundClick);
		SaveProgress();
		RunNewNode("NewGame");
		return;
	}

	if(HitTouchRect(saveRect, x, y, 44, 44, 2)) {
		CancelAutoPath();
		SaveProgress();
		saveBannerTimer = GetMilliseconds();
		PlaySoundIfLoaded(soundSave);
		return;
	}

	if(HitTouchRect(statusRect, x, y, 44, 44, 2)) {
		CancelAutoPath();
		PlaySoundIfLoaded(soundClick);
		DragonSetStatusEntryMode(DRAGON_STATUS_ENTRY_EQUIPMENT);
		SaveProgress();
		RunNewNode("Status");
		return;
	}

	if(HitTouchRect(actionRect, x, y, 44, 44, 2)) {
		CancelAutoPath();
		PlaySoundIfLoaded(soundClick);
		if(TryInteractFacing())
			return;
		DragonSetStatusEntryMode(DRAGON_STATUS_ENTRY_MAGIC);
		SaveProgress();
		RunNewNode("Status");
		return;
	}

	if(HitTouchRect(battleRect, x, y, 44, 44, 2)) {
		CancelAutoPath();
		PlaySoundIfLoaded(soundClick);
		DragonSetStatusEntryMode(DRAGON_STATUS_ENTRY_TECH);
		SaveProgress();
		RunNewNode("Status");
		return;
	}

	if(HitTouchRect(dpadUpRect, x, y, 44, 44, 2)) {
		CancelAutoPath();
		PlaySoundIfLoaded(soundClick);
		tutorialActive = true;
		return;
	}

	if(HitTouchRect(dpadDownRect, x, y, 44, 44, 2)) {
		CancelAutoPath();
		PlaySoundIfLoaded(soundClick);
#if defined(__APPLE__) && TARGET_OS_IOS
		SetMessage("Use the Home gesture to close on iOS.");
#else
		SaveProgress();
		std::exit(0);
#endif
		return;
	}

	const ERect touchFrame = (mapTileFrameRect.width > 0 and mapTileFrameRect.height > 0) ? mapTileFrameRect : gridFrameRect;
	if(touchFrame.IsPointInRect(x, y)) {
		ResetSwipeTracking();
		swipeTracking = true;
		swipeStartX = x;
		swipeStartY = y;
		swipeLastX = x;
		swipeLastY = y;
		const int64_t touchNow = GetMilliseconds();
		swipeLastMoveTime = touchNow;
		int targetX = mapX;
		int targetY = mapY;
		if(ScreenToMapCell(x, y, targetX, targetY) == false)
			return;
		int preferredX = targetX;
		int preferredY = targetY;
		if(ResolvePreferredTapTarget(targetX, targetY, preferredX, preferredY)) {
			targetX = preferredX;
			targetY = preferredY;
		}
		uint16_t unsupportedWarpPluginId = 0;
		const bool unsupportedWarpTarget = IsUnsupportedLegacyWarpCell(selectedArea, targetX, targetY, &unsupportedWarpPluginId);
		if(unsupportedWarpTarget) {
			CancelAutoPath();
			SetInteractionHint(targetX, targetY);
			SetUnavailableWarpMessage(unsupportedWarpPluginId);
			PlayBumpSound();
			return;
		}
		const MapEventDefinition* targetEvent = FindEvent(selectedArea, targetX, targetY, NULL);
		const bool targetEventPending = targetEvent != NULL
			and IsRequirementMet(*targetEvent, selectedArea)
			and IsEventComplete(*targetEvent, selectedArea) == false;
		const bool hasRoute = autoPathIndex >= 1 and autoPathIndex < (int)autoPathCells.size();
		const bool allowRepeatTap = hasRoute
			or (targetX == mapX and targetY == mapY)
			or (targetX == autoPathTargetX and targetY == autoPathTargetY)
			or targetEventPending;
		if(targetX == lastTapMapX
			and targetY == lastTapMapY
			and touchNow - lastTapAt < MAP_TOUCH_TAP_DEBOUNCE_MS
			and allowRepeatTap == false) {
			return;
		}
		lastTapAt = touchNow;
		lastTapMapX = targetX;
		lastTapMapY = targetY;
		const int tapDX = targetX - mapX;
		const int tapDY = targetY - mapY;
		if(std::abs(tapDX) + std::abs(tapDY) == 1) {
			if(targetEvent != NULL
				and IsRequirementMet(*targetEvent, selectedArea)
				and IsEventComplete(*targetEvent, selectedArea) == false
				and ShouldAutoInteractAtAdjacentEvent(*targetEvent, targetX, targetY)) {
				CancelAutoPath();
				facingDX = tapDX < 0 ? -1 : (tapDX > 0 ? 1 : 0);
				facingDY = tapDY < 0 ? -1 : (tapDY > 0 ? 1 : 0);
				PlaySoundIfLoaded(soundClick);
				TryInteractFacing();
				return;
			}
		}
		if(targetX == mapX and targetY == mapY) {
			CancelAutoPath();
			PlaySoundIfLoaded(soundClick);
			// Keep route-cancel taps deterministic: canceling an active route on the
			// current tile should not also trigger an interaction side effect.
			if(hasRoute) {
				SetMessageThrottled("Route cancelled.", MAP_ROUTE_FAILURE_MESSAGE_THROTTLE_MS);
				return;
			}
			TryInteractFacing();
			return;
		}
			if(hasRoute and targetX == autoPathTargetX and targetY == autoPathTargetY) {
				const int targetDistance = std::abs(targetX - mapX) + std::abs(targetY - mapY);
				if(targetEvent != NULL
					and targetDistance == 1
					and IsRequirementMet(*targetEvent, selectedArea)
				and IsEventComplete(*targetEvent, selectedArea) == false) {
				CancelAutoPath();
				facingDX = targetX < mapX ? -1 : (targetX > mapX ? 1 : 0);
				facingDY = targetY < mapY ? -1 : (targetY > mapY ? 1 : 0);
				PlaySoundIfLoaded(soundClick);
				if(TryInteractFacing())
					return;
				SetInteractionHint(targetX, targetY);
					SetRouteInteractionHintMessage(IsInteractOnlyEventType(targetEvent->type));
					return;
				}
				CancelAutoPath();
				SetMessageThrottled("Route cancelled.", MAP_ROUTE_FAILURE_MESSAGE_THROTTLE_MS);
				PlaySoundIfLoaded(soundClick);
				return;
			}
			if(std::abs(tapDX) + std::abs(tapDY) == 1
				and IsBlockedCell(selectedArea, targetX, targetY)
				and targetEvent == NULL) {
				CancelAutoPath();
				facingDX = tapDX < 0 ? -1 : (tapDX > 0 ? 1 : 0);
				facingDY = tapDY < 0 ? -1 : (tapDY > 0 ? 1 : 0);
				SetMessageThrottled("You cannot pass there.", MAP_INTERACTION_MISS_MESSAGE_THROTTLE_MS);
				PlayBumpSound();
				return;
			}
			if(StartAutoPathTo(targetX, targetY) == false) {
				CancelAutoPath();
				SetMessageThrottled("No clear route there.", MAP_ROUTE_FAILURE_MESSAGE_THROTTLE_MS);
				PlayBumpSound();
			return;
		}
		if(autoPathAdjusted) {
			const int adjustmentDistance = std::abs(autoPathTargetX - autoPathRequestedX) + std::abs(autoPathTargetY - autoPathRequestedY);
			if(adjustmentDistance >= 2)
				SetMessageThrottled("Path adjusted to nearest reachable tile.", MAP_ROUTE_MESSAGE_THROTTLE_MS);
		}
		return;
	}

	const bool hasActiveRoute = autoPathIndex >= 1 and autoPathIndex < (int)autoPathCells.size();
	if(hasActiveRoute) {
		CancelAutoPath();
		SetMessageThrottled("Route cancelled.", MAP_ROUTE_FAILURE_MESSAGE_THROTTLE_MS);
		PlaySoundIfLoaded(soundClick);
	}
}

void WorldMap::OnTouchMove (int x, int y) {
	EPoint input = NormalizeInputPoint(*this, x, y);
	x = input.x;
	y = input.y;
	const ERect inputBounds = MakePreferredViewRect(GetSafeRect(), DESIGN_PREFERRED_WIDTH, DESIGN_PREFERRED_HEIGHT);
	const ERect safeBounds = GetSafeRect();
	if(inputBounds.IsPointInRect(x, y) == false) {
		if(safeBounds.IsPointInRect(x, y) == false) {
			ResetSwipeTracking();
			return;
		}
	}
	UpdateLayoutRects();

	if(tutorialActive or endingActive or talkActive or promptActive or shopActive) {
		ResetSwipeTracking();
		return;
	}
	if(swipeTracking == false)
		return;
	const ERect touchFrame = (mapTileFrameRect.width > 0 and mapTileFrameRect.height > 0) ? mapTileFrameRect : gridFrameRect;
	if(touchFrame.IsPointInRect(x, y) == false) {
		ResetSwipeTracking();
		return;
	}
	if(std::abs(x - swipeStartX) + std::abs(y - swipeStartY) < MAP_TOUCH_DRAG_DEADZONE_PX)
		return;
	touchMovedThisSequence = true;

	swipeLastX = x;
	swipeLastY = y;
	int64_t now = GetMilliseconds();
	if(now - swipeLastMoveTime < MAP_TOUCH_DRAG_REPEAT_MS)
		return;
	if(now - lastAutoPathRetargetAt < MAP_TOUCH_RETARGET_COOLDOWN_MS)
		return;
	swipeLastMoveTime = now;

	int targetX = mapX;
	int targetY = mapY;
	if(ScreenToMapCell(x, y, targetX, targetY) == false)
		return;
	int preferredX = targetX;
	int preferredY = targetY;
	if(ResolvePreferredTapTarget(targetX, targetY, preferredX, preferredY)) {
		targetX = preferredX;
		targetY = preferredY;
	}
	uint16_t unsupportedWarpPluginId = 0;
	const bool unsupportedWarpTarget = IsUnsupportedLegacyWarpCell(selectedArea, targetX, targetY, &unsupportedWarpPluginId);
	if(unsupportedWarpTarget) {
		CancelAutoPath();
		SetInteractionHint(targetX, targetY);
		SetUnavailableWarpMessage(unsupportedWarpPluginId);
		return;
	}
	if(targetX == mapX and targetY == mapY)
		return;
	if(targetX == autoPathRequestedX and targetY == autoPathRequestedY)
		return;
	if(targetX == autoPathTargetX and targetY == autoPathTargetY)
		return;
	if(StartAutoPathTo(targetX, targetY) == false) {
		SetMessageThrottled("No clear route there.", MAP_ROUTE_FAILURE_MESSAGE_THROTTLE_MS);
		return;
	}
	if(autoPathAdjusted) {
		const int adjustmentDistance = std::abs(autoPathTargetX - autoPathRequestedX) + std::abs(autoPathTargetY - autoPathRequestedY);
		if(adjustmentDistance >= 2)
			SetMessageThrottled("Path adjusted to nearest reachable tile.", MAP_ROUTE_MESSAGE_THROTTLE_MS);
	}
}

void WorldMap::OnTouchUp (int x, int y) {
	EPoint input = NormalizeInputPoint(*this, x, y);
	x = input.x;
	y = input.y;
	const ERect inputBounds = MakePreferredViewRect(GetSafeRect(), DESIGN_PREFERRED_WIDTH, DESIGN_PREFERRED_HEIGHT);
	const ERect safeBounds = GetSafeRect();
	const bool touchUpFallbackAllowed = GetMilliseconds() >= touchUpGuardUntil;
	if((inputBounds.IsPointInRect(x, y) or safeBounds.IsPointInRect(x, y))
		and touchUpFallbackAllowed
		and touchHandledThisSequence == false
		and touchMovedThisSequence == false) {
		OnTouch(x, y);
	}
	ResetSwipeTracking();
	touchHandledThisSequence = false;
	touchMovedThisSequence = false;
}

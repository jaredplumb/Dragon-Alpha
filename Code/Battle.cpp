/**
 * @file Battle.cpp
 * @brief Battle system flow, command resolution, and combat rendering implementation.
 */
#include "Battle.h"
#include "LayoutUtil.h"
#include "LegacyItemMap.h"
#include "LegacyBattleData.generated.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <unordered_map>

namespace {
static const bool kBattleNodeFactoryRegistered = DragonEnsureNodeFactoryRegistered<Battle>();

#if defined(DRAGON_TEST)
void ReportValidationLabel (const EString& text, const ERect& rect) {
	if(text.IsEmpty() == false and rect.width > 0 and rect.height > 0)
		ESystem::ReportTextDraw(text, rect);
}
#endif
}

static const char* kEnemyArtName[DRAGON_ALPHA_AREA_COUNT] = {
	"BattleEnemyEleusis",
	"BattleEnemyMeadows",
	"BattleEnemyForests",
	"BattleEnemyCaves",
	"BattleEnemyMountains",
	"BattleEnemyPeak",
};

static const char* kBattleBackgroundName[DRAGON_ALPHA_AREA_COUNT] = {
	"AreaEleusisBattle",
	"AreaMeadowsBattle",
	"AreaForestsBattle",
	"AreaCavesBattle",
	"AreaMountainsBattle",
	"AreaPeakBattle",
};

static const char* kEnemyArtVariantName[DRAGON_ALPHA_AREA_COUNT][4] = {
	{"BattleEnemyEleusisA", "BattleEnemyEleusisB", "BattleEnemyEleusisC", "BattleEnemyEleusisD"},
	{"BattleEnemyMeadowsA", "BattleEnemyMeadowsB", "BattleEnemyMeadowsC", "BattleEnemyMeadowsD"},
	{"BattleEnemyForestsA", "BattleEnemyForestsB", "BattleEnemyForestsC", "BattleEnemyForestsD"},
	{"BattleEnemyCavesA", "BattleEnemyCavesB", "BattleEnemyCavesC", "BattleEnemyCavesD"},
	{"BattleEnemyMountainsA", "BattleEnemyMountainsB", "BattleEnemyMountainsC", "BattleEnemyMountainsD"},
	{"BattleEnemyPeakA", "BattleEnemyPeakB", "BattleEnemyPeakC", "BattleEnemyPeakD"},
};

static const char* kPlayerArtName[DRAGON_AVATAR_COUNT] = {
	"BattlePlayerWarrior",
	"BattlePlayerSorcerer",
	"BattlePlayerRanger",
};

static const char* kSkillButtonLabel[DRAGON_AVATAR_COUNT] = {
	"POWER",
	"ARCANE",
	"VOLLEY",
};
static const char* kAltSkillButtonLabel[DRAGON_AVATAR_COUNT] = {
	"RALLY",
	"NOVA",
	"PIN",
};
static constexpr int kBattleSkillChargeMax = 100;
static constexpr int kBattleTargetSnapDistancePx = 72;
static constexpr int kBattlePendingTargetSnapDistancePx = 96;
static constexpr int kBattleTargetHintThrottleMs = 450;
static constexpr int kLegacyMaxDamage = 99999;
static constexpr int kLegacyMaxHitPercent = 100;
static constexpr int kLegacyMaxAbsorb = 99999;
static constexpr int kLegacyMaxEvadePercent = 50;
static constexpr int kLegacyElementLight = 1;
static constexpr int kLegacyElementDark = 2;
static constexpr int kLegacyElementFire = 3;
static constexpr int kLegacyElementWater = 4;
static constexpr int kLegacyElementEarth = 5;
static constexpr int kLegacyElementAir = 6;
static constexpr uint16_t kChallengeOneFlag = 0x0020;
static constexpr uint16_t kChallengeTwoFlag = 0x0040;

#ifndef DRAGON_ALPHA_BATTLE_TRACE
#define DRAGON_ALPHA_BATTLE_TRACE 0
#endif

#if DRAGON_ALPHA_BATTLE_TRACE
#define BATTLE_TRACE(...) ESystem::Debug(__VA_ARGS__)
#else
#define BATTLE_TRACE(...) do { } while(0)
#endif

static bool MapLegacyBattleLootItemToModern (const DragonLegacyItemRef& item, int& outItemType, int& outGoldBonus) {
	return DragonMapLegacyItemRefToModern(item, outItemType, outGoldBonus);
}

static void DecodeLegacyPluginMapState (uint16_t& outPluginId, uint16_t& outMapId) {
	uint32_t packed = (uint32_t)gWorldState.reserved[0];
	packed |= ((uint32_t)gWorldState.reserved[1] << 8);
	packed |= ((uint32_t)gWorldState.reserved[2] << 16);
	outPluginId = (uint16_t)(packed & 0x07FFu);
	outMapId = (uint16_t)((packed >> 11) & 0x07FFu);
}

static const char* BattleBackgroundForArea (int areaIndex) {
	int safeArea = std::max(0, std::min(areaIndex, DRAGON_ALPHA_AREA_COUNT - 1));
	uint16_t pluginId = 0;
	uint16_t mapId = 0;
	DecodeLegacyPluginMapState(pluginId, mapId);
	if(safeArea == DRAGON_AREA_ELEUSIS and pluginId == 1 and mapId == 129)
		return "AreaEleusisStrongholdBattle";
	return kBattleBackgroundName[safeArea];
}

static EImage* ResolveLegacyBattleSpriteImage (uint16_t pluginId, uint16_t spriteId) {
	if(pluginId == 0 or spriteId == 0)
		return NULL;

	static std::unordered_map<int, std::unique_ptr<EImage>> sBattleSpriteCache;
	const int cacheKey = (((int)pluginId & 0xFFFF) << 16) | ((int)spriteId & 0xFFFF);
	auto cached = sBattleSpriteCache.find(cacheKey);
	if(cached != sBattleSpriteCache.end()) {
		if(cached->second == NULL)
			return NULL;
		return cached->second.get();
	}

	std::unique_ptr<EImage> image(new EImage());
	EString spriteName = EString().Format("BattleEnemyLegacyP%dS%d", (int)pluginId, (int)spriteId);
	if(image->New((const char*)spriteName) == false or image->GetWidth() < 8 or image->GetHeight() < 8) {
		sBattleSpriteCache.emplace(cacheKey, nullptr);
		return NULL;
	}
	auto inserted = sBattleSpriteCache.emplace(cacheKey, std::move(image));
	return inserted.first->second.get();
}

static bool IsLikelyFlatBattleBackground (const char* imageName) {
	if(imageName == NULL or imageName[0] == '\0')
		return false;

	EImage::Resource probe;
	if(probe.New(imageName) == false or probe.buffer == NULL or probe.width <= 0 or probe.height <= 0)
		return false;

	const int totalPixels = probe.width * probe.height;
	if(totalPixels <= 0)
		return false;
	const int sampleStride = std::max(1, totalPixels / 4096);
	int minLuma = 255;
	int maxLuma = 0;
	for(int i = 0; i < totalPixels; i += sampleStride) {
		const int pixelOffset = i * 4;
		const int red = probe.buffer[pixelOffset + 0];
		const int green = probe.buffer[pixelOffset + 1];
		const int blue = probe.buffer[pixelOffset + 2];
		const int luma = (red + green + blue) / 3;
		minLuma = std::min(minLuma, luma);
		maxLuma = std::max(maxLuma, luma);
	}
	return (maxLuma - minLuma) <= 10;
}

static const char* ResolveBattleBackgroundNameForArea (int areaIndex) {
	const char* preferred = BattleBackgroundForArea(areaIndex);
	if(IsLikelyFlatBattleBackground(preferred) == false)
		return preferred;

	// Some recovered classic battle payloads decode to flat gray frames.
	// Preserve legacy scene semantics by only falling back to other area battle
	// backdrops (never generic UI/static placeholders).
	static const char* kFallbackBattleBackgrounds[] = {
		"AreaEleusisStrongholdBattle",
		"AreaEleusisBattle",
		"AreaMeadowsBattle",
		"AreaForestsBattle",
		"AreaCavesBattle",
		"AreaMountainsBattle",
		"AreaPeakBattle",
	};
	for(size_t i = 0; i < sizeof(kFallbackBattleBackgrounds) / sizeof(kFallbackBattleBackgrounds[0]); i++) {
		const char* candidate = kFallbackBattleBackgrounds[i];
		if(std::strcmp(candidate, preferred) == 0)
			continue;
		if(IsLikelyFlatBattleBackground(candidate) == false)
			return candidate;
	}
	return preferred;
}

static void LoadLegacyBattleBackgroundForContext (EImage& imageBackground, int areaIndex, uint16_t pluginId, uint16_t mapId) {
	const char* fallbackName = ResolveBattleBackgroundNameForArea(areaIndex);
	if(pluginId != 0 and mapId != 0) {
		EString legacyMapBattleName = EString().Format("LegacyBattleP%dM%d", (int)pluginId, (int)mapId);
		// Guard against flat decode artifacts in future map-specific exports.
		if(IsLikelyFlatBattleBackground((const char*)legacyMapBattleName) == false) {
			LoadImageOrFallback(imageBackground, (const char*)legacyMapBattleName, fallbackName, EColor(0x120d1dff));
			return;
		}
	}
	LoadImageOrFallback(imageBackground, fallbackName, EColor(0x120d1dff));
}

Battle::Battle ()
:	imageBackground(EColor(0x120d1dff))
,	imageArena(EColor(0x261a34ff))
,	imagePlayer(EColor(0x5eaef2ff))
,	imageEnemy(EColor(0xee7c4cff))
,	imageHealthBack(EColor(0x0f1825ff))
,	imageHealthFill(EColor(0x43c367ff))
,	imageAction(EColor(0x32557cff))
,	imageActionAlt(EColor(0x6d4731ff))
,	imageOverlay(EColor(0x000000b0))
,	imageGameOver(EColor(0x6f2a2aff))
,	imageFxAttackHit(EColor(0xcf5d4cff))
,	imageFxHealHit(EColor(0x63c784ff))
,	imageFxFireCast(EColor(0xde7f3dff))
,	imageFxFireHit(EColor(0xf0954cff))
,	imageFxWaterCast(EColor(0x5b87cbff))
,	imageFxWaterHit(EColor(0x6da4deff))
,	imageFxEarthCast(EColor(0x9f8764ff))
,	imageFxEarthHit(EColor(0xbe9467ff))
,	imageFxAirCast(EColor(0x9dc8e8ff))
,	imageFxAirHit(EColor(0xb3d5eeff))
,	imageFxLightCast(EColor(0xf1d978ff))
,	imageFxLightHit(EColor(0xf3e79dff))
,	imageFxDarkCast(EColor(0x8665abff))
,	imageFxDarkHit(EColor(0x765296ff))
,	imageFxNoneCast(EColor(0xa0a0a0ff))
,	fontHeader("FontMissionHeader")
,	fontMain("FontMissionText")
,	transition(UITransition::FADE_BLACK, this)
,	battleRequest({})
,	hasBattleRequest(false)
,	enemyGroup()
,	activeEnemyIndex(-1)
,	totalEnemyRewardXP(0)
,	totalEnemyRewardGold(0)
,	retreatLocked(false)
,	battleRetreated(false)
,	actionText("")
,	areaInfo(NULL)
,	lastPlayerDamage(0)
,	lastEnemyDamage(0)
,	validationPlayerActionSerial(0)
,	validationPlayerActionText("")
,	validationEnemyActionSerial(0)
,	validationEnemyActionText("")
,	enemyArtVariant(0)
	,	playerTurnMeter(0)
	,	actionTimestamp(0)
	,	touchHandledThisSequence(false)
	,	touchMovedThisSequence(false)
	,	touchDownX(0)
	,	touchDownY(0)
,	startLevel(0)
,	levelGain(0)
,	defeatGoldLoss(0)
,	retreatHealthLoss(0)
,	victoryXP(0)
,	victoryGold(0)
,	victoryPotionDrops(0)
,	victoryScrollDrops(0)
,	victoryHealthRecovered(0)
,	victoryLegacyLootItems(0)
,	victoryLegacyLootGold(0)
,	victoryLegacyLootFirstItem("")
,	victoryMilestoneRewards("")
,	skillCharge(kBattleSkillChargeMax)
,	playerEffectType(BATTLE_EFFECT_NONE)
,	enemyEffectType(BATTLE_EFFECT_NONE)
,	playerEffectDuration(0)
,	enemyEffectDuration(0)
,	playerEffectTimestamp(0)
,	enemyEffectTimestamp(0)
	,	roundNumber(1)
	,	trialBattle(false)
	,	storyBattle(false)
	,	rareRoamingBattle(false)
	,	strictLegacyBattle(false)
	,	fallbackBattlePath(false)
	,	fallbackBattleReason("")
	,	battleSourceTag("LEGACY")
	,	skillMenuActive(false)
	,	itemMenuActive(false)
	,	legacyCommandMenuMode(LEGACY_COMMAND_MENU_NONE)
	,	pendingLegacyCommandAction()
	,	hasPendingLegacyCommandAction(false)
	,	legacyCommandActions()
,	pendingTargetCommand(PENDING_TARGET_NONE)
,	battleDone(false)
,	battleWon(false)
{
	uint16_t startupPluginId = 0;
	uint16_t startupMapId = 0;
	DecodeLegacyPluginMapState(startupPluginId, startupMapId);
	DragonPlayBattleMusic(gSelectedArea);
	LoadLegacyBattleBackgroundForContext(imageBackground, gSelectedArea, startupPluginId, startupMapId);
	LoadImageOrFallback(imageArena, "BattleArena", EColor(0x261a34ff));
	LoadImageOrFallback(imageHealthBack, "BattleHealthBack", EColor(0x0f1825ff));
	LoadImageOrFallback(imageHealthFill, "BattleHealthFill", EColor(0x43c367ff));
	imageSelectRed.New("BattleSelectRed");
	imageSelectYellow.New("BattleSelectYellow");
	imageSelectGreen.New("BattleSelectGreen");
	imageSpellName.New("BattleSpellName");
	LoadImageOrFallback(imageAction, "BattleAction", EColor(0x32557cff));
	LoadImageOrFallback(imageActionAlt, "BattleActionAlt", EColor(0x6d4731ff));
	LoadImageOrFallback(imageOverlay, "BattleOverlay", EColor(0x000000b0));
	LoadImageOrFallback(imageGameOver, "BattleGameOver", EColor(0x6f2a2aff));
	LoadImageOrFallback(imageFxAttackHit, "BattleFxAttackHit", EColor(0xcf5d4cff));
	LoadImageOrFallback(imageFxHealHit, "BattleFxHealHit", EColor(0x63c784ff));
	LoadImageOrFallback(imageFxFireCast, "BattleFxFireCast", EColor(0xde7f3dff));
	LoadImageOrFallback(imageFxFireHit, "BattleFxFireHit", EColor(0xf0954cff));
	LoadImageOrFallback(imageFxWaterCast, "BattleFxWaterCast", EColor(0x5b87cbff));
	LoadImageOrFallback(imageFxWaterHit, "BattleFxWaterHit", EColor(0x6da4deff));
	LoadImageOrFallback(imageFxEarthCast, "BattleFxEarthCast", EColor(0x9f8764ff));
	LoadImageOrFallback(imageFxEarthHit, "BattleFxEarthHit", EColor(0xbe9467ff));
	LoadImageOrFallback(imageFxAirCast, "BattleFxAirCast", EColor(0x9dc8e8ff));
	LoadImageOrFallback(imageFxAirHit, "BattleFxAirHit", EColor(0xb3d5eeff));
	LoadImageOrFallback(imageFxLightCast, "BattleFxLightCast", EColor(0xf1d978ff));
	LoadImageOrFallback(imageFxLightHit, "BattleFxLightHit", EColor(0xf3e79dff));
	LoadImageOrFallback(imageFxDarkCast, "BattleFxDarkCast", EColor(0x8665abff));
	LoadImageOrFallback(imageFxDarkHit, "BattleFxDarkHit", EColor(0x765296ff));
	LoadImageOrFallback(imageFxNoneCast, "BattleFxNoneCast", EColor(0xa0a0a0ff));

	// Legacy resource aliases can map tiny sprites into panel IDs; normalize to sane UI fills.
	if(imageArena.GetWidth() < 160 or imageArena.GetHeight() < 80)
		imageArena.New(EColor(0x261a34f0));
	if(imageHealthBack.GetWidth() <= 8 or imageHealthBack.GetHeight() <= 8)
		imageHealthBack.New(EColor(0x0f1825ff));
	if(imageHealthFill.GetWidth() <= 8 or imageHealthFill.GetHeight() <= 8)
		imageHealthFill.New(EColor(0x43c367ff));
	if((imageAction.GetWidth() <= 96 and imageAction.GetHeight() <= 96) or imageAction.GetWidth() < 100 or imageAction.GetHeight() < 24)
		imageAction.New(EColor(0x32557cff));
	if((imageActionAlt.GetWidth() <= 96 and imageActionAlt.GetHeight() <= 96) or imageActionAlt.GetWidth() < 100 or imageActionAlt.GetHeight() < 24)
		imageActionAlt.New(EColor(0x6d4731ff));
	if(imageOverlay.GetWidth() < 32 or imageOverlay.GetHeight() < 32)
		imageOverlay.New(EColor(0x000000b8));

	LoadSoundIfPresent(soundClick, "Click");
	LoadSoundIfPresent(soundHit, "BattleHit");
	LoadSoundIfPresent(soundMiss, "BattleMiss");
	LoadSoundIfPresent(soundCast, "BattleCast");
	LoadSoundIfPresent(soundDefeat, "BattleDefeat");
	LoadSoundIfPresent(soundLevelUp, "LevelUp");
	skillMenuRect = ERect();
	skillPrimaryRect = ERect();
	skillAlternateRect = ERect();
	skillTertiaryRect = ERect();
	skillCancelRect = ERect();
	itemMenuRect = ERect();
	itemPotionRect = ERect();
	itemScrollRect = ERect();
	itemCancelRect = ERect();
	techRect = ERect();
	enemyTargetRect = ERect();
	enemyTouchTargets.clear();

	BuildEnemy();
}

void Battle::ClearEnemyGroup () {
	enemyGroup.clear();
	enemyTouchTargets.clear();
	pendingTargetCommand = PENDING_TARGET_NONE;
	activeEnemyIndex = -1;
	totalEnemyRewardXP = 0;
	totalEnemyRewardGold = 0;
	enemy = EnemyData();
	enemyArtVariant = 0;
}

void Battle::PushEnemyUnit (const EnemyData& data, uint16_t pluginId, uint16_t monsterId, uint16_t spriteId, int artVariant, int formationX, int formationY, bool boss) {
	EnemyUnitRuntime unit = {};
	unit.data = data;
	unit.data.level = std::max(1, unit.data.level);
	unit.data.healthMax = std::max(1, unit.data.healthMax);
	unit.data.health = std::max(1, std::min(unit.data.health, unit.data.healthMax));
	unit.data.attack = std::max(1, unit.data.attack);
	unit.data.defense = std::max(1, unit.data.defense);
	unit.data.magic = std::max(1, unit.data.magic);
	unit.data.statStrength = std::max(1, unit.data.statStrength);
	unit.data.statDefense = std::max(1, unit.data.statDefense);
	unit.data.statSpeed = std::max(1, unit.data.statSpeed);
	unit.data.statMagic = std::max(1, unit.data.statMagic);
	unit.data.rewardXP = std::max(1, unit.data.rewardXP);
	unit.data.rewardGold = std::max(1, unit.data.rewardGold);
	unit.pluginId = pluginId;
	unit.monsterId = monsterId;
	unit.spriteId = spriteId;
	unit.artVariant = std::max(0, std::min(artVariant, 3));
	unit.formationX = std::max(0, formationX);
	unit.formationY = std::max(0, formationY);
	unit.turnMeter = std::max(1, unit.data.statSpeed);
	unit.boss = boss;
	enemyGroup.push_back(unit);
	totalEnemyRewardXP += unit.data.rewardXP;
	totalEnemyRewardGold += unit.data.rewardGold;
}

int Battle::LivingEnemyCount () const {
	int count = 0;
	for(size_t i = 0; i < enemyGroup.size(); i++) {
		if(enemyGroup[i].data.health > 0)
			count++;
	}
	return count;
}

int Battle::FirstLivingEnemyIndex () const {
	for(size_t i = 0; i < enemyGroup.size(); i++) {
		if(enemyGroup[i].data.health > 0)
			return (int)i;
	}
	return -1;
}

int Battle::NextLivingEnemyIndex (int fromIndex) const {
	if(enemyGroup.empty())
		return -1;
	int safeFrom = fromIndex;
	if(safeFrom < 0 or safeFrom >= (int)enemyGroup.size())
		safeFrom = 0;
	for(size_t offset = 1; offset <= enemyGroup.size(); offset++) {
		int index = (safeFrom + (int)offset) % (int)enemyGroup.size();
		if(enemyGroup[(size_t)index].data.health > 0)
			return index;
	}
	return -1;
}

int Battle::TotalEnemyHealth () const {
	int total = 0;
	for(size_t i = 0; i < enemyGroup.size(); i++)
		total += std::max(0, enemyGroup[i].data.health);
	return std::max(0, total);
}

int Battle::TotalEnemyHealthMax () const {
	int total = 0;
	for(size_t i = 0; i < enemyGroup.size(); i++)
		total += std::max(1, enemyGroup[i].data.healthMax);
	return std::max(1, total);
}

void Battle::SyncActiveEnemyFromGroup () {
	if(enemyGroup.empty()) {
		activeEnemyIndex = -1;
		enemy = EnemyData();
		return;
	}
	if(activeEnemyIndex < 0 or activeEnemyIndex >= (int)enemyGroup.size() or enemyGroup[(size_t)activeEnemyIndex].data.health <= 0)
		activeEnemyIndex = FirstLivingEnemyIndex();
	if(activeEnemyIndex < 0)
		activeEnemyIndex = 0;
	const EnemyUnitRuntime& active = enemyGroup[(size_t)activeEnemyIndex];
	enemy = active.data;
	enemyArtVariant = active.artVariant;
}

bool Battle::HandleEnemyDefeat () {
	if(activeEnemyIndex < 0 or activeEnemyIndex >= (int)enemyGroup.size()) {
		FinishVictory();
		return true;
	}
	EString defeatedName = enemyGroup[(size_t)activeEnemyIndex].data.name;
	enemyGroup[(size_t)activeEnemyIndex].data.health = 0;
	enemyGroup[(size_t)activeEnemyIndex].turnMeter = 0;
	const int nextIndex = FirstLivingEnemyIndex();
	if(nextIndex < 0) {
		enemy.health = 0;
		FinishVictory();
		return true;
	}
	activeEnemyIndex = nextIndex;
	SyncActiveEnemyFromGroup();
	actionText = EString().Format("%s defeated. %s steps forward.", (const char*)defeatedName, (const char*)enemy.name);
	actionTimestamp = GetMilliseconds();
	return false;
}

bool Battle::TryCycleEnemyTarget () {
	if(LivingEnemyCount() <= 1)
		return false;
	const int nextIndex = NextLivingEnemyIndex(activeEnemyIndex);
	if(nextIndex < 0 or nextIndex == activeEnemyIndex)
		return false;
	activeEnemyIndex = nextIndex;
	SyncActiveEnemyFromGroup();
	actionText = EString().Format("Targeting %s. %d foes remain.", (const char*)enemy.name, LivingEnemyCount());
	actionTimestamp = GetMilliseconds();
	return true;
}

void Battle::UpdateLayoutRects () {
	const LegacyCanvas canvas = MakeLegacyCanvasInPreferredView(*this, 800, 600);
	const ERect safe = canvas.frame;

	const int commandButtonCount = 5;
	const int buttonHeight = std::max(28, LegacyRect(canvas, 0, 0, 0, 36).height);
	const int preferredButtonWidth = std::max(82, LegacyRect(canvas, 0, 0, 112, 0).width);
	const int preferredGap = std::max(6, LegacyRect(canvas, 0, 0, 8, 0).width);
	const int lanePadding = std::max(8, LegacyRect(canvas, 0, 0, 10, 0).width);
	const int minimumButtonWidth = std::max(36, LegacyRect(canvas, 0, 0, 52, 0).width);
	const int minimumGap = std::max(2, LegacyRect(canvas, 0, 0, 4, 0).width);
	const int availableLaneWidth = std::max(1, safe.width - lanePadding * 2);

	int gap = preferredGap;
	int buttonWidth = preferredButtonWidth;
	int totalButtonWidth = buttonWidth * commandButtonCount + gap * (commandButtonCount - 1);
	if(totalButtonWidth > availableLaneWidth) {
		const int maxGapByWidth = (availableLaneWidth - minimumButtonWidth * commandButtonCount) / std::max(1, commandButtonCount - 1);
		gap = std::max(minimumGap, std::min(preferredGap, maxGapByWidth));
		int maxButtonWidth = (availableLaneWidth - gap * (commandButtonCount - 1)) / std::max(1, commandButtonCount);
		if(maxButtonWidth < minimumButtonWidth) {
			gap = minimumGap;
			maxButtonWidth = (availableLaneWidth - gap * (commandButtonCount - 1)) / std::max(1, commandButtonCount);
		}
		buttonWidth = std::max(32, std::min(preferredButtonWidth, std::max(32, maxButtonWidth)));
		totalButtonWidth = buttonWidth * commandButtonCount + gap * (commandButtonCount - 1);
	}
	const int legacyButtonY = LegacyRect(canvas, 0, 548, 0, 0).y;
	const int maxButtonY = safe.y + safe.height - buttonHeight - std::max(4, LegacyRect(canvas, 0, 0, 0, 6).height);
	const int buttonY = std::min(legacyButtonY, maxButtonY);
	const int startX = safe.x + std::max(0, (safe.width - totalButtonWidth) / 2);

	attackRect = ClipRectToBounds(ERect(startX, buttonY, buttonWidth, buttonHeight), safe);
	skillRect = ClipRectToBounds(ERect(attackRect.x + attackRect.width + gap, buttonY, buttonWidth, buttonHeight), safe);
	itemRect = ClipRectToBounds(ERect(skillRect.x + skillRect.width + gap, buttonY, buttonWidth, buttonHeight), safe);
	techRect = ClipRectToBounds(ERect(itemRect.x + itemRect.width + gap, buttonY, buttonWidth, buttonHeight), safe);
	retreatRect = ClipRectToBounds(ERect(techRect.x + techRect.width + gap, buttonY, buttonWidth, buttonHeight), safe);
	continueRect = ClipRectToBounds(
		ERect(
			safe.x + (safe.width - std::max(140, buttonWidth + 20)) / 2,
			buttonY,
			std::max(140, buttonWidth + 20),
			std::max(36, buttonHeight + 4)
		),
		safe
	);
	backRect = continueRect;
}

ERect Battle::CommandLaneRect () const {
	const ERect controls[5] = {attackRect, skillRect, itemRect, techRect, retreatRect};
	bool hasAny = false;
	int left = 0;
	int top = 0;
	int right = 0;
	int bottom = 0;
	for(const ERect& rect : controls) {
		if(rect.width <= 0 or rect.height <= 0)
			continue;
		if(hasAny == false) {
			left = rect.x;
			top = rect.y;
			right = rect.x + rect.width;
			bottom = rect.y + rect.height;
			hasAny = true;
			continue;
		}
		left = std::min(left, rect.x);
		top = std::min(top, rect.y);
		right = std::max(right, rect.x + rect.width);
		bottom = std::max(bottom, rect.y + rect.height);
	}
	if(hasAny == false)
		return ERect();
	return ERect(left, top, std::max(0, right - left), std::max(0, bottom - top));
}

bool Battle::HitCommandLaneControl (int x, int y) const {
	return HitTouchRect(attackRect, x, y, 72, 44, 2)
		or HitTouchRect(skillRect, x, y, 72, 44, 2)
		or HitTouchRect(itemRect, x, y, 72, 44, 2)
		or HitTouchRect(techRect, x, y, 72, 44, 2)
		or HitTouchRect(retreatRect, x, y, 72, 44, 2);
}

int Battle::FindLivingEnemyTouchTargetAtPoint (int x, int y) const {
	if(enemyTouchTargets.empty())
		return -1;
	for(auto target = enemyTouchTargets.rbegin(); target != enemyTouchTargets.rend(); ++target) {
		if(HitTouchRect(target->rect, x, y, 72, 44, 2) == false)
			continue;
		if(target->enemyIndex < 0 or target->enemyIndex >= (int)enemyGroup.size())
			continue;
		if(enemyGroup[(size_t)target->enemyIndex].data.health <= 0)
			continue;
		return target->enemyIndex;
	}
	return -1;
}

int Battle::FindNearestLivingEnemyTouchTarget (int x, int y, int maxDistance) const {
	if(enemyTouchTargets.empty())
		return -1;
	if(maxDistance <= 0)
		return -1;

	const int64_t maxDistanceSq = (int64_t)maxDistance * (int64_t)maxDistance;
	int64_t bestDistanceSq = std::numeric_limits<int64_t>::max();
	int bestEnemyIndex = -1;

	for(const EnemyTouchTarget& target : enemyTouchTargets) {
		if(target.enemyIndex < 0 or target.enemyIndex >= (int)enemyGroup.size())
			continue;
		if(enemyGroup[(size_t)target.enemyIndex].data.health <= 0)
			continue;

		const int centerX = target.rect.x + target.rect.width / 2;
		const int centerY = target.rect.y + target.rect.height / 2;
		const int64_t dx = (int64_t)x - (int64_t)centerX;
		const int64_t dy = (int64_t)y - (int64_t)centerY;
		const int64_t distanceSq = dx * dx + dy * dy;
		if(distanceSq > maxDistanceSq)
			continue;
		if(distanceSq >= bestDistanceSq)
			continue;

		bestDistanceSq = distanceSq;
		bestEnemyIndex = target.enemyIndex;
	}

	return bestEnemyIndex;
}

void Battle::SeedTurnMeters () {
	playerTurnMeter = std::max(1, DragonGetSpeed());
	for(size_t i = 0; i < enemyGroup.size(); i++)
		enemyGroup[i].turnMeter = std::max(1, enemyGroup[i].data.statSpeed);
}

Battle::CombatStats Battle::EnemyCombatStats (const EnemyData& enemyData) const {
	CombatStats stats = {};
	stats.str = std::max(1, enemyData.statStrength);
	stats.def = std::max(1, enemyData.statDefense);
	stats.spd = std::max(1, enemyData.statSpeed);
	stats.mag = std::max(1, enemyData.statMagic);
	stats.p = enemyData.resistPhysical;
	stats.m = enemyData.resistMagic;
	stats.l = enemyData.resistLight;
	stats.d = enemyData.resistDark;
	stats.f = enemyData.resistFire;
	stats.w = enemyData.resistWater;
	stats.e = enemyData.resistEarth;
	stats.a = enemyData.resistAir;
	return stats;
}

Battle::CombatStats Battle::PlayerCombatStats () const {
	CombatStats stats = {};
	const int level = std::max(1, (int)gSave.level);
	stats.str = std::max(1, (DragonGetAttack() + level) / 5);
	stats.def = std::max(1, (DragonGetDefense() + level) / 5);
	stats.spd = std::max(1, (DragonGetSpeed() + level) / 5);
	stats.mag = std::max(1, (DragonGetMagic() + level) / 5);
	stats.p = std::max(0, std::min(45, DragonGetDefense() / 10));
	stats.m = std::max(0, std::min(45, DragonGetMagic() / 10));
	stats.l = 0;
	stats.d = 0;
	stats.f = 0;
	stats.w = 0;
	stats.e = 0;
	stats.a = 0;
	const int avatarType = std::max((int)DRAGON_AVATAR_WARRIOR, std::min((int)gSave.avatarType, (int)DRAGON_AVATAR_COUNT - 1));
	switch(avatarType) {
		case DRAGON_AVATAR_WARRIOR:
			stats.e += 8;
			break;
		case DRAGON_AVATAR_SORCERER:
			stats.l += 8;
			break;
		default:
			stats.a += 8;
			break;
	}
	return stats;
}

int Battle::ElementResistanceForStats (const CombatStats& stats, int element) const {
	switch(element) {
		case kLegacyElementLight: return stats.l;
		case kLegacyElementDark: return stats.d;
		case kLegacyElementFire: return stats.f;
		case kLegacyElementWater: return stats.w;
		case kLegacyElementEarth: return stats.e;
		case kLegacyElementAir: return stats.a;
		default: return 0;
	}
}

int Battle::RollLegacyAttackDamage (int baseDamage, int hitPercent, int element, const CombatStats& targetStats, int targetLevel, bool& missed, bool& heal) const {
	missed = false;
	heal = false;
	int damage = baseDamage;
	if(damage > kLegacyMaxDamage)
		damage = kLegacyMaxDamage;
	if(hitPercent > kLegacyMaxHitPercent)
		hitPercent = kLegacyMaxHitPercent;
	long targetAbsorb = (2 * targetStats.str + 5 * targetStats.def + 2 * targetStats.spd + targetStats.mag + targetLevel) / 4;
	int targetEvade = (targetStats.str + 3 * targetStats.def + 5 * targetStats.spd + targetStats.mag + targetLevel) / 400;
	if(targetAbsorb > kLegacyMaxAbsorb)
		targetAbsorb = kLegacyMaxAbsorb;
	if(targetEvade > kLegacyMaxEvadePercent)
		targetEvade = kLegacyMaxEvadePercent;
	const int hitRoll = (int)ENode::GetRandom(100);
	if(hitRoll > (hitPercent - targetEvade)) {
		missed = true;
		return 0;
	}

	damage = damage * ((int)ENode::GetRandom(11) + 94) / 100;
	damage = damage * (100 - targetStats.p) / 100;
	if(damage < 0)
		damage = 0;
	damage = damage * (100 - ElementResistanceForStats(targetStats, element)) / 100;
	if(damage >= 0) {
		damage -= (int)targetAbsorb;
		if(damage > kLegacyMaxDamage)
			damage = kLegacyMaxDamage;
		else if(damage < 0)
			damage = 0;
		heal = false;
	}
	else {
		damage += (int)targetAbsorb;
		if(damage > 0) {
			damage = 1;
			heal = true;
		}
		else {
			damage *= -1;
			heal = true;
		}
		if(damage > kLegacyMaxDamage)
			damage = kLegacyMaxDamage;
	}
	return damage;
}

int Battle::RollLegacySpellDamage (int baseDamage, int element, bool isMagic, const CombatStats& targetStats, int targetLevel, bool& heal) const {
	heal = false;
	int damage = baseDamage;
	if(damage > kLegacyMaxDamage)
		damage = kLegacyMaxDamage;
	long targetAbsorb = (targetStats.str * 2 + targetStats.def * 5 + targetStats.spd * 2 + targetStats.mag + targetLevel) / 4;
	if(isMagic)
		targetAbsorb = (targetStats.str + targetStats.def * 3 + targetStats.spd + targetStats.mag * 5 + targetLevel) / 4;
	int targetResist = isMagic ? targetStats.m : targetStats.p;
	if(targetAbsorb > kLegacyMaxAbsorb)
		targetAbsorb = kLegacyMaxAbsorb;
	if(isMagic)
		damage = damage * ((int)ENode::GetRandom(51) + 99) / 100;
	else
		damage = damage * ((int)ENode::GetRandom(11) + 94) / 100;
	damage = damage * (100 - targetResist) / 100;
	if(damage < 0)
		damage = 0;
	damage = damage * (100 - ElementResistanceForStats(targetStats, element)) / 100;
	if(damage >= 0) {
		damage -= (int)targetAbsorb;
		if(damage > kLegacyMaxDamage)
			damage = kLegacyMaxDamage;
		else if(damage < 0)
			damage = 0;
		heal = false;
	}
	else {
		damage += (int)targetAbsorb;
		if(damage > 0) {
			damage = 1;
			heal = true;
		}
		else {
			damage *= -1;
			heal = true;
		}
		if(damage > kLegacyMaxDamage)
			damage = kLegacyMaxDamage;
	}
	return damage;
}

void Battle::BuildEnemy () {
	hasBattleRequest = DragonConsumeBattleRequest(battleRequest);
	skillMenuActive = false;
	itemMenuActive = false;
	legacyCommandMenuMode = LEGACY_COMMAND_MENU_NONE;
	hasPendingLegacyCommandAction = false;
	pendingLegacyCommandAction = LegacyCommandAction();
	legacyCommandActions.clear();
	battleDone = false;
	battleWon = false;
	battleRetreated = false;
	startLevel = std::max(1, (int)gSave.level);
	levelGain = 0;
	defeatGoldLoss = 0;
	retreatHealthLoss = 0;
	victoryXP = 0;
	victoryGold = 0;
	victoryPotionDrops = 0;
	victoryScrollDrops = 0;
	victoryHealthRecovered = 0;
	victoryLegacyLootItems = 0;
	victoryLegacyLootGold = 0;
	victoryLegacyLootFirstItem = "";
	victoryMilestoneRewards = "";
	roundNumber = 1;
	playerEffectType = BATTLE_EFFECT_NONE;
	enemyEffectType = BATTLE_EFFECT_NONE;
	playerEffectDuration = 0;
	enemyEffectDuration = 0;
	playerEffectTimestamp = 0;
	enemyEffectTimestamp = 0;
	validationPlayerActionSerial = 0;
	validationPlayerActionText = "";
	validationEnemyActionSerial = 0;
	validationEnemyActionText = "";
	enemyTargetRect = ERect();
	ClearEnemyGroup();
	int areaIndex = std::max(0, std::min(gSelectedArea, DRAGON_ALPHA_AREA_COUNT - 1));
	if(hasBattleRequest)
		areaIndex = std::max(0, std::min((int)battleRequest.areaIndex, DRAGON_ALPHA_AREA_COUNT - 1));
	if(hasBattleRequest and battleRequest.legacyPluginId != 0) {
		switch(battleRequest.legacyPluginId) {
			case 1: areaIndex = DRAGON_AREA_ELEUSIS; break;
			case 1000: areaIndex = DRAGON_AREA_MEADOWS; break;
			case 4: areaIndex = DRAGON_AREA_FORESTS; break;
			case 2: areaIndex = DRAGON_AREA_CAVES; break;
			case 3: areaIndex = (battleRequest.legacyMapId == 133 ? DRAGON_AREA_PEAK : DRAGON_AREA_MOUNTAINS); break;
			default: break;
		}
	}
	uint16_t backgroundPluginId = 0;
	uint16_t backgroundMapId = 0;
	if(hasBattleRequest and battleRequest.legacyPluginId != 0 and battleRequest.legacyMapId != 0) {
		backgroundPluginId = battleRequest.legacyPluginId;
		backgroundMapId = battleRequest.legacyMapId;
	}
	else
		DecodeLegacyPluginMapState(backgroundPluginId, backgroundMapId);

	gSelectedArea = areaIndex;
	areaInfo = DragonAreaByIndex(areaIndex);
	if(areaInfo == NULL)
		areaInfo = DragonAreaByIndex(0);
	LoadLegacyBattleBackgroundForContext(imageBackground, areaIndex, backgroundPluginId, backgroundMapId);
	const bool isStoryBattle = hasBattleRequest and (battleRequest.battleType == DRAGON_BATTLE_GATE or battleRequest.battleType == DRAGON_BATTLE_BOSS);
	storyBattle = isStoryBattle;
	trialBattle = hasBattleRequest and (battleRequest.completionFlagBit == kChallengeOneFlag or battleRequest.completionFlagBit == kChallengeTwoFlag);
	rareRoamingBattle = hasBattleRequest and battleRequest.battleType == DRAGON_BATTLE_RANDOM and
		(battleRequest.legacyEncounterRare != 0 or battleRequest.bonusXP > 0 or battleRequest.bonusGold > 0);
	strictLegacyBattle = hasBattleRequest;
	fallbackBattlePath = false;
	fallbackBattleReason = "";
	battleSourceTag = strictLegacyBattle ? "LEGACY" : "RUNTIME";

	BATTLE_TRACE(
		"[Battle] BuildEnemy start: request=%d type=%d area=%d plugin=%u map=%u group=%u forced=%s",
		(int)hasBattleRequest,
		hasBattleRequest ? (int)battleRequest.battleType : -1,
		areaIndex,
		hasBattleRequest ? (unsigned)battleRequest.legacyPluginId : 0u,
		hasBattleRequest ? (unsigned)battleRequest.legacyMapId : 0u,
		hasBattleRequest ? (unsigned)battleRequest.legacyGroupId : 0u,
		(hasBattleRequest and battleRequest.forcedEnemy[0] != '\0') ? battleRequest.forcedEnemy : "<none>"
	);

	bool usedLegacyGroup = false;
	int legacyGroupReferenceErrors = 0;
	auto buildUnitFromLegacyMonster = [&] (uint16_t sourceMonsterId, const DragonLegacyMonsterData* monster, bool bossUnit, int formationX, int formationY) {
		if(monster == NULL) {
			legacyGroupReferenceErrors++;
			BATTLE_TRACE(
				"[Battle] Missing legacy monster: plugin=%u monster=%u boss=%d",
				hasBattleRequest ? (unsigned)battleRequest.legacyPluginId : 0u,
				(unsigned)sourceMonsterId,
				(int)bossUnit
			);
			return;
		}
		EnemyData unit = {};
		unit.name = (monster->name != NULL and monster->name[0] != '\0') ? monster->name : (bossUnit ? "Boss" : "Enemy");
		const int strength = std::max(1, (int)monster->stats[0]);
		const int defense = std::max(1, (int)monster->stats[1]);
		const int speed = std::max(1, (int)monster->stats[2]);
		const int magic = std::max(1, (int)monster->stats[3]);
		const int levelBase = std::max(std::max(strength, defense), std::max(speed, magic));
		unit.level = std::max(1, std::min(99, levelBase + (bossUnit ? 1 : 0)));
		int rawHealth = 4 * (int)monster->stats[0] + 24 * (int)monster->stats[1];
		unit.healthMax = std::max(12, rawHealth);
		unit.health = unit.healthMax;
		unit.statStrength = strength;
		unit.statDefense = defense;
		unit.statSpeed = speed;
		unit.statMagic = magic;
		unit.resistPhysical = std::max(-300, std::min(300, (int)monster->stats[4]));
		unit.resistMagic = std::max(-300, std::min(300, (int)monster->stats[5]));
		unit.resistLight = std::max(-300, std::min(300, (int)monster->stats[6]));
		unit.resistDark = std::max(-300, std::min(300, (int)monster->stats[7]));
		unit.resistFire = std::max(-300, std::min(300, (int)monster->stats[8]));
		unit.resistWater = std::max(-300, std::min(300, (int)monster->stats[9]));
		unit.resistEarth = std::max(-300, std::min(300, (int)monster->stats[10]));
		unit.resistAir = std::max(-300, std::min(300, (int)monster->stats[11]));
		unit.attack = std::max(1, strength * 3 + speed);
		unit.defense = std::max(1, defense * 3 + speed / 2);
		unit.magic = std::max(1, magic * 3 + speed / 2);
		unit.rewardXP = std::max(2, (strength + defense + speed + magic) * 2 + (bossUnit ? 48 : 0));
		unit.rewardGold = std::max(2, (strength + defense + magic) / 2 + (bossUnit ? 22 : 4));
		PushEnemyUnit(unit, monster->pluginId, monster->id, monster->spriteId, (int)(monster->spriteId % 4), formationX, formationY, bossUnit);
		usedLegacyGroup = true;
		BATTLE_TRACE(
			"[Battle] Legacy unit: plugin=%u monster=%u sprite=%u boss=%d hp=%d atk=%d def=%d mag=%d rewardXP=%d rewardGold=%d",
			(unsigned)monster->pluginId,
			(unsigned)monster->id,
			(unsigned)monster->spriteId,
			(int)bossUnit,
			unit.healthMax,
			unit.attack,
			unit.defense,
			unit.magic,
			unit.rewardXP,
			unit.rewardGold
		);
	};

	if(hasBattleRequest and battleRequest.legacyPluginId != 0 and battleRequest.legacyGroupId != 0) {
		const DragonLegacyGroupData* group = DragonFindLegacyGroupData(battleRequest.legacyPluginId, battleRequest.legacyGroupId);
		if(group != NULL) {
			// Legacy `Point` payloads are stored in v/h order; preserve original
			// draw placement semantics by swapping into h/v battle coordinates.
			const int legacyBossX = (int)group->bossY;
			const int legacyBossY = (int)group->bossX;
			BATTLE_TRACE(
				"[Battle] Legacy group found: plugin=%u group=%u monsters=%d boss=%u bossLoc=(%d,%d)",
				(unsigned)group->pluginId,
				(unsigned)group->id,
				group->monsterCount,
				(unsigned)group->bossMonsterId,
				legacyBossX,
				legacyBossY
			);
			if(group->bossMonsterId != 0 and (battleRequest.battleType == DRAGON_BATTLE_GATE or battleRequest.battleType == DRAGON_BATTLE_BOSS)) {
				const DragonLegacyMonsterData* boss = DragonFindLegacyMonsterData(group->pluginId, group->bossMonsterId);
				buildUnitFromLegacyMonster(group->bossMonsterId, boss, true, legacyBossX, legacyBossY);
			}
			for(int i = 0; i < 32; i++) {
				if(group->monsters[i] == 0)
					continue;
				const DragonLegacyMonsterData* monster = DragonFindLegacyMonsterData(group->pluginId, group->monsters[i]);
				buildUnitFromLegacyMonster(group->monsters[i], monster, false, i % 4, i / 4);
			}
			if(group->bossMonsterId != 0 and (battleRequest.battleType != DRAGON_BATTLE_GATE and battleRequest.battleType != DRAGON_BATTLE_BOSS)) {
				const DragonLegacyMonsterData* boss = DragonFindLegacyMonsterData(group->pluginId, group->bossMonsterId);
				buildUnitFromLegacyMonster(group->bossMonsterId, boss, true, legacyBossX, legacyBossY);
			}
			if(legacyGroupReferenceErrors > 0) {
				fallbackBattlePath = true;
				fallbackBattleReason = EString().Format(
					"Legacy group P%u G%u has %d unresolved monster reference(s).",
					(unsigned)group->pluginId,
					(unsigned)group->id,
					legacyGroupReferenceErrors
				);
				BATTLE_TRACE("[Battle] %s", (const char*)fallbackBattleReason);
			}
		}
		else {
			fallbackBattlePath = true;
			fallbackBattleReason = EString().Format("Missing legacy group P%u G%u.", (unsigned)battleRequest.legacyPluginId, (unsigned)battleRequest.legacyGroupId);
			BATTLE_TRACE("[Battle] %s", (const char*)fallbackBattleReason);
		}
	}
	else if(strictLegacyBattle) {
		fallbackBattlePath = true;
		fallbackBattleReason = "Missing legacy plugin/group identifiers in battle request.";
		BATTLE_TRACE("[Battle] %s", (const char*)fallbackBattleReason);
	}

	if(legacyGroupReferenceErrors > 0) {
		fallbackBattlePath = true;
		if(fallbackBattleReason.IsEmpty())
			fallbackBattleReason = "Legacy group contains unresolved monster references.";
		battleDone = true;
		battleWon = false;
		battleRetreated = true;
		skillMenuActive = false;
		itemMenuActive = false;
		DragonPublishBattleResult(false, hasBattleRequest ? &battleRequest : NULL, true);
		actionText = "Legacy battle data unavailable.";
		actionTimestamp = GetMilliseconds();
		battleSourceTag = "LEGACY-MISSING";
		BATTLE_TRACE("[Battle] Strict legacy battle aborted: %s", (const char*)fallbackBattleReason);
		return;
	}

	if(usedLegacyGroup == false) {
		fallbackBattlePath = true;
		if(fallbackBattleReason.IsEmpty())
			fallbackBattleReason = "No legacy group resolved for this battle.";
		battleDone = true;
		battleWon = false;
		battleRetreated = true;
		skillMenuActive = false;
		itemMenuActive = false;
		DragonPublishBattleResult(false, hasBattleRequest ? &battleRequest : NULL, true);
		actionText = "Legacy battle data unavailable.";
		actionTimestamp = GetMilliseconds();
		battleSourceTag = "LEGACY-MISSING";
		BATTLE_TRACE("[Battle] Strict legacy battle aborted: %s", (const char*)fallbackBattleReason);
		return;
	}

	if(enemyGroup.empty()) {
		fallbackBattlePath = true;
		if(fallbackBattleReason.IsEmpty())
			fallbackBattleReason = "Legacy battle resolved no enemy units.";
		battleDone = true;
		battleWon = false;
		battleRetreated = true;
		skillMenuActive = false;
		itemMenuActive = false;
		DragonPublishBattleResult(false, hasBattleRequest ? &battleRequest : NULL, true);
		actionText = "Legacy battle data unavailable.";
		actionTimestamp = GetMilliseconds();
		battleSourceTag = "LEGACY-MISSING";
		BATTLE_TRACE("[Battle] Strict legacy battle aborted: %s", (const char*)fallbackBattleReason);
		return;
	}

	if(hasBattleRequest) {
		const int bonusXP = std::max(0, (int)battleRequest.bonusXP);
		const int bonusGold = std::max(0, (int)battleRequest.bonusGold);
		if(not enemyGroup.empty()) {
			enemyGroup[0].data.rewardXP += bonusXP;
			enemyGroup[0].data.rewardGold += bonusGold;
			totalEnemyRewardXP += bonusXP;
			totalEnemyRewardGold += bonusGold;
		}
	}

	activeEnemyIndex = FirstLivingEnemyIndex();
	if(activeEnemyIndex < 0)
		activeEnemyIndex = 0;
	if(usedLegacyGroup and isStoryBattle) {
		for(size_t i = 0; i < enemyGroup.size(); i++) {
			if(enemyGroup[i].boss) {
				activeEnemyIndex = (int)i;
				break;
			}
		}
	}
	SyncActiveEnemyFromGroup();

	if(hasBattleRequest and battleRequest.forcedEnemy[0] != '\0') {
		enemy.name = battleRequest.forcedEnemy;
		if(activeEnemyIndex >= 0 and activeEnemyIndex < (int)enemyGroup.size())
			enemyGroup[(size_t)activeEnemyIndex].data.name = enemy.name;
	}

	if(activeEnemyIndex >= 0 and activeEnemyIndex < (int)enemyGroup.size()) {
		enemy.rewardXP = enemyGroup[(size_t)activeEnemyIndex].data.rewardXP;
		enemy.rewardGold = enemyGroup[(size_t)activeEnemyIndex].data.rewardGold;
	}
	if(totalEnemyRewardXP <= 0)
		totalEnemyRewardXP = std::max(1, enemy.rewardXP);
	if(totalEnemyRewardGold <= 0)
		totalEnemyRewardGold = std::max(1, enemy.rewardGold);
	battleSourceTag = "LEGACY";

#if defined(DRAGON_TEST)
	if((DragonAutomationCaseIs("Validation_Battle_VictoryPanel")
			|| DragonAutomationCaseIs("Validation_WorldMap_ChallengeVictoryProgression")
			|| DragonAutomationCaseIs("Validation_WorldMap_GateBattleProgression"))
		&& enemyGroup.empty() == false) {
		enemyGroup.resize(1);
		enemyGroup[0].data.health = 1;
		activeEnemyIndex = 0;
		SyncActiveEnemyFromGroup();
	}
	if(DragonAutomationCaseIs("Validation_Battle_MultiEnemyContinue") && enemyGroup.empty() == false) {
		enemyGroup[0].data.health = 1;
		activeEnemyIndex = 0;
		SyncActiveEnemyFromGroup();
	}
	if((DragonAutomationCaseIs("Validation_Battle_EnemySpecialResponse")
			|| DragonAutomationCaseIs("Validation_Battle_EnemyHealResponse")
			|| DragonAutomationCaseIs("Validation_Battle_NoDamageResponse"))
		&& enemyGroup.empty() == false) {
		enemyGroup.resize(1);
		if(DragonAutomationCaseIs("Validation_Battle_EnemyHealResponse"))
			enemyGroup[0].data.health = std::max(1, enemyGroup[0].data.healthMax / 2);
		activeEnemyIndex = 0;
		SyncActiveEnemyFromGroup();
	}
	if(DragonAutomationCaseIs("Validation_Battle_ElementalResistOutcome") && enemyGroup.empty() == false) {
		enemyGroup.resize(1);
		enemyGroup[0].data.health = std::max(1, enemyGroup[0].data.healthMax / 2);
		activeEnemyIndex = 0;
		SyncActiveEnemyFromGroup();
	}
#endif

	BATTLE_TRACE(
		"[Battle] BuildEnemy ready: source=%s units=%d active=%d rewardXP=%d rewardGold=%d",
		(const char*)battleSourceTag,
		(int)enemyGroup.size(),
		activeEnemyIndex,
		totalEnemyRewardXP,
		totalEnemyRewardGold
	);

	int avatarIndex = std::max((int)DRAGON_AVATAR_WARRIOR, std::min((int)gSave.avatarType, (int)DRAGON_AVATAR_COUNT - 1));
	LoadImageOrFallback(imagePlayer, kPlayerArtName[avatarIndex], EColor(0x5eaef2ff));
	for(int variant = 0; variant < 4; variant++)
		LoadImageOrFallback(imageEnemyVariant[variant], kEnemyArtVariantName[areaIndex][variant], kEnemyArtName[areaIndex], EColor(0xee7c4cff));
	int variantIndex = std::max(0, std::min(enemyArtVariant, 3));
	LoadImageOrFallback(imageEnemy, kEnemyArtVariantName[areaIndex][variantIndex], kEnemyArtName[areaIndex], EColor(0xee7c4cff));
	LoadLegacyBattleBackgroundForContext(imageBackground, areaIndex, backgroundPluginId, backgroundMapId);

	retreatLocked = hasBattleRequest and battleRequest.noRetreat != 0;
	if(strictLegacyBattle and retreatLocked == false) {
		for(size_t i = 0; i < enemyGroup.size(); i++) {
			if(enemyGroup[i].boss) {
				retreatLocked = true;
				break;
			}
		}
	}
	skillCharge = std::max(35, std::min(kBattleSkillChargeMax, 48 + DragonGetSpeed() / 2));
	SeedTurnMeters();
	if(retreatLocked)
		actionText = "This is a decisive battle. Retreat is blocked.";
	else if(trialBattle)
		actionText = "Trial battle engaged. Defeat your challenger.";
	else if(rareRoamingBattle)
		actionText = "A rare foe appears. Choose your action.";
	else if(LivingEnemyCount() > 1)
		actionText = EString().Format("%d foes stand against you. Choose your action.", LivingEnemyCount());
	else
		actionText = "Choose your action.";
	actionTimestamp = GetMilliseconds();
}

int Battle::RollDamage (int attack, int defense, int bonus, bool& missed, bool& critical) const {
	missed = false;
	critical = false;

	int hitChance = 78 + (attack - defense) / 2 + bonus / 3;
	hitChance = std::max(58, std::min(96, hitChance));
	if((int)ENode::GetRandom(100) >= hitChance) {
		missed = true;
		return 0;
	}

	int critChance = std::max(4, std::min(35, 8 + bonus / 2));
	if((int)ENode::GetRandom(100) < critChance)
		critical = true;

	int power = attack + (int)ENode::GetRandom(10) + bonus;
	int mitigation = defense / 3 + (int)ENode::GetRandom(4);
	int total = power - mitigation;
	if(total < 1)
		total = 1;
	if(critical)
		total = total * 3 / 2 + 2;
	return total;
}

void Battle::GainSkillCharge (int amount) {
	if(amount <= 0)
		return;
	skillCharge = std::max(0, std::min(kBattleSkillChargeMax, skillCharge + amount));
}

bool Battle::CanUseSkill () const {
	return skillCharge >= kBattleSkillChargeMax;
}

bool Battle::AttackRequiresEnemyTargetSelection () const {
	return LivingEnemyCount() > 1;
}

bool Battle::MagicRequiresEnemyTargetSelection () const {
	if(strictLegacyBattle == false and CanUseSkill() == false)
		return false;
	if(LivingEnemyCount() <= 1)
		return false;
	const int avatarType = std::max((int)DRAGON_AVATAR_WARRIOR, std::min((int)gSave.avatarType, (int)DRAGON_AVATAR_COUNT - 1));
	// Legacy parity: warrior magic is always self-target; other avatars target enemies.
	if(avatarType == DRAGON_AVATAR_WARRIOR)
		return false;
	if(strictLegacyBattle)
		return true;
	// Non-legacy sorcerer magic keeps all-target behavior for adaptive mode.
	if(avatarType == DRAGON_AVATAR_SORCERER)
		return false;
	return true;
}

bool Battle::TechRequiresEnemyTargetSelection () const {
	if(strictLegacyBattle == false and CanUseSkill() == false)
		return false;
	if(strictLegacyBattle) {
		const int avatarType = std::max((int)DRAGON_AVATAR_WARRIOR, std::min((int)gSave.avatarType, (int)DRAGON_AVATAR_COUNT - 1));
		// Warrior tech is a self-rally in strict legacy mode.
		if(avatarType == DRAGON_AVATAR_WARRIOR)
			return false;
	}
	return LivingEnemyCount() > 1;
}

void Battle::ClearPendingTargetCommand () {
	pendingTargetCommand = PENDING_TARGET_NONE;
	hasPendingLegacyCommandAction = false;
	pendingLegacyCommandAction = LegacyCommandAction();
}

void Battle::SetActionTextThrottled (const EString& text, int64_t minIntervalMs) {
	const int64_t now = GetMilliseconds();
	if(actionText == text and now - actionTimestamp < std::max<int64_t>(0, minIntervalMs))
		return;
	actionText = text;
	actionTimestamp = now;
}

void Battle::BuildLegacyCommandList (LegacyCommandMenuMode mode, std::vector<LegacyCommandAction>& outCommands) const {
	outCommands.clear();
	const DragonLegacyCommandMenuMode sharedMode = (mode == LEGACY_COMMAND_MENU_TECH)
		? DRAGON_LEGACY_COMMAND_MENU_TECH
		: DRAGON_LEGACY_COMMAND_MENU_MAGIC;
	DragonLegacyCommandAction sharedActions[8] = {};
	const int sharedCount = DragonBuildLegacyCommandList(sharedMode, sharedActions, (int)(sizeof(sharedActions) / sizeof(sharedActions[0])));
	for(int i = 0; i < sharedCount; i++) {
		LegacyCommandAction action = {};
		action.name = sharedActions[i].name != nullptr ? sharedActions[i].name : "Action";
		action.effectElement = sharedActions[i].effectElement;
		action.power = sharedActions[i].power;
		action.hitPercent = sharedActions[i].hitPercent;
		action.defenseBoost = sharedActions[i].defenseBoost;
		action.isMagic = sharedActions[i].isMagic;
		action.isHeal = sharedActions[i].isHeal;
		action.isAll = sharedActions[i].isAll;
		action.usesSkillCharge = false;
		outCommands.push_back(action);
	}
}

bool Battle::LegacyCommandNeedsEnemyTarget (const LegacyCommandAction& action) const {
	if(action.isHeal or action.isAll)
		return false;
	return LivingEnemyCount() > 1;
}

void Battle::OpenLegacyCommandMenu (LegacyCommandMenuMode mode) {
	legacyCommandMenuMode = LEGACY_COMMAND_MENU_NONE;
	legacyCommandActions.clear();
	skillTertiaryRect = ERect();
	BuildLegacyCommandList(mode, legacyCommandActions);
	if(legacyCommandActions.empty()) {
		skillMenuActive = false;
		actionText = "No legacy actions available.";
		actionTimestamp = GetMilliseconds();
		PlaySoundIfLoaded(soundMiss);
		return;
	}
	ClearPendingTargetCommand();
	itemMenuActive = false;
	skillMenuActive = true;
	legacyCommandMenuMode = mode;
	actionText = mode == LEGACY_COMMAND_MENU_MAGIC
		? "Select a magic action."
		: "Select a tech action.";
	actionTimestamp = GetMilliseconds();
}

bool Battle::ResolveLegacyCommandAction (const LegacyCommandAction& action) {
	if(action.name.IsEmpty())
		return false;
	ClearPendingTargetCommand();
	skillMenuActive = false;
	itemMenuActive = false;
	legacyCommandMenuMode = LEGACY_COMMAND_MENU_NONE;
	legacyCommandActions.clear();
	if(battleDone)
		return false;
	SyncActiveEnemyFromGroup();
	if(LivingEnemyCount() <= 0) {
		FinishVictory();
		return true;
	}

	lastPlayerDamage = 0;
	lastEnemyDamage = 0;
	const int level = std::max(1, (int)gSave.level);
	auto castEffectForElement = [this] (int element) {
		switch(element) {
			case kLegacyElementFire: return BATTLE_EFFECT_FIRE_CAST;
			case kLegacyElementWater: return BATTLE_EFFECT_WATER_CAST;
			case kLegacyElementEarth: return BATTLE_EFFECT_EARTH_CAST;
			case kLegacyElementAir: return BATTLE_EFFECT_AIR_CAST;
			case kLegacyElementLight: return BATTLE_EFFECT_LIGHT_CAST;
			case kLegacyElementDark: return BATTLE_EFFECT_DARK_CAST;
			default: return BATTLE_EFFECT_NONE_CAST;
		}
	};
	auto hitEffectForElement = [this] (int element) {
		switch(element) {
			case kLegacyElementFire: return BATTLE_EFFECT_FIRE_HIT;
			case kLegacyElementWater: return BATTLE_EFFECT_WATER_HIT;
			case kLegacyElementEarth: return BATTLE_EFFECT_EARTH_HIT;
			case kLegacyElementAir: return BATTLE_EFFECT_AIR_HIT;
			case kLegacyElementLight: return BATTLE_EFFECT_LIGHT_HIT;
			case kLegacyElementDark: return BATTLE_EFFECT_DARK_HIT;
			default: return BATTLE_EFFECT_ATTACK_HIT;
		}
	};
	const int castEffect = castEffectForElement(action.effectElement);
	const int hitEffect = hitEffectForElement(action.effectElement);
	const bool strictLegacyCommandMath = strictLegacyBattle;
	int defenseBoost = std::max(0, action.defenseBoost);
	BATTLE_TRACE(
		"[Battle] LegacyCommand: name=%s magic=%d heal=%d all=%d power=%d hit=%d defenseBoost=%d target=%d",
		(const char*)action.name,
		(int)action.isMagic,
		(int)action.isHeal,
		(int)action.isAll,
		action.power,
		action.hitPercent,
		defenseBoost,
		activeEnemyIndex
	);

	auto rollLegacyCommandDamage = [&] (const CombatStats& targetStats, int targetLevel, bool& missed, bool& healed) {
		if(strictLegacyCommandMath) {
			missed = false;
			int baseDamage = action.isMagic
				? std::max(1, 5 * DragonGetMagic() + 2 * level)
				: std::max(1, DragonGetAttack() + 3 * DragonGetSpeed() + DragonGetMagic() + 2 * level);
			if(action.isAll)
				baseDamage = std::max(1, baseDamage * 4 / 5);
			return RollLegacySpellDamage(baseDamage, action.effectElement, action.isMagic, targetStats, std::max(1, targetLevel), healed);
		}

		const int baseDamage = action.isMagic
			? std::max(1, action.power + DragonGetMagic() * 6 + level * 2)
			: std::max(1, action.power + DragonGetAttack() * 4 + DragonGetSpeed() * 2 + level);
		const int hitPercent = std::max(58, std::min(100, action.hitPercent > 0 ? action.hitPercent : (76 + DragonGetSpeed() / 3)));
		return action.isMagic
			? RollLegacySpellDamage(baseDamage, action.effectElement, true, targetStats, std::max(1, targetLevel), healed)
			: RollLegacyAttackDamage(baseDamage, hitPercent, action.effectElement, targetStats, std::max(1, targetLevel), missed, healed);
	};

	auto applyEnemyUnitResult = [&] (EnemyUnitRuntime& unit, int& damagedTargets, int& healedTargets, int& missedTargets, int& totalDamage) {
		if(unit.data.health <= 0)
			return;
		const CombatStats targetStats = EnemyCombatStats(unit.data);
		bool missed = false;
		bool healed = false;
		const int damage = rollLegacyCommandDamage(targetStats, std::max(1, unit.data.level), missed, healed);
		if(missed) {
			missedTargets++;
			return;
		}
		if(healed) {
			unit.data.health = std::min(unit.data.healthMax, unit.data.health + std::max(0, damage));
			healedTargets++;
			return;
		}
		if(damage <= 0) {
			missedTargets++;
			return;
		}
		unit.data.health -= damage;
		if(unit.data.health < 0)
			unit.data.health = 0;
		damagedTargets++;
		totalDamage += damage;
	};

	if(action.isHeal) {
		const int maxHealth = DragonGetMaxHealth();
		const int before = gSave.health;
		const int healBase = action.isMagic
			? (5 * DragonGetMagic() + 2 * level)
			: (DragonGetAttack() + 3 * DragonGetSpeed() + DragonGetMagic() + 2 * level);
		const int heal = strictLegacyCommandMath
			? std::max(1, std::min(kLegacyMaxDamage, 40 * std::max(1, action.power) + healBase))
			: std::max(1, std::min(kLegacyMaxDamage, action.power + DragonGetMagic() * 4 + level * 2));
		gSave.health = (int16_t)std::min(maxHealth, (int)gSave.health + heal);
		const int gained = std::max(0, (int)gSave.health - before);
		actionText = gained > 0
			? EString().Format("%s restores %d HP.", (const char*)action.name, gained)
			: EString().Format("%s has no effect.", (const char*)action.name);
		validationPlayerActionSerial++;
		validationPlayerActionText = actionText;
		actionTimestamp = GetMilliseconds();
		TriggerPlayerEffect(BATTLE_EFFECT_HEAL_HIT, 520);
		PlaySoundIfLoaded(gained > 0 ? soundCast : soundMiss);
		if(gained > 0)
			DragonSaveCurrentSlot();
#if defined(DRAGON_TEST)
		if(DragonAutomationCaseIs("Validation_Battle_Magic_SeededOutcome")
			|| DragonAutomationCaseIs("Validation_Battle_Tech_SeededOutcome")) {
			roundNumber++;
			DragonSaveCurrentSlot();
			return true;
		}
#endif
		EString playerAction = actionText;
		ResolveEnemyTurn(defenseBoost, playerAction);
		return true;
	}

	if(action.isAll) {
		int totalDamage = 0;
		int damagedTargets = 0;
		int healedTargets = 0;
		int missedTargets = 0;
		for(size_t i = 0; i < enemyGroup.size(); i++)
			applyEnemyUnitResult(enemyGroup[i], damagedTargets, healedTargets, missedTargets, totalDamage);

		if(activeEnemyIndex < 0
			or activeEnemyIndex >= (int)enemyGroup.size()
			or enemyGroup[(size_t)activeEnemyIndex].data.health <= 0) {
			activeEnemyIndex = FirstLivingEnemyIndex();
		}
		SyncActiveEnemyFromGroup();
		lastPlayerDamage = totalDamage;
		if(damagedTargets > 0) {
			actionText = EString().Format("%s hits all foes for %d.", (const char*)action.name, totalDamage);
			TriggerEnemyEffect(hitEffect, 540);
			PlaySoundIfLoaded(soundCast);
		}
		else if(healedTargets > 0) {
			actionText = EString().Format("Enemy party absorbed %s.", (const char*)action.name);
			TriggerEnemyEffect(BATTLE_EFFECT_HEAL_HIT, 460);
			PlaySoundIfLoaded(soundMiss);
		}
		else {
			actionText = EString().Format("%s missed.", (const char*)action.name);
			TriggerEnemyEffect(castEffect, 420);
			PlaySoundIfLoaded(soundMiss);
		}
		actionTimestamp = GetMilliseconds();
		if(LivingEnemyCount() <= 0) {
			FinishVictory();
			return true;
		}
		EString playerAction = actionText;
		ResolveEnemyTurn(defenseBoost, playerAction);
		return true;
	}

	SyncActiveEnemyFromGroup();
	CombatStats targetStats = EnemyCombatStats(enemy);
	bool missed = false;
	bool healed = false;
	const int damage = rollLegacyCommandDamage(targetStats, level, missed, healed);

	if(missed) {
		actionText = EString().Format("%s missed.", (const char*)action.name);
		TriggerEnemyEffect(castEffect, 420);
		PlaySoundIfLoaded(soundMiss);
	}
	else if(healed) {
		enemy.health = std::min(enemy.healthMax, enemy.health + std::max(0, damage));
		if(activeEnemyIndex >= 0 and activeEnemyIndex < (int)enemyGroup.size())
			enemyGroup[(size_t)activeEnemyIndex].data.health = enemy.health;
		actionText = EString().Format("%s absorbed %s.", (const char*)enemy.name, (const char*)action.name);
		TriggerEnemyEffect(BATTLE_EFFECT_HEAL_HIT, 460);
		PlaySoundIfLoaded(soundMiss);
	}
	else if(damage <= 0) {
		actionText = EString().Format("%s dealt no damage.", (const char*)action.name);
		TriggerEnemyEffect(castEffect, 420);
		PlaySoundIfLoaded(soundMiss);
	}
	else {
		enemy.health -= damage;
		if(activeEnemyIndex >= 0 and activeEnemyIndex < (int)enemyGroup.size())
			enemyGroup[(size_t)activeEnemyIndex].data.health = enemy.health;
		lastPlayerDamage = damage;
		actionText = EString().Format("%s deals %d.", (const char*)action.name, damage);
		TriggerEnemyEffect(hitEffect, 520);
			PlaySoundIfLoaded(soundCast);
	}
	validationPlayerActionSerial++;
	validationPlayerActionText = actionText;
	actionTimestamp = GetMilliseconds();
#if defined(DRAGON_TEST)
	if(DragonAutomationCaseIs("Validation_Battle_ElementalNeutralOutcome")
		|| DragonAutomationCaseIs("Validation_Battle_ElementalResistOutcome")) {
		roundNumber++;
		DragonSaveCurrentSlot();
		return true;
	}
#endif
	if(enemy.health <= 0) {
		enemy.health = 0;
		if(activeEnemyIndex >= 0 and activeEnemyIndex < (int)enemyGroup.size())
			enemyGroup[(size_t)activeEnemyIndex].data.health = 0;
		if(HandleEnemyDefeat())
			return true;
		PlaySoundIfLoaded(soundHit);
		return true;
	}
	EString playerAction = actionText;
	ResolveEnemyTurn(defenseBoost, playerAction);
	return true;
}

void Battle::TriggerPlayerEffect (int effectType, int duration) {
	playerEffectType = effectType;
	playerEffectDuration = std::max(120, duration);
	playerEffectTimestamp = GetMilliseconds();
}

void Battle::TriggerEnemyEffect (int effectType, int duration) {
	enemyEffectType = effectType;
	enemyEffectDuration = std::max(120, duration);
	enemyEffectTimestamp = GetMilliseconds();
}

EImage* Battle::EffectImageByType (int effectType) {
	switch(effectType) {
		case BATTLE_EFFECT_ATTACK_HIT: return &imageFxAttackHit;
		case BATTLE_EFFECT_HEAL_HIT: return &imageFxHealHit;
		case BATTLE_EFFECT_FIRE_CAST: return &imageFxFireCast;
		case BATTLE_EFFECT_FIRE_HIT: return &imageFxFireHit;
		case BATTLE_EFFECT_WATER_CAST: return &imageFxWaterCast;
		case BATTLE_EFFECT_WATER_HIT: return &imageFxWaterHit;
		case BATTLE_EFFECT_EARTH_CAST: return &imageFxEarthCast;
		case BATTLE_EFFECT_EARTH_HIT: return &imageFxEarthHit;
		case BATTLE_EFFECT_AIR_CAST: return &imageFxAirCast;
		case BATTLE_EFFECT_AIR_HIT: return &imageFxAirHit;
		case BATTLE_EFFECT_LIGHT_CAST: return &imageFxLightCast;
		case BATTLE_EFFECT_LIGHT_HIT: return &imageFxLightHit;
		case BATTLE_EFFECT_DARK_CAST: return &imageFxDarkCast;
		case BATTLE_EFFECT_DARK_HIT: return &imageFxDarkHit;
		case BATTLE_EFFECT_NONE_CAST: return &imageFxNoneCast;
		default: return NULL;
	}
}

void Battle::DrawEffectForRect (const ERect& rect, int effectType, int64_t effectTimestamp, int duration) {
	if(effectType == BATTLE_EFFECT_NONE or effectTimestamp == 0 or duration <= 0)
		return;

	const int64_t elapsed = GetMilliseconds() - effectTimestamp;
	if(elapsed < 0 or elapsed >= duration)
		return;

	EImage* effectImage = EffectImageByType(effectType);
	if(effectImage == NULL or effectImage->IsEmpty())
		return;

	const int pad = std::max(8, std::min(rect.width, rect.height) / 4);
	const ERect drawRect(rect.x - pad, rect.y - pad, rect.width + pad * 2, rect.height + pad * 2);
	int alpha = 255 - (int)(elapsed * 190 / std::max(1, duration));
	alpha = std::max(72, std::min(255, alpha));
	DrawImageContain(*effectImage, drawRect, EColor((uint32_t)0xffffff00 | (uint32_t)alpha));
}

void Battle::ResolvePlayerAttack (bool usingSkill) {
	ClearPendingTargetCommand();
	if(battleDone)
		return;
	SyncActiveEnemyFromGroup();
	if(LivingEnemyCount() <= 0) {
		FinishVictory();
		return;
	}
	skillMenuActive = false;
	itemMenuActive = false;
	if(usingSkill and strictLegacyBattle == false and CanUseSkill() == false) {
		actionText = EString().Format("Skill charging... %d%%", skillCharge);
		actionTimestamp = GetMilliseconds();
		PlaySoundIfLoaded(soundMiss);
		return;
	}
	if(usingSkill and strictLegacyBattle == false)
		skillCharge = 0;

	int avatarType = std::max((int)DRAGON_AVATAR_WARRIOR, std::min((int)gSave.avatarType, (int)DRAGON_AVATAR_COUNT - 1));
	CombatStats targetStats = EnemyCombatStats(enemy);
	int element = 0;
	int hitPercent = std::max(55, std::min(96, 74 + DragonGetSpeed() / 3));
	int baseDamage = std::max(1, 24 + DragonGetAttack() * 5 + DragonGetSpeed() * 2);
	bool missed = false;
	bool healed = false;
	bool critical = false;
	int damage = 0;
	bool skipEnemyTurn = false;
	int healFromSkill = 0;

	if(usingSkill) {
		switch(avatarType) {
			case DRAGON_AVATAR_WARRIOR:
				element = kLegacyElementEarth;
				hitPercent = std::max(62, std::min(100, 80 + DragonGetSpeed() / 3));
				baseDamage = std::max(1, 40 + DragonGetAttack() * 6 + DragonGetSpeed() * 2);
				damage = RollLegacyAttackDamage(baseDamage, hitPercent, element, targetStats, 0, missed, healed);
				break;
			case DRAGON_AVATAR_SORCERER:
				element = kLegacyElementLight;
				baseDamage = std::max(1, 48 + DragonGetMagic() * 6 + DragonGetSpeed());
				damage = RollLegacySpellDamage(baseDamage, element, true, targetStats, 0, healed);
				break;
			default:
				element = kLegacyElementAir;
				hitPercent = std::max(65, std::min(100, 82 + DragonGetSpeed() / 3));
				baseDamage = std::max(1, 38 + (DragonGetAttack() + DragonGetSpeed()) * 3);
				damage = RollLegacyAttackDamage(baseDamage, hitPercent, element, targetStats, 0, missed, healed);
				break;
		}
	}
	else
		damage = RollLegacyAttackDamage(baseDamage, hitPercent, element, targetStats, 0, missed, healed);

#if defined(DRAGON_TEST)
	if((DragonAutomationCaseIs("Validation_Battle_VictoryPanel")
			|| DragonAutomationCaseIs("Validation_WorldMap_ChallengeVictoryProgression")
			|| DragonAutomationCaseIs("Validation_WorldMap_GateBattleProgression"))
		&& usingSkill == false) {
		missed = false;
		healed = false;
		damage = std::max(damage, 1);
	}
	if(DragonAutomationCaseIs("Validation_Battle_MultiEnemyContinue") && usingSkill == false) {
		missed = false;
		healed = false;
		damage = std::max(damage, 1);
	}
#endif

	if(missed == false and healed == false and damage > 0) {
		int critChance = usingSkill ? 18 : 10;
		critChance += std::max(0, DragonGetSpeed() / 18);
		critChance = std::max(4, std::min(35, critChance));
		if((int)ENode::GetRandom(100) < critChance) {
			critical = true;
			damage = damage * 3 / 2 + 2;
		}
	}

	lastEnemyDamage = 0;
	if(missed) {
		lastPlayerDamage = 0;
		actionText = usingSkill ? "Your technique missed." : "Your attack missed.";
		TriggerEnemyEffect(usingSkill ? BATTLE_EFFECT_NONE_CAST : BATTLE_EFFECT_WATER_CAST, usingSkill ? 380 : 320);
		PlaySoundIfLoaded(soundMiss);
	}
	else if(healed) {
		enemy.health = std::min(enemy.healthMax, enemy.health + damage);
		if(activeEnemyIndex >= 0 and activeEnemyIndex < (int)enemyGroup.size())
			enemyGroup[(size_t)activeEnemyIndex].data.health = enemy.health;
		lastPlayerDamage = 0;
		actionText = EString().Format("%s recovered %d HP.", (const char*)enemy.name, damage);
		TriggerEnemyEffect(BATTLE_EFFECT_HEAL_HIT, 430);
		PlaySoundIfLoaded(soundCast);
	}
	else if(damage <= 0) {
		lastPlayerDamage = 0;
		actionText = usingSkill ? "Your technique dealt no damage." : "Your attack dealt no damage.";
		TriggerEnemyEffect(usingSkill ? BATTLE_EFFECT_NONE_CAST : BATTLE_EFFECT_ATTACK_HIT, 320);
		PlaySoundIfLoaded(soundMiss);
	} else {
		enemy.health -= damage;
		if(activeEnemyIndex >= 0 and activeEnemyIndex < (int)enemyGroup.size())
			enemyGroup[(size_t)activeEnemyIndex].data.health = enemy.health;
		int totalDamage = damage;
		if(usingSkill == false) {
			lastPlayerDamage = damage;
			if(critical)
				actionText = EString().Format("Critical hit! %d damage.", damage);
			else
				actionText = EString().Format("Attack deals %d.", damage);
			TriggerEnemyEffect(BATTLE_EFFECT_ATTACK_HIT, critical ? 520 : 430);
			} else {
				const char* critPrefix = critical ? "Critical! " : "";
				switch(avatarType) {
				case DRAGON_AVATAR_WARRIOR:
					if((int)ENode::GetRandom(100) < 28) {
						skipEnemyTurn = true;
						actionText = EString().Format("%sPower Strike %d. Enemy staggered.", critPrefix, damage);
					} else {
						actionText = EString().Format("%sPower Strike %d.", critPrefix, damage);
					}
					TriggerEnemyEffect(BATTLE_EFFECT_EARTH_HIT, critical ? 560 : 460);
					break;
				case DRAGON_AVATAR_SORCERER: {
					int maxHealth = DragonGetMaxHealth();
					int before = gSave.health;
					int siphon = std::max(1, damage / 4);
					gSave.health = (int16_t)std::min(maxHealth, (int)gSave.health + siphon);
					healFromSkill = std::max(0, (int)gSave.health - before);
					actionText = EString().Format("%sArcane Surge %d. +%d HP.", critPrefix, damage, healFromSkill);
					TriggerEnemyEffect(BATTLE_EFFECT_LIGHT_HIT, critical ? 560 : 460);
					if(healFromSkill > 0)
						TriggerPlayerEffect(BATTLE_EFFECT_HEAL_HIT, 460);
					break;
				}
					default:
						if(enemy.health > 0 and (int)ENode::GetRandom(100) < 32) {
							bool extraMiss = false;
							bool extraHeal = false;
							int extraDamage = RollLegacyAttackDamage(
								std::max(1, 16 + DragonGetSpeed() * 4 + DragonGetAttack() * 2),
								std::max(58, std::min(100, 76 + DragonGetSpeed() / 4)),
								kLegacyElementAir,
								targetStats,
								0,
								extraMiss,
								extraHeal
							);
							if(extraMiss == false and extraHeal == false and extraDamage > 0) {
								enemy.health -= extraDamage;
								if(activeEnemyIndex >= 0 and activeEnemyIndex < (int)enemyGroup.size())
									enemyGroup[(size_t)activeEnemyIndex].data.health = enemy.health;
								totalDamage += extraDamage;
							}
						}
					actionText = EString().Format("%sQuick Volley %d.", critPrefix, totalDamage);
					TriggerEnemyEffect(BATTLE_EFFECT_AIR_HIT, critical ? 560 : 450);
					break;
			}
			lastPlayerDamage = totalDamage;
		}
		if(totalDamage > 0)
			PlaySoundIfLoaded(usingSkill ? soundCast : soundHit);
		else
			PlaySoundIfLoaded(soundMiss);
	}
	validationPlayerActionSerial++;
	validationPlayerActionText = actionText;
	actionTimestamp = GetMilliseconds();
	BATTLE_TRACE(
		"[Battle] PlayerAttack: usingSkill=%d target=%d name=%s damage=%d missed=%d healed=%d enemyHP=%d/%d",
		(int)usingSkill,
		activeEnemyIndex,
		(const char*)enemy.name,
		lastPlayerDamage,
		(int)missed,
		(int)healed,
		enemy.health,
		enemy.healthMax
	);
	if(usingSkill == false)
		GainSkillCharge(missed ? 24 : 30);

	if(healFromSkill > 0)
		DragonSaveCurrentSlot();

		if(enemy.health <= 0) {
			enemy.health = 0;
			if(activeEnemyIndex >= 0 and activeEnemyIndex < (int)enemyGroup.size())
				enemyGroup[(size_t)activeEnemyIndex].data.health = 0;
			if(HandleEnemyDefeat())
				return;
			PlaySoundIfLoaded(soundHit);
			return;
		}

#if defined(DRAGON_TEST)
		if(DragonAutomationCaseIs("Validation_Battle_SeededOutcome")) {
			roundNumber++;
			DragonSaveCurrentSlot();
			return;
		}
#endif

		if(skipEnemyTurn) {
			roundNumber++;
			DragonSaveCurrentSlot();
			return;
		}

	EString playerAction = actionText;
	ResolveEnemyTurn(0, playerAction);
}

void Battle::ResolvePlayerMagic () {
	ClearPendingTargetCommand();
	if(battleDone)
		return;
	SyncActiveEnemyFromGroup();
	if(LivingEnemyCount() <= 0) {
		FinishVictory();
		return;
	}
	skillMenuActive = false;
	itemMenuActive = false;
	if(strictLegacyBattle == false and CanUseSkill() == false) {
		actionText = EString().Format("Magic charging... %d%%", std::max(0, std::min(kBattleSkillChargeMax, skillCharge)));
		actionTimestamp = GetMilliseconds();
		PlaySoundIfLoaded(soundMiss);
		return;
	}

	if(strictLegacyBattle == false)
		skillCharge = 0;
	lastPlayerDamage = 0;
	lastEnemyDamage = 0;
	const int avatarType = std::max((int)DRAGON_AVATAR_WARRIOR, std::min((int)gSave.avatarType, (int)DRAGON_AVATAR_COUNT - 1));
	const int level = std::max(1, (int)gSave.level);
	if(strictLegacyBattle) {
		// Strict legacy command semantics: MAGIC maps to deterministic avatar commands.
		if(avatarType == DRAGON_AVATAR_WARRIOR) {
			const int maxHealth = DragonGetMaxHealth();
			const int before = gSave.health;
			const int heal = std::max(1, std::min(kLegacyMaxDamage, 80 + 5 * DragonGetMagic() + 2 * level));
			gSave.health = (int16_t)std::min(maxHealth, (int)gSave.health + heal);
			const int gained = std::max(0, (int)gSave.health - before);
			actionText = gained > 0 ? EString().Format("Cure restores %d HP.", gained) : "Cure has no effect.";
			actionTimestamp = GetMilliseconds();
			TriggerPlayerEffect(BATTLE_EFFECT_HEAL_HIT, 500);
			PlaySoundIfLoaded(gained > 0 ? soundCast : soundMiss);
			DragonSaveCurrentSlot();
			BATTLE_TRACE(
				"[Battle] PlayerMagicLegacy: avatar=%d self-heal gained=%d hp=%d/%d",
				avatarType,
				gained,
				(int)gSave.health,
				DragonGetMaxHealth()
			);
			EString playerAction = actionText;
			ResolveEnemyTurn(std::max(2, DragonGetDefense() / 4), playerAction);
			return;
		}

		CombatStats targetStats = EnemyCombatStats(enemy);
		bool missed = false;
		bool healed = false;
		const int element = (avatarType == DRAGON_AVATAR_SORCERER) ? kLegacyElementLight : kLegacyElementAir;
		int damage = 0;
		if(avatarType == DRAGON_AVATAR_SORCERER) {
			const int baseDamage = std::max(1, 84 + 6 * DragonGetMagic() + 2 * level);
			damage = RollLegacySpellDamage(baseDamage, element, true, targetStats, level, healed);
		}
		else {
			const int hitPercent = std::max(62, std::min(100, 82 + DragonGetSpeed() / 4));
			const int baseDamage = std::max(1, 40 + DragonGetMagic() * 3 + DragonGetAttack() * 2 + DragonGetSpeed() * 2);
			damage = RollLegacyAttackDamage(baseDamage, hitPercent, element, targetStats, level, missed, healed);
		}

		if(missed) {
			lastPlayerDamage = 0;
			actionText = "Magic missed.";
			TriggerEnemyEffect(BATTLE_EFFECT_NONE_CAST, 320);
			PlaySoundIfLoaded(soundMiss);
		}
		else if(healed) {
			enemy.health = std::min(enemy.healthMax, enemy.health + damage);
			if(activeEnemyIndex >= 0 and activeEnemyIndex < (int)enemyGroup.size())
				enemyGroup[(size_t)activeEnemyIndex].data.health = enemy.health;
			lastPlayerDamage = 0;
			actionText = EString().Format("%s absorbed the spell.", (const char*)enemy.name);
			TriggerEnemyEffect(BATTLE_EFFECT_HEAL_HIT, 420);
			PlaySoundIfLoaded(soundMiss);
		}
		else if(damage <= 0) {
			lastPlayerDamage = 0;
			actionText = "Magic dealt no damage.";
			TriggerEnemyEffect((element == kLegacyElementAir) ? BATTLE_EFFECT_AIR_CAST : BATTLE_EFFECT_LIGHT_CAST, 360);
			PlaySoundIfLoaded(soundMiss);
		}
		else {
			enemy.health -= damage;
			if(activeEnemyIndex >= 0 and activeEnemyIndex < (int)enemyGroup.size())
				enemyGroup[(size_t)activeEnemyIndex].data.health = enemy.health;
			lastPlayerDamage = damage;
			actionText = EString().Format("Magic deals %d.", damage);
			TriggerEnemyEffect((element == kLegacyElementAir) ? BATTLE_EFFECT_AIR_HIT : BATTLE_EFFECT_LIGHT_HIT, 460);
			PlaySoundIfLoaded(soundCast);
		}

		actionTimestamp = GetMilliseconds();
		BATTLE_TRACE(
			"[Battle] PlayerMagicLegacy: avatar=%d target=%d damage=%d missed=%d healed=%d enemyHP=%d/%d",
			avatarType,
			activeEnemyIndex,
			lastPlayerDamage,
			(int)missed,
			(int)healed,
			enemy.health,
			enemy.healthMax
		);
		if(enemy.health <= 0) {
			enemy.health = 0;
			if(activeEnemyIndex >= 0 and activeEnemyIndex < (int)enemyGroup.size())
				enemyGroup[(size_t)activeEnemyIndex].data.health = 0;
			if(HandleEnemyDefeat())
				return;
			PlaySoundIfLoaded(soundHit);
			return;
		}

		EString playerAction = actionText;
		ResolveEnemyTurn(0, playerAction);
		return;
	}

	CombatStats targetStats = EnemyCombatStats(enemy);
	bool healed = false;
	const int element = (avatarType == DRAGON_AVATAR_SORCERER) ? kLegacyElementLight : kLegacyElementAir;
	const int baseDamage = std::max(1, 80 + 5 * DragonGetMagic() + 2 * level);
	if(avatarType == DRAGON_AVATAR_SORCERER and LivingEnemyCount() > 1) {
		int totalDamage = 0;
		int damagedTargets = 0;
		int healedTargets = 0;
		for(size_t i = 0; i < enemyGroup.size(); i++) {
			EnemyUnitRuntime& unit = enemyGroup[i];
			if(unit.data.health <= 0)
				continue;
			const CombatStats unitStats = EnemyCombatStats(unit.data);
			bool unitHealed = false;
			const int unitDamage = RollLegacySpellDamage(baseDamage, element, true, unitStats, std::max(1, unit.data.level), unitHealed);
			if(unitHealed) {
				unit.data.health = std::min(unit.data.healthMax, unit.data.health + std::max(0, unitDamage));
				healedTargets++;
			}
			else if(unitDamage > 0) {
				unit.data.health -= unitDamage;
				if(unit.data.health < 0)
					unit.data.health = 0;
				totalDamage += unitDamage;
				damagedTargets++;
			}
		}
		SyncActiveEnemyFromGroup();
		if(activeEnemyIndex >= 0 and activeEnemyIndex < (int)enemyGroup.size() and enemyGroup[(size_t)activeEnemyIndex].data.health <= 0) {
			enemyGroup[(size_t)activeEnemyIndex].data.health = 0;
			enemy.health = 0;
			if(HandleEnemyDefeat())
				return;
		}

		lastPlayerDamage = totalDamage;
		if(damagedTargets > 0) {
			actionText = EString().Format("Magic hits all foes for %d.", totalDamage);
			TriggerEnemyEffect(BATTLE_EFFECT_LIGHT_HIT, 520);
			PlaySoundIfLoaded(soundCast);
		}
		else if(healedTargets > 0) {
			actionText = "Enemy party absorbed your magic.";
			TriggerEnemyEffect(BATTLE_EFFECT_HEAL_HIT, 420);
			PlaySoundIfLoaded(soundMiss);
		}
		else {
			actionText = "Magic dealt no damage.";
			TriggerEnemyEffect(BATTLE_EFFECT_LIGHT_CAST, 380);
			PlaySoundIfLoaded(soundMiss);
		}
		actionTimestamp = GetMilliseconds();
		BATTLE_TRACE(
			"[Battle] PlayerMagic: avatar=%d all-target damaged=%d healedTargets=%d totalDamage=%d living=%d",
			avatarType,
			damagedTargets,
			healedTargets,
			totalDamage,
			LivingEnemyCount()
		);
		if(LivingEnemyCount() <= 0) {
			FinishVictory();
			return;
		}
		EString playerAction = actionText;
		ResolveEnemyTurn(0, playerAction);
		return;
	}

	const int damage = RollLegacySpellDamage(baseDamage, element, true, targetStats, level, healed);

	if(healed) {
		enemy.health = std::min(enemy.healthMax, enemy.health + damage);
		if(activeEnemyIndex >= 0 and activeEnemyIndex < (int)enemyGroup.size())
			enemyGroup[(size_t)activeEnemyIndex].data.health = enemy.health;
		actionText = EString().Format("%s absorbed the spell.", (const char*)enemy.name);
		TriggerEnemyEffect(BATTLE_EFFECT_HEAL_HIT, 420);
		PlaySoundIfLoaded(soundMiss);
	}
	else if(damage <= 0) {
		actionText = "Magic dealt no damage.";
		TriggerEnemyEffect((element == kLegacyElementAir) ? BATTLE_EFFECT_AIR_CAST : BATTLE_EFFECT_LIGHT_CAST, 380);
		PlaySoundIfLoaded(soundMiss);
	}
	else {
		enemy.health -= damage;
		if(activeEnemyIndex >= 0 and activeEnemyIndex < (int)enemyGroup.size())
			enemyGroup[(size_t)activeEnemyIndex].data.health = enemy.health;
		lastPlayerDamage = damage;
		actionText = EString().Format("Magic deals %d.", damage);
		TriggerEnemyEffect((element == kLegacyElementAir) ? BATTLE_EFFECT_AIR_HIT : BATTLE_EFFECT_LIGHT_HIT, 460);
		PlaySoundIfLoaded(soundCast);
	}

	actionTimestamp = GetMilliseconds();
	BATTLE_TRACE(
		"[Battle] PlayerMagic: avatar=%d target=%d name=%s damage=%d healed=%d enemyHP=%d/%d",
		avatarType,
		activeEnemyIndex,
		(const char*)enemy.name,
		lastPlayerDamage,
		(int)healed,
		enemy.health,
		enemy.healthMax
	);
	if(enemy.health <= 0) {
		enemy.health = 0;
		if(activeEnemyIndex >= 0 and activeEnemyIndex < (int)enemyGroup.size())
			enemyGroup[(size_t)activeEnemyIndex].data.health = 0;
		if(HandleEnemyDefeat())
			return;
		PlaySoundIfLoaded(soundHit);
		return;
	}

	EString playerAction = actionText;
	ResolveEnemyTurn(0, playerAction);
}

void Battle::ResolvePlayerTech () {
	ClearPendingTargetCommand();
	if(battleDone)
		return;
	SyncActiveEnemyFromGroup();
	if(LivingEnemyCount() <= 0) {
		FinishVictory();
		return;
	}
	skillMenuActive = false;
	itemMenuActive = false;
	if(strictLegacyBattle) {
		ResolvePlayerTechLegacy();
		return;
	}
	if(CanUseSkill() == false) {
		actionText = EString().Format("Tech charging... %d%%", std::max(0, std::min(kBattleSkillChargeMax, skillCharge)));
		actionTimestamp = GetMilliseconds();
		PlaySoundIfLoaded(soundMiss);
		return;
	}
	ResolvePlayerAttack(true);
}

void Battle::ResolvePlayerTechLegacy () {
	if(battleDone)
		return;
	SyncActiveEnemyFromGroup();
	if(LivingEnemyCount() <= 0) {
		FinishVictory();
		return;
	}

	lastPlayerDamage = 0;
	lastEnemyDamage = 0;
	const int avatarType = std::max((int)DRAGON_AVATAR_WARRIOR, std::min((int)gSave.avatarType, (int)DRAGON_AVATAR_COUNT - 1));
	switch(avatarType) {
		case DRAGON_AVATAR_WARRIOR: {
			const int maxHealth = DragonGetMaxHealth();
			const int before = gSave.health;
			const int heal = std::max(8, maxHealth / 6);
			gSave.health = (int16_t)std::min(maxHealth, (int)gSave.health + heal);
			const int gained = std::max(0, (int)gSave.health - before);
			actionText = gained > 0 ? EString().Format("Rally restores %d HP.", gained) : "Rally strengthens your guard.";
			actionTimestamp = GetMilliseconds();
			TriggerPlayerEffect(BATTLE_EFFECT_HEAL_HIT, 520);
			PlaySoundIfLoaded(soundCast);
			DragonSaveCurrentSlot();
			BATTLE_TRACE(
				"[Battle] PlayerTechLegacy: avatar=%d rally gained=%d hp=%d/%d",
				avatarType,
				gained,
				(int)gSave.health,
				DragonGetMaxHealth()
			);
			EString playerAction = actionText;
			ResolveEnemyTurn(std::max(8, DragonGetDefense() / 2), playerAction);
			return;
		}
			case DRAGON_AVATAR_SORCERER: {
				CombatStats targetStats = EnemyCombatStats(enemy);
				bool healed = false;
				int damage = RollLegacySpellDamage(
				std::max(1, 56 + DragonGetMagic() * 7 + DragonGetSpeed()),
				kLegacyElementLight,
				true,
					targetStats,
					0,
					healed
				);
				if(healed) {
					enemy.health = std::min(enemy.healthMax, enemy.health + damage);
					if(activeEnemyIndex >= 0 and activeEnemyIndex < (int)enemyGroup.size())
						enemyGroup[(size_t)activeEnemyIndex].data.health = enemy.health;
				actionText = EString().Format("%s absorbed Nova and recovered %d HP.", (const char*)enemy.name, damage);
				TriggerEnemyEffect(BATTLE_EFFECT_HEAL_HIT, 420);
				PlaySoundIfLoaded(soundMiss);
			}
			else if(damage <= 0) {
				actionText = "Nova dealt no damage.";
				TriggerEnemyEffect(BATTLE_EFFECT_LIGHT_CAST, 420);
					PlaySoundIfLoaded(soundMiss);
				}
				else {
					enemy.health -= damage;
					if(activeEnemyIndex >= 0 and activeEnemyIndex < (int)enemyGroup.size())
						enemyGroup[(size_t)activeEnemyIndex].data.health = enemy.health;
					lastPlayerDamage = damage;
					actionText = EString().Format("Nova deals %d.", damage);
					TriggerEnemyEffect(BATTLE_EFFECT_LIGHT_HIT, 520);
					PlaySoundIfLoaded(soundCast);
				}
			actionTimestamp = GetMilliseconds();
			BATTLE_TRACE(
				"[Battle] PlayerTechLegacy: avatar=%d nova damage=%d healed=%d enemyHP=%d/%d",
				avatarType,
				lastPlayerDamage,
				(int)healed,
				enemy.health,
				enemy.healthMax
			);
			if(enemy.health <= 0) {
				enemy.health = 0;
				if(activeEnemyIndex >= 0 and activeEnemyIndex < (int)enemyGroup.size())
					enemyGroup[(size_t)activeEnemyIndex].data.health = 0;
				if(HandleEnemyDefeat())
					return;
				PlaySoundIfLoaded(soundHit);
				return;
			}
			EString playerAction = actionText;
			ResolveEnemyTurn(0, playerAction);
			return;
		}
		default: {
			CombatStats targetStats = EnemyCombatStats(enemy);
			bool missed = false;
			bool healed = false;
				int damage = RollLegacyAttackDamage(
					std::max(1, 44 + DragonGetSpeed() * 4 + DragonGetAttack() * 3),
				std::max(62, std::min(100, 84 + DragonGetSpeed() / 3)),
				kLegacyElementAir,
				targetStats,
				0,
					missed,
					healed
				);
				if(missed) {
					actionText = "Pin Shot missed.";
				TriggerEnemyEffect(BATTLE_EFFECT_AIR_CAST, 420);
				PlaySoundIfLoaded(soundMiss);
				actionTimestamp = GetMilliseconds();
				EString playerAction = actionText;
				ResolveEnemyTurn(0, playerAction);
				return;
			}
			if(healed) {
				enemy.health = std::min(enemy.healthMax, enemy.health + damage);
				if(activeEnemyIndex >= 0 and activeEnemyIndex < (int)enemyGroup.size())
					enemyGroup[(size_t)activeEnemyIndex].data.health = enemy.health;
				actionText = EString().Format("%s absorbed Pin Shot.", (const char*)enemy.name);
				actionTimestamp = GetMilliseconds();
				TriggerEnemyEffect(BATTLE_EFFECT_HEAL_HIT, 420);
				PlaySoundIfLoaded(soundMiss);
				EString playerAction = actionText;
				ResolveEnemyTurn(0, playerAction);
				return;
			}
			if(damage <= 0) {
				actionText = "Pin Shot dealt no damage.";
				actionTimestamp = GetMilliseconds();
				TriggerEnemyEffect(BATTLE_EFFECT_AIR_CAST, 420);
				PlaySoundIfLoaded(soundMiss);
				EString playerAction = actionText;
				ResolveEnemyTurn(0, playerAction);
				return;
			}

				enemy.health -= damage;
				if(activeEnemyIndex >= 0 and activeEnemyIndex < (int)enemyGroup.size())
					enemyGroup[(size_t)activeEnemyIndex].data.health = enemy.health;
				lastPlayerDamage = damage;
				actionText = EString().Format("Pin Shot deals %d.", damage);
				actionTimestamp = GetMilliseconds();
				TriggerEnemyEffect(BATTLE_EFFECT_AIR_HIT, 520);
				PlaySoundIfLoaded(soundCast);
				BATTLE_TRACE(
					"[Battle] PlayerTechLegacy: avatar=%d pinshot damage=%d enemyHP=%d/%d",
					avatarType,
					lastPlayerDamage,
					enemy.health,
					enemy.healthMax
				);
			if(enemy.health <= 0) {
				enemy.health = 0;
				if(activeEnemyIndex >= 0 and activeEnemyIndex < (int)enemyGroup.size())
					enemyGroup[(size_t)activeEnemyIndex].data.health = 0;
				if(HandleEnemyDefeat())
					return;
				PlaySoundIfLoaded(soundHit);
				return;
			}
			EString playerAction = actionText;
			ResolveEnemyTurn(0, playerAction);
			return;
		}
	}
}

void Battle::ResolveAlternateSkill () {
	if(strictLegacyBattle) {
		ResolvePlayerTechLegacy();
		return;
	}
	if(battleDone)
		return;
	SyncActiveEnemyFromGroup();
	if(LivingEnemyCount() <= 0) {
		FinishVictory();
		return;
	}
	skillMenuActive = false;
	itemMenuActive = false;
	if(CanUseSkill() == false) {
		actionText = EString().Format("Skill charging... %d%%", skillCharge);
		actionTimestamp = GetMilliseconds();
		PlaySoundIfLoaded(soundMiss);
		return;
	}

	skillCharge = 0;
	lastPlayerDamage = 0;
	lastEnemyDamage = 0;
	int avatarType = std::max((int)DRAGON_AVATAR_WARRIOR, std::min((int)gSave.avatarType, (int)DRAGON_AVATAR_COUNT - 1));
	switch(avatarType) {
		case DRAGON_AVATAR_WARRIOR: {
			int maxHealth = DragonGetMaxHealth();
			int before = gSave.health;
			int heal = std::max(8, maxHealth / 6);
			gSave.health = (int16_t)std::min(maxHealth, (int)gSave.health + heal);
			int gained = std::max(0, (int)gSave.health - before);
			actionText = gained > 0 ? EString().Format("Rally restores %d HP.", gained) : "Rally strengthens your guard.";
			actionTimestamp = GetMilliseconds();
			TriggerPlayerEffect(BATTLE_EFFECT_HEAL_HIT, 520);
			PlaySoundIfLoaded(soundCast);
			DragonSaveCurrentSlot();
			EString playerAction = actionText;
			ResolveEnemyTurn(std::max(8, DragonGetDefense() / 2), playerAction);
			return;
		}
		case DRAGON_AVATAR_SORCERER: {
			CombatStats targetStats = EnemyCombatStats(enemy);
			bool healed = false;
			int damage = RollLegacySpellDamage(
				std::max(1, 56 + DragonGetMagic() * 7 + DragonGetSpeed()),
				kLegacyElementLight,
				true,
				targetStats,
				0,
				healed
			);
			bool critical = false;
			if(healed) {
				enemy.health = std::min(enemy.healthMax, enemy.health + damage);
				if(activeEnemyIndex >= 0 and activeEnemyIndex < (int)enemyGroup.size())
					enemyGroup[(size_t)activeEnemyIndex].data.health = enemy.health;
				actionText = EString().Format("%s absorbed Nova and recovered %d HP.", (const char*)enemy.name, damage);
				TriggerEnemyEffect(BATTLE_EFFECT_HEAL_HIT, 420);
				PlaySoundIfLoaded(soundMiss);
			}
			else if(damage <= 0) {
				actionText = "Nova dealt no damage.";
				TriggerEnemyEffect(BATTLE_EFFECT_LIGHT_CAST, 420);
				PlaySoundIfLoaded(soundMiss);
			}
			else {
				int critChance = std::max(8, std::min(35, 16 + DragonGetSpeed() / 18));
				if((int)ENode::GetRandom(100) < critChance) {
					critical = true;
					damage = damage * 3 / 2 + 2;
				}
				enemy.health -= damage;
				if(activeEnemyIndex >= 0 and activeEnemyIndex < (int)enemyGroup.size())
					enemyGroup[(size_t)activeEnemyIndex].data.health = enemy.health;
				lastPlayerDamage = damage;
				actionText = critical ? EString().Format("Critical Nova! %d damage.", damage) : EString().Format("Nova deals %d.", damage);
				TriggerEnemyEffect(BATTLE_EFFECT_LIGHT_HIT, critical ? 620 : 520);
				PlaySoundIfLoaded(soundCast);
			}
			actionTimestamp = GetMilliseconds();
			if(enemy.health <= 0) {
				enemy.health = 0;
				if(activeEnemyIndex >= 0 and activeEnemyIndex < (int)enemyGroup.size())
					enemyGroup[(size_t)activeEnemyIndex].data.health = 0;
				if(HandleEnemyDefeat())
					return;
				PlaySoundIfLoaded(soundHit);
				return;
			}
			EString playerAction = actionText;
			ResolveEnemyTurn(0, playerAction);
			return;
		}
		default: {
			CombatStats targetStats = EnemyCombatStats(enemy);
			bool missed = false;
			bool healed = false;
			int damage = RollLegacyAttackDamage(
				std::max(1, 44 + DragonGetSpeed() * 4 + DragonGetAttack() * 3),
				std::max(62, std::min(100, 84 + DragonGetSpeed() / 3)),
				kLegacyElementAir,
				targetStats,
				0,
				missed,
				healed
			);
			bool critical = false;
			if(missed == false and healed == false and damage > 0 and (int)ENode::GetRandom(100) < std::max(8, std::min(35, 16 + DragonGetSpeed() / 20))) {
				critical = true;
				damage = damage * 3 / 2 + 2;
			}
			if(missed) {
				actionText = "Pin Shot missed.";
				TriggerEnemyEffect(BATTLE_EFFECT_AIR_CAST, 420);
				PlaySoundIfLoaded(soundMiss);
				actionTimestamp = GetMilliseconds();
				EString playerAction = actionText;
				ResolveEnemyTurn(0, playerAction);
				return;
			}
			if(healed) {
				enemy.health = std::min(enemy.healthMax, enemy.health + damage);
				if(activeEnemyIndex >= 0 and activeEnemyIndex < (int)enemyGroup.size())
					enemyGroup[(size_t)activeEnemyIndex].data.health = enemy.health;
				actionText = EString().Format("%s absorbed Pin Shot.", (const char*)enemy.name);
				actionTimestamp = GetMilliseconds();
				TriggerEnemyEffect(BATTLE_EFFECT_HEAL_HIT, 420);
				PlaySoundIfLoaded(soundMiss);
				EString playerAction = actionText;
				ResolveEnemyTurn(0, playerAction);
				return;
			}
			if(damage <= 0) {
				actionText = "Pin Shot dealt no damage.";
				actionTimestamp = GetMilliseconds();
				TriggerEnemyEffect(BATTLE_EFFECT_AIR_CAST, 420);
				PlaySoundIfLoaded(soundMiss);
				EString playerAction = actionText;
				ResolveEnemyTurn(0, playerAction);
				return;
			}

			enemy.health -= damage;
			if(activeEnemyIndex >= 0 and activeEnemyIndex < (int)enemyGroup.size())
				enemyGroup[(size_t)activeEnemyIndex].data.health = enemy.health;
			lastPlayerDamage = damage;
			bool pinned = (int)ENode::GetRandom(100) < 45;
			actionText = pinned ? EString().Format("Pin Shot %d. Enemy pinned.", damage) : EString().Format("Pin Shot %d.", damage);
			actionTimestamp = GetMilliseconds();
			TriggerEnemyEffect(BATTLE_EFFECT_AIR_HIT, critical ? 620 : 520);
			PlaySoundIfLoaded(soundCast);
			if(enemy.health <= 0) {
				enemy.health = 0;
				if(activeEnemyIndex >= 0 and activeEnemyIndex < (int)enemyGroup.size())
					enemyGroup[(size_t)activeEnemyIndex].data.health = 0;
				if(HandleEnemyDefeat())
					return;
				PlaySoundIfLoaded(soundHit);
				return;
			}
			if(pinned) {
				roundNumber++;
				DragonSaveCurrentSlot();
				return;
			}
			EString playerAction = actionText;
			ResolveEnemyTurn(0, playerAction);
			return;
		}
	}
}

bool Battle::HasUsableConsumables () const {
	int potionCount = CountInventoryType(DRAGON_ITEM_HEALTH_POTION);
	int scrollCount = CountInventoryType(DRAGON_ITEM_FIRE_SCROLL);
	if(potionCount > 0 and gSave.health < DragonGetMaxHealth())
		return true;
	return scrollCount > 0;
}

int Battle::CountInventoryType (int itemType) const {
	if(itemType <= DRAGON_ITEM_NONE or itemType >= DRAGON_ITEM_COUNT)
		return 0;
	if(gSave.inventoryCount <= 0)
		return 0;
	int total = 0;
	for(int i = 0; i < gSave.inventoryCount; i++) {
		if(gSave.inventory[i].count <= 0)
			continue;
		if(gSave.inventory[i].type == itemType)
			total += gSave.inventory[i].count;
	}
	return total;
}

bool Battle::ResolvePlayerItem (int itemType) {
	if(battleDone)
		return false;
	SyncActiveEnemyFromGroup();
	if(LivingEnemyCount() <= 0) {
		FinishVictory();
		return false;
	}
	itemMenuActive = false;

	const DragonItemInfo* potion = DragonItemByType(DRAGON_ITEM_HEALTH_POTION);
	int maxHealth = DragonGetMaxHealth();
	lastPlayerDamage = 0;
	lastEnemyDamage = 0;

	if(itemType == DRAGON_ITEM_HEALTH_POTION) {
		if(gSave.health >= maxHealth) {
			actionText = "Health is already full.";
			actionTimestamp = GetMilliseconds();
			PlaySoundIfLoaded(soundMiss);
			return false;
		}
		if(DragonInventoryConsumeType(DRAGON_ITEM_HEALTH_POTION, 1) == false) {
			actionText = "No Health Potion available.";
			actionTimestamp = GetMilliseconds();
			PlaySoundIfLoaded(soundMiss);
			return false;
		}

		int heal = potion ? std::max(1, (int)potion->heal) : 24;
		int before = gSave.health;
		gSave.health = (int16_t)std::min(maxHealth, (int)gSave.health + heal);
		actionText = EString().Format("Used Health Potion. +%d HP.", (int)gSave.health - before);
		actionTimestamp = GetMilliseconds();
		TriggerPlayerEffect(BATTLE_EFFECT_HEAL_HIT, 440);
		DragonSaveCurrentSlot();
		PlaySoundIfLoaded(soundCast);
		GainSkillCharge(18);
		EString playerAction = actionText;
		ResolveEnemyTurn(0, playerAction);
		return true;
	}

	if(itemType == DRAGON_ITEM_FIRE_SCROLL) {
		if(DragonInventoryConsumeType(DRAGON_ITEM_FIRE_SCROLL, 1) == false) {
			actionText = "No Fire Scroll available.";
			actionTimestamp = GetMilliseconds();
			PlaySoundIfLoaded(soundMiss);
			return false;
		}

		CombatStats targetStats = EnemyCombatStats(enemy);
		bool healed = false;
		int damage = RollLegacySpellDamage(
			std::max(1, 36 + DragonGetMagic() * 5 + DragonGetSpeed()),
			kLegacyElementFire,
			true,
			targetStats,
			0,
			healed
		);
		bool critical = false;
		if(healed) {
			enemy.health = std::min(enemy.healthMax, enemy.health + damage);
			if(activeEnemyIndex >= 0 and activeEnemyIndex < (int)enemyGroup.size())
				enemyGroup[(size_t)activeEnemyIndex].data.health = enemy.health;
			actionText = EString().Format("%s absorbed the scroll flame.", (const char*)enemy.name);
			TriggerEnemyEffect(BATTLE_EFFECT_HEAL_HIT, 360);
			PlaySoundIfLoaded(soundMiss);
		}
		else if(damage <= 0) {
			actionText = "Scroll blast dealt no damage.";
			TriggerEnemyEffect(BATTLE_EFFECT_FIRE_CAST, 360);
			PlaySoundIfLoaded(soundMiss);
		}
		else {
			int critChance = std::max(8, std::min(35, 14 + DragonGetSpeed() / 20));
			if((int)ENode::GetRandom(100) < critChance) {
				critical = true;
				damage = damage * 3 / 2 + 2;
			}
			enemy.health -= damage;
			if(activeEnemyIndex >= 0 and activeEnemyIndex < (int)enemyGroup.size())
				enemyGroup[(size_t)activeEnemyIndex].data.health = enemy.health;
			lastPlayerDamage = damage;
			actionText = critical ? EString().Format("Critical scroll blast! %d damage.", damage) : EString().Format("Scroll blast deals %d.", damage);
			TriggerEnemyEffect(BATTLE_EFFECT_FIRE_HIT, critical ? 560 : 460);
			PlaySoundIfLoaded(soundCast);
		}
		actionTimestamp = GetMilliseconds();
		DragonSaveCurrentSlot();
		GainSkillCharge(12);
		if(enemy.health <= 0) {
			enemy.health = 0;
			if(activeEnemyIndex >= 0 and activeEnemyIndex < (int)enemyGroup.size())
				enemyGroup[(size_t)activeEnemyIndex].data.health = 0;
			if(HandleEnemyDefeat())
				return true;
			PlaySoundIfLoaded(soundHit);
			return true;
		}
		EString playerAction = actionText;
		ResolveEnemyTurn(0, playerAction);
		return true;
	}

	actionText = "Cannot use that item.";
	actionTimestamp = GetMilliseconds();
	PlaySoundIfLoaded(soundMiss);
	return false;
}

void Battle::ResolveDefend () {
	ClearPendingTargetCommand();
	if(battleDone)
		return;
	skillMenuActive = false;
	itemMenuActive = false;

	int maxHealth = DragonGetMaxHealth();
	int guardHeal = std::max(1, maxHealth / 30);
	int before = gSave.health;
	gSave.health = (int16_t)std::min(maxHealth, (int)gSave.health + guardHeal);
	int gained = std::max(0, (int)gSave.health - before);
	lastPlayerDamage = 0;
	lastEnemyDamage = 0;
	actionText = gained > 0 ? EString().Format("You defend and recover %d HP.", gained) : "You take a defensive stance.";
	actionTimestamp = GetMilliseconds();
	TriggerPlayerEffect(gained > 0 ? BATTLE_EFFECT_WATER_HIT : BATTLE_EFFECT_WATER_CAST, 420);
	PlaySoundIfLoaded(soundClick);
	GainSkillCharge(22);

	EString playerAction = actionText;
	ResolveEnemyTurn(std::max(4, DragonGetDefense() / 2), playerAction);
}

const DragonLegacySpecialData* Battle::ChooseEnemySpecial (const EnemyUnitRuntime& actingUnit, uint16_t& outSpecialId) const {
	outSpecialId = 0;
	if(actingUnit.pluginId == 0 or actingUnit.monsterId == 0)
		return NULL;

	const DragonLegacyMonsterData* legacyMonster = DragonFindLegacyMonsterData(actingUnit.pluginId, actingUnit.monsterId);
	if(legacyMonster == NULL)
		return NULL;

	std::vector<uint16_t> specials;
	specials.reserve(12);
	for(int i = 0; i < 12; i++) {
		if(legacyMonster->specials[i] != 0)
			specials.push_back(legacyMonster->specials[i]);
	}
	if(specials.empty())
		return NULL;

#if defined(DRAGON_TEST)
	if(DragonAutomationCaseIs("Validation_Battle_EnemyHealResponse")) {
		for(size_t i = 0; i < specials.size(); i++) {
			const uint16_t specialId = specials[i];
			const DragonLegacySpecialData* special = DragonFindLegacySpecialData(actingUnit.pluginId, specialId);
			if(special != NULL and special->isHeal != 0) {
				outSpecialId = specialId;
				return special;
			}
		}
	}
	if(DragonAutomationCaseIs("Validation_Battle_EnemySpecialResponse")
		|| DragonAutomationCaseIs("Validation_Battle_NoDamageResponse")) {
		for(size_t i = 0; i < specials.size(); i++) {
			const uint16_t specialId = specials[i];
			const DragonLegacySpecialData* special = DragonFindLegacySpecialData(actingUnit.pluginId, specialId);
			if(special != NULL and special->isHeal == 0 and std::strcmp(special->name, "Attack") != 0) {
				outSpecialId = specialId;
				return special;
			}
		}
	}
#endif

	for(size_t attempt = 0; attempt < specials.size(); attempt++) {
		const uint16_t specialId = specials[(size_t)ENode::GetRandom((uint32_t)specials.size())];
		const DragonLegacySpecialData* special = DragonFindLegacySpecialData(actingUnit.pluginId, specialId);
		if(special == NULL)
			continue;
		outSpecialId = specialId;
		return special;
	}
	return NULL;
}

void Battle::ResolveEnemyActionForIndex (int actingIndex, int defenseBoost, const char* actionPrefix) {
	if(actingIndex < 0 or actingIndex >= (int)enemyGroup.size())
		return;
	EnemyUnitRuntime& actingUnit = enemyGroup[(size_t)actingIndex];
	EnemyData& actingEnemy = actingUnit.data;
	if(actingEnemy.health <= 0)
		return;

	auto finalizeAction = [&] (const EString& enemyLine) {
		if(actionPrefix != NULL and actionPrefix[0] != '\0')
			actionText = EString().Format("%s\n%s", actionPrefix, (const char*)enemyLine);
		else
			actionText = enemyLine;
		validationEnemyActionSerial++;
		validationEnemyActionText = enemyLine;
		actionTimestamp = GetMilliseconds();
	};
	auto castEffectByElement = [&] (int element) {
		switch(element) {
			case 1: return BATTLE_EFFECT_LIGHT_CAST;
			case 2: return BATTLE_EFFECT_DARK_CAST;
			case 3: return BATTLE_EFFECT_FIRE_CAST;
			case 4: return BATTLE_EFFECT_WATER_CAST;
			case 5: return BATTLE_EFFECT_EARTH_CAST;
			case 6: return BATTLE_EFFECT_AIR_CAST;
			default: return BATTLE_EFFECT_NONE_CAST;
		}
	};
	auto hitEffectByElement = [&] (int element) {
		switch(element) {
			case 1: return BATTLE_EFFECT_LIGHT_HIT;
			case 2: return BATTLE_EFFECT_DARK_HIT;
			case 3: return BATTLE_EFFECT_FIRE_HIT;
			case 4: return BATTLE_EFFECT_WATER_HIT;
			case 5: return BATTLE_EFFECT_EARTH_HIT;
			case 6: return BATTLE_EFFECT_AIR_HIT;
			default: return BATTLE_EFFECT_ATTACK_HIT;
		}
	};

	uint16_t selectedSpecialId = 0;
	const DragonLegacySpecialData* selectedSpecial = ChooseEnemySpecial(actingUnit, selectedSpecialId);
	BATTLE_TRACE(
		"[Battle] Enemy turn: actor=%s plugin=%u monster=%u special=%u hp=%d/%d",
		(const char*)actingEnemy.name,
		(unsigned)actingUnit.pluginId,
		(unsigned)actingUnit.monsterId,
		(unsigned)selectedSpecialId,
		actingEnemy.health,
		actingEnemy.healthMax
	);

	int enemyHitEffect = BATTLE_EFFECT_ATTACK_HIT;
	int enemyCastEffect = BATTLE_EFFECT_NONE_CAST;
	bool usedSpecialAttack = false;
	const char* usedSpecialName = NULL;
	bool missed = false;
	bool healed = false;
	int damage = 0;
	const CombatStats playerStats = PlayerCombatStats();
	const int playerLevel = std::max(1, (int)gSave.level);

	if(selectedSpecial != NULL and selectedSpecial->isHeal != 0) {
		int totalRestored = 0;
		auto applyHeal = [&] (int targetIndex) {
			if(targetIndex < 0 or targetIndex >= (int)enemyGroup.size())
				return;
			EnemyData& healTarget = enemyGroup[(size_t)targetIndex].data;
			if(healTarget.health <= 0)
				return;
			int healAmount = std::max(4, (int)selectedSpecial->power * 4 + actingEnemy.level + (int)ENode::GetRandom(10));
			if(selectedSpecial->type == 1)
				healAmount += std::max(0, actingEnemy.statMagic * 2);
			else
				healAmount += std::max(0, actingEnemy.statStrength + actingEnemy.statSpeed);
			const int before = healTarget.health;
			healTarget.health = std::min(healTarget.healthMax, healTarget.health + healAmount);
			const int restored = std::max(0, healTarget.health - before);
			if(restored > 0)
				totalRestored += restored;
			if(targetIndex == activeEnemyIndex)
				enemy.health = healTarget.health;
		};

		int bossIndex = -1;
		for(size_t i = 0; i < enemyGroup.size(); i++) {
			if(enemyGroup[i].data.health > 0 and enemyGroup[i].boss) {
				bossIndex = (int)i;
				break;
			}
		}
		int targetIndex = actingIndex;
		if(bossIndex >= 0 and (int)ENode::GetRandom(100) <= 80)
			targetIndex = bossIndex;
		else {
			int lowestPercent = 101;
			for(size_t i = 0; i < enemyGroup.size(); i++) {
				if(enemyGroup[i].data.health <= 0)
					continue;
				int percent = enemyGroup[i].data.health * 100 / std::max(1, enemyGroup[i].data.healthMax);
				if(percent <= lowestPercent) {
					lowestPercent = percent;
					targetIndex = (int)i;
				}
			}
		}
		applyHeal(targetIndex);

		lastPlayerDamage = 0;
		lastEnemyDamage = 0;
		if(selectedSpecial->name != NULL and selectedSpecial->name[0] != '\0') {
			if(totalRestored > 0)
				finalizeAction(EString().Format("%s used %s and recovered %d HP.", (const char*)actingEnemy.name, selectedSpecial->name, totalRestored));
			else
				finalizeAction(EString().Format("%s used %s, but no wounds remained.", (const char*)actingEnemy.name, selectedSpecial->name));
		}
		else {
			if(totalRestored > 0)
				finalizeAction(EString().Format("%s recovered %d HP.", (const char*)actingEnemy.name, totalRestored));
			else
				finalizeAction(EString().Format("%s attempted to recover.", (const char*)actingEnemy.name));
		}
		TriggerEnemyEffect(castEffectByElement((int)selectedSpecial->element), 440);
		PlaySoundIfLoaded(totalRestored > 0 ? soundCast : soundMiss);
		GainSkillCharge(8);
		roundNumber++;
		return;
	}

	if(selectedSpecial != NULL) {
		usedSpecialAttack = true;
		usedSpecialName = selectedSpecial->name;
		enemyCastEffect = castEffectByElement((int)selectedSpecial->element);
		enemyHitEffect = hitEffectByElement((int)selectedSpecial->element);
		const int speedBonus = std::max(0, std::min(40, std::abs((int)selectedSpecial->speed)));
		if(selectedSpecial->type == 0) {
			const int hitPercent = std::max(42, std::min(100, 50 + (actingEnemy.statStrength + 8 * actingEnemy.statSpeed) / 200 + speedBonus / 2));
			int baseDamage = std::max(1, 40 * (int)selectedSpecial->power + 5 * actingEnemy.statStrength + speedBonus);
			if(selectedSpecial->isAll != 0)
				baseDamage = baseDamage * 4 / 5;
			damage = RollLegacyAttackDamage(baseDamage, hitPercent, (int)selectedSpecial->element, playerStats, playerLevel, missed, healed);
		}
		else if(selectedSpecial->type == 1) {
			int baseDamage = std::max(1, 40 * (int)selectedSpecial->power + 5 * actingEnemy.statMagic + speedBonus);
			if(selectedSpecial->isAll != 0)
				baseDamage = baseDamage * 4 / 5;
			damage = RollLegacySpellDamage(baseDamage, (int)selectedSpecial->element, true, playerStats, playerLevel, healed);
		}
		else {
			int baseDamage = std::max(1, 40 * (int)selectedSpecial->power + actingEnemy.statStrength + 3 * actingEnemy.statSpeed + actingEnemy.statMagic + speedBonus);
			if(selectedSpecial->isAll != 0)
				baseDamage = baseDamage * 4 / 5;
			damage = RollLegacySpellDamage(baseDamage, (int)selectedSpecial->element, false, playerStats, playerLevel, healed);
		}
	}
	else {
		const int hitPercent = std::max(42, std::min(100, 50 + (actingEnemy.statStrength + 8 * actingEnemy.statSpeed) / 200));
		int baseDamage = std::max(1, 5 * actingEnemy.statStrength);
		damage = RollLegacyAttackDamage(baseDamage, hitPercent, 0, playerStats, playerLevel, missed, healed);
	}

	if(defenseBoost > 0 and missed == false and healed == false and damage > 0) {
		damage -= std::max(0, defenseBoost / 3);
		if(damage < 0)
			damage = 0;
	}

#if defined(DRAGON_TEST)
	if(DragonAutomationCaseIs("Validation_WorldMap_ChallengeDefeatPanel")) {
		// Keep this world-map challenge case deterministic: one scripted player action
		// should always advance to the defeat panel in the same round.
		missed = false;
		healed = false;
		damage = std::max(damage, std::max(1, (int)gSave.health));
	}
	if(DragonAutomationCaseIs("Validation_Battle_NoDamageResponse")) {
		missed = false;
		healed = false;
		damage = 0;
	}
#endif

	if(missed) {
		lastEnemyDamage = 0;
		if(usedSpecialAttack and usedSpecialName != NULL and usedSpecialName[0] != '\0')
			finalizeAction(EString().Format("%s used %s, but it missed.", (const char*)actingEnemy.name, usedSpecialName));
		else
			finalizeAction(EString().Format("%s missed.", (const char*)actingEnemy.name));
		TriggerPlayerEffect(usedSpecialAttack ? enemyCastEffect : BATTLE_EFFECT_NONE_CAST, 320);
		PlaySoundIfLoaded(soundMiss);
	}
	else if(healed) {
		int maxHealth = DragonGetMaxHealth();
		int before = gSave.health;
		gSave.health = (int16_t)std::min(maxHealth, (int)gSave.health + std::max(1, damage));
		int restored = std::max(0, (int)gSave.health - before);
		lastEnemyDamage = 0;
		if(usedSpecialAttack and usedSpecialName != NULL and usedSpecialName[0] != '\0')
			finalizeAction(EString().Format("%s used %s and restored %d HP.", (const char*)actingEnemy.name, usedSpecialName, restored));
		else
			finalizeAction(EString().Format("%s restored %d HP.", (const char*)actingEnemy.name, restored));
		TriggerPlayerEffect(BATTLE_EFFECT_HEAL_HIT, 420);
		PlaySoundIfLoaded(soundCast);
	}
	else if(damage <= 0) {
		lastEnemyDamage = 0;
		if(usedSpecialAttack and usedSpecialName != NULL and usedSpecialName[0] != '\0')
			finalizeAction(EString().Format("%s used %s, but it dealt no damage.", (const char*)actingEnemy.name, usedSpecialName));
		else
			finalizeAction(EString().Format("%s's attack was blocked.", (const char*)actingEnemy.name));
		TriggerPlayerEffect(enemyHitEffect, 360);
		PlaySoundIfLoaded(soundMiss);
	}
	else {
		gSave.health = (int16_t)std::max(0, (int)gSave.health - damage);
		lastEnemyDamage = damage;
		if(usedSpecialAttack and usedSpecialName != NULL and usedSpecialName[0] != '\0')
			finalizeAction(EString().Format("%s used %s for %d.", (const char*)actingEnemy.name, usedSpecialName, damage));
		else
			finalizeAction(EString().Format("%s dealt %d.", (const char*)actingEnemy.name, damage));
		TriggerPlayerEffect(enemyHitEffect, 420);
		PlaySoundIfLoaded(soundHit);
	}

	if(missed or damage <= 0)
		GainSkillCharge(10);
	else
		GainSkillCharge(std::max(12, std::min(26, 10 + damage / 3)));

	if(gSave.health <= 0) {
		gSave.health = 0;
		FinishDefeat();
		return;
	}

	roundNumber++;
}

void Battle::ResolveEnemyTurn (int defenseBoost, const char* actionPrefix) {
	if(battleDone)
		return;
	if(LivingEnemyCount() <= 0) {
		FinishVictory();
		return;
	}

#if defined(DRAGON_TEST)
	if((DragonAutomationCaseIs("Validation_Battle_EnemySpecialResponse")
			|| DragonAutomationCaseIs("Validation_Battle_EnemyHealResponse")
			|| DragonAutomationCaseIs("Validation_Battle_NoDamageResponse"))
		&& actionPrefix != NULL
		&& actionPrefix[0] != '\0') {
		const int actingIndex = FirstLivingEnemyIndex();
		if(actingIndex >= 0) {
			ResolveEnemyActionForIndex(actingIndex, defenseBoost, actionPrefix);
			if(battleDone)
				return;
			if(actingIndex < (int)enemyGroup.size())
				enemyGroup[(size_t)actingIndex].turnMeter = 0;
			playerTurnMeter = std::max(1, DragonGetSpeed());
			DragonSaveCurrentSlot();
			return;
		}
	}
#endif

	if(playerTurnMeter <= 0)
		playerTurnMeter = std::max(1, DragonGetSpeed());

	int playerModifier = playerTurnMeter + (int)ENode::GetRandom(25) - 12;
	if(playerModifier < 1)
		playerModifier = 1;
	playerTurnMeter -= playerModifier;

	bool prefixPending = actionPrefix != NULL and actionPrefix[0] != '\0';
	int guard = 0;
	while(battleDone == false and guard < 32) {
		if(LivingEnemyCount() <= 0) {
			FinishVictory();
			return;
		}

		int who = -1; // -1 means player turn is ready
		int turnSpd = (playerTurnMeter > 0) ? playerTurnMeter : -1;
		for(size_t i = 0; i < enemyGroup.size(); i++) {
			if(enemyGroup[i].data.health <= 0)
				continue;
			if(enemyGroup[i].turnMeter > 0 and enemyGroup[i].turnMeter > turnSpd) {
				who = (int)i;
				turnSpd = enemyGroup[i].turnMeter;
			}
		}

		if(turnSpd == -1) {
			playerTurnMeter += std::max(1, DragonGetSpeed());
			for(size_t i = 0; i < enemyGroup.size(); i++) {
				if(enemyGroup[i].data.health <= 0)
					continue;
				enemyGroup[i].turnMeter += std::max(1, enemyGroup[i].data.statSpeed);
			}
			guard++;
			continue;
		}

		if(who == -1)
			break;

		const char* prefix = prefixPending ? actionPrefix : NULL;
		prefixPending = false;
		ResolveEnemyActionForIndex(who, defenseBoost, prefix);
		if(battleDone)
			return;

		int enemyModifier = turnSpd + (int)ENode::GetRandom(25) - 12;
		if(enemyModifier < 1)
			enemyModifier = 1;
		if(who >= 0 and who < (int)enemyGroup.size()) {
			enemyGroup[(size_t)who].turnMeter -= enemyModifier;
			if(enemyGroup[(size_t)who].data.health <= 0)
				enemyGroup[(size_t)who].turnMeter = 0;
		}
		guard++;
	}

	DragonSaveCurrentSlot();
}

bool Battle::AwardLegacyBattleLoot (int& outItemCount, int& outGoldGain, EString& outFirstItemName) {
	outItemCount = 0;
	outGoldGain = 0;
	outFirstItemName = "";
	if(strictLegacyBattle == false or hasBattleRequest == false)
		return false;
	if(battleRequest.legacyPluginId == 0 or battleRequest.legacyGroupId == 0)
		return false;

	const DragonLegacyGroupData* group = DragonFindLegacyGroupData(battleRequest.legacyPluginId, battleRequest.legacyGroupId);
	if(group == NULL)
		return false;

	auto awardWinItem = [&] (const DragonLegacyWinItem& winItem) {
		if(winItem.item.itemType == 0)
			return;
		if(winItem.winPercent <= 0)
			return;
		const int roll = (int)ENode::GetRandom(100) + 1;
		if(roll > winItem.winPercent)
			return;

		int mappedItemType = DRAGON_ITEM_NONE;
		int mappedGold = 0;
		if(MapLegacyBattleLootItemToModern(winItem.item, mappedItemType, mappedGold) == false)
			return;

		outGoldGain += std::max(0, mappedGold);
		if(mappedItemType <= DRAGON_ITEM_NONE or mappedItemType >= DRAGON_ITEM_COUNT)
			return;

		if(DragonInventoryAdd(mappedItemType, 1)) {
			outItemCount++;
			if(outFirstItemName.IsEmpty()) {
				const DragonItemInfo* itemInfo = DragonItemByType(mappedItemType);
				if(itemInfo != NULL and itemInfo->name != NULL and itemInfo->name[0] != '\0')
					outFirstItemName = itemInfo->name;
			}
			return;
		}

		const DragonItemInfo* itemInfo = DragonItemByType(mappedItemType);
		if(itemInfo != NULL)
			outGoldGain += std::max(4, (int)itemInfo->value / 2);
	};

	for(size_t i = 0; i < enemyGroup.size(); i++) {
		const EnemyUnitRuntime& unit = enemyGroup[i];
		if(unit.pluginId == 0 or unit.monsterId == 0)
			continue;
		const DragonLegacyMonsterData* monster = DragonFindLegacyMonsterData(unit.pluginId, unit.monsterId);
		if(monster == NULL)
			continue;
		const int treasureCount = std::max(0, std::min(monster->treasureCount, 2));
		for(int t = 0; t < treasureCount; t++)
			awardWinItem(monster->treasure[t]);
	}

	const int groupTreasureCount = std::max(0, std::min(group->treasureCount, 4));
	for(int t = 0; t < groupTreasureCount; t++)
		awardWinItem(group->treasure[t]);

	if(outGoldGain > 0)
		gSave.gold = std::min(DESIGN_MAX_GOLD, gSave.gold + outGoldGain);
	return outItemCount > 0 or outGoldGain > 0;
}

void Battle::TryRetreat () {
	ClearPendingTargetCommand();
	if(battleDone)
		return;
	SyncActiveEnemyFromGroup();
	if(LivingEnemyCount() <= 0) {
		FinishVictory();
		return;
	}
	if(retreatLocked) {
		actionText = "You can not run from a boss fight.";
		actionTimestamp = GetMilliseconds();
		PlaySoundIfLoaded(soundMiss);
		return;
	}

	// Legacy parity: run succeeds immediately for non-boss encounters.
	if(strictLegacyBattle) {
		FinishRetreat();
		return;
	}

	int chance = 45 + (DragonGetSpeed() - enemy.level * 2);
	const int battlesPlayed = (int)gSave.battlesWon + (int)gSave.battlesLost;
	int minimumChance = battlesPlayed < 4 ? 35 : 20;
	chance = std::max(minimumChance, std::min(90, chance));
	if((int)ENode::GetRandom(100) < chance) {
		FinishRetreat();
		return;
	}

	actionText = "Run failed.";
	actionTimestamp = GetMilliseconds();
	PlaySoundIfLoaded(soundMiss);
	EString playerAction = actionText;
	ResolveEnemyTurn(0, playerAction);
}

void Battle::FinishVictory () {
	battleDone = true;
	battleWon = true;
	battleRetreated = false;
	skillMenuActive = false;
	itemMenuActive = false;
	victoryXP = 0;
	victoryGold = 0;
	victoryPotionDrops = 0;
	victoryScrollDrops = 0;
	victoryHealthRecovered = 0;
	victoryMilestoneRewards = "";
	levelGain = 0;

	if(strictLegacyBattle) {
		if(gSelectedArea >= 0 and gSelectedArea < DRAGON_ALPHA_AREA_COUNT)
			gSave.areaIndex = (uint8_t)gSelectedArea;
		gSave.battlesWon++;
		DragonSaveCurrentSlot();
	}
	else {
		const int rewardXP = std::max(1, totalEnemyRewardXP > 0 ? totalEnemyRewardXP : enemy.rewardXP);
		const int rewardGold = std::max(1, totalEnemyRewardGold > 0 ? totalEnemyRewardGold : enemy.rewardGold);
		DragonBattleRewards rewards = DragonAwardBattleRewards(gSelectedArea, rewardXP, rewardGold, true);
		victoryXP = rewards.xpGranted;
		victoryGold = rewards.goldGranted;
		victoryPotionDrops = rewards.potionDrops;
		victoryScrollDrops = rewards.scrollDrops;
		victoryHealthRecovered = rewards.healthRecovered;
		victoryMilestoneRewards = rewards.milestoneRewards;
		levelGain = rewards.levelsGained;
	}

	victoryLegacyLootItems = 0;
	victoryLegacyLootGold = 0;
	victoryLegacyLootFirstItem = "";
	AwardLegacyBattleLoot(victoryLegacyLootItems, victoryLegacyLootGold, victoryLegacyLootFirstItem);
	if(victoryLegacyLootItems > 0 or victoryLegacyLootGold > 0)
		DragonSaveCurrentSlot();
	DragonPublishBattleResult(true, hasBattleRequest ? &battleRequest : NULL, false);
	if(strictLegacyBattle) {
		actionText = "Victory.";
		if(victoryLegacyLootItems > 0 or victoryLegacyLootGold > 0)
			actionText += " Loot recovered.";
	}
	else {
		if(levelGain > 0 and not victoryMilestoneRewards.IsEmpty())
			actionText = EString().Format("Victory! Level up +%d. New gear unlocked.", levelGain);
		else if(levelGain > 0)
			actionText = EString().Format("Victory! Level up +%d.", levelGain);
		else if(victoryPotionDrops > 0 or victoryScrollDrops > 0 or victoryLegacyLootItems > 0 or victoryLegacyLootGold > 0)
			actionText = "Victory! Supplies recovered.";
		else
			actionText = "Victory!";
	}
	actionTimestamp = GetMilliseconds();
	PlaySoundIfLoaded(soundLevelUp);
}

void Battle::FinishDefeat () {
	battleDone = true;
	battleWon = false;
	battleRetreated = false;
	skillMenuActive = false;
	itemMenuActive = false;
	int goldBefore = gSave.gold;
	DragonApplyBattleDefeat();
	defeatGoldLoss = std::max(0, goldBefore - (int)gSave.gold);
	DragonPublishBattleResult(false, hasBattleRequest ? &battleRequest : NULL, false);
	actionText = defeatGoldLoss > 0 ? EString().Format("Defeat. Lost %d gold.", defeatGoldLoss) : "Defeat.";
	actionTimestamp = GetMilliseconds();
	PlaySoundIfLoaded(soundDefeat);
}

void Battle::FinishRetreat () {
	battleDone = true;
	battleWon = false;
	battleRetreated = true;
	skillMenuActive = false;
	itemMenuActive = false;
	int penalty = 0;
	if((int)ENode::GetRandom(100) < 50) {
		int percent = 5 + (int)ENode::GetRandom(26);
		penalty = std::max(1, DragonGetMaxHealth() * percent / 100);
	}
	retreatHealthLoss = penalty;
	if(penalty > 0)
		gSave.health = (int16_t)std::max(1, (int)gSave.health - penalty);
	DragonSaveCurrentSlot();
	DragonPublishBattleResult(false, hasBattleRequest ? &battleRequest : NULL, true);
	actionText = (retreatHealthLoss > 0) ? EString().Format("You escaped the battle. -%d HP.", retreatHealthLoss) : "You escaped the battle.";
	actionTimestamp = GetMilliseconds();
	PlaySoundIfLoaded(soundClick);
}

void Battle::OnDraw () {
	SyncActiveEnemyFromGroup();
	const LegacyCanvas canvas = MakeLegacyCanvasInPreferredView(*this, 800, 600);
	const ERect safe = canvas.frame;
	DrawImageContain(imageBackground, safe);

	ERect arena = LegacyRect(canvas, 128, 88, 672, 512);

	const int playerSize = std::max(66, LegacyRect(canvas, 0, 0, 96, 96).width);
	const int enemySize = std::max(78, LegacyRect(canvas, 0, 0, 118, 118).width);
	ERect playerRect(LegacyRect(canvas, 182, 332, playerSize, playerSize));
	ERect enemyRect(LegacyRect(canvas, 554, 186, enemySize, enemySize));
	enemyTargetRect = enemyRect;
	enemyTouchTargets.clear();
	const bool showPlayerTurnSelect = battleDone == false
		and pendingTargetCommand == PENDING_TARGET_NONE
		and skillMenuActive == false
		and itemMenuActive == false;
	if(showPlayerTurnSelect and imageSelectGreen.IsEmpty() == false) {
		const ERect playerSelectRect = InsetRect(playerRect, -2);
		DrawImageContain(imageSelectGreen, playerSelectRect, EColor(0xffffffe8));
	}
	DrawImageContain(imagePlayer, playerRect);
	const bool selectingTarget = pendingTargetCommand != PENDING_TARGET_NONE;
	EImage* activeTargetSelectImage = selectingTarget ? &imageSelectRed : &imageSelectYellow;
	const EColor activeTargetSelectTint = selectingTarget ? EColor(0xffffffef) : EColor(0xffffffd8);
	auto drawTargetSelect = [&] (const ERect& targetRect) {
		const ERect highlightRect = InsetRect(targetRect, -2);
		if(activeTargetSelectImage != NULL and activeTargetSelectImage->IsEmpty() == false)
			DrawImageContain(*activeTargetSelectImage, highlightRect, activeTargetSelectTint);
		else
			imageAction.DrawRect(highlightRect, EColor(0xffffffe0));
	};
	std::vector<int> livingEnemyIndices;
	livingEnemyIndices.reserve(enemyGroup.size());
	for(size_t i = 0; i < enemyGroup.size(); i++) {
		if(enemyGroup[i].data.health > 0)
			livingEnemyIndices.push_back((int)i);
	}

	if(strictLegacyBattle and enemyGroup.empty() == false) {
		const int legacyBaseX = canvas.frame.x + (int)(224.0 * canvas.scale + 0.5);
		const int legacyBaseY = canvas.frame.y + (int)(128.0 * canvas.scale + 0.5);
		const int legacyStepX = std::max(1, (int)(128.0 * canvas.scale + 0.5));
		const int legacyStepY = std::max(1, (int)(64.0 * canvas.scale + 0.5));
		for(size_t i = 0; i < enemyGroup.size(); i++) {
			if(enemyGroup[i].data.health <= 0)
				continue;

			const int enemyIndex = (int)i;
			const int variantIndex = std::max(0, std::min(enemyGroup[i].artVariant, 3));
			EImage* legacySprite = ResolveLegacyBattleSpriteImage(enemyGroup[i].pluginId, enemyGroup[i].spriteId);
			EImage* drawImage = legacySprite != NULL ? legacySprite : &imageEnemyVariant[variantIndex];

			int spriteWidth = drawImage != NULL ? drawImage->GetWidth() : 0;
			int spriteHeight = drawImage != NULL ? drawImage->GetHeight() : 0;
			if(spriteWidth <= 0 or spriteHeight <= 0) {
				spriteWidth = 32;
				spriteHeight = 32;
			}

			const int drawW = std::max(16, std::min(arena.width, (int)(spriteWidth * canvas.scale + 0.5)));
			const int drawH = std::max(16, std::min(arena.height, (int)(spriteHeight * canvas.scale + 0.5)));
			const int formationX = std::max(0, std::min(enemyGroup[i].formationX, 7));
			const int formationY = std::max(0, std::min(enemyGroup[i].formationY, 7));
			const int drawX = legacyBaseX + formationX * legacyStepX;
			const int drawY = legacyBaseY + formationY * legacyStepY - drawH;
			ERect unitRect(drawX, drawY, drawW, drawH);

			if(unitRect.x + unitRect.width <= arena.x
				or unitRect.y + unitRect.height <= arena.y
				or unitRect.x >= arena.x + arena.width
				or unitRect.y >= arena.y + arena.height)
				continue;

			if(drawImage != NULL)
				drawImage->DrawRect(unitRect);

			EnemyTouchTarget target = {};
			target.enemyIndex = enemyIndex;
			target.rect = unitRect;
			enemyTouchTargets.push_back(target);

			if(enemyIndex == activeEnemyIndex) {
				enemyTargetRect = unitRect;
				drawTargetSelect(unitRect);
				if(drawImage != NULL)
					drawImage->DrawRect(unitRect);
			}
		}

		if(activeEnemyIndex < 0 or activeEnemyIndex >= (int)enemyGroup.size() or enemyGroup[(size_t)activeEnemyIndex].data.health <= 0) {
			if(enemyTouchTargets.empty() == false)
				enemyTargetRect = enemyTouchTargets[0].rect;
		}
	}
	else if(livingEnemyIndices.size() <= 1) {
		int targetIndex = activeEnemyIndex;
		if(targetIndex < 0 and livingEnemyIndices.empty() == false)
			targetIndex = livingEnemyIndices[0];
		const int variantIndex = targetIndex >= 0 and targetIndex < (int)enemyGroup.size()
			? std::max(0, std::min(enemyGroup[(size_t)targetIndex].artVariant, 3))
			: std::max(0, std::min(enemyArtVariant, 3));
		EImage* legacySprite = targetIndex >= 0 and targetIndex < (int)enemyGroup.size()
			? ResolveLegacyBattleSpriteImage(enemyGroup[(size_t)targetIndex].pluginId, enemyGroup[(size_t)targetIndex].spriteId)
			: NULL;
		if(legacySprite != NULL)
			DrawImageContain(*legacySprite, enemyRect);
		else
			DrawImageContain(imageEnemyVariant[variantIndex], enemyRect);
		enemyTargetRect = enemyRect;
		if(targetIndex == activeEnemyIndex) {
			drawTargetSelect(enemyRect);
			if(legacySprite != NULL)
				DrawImageContain(*legacySprite, enemyRect);
			else
				DrawImageContain(imageEnemyVariant[variantIndex], enemyRect);
		}
		if(targetIndex >= 0) {
			EnemyTouchTarget target = {};
			target.enemyIndex = targetIndex;
			target.rect = enemyRect;
			enemyTouchTargets.push_back(target);
		}
	}
	else {
		const int displayCount = std::min((int)enemyGroup.size(), 16);
		int cols = displayCount <= 2 ? 2 : (displayCount <= 6 ? 3 : 4);
		int rows = std::max(1, (displayCount + cols - 1) / cols);
		int maxFormationX = 0;
		int maxFormationY = 0;
		std::vector<int> formationHashes;
		formationHashes.reserve((size_t)displayCount);
		for(int i = 0; i < displayCount; i++) {
			const int formX = std::max(0, std::min(enemyGroup[(size_t)i].formationX, 7));
			const int formY = std::max(0, std::min(enemyGroup[(size_t)i].formationY, 7));
			maxFormationX = std::max(maxFormationX, formX);
			maxFormationY = std::max(maxFormationY, formY);
			const int hash = formY * 8 + formX;
			if(std::find(formationHashes.begin(), formationHashes.end(), hash) == formationHashes.end())
				formationHashes.push_back(hash);
		}
		const bool hasFormationLayout = formationHashes.size() >= 2;
		if(hasFormationLayout) {
			cols = std::max(2, maxFormationX + 1);
			rows = std::max(1, maxFormationY + 1);
		}

		const int slotGapX = std::max(6, LegacyRect(canvas, 0, 0, 8, 0).width);
		const int slotGapY = std::max(4, LegacyRect(canvas, 0, 0, 0, 8).height);
		const int fieldWidth = std::max(enemyRect.width + 120, LegacyRect(canvas, 0, 0, 308, 0).width);
		const int fieldHeight = std::max(enemyRect.height + 96, LegacyRect(canvas, 0, 0, 0, 244).height);
		ERect enemyField(enemyRect.x - (fieldWidth - enemyRect.width) / 2,
			enemyRect.y - std::max(18, (fieldHeight - enemyRect.height) / 3),
			fieldWidth,
			fieldHeight);
		enemyField = ClipRectToBounds(enemyField, arena);

		const int slotWidth = std::max(28, (enemyField.width - slotGapX * std::max(0, cols - 1)) / std::max(1, cols));
		const int slotHeight = std::max(28, (enemyField.height - slotGapY * std::max(0, rows - 1)) / std::max(1, rows));
		const int drawSize = std::max(24, std::min(enemySize, std::min(slotWidth, slotHeight)));
		const int usedWidth = drawSize * cols + slotGapX * std::max(0, cols - 1);
		const int usedHeight = drawSize * rows + slotGapY * std::max(0, rows - 1);
		const int startX = enemyField.x + (enemyField.width - usedWidth) / 2;
		const int startY = enemyField.y + (enemyField.height - usedHeight) / 2;

		for(int slot = 0; slot < displayCount; slot++) {
			const int enemyIndex = slot;
			int col = slot % cols;
			int row = slot / cols;
			if(hasFormationLayout) {
				col = std::max(0, std::min(enemyGroup[(size_t)enemyIndex].formationX, cols - 1));
				row = std::max(0, std::min(enemyGroup[(size_t)enemyIndex].formationY, rows - 1));
			}
			const ERect unitRect(startX + col * (drawSize + slotGapX), startY + row * (drawSize + slotGapY), drawSize, drawSize);
			if(enemyGroup[(size_t)enemyIndex].data.health > 0) {
				const int variantIndex = std::max(0, std::min(enemyGroup[(size_t)enemyIndex].artVariant, 3));
				EImage* legacySprite = ResolveLegacyBattleSpriteImage(enemyGroup[(size_t)enemyIndex].pluginId, enemyGroup[(size_t)enemyIndex].spriteId);
				if(legacySprite != NULL)
					DrawImageContain(*legacySprite, unitRect);
				else
					DrawImageContain(imageEnemyVariant[variantIndex], unitRect);

				EnemyTouchTarget target = {};
				target.enemyIndex = enemyIndex;
				target.rect = unitRect;
				enemyTouchTargets.push_back(target);

				if(enemyIndex == activeEnemyIndex) {
					enemyTargetRect = unitRect;
					drawTargetSelect(unitRect);
					if(legacySprite != NULL)
						DrawImageContain(*legacySprite, unitRect);
					else
						DrawImageContain(imageEnemyVariant[variantIndex], unitRect);
				}
			}
		}

		if(activeEnemyIndex < 0 or activeEnemyIndex >= (int)enemyGroup.size() or enemyGroup[(size_t)activeEnemyIndex].data.health <= 0) {
			if(enemyTouchTargets.empty() == false)
				enemyTargetRect = enemyTouchTargets[0].rect;
		}
	}

	if(playerEffectType != BATTLE_EFFECT_NONE and playerEffectTimestamp != 0 and GetMilliseconds() - playerEffectTimestamp >= playerEffectDuration)
		playerEffectType = BATTLE_EFFECT_NONE;
	if(enemyEffectType != BATTLE_EFFECT_NONE and enemyEffectTimestamp != 0 and GetMilliseconds() - enemyEffectTimestamp >= enemyEffectDuration)
		enemyEffectType = BATTLE_EFFECT_NONE;
	DrawEffectForRect(playerRect, playerEffectType, playerEffectTimestamp, playerEffectDuration);
	DrawEffectForRect(enemyTargetRect, enemyEffectType, enemyEffectTimestamp, enemyEffectDuration);

	ERect playerBar = LegacyRect(canvas, 182, 98, 240, 16);
	ERect enemyBar = LegacyRect(canvas, 538, 98, 240, 16);
	const int livingEnemies = LivingEnemyCount();
	const int enemyHealthBarValue = livingEnemies > 1 ? TotalEnemyHealth() : std::max(0, enemy.health);
	const int enemyHealthBarMax = livingEnemies > 1 ? TotalEnemyHealthMax() : std::max(1, enemy.healthMax);
#if defined(DRAGON_TEST)
	ReportValidationLabel(EString().Format("Enemy count: %d", livingEnemies), enemyBar);
	ReportValidationLabel(EString().Format("Selected target index: %d", std::max(0, activeEnemyIndex)), enemyTargetRect);
	if(enemy.name.IsEmpty() == false)
		ReportValidationLabel(EString().Format("Selected target name: %s", (const char*)enemy.name), enemyTargetRect);
	if(pendingTargetCommand != PENDING_TARGET_NONE) {
		const char* targetMode = pendingTargetCommand == PENDING_TARGET_ATTACK
			? "ATTACK"
			: (pendingTargetCommand == PENDING_TARGET_MAGIC ? "MAGIC" : "TECH");
		if(hasPendingLegacyCommandAction and pendingLegacyCommandAction.name.IsEmpty() == false)
			targetMode = pendingLegacyCommandAction.name;
		ReportValidationLabel(EString().Format("Pending target mode: %s", targetMode), enemyBar);
	}
	for(size_t i = 0; i < enemyTouchTargets.size(); i++) {
		const EnemyTouchTarget& target = enemyTouchTargets[i];
		if(target.enemyIndex < 0 or target.enemyIndex >= (int)enemyGroup.size())
			continue;
		ReportValidationLabel(
			EString().Format("Enemy target %d: %s", target.enemyIndex, (const char*)enemyGroup[(size_t)target.enemyIndex].data.name),
			target.rect
		);
	}
#endif
	imageHealthBack.DrawRect(playerBar);
	imageHealthBack.DrawRect(enemyBar);
	imageHealthFill.DrawRect(ERect(playerBar.x + 2, playerBar.y + 2, (playerBar.width - 4) * std::max(0, (int)gSave.health) / std::max(1, DragonGetMaxHealth()), playerBar.height - 4));
	imageHealthFill.DrawRect(ERect(enemyBar.x + 2, enemyBar.y + 2, (enemyBar.width - 4) * enemyHealthBarValue / std::max(1, enemyHealthBarMax), enemyBar.height - 4));
	if(livingEnemies > 1) {
		const int pipCount = std::min(8, (int)enemyGroup.size());
		const int pipGap = 4;
		const int pipWidth = std::max(10, (enemyBar.width - (pipCount - 1) * pipGap) / std::max(1, pipCount));
		const int pipHeight = std::max(6, LegacyRect(canvas, 0, 0, 0, 8).height);
		int pipX = enemyBar.x;
		for(int i = 0; i < pipCount; i++) {
			const bool alive = enemyGroup[(size_t)i].data.health > 0;
			const ERect pipRect(pipX, enemyBar.y + enemyBar.height + 2, pipWidth, pipHeight);
			imageHealthBack.DrawRect(pipRect);
			if(alive)
				imageHealthFill.DrawRect(ERect(pipRect.x + 1, pipRect.y + 1, std::max(2, pipRect.width - 2), std::max(2, pipRect.height - 2)));
			pipX += pipWidth + pipGap;
		}
	}

	// Keep action row hit regions synchronized even when input arrives before draw.
	UpdateLayoutRects();

	if(not fontHeader.IsEmpty()) {
		const int headerHeight = LayoutLineHeight(fontHeader, 12, 24);
		ERect playerNameRect(playerBar.x, playerBar.y - headerHeight - 2, playerBar.width, headerHeight);
		ERect enemyNameRect(enemyBar.x, enemyBar.y - headerHeight - 2, enemyBar.width, headerHeight);
		DrawLeftClampedLabel(fontHeader, gSave.name[0] == '\0' ? "Hero" : gSave.name, playerNameRect);
		if(livingEnemies > 1)
			DrawLeftClampedLabel(fontHeader, EString().Format("%s +%d", (const char*)enemy.name, livingEnemies - 1), enemyNameRect);
		else
			DrawLeftClampedLabel(fontHeader, enemy.name, enemyNameRect);
	}

	if(not fontMain.IsEmpty()) {
		const bool compactLegacyHud = strictLegacyBattle;
		if(compactLegacyHud == false and areaInfo != NULL) {
			const char* battleTag = "Battle";
			if(storyBattle)
				battleTag = "Story Battle";
			else if(trialBattle)
				battleTag = "Trial";
			else if(rareRoamingBattle)
				battleTag = "Elite";
			const int infoLineHeight = LayoutLineHeight(fontMain, 12, 20);
			ERect sceneInfoRect(arena.x + 20, arena.y + 2, std::max(80, arena.width - 40), infoLineHeight);
			DrawLeftClampedLabel(fontMain, EString().Format("%s | %s | R%d", areaInfo->name, battleTag, std::max(1, roundNumber)), sceneInfoRect);
			if(fallbackBattlePath) {
				ERect sourceRect(sceneInfoRect.x, sceneInfoRect.y + infoLineHeight + 1, sceneInfoRect.width, infoLineHeight);
				DrawLeftClampedLabel(fontMain, EString().Format("Source: %s", (const char*)battleSourceTag), sourceRect);
			}
		}
		const int statsLineHeight = LayoutLineHeight(fontMain, 12, 20);
		ERect playerStatsRect(playerBar.x, playerBar.y + 18, playerBar.width, statsLineHeight);
		ERect enemyStatsRect(enemyBar.x, enemyBar.y + 18, enemyBar.width, statsLineHeight);
		DrawLeftClampedLabel(fontMain, EString().Format("HP %d/%d", (int)gSave.health, DragonGetMaxHealth()), playerStatsRect);
		if(livingEnemies > 1) {
			DrawLeftClampedLabel(
				fontMain,
				EString().Format("Target %s %d/%d", (const char*)enemy.name, std::max(0, enemy.health), std::max(1, enemy.healthMax)),
				enemyStatsRect
			);
			if(compactLegacyHud == false) {
				ERect enemyGroupRect(enemyBar.x, enemyStatsRect.y + statsLineHeight, enemyBar.width, statsLineHeight);
				DrawLeftClampedLabel(fontMain, EString().Format("Group %d/%d", enemyHealthBarValue, enemyHealthBarMax), enemyGroupRect);
			}
		}
		else {
			DrawLeftClampedLabel(fontMain, EString().Format("HP %d/%d", enemy.health, enemy.healthMax), enemyStatsRect);
			if(compactLegacyHud == false) {
				ERect enemyLevelRect(enemyBar.x, enemyStatsRect.y + statsLineHeight, enemyBar.width, statsLineHeight);
				DrawLeftClampedLabel(fontMain, EString().Format("Lv %d", enemy.level), enemyLevelRect);
			}
		}
		if(strictLegacyBattle == false) {
			const int skillPercent = std::max(0, std::min(kBattleSkillChargeMax, skillCharge));
			ERect chargeBack(playerBar.x, playerBar.y + LegacyRect(canvas, 0, 0, 0, 38).height, playerBar.width, std::max(10, LegacyRect(canvas, 0, 0, 0, 12).height));
			imageHealthBack.DrawRect(chargeBack);
			imageHealthFill.DrawRect(ERect(chargeBack.x + 2, chargeBack.y + 2, (chargeBack.width - 4) * skillPercent / kBattleSkillChargeMax, chargeBack.height - 4));
			ERect chargeLabelRect(chargeBack.x, chargeBack.y + chargeBack.height + 2, chargeBack.width, statsLineHeight);
			DrawLeftClampedLabel(fontMain, EString().Format("Charge %d%%", skillPercent), chargeLabelRect);
		}
			const int actionBottomLimit = attackRect.y - std::max(4, LegacyRect(canvas, 0, 0, 0, 8).height);
			const ERect strictActionRect = LegacyRect(canvas, 190, 456, 548, 84);
			const ERect adaptiveActionRect = LegacyRect(canvas, 190, 446, 548, 104);
			ERect actionRectBase = compactLegacyHud ? strictActionRect : adaptiveActionRect;
			int actionHeight = actionRectBase.height;
			if(actionBottomLimit > actionRectBase.y + 24)
				actionHeight = std::min(actionHeight, actionBottomLimit - actionRectBase.y);
			actionHeight = std::max(24, actionHeight);
			actionRectBase.height = actionHeight;
		ERect actionRectText = ClipRectToBounds(actionRectBase, safe);
		if(actionRectText.width > 0 and actionRectText.height > 0) {
			DrawWrappedLabel(fontMain, actionText, actionRectText, compactLegacyHud ? 3 : 4);
	#if defined(DRAGON_TEST)
			if(validationPlayerActionSerial > 0 and validationPlayerActionText.IsEmpty() == false) {
				ReportValidationLabel(EString().Format("Player action serial: %d", validationPlayerActionSerial), actionRectText);
				ReportValidationLabel(EString().Format("Player action result: %s", (const char*)validationPlayerActionText), actionRectText);
			}
			if(validationEnemyActionSerial > 0 and validationEnemyActionText.IsEmpty() == false) {
				ReportValidationLabel(EString().Format("Enemy action serial: %d", validationEnemyActionSerial), actionRectText);
				ReportValidationLabel(EString().Format("Enemy action result: %s", (const char*)validationEnemyActionText), actionRectText);
			}
	#endif
		}
		if(pendingTargetCommand != PENDING_TARGET_NONE) {
			const char* targetMode = pendingTargetCommand == PENDING_TARGET_ATTACK
				? "ATTACK"
				: (pendingTargetCommand == PENDING_TARGET_MAGIC ? "MAGIC" : "TECH");
			if(hasPendingLegacyCommandAction and not pendingLegacyCommandAction.name.IsEmpty())
				targetMode = pendingLegacyCommandAction.name;
			const int selectHeight = LayoutLineHeight(fontMain, 12, 28);
			ERect selectRect = ClipRectToBounds(
				ERect(actionRectText.x, actionRectText.y - selectHeight - 4, actionRectText.width, selectHeight),
				safe
			);
			if(strictLegacyBattle and imageSpellName.IsEmpty() == false) {
				const int panelWidth = std::min(selectRect.width, std::max(selectHeight * 2, LegacyRect(canvas, 0, 0, 192, 0).width));
				const int panelHeight = std::max(selectRect.height, LegacyRect(canvas, 0, 0, 0, 24).height);
				ERect panelRect(
					selectRect.x + (selectRect.width - panelWidth) / 2,
					selectRect.y + (selectRect.height - panelHeight) / 2,
					panelWidth,
					panelHeight
				);
				panelRect = ClipRectToBounds(panelRect, safe);
				DrawImageContain(imageSpellName, panelRect, EColor(0xffffffd8));
				selectRect = InsetRect(panelRect, 2);
			}
			if(selectRect.width > 0 and selectRect.height > 0)
				DrawCenteredLabel(fontMain, EString().Format("SELECT TARGET FOR %s", targetMode), selectRect);
		}

		if(actionTimestamp != 0 and GetMilliseconds() - actionTimestamp < 1200) {
			if(lastPlayerDamage > 0)
				fontMain.Draw(EString().Format("-%d", lastPlayerDamage), enemyTargetRect.x + enemyTargetRect.width / 4, enemyTargetRect.y - 12);
			if(lastEnemyDamage > 0)
				fontMain.Draw(EString().Format("-%d", lastEnemyDamage), playerRect.x + playerRect.width / 4, playerRect.y - 12);
		}
	}

	imageAction.DrawRect(attackRect);
	imageAction.DrawRect(skillRect);
	const bool legacyMagicTechReady = strictLegacyBattle or CanUseSkill();
	(legacyMagicTechReady ? imageAction : imageActionAlt).DrawRect(itemRect);
	(legacyMagicTechReady ? imageAction : imageActionAlt).DrawRect(techRect);
	(retreatLocked ? imageActionAlt : imageAction).DrawRect(retreatRect);
#if defined(DRAGON_TEST)
	ReportValidationLabel("ATTACK", attackRect);
	ReportValidationLabel("DEFEND", skillRect);
	ReportValidationLabel("MAGIC", itemRect);
	ReportValidationLabel("TECH", techRect);
	ReportValidationLabel(retreatLocked ? "NO RUN" : "RUN", retreatRect);
	const bool compactLegacyHudForTelemetry = strictLegacyBattle;
	const int actionBottomLimit = attackRect.y - std::max(4, LegacyRect(canvas, 0, 0, 0, 8).height);
	const ERect strictActionRect = LegacyRect(canvas, 190, 456, 548, 84);
	const ERect adaptiveActionRect = LegacyRect(canvas, 190, 446, 548, 104);
	ERect actionRectBase = compactLegacyHudForTelemetry ? strictActionRect : adaptiveActionRect;
	int actionHeight = actionRectBase.height;
	if(actionBottomLimit > actionRectBase.y + 24)
		actionHeight = std::min(actionHeight, actionBottomLimit - actionRectBase.y);
	actionHeight = std::max(24, actionHeight);
	actionRectBase.height = actionHeight;
	const ERect actionRectText = ClipRectToBounds(actionRectBase, safe);
	if(actionRectText.width > 0 and actionRectText.height > 0) {
		if(validationPlayerActionSerial > 0 and validationPlayerActionText.IsEmpty() == false) {
			ReportValidationLabel(EString().Format("Player action serial: %d", validationPlayerActionSerial), actionRectText);
			ReportValidationLabel(EString().Format("Player action result: %s", (const char*)validationPlayerActionText), actionRectText);
		}
		if(validationEnemyActionSerial > 0 and validationEnemyActionText.IsEmpty() == false) {
			ReportValidationLabel(EString().Format("Enemy action serial: %d", validationEnemyActionSerial), actionRectText);
			ReportValidationLabel(EString().Format("Enemy action result: %s", (const char*)validationEnemyActionText), actionRectText);
		}
	}
#endif
	const int mainLineHeight = fontMain.IsEmpty() ? 16 : LayoutLineHeight(fontMain, 12, 28);
	const int mainLineStep = fontMain.IsEmpty() ? 18 : LayoutLineStep(fontMain, 14, 30, 2);

	if(not fontMain.IsEmpty()) {
		DrawCenteredLabel(fontMain, "ATTACK", attackRect);
		DrawCenteredLabel(fontMain, "DEFEND", skillRect);
		DrawCenteredLabel(fontMain, "MAGIC", itemRect);
		DrawCenteredLabel(fontMain, "TECH", techRect);
		DrawCenteredLabel(fontMain, retreatLocked ? "NO RUN" : "RUN", retreatRect);
		if(strictLegacyBattle == false and CanUseSkill() == false) {
			ERect skillHintRect(itemRect.x, itemRect.y - mainLineHeight - 2, techRect.x + techRect.width - itemRect.x, mainLineHeight);
			DrawCenteredLabel(fontMain, EString().Format("CHARGE %d%%", std::max(0, std::min(kBattleSkillChargeMax, skillCharge))), skillHintRect);
		}
	}

	if(skillMenuActive) {
		imageOverlay.DrawRect(safe);
		const bool showLegacyCommands = strictLegacyBattle and legacyCommandMenuMode != LEGACY_COMMAND_MENU_NONE and legacyCommandActions.empty() == false;
		const bool showAlternateSkill = showLegacyCommands ? (legacyCommandActions.size() >= 2) : (strictLegacyBattle == false);
		const bool showTertiarySkill = showLegacyCommands and legacyCommandActions.size() >= 3;
		const int rowCount = showTertiarySkill ? 3 : (showAlternateSkill ? 2 : 1);
		const int menuInsetX = std::max(12, LegacyRect(canvas, 0, 0, 16, 0).width);
		const int menuGapY = std::max(4, LegacyRect(canvas, 0, 0, 0, 8).height);
		const int baseRowHeight = std::max(26, LegacyRect(canvas, 0, 0, 0, 42).height);
		const int rowHeight = (showLegacyCommands and rowCount >= 3)
			? std::max(24, baseRowHeight - std::max(4, baseRowHeight / 5))
			: baseRowHeight;
		const int cancelHeight = std::max(24, LegacyRect(canvas, 0, 0, 0, 28).height);
		const int titleHeight = fontHeader.IsEmpty() ? 18 : LayoutLineHeight(fontHeader, 12, 24);
		const int menuWidth = std::min(std::max(260, LegacyRect(canvas, 0, 0, 360, 0).width), std::max(220, safe.width - 16));
		const int desiredMenuHeight = std::max(
			176,
			titleHeight + 22 + rowCount * rowHeight + std::max(0, rowCount - 1) * menuGapY + menuGapY + cancelHeight + 14
		);
		const int menuHeight = std::min(desiredMenuHeight, std::max(176, safe.height - 16));
		skillMenuRect = ClipRectToBounds(
			ERect(
				safe.x + (safe.width - menuWidth) / 2,
				safe.y + (safe.height - menuHeight) / 2,
				menuWidth,
				menuHeight
			),
			safe
		);
		DrawImageContain(imageArena, skillMenuRect, EColor(0xffffffb8));

		const int contentX = skillMenuRect.x + menuInsetX;
		const int contentWidth = std::max(100, skillMenuRect.width - menuInsetX * 2);
		const int rowStartY = skillMenuRect.y + std::max(24, titleHeight + 14);
		skillPrimaryRect = ERect(contentX, rowStartY, contentWidth, rowHeight);
		if(showAlternateSkill)
			skillAlternateRect = ERect(skillPrimaryRect.x, skillPrimaryRect.y + skillPrimaryRect.height + menuGapY, skillPrimaryRect.width, skillPrimaryRect.height);
		else
			skillAlternateRect = ERect();
		if(showTertiarySkill)
			skillTertiaryRect = ERect(skillAlternateRect.x, skillAlternateRect.y + skillAlternateRect.height + menuGapY, skillAlternateRect.width, skillAlternateRect.height);
		else
			skillTertiaryRect = ERect();
		const int lastButtonBottom = showTertiarySkill
			? (skillTertiaryRect.y + skillTertiaryRect.height)
			: (showAlternateSkill ? (skillAlternateRect.y + skillAlternateRect.height) : (skillPrimaryRect.y + skillPrimaryRect.height));
		const int cancelWidth = std::min(contentWidth, std::max(96, LegacyRect(canvas, 0, 0, 112, 0).width));
		skillCancelRect = ERect(
			skillMenuRect.x + (skillMenuRect.width - cancelWidth) / 2,
			lastButtonBottom + menuGapY,
			cancelWidth,
			cancelHeight
		);
		if(skillCancelRect.y + skillCancelRect.height > skillMenuRect.y + skillMenuRect.height - 8)
			skillCancelRect.y = skillMenuRect.y + skillMenuRect.height - skillCancelRect.height - 8;

		imageAction.DrawRect(skillPrimaryRect);
		if(showAlternateSkill)
			imageAction.DrawRect(skillAlternateRect);
		if(showTertiarySkill)
			imageAction.DrawRect(skillTertiaryRect);
			imageActionAlt.DrawRect(skillCancelRect);

			const ERect skillHeaderRect(skillMenuRect.x + 12, skillMenuRect.y + 8, skillMenuRect.width - 24, titleHeight);
			const bool useMagicHeader = legacyCommandMenuMode == LEGACY_COMMAND_MENU_MAGIC;
			const EString skillHeaderText = showLegacyCommands ? (useMagicHeader ? "MAGIC" : "TECH") : "SKILLS";
#if defined(DRAGON_TEST)
			ReportValidationLabel(skillHeaderText, skillHeaderRect);
			if(showLegacyCommands) {
				if(legacyCommandActions.empty() == false)
					ReportValidationLabel(legacyCommandActions[0].name, skillPrimaryRect);
				if(showAlternateSkill and legacyCommandActions.size() >= 2)
					ReportValidationLabel(legacyCommandActions[1].name, skillAlternateRect);
				if(showTertiarySkill and legacyCommandActions.size() >= 3)
					ReportValidationLabel(legacyCommandActions[2].name, skillTertiaryRect);
			}
			else {
				int avatarType = std::max((int)DRAGON_AVATAR_WARRIOR, std::min((int)gSave.avatarType, (int)DRAGON_AVATAR_COUNT - 1));
				ReportValidationLabel(showAlternateSkill ? kSkillButtonLabel[avatarType] : "LEGACY SKILL", skillPrimaryRect);
				if(showAlternateSkill)
					ReportValidationLabel(kAltSkillButtonLabel[avatarType], skillAlternateRect);
			}
			ReportValidationLabel("CANCEL", skillCancelRect);
#endif

			if(not fontHeader.IsEmpty()) {
				DrawCenteredLabel(
					fontHeader,
					skillHeaderText,
					skillHeaderRect
				);
			}
			if(not fontMain.IsEmpty()) {
			int avatarType = std::max((int)DRAGON_AVATAR_WARRIOR, std::min((int)gSave.avatarType, (int)DRAGON_AVATAR_COUNT - 1));
			if(showLegacyCommands) {
				DrawCenteredLabel(fontMain, legacyCommandActions[0].name, skillPrimaryRect);
				if(showAlternateSkill)
					DrawCenteredLabel(fontMain, legacyCommandActions[1].name, skillAlternateRect);
				if(showTertiarySkill)
					DrawCenteredLabel(fontMain, legacyCommandActions[2].name, skillTertiaryRect);
			}
			else {
				DrawCenteredLabel(fontMain, showAlternateSkill ? kSkillButtonLabel[avatarType] : "LEGACY SKILL", skillPrimaryRect);
				if(showAlternateSkill)
					DrawCenteredLabel(fontMain, kAltSkillButtonLabel[avatarType], skillAlternateRect);
			}
			DrawCenteredLabel(fontMain, "CANCEL", skillCancelRect);
		}
	}

	if(itemMenuActive and strictLegacyBattle == false) {
		imageOverlay.DrawRect(safe);
		itemMenuRect = LegacyRect(canvas, 220, 188, 360, 208);
		DrawImageContain(imageArena, itemMenuRect, EColor(0xffffffb8));
		itemPotionRect = ERect(
			itemMenuRect.x + LegacyRect(canvas, 0, 0, 16, 0).width,
			itemMenuRect.y + LegacyRect(canvas, 0, 0, 0, 42).height,
			itemMenuRect.width - LegacyRect(canvas, 0, 0, 32, 0).width,
			std::max(30, LegacyRect(canvas, 0, 0, 0, 42).height)
		);
		itemScrollRect = ERect(itemPotionRect.x, itemPotionRect.y + itemPotionRect.height + std::max(6, LegacyRect(canvas, 0, 0, 8, 0).width), itemPotionRect.width, itemPotionRect.height);
		itemCancelRect = LegacyRect(canvas, 344, 358, 112, 28);

		const int potionCount = CountInventoryType(DRAGON_ITEM_HEALTH_POTION);
		const int scrollCount = CountInventoryType(DRAGON_ITEM_FIRE_SCROLL);
		const bool potionUsable = potionCount > 0 and gSave.health < DragonGetMaxHealth();
		const bool scrollUsable = scrollCount > 0;

		(potionUsable ? imageAction : imageActionAlt).DrawRect(itemPotionRect, potionUsable ? EColor(0xffffffff) : EColor(0xffffffd4));
		(scrollUsable ? imageAction : imageActionAlt).DrawRect(itemScrollRect, scrollUsable ? EColor(0xffffffff) : EColor(0xffffffd4));
		imageActionAlt.DrawRect(itemCancelRect);

		if(not fontHeader.IsEmpty())
			DrawCenteredLabel(fontHeader, "ITEMS", ERect(itemMenuRect.x + 12, itemMenuRect.y + 12, itemMenuRect.width - 24, 20));
		if(not fontMain.IsEmpty()) {
			DrawCenteredLabel(fontMain, EString().Format("Health Potion  x%d", potionCount), itemPotionRect);
			DrawCenteredLabel(fontMain, EString().Format("Fire Scroll  x%d", scrollCount), itemScrollRect);
			DrawCenteredLabel(fontMain, "CANCEL", itemCancelRect);
			if(potionCount > 0 and potionUsable == false)
				DrawCenteredLabel(fontMain, "HP is already full.", ERect(itemMenuRect.x + 12, itemMenuRect.y + itemMenuRect.height - 60, itemMenuRect.width - 24, 16));
			else if(potionUsable == false and scrollUsable == false)
				DrawCenteredLabel(fontMain, "No usable consumables.", ERect(itemMenuRect.x + 12, itemMenuRect.y + itemMenuRect.height - 60, itemMenuRect.width - 24, 16));
		}
	}

		if(battleDone) {
			imageOverlay.DrawRect(safe);
			if(battleWon == false and battleRetreated == false and imageGameOver.IsEmpty() == false) {
				ERect gameOverRect = LegacyRect(canvas, 220, 190, 360, 136);
				DrawImageContain(imageGameOver, gameOverRect, EColor(0xffffffd8));
			}
			((battleWon or battleRetreated) ? imageAction : imageActionAlt).DrawRect(continueRect);
			const EString outcomeHeader = battleWon ? "VICTORY" : (battleRetreated ? "RETREATED" : "DEFEAT");
#if defined(DRAGON_TEST)
			ReportValidationLabel(outcomeHeader, LegacyRect(canvas, 312, 276, 176, 24));
			ReportValidationLabel("CONTINUE", continueRect);
#endif
			if(not fontHeader.IsEmpty()) {
				ERect headerRect = LegacyRect(canvas, 312, 276, 176, 24);
				DrawCenteredLabel(fontHeader, outcomeHeader, headerRect);
			}
				if(not fontMain.IsEmpty()) {
					EString resultText;
				if(battleWon) {
					if(strictLegacyBattle)
						resultText = "Victory.";
					else {
						resultText = EString().Format("+%d XP  +%d Gold", victoryXP, victoryGold);
						if(victoryHealthRecovered > 0)
							resultText += EString().Format("  +%d HP", victoryHealthRecovered);
						if(levelGain > 0)
							resultText += EString().Format("  Lv +%d", levelGain);
						if(not victoryMilestoneRewards.IsEmpty())
							resultText += EString().Format("  Gear: %s", (const char*)victoryMilestoneRewards);
						if(victoryPotionDrops > 0 or victoryScrollDrops > 0) {
							resultText += "  Loot:";
							if(victoryPotionDrops > 0)
								resultText += EString().Format(" Potion x%d", victoryPotionDrops);
							if(victoryScrollDrops > 0)
								resultText += EString().Format(" Scroll x%d", victoryScrollDrops);
						}
					}
					if(victoryLegacyLootItems > 0 or victoryLegacyLootGold > 0) {
						resultText += strictLegacyBattle ? "  Loot:" : "  Legacy:";
						if(victoryLegacyLootItems > 0) {
							if(victoryLegacyLootItems == 1 and not victoryLegacyLootFirstItem.IsEmpty())
								resultText += EString().Format(" %s", (const char*)victoryLegacyLootFirstItem);
							else
								resultText += EString().Format(" Items x%d", victoryLegacyLootItems);
						}
						if(victoryLegacyLootGold > 0)
							resultText += EString().Format(" +%dg", victoryLegacyLootGold);
					}
				}
			else if(battleRetreated)
				resultText = retreatHealthLoss > 0 ? EString().Format("You escaped. -%d HP.", retreatHealthLoss) : "You escaped.";
			else
				resultText = defeatGoldLoss > 0 ? EString().Format("You fall back. -%d Gold.", defeatGoldLoss) : "You fall back and regroup.";
				ERect resultRect = ClipRectToBounds(LegacyRect(canvas, 152, 306, 496, mainLineStep * 3 + 4), safe);
#if defined(DRAGON_TEST)
				if(resultText.IsEmpty() == false)
					ReportValidationLabel(resultText, resultRect);
#endif
				DrawWrappedLabel(fontMain, resultText, resultRect, 3);
				DrawCenteredLabel(fontMain, "CONTINUE", continueRect);
			}
		}
}

void Battle::OnTouch (int x, int y) {
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
	UpdateLayoutRects();

	if(battleDone) {
		if(HitTouchRect(continueRect, x, y, 72, 44, 2)) {
			PlaySoundIfLoaded(soundClick);
			RunNewNode("WorldMap");
		}
		return;
	}

	if(skillMenuActive) {
		if(skillMenuRect.IsPointInRect(x, y) == false) {
			skillMenuActive = false;
			legacyCommandMenuMode = LEGACY_COMMAND_MENU_NONE;
			legacyCommandActions.clear();
			skillTertiaryRect = ERect();
			PlaySoundIfLoaded(soundClick);
			return;
		}
		if(HitTouchRect(skillCancelRect, x, y, 72, 44, 2)) {
			skillMenuActive = false;
			legacyCommandMenuMode = LEGACY_COMMAND_MENU_NONE;
			legacyCommandActions.clear();
			skillTertiaryRect = ERect();
			PlaySoundIfLoaded(soundClick);
			return;
		}
		auto triggerLegacyCommandSelection = [&] (size_t commandIndex) {
			if(commandIndex >= legacyCommandActions.size())
				return false;
			const LegacyCommandAction action = legacyCommandActions[commandIndex];
			skillMenuActive = false;
			legacyCommandMenuMode = LEGACY_COMMAND_MENU_NONE;
			legacyCommandActions.clear();
			skillTertiaryRect = ERect();
			PlaySoundIfLoaded(soundClick);
			if(LegacyCommandNeedsEnemyTarget(action)) {
				pendingTargetCommand = action.isMagic ? PENDING_TARGET_MAGIC : PENDING_TARGET_TECH;
				pendingLegacyCommandAction = action;
				hasPendingLegacyCommandAction = true;
				SetActionTextThrottled(EString().Format("Select an enemy target for %s.", (const char*)action.name), kBattleTargetHintThrottleMs);
				return true;
			}
			ResolveLegacyCommandAction(action);
			return true;
		};
		if(strictLegacyBattle and legacyCommandMenuMode != LEGACY_COMMAND_MENU_NONE and legacyCommandActions.empty() == false) {
			if(HitTouchRect(skillPrimaryRect, x, y, 72, 44, 2)) {
				triggerLegacyCommandSelection(0);
				return;
			}
			if(HitTouchRect(skillAlternateRect, x, y, 72, 44, 2)) {
				triggerLegacyCommandSelection(1);
				return;
			}
			if(HitTouchRect(skillTertiaryRect, x, y, 72, 44, 2)) {
				triggerLegacyCommandSelection(2);
				return;
			}
			return;
		}
		if(HitTouchRect(skillPrimaryRect, x, y, 72, 44, 2)) {
			skillMenuActive = false;
			PlaySoundIfLoaded(soundClick);
			ResolvePlayerAttack(true);
			return;
		}
		if(HitTouchRect(skillAlternateRect, x, y, 72, 44, 2)) {
			skillMenuActive = false;
			PlaySoundIfLoaded(soundClick);
			ResolveAlternateSkill();
			return;
		}
		return;
	}

	if(itemMenuActive) {
		if(itemMenuRect.IsPointInRect(x, y) == false) {
			itemMenuActive = false;
			PlaySoundIfLoaded(soundClick);
			return;
		}
		if(HitTouchRect(itemCancelRect, x, y, 72, 44, 2)) {
			itemMenuActive = false;
			PlaySoundIfLoaded(soundClick);
			return;
		}

		const int potionCount = CountInventoryType(DRAGON_ITEM_HEALTH_POTION);
		const int scrollCount = CountInventoryType(DRAGON_ITEM_FIRE_SCROLL);
		const bool potionUsable = potionCount > 0 and gSave.health < DragonGetMaxHealth();
		const bool scrollUsable = scrollCount > 0;

		if(HitTouchRect(itemPotionRect, x, y, 72, 44, 2)) {
			if(potionUsable) {
				PlaySoundIfLoaded(soundClick);
				if(ResolvePlayerItem(DRAGON_ITEM_HEALTH_POTION))
					itemMenuActive = false;
			} else {
				actionText = potionCount > 0 ? "Health is already full." : "No Health Potion available.";
				actionTimestamp = GetMilliseconds();
				PlaySoundIfLoaded(soundMiss);
			}
			return;
		}
		if(HitTouchRect(itemScrollRect, x, y, 72, 44, 2)) {
			if(scrollUsable) {
				PlaySoundIfLoaded(soundClick);
				if(ResolvePlayerItem(DRAGON_ITEM_FIRE_SCROLL))
					itemMenuActive = false;
			} else {
				actionText = "No Fire Scroll available.";
				actionTimestamp = GetMilliseconds();
				PlaySoundIfLoaded(soundMiss);
			}
			return;
		}
		return;
	}

	const int livingEnemyCount = LivingEnemyCount();
	const ERect commandLaneRect = CommandLaneRect();
	const bool nearCommandRow = commandLaneRect.height > 0 and y >= commandLaneRect.y - 4;
	const bool hitCommandLaneControl = HitCommandLaneControl(x, y);
	auto resolvePendingTargetSelection = [&] (int enemyIndex) -> bool {
		if(enemyIndex < 0 or enemyIndex >= (int)enemyGroup.size())
			return false;
		if(enemyGroup[(size_t)enemyIndex].data.health <= 0)
			return false;
		activeEnemyIndex = enemyIndex;
		SyncActiveEnemyFromGroup();
		PlaySoundIfLoaded(soundClick);
		if(hasPendingLegacyCommandAction) {
			ResolveLegacyCommandAction(pendingLegacyCommandAction);
			return true;
		}
		if(pendingTargetCommand == PENDING_TARGET_ATTACK)
			ResolvePlayerAttack(false);
		else if(pendingTargetCommand == PENDING_TARGET_MAGIC)
			ResolvePlayerMagic();
		else if(pendingTargetCommand == PENDING_TARGET_TECH)
			ResolvePlayerTech();
		return true;
	};
	const int exactEnemyIndex = FindLivingEnemyTouchTargetAtPoint(x, y);
	if(pendingTargetCommand != PENDING_TARGET_NONE and exactEnemyIndex >= 0) {
		// During target-select mode, allow direct enemy taps even when oversized
		// legacy sprites overlap the command-row band.
		if((nearCommandRow and hitCommandLaneControl) == false) {
			if(resolvePendingTargetSelection(exactEnemyIndex))
				return;
		}
	}
	if(nearCommandRow == false) {
		for(auto target = enemyTouchTargets.rbegin(); target != enemyTouchTargets.rend(); ++target) {
			if(HitTouchRect(target->rect, x, y, 72, 44, 2) == false)
				continue;
			if(target->enemyIndex >= 0
				and target->enemyIndex < (int)enemyGroup.size()
				and enemyGroup[(size_t)target->enemyIndex].data.health > 0
				and pendingTargetCommand != PENDING_TARGET_NONE) {
				if(target->enemyIndex != exactEnemyIndex and resolvePendingTargetSelection(target->enemyIndex))
					return;
			}
			if(livingEnemyCount <= 1)
				break;
			if(target->enemyIndex == activeEnemyIndex) {
				if(TryCycleEnemyTarget()) {
					PlaySoundIfLoaded(soundClick);
					return;
				}
				break;
			}
			if(target->enemyIndex >= 0
				and target->enemyIndex < (int)enemyGroup.size()
				and enemyGroup[(size_t)target->enemyIndex].data.health > 0) {
				activeEnemyIndex = target->enemyIndex;
				SyncActiveEnemyFromGroup();
				actionText = EString().Format("Targeting %s. %d foes remain.", (const char*)enemy.name, livingEnemyCount);
				actionTimestamp = GetMilliseconds();
				PlaySoundIfLoaded(soundClick);
				return;
			}
		}
	}
	if(pendingTargetCommand != PENDING_TARGET_NONE and nearCommandRow == false) {
		if(livingEnemyCount == 1) {
			const int loneEnemyIndex = FirstLivingEnemyIndex();
			if(loneEnemyIndex >= 0 and resolvePendingTargetSelection(loneEnemyIndex))
				return;
		}
		const int nearestEnemyIndex = FindNearestLivingEnemyTouchTarget(x, y, kBattlePendingTargetSnapDistancePx);
		if(nearestEnemyIndex >= 0 and resolvePendingTargetSelection(nearestEnemyIndex))
			return;
		SetActionTextThrottled("Tap an enemy portrait to confirm target.", kBattleTargetHintThrottleMs);
		PlaySoundIfLoaded(soundMiss);
		return;
	}
	if(pendingTargetCommand == PENDING_TARGET_NONE and livingEnemyCount > 1 and nearCommandRow == false) {
		const int nearestEnemyIndex = FindNearestLivingEnemyTouchTarget(x, y, kBattleTargetSnapDistancePx);
		if(nearestEnemyIndex >= 0) {
			if(nearestEnemyIndex == activeEnemyIndex) {
				if(TryCycleEnemyTarget()) {
					PlaySoundIfLoaded(soundClick);
					return;
				}
			}
			else if(nearestEnemyIndex < (int)enemyGroup.size() and enemyGroup[(size_t)nearestEnemyIndex].data.health > 0) {
				activeEnemyIndex = nearestEnemyIndex;
				SyncActiveEnemyFromGroup();
				actionText = EString().Format("Targeting %s. %d foes remain.", (const char*)enemy.name, livingEnemyCount);
				actionTimestamp = GetMilliseconds();
				PlaySoundIfLoaded(soundClick);
				return;
			}
		}
	}

	if(HitTouchRect(attackRect, x, y, 72, 44, 2)) {
		itemMenuActive = false;
		skillMenuActive = false;
		PlaySoundIfLoaded(soundClick);
		if(pendingTargetCommand == PENDING_TARGET_ATTACK) {
			ClearPendingTargetCommand();
			actionText = "Attack target selection canceled.";
			actionTimestamp = GetMilliseconds();
			return;
		}
		if(AttackRequiresEnemyTargetSelection()) {
			pendingTargetCommand = PENDING_TARGET_ATTACK;
			SetActionTextThrottled("Select an enemy target for ATTACK.", kBattleTargetHintThrottleMs);
			return;
		}
		ResolvePlayerAttack(false);
		return;
	}
	if(HitTouchRect(skillRect, x, y, 72, 44, 2)) {
		ClearPendingTargetCommand();
		itemMenuActive = false;
		skillMenuActive = false;
		PlaySoundIfLoaded(soundClick);
		ResolveDefend();
		return;
	}
	if(HitTouchRect(itemRect, x, y, 72, 44, 2)) {
		itemMenuActive = false;
		skillMenuActive = false;
		PlaySoundIfLoaded(soundClick);
		if(pendingTargetCommand == PENDING_TARGET_MAGIC) {
			const EString pendingLabel = pendingLegacyCommandAction.name;
			const bool hadLegacyAction = hasPendingLegacyCommandAction;
			ClearPendingTargetCommand();
			actionText = hadLegacyAction and not pendingLabel.IsEmpty()
				? EString().Format("%s target selection canceled.", (const char*)pendingLabel)
				: "Magic target selection canceled.";
			actionTimestamp = GetMilliseconds();
			return;
		}
		if(strictLegacyBattle) {
			OpenLegacyCommandMenu(LEGACY_COMMAND_MENU_MAGIC);
			return;
		}
		if(MagicRequiresEnemyTargetSelection()) {
			pendingTargetCommand = PENDING_TARGET_MAGIC;
			SetActionTextThrottled("Select an enemy target for MAGIC.", kBattleTargetHintThrottleMs);
			return;
		}
		ResolvePlayerMagic();
		return;
	}
	if(HitTouchRect(techRect, x, y, 72, 44, 2)) {
		itemMenuActive = false;
		skillMenuActive = false;
		PlaySoundIfLoaded(soundClick);
		if(pendingTargetCommand == PENDING_TARGET_TECH) {
			const EString pendingLabel = pendingLegacyCommandAction.name;
			const bool hadLegacyAction = hasPendingLegacyCommandAction;
			ClearPendingTargetCommand();
			actionText = hadLegacyAction and not pendingLabel.IsEmpty()
				? EString().Format("%s target selection canceled.", (const char*)pendingLabel)
				: "Tech target selection canceled.";
			actionTimestamp = GetMilliseconds();
			return;
		}
		if(strictLegacyBattle) {
			OpenLegacyCommandMenu(LEGACY_COMMAND_MENU_TECH);
			return;
		}
		if(TechRequiresEnemyTargetSelection()) {
			pendingTargetCommand = PENDING_TARGET_TECH;
			SetActionTextThrottled("Select an enemy target for TECH.", kBattleTargetHintThrottleMs);
			return;
		}
		ResolvePlayerTech();
		return;
	}
	if(HitTouchRect(retreatRect, x, y, 72, 44, 2)) {
		ClearPendingTargetCommand();
		itemMenuActive = false;
		skillMenuActive = false;
		PlaySoundIfLoaded(soundClick);
		TryRetreat();
		return;
	}
	if(pendingTargetCommand != PENDING_TARGET_NONE and nearCommandRow) {
		SetActionTextThrottled("Tap ATTACK, MAGIC, or TECH to cancel target selection.", kBattleTargetHintThrottleMs);
		PlaySoundIfLoaded(soundMiss);
		return;
	}
}

void Battle::OnTouchUp (int x, int y) {
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

void Battle::OnTouchMove (int x, int y) {
	EPoint input = NormalizeInputPoint(*this, x, y);
	x = input.x;
	y = input.y;
	if(touchHandledThisSequence
		and (std::abs(x - touchDownX) > 3 or std::abs(y - touchDownY) > 3))
		touchMovedThisSequence = true;
	if(battleDone or pendingTargetCommand == PENDING_TARGET_NONE)
		return;
	if(skillMenuActive or itemMenuActive)
		return;
	UpdateLayoutRects();

	const ERect commandLaneRect = CommandLaneRect();
	const bool nearCommandRow = commandLaneRect.height > 0 and y >= commandLaneRect.y - 4;
	if(nearCommandRow)
		return;

	int hoverEnemyIndex = FindLivingEnemyTouchTargetAtPoint(x, y);
	if(hoverEnemyIndex < 0)
		hoverEnemyIndex = FindNearestLivingEnemyTouchTarget(x, y, kBattlePendingTargetSnapDistancePx);
	if(hoverEnemyIndex < 0 or hoverEnemyIndex == activeEnemyIndex)
		return;
	if(hoverEnemyIndex >= (int)enemyGroup.size() or enemyGroup[(size_t)hoverEnemyIndex].data.health <= 0)
		return;

	activeEnemyIndex = hoverEnemyIndex;
	SyncActiveEnemyFromGroup();
}

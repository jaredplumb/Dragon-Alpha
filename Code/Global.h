/**
 * @file Global.h
 * @brief Shared Dragon Alpha gameplay constants, globals, and runtime declarations.
 */
#ifndef DRAGON_ALPHA_GLOBAL_H
#define DRAGON_ALPHA_GLOBAL_H

#include "Engine.h"
#include "DesignRuntime.generated.h"

static constexpr int DRAGON_ALPHA_SLOT_COUNT = 3;
static constexpr int DRAGON_ALPHA_NAME_SIZE = 24;
static constexpr int DRAGON_ALPHA_INVENTORY_CAPACITY = 24;
static constexpr int DRAGON_ALPHA_AREA_COUNT = 6;
static constexpr int DRAGON_ALPHA_MAP_WIDTH = 84;
static constexpr int DRAGON_ALPHA_MAP_HEIGHT = 64;
static constexpr int DRAGON_LEGACY_FLAG_CAPACITY = 2048;
// Legacy max level is 9999, but current save schema stores level as uint8_t.
static constexpr int DRAGON_RUNTIME_LEVEL_CAP = 255;

enum DragonAvatarType {
	DRAGON_AVATAR_WARRIOR = 0,
	DRAGON_AVATAR_SORCERER,
	DRAGON_AVATAR_RANGER,
	DRAGON_AVATAR_COUNT,
};

enum DragonAreaType {
	DRAGON_AREA_ELEUSIS = 0,
	DRAGON_AREA_MEADOWS,
	DRAGON_AREA_FORESTS,
	DRAGON_AREA_CAVES,
	DRAGON_AREA_MOUNTAINS,
	DRAGON_AREA_PEAK,
};

enum DragonItemType {
	DRAGON_ITEM_NONE = 0,
	DRAGON_ITEM_IRON_BLADE,
	DRAGON_ITEM_ADEPT_WAND,
	DRAGON_ITEM_HUNTER_BOW,
	DRAGON_ITEM_STEEL_SABER,
	DRAGON_ITEM_WAR_HAMMER,
	DRAGON_ITEM_MYSTIC_TOME,
	DRAGON_ITEM_LEATHER_ARMOR,
	DRAGON_ITEM_TRAVELER_CLOAK,
	DRAGON_ITEM_TOWER_SHIELD,
	DRAGON_ITEM_SCOUT_CHARM,
	DRAGON_ITEM_SEER_CHARM,
	DRAGON_ITEM_DRAGON_TALISMAN,
	DRAGON_ITEM_GUARDIAN_RING,
	DRAGON_ITEM_HEALTH_POTION,
	DRAGON_ITEM_FIRE_SCROLL,
	DRAGON_ITEM_COUNT,
};

enum DragonItemSlot {
	DRAGON_SLOT_NONE = 0,
	DRAGON_SLOT_WEAPON,
	DRAGON_SLOT_ARMOR,
	DRAGON_SLOT_RELIC,
	DRAGON_SLOT_CONSUMABLE,
};

enum DragonBattleType {
	DRAGON_BATTLE_RANDOM = 0,
	DRAGON_BATTLE_GATE,
	DRAGON_BATTLE_BOSS,
};

enum DragonStatusEntryMode {
	DRAGON_STATUS_ENTRY_EQUIPMENT = 0,
	DRAGON_STATUS_ENTRY_MAGIC,
	DRAGON_STATUS_ENTRY_TECH,
};

enum DragonLegacyCommandMenuMode {
	DRAGON_LEGACY_COMMAND_MENU_MAGIC = 0,
	DRAGON_LEGACY_COMMAND_MENU_TECH,
};

struct DragonLegacyCommandAction {
	const char* name;
	int effectElement;
	int power;
	int hitPercent;
	int defenseBoost;
	bool isMagic;
	bool isHeal;
	bool isAll;
};

struct DragonItemInfo {
	uint8_t type;
	uint8_t slot;
	const char* name;
	int16_t attack;
	int16_t defense;
	int16_t magic;
	int16_t speed;
	int16_t health;
	int16_t heal;
	int16_t value;
};

struct DragonInventoryEntry {
	uint8_t type;
	uint8_t count;
};

struct DragonSaveInfo {
	uint32_t version;
	uint8_t selectedSlot;
	uint8_t isSoundOn;
	uint8_t isMusicOn;
	uint8_t isFullScreenOn;
};

struct DragonWorldState {
	uint32_t version;
	uint8_t slotIndex;
	uint8_t reserved[3];
	uint8_t areaMapX[DRAGON_ALPHA_AREA_COUNT];
	uint8_t areaMapY[DRAGON_ALPHA_AREA_COUNT];
	uint16_t areaEventFlags[DRAGON_ALPHA_AREA_COUNT];
	uint8_t legacyFlags[(DRAGON_LEGACY_FLAG_CAPACITY + 7) / 8];
};

struct DragonSaveSlot {
	uint32_t version;
	uint8_t slotIndex;
	uint8_t avatarType;
	uint8_t level;
	uint8_t areaIndex;
	char name[DRAGON_ALPHA_NAME_SIZE];
	uint32_t xp;
	int32_t gold;
	int16_t health;
	int16_t healthMax;
	int16_t attackBase;
	int16_t defenseBase;
	int16_t magicBase;
	int16_t speedBase;
	uint8_t equippedWeapon;
	uint8_t equippedArmor;
	uint8_t equippedRelic;
	uint8_t battlesWon;
	uint8_t battlesLost;
	uint8_t discoveredAreaMask;
	uint8_t inventoryCount;
	DragonInventoryEntry inventory[DRAGON_ALPHA_INVENTORY_CAPACITY];
};

struct DragonAreaInfo {
	const char* name;
	const char* enemyFamily;
	int recommendedLevel;
	int baseEnemyHealth;
	int baseEnemyAttack;
	int rewardGoldMin;
	int rewardGoldMax;
};

struct LegacyAssetSummary {
	int pluginAppCount;
	int mapCount;
	int map2Count;
	int maskCount;
	int battleCount;
	int midiCount;
	int monsterArtCount;
	int itemArtCount;
	bool pluginDirectoryFound;
	bool runtimeResourceSetFound;
	bool helpPageFound;
};

struct DragonBattleRequest {
	bool active;
	uint8_t areaIndex;
	uint8_t battleType;
	uint8_t enemyTier;
	uint8_t noRetreat;
	uint16_t completionFlagBit;
	uint16_t legacyPluginId;
	uint16_t legacyMapId;
	uint16_t legacyGroupId;
	uint8_t legacyEncounterRare;
	uint8_t unlockAreaOnVictory;
	uint8_t reserved;
	int16_t bonusXP;
	int16_t bonusGold;
	char forcedEnemy[32];
};

struct DragonBattleResult {
	bool active;
	uint8_t victory;
	uint8_t retreated;
	uint8_t areaIndex;
	uint8_t battleType;
	uint16_t completionFlagBit;
	uint8_t unlockAreaOnVictory;
	uint8_t reserved;
};

struct DragonBattleRewards {
	int xpGranted;
	int goldGranted;
	int levelsGained;
	int potionDrops;
	int scrollDrops;
	int healthRecovered;
	char milestoneRewards[96];
};

extern DragonSaveInfo gSaveInfo;
extern DragonSaveSlot gSave;
extern DragonWorldState gWorldState;
extern DragonBattleRequest gBattleRequest;
extern DragonBattleResult gBattleResult;
extern int gPendingSlot;
extern int gSelectedArea;
extern LegacyAssetSummary gLegacyAssets;

template <typename T>
inline bool DragonEnsureNodeFactoryRegistered () {
	return T::_factory != nullptr;
}

const DragonAreaInfo* DragonAreaByIndex (int areaIndex);
const DragonItemInfo* DragonItemByType (int itemType);
bool DragonReadSlotPreview (int slot, DragonSaveSlot& outSlot);
bool DragonSlotHasCorruption (int slot);
bool DragonRecoverCorruptedSlot (int slot);

void DragonEnsureSaveInfo (void);
bool DragonIsSoundEnabled (void);
bool DragonIsMusicEnabled (void);
bool DragonIsFullscreenEnabled (void);
void DragonSetSoundEnabled (bool enabled);
void DragonSetMusicEnabled (bool enabled);
void DragonSetFullscreenEnabled (bool enabled);
void DragonApplyAudioPreferences (void);
void DragonPlayMenuMusic (void);
void DragonPlayWorldMapMusic (int areaIndex);
void DragonPlayBattleMusic (int areaIndex);
void DragonStopMusicPlayback (void);
int DragonFindFirstExistingSlot (void);
bool DragonSlotExists (int slot);
bool DragonLoadSlot (int slot);
bool DragonSaveCurrentSlot (void);
bool DragonDeleteSlot (int slot);
bool DragonCreateSlot (int slot, int avatarType, const char* name);
bool DragonLoadWorldState (int slot);
bool DragonSaveWorldState (void);
void DragonResetWorldState (int slot);
bool DragonGetLegacyFlag (uint16_t flagId);
void DragonSetLegacyFlag (uint16_t flagId, bool enabled = true);
void DragonQueueBattle (int areaIndex, int battleType, int enemyTier, bool noRetreat, uint16_t completionFlagBit, int unlockAreaOnVictory, int bonusXP, int bonusGold, const char* forcedEnemy);
void DragonQueueLegacyGroupBattle (int areaIndex, int battleType, uint16_t pluginId, uint16_t mapId, uint16_t groupId, bool rareEncounter, bool noRetreat, uint16_t completionFlagBit, int unlockAreaOnVictory, int bonusXP, int bonusGold, const char* forcedEnemy);
bool DragonConsumeBattleRequest (DragonBattleRequest& outRequest);
void DragonPublishBattleResult (bool victory, const DragonBattleRequest* request, bool retreated = false);
bool DragonConsumeBattleResult (DragonBattleResult& outResult);

DragonBattleRewards DragonAwardBattleRewards (int areaIndex, int bonusXP, int bonusGold, bool allowRandomDrops = true);
EString DragonGrantLevelMilestoneRewards (int oldLevel, int newLevel);
void DragonApplyBattleDefeat (void);
int DragonGetLevelXPRequirement (int level);
int DragonGetAttack (void);
int DragonGetDefense (void);
int DragonGetMagic (void);
int DragonGetSpeed (void);
int DragonGetMaxHealth (void);
void DragonApplyCurrentLevelGrowth (void);
void DragonHealToFull (void);
void DragonSetStatusEntryMode (DragonStatusEntryMode mode);
DragonStatusEntryMode DragonConsumeStatusEntryMode (void);
int DragonBuildLegacyCommandList (DragonLegacyCommandMenuMode mode, DragonLegacyCommandAction* outActions, int maxActions);
int DragonComputeLegacyCommandFieldHeal (const DragonLegacyCommandAction& action);

bool DragonInventoryAdd (int itemType, int count = 1);
bool DragonInventoryUse (int inventoryIndex);
bool DragonEquipFromInventory (int inventoryIndex);
bool DragonUnequipSlot (int slotType);
bool DragonInventoryConsumeType (int itemType, int count = 1);

#if defined(DRAGON_TEST)
bool DragonAutomationCaseIs (const char* caseId);
bool DragonWriteCorruptedSlotForTesting (int slot);
#endif

LegacyAssetSummary BuildLegacyAssetSummary (void);
bool DragonImageResourcesMatch (const char* firstImageId, const char* secondImageId);

#endif // DRAGON_ALPHA_GLOBAL_H

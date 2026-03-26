/**
 * @file Global.cpp
 * @brief Shared Dragon Alpha gameplay globals and cross-scene utility implementation.
 */
#include "Global.h"
#include <algorithm>
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static constexpr uint32_t DRAGON_SAVE_INFO_VERSION = 3;
static constexpr uint32_t DRAGON_SAVE_SLOT_VERSION = 2;
static constexpr uint32_t DRAGON_WORLD_STATE_VERSION = 2;
static constexpr uint32_t DRAGON_SAVE_SLOT_MAGIC = 0x44534C54; // "DSLT"
static constexpr uint32_t DRAGON_WORLD_STATE_MAGIC = 0x44575354; // "DWST"
static constexpr uint16_t DRAGON_SAVE_SLOT_SCHEMA = 1;
static constexpr uint16_t DRAGON_WORLD_STATE_SCHEMA = 1;
static constexpr int DRAGON_LEGACY_STARTING_GOLD = 10;
static constexpr int DRAGON_LEGACY_MAX_DAMAGE = 99999;
static constexpr int DRAGON_LEGACY_ELEMENT_LIGHT = 1;
static constexpr int DRAGON_LEGACY_ELEMENT_WATER = 4;
static constexpr int DRAGON_LEGACY_ELEMENT_EARTH = 5;
static constexpr int DRAGON_LEGACY_ELEMENT_AIR = 6;
static const char* DRAGON_SAVE_INFO_NAME = "DragonAlpha.SaveInfo";
static const char* DRAGON_SAVE_SLOT_NAME = "DragonAlpha.SaveSlot";
static const char* DRAGON_WORLD_STATE_NAME = "DragonAlpha.WorldState";

struct DragonSlotFileData {
	uint32_t magic;
	uint16_t schema;
	uint16_t payloadSize;
	uint32_t payloadChecksum;
	DragonSaveSlot payload;
};

struct DragonWorldStateFileData {
	uint32_t magic;
	uint16_t schema;
	uint16_t payloadSize;
	uint32_t payloadChecksum;
	DragonWorldState payload;
};

static const DragonAreaInfo kAreaList[DRAGON_ALPHA_AREA_COUNT] = {
	{"Eleusis", "Bandits", 1, 58, 10, 10, 24},
	{"The Meadows", "Wolves", 2, 72, 12, 16, 32},
	{"The Forests", "Goblins", 3, 88, 14, 22, 40},
	{"Eleusis Caves", "Shadows", 4, 104, 17, 30, 52},
	{"The Mountains", "Drakes", 5, 122, 20, 38, 64},
	{"The Peak", "Ancients", 6, 146, 24, 50, 80},
};

static const DragonItemInfo kItemList[] = {
	{DRAGON_ITEM_NONE, DRAGON_SLOT_NONE, "None", 0, 0, 0, 0, 0, 0, 0},
	{DRAGON_ITEM_IRON_BLADE, DRAGON_SLOT_WEAPON, "Iron Blade", 5, 0, 0, 0, 0, 0, 45},
	{DRAGON_ITEM_ADEPT_WAND, DRAGON_SLOT_WEAPON, "Adept Wand", 1, 0, 6, 0, 0, 0, 55},
	{DRAGON_ITEM_HUNTER_BOW, DRAGON_SLOT_WEAPON, "Hunter Bow", 3, 0, 0, 2, 0, 0, 52},
	{DRAGON_ITEM_STEEL_SABER, DRAGON_SLOT_WEAPON, "Steel Saber", 8, 0, 0, 1, 0, 0, 78},
	{DRAGON_ITEM_WAR_HAMMER, DRAGON_SLOT_WEAPON, "War Hammer", 10, 1, 0, 0, 0, 0, 92},
	{DRAGON_ITEM_MYSTIC_TOME, DRAGON_SLOT_WEAPON, "Mystic Tome", 1, 0, 9, 1, 0, 0, 94},
	{DRAGON_ITEM_LEATHER_ARMOR, DRAGON_SLOT_ARMOR, "Leather Armor", 0, 4, 0, 0, 10, 0, 42},
	{DRAGON_ITEM_TRAVELER_CLOAK, DRAGON_SLOT_ARMOR, "Traveler Cloak", 0, 2, 2, 1, 8, 0, 50},
	{DRAGON_ITEM_TOWER_SHIELD, DRAGON_SLOT_ARMOR, "Tower Shield", 0, 8, 0, 0, 14, 0, 96},
	{DRAGON_ITEM_SCOUT_CHARM, DRAGON_SLOT_RELIC, "Scout Charm", 1, 0, 0, 3, 0, 0, 70},
	{DRAGON_ITEM_SEER_CHARM, DRAGON_SLOT_RELIC, "Seer Charm", 0, 0, 4, 0, 0, 0, 75},
	{DRAGON_ITEM_DRAGON_TALISMAN, DRAGON_SLOT_RELIC, "Dragon Talisman", 2, 1, 2, 2, 0, 0, 118},
	{DRAGON_ITEM_GUARDIAN_RING, DRAGON_SLOT_RELIC, "Guardian Ring", 0, 3, 2, 0, 10, 0, 110},
	{DRAGON_ITEM_HEALTH_POTION, DRAGON_SLOT_CONSUMABLE, "Health Potion", 0, 0, 0, 0, 0, 48, 18},
	{DRAGON_ITEM_FIRE_SCROLL, DRAGON_SLOT_CONSUMABLE, "Fire Scroll", 0, 0, 0, 0, 0, 0, 22},
};

static const char* DRAGON_MENU_MUSIC_TRACK = "Music/theme.mid";
static const char* kDragonMapMusicTrackByArea[DRAGON_ALPHA_AREA_COUNT] = {
	"Music/Core__map.mid",
	"Music/Meadows__map.mid",
	"Music/Forest__map.mid",
	"Music/Caves__map.mid",
	"Music/Mountain__map.mid",
	"Music/Mountain__map.mid",
};
static const char* kDragonBattleMusicTrackByArea[DRAGON_ALPHA_AREA_COUNT] = {
	"Music/Core__battle.mid",
	"Music/Core__battle.mid",
	"Music/Forest__battle.mid",
	"Music/Caves__battle.mid",
	"Music/Mountain__battle.mid",
	"Music/Mountain__battle.mid",
};

DragonSaveInfo gSaveInfo = {};
DragonSaveSlot gSave = {};
DragonWorldState gWorldState = {};
DragonBattleRequest gBattleRequest = {};
DragonBattleResult gBattleResult = {};
int gPendingSlot = 0;
int gSelectedArea = 0;
LegacyAssetSummary gLegacyAssets = {};
static uint8_t gCorruptSlotMask = 0;
static uint8_t gCorruptWorldStateMask = 0;
static DragonStatusEntryMode gStatusEntryMode = DRAGON_STATUS_ENTRY_EQUIPMENT;

static uint32_t ComputePayloadChecksum (const void* bytes, size_t size) {
	const uint8_t* data = (const uint8_t*)bytes;
	uint32_t hash = 2166136261u;
	for(size_t i = 0; i < size; i++) {
		hash ^= (uint32_t)data[i];
		hash *= 16777619u;
	}
	return hash;
}

static bool IsValidSlotPayload (const DragonSaveSlot& payload, int slot) {
	bool hasNameTerminator = false;
	for(int i = 0; i < DRAGON_ALPHA_NAME_SIZE; i++) {
		const unsigned char c = (unsigned char)payload.name[i];
		if(c == '\0') {
			hasNameTerminator = true;
			break;
		}
		// Keep loaded names free of control bytes to prevent UI/layout issues.
		if(c < 32 or c == 127)
			return false;
	}
	if(hasNameTerminator == false)
		return false;

	if(payload.version != DRAGON_SAVE_SLOT_VERSION)
		return false;
	if(payload.slotIndex != slot)
		return false;
	if(payload.level < 1 or payload.level > DRAGON_RUNTIME_LEVEL_CAP)
		return false;
	if(payload.avatarType >= DRAGON_AVATAR_COUNT)
		return false;
	if(payload.areaIndex >= DRAGON_ALPHA_AREA_COUNT)
		return false;
	if(payload.inventoryCount > DRAGON_ALPHA_INVENTORY_CAPACITY)
		return false;
	if(payload.equippedWeapon >= DRAGON_ITEM_COUNT or payload.equippedArmor >= DRAGON_ITEM_COUNT or payload.equippedRelic >= DRAGON_ITEM_COUNT)
		return false;
	for(int i = 0; i < payload.inventoryCount; i++) {
		if(payload.inventory[i].type >= DRAGON_ITEM_COUNT)
			return false;
	}
	return true;
}

static bool IsValidWorldStatePayload (const DragonWorldState& payload, int slot) {
	if(payload.version != DRAGON_WORLD_STATE_VERSION)
		return false;
	if(payload.slotIndex != slot)
		return false;
	for(int i = 0; i < DRAGON_ALPHA_AREA_COUNT; i++) {
		if(payload.areaMapX[i] >= DRAGON_ALPHA_MAP_WIDTH or payload.areaMapY[i] >= DRAGON_ALPHA_MAP_HEIGHT)
			return false;
	}
	return true;
}

static void SetCorruptBit (uint8_t& mask, int slot, bool isCorrupt) {
	if(slot < 1 or slot > DRAGON_ALPHA_SLOT_COUNT)
		return;
	const uint8_t bit = (uint8_t)(1u << (slot - 1));
	if(isCorrupt)
		mask |= bit;
	else
		mask &= (uint8_t)~bit;
}

static bool GetCorruptBit (uint8_t mask, int slot) {
	if(slot < 1 or slot > DRAGON_ALPHA_SLOT_COUNT)
		return false;
	const uint8_t bit = (uint8_t)(1u << (slot - 1));
	return (mask & bit) != 0;
}

static bool FileExists (const char* path) {
	FILE* file = fopen(path, "rb");
	if(file == NULL)
		return false;
	fclose(file);
	return true;
}

static bool IsDirectory (const char* path) {
	struct stat st;
	if(stat(path, &st) != 0)
		return false;
	return S_ISDIR(st.st_mode) != 0;
}

static bool EndsWithIgnoreCase (const char* value, const char* suffix) {
	size_t valueLen = strlen(value);
	size_t suffixLen = strlen(suffix);
	if(valueLen < suffixLen)
		return false;
	value += (valueLen - suffixLen);
	for(size_t i = 0; i < suffixLen; i++) {
		if(tolower((unsigned char)value[i]) != tolower((unsigned char)suffix[i]))
			return false;
	}
	return true;
}

static int CountExtensionsRecursive (const char* path, const char* extension) {
	DIR* dir = opendir(path);
	if(dir == NULL)
		return 0;

	int total = 0;
	struct dirent* entry;
	while((entry = readdir(dir)) != NULL) {
		if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;

		char child[PATH_MAX];
		snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);

		struct stat st;
		if(stat(child, &st) != 0)
			continue;

		if(S_ISDIR(st.st_mode))
			total += CountExtensionsRecursive(child, extension);
		else if(S_ISREG(st.st_mode) and EndsWithIgnoreCase(entry->d_name, extension))
			total++;
	}

	closedir(dir);
	return total;
}

static int CountTopLevelPluginApps (const char* pluginsPath) {
	DIR* dir = opendir(pluginsPath);
	if(dir == NULL)
		return 0;

	int total = 0;
	struct dirent* entry;
	while((entry = readdir(dir)) != NULL) {
		if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;

		char child[PATH_MAX];
		snprintf(child, sizeof(child), "%s/%s", pluginsPath, entry->d_name);
		if(not IsDirectory(child))
			continue;

		if(EndsWithIgnoreCase(entry->d_name, ".app")) {
			total++;
			continue;
		}

		char dataPath[PATH_MAX];
		snprintf(dataPath, sizeof(dataPath), "%s/Data", child);
		if(FileExists(dataPath))
			total++;
	}

	closedir(dir);
	return total;
}

static const char* FindExistingDirectoryPath (const char** paths, int count) {
	for(int i = 0; i < count; i++)
		if(IsDirectory(paths[i]))
			return paths[i];
	return NULL;
}

static bool FileExistsAtAnyPath (const char** paths, int count) {
	for(int i = 0; i < count; i++)
		if(FileExists(paths[i]))
			return true;
	return false;
}

static bool DirectoryExistsAtAnyPath (const char** paths, int count) {
	for(int i = 0; i < count; i++)
		if(IsDirectory(paths[i]))
			return true;
	return false;
}

static EString BuildSlotName (int slot) {
	return EString().Format("%s.%d", DRAGON_SAVE_SLOT_NAME, slot);
}

static EString BuildWorldStateName (int slot) {
	return EString().Format("%s.%d", DRAGON_WORLD_STATE_NAME, slot);
}

static bool ReadSlotPayload (int slot, DragonSaveSlot& outPayload, bool& outCorrupt) {
	outCorrupt = false;
	memset(&outPayload, 0, sizeof(outPayload));

	DragonSlotFileData stored = {};
	if(ESystem::SaveRead(BuildSlotName(slot), &stored, sizeof(stored))) {
		const bool hasValidHeader = stored.magic == DRAGON_SAVE_SLOT_MAGIC and
			stored.schema == DRAGON_SAVE_SLOT_SCHEMA and
			stored.payloadSize == sizeof(DragonSaveSlot);
		if(hasValidHeader == false) {
			outCorrupt = true;
			return false;
		}

		const uint32_t checksum = ComputePayloadChecksum(&stored.payload, sizeof(stored.payload));
		if(stored.payloadChecksum != checksum) {
			outCorrupt = true;
			return false;
		}

		if(IsValidSlotPayload(stored.payload, slot) == false) {
			outCorrupt = true;
			return false;
		}

		outPayload = stored.payload;
		return true;
	}

	DragonSaveSlot legacy = {};
	if(ESystem::SaveRead(BuildSlotName(slot), &legacy, sizeof(legacy))) {
		if(IsValidSlotPayload(legacy, slot) == false) {
			outCorrupt = true;
			return false;
		}
		outPayload = legacy;
		return true;
	}

	return false;
}

static bool WriteSlotPayload (const DragonSaveSlot& payload) {
	if(payload.slotIndex < 1 or payload.slotIndex > DRAGON_ALPHA_SLOT_COUNT)
		return false;
	DragonSlotFileData stored = {};
	stored.magic = DRAGON_SAVE_SLOT_MAGIC;
	stored.schema = DRAGON_SAVE_SLOT_SCHEMA;
	stored.payloadSize = sizeof(DragonSaveSlot);
	stored.payload = payload;
	stored.payloadChecksum = ComputePayloadChecksum(&stored.payload, sizeof(stored.payload));
	return ESystem::SaveWrite(BuildSlotName(payload.slotIndex), &stored, sizeof(stored));
}

static bool ReadWorldStatePayload (int slot, DragonWorldState& outPayload, bool& outCorrupt) {
	outCorrupt = false;
	memset(&outPayload, 0, sizeof(outPayload));

	DragonWorldStateFileData stored = {};
	if(ESystem::SaveRead(BuildWorldStateName(slot), &stored, sizeof(stored))) {
		const bool hasValidHeader = stored.magic == DRAGON_WORLD_STATE_MAGIC and
			stored.schema == DRAGON_WORLD_STATE_SCHEMA and
			stored.payloadSize == sizeof(DragonWorldState);
		if(hasValidHeader == false) {
			outCorrupt = true;
			return false;
		}

		const uint32_t checksum = ComputePayloadChecksum(&stored.payload, sizeof(stored.payload));
		if(stored.payloadChecksum != checksum) {
			outCorrupt = true;
			return false;
		}

		if(IsValidWorldStatePayload(stored.payload, slot) == false) {
			outCorrupt = true;
			return false;
		}

		outPayload = stored.payload;
		return true;
	}

	DragonWorldState legacy = {};
	if(ESystem::SaveRead(BuildWorldStateName(slot), &legacy, sizeof(legacy))) {
		if(IsValidWorldStatePayload(legacy, slot) == false) {
			outCorrupt = true;
			return false;
		}
		outPayload = legacy;
		return true;
	}

	return false;
}

static bool WriteWorldStatePayload (const DragonWorldState& payload) {
	if(payload.slotIndex < 1 or payload.slotIndex > DRAGON_ALPHA_SLOT_COUNT)
		return false;
	DragonWorldStateFileData stored = {};
	stored.magic = DRAGON_WORLD_STATE_MAGIC;
	stored.schema = DRAGON_WORLD_STATE_SCHEMA;
	stored.payloadSize = sizeof(DragonWorldState);
	stored.payload = payload;
	stored.payloadChecksum = ComputePayloadChecksum(&stored.payload, sizeof(stored.payload));
	return ESystem::SaveWrite(BuildWorldStateName(payload.slotIndex), &stored, sizeof(stored));
}

static void NormalizeSaveInfoPreferences (DragonSaveInfo& info) {
	info.isSoundOn = info.isSoundOn ? 1 : 0;
	info.isMusicOn = info.isMusicOn ? 1 : 0;
	info.isFullScreenOn = info.isFullScreenOn ? 1 : 0;
}

static void SetDefaultSaveInfo (DragonSaveInfo& info) {
	memset(&info, 0, sizeof(info));
	info.version = DRAGON_SAVE_INFO_VERSION;
	info.selectedSlot = (uint8_t)DragonFindFirstExistingSlot();
	info.isSoundOn = 1;
	info.isMusicOn = 1;
	info.isFullScreenOn = 0;
}

static const char* DragonMapMusicTrackForArea (int areaIndex) {
	if(areaIndex < 0 || areaIndex >= DRAGON_ALPHA_AREA_COUNT)
		return kDragonMapMusicTrackByArea[DRAGON_AREA_ELEUSIS];
	return kDragonMapMusicTrackByArea[areaIndex];
}

static const char* DragonBattleMusicTrackForArea (int areaIndex) {
	if(areaIndex < 0 || areaIndex >= DRAGON_ALPHA_AREA_COUNT)
		return kDragonBattleMusicTrackByArea[DRAGON_AREA_ELEUSIS];
	return kDragonBattleMusicTrackByArea[areaIndex];
}

static void ApplyMusicEnabledPreference () {
	const bool musicEnabled = (DESIGN_ENABLE_AUDIO != 0) and gSaveInfo.isMusicOn != 0;
	ESound::SetMusicEnabled(musicEnabled);
}

static bool SaveSaveInfo () {
	gSaveInfo.version = DRAGON_SAVE_INFO_VERSION;
	NormalizeSaveInfoPreferences(gSaveInfo);
	return ESystem::SaveWrite(DRAGON_SAVE_INFO_NAME, &gSaveInfo, sizeof(gSaveInfo));
}

static void ClearBattleRequest () {
	memset(&gBattleRequest, 0, sizeof(gBattleRequest));
	gBattleRequest.active = false;
}

static void ClearBattleResult () {
	memset(&gBattleResult, 0, sizeof(gBattleResult));
	gBattleResult.active = false;
}

static void ClearSaveData (DragonSaveSlot& slot) {
	memset(&slot, 0, sizeof(DragonSaveSlot));
	slot.version = DRAGON_SAVE_SLOT_VERSION;
}

static void ResetActiveRuntimeSlotState () {
	memset(&gSave, 0, sizeof(gSave));
	memset(&gWorldState, 0, sizeof(gWorldState));
	gSelectedArea = 0;
	gPendingSlot = 0;
}

static void SetDefaultWorldState (DragonWorldState& state, int slot) {
	memset(&state, 0, sizeof(DragonWorldState));
	state.version = DRAGON_WORLD_STATE_VERSION;
	state.slotIndex = (uint8_t)slot;
	static const uint8_t kDefaultMapX[DRAGON_ALPHA_AREA_COUNT] = {25, 22, 20, 18, 16, 14};
	static const uint8_t kDefaultMapY[DRAGON_ALPHA_AREA_COUNT] = {23, 23, 24, 24, 22, 21};
	for(int i = 0; i < DRAGON_ALPHA_AREA_COUNT; i++) {
		state.areaMapX[i] = kDefaultMapX[i];
		state.areaMapY[i] = kDefaultMapY[i];
		state.areaEventFlags[i] = 0;
	}
}

static bool IsValidWorldState (const DragonWorldState& state, int slot) {
	if(state.version != DRAGON_WORLD_STATE_VERSION)
		return false;
	if(state.slotIndex != slot)
		return false;
	return true;
}

static bool GetLegacyFlagBit (const DragonWorldState& state, uint16_t flagId) {
	if(flagId == 0 || flagId >= DRAGON_LEGACY_FLAG_CAPACITY)
		return false;
	const int bitIndex = (int)flagId;
	const int byteIndex = bitIndex >> 3;
	const uint8_t mask = (uint8_t)(1u << (bitIndex & 7));
	return (state.legacyFlags[byteIndex] & mask) != 0;
}

static void SetLegacyFlagBit (DragonWorldState& state, uint16_t flagId, bool enabled) {
	if(flagId == 0 || flagId >= DRAGON_LEGACY_FLAG_CAPACITY)
		return;
	const int bitIndex = (int)flagId;
	const int byteIndex = bitIndex >> 3;
	const uint8_t mask = (uint8_t)(1u << (bitIndex & 7));
	if(enabled)
		state.legacyFlags[byteIndex] |= mask;
	else
		state.legacyFlags[byteIndex] &= (uint8_t)~mask;
}

static void CompactInventoryInternal (DragonSaveSlot& slot) {
	for(int i = 0; i < slot.inventoryCount; i++) {
		if(slot.inventory[i].count != 0)
			continue;
		for(int j = i + 1; j < slot.inventoryCount; j++)
			slot.inventory[j - 1] = slot.inventory[j];
		slot.inventoryCount--;
		i--;
	}

	if(slot.inventoryCount > 1) {
		auto slotPriority = [] (uint8_t itemType) {
			const DragonItemInfo* info = DragonItemByType(itemType);
			if(info == NULL)
				return 99;
			switch(info->slot) {
				case DRAGON_SLOT_WEAPON: return 0;
				case DRAGON_SLOT_ARMOR: return 1;
				case DRAGON_SLOT_RELIC: return 2;
				case DRAGON_SLOT_CONSUMABLE: return 3;
				default: return 4;
			}
		};
		std::sort(slot.inventory, slot.inventory + slot.inventoryCount, [&slotPriority] (const DragonInventoryEntry& a, const DragonInventoryEntry& b) {
			int aPriority = slotPriority(a.type);
			int bPriority = slotPriority(b.type);
			if(aPriority != bPriority)
				return aPriority < bPriority;
			if(a.type != b.type)
				return a.type < b.type;
			return a.count > b.count;
		});
	}
}

static bool SaveOwnsItemType (const DragonSaveSlot& slot, int itemType) {
	if(itemType <= DRAGON_ITEM_NONE or itemType >= DRAGON_ITEM_COUNT)
		return false;
	if(slot.equippedWeapon == itemType or slot.equippedArmor == itemType or slot.equippedRelic == itemType)
		return true;
	for(int i = 0; i < slot.inventoryCount; i++) {
		if(slot.inventory[i].type == itemType and slot.inventory[i].count > 0)
			return true;
	}
	return false;
}

static bool InventoryAddToSlot (DragonSaveSlot& slot, int itemType, int count) {
	if(itemType <= DRAGON_ITEM_NONE or itemType >= DRAGON_ITEM_COUNT or count <= 0)
		return false;

	for(int i = 0; i < slot.inventoryCount; i++) {
		if(slot.inventory[i].type != itemType)
			continue;
		int newCount = (int)slot.inventory[i].count + count;
		slot.inventory[i].count = (uint8_t)std::min(255, newCount);
		return true;
	}

	if(slot.inventoryCount >= DRAGON_ALPHA_INVENTORY_CAPACITY)
		return false;

	slot.inventory[slot.inventoryCount].type = (uint8_t)itemType;
	slot.inventory[slot.inventoryCount].count = (uint8_t)std::min(255, count);
	slot.inventoryCount++;
	return true;
}

static int GetEquipBonus (int itemType, int16_t DragonItemInfo::* field) {
	const DragonItemInfo* info = DragonItemByType(itemType);
	if(info == NULL)
		return 0;
	return info->*field;
}

static void ClampVitals () {
	if(gSave.healthMax < 1)
		gSave.healthMax = 1;
	if(gSave.health > DragonGetMaxHealth())
		gSave.health = (int16_t)DragonGetMaxHealth();
	if(gSave.health < 0)
		gSave.health = 0;
	if(gSave.areaIndex >= DRAGON_ALPHA_AREA_COUNT)
		gSave.areaIndex = 0;
	if(gSave.level < 1)
		gSave.level = 1;
	if(gSave.level > DRAGON_RUNTIME_LEVEL_CAP)
		gSave.level = DRAGON_RUNTIME_LEVEL_CAP;
	if(gSave.discoveredAreaMask == 0)
		gSave.discoveredAreaMask = 1;
}

static void SeedAvatarStats (DragonSaveSlot& slot) {
	switch(slot.avatarType) {
		default:
		case DRAGON_AVATAR_WARRIOR:
			slot.attackBase = 12;
			slot.defenseBase = 9;
			slot.magicBase = 4;
			slot.speedBase = 6;
			slot.healthMax = 112;
			slot.equippedWeapon = DRAGON_ITEM_IRON_BLADE;
			slot.equippedArmor = DRAGON_ITEM_LEATHER_ARMOR;
			slot.equippedRelic = DRAGON_ITEM_NONE;
			break;
		case DRAGON_AVATAR_SORCERER:
			slot.attackBase = 6;
			slot.defenseBase = 6;
			slot.magicBase = 14;
			slot.speedBase = 8;
			slot.healthMax = 88;
			slot.equippedWeapon = DRAGON_ITEM_ADEPT_WAND;
			slot.equippedArmor = DRAGON_ITEM_TRAVELER_CLOAK;
			slot.equippedRelic = DRAGON_ITEM_SEER_CHARM;
			break;
		case DRAGON_AVATAR_RANGER:
			slot.attackBase = 9;
			slot.defenseBase = 8;
			slot.magicBase = 6;
			slot.speedBase = 12;
			slot.healthMax = 98;
			slot.equippedWeapon = DRAGON_ITEM_HUNTER_BOW;
			slot.equippedArmor = DRAGON_ITEM_LEATHER_ARMOR;
			slot.equippedRelic = DRAGON_ITEM_SCOUT_CHARM;
			break;
	}
}

static int MilestoneItemForAvatarLevel (int avatarType, int level) {
	switch(avatarType) {
		case DRAGON_AVATAR_WARRIOR:
			switch(level) {
				case 5: return DRAGON_ITEM_STEEL_SABER;
				case 10: return DRAGON_ITEM_TOWER_SHIELD;
				case 20: return DRAGON_ITEM_WAR_HAMMER;
				case 30: return DRAGON_ITEM_GUARDIAN_RING;
				case 45: return DRAGON_ITEM_DRAGON_TALISMAN;
			}
			break;
		case DRAGON_AVATAR_SORCERER:
			switch(level) {
				case 5: return DRAGON_ITEM_TRAVELER_CLOAK;
				case 10: return DRAGON_ITEM_SEER_CHARM;
				case 20: return DRAGON_ITEM_MYSTIC_TOME;
				case 30: return DRAGON_ITEM_GUARDIAN_RING;
				case 45: return DRAGON_ITEM_DRAGON_TALISMAN;
			}
			break;
		case DRAGON_AVATAR_RANGER:
			switch(level) {
				case 5: return DRAGON_ITEM_STEEL_SABER;
				case 10: return DRAGON_ITEM_TRAVELER_CLOAK;
				case 20: return DRAGON_ITEM_GUARDIAN_RING;
				case 30: return DRAGON_ITEM_WAR_HAMMER;
				case 45: return DRAGON_ITEM_DRAGON_TALISMAN;
			}
			break;
	}
	return DRAGON_ITEM_NONE;
}

static int ItemScoreForAvatar (const DragonItemInfo* info, int avatarType) {
	if(info == NULL)
		return -100000;
	int attackWeight = 3;
	int defenseWeight = 2;
	int magicWeight = 2;
	int speedWeight = 2;
	int healthWeight = 1;
	switch(avatarType) {
		case DRAGON_AVATAR_WARRIOR:
			attackWeight = 4;
			defenseWeight = 4;
			magicWeight = 1;
			speedWeight = 2;
			healthWeight = 2;
			break;
		case DRAGON_AVATAR_SORCERER:
			attackWeight = 1;
			defenseWeight = 2;
			magicWeight = 5;
			speedWeight = 2;
			healthWeight = 1;
			break;
		case DRAGON_AVATAR_RANGER:
			attackWeight = 3;
			defenseWeight = 2;
			magicWeight = 1;
			speedWeight = 5;
			healthWeight = 1;
			break;
		default:
			break;
	}
	return
		(int)info->attack * attackWeight +
		(int)info->defense * defenseWeight +
		(int)info->magic * magicWeight +
		(int)info->speed * speedWeight +
		(int)info->health * healthWeight;
}

static bool TryAutoEquipMilestoneItem (int itemType) {
	const DragonItemInfo* info = DragonItemByType(itemType);
	if(info == NULL or info->slot == DRAGON_SLOT_NONE or info->slot == DRAGON_SLOT_CONSUMABLE)
		return false;

	int equippedType = DRAGON_ITEM_NONE;
	switch(info->slot) {
		case DRAGON_SLOT_WEAPON:
			equippedType = gSave.equippedWeapon;
			break;
		case DRAGON_SLOT_ARMOR:
			equippedType = gSave.equippedArmor;
			break;
		case DRAGON_SLOT_RELIC:
			equippedType = gSave.equippedRelic;
			break;
		default:
			return false;
	}
	if(equippedType == itemType)
		return false;

	const DragonItemInfo* equippedInfo = DragonItemByType(equippedType);
	int newScore = ItemScoreForAvatar(info, gSave.avatarType);
	int currentScore = equippedType == DRAGON_ITEM_NONE ? -100000 : ItemScoreForAvatar(equippedInfo, gSave.avatarType);
	if(equippedType != DRAGON_ITEM_NONE and newScore <= currentScore)
		return false;

	if(equippedType != DRAGON_ITEM_NONE and gSave.inventoryCount >= DRAGON_ALPHA_INVENTORY_CAPACITY) {
		bool canReturnPrevious = false;
		for(int i = 0; i < gSave.inventoryCount; i++) {
			if(gSave.inventory[i].type == equippedType and gSave.inventory[i].count > 0) {
				canReturnPrevious = true;
				break;
			}
		}
		if(canReturnPrevious == false)
			return false;
	}

	int inventoryIndex = -1;
	for(int i = 0; i < gSave.inventoryCount; i++) {
		if(gSave.inventory[i].type != itemType or gSave.inventory[i].count == 0)
			continue;
		inventoryIndex = i;
		break;
	}
	if(inventoryIndex < 0)
		return false;
	return DragonEquipFromInventory(inventoryIndex);
}

const DragonAreaInfo* DragonAreaByIndex (int areaIndex) {
	if(areaIndex < 0 or areaIndex >= DRAGON_ALPHA_AREA_COUNT)
		return NULL;
	return &kAreaList[areaIndex];
}

const DragonItemInfo* DragonItemByType (int itemType) {
	if(itemType < 0 or itemType >= DRAGON_ITEM_COUNT)
		return NULL;
	return &kItemList[itemType];
}

bool DragonReadSlotPreview (int slot, DragonSaveSlot& outSlot) {
	memset(&outSlot, 0, sizeof(outSlot));
	if(slot < 1 or slot > DRAGON_ALPHA_SLOT_COUNT)
		return false;
	bool isCorrupt = false;
	const bool loaded = ReadSlotPayload(slot, outSlot, isCorrupt);
	SetCorruptBit(gCorruptSlotMask, slot, isCorrupt);
	return loaded;
}

int DragonFindFirstExistingSlot (void) {
	for(int slot = 1; slot <= DRAGON_ALPHA_SLOT_COUNT; slot++) {
		if(DragonSlotExists(slot))
			return slot;
	}
	return 0;
}

void DragonEnsureSaveInfo (void) {
	DragonSaveInfo loaded = {};
	if(ESystem::SaveRead(DRAGON_SAVE_INFO_NAME, &loaded, sizeof(loaded))) {
		if(loaded.version == DRAGON_SAVE_INFO_VERSION and loaded.selectedSlot <= DRAGON_ALPHA_SLOT_COUNT) {
			gSaveInfo = loaded;
			NormalizeSaveInfoPreferences(gSaveInfo);
			bool changed = false;
			if(gSaveInfo.selectedSlot != 0 and DragonSlotExists(gSaveInfo.selectedSlot) == false) {
				gSaveInfo.selectedSlot = (uint8_t)DragonFindFirstExistingSlot();
				changed = true;
			}
			if(changed)
				SaveSaveInfo();
			return;
		}

		// Migrate v2 save info to v3 preference-backed settings.
		if(loaded.version == 2 and loaded.selectedSlot <= DRAGON_ALPHA_SLOT_COUNT) {
			SetDefaultSaveInfo(gSaveInfo);
			gSaveInfo.selectedSlot = loaded.selectedSlot;
			if(gSaveInfo.selectedSlot != 0 and DragonSlotExists(gSaveInfo.selectedSlot) == false)
				gSaveInfo.selectedSlot = (uint8_t)DragonFindFirstExistingSlot();
			SaveSaveInfo();
			return;
		}
	}

	SetDefaultSaveInfo(gSaveInfo);
	SaveSaveInfo();
}

bool DragonIsSoundEnabled (void) {
	if(gSaveInfo.version != DRAGON_SAVE_INFO_VERSION)
		DragonEnsureSaveInfo();
	return gSaveInfo.isSoundOn != 0;
}

bool DragonIsMusicEnabled (void) {
	if(gSaveInfo.version != DRAGON_SAVE_INFO_VERSION)
		DragonEnsureSaveInfo();
	return gSaveInfo.isMusicOn != 0;
}

bool DragonIsFullscreenEnabled (void) {
	if(gSaveInfo.version != DRAGON_SAVE_INFO_VERSION)
		DragonEnsureSaveInfo();
	return gSaveInfo.isFullScreenOn != 0;
}

void DragonApplyAudioPreferences (void) {
	if(gSaveInfo.version != DRAGON_SAVE_INFO_VERSION)
		DragonEnsureSaveInfo();
	const bool audioEnabled = (DESIGN_ENABLE_AUDIO != 0) and gSaveInfo.isSoundOn != 0;
	ESound::SetMasterVolume(audioEnabled ? 1.0f : 0.0f);
	ApplyMusicEnabledPreference();
}

void DragonPlayMenuMusic (void) {
	DragonApplyAudioPreferences();
	ESound::PlayMusicTrack(DRAGON_MENU_MUSIC_TRACK);
}

void DragonPlayWorldMapMusic (int areaIndex) {
	DragonApplyAudioPreferences();
	ESound::PlayMusicTrack(DragonMapMusicTrackForArea(areaIndex));
}

void DragonPlayBattleMusic (int areaIndex) {
	DragonApplyAudioPreferences();
	ESound::PlayMusicTrack(DragonBattleMusicTrackForArea(areaIndex));
}

void DragonStopMusicPlayback (void) {
	ESound::StopMusic();
}

void DragonSetSoundEnabled (bool enabled) {
	if(gSaveInfo.version != DRAGON_SAVE_INFO_VERSION)
		DragonEnsureSaveInfo();
	gSaveInfo.isSoundOn = enabled ? 1 : 0;
	SaveSaveInfo();
	DragonApplyAudioPreferences();
}

void DragonSetMusicEnabled (bool enabled) {
	if(gSaveInfo.version != DRAGON_SAVE_INFO_VERSION)
		DragonEnsureSaveInfo();
	gSaveInfo.isMusicOn = enabled ? 1 : 0;
	SaveSaveInfo();
	ApplyMusicEnabledPreference();
}

void DragonSetFullscreenEnabled (bool enabled) {
	if(gSaveInfo.version != DRAGON_SAVE_INFO_VERSION)
		DragonEnsureSaveInfo();
	gSaveInfo.isFullScreenOn = enabled ? 1 : 0;
	SaveSaveInfo();
	ESystem::SetFullscreenEnabled(enabled);
}

void DragonResetWorldState (int slot) {
	if(slot < 1 or slot > DRAGON_ALPHA_SLOT_COUNT)
		return;

	SetDefaultWorldState(gWorldState, slot);
	WriteWorldStatePayload(gWorldState);
	SetCorruptBit(gCorruptWorldStateMask, slot, false);
}

bool DragonLoadWorldState (int slot) {
	if(slot < 1 or slot > DRAGON_ALPHA_SLOT_COUNT)
		return false;

	DragonWorldState loaded = {};
	bool isCorrupt = false;
	if(ReadWorldStatePayload(slot, loaded, isCorrupt) and IsValidWorldState(loaded, slot)) {
		gWorldState = loaded;
		SetCorruptBit(gCorruptWorldStateMask, slot, false);
		return true;
	}
	SetCorruptBit(gCorruptWorldStateMask, slot, isCorrupt);

	DragonResetWorldState(slot);
	return true;
}

bool DragonSaveWorldState (void) {
	if(gSave.slotIndex < 1 or gSave.slotIndex > DRAGON_ALPHA_SLOT_COUNT)
		return false;

	gWorldState.version = DRAGON_WORLD_STATE_VERSION;
	gWorldState.slotIndex = gSave.slotIndex;
	SetCorruptBit(gCorruptWorldStateMask, gSave.slotIndex, false);
	return WriteWorldStatePayload(gWorldState);
}

bool DragonGetLegacyFlag (uint16_t flagId) {
	return GetLegacyFlagBit(gWorldState, flagId);
}

void DragonSetLegacyFlag (uint16_t flagId, bool enabled) {
	SetLegacyFlagBit(gWorldState, flagId, enabled);
}

bool DragonSlotExists (int slot) {
	DragonSaveSlot preview = {};
	return DragonReadSlotPreview(slot, preview);
}

bool DragonSlotHasCorruption (int slot) {
	if(slot < 1 or slot > DRAGON_ALPHA_SLOT_COUNT)
		return false;
	return GetCorruptBit(gCorruptSlotMask, slot) or GetCorruptBit(gCorruptWorldStateMask, slot);
}

bool DragonRecoverCorruptedSlot (int slot) {
	if(slot < 1 or slot > DRAGON_ALPHA_SLOT_COUNT)
		return false;
	if(DragonSlotHasCorruption(slot) == false)
		return false;

	ESystem::SaveDelete(BuildSlotName(slot));
	ESystem::SaveDelete(BuildWorldStateName(slot));
	SetCorruptBit(gCorruptSlotMask, slot, false);
	SetCorruptBit(gCorruptWorldStateMask, slot, false);

	DragonEnsureSaveInfo();
	if(gSaveInfo.selectedSlot == slot) {
		gSaveInfo.selectedSlot = (uint8_t)DragonFindFirstExistingSlot();
		SaveSaveInfo();
	}
	if(gSave.slotIndex == slot) {
		ResetActiveRuntimeSlotState();
	}
	ClearBattleRequest();
	ClearBattleResult();
	return true;
}

bool DragonLoadSlot (int slot) {
	DragonEnsureSaveInfo();
	if(slot < 1 or slot > DRAGON_ALPHA_SLOT_COUNT)
		return false;

	DragonSaveSlot loaded = {};
	if(DragonReadSlotPreview(slot, loaded) == false)
		return false;

	gSave = loaded;
	ClampVitals();
	DragonLoadWorldState(slot);
	gSelectedArea = std::min((int)gSave.areaIndex, DRAGON_ALPHA_AREA_COUNT - 1);
	gSaveInfo.selectedSlot = (uint8_t)slot;
	SaveSaveInfo();
	ClearBattleRequest();
	ClearBattleResult();
	return true;
}

bool DragonSaveCurrentSlot (void) {
	if(gSave.slotIndex < 1 or gSave.slotIndex > DRAGON_ALPHA_SLOT_COUNT)
		return false;

	ClampVitals();
	bool wroteSlot = WriteSlotPayload(gSave);
	if(wroteSlot == false)
		return false;
	SetCorruptBit(gCorruptSlotMask, gSave.slotIndex, false);
	if(DragonSaveWorldState() == false)
		return false;

	DragonEnsureSaveInfo();
	gSaveInfo.selectedSlot = gSave.slotIndex;
	return SaveSaveInfo();
}

bool DragonDeleteSlot (int slot) {
	if(slot < 1 or slot > DRAGON_ALPHA_SLOT_COUNT)
		return false;

	bool deleted = ESystem::SaveDelete(BuildSlotName(slot));
	bool deletedWorld = ESystem::SaveDelete(BuildWorldStateName(slot));
	SetCorruptBit(gCorruptSlotMask, slot, false);
	SetCorruptBit(gCorruptWorldStateMask, slot, false);
	DragonEnsureSaveInfo();
	if(gSaveInfo.selectedSlot == slot) {
		gSaveInfo.selectedSlot = (uint8_t)DragonFindFirstExistingSlot();
		SaveSaveInfo();
	}
	if(gSave.slotIndex == slot) {
		ResetActiveRuntimeSlotState();
	}
	ClearBattleRequest();
	ClearBattleResult();
	return deleted or deletedWorld;
}

bool DragonCreateSlot (int slot, int avatarType, const char* name) {
	if(slot < 1 or slot > DRAGON_ALPHA_SLOT_COUNT)
		return false;
	if(avatarType < DRAGON_AVATAR_WARRIOR or avatarType >= DRAGON_AVATAR_COUNT)
		return false;

	ClearSaveData(gSave);
	gSave.slotIndex = (uint8_t)slot;
	gSave.avatarType = (uint8_t)avatarType;
	gSave.level = 1;
	gSave.areaIndex = 0;
	gSave.discoveredAreaMask = 1;
	gSave.gold = DRAGON_LEGACY_STARTING_GOLD;
	gSave.xp = 0;

	const char* fallback = "Hero";
	const char* chosen = (name != NULL and name[0] != '\0') ? name : fallback;
	EString::strncpy(gSave.name, chosen, DRAGON_ALPHA_NAME_SIZE - 1);
	gSave.name[DRAGON_ALPHA_NAME_SIZE - 1] = '\0';

	SeedAvatarStats(gSave);
	gSave.health = (int16_t)DragonGetMaxHealth();

	InventoryAddToSlot(gSave, DRAGON_ITEM_HEALTH_POTION, 3);
	InventoryAddToSlot(gSave, DRAGON_ITEM_FIRE_SCROLL, 1);
	CompactInventoryInternal(gSave);

	gSelectedArea = 0;
	DragonResetWorldState(slot);
	ClearBattleRequest();
	ClearBattleResult();

	DragonEnsureSaveInfo();
	gSaveInfo.selectedSlot = (uint8_t)slot;
	SaveSaveInfo();
	return DragonSaveCurrentSlot();
}

int DragonGetLevelXPRequirement (int level) {
	if(level < 1)
		level = 1;
	return level * level * 45 + level * 30;
}

int DragonGetAttack (void) {
	int total = gSave.attackBase + gSave.level + GetEquipBonus(gSave.equippedWeapon, &DragonItemInfo::attack);
	total += GetEquipBonus(gSave.equippedArmor, &DragonItemInfo::attack);
	total += GetEquipBonus(gSave.equippedRelic, &DragonItemInfo::attack);
	return std::max(1, total);
}

int DragonGetDefense (void) {
	int total = gSave.defenseBase + gSave.level / 2 + GetEquipBonus(gSave.equippedWeapon, &DragonItemInfo::defense);
	total += GetEquipBonus(gSave.equippedArmor, &DragonItemInfo::defense);
	total += GetEquipBonus(gSave.equippedRelic, &DragonItemInfo::defense);
	return std::max(1, total);
}

int DragonGetMagic (void) {
	int total = gSave.magicBase + gSave.level / 2 + GetEquipBonus(gSave.equippedWeapon, &DragonItemInfo::magic);
	total += GetEquipBonus(gSave.equippedArmor, &DragonItemInfo::magic);
	total += GetEquipBonus(gSave.equippedRelic, &DragonItemInfo::magic);
	return std::max(1, total);
}

int DragonGetSpeed (void) {
	int total = gSave.speedBase + gSave.level / 3 + GetEquipBonus(gSave.equippedWeapon, &DragonItemInfo::speed);
	total += GetEquipBonus(gSave.equippedArmor, &DragonItemInfo::speed);
	total += GetEquipBonus(gSave.equippedRelic, &DragonItemInfo::speed);
	return std::max(1, total);
}

int DragonGetMaxHealth (void) {
	int total = gSave.healthMax + gSave.level * 3 + GetEquipBonus(gSave.equippedWeapon, &DragonItemInfo::health);
	total += GetEquipBonus(gSave.equippedArmor, &DragonItemInfo::health);
	total += GetEquipBonus(gSave.equippedRelic, &DragonItemInfo::health);
	return std::max(1, total);
}

void DragonApplyCurrentLevelGrowth (void) {
	switch(gSave.avatarType) {
		case DRAGON_AVATAR_WARRIOR:
			gSave.attackBase += 2;
			gSave.defenseBase += 2;
			gSave.magicBase += 1;
			gSave.speedBase += 1;
			gSave.healthMax += 10;
			break;
		case DRAGON_AVATAR_SORCERER:
			gSave.attackBase += 1;
			gSave.defenseBase += 1;
			gSave.magicBase += 3;
			gSave.speedBase += 1;
			gSave.healthMax += 8;
			break;
		default:
			gSave.attackBase += 2;
			gSave.defenseBase += 1;
			gSave.magicBase += 1;
			gSave.speedBase += 2;
			gSave.healthMax += 9;
			break;
	}
}

void DragonHealToFull (void) {
	gSave.health = (int16_t)DragonGetMaxHealth();
}

void DragonSetStatusEntryMode (DragonStatusEntryMode mode) {
	switch(mode) {
		case DRAGON_STATUS_ENTRY_EQUIPMENT:
		case DRAGON_STATUS_ENTRY_MAGIC:
		case DRAGON_STATUS_ENTRY_TECH:
			gStatusEntryMode = mode;
			break;
		default:
			gStatusEntryMode = DRAGON_STATUS_ENTRY_EQUIPMENT;
			break;
	}
}

DragonStatusEntryMode DragonConsumeStatusEntryMode (void) {
	const DragonStatusEntryMode mode = gStatusEntryMode;
	gStatusEntryMode = DRAGON_STATUS_ENTRY_EQUIPMENT;
	return mode;
}

int DragonBuildLegacyCommandList (DragonLegacyCommandMenuMode mode, DragonLegacyCommandAction* outActions, int maxActions) {
	if(outActions == nullptr or maxActions <= 0)
		return 0;

	auto pushAction = [&] (const char* name, int element, int power, int hitPercent, int defenseBoost, bool isMagic, bool isHeal, bool isAll, int& count) {
		if(count >= maxActions)
			return;
		DragonLegacyCommandAction& action = outActions[count++];
		action.name = (name != nullptr and name[0] != '\0') ? name : "Action";
		action.effectElement = element;
		action.power = power;
		action.hitPercent = hitPercent;
		action.defenseBoost = std::max(0, defenseBoost);
		action.isMagic = isMagic;
		action.isHeal = isHeal;
		action.isAll = isAll;
	};

	const int avatarType = std::max((int)DRAGON_AVATAR_WARRIOR, std::min((int)gSave.avatarType, (int)DRAGON_AVATAR_COUNT - 1));
	int count = 0;
	if(mode == DRAGON_LEGACY_COMMAND_MENU_MAGIC) {
		switch(avatarType) {
			case DRAGON_AVATAR_WARRIOR:
				pushAction("Cure", DRAGON_LEGACY_ELEMENT_LIGHT, 2, 100, std::max(2, DragonGetDefense() / 4), true, true, false, count);
				pushAction("Guard Heal", DRAGON_LEGACY_ELEMENT_WATER, 1, 100, std::max(8, DragonGetDefense() / 2), true, true, false, count);
				break;
			case DRAGON_AVATAR_SORCERER:
				pushAction("Light Wave", DRAGON_LEGACY_ELEMENT_LIGHT, 0, 100, 0, true, false, false, count);
				pushAction("Light Wave-All", DRAGON_LEGACY_ELEMENT_LIGHT, 0, 100, 0, true, false, true, count);
				break;
			default:
				pushAction("Bolt", DRAGON_LEGACY_ELEMENT_AIR, 0, 84, 0, false, false, false, count);
				pushAction("Bolt-All", DRAGON_LEGACY_ELEMENT_AIR, 0, 78, 0, false, false, true, count);
				break;
		}
	}
	else if(mode == DRAGON_LEGACY_COMMAND_MENU_TECH) {
		switch(avatarType) {
			case DRAGON_AVATAR_WARRIOR:
				pushAction("Rally", DRAGON_LEGACY_ELEMENT_WATER, 1, 100, std::max(8, DragonGetDefense() / 2), false, true, false, count);
				pushAction("Power Strike", DRAGON_LEGACY_ELEMENT_EARTH, 0, 88, 0, false, false, false, count);
				break;
			case DRAGON_AVATAR_SORCERER:
				pushAction("Nova", DRAGON_LEGACY_ELEMENT_LIGHT, 0, 100, 0, true, false, false, count);
				pushAction("Nova-All", DRAGON_LEGACY_ELEMENT_LIGHT, 0, 100, 0, true, false, true, count);
				break;
			default:
				pushAction("Pin Shot", DRAGON_LEGACY_ELEMENT_AIR, 0, 88, 0, false, false, false, count);
				pushAction("Volley", DRAGON_LEGACY_ELEMENT_AIR, 0, 82, 0, false, false, true, count);
				break;
		}
	}
	return count;
}

int DragonComputeLegacyCommandFieldHeal (const DragonLegacyCommandAction& action) {
	if(action.isHeal == false)
		return 0;

	const int level = std::max(1, (int)gSave.level);
	const int healBase = action.isMagic
		? (5 * DragonGetMagic() + 2 * level)
		: (DragonGetAttack() + 3 * DragonGetSpeed() + DragonGetMagic() + 2 * level);
	const int heal = 40 * std::max(1, action.power) + healBase;
	return std::max(1, std::min(DRAGON_LEGACY_MAX_DAMAGE, heal));
}

EString DragonGrantLevelMilestoneRewards (int oldLevel, int newLevel) {
	if(newLevel <= oldLevel)
		return "";
	if(oldLevel < 1)
		oldLevel = 1;
	if(newLevel > DRAGON_RUNTIME_LEVEL_CAP)
		newLevel = DRAGON_RUNTIME_LEVEL_CAP;

	EString rewards;
	for(int level = oldLevel + 1; level <= newLevel; level++) {
		int itemType = MilestoneItemForAvatarLevel(gSave.avatarType, level);
		if(itemType == DRAGON_ITEM_NONE)
			continue;
		if(SaveOwnsItemType(gSave, itemType))
			continue;
		if(DragonInventoryAdd(itemType, 1) == false)
			continue;
		bool autoEquipped = TryAutoEquipMilestoneItem(itemType);
		const DragonItemInfo* info = DragonItemByType(itemType);
		if(info == NULL)
			continue;
		if(not rewards.IsEmpty())
			rewards += ", ";
		rewards += info->name;
		if(autoEquipped)
			rewards += " (equipped)";
	}
	return rewards;
}

bool DragonInventoryAdd (int itemType, int count) {
	bool result = InventoryAddToSlot(gSave, itemType, count);
	if(result)
		CompactInventoryInternal(gSave);
	return result;
}

bool DragonInventoryUse (int inventoryIndex) {
	if(inventoryIndex < 0 or inventoryIndex >= gSave.inventoryCount)
		return false;

	DragonInventoryEntry& entry = gSave.inventory[inventoryIndex];
	const DragonItemInfo* info = DragonItemByType(entry.type);
	if(info == NULL or info->slot != DRAGON_SLOT_CONSUMABLE)
		return false;
	if(info->heal <= 0)
		return false;
	if(gSave.health >= DragonGetMaxHealth())
		return false;

	if(entry.count == 0)
		return false;

	gSave.health = (int16_t)std::min(DragonGetMaxHealth(), (int)gSave.health + (int)info->heal);
	entry.count--;
	CompactInventoryInternal(gSave);
	return true;
}

bool DragonInventoryConsumeType (int itemType, int count) {
	if(itemType <= DRAGON_ITEM_NONE or itemType >= DRAGON_ITEM_COUNT or count <= 0)
		return false;

	for(int i = 0; i < gSave.inventoryCount; i++) {
		DragonInventoryEntry& entry = gSave.inventory[i];
		if(entry.type != itemType)
			continue;
		if((int)entry.count < count)
			return false;
		entry.count = (uint8_t)((int)entry.count - count);
		CompactInventoryInternal(gSave);
		return true;
	}
	return false;
}

#if defined(DRAGON_TEST)
bool DragonAutomationCaseIs (const char* caseId) {
	if(caseId == NULL or caseId[0] == '\0')
		return false;
	for(int i = 1; i + 1 < ESystem::GetArgCount(); i++) {
		const char* arg = ESystem::GetArgValue(i);
		if(arg == NULL or std::strcmp(arg, "--case-id") != 0)
			continue;
		const char* value = ESystem::GetArgValue(i + 1);
		return value != NULL and std::strcmp(value, caseId) == 0;
	}
	return false;
}

bool DragonWriteCorruptedSlotForTesting (int slot) {
	if(slot < 1 or slot > DRAGON_ALPHA_SLOT_COUNT)
		return false;

	DragonSaveSlot corrupt = {};
	if(ESystem::SaveWrite(BuildSlotName(slot), &corrupt, sizeof(corrupt)) == false)
		return false;
	ESystem::SaveDelete(BuildWorldStateName(slot));

	DragonSaveSlot ignored = {};
	DragonReadSlotPreview(slot, ignored);
	return DragonSlotHasCorruption(slot);
}
#endif

void DragonQueueBattle (int areaIndex, int battleType, int enemyTier, bool noRetreat, uint16_t completionFlagBit, int unlockAreaOnVictory, int bonusXP, int bonusGold, const char* forcedEnemy) {
	ClearBattleRequest();
	gBattleRequest.active = true;
	gBattleRequest.areaIndex = (uint8_t)std::max(0, std::min(areaIndex, DRAGON_ALPHA_AREA_COUNT - 1));
	gBattleRequest.battleType = (uint8_t)std::max((int)DRAGON_BATTLE_RANDOM, std::min(battleType, (int)DRAGON_BATTLE_BOSS));
	gBattleRequest.enemyTier = (uint8_t)std::max(0, std::min(enemyTier, 20));
	gBattleRequest.noRetreat = noRetreat ? 1 : 0;
	gBattleRequest.completionFlagBit = completionFlagBit;
	gBattleRequest.legacyPluginId = 0;
	gBattleRequest.legacyMapId = 0;
	gBattleRequest.legacyGroupId = 0;
	gBattleRequest.legacyEncounterRare = 0;
	gBattleRequest.unlockAreaOnVictory = (uint8_t)std::max(-1, std::min(unlockAreaOnVictory, DRAGON_ALPHA_AREA_COUNT - 1));
	gBattleRequest.bonusXP = (int16_t)std::max(-32000, std::min(bonusXP, 32000));
	gBattleRequest.bonusGold = (int16_t)std::max(-32000, std::min(bonusGold, 32000));
	if(forcedEnemy != NULL and forcedEnemy[0] != '\0')
		EString::strncpy(gBattleRequest.forcedEnemy, forcedEnemy, (int)sizeof(gBattleRequest.forcedEnemy) - 1);
	gBattleRequest.forcedEnemy[sizeof(gBattleRequest.forcedEnemy) - 1] = '\0';
}

void DragonQueueLegacyGroupBattle (int areaIndex, int battleType, uint16_t pluginId, uint16_t mapId, uint16_t groupId, bool rareEncounter, bool noRetreat, uint16_t completionFlagBit, int unlockAreaOnVictory, int bonusXP, int bonusGold, const char* forcedEnemy) {
	DragonQueueBattle(areaIndex, battleType, 0, noRetreat, completionFlagBit, unlockAreaOnVictory, bonusXP, bonusGold, forcedEnemy);
	gBattleRequest.legacyPluginId = pluginId;
	gBattleRequest.legacyMapId = mapId;
	gBattleRequest.legacyGroupId = groupId;
	gBattleRequest.legacyEncounterRare = rareEncounter ? 1 : 0;
}

bool DragonConsumeBattleRequest (DragonBattleRequest& outRequest) {
	if(gBattleRequest.active == false)
		return false;
	outRequest = gBattleRequest;
	ClearBattleRequest();
	return true;
}

void DragonPublishBattleResult (bool victory, const DragonBattleRequest* request, bool retreated) {
	ClearBattleResult();
	gBattleResult.active = true;
	gBattleResult.victory = victory ? 1 : 0;
	gBattleResult.retreated = retreated ? 1 : 0;
	if(request != NULL) {
		gBattleResult.areaIndex = request->areaIndex;
		gBattleResult.battleType = request->battleType;
		gBattleResult.completionFlagBit = request->completionFlagBit;
		gBattleResult.unlockAreaOnVictory = request->unlockAreaOnVictory;
	} else {
		gBattleResult.areaIndex = (uint8_t)std::max(0, std::min(gSelectedArea, DRAGON_ALPHA_AREA_COUNT - 1));
		gBattleResult.battleType = DRAGON_BATTLE_RANDOM;
		gBattleResult.completionFlagBit = 0;
		gBattleResult.unlockAreaOnVictory = 0xFF;
	}
}

bool DragonConsumeBattleResult (DragonBattleResult& outResult) {
	if(gBattleResult.active == false)
		return false;
	outResult = gBattleResult;
	ClearBattleResult();
	return true;
}

bool DragonEquipFromInventory (int inventoryIndex) {
	if(inventoryIndex < 0 or inventoryIndex >= gSave.inventoryCount)
		return false;

	DragonInventoryEntry& entry = gSave.inventory[inventoryIndex];
	const DragonItemInfo* info = DragonItemByType(entry.type);
	if(info == NULL)
		return false;

	if(info->slot == DRAGON_SLOT_CONSUMABLE)
		return DragonInventoryUse(inventoryIndex);

	uint8_t* equipField = NULL;
	switch(info->slot) {
		case DRAGON_SLOT_WEAPON: equipField = &gSave.equippedWeapon; break;
		case DRAGON_SLOT_ARMOR: equipField = &gSave.equippedArmor; break;
		case DRAGON_SLOT_RELIC: equipField = &gSave.equippedRelic; break;
		default: return false;
	}

	uint8_t previous = *equipField;
	*equipField = entry.type;

	if(entry.count > 0)
		entry.count--;
	if(previous != DRAGON_ITEM_NONE)
		InventoryAddToSlot(gSave, previous, 1);

	CompactInventoryInternal(gSave);
	if(gSave.health > DragonGetMaxHealth())
		gSave.health = (int16_t)DragonGetMaxHealth();
	return true;
}

bool DragonUnequipSlot (int slotType) {
	uint8_t* equipField = NULL;
	switch(slotType) {
		case DRAGON_SLOT_WEAPON: equipField = &gSave.equippedWeapon; break;
		case DRAGON_SLOT_ARMOR: equipField = &gSave.equippedArmor; break;
		case DRAGON_SLOT_RELIC: equipField = &gSave.equippedRelic; break;
		default: return false;
	}

	if(equipField == NULL or *equipField == DRAGON_ITEM_NONE)
		return false;
	if(DragonInventoryAdd(*equipField, 1) == false)
		return false;

	*equipField = DRAGON_ITEM_NONE;
	if(gSave.health > DragonGetMaxHealth())
		gSave.health = (int16_t)DragonGetMaxHealth();
	return true;
}

DragonBattleRewards DragonAwardBattleRewards (int areaIndex, int bonusXP, int bonusGold, bool allowRandomDrops) {
	DragonBattleRewards rewards = {};
	if(areaIndex >= 0 and areaIndex < DRAGON_ALPHA_AREA_COUNT)
		gSave.areaIndex = (uint8_t)areaIndex;

	gSave.battlesWon++;
	int goldBefore = gSave.gold;
	int64_t nextGold = (int64_t)gSave.gold + (int64_t)bonusGold;
	if(nextGold < 0)
		nextGold = 0;
	if(nextGold > DESIGN_MAX_GOLD)
		nextGold = DESIGN_MAX_GOLD;
	gSave.gold = (int32_t)nextGold;
	rewards.goldGranted = std::max(0, gSave.gold - goldBefore);

	uint32_t xpGain = (uint32_t)std::max(0, bonusXP);
	if(UINT32_MAX - gSave.xp < xpGain)
		gSave.xp = UINT32_MAX;
	else
		gSave.xp += xpGain;
	rewards.xpGranted = (int)xpGain;

	int levelBefore = gSave.level;
	while(gSave.level < DRAGON_RUNTIME_LEVEL_CAP) {
		int needed = DragonGetLevelXPRequirement(gSave.level);
		if((int)gSave.xp < needed)
			break;
		gSave.xp -= (uint32_t)needed;
		gSave.level++;
		DragonApplyCurrentLevelGrowth();
	}
	rewards.levelsGained = std::max(0, (int)gSave.level - levelBefore);
	EString milestoneRewards = DragonGrantLevelMilestoneRewards(levelBefore, gSave.level);
	if(not milestoneRewards.IsEmpty())
		EString::strncpy(rewards.milestoneRewards, milestoneRewards, (int)sizeof(rewards.milestoneRewards) - 1);

	if(allowRandomDrops) {
		if(ENode::GetRandom(100) < 28 and DragonInventoryAdd(DRAGON_ITEM_HEALTH_POTION, 1))
			rewards.potionDrops++;
		if(ENode::GetRandom(100) < 20 and DragonInventoryAdd(DRAGON_ITEM_FIRE_SCROLL, 1))
			rewards.scrollDrops++;
	}

	int maxHealth = DragonGetMaxHealth();
	int healthBefore = gSave.health;
	int recovery = std::max(4, maxHealth / 8);
	gSave.health = (int16_t)std::min(maxHealth, (int)gSave.health + recovery);
	if(gSave.health < 1)
		gSave.health = 1;
	rewards.healthRecovered = std::max(0, (int)gSave.health - healthBefore);
	DragonSaveCurrentSlot();
	return rewards;
}

void DragonApplyBattleDefeat (void) {
	gSave.battlesLost++;
	int penalty = std::max(4, gSave.gold / 8);
	gSave.gold = std::max(0, gSave.gold - penalty);
	gSave.health = (int16_t)std::max(1, DragonGetMaxHealth() / 2);
	DragonSaveCurrentSlot();
}

LegacyAssetSummary BuildLegacyAssetSummary (void) {
	LegacyAssetSummary summary = {};

	const char* pluginsCandidates[] = {
		"Assets/Legacy/Data/Areas",
		"../Assets/Legacy/Data/Areas",
		"../../Assets/Legacy/Data/Areas",
		"../../../Assets/Legacy/Data/Areas",
	};
	const char* pluginsPath = FindExistingDirectoryPath(pluginsCandidates, sizeof(pluginsCandidates) / sizeof(pluginsCandidates[0]));
	if(pluginsPath != NULL) {
		summary.pluginDirectoryFound = true;
		summary.pluginAppCount = CountTopLevelPluginApps(pluginsPath);
		summary.mapCount = CountExtensionsRecursive(pluginsPath, ".map");
		summary.map2Count = CountExtensionsRecursive(pluginsPath, ".map2");
		summary.maskCount = CountExtensionsRecursive(pluginsPath, ".mask");
		summary.battleCount = CountExtensionsRecursive(pluginsPath, ".battle");
		summary.midiCount = CountExtensionsRecursive(pluginsPath, ".mid") + CountExtensionsRecursive(pluginsPath, ".midi");
	}

	const char* artCandidates[] = {
		"Assets/Legacy/Art/Additional",
		"../Assets/Legacy/Art/Additional",
		"../../Assets/Legacy/Art/Additional",
		"../../../Assets/Legacy/Art/Additional",
	};
	const char* artPath = FindExistingDirectoryPath(artCandidates, sizeof(artCandidates) / sizeof(artCandidates[0]));
	if(artPath != NULL) {
		char monstersPath[PATH_MAX];
		char itemsPath[PATH_MAX];
		snprintf(monstersPath, sizeof(monstersPath), "%s/Monsters", artPath);
		snprintf(itemsPath, sizeof(itemsPath), "%s/Items", artPath);
		summary.monsterArtCount = CountExtensionsRecursive(monstersPath, ".jpg") + CountExtensionsRecursive(monstersPath, ".png");
		summary.itemArtCount = CountExtensionsRecursive(itemsPath, ".jpg") + CountExtensionsRecursive(itemsPath, ".png");
	}

	const char* runtimeResourceCandidates[] = {
		"Images",
		"Resources/Images",
		"../Resources/Images",
	};
	const char* helpPageCandidates[] = {
		"Help.html",
		"Resources/Apple/Help.html",
		"../Resources/Apple/Help.html",
	};

	summary.runtimeResourceSetFound = DirectoryExistsAtAnyPath(runtimeResourceCandidates, sizeof(runtimeResourceCandidates) / sizeof(runtimeResourceCandidates[0]));
	summary.helpPageFound = FileExistsAtAnyPath(helpPageCandidates, sizeof(helpPageCandidates) / sizeof(helpPageCandidates[0]));
	return summary;
}

bool DragonImageResourcesMatch (const char* firstImageId, const char* secondImageId) {
	if(firstImageId == NULL or secondImageId == NULL)
		return false;
	if(firstImageId[0] == '\0' or secondImageId[0] == '\0')
		return false;
	if(std::strcmp(firstImageId, secondImageId) == 0)
		return true;

	EImage::Resource first;
	EImage::Resource second;
	if(first.New(firstImageId) == false or second.New(secondImageId) == false)
		return false;
	if(first.buffer == NULL or second.buffer == NULL)
		return false;
	if(first.width <= 0 or first.height <= 0 or second.width <= 0 or second.height <= 0)
		return false;
	if(first.width != second.width or first.height != second.height)
		return false;

	const int totalPixels = first.width * first.height;
	const int sampleStride = std::max(1, totalPixels / 8192);
	int compared = 0;
	int mismatched = 0;
	for(int pixelIndex = 0; pixelIndex < totalPixels; pixelIndex += sampleStride) {
		const int offset = pixelIndex * 4;
		for(int channel = 0; channel < 4; channel++) {
			compared++;
			if(first.buffer[offset + channel] != second.buffer[offset + channel])
				mismatched++;
		}
	}
	if(compared <= 0)
		return false;
	const double mismatchRatio = (double)mismatched / (double)compared;
	return mismatchRatio <= 0.001;
}

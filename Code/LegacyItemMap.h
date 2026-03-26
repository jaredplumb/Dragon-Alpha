/**
 * @file LegacyItemMap.h
 * @brief Legacy-to-modern item mapping declarations used during migration compatibility.
 */
#ifndef DRAGON_ALPHA_LEGACY_ITEM_MAP_H
#define DRAGON_ALPHA_LEGACY_ITEM_MAP_H

#include "Global.h"
#include "LegacyBattleData.generated.h"

#include <algorithm>
#include <cctype>
#include <string>

static inline int DragonLegacyAreaTierForPlugin (uint16_t pluginId) {
	switch(pluginId) {
		case 1: return 0;
		case 1000: return 1;
		case 4: return 2;
		case 2: return 3;
		case 3: return 4;
		default: return 0;
	}
}

static inline bool DragonMapLegacyItemRefToModern (const DragonLegacyItemRef& item, int& outItemType, int& outGoldBonus) {
	outItemType = DRAGON_ITEM_NONE;
	outGoldBonus = 0;
	if(item.itemType == 0)
		return false;

	const int areaTier = DragonLegacyAreaTierForPlugin(item.pluginId);
	const int resourceTier = std::max(0, std::min(31, (int)item.resourceId - 128));
	const DragonLegacyNamedItemData* namedItem = DragonFindLegacyNamedItemData(item.pluginId, item.itemType, item.resourceId);
	std::string legacyName;
	if(namedItem != NULL and namedItem->name != NULL and namedItem->name[0] != '\0') {
		legacyName = namedItem->name;
		for(size_t i = 0; i < legacyName.size(); i++)
			legacyName[i] = (char)std::tolower((unsigned char)legacyName[i]);
	}
	auto hasToken = [&legacyName] (const char* token) -> bool {
		if(token == NULL or token[0] == '\0' or legacyName.empty())
			return false;
		return legacyName.find(token) != std::string::npos;
	};

	const bool weaponLike = hasToken("sword") or hasToken("knife") or hasToken("axe")
		or hasToken("hammer") or hasToken("spear") or hasToken("staff")
		or hasToken("club") or hasToken("scimitar") or hasToken("blade");
	const bool armorLike = hasToken("armor") or hasToken("shirt") or hasToken("robe")
		or hasToken("cloak") or hasToken("boots") or hasToken("pants")
		or hasToken("helmet") or hasToken("cap") or hasToken("gloves")
		or hasToken("gauntlets") or hasToken("guard") or hasToken("carapace")
		or hasToken("garb") or hasToken("mask");
	const bool relicLike = hasToken("ring") or hasToken("charm") or hasToken("talisman")
		or hasToken("book") or hasToken("mirror");
	const bool spellLike = hasToken("wave") or hasToken("blast") or hasToken("bolt")
		or hasToken("fireball") or hasToken("beam") or hasToken("assassin")
		or hasToken("pierce");

	if(item.itemType == 101 or (item.itemType == 102 and hasToken("coin"))) {
		int bonus = 18 + areaTier * 10 + std::min(42, resourceTier * 2);
		if(hasToken("bronze"))
			bonus += 12;
		if(hasToken("silver"))
			bonus += 24;
		if(hasToken("gold"))
			bonus += 40;
		if(hasToken("platinum"))
			bonus += 80;
		if(hasToken("titanium"))
			bonus += 120;
		if(hasToken("kuaru"))
			bonus += 160;
		outGoldBonus = bonus;
		return true;
	}

	if(item.itemType == 100 or hasToken("potion")) {
		outItemType = DRAGON_ITEM_HEALTH_POTION;
		return true;
	}

	if(item.itemType == 50 or item.itemType == 51 or item.itemType == 52 or spellLike) {
		outItemType = DRAGON_ITEM_FIRE_SCROLL;
		return true;
	}

	if(item.itemType == 2) {
		if(hasToken("dragon") or hasToken("kuaru"))
			outItemType = DRAGON_ITEM_DRAGON_TALISMAN;
		else if(hasToken("sorcerer") or hasToken("angel"))
			outItemType = DRAGON_ITEM_SEER_CHARM;
		else
			outItemType = DRAGON_ITEM_MYSTIC_TOME;
		return true;
	}

	if(item.itemType == 1 or weaponLike) {
		static const int kWeaponProgression[] = {
			DRAGON_ITEM_IRON_BLADE,
			DRAGON_ITEM_HUNTER_BOW,
			DRAGON_ITEM_ADEPT_WAND,
			DRAGON_ITEM_STEEL_SABER,
			DRAGON_ITEM_WAR_HAMMER,
			DRAGON_ITEM_MYSTIC_TOME,
		};
		int index = std::max(0, std::min((int)(sizeof(kWeaponProgression) / sizeof(kWeaponProgression[0])) - 1, areaTier + resourceTier / 3));
		if(hasToken("staff"))
			index = std::max(index, 2);
		if(hasToken("hammer") or hasToken("axe"))
			index = std::max(index, 4);
		if(hasToken("kuaru"))
			index = (int)(sizeof(kWeaponProgression) / sizeof(kWeaponProgression[0])) - 1;
		outItemType = kWeaponProgression[index];
		return true;
	}

	if(relicLike or item.itemType >= 6) {
		if(hasToken("ring"))
			outItemType = (areaTier >= 4 or resourceTier >= 12) ? DRAGON_ITEM_GUARDIAN_RING : DRAGON_ITEM_SEER_CHARM;
		else if(hasToken("book") or hasToken("mirror"))
			outItemType = (areaTier >= 3 or resourceTier >= 10) ? DRAGON_ITEM_SEER_CHARM : DRAGON_ITEM_SCOUT_CHARM;
		else if(hasToken("dragon") or hasToken("kuaru"))
			outItemType = DRAGON_ITEM_DRAGON_TALISMAN;
		else if(hasToken("guardian"))
			outItemType = DRAGON_ITEM_GUARDIAN_RING;
		else
			outItemType = (areaTier >= 3 or resourceTier >= 10) ? DRAGON_ITEM_SEER_CHARM : DRAGON_ITEM_SCOUT_CHARM;
		return true;
	}

	if(hasToken("shield")) {
		outItemType = (areaTier >= 3 or resourceTier >= 10) ? DRAGON_ITEM_TOWER_SHIELD : DRAGON_ITEM_TRAVELER_CLOAK;
		return true;
	}

	if(armorLike or item.itemType == 3 or item.itemType == 4 or item.itemType == 5) {
		if(areaTier >= 4 or resourceTier >= 12)
			outItemType = DRAGON_ITEM_TOWER_SHIELD;
		else if(areaTier >= 2 or resourceTier >= 8)
			outItemType = DRAGON_ITEM_TRAVELER_CLOAK;
		else
			outItemType = DRAGON_ITEM_LEATHER_ARMOR;
		return true;
	}

	switch(item.itemType) {
		case 3:
			outItemType = (areaTier >= 2 or resourceTier >= 8) ? DRAGON_ITEM_TRAVELER_CLOAK : DRAGON_ITEM_LEATHER_ARMOR;
			return true;
		case 4:
		case 5:
			outItemType = (areaTier >= 3 or resourceTier >= 10) ? DRAGON_ITEM_TOWER_SHIELD : DRAGON_ITEM_TRAVELER_CLOAK;
			return true;
		case 6:
		case 7:
		case 8:
			outItemType = (areaTier >= 4 or resourceTier >= 10) ? DRAGON_ITEM_DRAGON_TALISMAN : DRAGON_ITEM_SEER_CHARM;
			return true;
		case 9:
		case 10:
		case 11:
		case 12:
		case 13:
			outItemType = (areaTier >= 4 or resourceTier >= 12) ? DRAGON_ITEM_GUARDIAN_RING : DRAGON_ITEM_SEER_CHARM;
			return true;
		case 102:
			outItemType = (areaTier >= 3 or resourceTier >= 10) ? DRAGON_ITEM_GUARDIAN_RING : DRAGON_ITEM_DRAGON_TALISMAN;
			return true;
		default:
			return false;
	}
}

#endif // DRAGON_ALPHA_LEGACY_ITEM_MAP_H

/**
 * @file Battle.h
 * @brief Battle scene interface, combat state, and command declarations.
 */
#ifndef DRAGON_ALPHA_BATTLE_H
#define DRAGON_ALPHA_BATTLE_H

#include "Global.h"
#include <vector>

struct DragonLegacySpecialData;

class Battle : public ENodeWithName<Battle, "Battle"> {
public:
	Battle ();
	void OnDraw () override;
	void OnTouch (int x, int y) override;
	void OnTouchUp (int x, int y) override;
	void OnTouchMove (int x, int y) override;

private:
	enum BattleEffectType {
		BATTLE_EFFECT_NONE = 0,
		BATTLE_EFFECT_ATTACK_HIT,
		BATTLE_EFFECT_HEAL_HIT,
		BATTLE_EFFECT_FIRE_CAST,
		BATTLE_EFFECT_FIRE_HIT,
		BATTLE_EFFECT_WATER_CAST,
		BATTLE_EFFECT_WATER_HIT,
		BATTLE_EFFECT_EARTH_CAST,
		BATTLE_EFFECT_EARTH_HIT,
		BATTLE_EFFECT_AIR_CAST,
		BATTLE_EFFECT_AIR_HIT,
		BATTLE_EFFECT_LIGHT_CAST,
		BATTLE_EFFECT_LIGHT_HIT,
		BATTLE_EFFECT_DARK_CAST,
		BATTLE_EFFECT_DARK_HIT,
		BATTLE_EFFECT_NONE_CAST,
	};

	struct EnemyData {
		EString name;
		int level;
		int health;
		int healthMax;
		int attack;
		int defense;
		int magic;
		int statStrength;
		int statDefense;
		int statSpeed;
		int statMagic;
		int resistPhysical;
		int resistMagic;
		int resistLight;
		int resistDark;
		int resistFire;
		int resistWater;
		int resistEarth;
		int resistAir;
		int rewardXP;
		int rewardGold;
	};

	struct CombatStats {
		int str;
		int def;
		int spd;
		int mag;
		int p;
		int m;
		int l;
		int d;
		int f;
		int w;
		int e;
		int a;
	};

	struct EnemyUnitRuntime {
		EnemyData data;
		uint16_t pluginId;
		uint16_t monsterId;
		uint16_t spriteId;
		int artVariant;
		int formationX;
		int formationY;
		int turnMeter;
		bool boss;
	};

	struct EnemyTouchTarget {
		int enemyIndex;
		ERect rect;
	};

	enum PendingTargetCommand {
		PENDING_TARGET_NONE = 0,
		PENDING_TARGET_ATTACK,
		PENDING_TARGET_MAGIC,
		PENDING_TARGET_TECH,
	};

	enum LegacyCommandMenuMode {
		LEGACY_COMMAND_MENU_NONE = 0,
		LEGACY_COMMAND_MENU_MAGIC,
		LEGACY_COMMAND_MENU_TECH,
	};

	struct LegacyCommandAction {
		EString name;
		int effectElement;
		int power;
		int hitPercent;
		int defenseBoost;
		bool isMagic;
		bool isHeal;
		bool isAll;
		bool usesSkillCharge;
	};

	void BuildEnemy ();
	void ClearEnemyGroup ();
	void PushEnemyUnit (const EnemyData& data, uint16_t pluginId, uint16_t monsterId, uint16_t spriteId, int artVariant, int formationX, int formationY, bool boss);
	int LivingEnemyCount () const;
	int FirstLivingEnemyIndex () const;
	int NextLivingEnemyIndex (int fromIndex) const;
	int TotalEnemyHealth () const;
	int TotalEnemyHealthMax () const;
	void SyncActiveEnemyFromGroup ();
	bool HandleEnemyDefeat ();
	bool TryCycleEnemyTarget ();
	void UpdateLayoutRects ();
	ERect CommandLaneRect () const;
	bool HitCommandLaneControl (int x, int y) const;
	int FindLivingEnemyTouchTargetAtPoint (int x, int y) const;
	int FindNearestLivingEnemyTouchTarget (int x, int y, int maxDistance) const;
	void SeedTurnMeters ();
	void ResolveEnemyActionForIndex (int actingIndex, int defenseBoost, const char* actionPrefix);
	CombatStats EnemyCombatStats (const EnemyData& enemyData) const;
	CombatStats PlayerCombatStats () const;
	int ElementResistanceForStats (const CombatStats& stats, int element) const;
	int RollLegacyAttackDamage (int baseDamage, int hitPercent, int element, const CombatStats& targetStats, int targetLevel, bool& missed, bool& heal) const;
	int RollLegacySpellDamage (int baseDamage, int element, bool isMagic, const CombatStats& targetStats, int targetLevel, bool& heal) const;
	int RollDamage (int attack, int defense, int bonus, bool& missed, bool& critical) const;
	void GainSkillCharge (int amount);
	bool CanUseSkill () const;
	void TriggerPlayerEffect (int effectType, int duration = 460);
	void TriggerEnemyEffect (int effectType, int duration = 460);
	EImage* EffectImageByType (int effectType);
	void DrawEffectForRect (const ERect& rect, int effectType, int64_t effectTimestamp, int duration);
	bool AttackRequiresEnemyTargetSelection () const;
	bool MagicRequiresEnemyTargetSelection () const;
	bool TechRequiresEnemyTargetSelection () const;
	void ClearPendingTargetCommand ();
	void SetActionTextThrottled (const EString& text, int64_t minIntervalMs);
	void ResolvePlayerAttack (bool usingSkill);
	void ResolvePlayerMagic ();
	void ResolvePlayerTech ();
	void ResolvePlayerTechLegacy ();
	void BuildLegacyCommandList (LegacyCommandMenuMode mode, std::vector<LegacyCommandAction>& outCommands) const;
	bool LegacyCommandNeedsEnemyTarget (const LegacyCommandAction& action) const;
	void OpenLegacyCommandMenu (LegacyCommandMenuMode mode);
	bool ResolveLegacyCommandAction (const LegacyCommandAction& action);
	void ResolveAlternateSkill ();
	int CountInventoryType (int itemType) const;
	bool HasUsableConsumables () const;
	bool ResolvePlayerItem (int itemType);
	void ResolveDefend ();
	void ResolveEnemyTurn (int defenseBoost = 0, const char* actionPrefix = NULL);
	const DragonLegacySpecialData* ChooseEnemySpecial (const EnemyUnitRuntime& actingUnit, uint16_t& outSpecialId) const;
	bool AwardLegacyBattleLoot (int& outItemCount, int& outGoldGain, EString& outFirstItemName);
	void TryRetreat ();
	void FinishVictory ();
	void FinishDefeat ();
	void FinishRetreat ();

	EImage imageBackground;
	EImage imageArena;
	EImage imagePlayer;
	EImage imageEnemy;
	EImage imageEnemyVariant[4];
	EImage imageHealthBack;
	EImage imageHealthFill;
	EImage imageSelectRed;
	EImage imageSelectYellow;
	EImage imageSelectGreen;
	EImage imageSpellName;
	EImage imageAction;
	EImage imageActionAlt;
	EImage imageOverlay;
	EImage imageGameOver;
	EImage imageFxAttackHit;
	EImage imageFxHealHit;
	EImage imageFxFireCast;
	EImage imageFxFireHit;
	EImage imageFxWaterCast;
	EImage imageFxWaterHit;
	EImage imageFxEarthCast;
	EImage imageFxEarthHit;
	EImage imageFxAirCast;
	EImage imageFxAirHit;
	EImage imageFxLightCast;
	EImage imageFxLightHit;
	EImage imageFxDarkCast;
	EImage imageFxDarkHit;
	EImage imageFxNoneCast;
	ESound soundClick;
	ESound soundHit;
	ESound soundMiss;
	ESound soundCast;
	ESound soundDefeat;
	ESound soundLevelUp;
	EFont fontHeader;
	EFont fontMain;
	UITransition transition;

	EnemyData enemy;
	DragonBattleRequest battleRequest;
	bool hasBattleRequest;
	std::vector<EnemyUnitRuntime> enemyGroup;
	std::vector<EnemyTouchTarget> enemyTouchTargets;
	int activeEnemyIndex;
	int totalEnemyRewardXP;
	int totalEnemyRewardGold;
	bool retreatLocked;
	bool battleRetreated;
	EString actionText;
	const DragonAreaInfo* areaInfo;
	ERect attackRect;
	ERect skillRect;
	ERect itemRect;
	ERect techRect;
	ERect retreatRect;
	ERect continueRect;
	ERect backRect;
	ERect enemyTargetRect;
	ERect skillMenuRect;
	ERect skillPrimaryRect;
	ERect skillAlternateRect;
	ERect skillCancelRect;
	ERect skillTertiaryRect;
	ERect itemMenuRect;
	ERect itemPotionRect;
	ERect itemScrollRect;
	ERect itemCancelRect;
	int lastPlayerDamage;
	int lastEnemyDamage;
	int validationPlayerActionSerial;
	EString validationPlayerActionText;
	int validationEnemyActionSerial;
	EString validationEnemyActionText;
	int enemyArtVariant;
	int playerTurnMeter;
	int64_t actionTimestamp;
	bool touchHandledThisSequence;
	bool touchMovedThisSequence;
	int touchDownX;
	int touchDownY;
	int startLevel;
	int levelGain;
	int defeatGoldLoss;
	int retreatHealthLoss;
	int victoryXP;
	int victoryGold;
	int victoryPotionDrops;
	int victoryScrollDrops;
	int victoryHealthRecovered;
	int victoryLegacyLootItems;
	int victoryLegacyLootGold;
	EString victoryLegacyLootFirstItem;
	EString victoryMilestoneRewards;
	int skillCharge;
	int playerEffectType;
	int enemyEffectType;
	int playerEffectDuration;
	int enemyEffectDuration;
	int64_t playerEffectTimestamp;
	int64_t enemyEffectTimestamp;
	int roundNumber;
	bool trialBattle;
	bool storyBattle;
	bool rareRoamingBattle;
	bool strictLegacyBattle;
	bool fallbackBattlePath;
	EString fallbackBattleReason;
	EString battleSourceTag;
	bool skillMenuActive;
	bool itemMenuActive;
	LegacyCommandMenuMode legacyCommandMenuMode;
	LegacyCommandAction pendingLegacyCommandAction;
	bool hasPendingLegacyCommandAction;
	std::vector<LegacyCommandAction> legacyCommandActions;
	PendingTargetCommand pendingTargetCommand;
	bool battleDone;
	bool battleWon;
};

#endif // DRAGON_ALPHA_BATTLE_H

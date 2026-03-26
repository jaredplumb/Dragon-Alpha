/**
 * @file WorldMap.h
 * @brief World map scene interface, progression state, and traversal declarations.
 */
#ifndef DRAGON_ALPHA_WORLDMAP_H
#define DRAGON_ALPHA_WORLDMAP_H

#include "Global.h"
#include <vector>

struct DragonLegacyMapScriptData;
struct DragonLegacyMapObject;
struct DragonLegacyShopData;
struct DragonLegacyItemRef;

class WorldMap : public ENodeWithName<WorldMap, "WorldMap"> {
public:
	WorldMap ();
	void OnDraw () override;
	void OnTouch (int x, int y) override;
	void OnTouchMove (int x, int y) override;
	void OnTouchUp (int x, int y) override;

private:
	enum MapEventType {
		MAP_EVENT_NONE = 0,
		MAP_EVENT_TALK,
		MAP_EVENT_ITEM_GATE,
		MAP_EVENT_TREASURE,
		MAP_EVENT_HEAL,
		MAP_EVENT_TRAIN,
		MAP_EVENT_SHOP,
		MAP_EVENT_WARP,
		MAP_EVENT_BATTLE_GATE,
		MAP_EVENT_BATTLE_CHALLENGE,
	};

	struct MapEventDefinition {
		uint8_t x;
		uint8_t y;
		uint8_t type;
		uint8_t value;
		uint16_t flagBit;
		uint16_t requirementFlagBit;
		const char* text;
		const char* confirmText;
		uint16_t legacyObjectIndex;
		uint16_t legacyObjectType;
	};

	struct MapScriptAction {
		uint8_t type;
		uint8_t value;
		uint16_t flagBit;
		const char* text;
		int8_t nextAction;
		int8_t nextNoAction;
		uint8_t legacyScript;
		uint16_t legacyObjectType;
		uint16_t legacyData0;
		uint16_t legacyData1;
		uint16_t legacyData2;
		uint16_t legacyData3;
	};

	static constexpr int MAP_EVENT_CAPACITY = 50;
	static constexpr uint16_t LEGACY_OBJECT_INDEX_NONE = 0xFFFF;
	static constexpr int SHOP_MAX_OFFERS = 90;
	static constexpr int SHOP_VISIBLE_ROWS = 6;
	static constexpr int MAP_ROUTE_MESSAGE_THROTTLE_MS = 700;
	static constexpr int MAP_ROUTE_FAILURE_MESSAGE_THROTTLE_MS = 450;
	static constexpr int MAP_INTERACTION_MISS_MESSAGE_THROTTLE_MS = 450;
	static constexpr int MAP_MOVE_ANIMATION_MS = 96;

	void SyncSelectedAreaFromSave ();
	void SyncMapPositionFromWorldState ();
	void EnsureAreaVisualLoaded ();
	void LoadAreaMaskCollisions ();
	void RebuildAreaBlockedCells (int area, int preferredX, int preferredY);
	void ApplyLegacyObjectCollisions (int area);
	void RebuildEventPositions (int area);
	bool ResolveEventPositionAtIndex (int area, int eventIndex, int& outX, int& outY) const;
	bool FindNearestOpenCell (int area, int originX, int originY, int& outX, int& outY) const;
	bool IsReachableCell (int area, int startX, int startY, int targetX, int targetY) const;
	int EventCountForArea (int area) const;
	const MapEventDefinition* EventDefinitionAt (int area, int eventIndex) const;
	const DragonLegacyMapScriptData* LegacyScriptMapDataForArea (int area) const;
	bool EnsureLegacyEventCache (int area) const;
	bool LegacyMapObjectForEvent (int area, const MapEventDefinition& eventDefinition, DragonLegacyMapObject& outObject) const;
	bool IsLegacyInteractiveObjectType (uint16_t objectType) const;
	uint8_t MapEventTypeForLegacyObject (uint16_t objectType) const;
	bool BuildLegacyScriptActions (int area, int eventIndex, std::vector<MapScriptAction>& outActions) const;
	int AppendLegacyScriptActionRecursive (const DragonLegacyMapScriptData* mapData, const DragonLegacyMapObject& object, std::vector<MapScriptAction>& actions, int depth, std::vector<uint16_t>& recursionStack) const;
	bool ResolveLegacyScriptAction (int area, int eventIndex, int actionIndex, MapScriptAction& outAction) const;
	bool ExecuteEventScript (int area, int eventIndex, int actionIndex, bool fromMove);
	bool ExecuteEventAction (const MapEventDefinition& eventDefinition, int area, int eventIndex, const MapScriptAction& action, bool fromMove);
	bool ExecuteLegacyEventAction (int area, int eventIndex, const MapScriptAction& action, bool fromMove);
	EString ResolveScriptActionText (const MapEventDefinition& eventDefinition, const MapScriptAction& action) const;
	EString BuildConfirmPromptText (const MapEventDefinition& eventDefinition, const char* promptFallback) const;
	EString ResolveDeclineMessage (const MapEventDefinition* eventDefinition, const EString& promptText, bool trainingPending) const;
	int LegacyTrainingCostForLevel (int level) const;
	bool ApplyLegacyTrainingPurchase (int cost);
	void ShowTalkPrompt (const EString& text, int area, int eventIndex, int nextAction, bool fromMove);
	void ResolveTalkPrompt ();
	bool ResolveShopPrompt ();
	void SaveProgress ();
	void ApplyPendingBattleResult ();
	void SyncLegacyMapStateFromSelection ();
	void RestoreLegacyMapStateFromWorldState ();
	void StoreLegacyMapStateToWorldState ();
	const char* LegacyAreaMapImageName (int area) const;
	const char* LegacyAreaOverlayImageName (int area) const;
	const char* LegacyAreaMaskImageName (int area) const;
	void ApplyLegacyWarpState (int targetArea, uint16_t targetPluginId, uint16_t targetMapId, int targetX, int targetY);
	bool TryApplyLegacyWarpAtCell (int x, int y, bool fromMove);
	bool TryApplyLegacyBoundaryWarp (int nextX, int nextY, bool fromMove);
	void SetUnavailableWarpMessage (uint16_t pluginId);
	int AreaIndexForLegacyPluginMap (uint16_t pluginId, uint16_t mapId) const;
	bool IsAreaUnlocked (int area) const;
	void NormalizeProgressionState ();
	bool IsBlockedCell (int area, int x, int y) const;
	const MapEventDefinition* FindEvent (int area, int x, int y, int* outEventIndex = NULL) const;
	bool IsEventComplete (const MapEventDefinition& eventDefinition, int area = -1) const;
	void MarkEventComplete (const MapEventDefinition& eventDefinition, int area = -1);
	bool IsRequirementMet (const MapEventDefinition& eventDefinition, int area = -1) const;
	void SetMessage (const EString& text);
	void SetMessageThrottled (const EString& text, int64_t minIntervalMs);
	void SetRouteInteractionHintMessage (bool interactOnly);
	EString ObjectiveText () const;
	void MoveBy (int dx, int dy);
	bool TryInteractFacing ();
	bool TriggerEventAt (int x, int y, bool fromMove);
	bool MapLegacyItemRefToModern (const DragonLegacyItemRef& item, int& outItemType, int& outGoldBonus) const;
	bool AwardLegacyItems (const DragonLegacyItemRef* items, int itemCount, bool freePickup, const char* sourceName = NULL);
	void AwardLegacyTreasure (uint16_t pluginId, uint16_t treasureId);
	void OpenShop ();
	void OpenLegacyShop (uint16_t pluginId, uint16_t shopId);
	void BuildShopOffers ();
	void BuildShopOffersFromLegacy (const DragonLegacyShopData* shopData);
	bool TryBuyShopOffer (int offerIndex);
	const char* ItemImageName (int itemType) const;
	bool TryRandomBattle ();
	int EncounterChancePercentForSteps (int stepsSinceLastBattle) const;
	void BeginBattleTransition ();
	void ShowConfirmPrompt (const MapEventDefinition& eventDefinition, int area, int eventIndex, const MapScriptAction& action, bool fromMove);
	void ResolveConfirmPrompt (bool accepted);
	EImage& EventIcon (const MapEventDefinition& eventDefinition);
	bool IsInteractOnlyEventType (uint8_t eventType) const;
	bool ShouldAutoInteractAtAdjacentEvent (const MapEventDefinition& eventDefinition, int eventX, int eventY) const;
	void PlayStepSound ();
	void PlayBumpSound ();
	bool IsLegacyWarpCell (int area, int x, int y) const;
	bool IsUnsupportedLegacyWarpCell (int area, int x, int y, uint16_t* outPluginId = NULL) const;
	bool IsPathBlockedCell (int area, int x, int y) const;
	bool IsEventReachableForObjective (int area, int eventIndex, const MapEventDefinition& eventDefinition) const;
	int PathAffinityScore (int area, int x, int y) const;
	int PrimaryObjectiveEventIndex (int area) const;
	bool ScreenToMapCell (int screenX, int screenY, int& outMapX, int& outMapY) const;
	bool ResolvePreferredTapTarget (int desiredX, int desiredY, int& outX, int& outY) const;
	bool FindReachableDestination (int area, int startX, int startY, int desiredX, int desiredY, int& outX, int& outY) const;
	bool FindPathAStar (int area, int startX, int startY, int targetX, int targetY, std::vector<EPoint>& outPath) const;
	bool StartAutoPathTo (int targetX, int targetY);
	void AdvanceAutoPath ();
	bool HandleAdjustedInteractionAtRouteEnd ();
	void CancelAutoPath ();
	bool IsMoveAnimationActive (int64_t nowMs) const;
	void StartMoveAnimation (int fromX, int fromY, int toX, int toY);
	void DrawLegacyMapObjectOverlays ();
	void UpdateLayoutRects ();
	void SetInteractionHint (int mapX, int mapY);
	void ResetSwipeTracking ();

	EImage imageBackground;
	EImage imageUiLegacy;
	EImage imageUiOverlay1Legacy;
	EImage imageUiOverlay2Legacy;
	EImage imageHelpLegacy;
	EImage imageAreaMap;
	EImage imageAreaOverlay;
	EImage imagePanel;
	EImage imageAreaOpen;
	EImage imageAreaLocked;
	EImage imageAreaSelected;
	EImage imageGridOpen;
	EImage imageGridBlocked;
	EImage imageGridAccent;
	EImage imageGridPlayer;
	EImage imageMapAvatar;
	EImage imageBattleButton;
	EImage imageStatusButton;
	EImage imageSaveButton;
	EImage imageMenuButton;
	EImage imageEventTalk;
	EImage imageEventTreasure;
	EImage imageEventHeal;
	EImage imageEventTrain;
	EImage imageEventShop;
	EImage imageEventWarp;
	EImage imageEventGate;
	EImage imageTutorialLegacy;
	EImage itemIcon[DRAGON_ITEM_COUNT];
	ESound soundClick;
	ESound soundBattleEntry;
	ESound soundTreasure;
	ESound soundHeal;
	ESound soundSave;
	ESound soundStep[3];
	ESound soundBump[4];
	EFont fontHeader;
	EFont fontMain;
	UITransition transition;

	ERect areaRect[DRAGON_ALPHA_AREA_COUNT];
	ERect tileRect[DRAGON_ALPHA_MAP_WIDTH][DRAGON_ALPHA_MAP_HEIGHT];
	ERect gridFrameRect;
	ERect mapTileFrameRect;
	ERect mapAvatarSourceRect;
	int mapTileSize;
	ERect battleRect;
	ERect statusRect;
	ERect actionRect;
	ERect saveRect;
	ERect menuRect;
	ERect dpadUpRect;
	ERect dpadDownRect;
	ERect dpadLeftRect;
	ERect dpadRightRect;
	ERect shopRect;
	ERect shopOfferRect[SHOP_VISIBLE_ROWS];
	ERect shopCloseRect;
	ERect shopPrevRect;
	ERect shopNextRect;
	int selectedArea;
	int mapX;
	int mapY;
	bool moveAnimating;
	int moveFromX;
	int moveFromY;
	int moveToX;
	int moveToY;
	int64_t moveAnimationStartMs;
	int64_t moveAnimationEndMs;
	int visibleStartX;
	int visibleStartY;
	int visibleCols;
	int visibleRows;
	int loadedAreaVisual;
	uint16_t legacyPluginIdByArea[DRAGON_ALPHA_AREA_COUNT];
	uint16_t legacyMapIdByArea[DRAGON_ALPHA_AREA_COUNT];
	bool areaMaskBlocked[DRAGON_ALPHA_AREA_COUNT][DRAGON_ALPHA_MAP_WIDTH * DRAGON_ALPHA_MAP_HEIGHT];
	bool areaBlocked[DRAGON_ALPHA_AREA_COUNT][DRAGON_ALPHA_MAP_WIDTH * DRAGON_ALPHA_MAP_HEIGHT];
	ERect activeMapSourceRect;
	ERect activeOverlaySourceRect;
	bool eventPositionValid[DRAGON_ALPHA_AREA_COUNT][MAP_EVENT_CAPACITY];
	uint8_t eventPositionX[DRAGON_ALPHA_AREA_COUNT][MAP_EVENT_CAPACITY];
	uint8_t eventPositionY[DRAGON_ALPHA_AREA_COUNT][MAP_EVENT_CAPACITY];
	mutable bool legacyEventCacheValid[DRAGON_ALPHA_AREA_COUNT];
	mutable uint16_t legacyEventCachePluginId[DRAGON_ALPHA_AREA_COUNT];
	mutable uint16_t legacyEventCacheMapId[DRAGON_ALPHA_AREA_COUNT];
	mutable int legacyEventCacheCount[DRAGON_ALPHA_AREA_COUNT];
	mutable MapEventDefinition legacyEventCache[DRAGON_ALPHA_AREA_COUNT][MAP_EVENT_CAPACITY];
	int64_t saveBannerTimer;
	int64_t messageTimestamp;
	EString messageText;
	bool promptActive;
	int promptEventArea;
	int promptEventIndex;
	int promptNextAction;
	int promptNextNoAction;
	bool promptFromMove;
	bool promptLegacyScript;
	bool promptTrainingPending;
	int promptTrainingCost;
	EString promptText;
	ERect promptRect;
	ERect promptYesRect;
	ERect promptNoRect;
	bool talkActive;
	int talkEventArea;
	int talkEventIndex;
	int talkNextAction;
	bool talkFromMove;
	bool talkLegacyScript;
	EString talkText;
	ERect talkRect;
	ERect talkOkRect;
	ERect tutorialRect;
	ERect tutorialOkRect;
	ERect endingRect;
	ERect endingContinueRect;
	ERect endingMenuRect;
	struct ShopOffer {
		int itemType;
		int price;
	};
	ShopOffer shopOffers[SHOP_MAX_OFFERS];
	int shopOfferCount;
	int shopScrollOffset;
	bool legacyShopContextActive;
	uint16_t legacyShopPluginId;
	uint16_t legacyShopId;
	bool shopActive;
	bool shopHasPendingScript;
	int shopEventArea;
	int shopEventIndex;
	int shopNextAction;
	bool shopFromMove;
	bool tutorialActive;
	bool endingActive;
	int stepsSinceBattle;
	int stepsSinceRecovery;
	int facingDX;
	int facingDY;
	std::vector<MapScriptAction> legacyScriptActions;
	int legacyScriptEventArea;
	int legacyScriptEventIndex;
	std::vector<EPoint> autoPathCells;
	int autoPathIndex;
	int autoPathTargetX;
	int autoPathTargetY;
	int autoPathRequestedX;
	int autoPathRequestedY;
	bool autoPathAdjusted;
	int64_t autoPathNextMoveTime;
	int64_t lastAutoPathRetargetAt;
	int interactHintX;
	int interactHintY;
	int64_t interactHintUntil;
	int64_t lastTapAt;
	int64_t lastBumpSoundAt;
	int lastTapMapX;
	int lastTapMapY;
	bool swipeTracking;
	int swipeStartX;
	int swipeStartY;
	int swipeLastX;
	int swipeLastY;
	int64_t swipeLastMoveTime;
	int64_t touchUpGuardUntil;
	bool touchHandledThisSequence;
	bool touchMovedThisSequence;
};

#endif // DRAGON_ALPHA_WORLDMAP_H

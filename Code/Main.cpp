/**
 * @file Main.cpp
 * @brief Application entry/runtime bootstrap and validation harness implementation.
 */
#include "Global.h"
#include "LayoutUtil.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace {

#if defined(DRAGON_TEST)

struct DragonValidationRun {
	bool active = false;
	bool captureRequested = false;
	bool captureCompleted = false;
	bool captureSucceeded = false;
	bool stepInputInjected = false;
	bool stepTelemetryValidated = false;
	int frameCount = 0;
	int captureFrame = 0;
	int captureWidth = 0;
	int captureHeight = 0;
	int captureDelayMs = 2600;
	int interactionDelayMs = 300;
	int reportedTextDrawCount = 0;
	int matchedTextDrawCount = 0;
	int stepIndex = 0;
	int stepCount = 1;
	int timeoutMs = 7000;
	int64_t firstDrawMilliseconds = 0;
	int64_t stepStartedMilliseconds = 0;
	std::string caseId;
	std::string outputDir;
	std::string capturePath;
	std::string reportPath;
	std::string logPath;
	std::string saveDir;
	std::string sceneName;
	std::string entryMode = "boot";
	std::string validationMode = "pending";
	std::string error;
	std::string roundTripMapText;
	std::string roundTripPositionText;
};

static DragonValidationRun gDragonValidation;
static void DragonValidationFail (const char* message);
static bool DragonValidateShopTelemetry (int expectedOffset);

static bool DragonHasArg (const char* flag) {
	if(flag == nullptr || flag[0] == '\0')
		return false;
	for(int i = 1; i < ESystem::GetArgCount(); i++) {
		const char* value = ESystem::GetArgValue(i);
		if(value != nullptr && std::strcmp(value, flag) == 0)
			return true;
	}
	return false;
}

static const char* DragonFindArgValue (const char* flag) {
	if(flag == nullptr || flag[0] == '\0')
		return nullptr;
	for(int i = 1; i + 1 < ESystem::GetArgCount(); i++) {
		const char* value = ESystem::GetArgValue(i);
		if(value != nullptr && std::strcmp(value, flag) == 0)
			return ESystem::GetArgValue(i + 1);
	}
	return nullptr;
}

static int DragonParsePositiveIntArg (const char* flag, int fallback) {
	const char* value = DragonFindArgValue(flag);
	if(value == nullptr || value[0] == '\0')
		return fallback;

	char* end = nullptr;
	const long parsed = std::strtol(value, &end, 10);
	if(end == value || end == nullptr || *end != '\0' || parsed <= 0 || parsed > INT_MAX)
		return fallback;
	return (int)parsed;
}

static bool DragonEnsureDirectoryExists (const std::string& path) {
	if(path.empty())
		return false;

	std::string normalized = path;
	while(normalized.length() > 1 && normalized.back() == '/')
		normalized.pop_back();
	if(normalized.empty())
		return false;
	if(normalized == "/")
		return true;

	struct stat st;
	if(stat(normalized.c_str(), &st) == 0)
		return S_ISDIR(st.st_mode) != 0;

	size_t searchFrom = normalized[0] == '/' ? 1 : 0;
	while(true) {
		size_t slash = normalized.find('/', searchFrom);
		std::string partial = slash == std::string::npos ? normalized : normalized.substr(0, slash);
		if(!partial.empty()) {
			if(stat(partial.c_str(), &st) != 0) {
				if(mkdir(partial.c_str(), 0755) != 0 && errno != EEXIST)
					return false;
			}
			else if(!S_ISDIR(st.st_mode))
				return false;
		}
		if(slash == std::string::npos)
			break;
		searchFrom = slash + 1;
	}

	return stat(normalized.c_str(), &st) == 0 && S_ISDIR(st.st_mode) != 0;
}

static std::string DragonEscapeJSON (const std::string& text) {
	std::string escaped;
	escaped.reserve(text.length() + 16);
	for(char c : text) {
		switch(c) {
			case '\\': escaped += "\\\\"; break;
			case '"': escaped += "\\\""; break;
			case '\n': escaped += "\\n"; break;
			case '\r': escaped += "\\r"; break;
			case '\t': escaped += "\\t"; break;
			default:
				if((unsigned char)c < 32) {
					char buffer[7];
					std::snprintf(buffer, sizeof(buffer), "\\u%04x", (unsigned char)c);
					escaped += buffer;
				}
				else
					escaped += c;
				break;
		}
	}
	return escaped;
}

static void DragonWriteRectJSON (FILE* file, const char* key, const ERect& rect, bool trailingComma) {
	if(file == nullptr || key == nullptr)
		return;
	std::fprintf(
		file,
		"  \"%s\": { \"x\": %d, \"y\": %d, \"width\": %d, \"height\": %d }%s\n",
		key,
		rect.x,
		rect.y,
		rect.width,
		rect.height,
		trailingComma ? "," : ""
	);
}

static void DragonWriteSampledTextDrawsJSON (FILE* file, int limit) {
	if(file == nullptr || limit <= 0)
		return;

	std::vector<std::string> sampledTexts;
	const int count = ESystem::GetReportedTextDrawCount();
	for(int i = count - 1; i >= 0 && (int)sampledTexts.size() < limit; i--) {
		EString text;
		ERect rect;
		if(ESystem::GetReportedTextDraw(i, text, rect) == false || text.IsEmpty())
			continue;
		const std::string value = text.String();
		bool seen = false;
		for(const std::string& existing : sampledTexts) {
			if(existing == value) {
				seen = true;
				break;
			}
		}
		if(seen == false)
			sampledTexts.push_back(value);
	}
	std::reverse(sampledTexts.begin(), sampledTexts.end());

	std::fprintf(file, "  \"sampledTextDraws\": [");
	for(size_t i = 0; i < sampledTexts.size(); i++) {
		if(i > 0)
			std::fprintf(file, ", ");
		std::fprintf(file, "\"%s\"", DragonEscapeJSON(sampledTexts[i]).c_str());
	}
	std::fprintf(file, "],\n");
}

static bool DragonRectInsideRect (const ERect& outer, const ERect& inner) {
	return inner.width > 0
		&& inner.height > 0
		&& inner.x >= outer.x
		&& inner.y >= outer.y
		&& inner.GetRight() <= outer.GetRight()
		&& inner.GetBottom() <= outer.GetBottom();
}

static bool DragonRectNearRect (const ERect& actual, const ERect& expected, int toleranceX, int toleranceY) {
	return std::abs(actual.x - expected.x) <= std::max(0, toleranceX)
		&& std::abs(actual.y - expected.y) <= std::max(0, toleranceY)
		&& std::abs(actual.width - expected.width) <= std::max(0, toleranceX)
		&& std::abs(actual.height - expected.height) <= std::max(0, toleranceY);
}

static bool DragonFindReportedTextLineContaining (const char* fragment, EString* outText = nullptr, ERect* outRect = nullptr) {
	if(fragment == nullptr || fragment[0] == '\0')
		return false;

	const int count = ESystem::GetReportedTextDrawCount();
	gDragonValidation.reportedTextDrawCount = count;
	const EString needle(fragment);
	for(int i = count - 1; i >= 0; i--) {
		EString text;
		ERect rect;
		if(ESystem::GetReportedTextDraw(i, text, rect) == false)
			continue;
		if(text.Contains(needle) == false)
			continue;
		if(outText != nullptr)
			*outText = text;
		if(outRect != nullptr)
			*outRect = rect;
		return true;
	}
	return false;
}

static bool DragonFindReportedTextContaining (const char* fragment, ERect* outRect = nullptr) {
	return DragonFindReportedTextLineContaining(fragment, nullptr, outRect);
}

static int DragonCountReportedTextContaining (const char* fragment) {
	if(fragment == nullptr || fragment[0] == '\0')
		return 0;

	int matches = 0;
	const int count = ESystem::GetReportedTextDrawCount();
	const EString needle(fragment);
	for(int i = count - 1; i >= 0; i--) {
		EString text;
		ERect rect;
		if(ESystem::GetReportedTextDraw(i, text, rect) == false)
			continue;
		if(text.Contains(needle))
			matches++;
	}
	return matches;
}

static bool DragonCaseIs (const char* caseId) {
	return caseId != nullptr && gDragonValidation.caseId == caseId;
}

static bool DragonValidateSplashTelemetry () {
	const ERect safeRect = ESystem::GetSafeRect();
	const LegacyCanvas canvas = MakeLegacyCanvas(safeRect, 800, 600);
	ERect titleRect;
	ERect creditRect;
	ERect modernRect;
	ERect okRect;
	ERect newGameRect;
	ERect openGameRect;
	ERect soundRect;
	ERect musicRect;
	ERect fullscreenRect;
	ERect quitRect;
	const bool hasTitle = DragonFindReportedTextContaining("ABOUT DRAGON ALPHA", &titleRect);
	const bool hasCredit = DragonFindReportedTextContaining("Dracosoft", &creditRect);
	const bool hasModern = DragonFindReportedTextContaining("standalone", &modernRect);
	const bool hasOk = DragonFindReportedTextContaining("OK", &okRect);
	const bool hasNewGame = DragonFindReportedTextContaining("NEW GAME", &newGameRect);
	const bool hasOpenGame = DragonFindReportedTextContaining("OPEN GAME...", &openGameRect);
	const bool hasSound = DragonFindReportedTextContaining("SOUND", &soundRect);
	const bool hasMusic = DragonFindReportedTextContaining("MUSIC", &musicRect);
	const bool hasFullscreen = DragonFindReportedTextContaining("FULLSCREEN", &fullscreenRect);
	const bool hasQuit = DragonFindReportedTextContaining("QUIT", &quitRect);

	const ERect expectedNewGameRect = LegacyRect(canvas, 8, 329, 296, 30);
	const ERect expectedOpenGameRect = LegacyRect(canvas, 8, 370, 296, 30);
	const ERect expectedSoundRect = LegacyRect(canvas, 8, 428, 296, 30);
	const ERect expectedMusicRect = LegacyRect(canvas, 8, 469, 296, 30);
	const ERect expectedFullscreenRect = LegacyRect(canvas, 8, 510, 296, 30);
	const ERect expectedQuitRect = LegacyRect(canvas, 8, 568, 296, 30);
	const int rowXTolerance = std::max(2, expectedNewGameRect.width / 32);
	const int rowYTolerance = std::max(2, expectedNewGameRect.height / 5);
	const bool newGameAligned = hasNewGame
		&& DragonRectNearRect(newGameRect, expectedNewGameRect, rowXTolerance, rowYTolerance);
	const bool openGameAligned = hasOpenGame
		&& DragonRectNearRect(openGameRect, expectedOpenGameRect, rowXTolerance, rowYTolerance);
	const bool soundAligned = hasSound
		&& DragonRectNearRect(soundRect, expectedSoundRect, rowXTolerance, rowYTolerance);
	const bool musicAligned = hasMusic
		&& DragonRectNearRect(musicRect, expectedMusicRect, rowXTolerance, rowYTolerance);
	const bool fullscreenAligned = hasFullscreen
		&& DragonRectNearRect(fullscreenRect, expectedFullscreenRect, rowXTolerance, rowYTolerance);
	const bool quitAligned = hasQuit
		&& DragonRectNearRect(quitRect, expectedQuitRect, rowXTolerance, rowYTolerance);

	int matched = 0;
	if(hasTitle) matched++;
	if(hasCredit) matched++;
	if(hasModern) matched++;
	if(hasOk) matched++;
	if(newGameAligned) matched++;
	if(openGameAligned) matched++;
	if(soundAligned) matched++;
	if(musicAligned) matched++;
	if(fullscreenAligned) matched++;
	if(quitAligned) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	if(hasTitle == false
		|| hasCredit == false
		|| hasModern == false
		|| hasOk == false
		|| newGameAligned == false
		|| openGameAligned == false
		|| soundAligned == false
		|| musicAligned == false
		|| fullscreenAligned == false
		|| quitAligned == false) {
		return false;
	}
	if(DragonRectInsideRect(safeRect, titleRect) == false
		|| DragonRectInsideRect(safeRect, creditRect) == false
		|| DragonRectInsideRect(safeRect, modernRect) == false
		|| DragonRectInsideRect(safeRect, okRect) == false
		|| DragonRectInsideRect(safeRect, newGameRect) == false
		|| DragonRectInsideRect(safeRect, openGameRect) == false
		|| DragonRectInsideRect(safeRect, soundRect) == false
		|| DragonRectInsideRect(safeRect, musicRect) == false
		|| DragonRectInsideRect(safeRect, fullscreenRect) == false
		|| DragonRectInsideRect(safeRect, quitRect) == false) {
		return false;
	}
	if(titleRect.y >= okRect.y)
		return false;
	if(!(newGameRect.y < openGameRect.y
		&& openGameRect.y < soundRect.y
		&& soundRect.y < musicRect.y
		&& musicRect.y < fullscreenRect.y
		&& fullscreenRect.y < quitRect.y)) {
		return false;
	}
	return true;
}

static bool DragonValidateNewGameTelemetry () {
	ERect backRect;
	ERect slot1Rect;
	ERect slot2Rect;
	ERect slot3Rect;
	ERect actionRect;
	ERect hintRect;
	const bool hasBack = DragonFindReportedTextContaining("BACK", &backRect);
	const bool hasSlot1 = DragonFindReportedTextContaining("SLOT 1", &slot1Rect);
	const bool hasSlot2 = DragonFindReportedTextContaining("SLOT 2", &slot2Rect);
	const bool hasSlot3 = DragonFindReportedTextContaining("SLOT 3", &slot3Rect);
	const bool hasAction = DragonFindReportedTextContaining("NEW GAME", &actionRect);
	const bool hasHint = DragonFindReportedTextContaining("No saves found", &hintRect);

	int matched = 0;
	if(hasBack) matched++;
	if(hasSlot1) matched++;
	if(hasSlot2) matched++;
	if(hasSlot3) matched++;
	if(hasAction) matched++;
	if(hasHint) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	if(hasBack == false || hasSlot1 == false || hasSlot2 == false || hasSlot3 == false || hasAction == false || hasHint == false)
		return false;
	return true;
}

static bool DragonValidateNewAvatarTelemetry (const char* expectedSelectedClass) {
	ERect headerRect;
	ERect slotRect;
	ERect soldierRect;
	ERect mageRect;
	ERect thiefRect;
	ERect nameRect;
	ERect beginRect;
	ERect backRect;
	ERect selectedRect;
	const bool hasHeader = DragonFindReportedTextContaining("NEW GAME - SELECT CLASS", &headerRect);
	const bool hasSlot = DragonFindReportedTextContaining("Save Slot 1", &slotRect);
	const bool hasSoldier = DragonFindReportedTextContaining("Soldier", &soldierRect);
	const bool hasMage = DragonFindReportedTextContaining("Mage", &mageRect);
	const bool hasThief = DragonFindReportedTextContaining("Thief", &thiefRect);
	const bool hasName = DragonFindReportedTextContaining("Name:", &nameRect);
	const bool hasBegin = DragonFindReportedTextContaining("BEGIN GAME", &beginRect);
	const bool hasBack = DragonFindReportedTextContaining("BACK", &backRect);
	const EString selectedLabel = EString().Format("Selected class: %s", expectedSelectedClass != nullptr ? expectedSelectedClass : "");
	const bool hasSelected = DragonFindReportedTextContaining(selectedLabel, &selectedRect);

	int matched = 0;
	if(hasHeader) matched++;
	if(hasSlot) matched++;
	if(hasSoldier) matched++;
	if(hasMage) matched++;
	if(hasThief) matched++;
	if(hasName) matched++;
	if(hasBegin) matched++;
	if(hasBack) matched++;
	if(hasSelected) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	if(hasHeader == false
		|| hasSlot == false
		|| hasSoldier == false
		|| hasMage == false
		|| hasThief == false
		|| hasName == false
		|| hasBegin == false
		|| hasBack == false
		|| hasSelected == false) {
		return false;
	}
	return true;
}

static bool DragonValidateWorldMapTelemetryInternal (bool requireObjective) {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect mapRect;
	ERect positionRect;
	ERect heroRect;
	ERect levelRect;
	ERect healthRect;
	ERect avatarRect;
	ERect openRect;
	ERect saveRect;
	ERect statusRect;
	ERect actionRect;
	ERect battleRect;
	ERect objectiveRect;
	const bool hasMap = DragonFindReportedTextContaining("Map:", &mapRect);
	const bool hasPosition = DragonFindReportedTextContaining("Position:", &positionRect);
	const bool hasHero = DragonFindReportedTextContaining("Hero:", &heroRect);
	const bool hasLevel = DragonFindReportedTextContaining("Lv ", &levelRect);
	const bool hasHealth = DragonFindReportedTextContaining("HP ", &healthRect);
	const bool hasAvatar = DragonFindReportedTextContaining("Map avatar bounds", &avatarRect);
	const bool hasOpen = DragonFindReportedTextContaining("OPEN GAME...", &openRect);
	const bool hasSave = DragonFindReportedTextContaining("SAVE GAME", &saveRect);
	const bool hasStatus = DragonFindReportedTextContaining("EQUIPMENT", &statusRect);
	const bool hasAction = DragonFindReportedTextContaining("INTERACT", &actionRect);
	const bool hasBattle = DragonFindReportedTextContaining("BATTLE", &battleRect);
	const bool hasObjective = requireObjective == false || DragonFindReportedTextContaining("Objective:", &objectiveRect);

	int matched = 0;
	if(hasMap) matched++;
	if(hasPosition) matched++;
	if(hasHero) matched++;
	if(hasLevel) matched++;
	if(hasHealth) matched++;
	if(hasAvatar) matched++;
	if(hasOpen) matched++;
	if(hasSave) matched++;
	if(hasStatus) matched++;
	if(hasAction) matched++;
	if(hasBattle) matched++;
	if(hasObjective) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	if(hasMap == false
		|| hasPosition == false
		|| hasHero == false
		|| hasLevel == false
		|| hasHealth == false
		|| hasAvatar == false
		|| hasOpen == false
		|| hasSave == false
		|| hasStatus == false
		|| hasAction == false
		|| hasBattle == false
		|| hasObjective == false) {
		return false;
	}
	if(DragonRectInsideRect(safeRect, openRect) == false
		|| DragonRectInsideRect(safeRect, saveRect) == false
		|| DragonRectInsideRect(safeRect, statusRect) == false
		|| DragonRectInsideRect(safeRect, actionRect) == false
		|| DragonRectInsideRect(safeRect, battleRect) == false
		|| DragonRectInsideRect(safeRect, avatarRect) == false
		|| DragonRectInsideRect(safeRect, positionRect) == false
		|| (requireObjective && DragonRectInsideRect(safeRect, objectiveRect) == false)) {
		return false;
	}
	const int minAvatarSize = std::max(12, safeRect.height / 56);
	if(avatarRect.width < minAvatarSize || avatarRect.height < minAvatarSize)
		return false;
	return true;
}

static bool DragonValidateWorldMapTelemetry () {
	return DragonValidateWorldMapTelemetryInternal(true);
}

static bool DragonValidateWorldMapMessageTelemetry () {
	return DragonValidateWorldMapTelemetryInternal(false);
}

static bool DragonValidateWorldMapStateTelemetry (const char* expectedPosition = nullptr, const char* expectedLevel = nullptr, const char* expectedHealth = nullptr, const char* expectedGold = nullptr, const char* expectedMessage = nullptr, bool requireObjective = true) {
	const bool baseValid = requireObjective ? DragonValidateWorldMapTelemetry() : DragonValidateWorldMapMessageTelemetry();
	int matched = 0;
	bool valid = true;

	if(expectedPosition != nullptr && expectedPosition[0] != '\0') {
		const bool hasPosition = DragonFindReportedTextContaining(expectedPosition);
		matched += hasPosition ? 1 : 0;
		valid = valid && hasPosition;
	}
	if(expectedLevel != nullptr && expectedLevel[0] != '\0') {
		const bool hasLevel = DragonFindReportedTextContaining(expectedLevel);
		matched += hasLevel ? 1 : 0;
		valid = valid && hasLevel;
	}
	if(expectedHealth != nullptr && expectedHealth[0] != '\0') {
		const bool hasHealth = DragonFindReportedTextContaining(expectedHealth);
		matched += hasHealth ? 1 : 0;
		valid = valid && hasHealth;
	}
	if(expectedGold != nullptr && expectedGold[0] != '\0') {
		const bool hasGold = DragonFindReportedTextContaining(expectedGold);
		matched += hasGold ? 1 : 0;
		valid = valid && hasGold;
	}
	if(expectedMessage != nullptr && expectedMessage[0] != '\0') {
		const bool hasMessage = DragonFindReportedTextContaining(expectedMessage);
		matched += hasMessage ? 1 : 0;
		valid = valid && hasMessage;
	}

	if(baseValid)
		gDragonValidation.matchedTextDrawCount += matched;
	return baseValid && valid;
}

static bool DragonReadWorldMapPositionTelemetry (int& outX, int& outY) {
	EString positionLine;
	if(DragonFindReportedTextLineContaining("Position:", &positionLine) == false)
		return false;
	return std::sscanf((const char*)positionLine, "Position: %d,%d", &outX, &outY) == 2;
}

static bool DragonReadWorldMapEventPositionTelemetry (int eventIndex, int& outX, int& outY) {
	EString eventLine;
	const EString pendingPrefix = EString().Format("Pending event %d:", eventIndex);
	const EString visiblePrefix = EString().Format("Event %d:", eventIndex);
	int parsedIndex = -1;
	if(DragonFindReportedTextLineContaining((const char*)pendingPrefix, &eventLine)
		&& std::sscanf((const char*)eventLine, "Pending event %d: %*s %d,%d", &parsedIndex, &outX, &outY) == 3
		&& parsedIndex == eventIndex) {
		return true;
	}
	if(DragonFindReportedTextLineContaining((const char*)visiblePrefix, &eventLine)
		&& std::sscanf((const char*)eventLine, "Event %d: %*s %d,%d", &parsedIndex, &outX, &outY) == 3
		&& parsedIndex == eventIndex) {
		return true;
	}
	return false;
}

static bool DragonReadWorldMapEventPositionByTypeTelemetry (const char* eventType, int& outEventIndex, int& outX, int& outY) {
	if(eventType == nullptr || eventType[0] == '\0')
		return false;

	const int count = ESystem::GetReportedTextDrawCount();
	for(int i = count - 1; i >= 0; i--) {
		EString text;
		ERect rect;
		if(ESystem::GetReportedTextDraw(i, text, rect) == false)
			continue;

		int parsedIndex = -1;
		int parsedX = 0;
		int parsedY = 0;
		char parsedType[64] = {};
		if(std::sscanf((const char*)text, "Pending event %d: %63s %d,%d", &parsedIndex, parsedType, &parsedX, &parsedY) == 4
			|| std::sscanf((const char*)text, "Event %d: %63s %d,%d", &parsedIndex, parsedType, &parsedX, &parsedY) == 4) {
			if(std::strcmp(parsedType, eventType) == 0) {
				outEventIndex = parsedIndex;
				outX = parsedX;
				outY = parsedY;
				return true;
			}
		}
	}
	return false;
}

static bool DragonReadWorldMapGoldTelemetry (int& outGold) {
	EString goldLine;
	if(DragonFindReportedTextLineContaining("Gold ", &goldLine) == false)
		return false;
	int parsedGold = 0;
	if(std::sscanf((const char*)goldLine, "Gold %d", &parsedGold) != 1)
		return false;
	outGold = parsedGold;
	return true;
}

static bool DragonValidateWorldMapNearPositionTelemetry (int targetX, int targetY, int maxDistance, const char* expectedLevel = nullptr, const char* expectedHealth = nullptr, const char* expectedGold = nullptr, const char* expectedMessage = nullptr) {
	const bool baseValid = DragonValidateWorldMapStateTelemetry(nullptr, expectedLevel, expectedHealth, expectedGold, expectedMessage, false);
	int actualX = 0;
	int actualY = 0;
	const bool hasPosition = DragonReadWorldMapPositionTelemetry(actualX, actualY);
	const bool withinDistance = hasPosition && std::abs(actualX - targetX) + std::abs(actualY - targetY) <= std::max(0, maxDistance);
	if(baseValid)
		gDragonValidation.matchedTextDrawCount += withinDistance ? 1 : 0;
	return baseValid && withinDistance;
}

static bool DragonValidateWorldMapPendingEventTelemetry (int eventIndex, const char* expectedLevel = nullptr, const char* expectedHealth = nullptr, const char* expectedGold = nullptr) {
	const bool baseValid = DragonValidateWorldMapStateTelemetry(nullptr, expectedLevel, expectedHealth, expectedGold, nullptr, false);
	int eventX = 0;
	int eventY = 0;
	const bool hasEvent = DragonReadWorldMapEventPositionTelemetry(eventIndex, eventX, eventY);
	if(baseValid)
		gDragonValidation.matchedTextDrawCount += hasEvent ? 1 : 0;
	return baseValid && hasEvent;
}

static bool DragonValidateWorldMapGoldAboveTelemetry (int minimumGoldExclusive, const char* expectedLevel = nullptr, const char* expectedHealth = nullptr) {
	const bool baseValid = DragonValidateWorldMapStateTelemetry(nullptr, expectedLevel, expectedHealth, nullptr, nullptr, false);
	int gold = 0;
	const bool hasGold = DragonReadWorldMapGoldTelemetry(gold);
	const bool goldAbove = hasGold && gold > minimumGoldExclusive;
	if(baseValid)
		gDragonValidation.matchedTextDrawCount += goldAbove ? 1 : 0;
	return baseValid && goldAbove;
}

static bool DragonValidateWorldMapRouteTelemetry (const char* expectedTarget = nullptr, const char* expectedRequested = nullptr) {
	ERect targetRect;
	ERect requestedRect;
	ERect playerRect;
	const bool baseValid = DragonValidateWorldMapTelemetry();
	const bool hasTarget = DragonFindReportedTextContaining(
		(expectedTarget != nullptr && expectedTarget[0] != '\0') ? expectedTarget : "Route target:",
		&targetRect
	);
	const bool hasRequested = DragonFindReportedTextContaining(
		(expectedRequested != nullptr && expectedRequested[0] != '\0') ? expectedRequested : "Route requested:",
		&requestedRect
	);
	const bool hasPlayer = DragonFindReportedTextContaining("Player tile:", &playerRect);
	if(baseValid)
		gDragonValidation.matchedTextDrawCount += (hasTarget ? 1 : 0) + (hasRequested ? 1 : 0) + (hasPlayer ? 1 : 0);
	return baseValid && hasTarget && hasRequested && hasPlayer;
}

static bool DragonValidateWorldMapNoRouteTelemetry (const char* expectedMessage = nullptr) {
	const bool baseValid = DragonValidateWorldMapStateTelemetry(nullptr, nullptr, nullptr, nullptr, expectedMessage, false);
	const bool routeTargetCleared = DragonFindReportedTextContaining("Route target:") == false;
	const bool routeRequestedCleared = DragonFindReportedTextContaining("Route requested:") == false;
	const bool routeCleared = routeTargetCleared && routeRequestedCleared;
	if(baseValid)
		gDragonValidation.matchedTextDrawCount += routeCleared ? 1 : 0;
	return baseValid && routeCleared;
}

static bool DragonValidateWorldMapPromptTelemetry (const char* expectedFragment = nullptr) {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect promptRect;
	ERect yesRect;
	ERect noRect;
	EString promptLine;
	const bool baseValid = DragonValidateWorldMapMessageTelemetry();
	const bool hasPrompt = DragonFindReportedTextLineContaining("Prompt:", &promptLine, &promptRect);
	const bool hasPromptBody = hasPrompt && promptLine.GetLength() > (int)std::strlen("Prompt: ");
	const bool hasExpected = expectedFragment == nullptr || expectedFragment[0] == '\0' || promptLine.Contains(EString(expectedFragment));
	const bool hasYes = DragonFindReportedTextContaining("YES", &yesRect);
	const bool hasNo = DragonFindReportedTextContaining("NO", &noRect);
	if(baseValid)
		gDragonValidation.matchedTextDrawCount += (hasPrompt ? 1 : 0) + (hasYes ? 1 : 0) + (hasNo ? 1 : 0);
	return baseValid
		&& hasPrompt
		&& hasPromptBody
		&& hasExpected
		&& hasYes
		&& hasNo
		&& DragonRectInsideRect(safeRect, promptRect)
		&& DragonRectInsideRect(safeRect, yesRect)
		&& DragonRectInsideRect(safeRect, noRect);
}

static bool DragonValidateWorldMapPromptOrShopTelemetry () {
	if(DragonValidateWorldMapPromptTelemetry())
		return true;
	return DragonValidateShopTelemetry(0);
}

static bool DragonValidateWorldMapTalkTelemetry (const char* expectedFragment = nullptr) {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect talkRect;
	ERect continueRect;
	EString talkLine;
	const bool baseValid = DragonValidateWorldMapMessageTelemetry();
	const bool hasTalk = DragonFindReportedTextLineContaining("Talk:", &talkLine, &talkRect);
	const bool hasTalkBody = hasTalk && talkLine.GetLength() > (int)std::strlen("Talk: ");
	const bool hasExpected = expectedFragment == nullptr || expectedFragment[0] == '\0' || talkLine.Contains(EString(expectedFragment));
	const bool hasContinue = DragonFindReportedTextContaining("CONTINUE", &continueRect);
	if(baseValid)
		gDragonValidation.matchedTextDrawCount += (hasTalk ? 1 : 0) + (hasContinue ? 1 : 0);
	return baseValid
		&& hasTalk
		&& hasTalkBody
		&& hasExpected
		&& hasContinue
		&& DragonRectInsideRect(safeRect, talkRect)
		&& DragonRectInsideRect(safeRect, continueRect);
}

static bool DragonValidateWorldMapSavedTelemetry () {
	ERect savedRect;
	const bool baseValid = DragonValidateWorldMapTelemetry();
	const bool hasSaved = DragonFindReportedTextContaining("Saved.", &savedRect);
	if(baseValid)
		gDragonValidation.matchedTextDrawCount += hasSaved ? 1 : 0;
	return baseValid && hasSaved;
}

static bool DragonValidateWorldMapPostShopTelemetry () {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect mapRect;
	ERect positionRect;
	ERect heroRect;
	ERect levelRect;
	ERect healthRect;
	ERect openRect;
	ERect saveRect;
	ERect statusRect;
	ERect actionRect;
	ERect battleRect;
	const bool hasMap = DragonFindReportedTextContaining("Map: Eleusis", &mapRect);
	const bool hasPosition = DragonFindReportedTextContaining("Position: 23,59", &positionRect);
	const bool hasHero = DragonFindReportedTextContaining("Hero: ARIN", &heroRect);
	const bool hasLevel = DragonFindReportedTextContaining("Lv 1", &levelRect);
	const bool hasHealth = DragonFindReportedTextContaining("HP 125/125", &healthRect);
	const bool hasOpen = DragonFindReportedTextContaining("OPEN GAME...", &openRect);
	const bool hasSave = DragonFindReportedTextContaining("SAVE GAME", &saveRect);
	const bool hasStatus = DragonFindReportedTextContaining("EQUIPMENT", &statusRect);
	const bool hasAction = DragonFindReportedTextContaining("INTERACT", &actionRect);
	const bool hasBattle = DragonFindReportedTextContaining("BATTLE", &battleRect);
	const bool merchantClosed = DragonFindReportedTextContaining("MERCHANT") == false
		&& DragonFindReportedTextContaining("DONE") == false;

	int matched = 0;
	if(hasMap) matched++;
	if(hasPosition) matched++;
	if(hasHero) matched++;
	if(hasLevel) matched++;
	if(hasHealth) matched++;
	if(hasOpen) matched++;
	if(hasSave) matched++;
	if(hasStatus) matched++;
	if(hasAction) matched++;
	if(hasBattle) matched++;
	if(merchantClosed) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	return hasMap
		&& hasPosition
		&& hasHero
		&& hasLevel
		&& hasHealth
		&& hasOpen
		&& hasSave
		&& hasStatus
		&& hasAction
		&& hasBattle
		&& merchantClosed
		&& DragonRectInsideRect(safeRect, openRect)
		&& DragonRectInsideRect(safeRect, saveRect)
		&& DragonRectInsideRect(safeRect, statusRect)
		&& DragonRectInsideRect(safeRect, actionRect)
		&& DragonRectInsideRect(safeRect, battleRect)
		&& DragonRectInsideRect(safeRect, positionRect);
}

static bool DragonCaptureRoundTripWorldMapState () {
	EString mapText;
	EString positionText;
	if(DragonFindReportedTextLineContaining("Map:", &mapText) == false)
		return false;
	if(DragonFindReportedTextLineContaining("Position:", &positionText) == false)
		return false;
	gDragonValidation.roundTripMapText = mapText.String();
	gDragonValidation.roundTripPositionText = positionText.String();
	return true;
}

static bool DragonValidateWorldMapRoundTripTelemetry () {
	if(DragonValidateWorldMapTelemetry() == false)
		return false;

	EString mapText;
	EString positionText;
	if(DragonFindReportedTextLineContaining("Map:", &mapText) == false)
		return false;
	if(DragonFindReportedTextLineContaining("Position:", &positionText) == false)
		return false;
	if(gDragonValidation.roundTripMapText.empty() == false && mapText.String() != gDragonValidation.roundTripMapText)
		return false;
	if(gDragonValidation.roundTripPositionText.empty() == false && positionText.String() != gDragonValidation.roundTripPositionText)
		return false;
	return true;
}

static bool DragonValidateWorldMapProgressionTelemetry () {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect mapRect;
	ERect positionRect;
	ERect heroRect;
	ERect levelRect;
	ERect healthRect;
	ERect openRect;
	ERect saveRect;
	ERect statusRect;
	ERect actionRect;
	ERect battleRect;
	ERect maskRect;
	ERect flagsRect;
	ERect peakFlagsRect;
	const bool hasMap = DragonFindReportedTextContaining("Map: Eleusis Caves", &mapRect);
	const bool hasPosition = DragonFindReportedTextContaining("Position: 30,18", &positionRect);
	const bool hasHero = DragonFindReportedTextContaining("Hero: LYRA", &heroRect);
	const bool hasLevel = DragonFindReportedTextContaining("Lv 7", &levelRect);
	const bool hasHealth = DragonFindReportedTextContaining("HP ", &healthRect);
	const bool hasOpen = DragonFindReportedTextContaining("OPEN GAME...", &openRect);
	const bool hasSave = DragonFindReportedTextContaining("SAVE GAME", &saveRect);
	const bool hasStatus = DragonFindReportedTextContaining("EQUIPMENT", &statusRect);
	const bool hasAction = DragonFindReportedTextContaining("INTERACT", &actionRect);
	const bool hasBattle = DragonFindReportedTextContaining("BATTLE", &battleRect);
	const bool hasMask = DragonFindReportedTextContaining("Discovered mask: 0x3F", &maskRect);
	const bool hasFlags = DragonFindReportedTextContaining("Area 3 flags: 0x0006", &flagsRect);
	const bool hasPeakFlags = DragonFindReportedTextContaining("Area 5 flags: 0x0066", &peakFlagsRect);

	int matched = 0;
	if(hasMap) matched++;
	if(hasPosition) matched++;
	if(hasHero) matched++;
	if(hasLevel) matched++;
	if(hasHealth) matched++;
	if(hasOpen) matched++;
	if(hasSave) matched++;
	if(hasStatus) matched++;
	if(hasAction) matched++;
	if(hasBattle) matched++;
	if(hasMask) matched++;
	if(hasFlags) matched++;
	if(hasPeakFlags) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	return hasMap
		&& hasPosition
		&& hasHero
		&& hasLevel
		&& hasHealth
		&& hasOpen
		&& hasSave
		&& hasStatus
		&& hasAction
		&& hasBattle
		&& hasMask
		&& hasFlags
		&& hasPeakFlags
		&& DragonRectInsideRect(safeRect, openRect)
		&& DragonRectInsideRect(safeRect, saveRect)
		&& DragonRectInsideRect(safeRect, statusRect)
		&& DragonRectInsideRect(safeRect, actionRect)
		&& DragonRectInsideRect(safeRect, battleRect)
		&& DragonRectInsideRect(safeRect, positionRect)
		&& DragonRectInsideRect(safeRect, maskRect)
		&& DragonRectInsideRect(safeRect, flagsRect)
		&& DragonRectInsideRect(safeRect, peakFlagsRect);
}

static bool DragonValidateWorldMapSavedProgressionTelemetry () {
	ERect savedRect;
	const bool baseValid = DragonValidateWorldMapProgressionTelemetry();
	const bool hasSaved = DragonFindReportedTextContaining("Saved.", &savedRect);
	if(baseValid)
		gDragonValidation.matchedTextDrawCount += hasSaved ? 1 : 0;
	return baseValid && hasSaved;
}

static bool DragonValidateWorldMapProgressionLoadoutTelemetry () {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect mapRect;
	ERect positionRect;
	ERect heroRect;
	ERect levelRect;
	ERect healthRect;
	ERect openRect;
	ERect saveRect;
	ERect statusRect;
	ERect actionRect;
	ERect battleRect;
	ERect maskRect;
	ERect flagsRect;
	ERect peakFlagsRect;
	const bool hasMap = DragonFindReportedTextContaining("Map: Eleusis Caves", &mapRect);
	const bool hasPosition = DragonFindReportedTextContaining("Position: 30,18", &positionRect);
	const bool hasHero = DragonFindReportedTextContaining("Hero: ARIN", &heroRect);
	const bool hasLevel = DragonFindReportedTextContaining("Lv 18", &levelRect);
	const bool hasHealth = DragonFindReportedTextContaining("HP ", &healthRect);
	const bool hasOpen = DragonFindReportedTextContaining("OPEN GAME...", &openRect);
	const bool hasSave = DragonFindReportedTextContaining("SAVE GAME", &saveRect);
	const bool hasStatus = DragonFindReportedTextContaining("EQUIPMENT", &statusRect);
	const bool hasAction = DragonFindReportedTextContaining("INTERACT", &actionRect);
	const bool hasBattle = DragonFindReportedTextContaining("BATTLE", &battleRect);
	const bool hasMask = DragonFindReportedTextContaining("Discovered mask: 0x3F", &maskRect);
	const bool hasFlags = DragonFindReportedTextContaining("Area 3 flags: 0x0006", &flagsRect);
	const bool hasPeakFlags = DragonFindReportedTextContaining("Area 5 flags: 0x0066", &peakFlagsRect);

	int matched = 0;
	if(hasMap) matched++;
	if(hasPosition) matched++;
	if(hasHero) matched++;
	if(hasLevel) matched++;
	if(hasHealth) matched++;
	if(hasOpen) matched++;
	if(hasSave) matched++;
	if(hasStatus) matched++;
	if(hasAction) matched++;
	if(hasBattle) matched++;
	if(hasMask) matched++;
	if(hasFlags) matched++;
	if(hasPeakFlags) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	return hasMap
		&& hasPosition
		&& hasHero
		&& hasLevel
		&& hasHealth
		&& hasOpen
		&& hasSave
		&& hasStatus
		&& hasAction
		&& hasBattle
		&& hasMask
		&& hasFlags
		&& hasPeakFlags
		&& DragonRectInsideRect(safeRect, openRect)
		&& DragonRectInsideRect(safeRect, saveRect)
		&& DragonRectInsideRect(safeRect, statusRect)
		&& DragonRectInsideRect(safeRect, actionRect)
		&& DragonRectInsideRect(safeRect, battleRect)
		&& DragonRectInsideRect(safeRect, positionRect)
		&& DragonRectInsideRect(safeRect, maskRect)
		&& DragonRectInsideRect(safeRect, flagsRect)
		&& DragonRectInsideRect(safeRect, peakFlagsRect);
}

static bool DragonValidateWorldMapSavedProgressionLoadoutTelemetry () {
	ERect savedRect;
	const bool baseValid = DragonValidateWorldMapProgressionLoadoutTelemetry();
	const bool hasSaved = DragonFindReportedTextContaining("Saved.", &savedRect);
	if(baseValid)
		gDragonValidation.matchedTextDrawCount += hasSaved ? 1 : 0;
	return baseValid && hasSaved;
}

static bool DragonValidateStatusTelemetry (const char* expectedHeader = "STATUS / INVENTORY") {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect headerRect;
	ERect previewRect;
	ERect levelRect;
	ERect healthRect;
	ERect weaponRect;
	ERect armorRect;
	ERect relicRect;
	ERect potionRect;
	ERect prevRect;
	ERect nextRect;
	ERect actionRect;
	ERect sellRect;
	ERect backRect;
	const char* headerText = (expectedHeader != nullptr && expectedHeader[0] != '\0') ? expectedHeader : "STATUS / INVENTORY";
	const bool hasHeader = DragonFindReportedTextContaining(headerText, &headerRect);
	const bool hasPreview = DragonFindReportedTextContaining("Status item preview", &previewRect);
	const bool hasLevel = DragonFindReportedTextContaining("Lv.1", &levelRect);
	const bool hasHealth = DragonFindReportedTextContaining("HP ", &healthRect);
	const bool hasWeapon = DragonFindReportedTextContaining("Weapon:", &weaponRect);
	const bool hasArmor = DragonFindReportedTextContaining("Armor:", &armorRect);
	const bool hasRelic = DragonFindReportedTextContaining("Relic:", &relicRect);
	const bool hasPotion = DragonFindReportedTextContaining("Health Potion", &potionRect);
	const bool hasPrev = DragonFindReportedTextContaining("PREV", &prevRect);
	const bool hasNext = DragonFindReportedTextContaining("NEXT", &nextRect);
	const bool hasAction = DragonFindReportedTextContaining("USE", &actionRect);
	const bool hasSell = DragonFindReportedTextContaining("SELL", &sellRect);
	const bool hasBack = DragonFindReportedTextContaining("BACK", &backRect);

	int matched = 0;
	if(hasHeader) matched++;
	if(hasPreview) matched++;
	if(hasLevel) matched++;
	if(hasHealth) matched++;
	if(hasWeapon) matched++;
	if(hasArmor) matched++;
	if(hasRelic) matched++;
	if(hasPotion) matched++;
	if(hasPrev) matched++;
	if(hasNext) matched++;
	if(hasAction) matched++;
	if(hasSell) matched++;
	if(hasBack) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	if(hasHeader == false
		|| hasPreview == false
		|| hasLevel == false
		|| hasHealth == false
		|| hasWeapon == false
		|| hasArmor == false
		|| hasRelic == false
		|| hasPotion == false
		|| hasPrev == false
		|| hasNext == false
		|| hasAction == false
		|| hasSell == false
		|| hasBack == false) {
		return false;
	}
	if(DragonRectInsideRect(safeRect, previewRect) == false
		|| DragonRectInsideRect(safeRect, prevRect) == false
		|| DragonRectInsideRect(safeRect, nextRect) == false
		|| DragonRectInsideRect(safeRect, actionRect) == false
		|| DragonRectInsideRect(safeRect, sellRect) == false
		|| DragonRectInsideRect(safeRect, backRect) == false) {
		return false;
	}
	const int minPreviewSize = std::max(36, (safeRect.height * 6) / 100);
	if(previewRect.width < minPreviewSize
		|| previewRect.height < minPreviewSize) {
		return false;
	}
	return true;
}

static bool DragonValidateStatusFieldCommandTelemetry (const char* expectedHeader, const char* expectedPrimary, const char* expectedSecondary, const char* expectedAction = nullptr) {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect headerRect;
	ERect previewRect;
	ERect levelRect;
	ERect healthRect;
	ERect primaryRect;
	ERect secondaryRect;
	ERect prevRect;
	ERect nextRect;
	ERect actionRect;
	ERect infoRect;
	ERect backRect;
	const bool hasHeader = DragonFindReportedTextContaining((expectedHeader != nullptr && expectedHeader[0] != '\0') ? expectedHeader : "MAGIC / COMMANDS", &headerRect);
	const bool hasPreview = DragonFindReportedTextContaining("Status item preview", &previewRect);
	const bool hasLevel = DragonFindReportedTextContaining("Lv.1", &levelRect);
	const bool hasHealth = DragonFindReportedTextContaining("HP ", &healthRect);
	const bool hasPrimary = DragonFindReportedTextContaining((expectedPrimary != nullptr && expectedPrimary[0] != '\0') ? expectedPrimary : "Cure", &primaryRect);
	const bool hasSecondary = DragonFindReportedTextContaining((expectedSecondary != nullptr && expectedSecondary[0] != '\0') ? expectedSecondary : "Guard Heal", &secondaryRect);
	const bool hasPrev = DragonFindReportedTextContaining("PREV", &prevRect);
	const bool hasNext = DragonFindReportedTextContaining("NEXT", &nextRect);
	const bool hasAction = DragonFindReportedTextContaining((expectedAction != nullptr && expectedAction[0] != '\0') ? expectedAction : "CAST", &actionRect);
	const bool hasInfo = DragonFindReportedTextContaining("INFO", &infoRect);
	const bool hasBack = DragonFindReportedTextContaining("BACK", &backRect);

	int matched = 0;
	if(hasHeader) matched++;
	if(hasPreview) matched++;
	if(hasLevel) matched++;
	if(hasHealth) matched++;
	if(hasPrimary) matched++;
	if(hasSecondary) matched++;
	if(hasPrev) matched++;
	if(hasNext) matched++;
	if(hasAction) matched++;
	if(hasInfo) matched++;
	if(hasBack) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	return hasHeader
		&& hasPreview
		&& hasLevel
		&& hasHealth
		&& hasPrimary
		&& hasSecondary
		&& hasPrev
		&& hasNext
		&& hasAction
		&& hasInfo
		&& hasBack
		&& DragonRectInsideRect(safeRect, previewRect)
		&& DragonRectInsideRect(safeRect, prevRect)
		&& DragonRectInsideRect(safeRect, nextRect)
		&& DragonRectInsideRect(safeRect, actionRect)
		&& DragonRectInsideRect(safeRect, infoRect)
		&& DragonRectInsideRect(safeRect, backRect);
}

static bool DragonValidateStatusWarriorLevelTwoTelemetry () {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect headerRect;
	ERect levelRect;
	ERect healthRect;
	ERect statsRect;
	ERect weaponRect;
	ERect armorRect;
	ERect relicRect;
	ERect potionRect;
	ERect actionRect;
	ERect sellRect;
	ERect backRect;
	const bool hasHeader = DragonFindReportedTextContaining("STATUS / INVENTORY", &headerRect);
	const bool hasLevel = DragonFindReportedTextContaining("ARIN  Lv.2  XP 74/240", &levelRect);
	const bool hasHealth = DragonFindReportedTextContaining("HP 138/138  Gold 799  Area Eleusis", &healthRect);
	const bool hasStats = DragonFindReportedTextContaining("STR 21  DEF 16  SPD 7  MAG 6", &statsRect);
	const bool hasWeapon = DragonFindReportedTextContaining("Weapon: Iron Blade", &weaponRect);
	const bool hasArmor = DragonFindReportedTextContaining("Armor: Leather Armor", &armorRect);
	const bool hasRelic = DragonFindReportedTextContaining("Relic: None", &relicRect);
	const bool hasPotion = DragonFindReportedTextContaining("Health Potion", &potionRect);
	const bool hasAction = DragonFindReportedTextContaining("USE", &actionRect);
	const bool hasSell = DragonFindReportedTextContaining("SELL", &sellRect);
	const bool hasBack = DragonFindReportedTextContaining("BACK", &backRect);

	int matched = 0;
	if(hasHeader) matched++;
	if(hasLevel) matched++;
	if(hasHealth) matched++;
	if(hasStats) matched++;
	if(hasWeapon) matched++;
	if(hasArmor) matched++;
	if(hasRelic) matched++;
	if(hasPotion) matched++;
	if(hasAction) matched++;
	if(hasSell) matched++;
	if(hasBack) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	return hasHeader
		&& hasLevel
		&& hasHealth
		&& hasStats
		&& hasWeapon
		&& hasArmor
		&& hasRelic
		&& hasPotion
		&& hasAction
		&& hasSell
		&& hasBack
		&& DragonRectInsideRect(safeRect, actionRect)
		&& DragonRectInsideRect(safeRect, sellRect)
		&& DragonRectInsideRect(safeRect, backRect);
}

static bool DragonValidateStatusStarterClassTelemetry (const char* expectedHealth, const char* expectedStats, const char* expectedWeapon, const char* expectedArmor, const char* expectedRelic) {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect headerRect;
	ERect levelRect;
	ERect healthRect;
	ERect statsRect;
	ERect weaponRect;
	ERect armorRect;
	ERect relicRect;
	ERect potionRect;
	ERect actionRect;
	ERect sellRect;
	ERect backRect;
	const bool hasHeader = DragonFindReportedTextContaining("STATUS / INVENTORY", &headerRect);
	const bool hasLevel = DragonFindReportedTextContaining("XP 0/75", &levelRect);
	const bool hasHealth = DragonFindReportedTextContaining(expectedHealth, &healthRect);
	const bool hasStats = DragonFindReportedTextContaining(expectedStats, &statsRect);
	const bool hasWeapon = DragonFindReportedTextContaining(expectedWeapon, &weaponRect);
	const bool hasArmor = DragonFindReportedTextContaining(expectedArmor, &armorRect);
	const bool hasRelic = DragonFindReportedTextContaining(expectedRelic, &relicRect);
	const bool hasPotion = DragonFindReportedTextContaining("Health Potion", &potionRect);
	const bool hasAction = DragonFindReportedTextContaining("USE", &actionRect);
	const bool hasSell = DragonFindReportedTextContaining("SELL", &sellRect);
	const bool hasBack = DragonFindReportedTextContaining("BACK", &backRect);

	int matched = 0;
	if(hasHeader) matched++;
	if(hasLevel) matched++;
	if(hasHealth) matched++;
	if(hasStats) matched++;
	if(hasWeapon) matched++;
	if(hasArmor) matched++;
	if(hasRelic) matched++;
	if(hasPotion) matched++;
	if(hasAction) matched++;
	if(hasSell) matched++;
	if(hasBack) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	return hasHeader
		&& hasLevel
		&& hasHealth
		&& hasStats
		&& hasWeapon
		&& hasArmor
		&& hasRelic
		&& hasPotion
		&& hasAction
		&& hasSell
		&& hasBack
		&& DragonRectInsideRect(safeRect, actionRect)
		&& DragonRectInsideRect(safeRect, sellRect)
		&& DragonRectInsideRect(safeRect, backRect);
}

static bool DragonValidateStatusUsePotionTelemetry () {
	ERect messageRect;
	ERect healthRect;
	EString potionLine;
	const bool baseValid = DragonValidateStatusTelemetry();
	const bool hasMessage = DragonFindReportedTextContaining("Used Health Potion (+48 HP).", &messageRect);
	const bool hasHealth = DragonFindReportedTextContaining("HP 108/125", &healthRect);
	const bool hasPotion = DragonFindReportedTextLineContaining("Health Potion", &potionLine) && potionLine.Contains(EString("x2"));
	if(baseValid)
		gDragonValidation.matchedTextDrawCount += (hasMessage ? 1 : 0) + (hasHealth ? 1 : 0) + (hasPotion ? 1 : 0);
	return baseValid && hasMessage && hasHealth && hasPotion;
}

static bool DragonValidateStatusUnequipWeaponTelemetry () {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect headerRect;
	ERect levelRect;
	ERect healthRect;
	ERect weaponRect;
	ERect armorRect;
	ERect relicRect;
	ERect ironBladeRect;
	ERect actionRect;
	ERect sellRect;
	ERect backRect;
	ERect messageRect;
	EString ironBladeLine;
	const bool hasHeader = DragonFindReportedTextContaining("STATUS / INVENTORY", &headerRect);
	const bool hasLevel = DragonFindReportedTextContaining("Lv.1", &levelRect);
	const bool hasHealth = DragonFindReportedTextContaining("HP 125/125", &healthRect);
	const bool hasWeapon = DragonFindReportedTextContaining("Weapon: None", &weaponRect);
	const bool hasArmor = DragonFindReportedTextContaining("Armor: Leather Armor", &armorRect);
	const bool hasRelic = DragonFindReportedTextContaining("Relic: None", &relicRect);
	const bool hasIronBlade = DragonFindReportedTextLineContaining("Iron Blade", &ironBladeLine, &ironBladeRect) && ironBladeLine.Contains(EString("x1"));
	const bool hasAction = DragonFindReportedTextContaining("EQUIP", &actionRect);
	const bool hasSell = DragonFindReportedTextContaining("SELL", &sellRect);
	const bool hasBack = DragonFindReportedTextContaining("BACK", &backRect);
	const bool hasMessage = DragonFindReportedTextContaining("Unequipped Iron Blade.", &messageRect);

	int matched = 0;
	if(hasHeader) matched++;
	if(hasLevel) matched++;
	if(hasHealth) matched++;
	if(hasWeapon) matched++;
	if(hasArmor) matched++;
	if(hasRelic) matched++;
	if(hasIronBlade) matched++;
	if(hasAction) matched++;
	if(hasSell) matched++;
	if(hasBack) matched++;
	if(hasMessage) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	return hasHeader
		&& hasLevel
		&& hasHealth
		&& hasWeapon
		&& hasArmor
		&& hasRelic
		&& hasIronBlade
		&& hasAction
		&& hasSell
		&& hasBack
		&& DragonRectInsideRect(safeRect, actionRect)
		&& DragonRectInsideRect(safeRect, sellRect)
		&& DragonRectInsideRect(safeRect, backRect);
}

static bool DragonValidateStatusEquipWeaponReadyTelemetry () {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect weaponRect;
	ERect armorRect;
	ERect relicRect;
	ERect ironBladeRect;
	ERect actionRect;
	ERect sellRect;
	ERect backRect;
	EString ironBladeLine;
	const bool hasWeapon = DragonFindReportedTextContaining("Weapon: None", &weaponRect);
	const bool hasArmor = DragonFindReportedTextContaining("Armor: Leather Armor", &armorRect);
	const bool hasRelic = DragonFindReportedTextContaining("Relic: None", &relicRect);
	const bool hasIronBlade = DragonFindReportedTextLineContaining("Iron Blade", &ironBladeLine, &ironBladeRect) && ironBladeLine.Contains(EString("x1"));
	const bool hasAction = DragonFindReportedTextContaining("EQUIP", &actionRect);
	const bool hasSell = DragonFindReportedTextContaining("SELL", &sellRect);
	const bool hasBack = DragonFindReportedTextContaining("BACK", &backRect);

	int matched = 0;
	if(hasWeapon) matched++;
	if(hasArmor) matched++;
	if(hasRelic) matched++;
	if(hasIronBlade) matched++;
	if(hasAction) matched++;
	if(hasSell) matched++;
	if(hasBack) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	return hasWeapon
		&& hasArmor
		&& hasRelic
		&& hasIronBlade
		&& hasAction
		&& hasSell
		&& hasBack
		&& DragonRectInsideRect(safeRect, actionRect)
		&& DragonRectInsideRect(safeRect, sellRect)
		&& DragonRectInsideRect(safeRect, backRect);
}

static bool DragonValidateStatusEquipWeaponTelemetry () {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect headerRect;
	ERect healthRect;
	ERect weaponRect;
	ERect armorRect;
	ERect relicRect;
	ERect potionRect;
	ERect actionRect;
	ERect sellRect;
	ERect backRect;
	ERect messageRect;
	const bool hasHeader = DragonFindReportedTextContaining("STATUS / INVENTORY", &headerRect);
	const bool hasHealth = DragonFindReportedTextContaining("HP 125/125", &healthRect);
	const bool hasWeapon = DragonFindReportedTextContaining("Weapon: Iron Blade", &weaponRect);
	const bool hasArmor = DragonFindReportedTextContaining("Armor: Leather Armor", &armorRect);
	const bool hasRelic = DragonFindReportedTextContaining("Relic: None", &relicRect);
	const bool hasPotion = DragonFindReportedTextContaining("Health Potion", &potionRect);
	const bool hasAction = DragonFindReportedTextContaining("USE", &actionRect);
	const bool hasSell = DragonFindReportedTextContaining("SELL", &sellRect);
	const bool hasBack = DragonFindReportedTextContaining("BACK", &backRect);
	const bool hasMessage = DragonFindReportedTextContaining("Equipped Iron Blade.", &messageRect);
	const bool ironBladeRemoved = DragonFindReportedTextContaining("Iron Blade  x1") == false
		&& DragonFindReportedTextContaining("Iron Blade x1") == false;

	int matched = 0;
	if(hasHeader) matched++;
	if(hasHealth) matched++;
	if(hasWeapon) matched++;
	if(hasArmor) matched++;
	if(hasRelic) matched++;
	if(hasPotion) matched++;
	if(hasAction) matched++;
	if(hasSell) matched++;
	if(hasBack) matched++;
	if(hasMessage) matched++;
	if(ironBladeRemoved) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	return hasHeader
		&& hasHealth
		&& hasWeapon
		&& hasArmor
		&& hasRelic
		&& hasPotion
		&& hasAction
		&& hasSell
		&& hasBack
		&& hasMessage
		&& ironBladeRemoved
		&& DragonRectInsideRect(safeRect, actionRect)
		&& DragonRectInsideRect(safeRect, sellRect)
		&& DragonRectInsideRect(safeRect, backRect);
}

static bool DragonValidateStatusUnequipArmorTelemetry () {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect headerRect;
	ERect levelRect;
	ERect healthRect;
	ERect weaponRect;
	ERect armorRect;
	ERect relicRect;
	ERect leatherArmorRect;
	ERect actionRect;
	ERect sellRect;
	ERect backRect;
	ERect messageRect;
	EString leatherArmorLine;
	const bool hasHeader = DragonFindReportedTextContaining("STATUS / INVENTORY", &headerRect);
	const bool hasLevel = DragonFindReportedTextContaining("Lv.1", &levelRect);
	const bool hasHealth = DragonFindReportedTextContaining("HP 115/115", &healthRect);
	const bool hasWeapon = DragonFindReportedTextContaining("Weapon: Iron Blade", &weaponRect);
	const bool hasArmor = DragonFindReportedTextContaining("Armor: None", &armorRect);
	const bool hasRelic = DragonFindReportedTextContaining("Relic: None", &relicRect);
	const bool hasLeatherArmor = DragonFindReportedTextLineContaining("Leather Armor", &leatherArmorLine, &leatherArmorRect) && leatherArmorLine.Contains(EString("x1"));
	const bool hasAction = DragonFindReportedTextContaining("EQUIP", &actionRect);
	const bool hasSell = DragonFindReportedTextContaining("SELL", &sellRect);
	const bool hasBack = DragonFindReportedTextContaining("BACK", &backRect);
	const bool hasMessage = DragonFindReportedTextContaining("Unequipped Leather Armor.", &messageRect);

	int matched = 0;
	if(hasHeader) matched++;
	if(hasLevel) matched++;
	if(hasHealth) matched++;
	if(hasWeapon) matched++;
	if(hasArmor) matched++;
	if(hasRelic) matched++;
	if(hasLeatherArmor) matched++;
	if(hasAction) matched++;
	if(hasSell) matched++;
	if(hasBack) matched++;
	if(hasMessage) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	return hasHeader
		&& hasLevel
		&& hasHealth
		&& hasWeapon
		&& hasArmor
		&& hasRelic
		&& hasLeatherArmor
		&& hasAction
		&& hasSell
		&& hasBack
		&& hasMessage
		&& DragonRectInsideRect(safeRect, actionRect)
		&& DragonRectInsideRect(safeRect, sellRect)
		&& DragonRectInsideRect(safeRect, backRect);
}

static bool DragonValidateStatusEquipArmorReadyTelemetry () {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect weaponRect;
	ERect armorRect;
	ERect relicRect;
	ERect leatherArmorRect;
	ERect actionRect;
	ERect sellRect;
	ERect backRect;
	EString leatherArmorLine;
	const bool hasWeapon = DragonFindReportedTextContaining("Weapon: Iron Blade", &weaponRect);
	const bool hasArmor = DragonFindReportedTextContaining("Armor: None", &armorRect);
	const bool hasRelic = DragonFindReportedTextContaining("Relic: None", &relicRect);
	const bool hasLeatherArmor = DragonFindReportedTextLineContaining("Leather Armor", &leatherArmorLine, &leatherArmorRect) && leatherArmorLine.Contains(EString("x1"));
	const bool hasAction = DragonFindReportedTextContaining("EQUIP", &actionRect);
	const bool hasSell = DragonFindReportedTextContaining("SELL", &sellRect);
	const bool hasBack = DragonFindReportedTextContaining("BACK", &backRect);

	int matched = 0;
	if(hasWeapon) matched++;
	if(hasArmor) matched++;
	if(hasRelic) matched++;
	if(hasLeatherArmor) matched++;
	if(hasAction) matched++;
	if(hasSell) matched++;
	if(hasBack) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	return hasWeapon
		&& hasArmor
		&& hasRelic
		&& hasLeatherArmor
		&& hasAction
		&& hasSell
		&& hasBack
		&& DragonRectInsideRect(safeRect, actionRect)
		&& DragonRectInsideRect(safeRect, sellRect)
		&& DragonRectInsideRect(safeRect, backRect);
}

static bool DragonValidateStatusEquipArmorTelemetry () {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect headerRect;
	ERect healthRect;
	ERect weaponRect;
	ERect armorRect;
	ERect relicRect;
	ERect potionRect;
	ERect actionRect;
	ERect sellRect;
	ERect backRect;
	ERect messageRect;
	const bool hasHeader = DragonFindReportedTextContaining("STATUS / INVENTORY", &headerRect);
	const bool hasHealth = DragonFindReportedTextContaining("HP 115/125", &healthRect);
	const bool hasWeapon = DragonFindReportedTextContaining("Weapon: Iron Blade", &weaponRect);
	const bool hasArmor = DragonFindReportedTextContaining("Armor: Leather Armor", &armorRect);
	const bool hasRelic = DragonFindReportedTextContaining("Relic: None", &relicRect);
	const bool hasPotion = DragonFindReportedTextContaining("Health Potion", &potionRect);
	const bool hasAction = DragonFindReportedTextContaining("USE", &actionRect);
	const bool hasSell = DragonFindReportedTextContaining("SELL", &sellRect);
	const bool hasBack = DragonFindReportedTextContaining("BACK", &backRect);
	const bool hasMessage = DragonFindReportedTextContaining("Equipped Leather Armor.", &messageRect);
	const bool leatherArmorRemoved = DragonFindReportedTextContaining("Leather Armor  x1") == false
		&& DragonFindReportedTextContaining("Leather Armor x1") == false;

	int matched = 0;
	if(hasHeader) matched++;
	if(hasHealth) matched++;
	if(hasWeapon) matched++;
	if(hasArmor) matched++;
	if(hasRelic) matched++;
	if(hasPotion) matched++;
	if(hasAction) matched++;
	if(hasSell) matched++;
	if(hasBack) matched++;
	if(hasMessage) matched++;
	if(leatherArmorRemoved) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	return hasHeader
		&& hasHealth
		&& hasWeapon
		&& hasArmor
		&& hasRelic
		&& hasPotion
		&& hasAction
		&& hasSell
		&& hasBack
		&& hasMessage
		&& leatherArmorRemoved
		&& DragonRectInsideRect(safeRect, actionRect)
		&& DragonRectInsideRect(safeRect, sellRect)
		&& DragonRectInsideRect(safeRect, backRect);
}

static bool DragonValidateStatusSorcererTelemetry () {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect headerRect;
	ERect levelRect;
	ERect healthRect;
	ERect weaponRect;
	ERect armorRect;
	ERect relicRect;
	ERect potionRect;
	ERect actionRect;
	ERect sellRect;
	ERect backRect;
	const bool hasHeader = DragonFindReportedTextContaining("STATUS / INVENTORY", &headerRect);
	const bool hasLevel = DragonFindReportedTextContaining("LYRA  Lv.1  XP 0/75", &levelRect);
	const bool hasHealth = DragonFindReportedTextContaining("HP 96/99  Gold 10  Area Eleusis", &healthRect);
	const bool hasWeapon = DragonFindReportedTextContaining("Weapon: Adept Wand", &weaponRect);
	const bool hasArmor = DragonFindReportedTextContaining("Armor: Traveler Cloak", &armorRect);
	const bool hasRelic = DragonFindReportedTextContaining("Relic: Seer Charm", &relicRect);
	const bool hasPotion = DragonFindReportedTextContaining("Health Potion", &potionRect);
	const bool hasAction = DragonFindReportedTextContaining("USE", &actionRect);
	const bool hasSell = DragonFindReportedTextContaining("SELL", &sellRect);
	const bool hasBack = DragonFindReportedTextContaining("BACK", &backRect);

	int matched = 0;
	if(hasHeader) matched++;
	if(hasLevel) matched++;
	if(hasHealth) matched++;
	if(hasWeapon) matched++;
	if(hasArmor) matched++;
	if(hasRelic) matched++;
	if(hasPotion) matched++;
	if(hasAction) matched++;
	if(hasSell) matched++;
	if(hasBack) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	return hasHeader
		&& hasLevel
		&& hasHealth
		&& hasWeapon
		&& hasArmor
		&& hasRelic
		&& hasPotion
		&& hasAction
		&& hasSell
		&& hasBack
		&& DragonRectInsideRect(safeRect, actionRect)
		&& DragonRectInsideRect(safeRect, sellRect)
		&& DragonRectInsideRect(safeRect, backRect);
}

static bool DragonValidateStatusUnequipRelicTelemetry () {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect headerRect;
	ERect levelRect;
	ERect healthRect;
	ERect weaponRect;
	ERect armorRect;
	ERect relicRect;
	ERect seerCharmRect;
	ERect actionRect;
	ERect sellRect;
	ERect backRect;
	ERect messageRect;
	EString seerCharmLine;
	const bool hasHeader = DragonFindReportedTextContaining("STATUS / INVENTORY", &headerRect);
	const bool hasLevel = DragonFindReportedTextContaining("LYRA  Lv.1  XP 0/75", &levelRect);
	const bool hasHealth = DragonFindReportedTextContaining("HP 96/99  Gold 10  Area Eleusis", &healthRect);
	const bool hasWeapon = DragonFindReportedTextContaining("Weapon: Adept Wand", &weaponRect);
	const bool hasArmor = DragonFindReportedTextContaining("Armor: Traveler Cloak", &armorRect);
	const bool hasRelic = DragonFindReportedTextContaining("Relic: None", &relicRect);
	const bool hasSeerCharm = DragonFindReportedTextLineContaining("Seer Charm", &seerCharmLine, &seerCharmRect) && seerCharmLine.Contains(EString("x1"));
	const bool hasAction = DragonFindReportedTextContaining("EQUIP", &actionRect);
	const bool hasSell = DragonFindReportedTextContaining("SELL", &sellRect);
	const bool hasBack = DragonFindReportedTextContaining("BACK", &backRect);
	const bool hasMessage = DragonFindReportedTextContaining("Unequipped Seer Charm.", &messageRect);

	int matched = 0;
	if(hasHeader) matched++;
	if(hasLevel) matched++;
	if(hasHealth) matched++;
	if(hasWeapon) matched++;
	if(hasArmor) matched++;
	if(hasRelic) matched++;
	if(hasSeerCharm) matched++;
	if(hasAction) matched++;
	if(hasSell) matched++;
	if(hasBack) matched++;
	if(hasMessage) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	return hasHeader
		&& hasLevel
		&& hasHealth
		&& hasWeapon
		&& hasArmor
		&& hasRelic
		&& hasSeerCharm
		&& hasAction
		&& hasSell
		&& hasBack
		&& hasMessage
		&& DragonRectInsideRect(safeRect, actionRect)
		&& DragonRectInsideRect(safeRect, sellRect)
		&& DragonRectInsideRect(safeRect, backRect);
}

static bool DragonValidateStatusEquipRelicReadyTelemetry () {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect weaponRect;
	ERect armorRect;
	ERect relicRect;
	ERect seerCharmRect;
	ERect actionRect;
	ERect sellRect;
	ERect backRect;
	EString seerCharmLine;
	const bool hasWeapon = DragonFindReportedTextContaining("Weapon: Adept Wand", &weaponRect);
	const bool hasArmor = DragonFindReportedTextContaining("Armor: Traveler Cloak", &armorRect);
	const bool hasRelic = DragonFindReportedTextContaining("Relic: None", &relicRect);
	const bool hasSeerCharm = DragonFindReportedTextLineContaining("Seer Charm", &seerCharmLine, &seerCharmRect) && seerCharmLine.Contains(EString("x1"));
	const bool hasAction = DragonFindReportedTextContaining("EQUIP", &actionRect);
	const bool hasSell = DragonFindReportedTextContaining("SELL", &sellRect);
	const bool hasBack = DragonFindReportedTextContaining("BACK", &backRect);

	int matched = 0;
	if(hasWeapon) matched++;
	if(hasArmor) matched++;
	if(hasRelic) matched++;
	if(hasSeerCharm) matched++;
	if(hasAction) matched++;
	if(hasSell) matched++;
	if(hasBack) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	return hasWeapon
		&& hasArmor
		&& hasRelic
		&& hasSeerCharm
		&& hasAction
		&& hasSell
		&& hasBack
		&& DragonRectInsideRect(safeRect, actionRect)
		&& DragonRectInsideRect(safeRect, sellRect)
		&& DragonRectInsideRect(safeRect, backRect);
}

static bool DragonValidateStatusEquipRelicTelemetry () {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect headerRect;
	ERect healthRect;
	ERect weaponRect;
	ERect armorRect;
	ERect relicRect;
	ERect potionRect;
	ERect actionRect;
	ERect sellRect;
	ERect backRect;
	ERect messageRect;
	const bool hasHeader = DragonFindReportedTextContaining("STATUS / INVENTORY", &headerRect);
	const bool hasHealth = DragonFindReportedTextContaining("HP 96/99  Gold 10  Area Eleusis", &healthRect);
	const bool hasWeapon = DragonFindReportedTextContaining("Weapon: Adept Wand", &weaponRect);
	const bool hasArmor = DragonFindReportedTextContaining("Armor: Traveler Cloak", &armorRect);
	const bool hasRelic = DragonFindReportedTextContaining("Relic: Seer Charm", &relicRect);
	const bool hasPotion = DragonFindReportedTextContaining("Health Potion", &potionRect);
	const bool hasAction = DragonFindReportedTextContaining("USE", &actionRect);
	const bool hasSell = DragonFindReportedTextContaining("SELL", &sellRect);
	const bool hasBack = DragonFindReportedTextContaining("BACK", &backRect);
	const bool hasMessage = DragonFindReportedTextContaining("Equipped Seer Charm.", &messageRect);
	const bool seerCharmRemoved = DragonFindReportedTextContaining("Seer Charm  x1") == false
		&& DragonFindReportedTextContaining("Seer Charm x1") == false;

	int matched = 0;
	if(hasHeader) matched++;
	if(hasHealth) matched++;
	if(hasWeapon) matched++;
	if(hasArmor) matched++;
	if(hasRelic) matched++;
	if(hasPotion) matched++;
	if(hasAction) matched++;
	if(hasSell) matched++;
	if(hasBack) matched++;
	if(hasMessage) matched++;
	if(seerCharmRemoved) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	return hasHeader
		&& hasHealth
		&& hasWeapon
		&& hasArmor
		&& hasRelic
		&& hasPotion
		&& hasAction
		&& hasSell
		&& hasBack
		&& hasMessage
		&& seerCharmRemoved
		&& DragonRectInsideRect(safeRect, actionRect)
		&& DragonRectInsideRect(safeRect, sellRect)
		&& DragonRectInsideRect(safeRect, backRect);
}

static bool DragonValidateStatusFireScrollSelectedTelemetry () {
	ERect actionRect;
	ERect sellRect;
	EString fireScrollLine;
	const bool hasAction = DragonFindReportedTextContaining("USE", &actionRect);
	const bool hasSell = DragonFindReportedTextContaining("SELL", &sellRect);
	const bool hasFireScroll = DragonFindReportedTextLineContaining("Fire Scroll", &fireScrollLine) && fireScrollLine.Contains(EString("x1"));
	const bool hasFireScrollDetails = DragonCountReportedTextContaining("Fire Scroll") >= 2;
	int matched = 0;
	if(hasAction) matched++;
	if(hasSell) matched++;
	if(hasFireScroll) matched++;
	if(hasFireScrollDetails) matched++;
	gDragonValidation.matchedTextDrawCount = matched;
	return hasAction && hasSell && hasFireScroll && hasFireScrollDetails;
}

static bool DragonValidateStatusSellScrollTelemetry () {
	ERect goldRect;
	const bool baseValid = DragonValidateStatusTelemetry();
	const bool hasGold = DragonFindReportedTextContaining("Gold 15", &goldRect);
	const bool soldAway = DragonFindReportedTextContaining("Fire Scroll") == false;
	if(baseValid)
		gDragonValidation.matchedTextDrawCount += (hasGold ? 1 : 0) + (soldAway ? 1 : 0);
	return baseValid && hasGold && soldAway;
}

static bool DragonValidateStatusSellEquippedWeaponReadyTelemetry () {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect headerRect;
	ERect healthRect;
	ERect weaponRect;
	ERect armorRect;
	ERect relicRect;
	ERect ironBladeRect;
	ERect detailRect;
	ERect actionRect;
	ERect sellRect;
	ERect backRect;
	EString ironBladeLine;
	const bool hasHeader = DragonFindReportedTextContaining("STATUS / INVENTORY", &headerRect);
	const bool hasHealth = DragonFindReportedTextContaining("HP 125/125  Gold 10  Area Eleusis", &healthRect);
	const bool hasWeapon = DragonFindReportedTextContaining("Weapon: Iron Blade", &weaponRect);
	const bool hasArmor = DragonFindReportedTextContaining("Armor: Leather Armor", &armorRect);
	const bool hasRelic = DragonFindReportedTextContaining("Relic: None", &relicRect);
	const bool hasIronBlade = DragonFindReportedTextLineContaining("Iron Blade", &ironBladeLine, &ironBladeRect)
		&& ironBladeLine.Contains(EString("x1"))
		&& ironBladeLine.Contains(EString("[E]"));
	const bool hasDetail = DragonFindReportedTextContaining("Sell 11g", &detailRect)
		&& DragonFindReportedTextContaining("dATK+0");
	const bool hasAction = DragonFindReportedTextContaining("EQUIP", &actionRect);
	const bool hasSell = DragonFindReportedTextContaining("SELL", &sellRect);
	const bool hasBack = DragonFindReportedTextContaining("BACK", &backRect);

	int matched = 0;
	if(hasHeader) matched++;
	if(hasHealth) matched++;
	if(hasWeapon) matched++;
	if(hasArmor) matched++;
	if(hasRelic) matched++;
	if(hasIronBlade) matched++;
	if(hasDetail) matched++;
	if(hasAction) matched++;
	if(hasSell) matched++;
	if(hasBack) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	return hasHeader
		&& hasHealth
		&& hasWeapon
		&& hasArmor
		&& hasRelic
		&& hasIronBlade
		&& hasDetail
		&& hasAction
		&& hasSell
		&& hasBack
		&& DragonRectInsideRect(safeRect, actionRect)
		&& DragonRectInsideRect(safeRect, sellRect)
		&& DragonRectInsideRect(safeRect, backRect);
}

static bool DragonValidateStatusSellEquippedWeaponTelemetry () {
	ERect goldRect;
	ERect messageRect;
	const bool baseValid = DragonValidateStatusTelemetry();
	const bool hasGold = DragonFindReportedTextContaining("Gold 21", &goldRect);
	const bool hasMessage = DragonFindReportedTextContaining("Sold Iron Blade for 11 gold.", &messageRect);
	const bool soldAway = DragonFindReportedTextContaining("Iron Blade  x1") == false
		&& DragonFindReportedTextContaining("Iron Blade x1") == false;
	if(baseValid)
		gDragonValidation.matchedTextDrawCount += (hasGold ? 1 : 0) + (hasMessage ? 1 : 0) + (soldAway ? 1 : 0);
	return baseValid && hasGold && hasMessage && soldAway;
}

static bool DragonValidateStatusInventoryFullReadyTelemetry () {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect healthRect;
	ERect weaponRect;
	ERect armorRect;
	ERect relicRect;
	ERect adeptWandRect;
	ERect nextRect;
	ERect actionRect;
	ERect sellRect;
	ERect backRect;
	EString adeptWandLine;
	const bool hasHealth = DragonFindReportedTextContaining("HP 125/125  Gold 10  Area Eleusis", &healthRect);
	const bool hasWeapon = DragonFindReportedTextContaining("Weapon: Iron Blade", &weaponRect);
	const bool hasArmor = DragonFindReportedTextContaining("Armor: Leather Armor", &armorRect);
	const bool hasRelic = DragonFindReportedTextContaining("Relic: None", &relicRect);
	const bool hasAdeptWand = DragonFindReportedTextLineContaining("Adept Wand", &adeptWandLine, &adeptWandRect)
		&& adeptWandLine.Contains(EString("x1"));
	const bool hasNext = DragonFindReportedTextContaining("NEXT", &nextRect);
	const bool hasAction = DragonFindReportedTextContaining("EQUIP", &actionRect);
	const bool hasSell = DragonFindReportedTextContaining("SELL", &sellRect);
	const bool hasBack = DragonFindReportedTextContaining("BACK", &backRect);

	int matched = 0;
	if(hasHealth) matched++;
	if(hasWeapon) matched++;
	if(hasArmor) matched++;
	if(hasRelic) matched++;
	if(hasAdeptWand) matched++;
	if(hasNext) matched++;
	if(hasAction) matched++;
	if(hasSell) matched++;
	if(hasBack) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	return hasHealth
		&& hasWeapon
		&& hasArmor
		&& hasRelic
		&& hasAdeptWand
		&& hasNext
		&& hasAction
		&& hasSell
		&& hasBack
		&& DragonRectInsideRect(safeRect, nextRect)
		&& DragonRectInsideRect(safeRect, actionRect)
		&& DragonRectInsideRect(safeRect, sellRect)
		&& DragonRectInsideRect(safeRect, backRect);
}

static bool DragonValidateStatusInventoryFullBlockedTelemetry () {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect healthRect;
	ERect weaponRect;
	ERect armorRect;
	ERect relicRect;
	ERect messageRect;
	ERect actionRect;
	ERect sellRect;
	ERect backRect;
	const bool hasHealth = DragonFindReportedTextContaining("HP 125/125  Gold 10  Area Eleusis", &healthRect);
	const bool hasWeapon = DragonFindReportedTextContaining("Weapon: Iron Blade", &weaponRect);
	const bool hasArmor = DragonFindReportedTextContaining("Armor: Leather Armor", &armorRect);
	const bool hasRelic = DragonFindReportedTextContaining("Relic: None", &relicRect);
	const bool hasMessage = DragonFindReportedTextContaining("Inventory is full.", &messageRect);
	const bool hasAction = DragonFindReportedTextContaining("EQUIP", &actionRect);
	const bool hasSell = DragonFindReportedTextContaining("SELL", &sellRect);
	const bool hasBack = DragonFindReportedTextContaining("BACK", &backRect);
	const bool ironBladeHidden = DragonFindReportedTextContaining("Iron Blade  x1") == false
		&& DragonFindReportedTextContaining("Iron Blade x1") == false;

	int matched = 0;
	if(hasHealth) matched++;
	if(hasWeapon) matched++;
	if(hasArmor) matched++;
	if(hasRelic) matched++;
	if(hasMessage) matched++;
	if(hasAction) matched++;
	if(hasSell) matched++;
	if(hasBack) matched++;
	if(ironBladeHidden) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	return hasHealth
		&& hasWeapon
		&& hasArmor
		&& hasRelic
		&& hasMessage
		&& hasAction
		&& hasSell
		&& hasBack
		&& ironBladeHidden
		&& DragonRectInsideRect(safeRect, actionRect)
		&& DragonRectInsideRect(safeRect, sellRect)
		&& DragonRectInsideRect(safeRect, backRect);
}

static bool DragonValidateStatusLargeInventoryTelemetry (const char* expectedItem, const char* expectedAction) {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect headerRect;
	ERect levelRect;
	ERect areaRect;
	ERect weaponRect;
	ERect armorRect;
	ERect relicRect;
	ERect itemRect;
	ERect prevRect;
	ERect nextRect;
	ERect actionRect;
	ERect sellRect;
	ERect backRect;
	EString itemLine;
	const bool hasHeader = DragonFindReportedTextContaining("STATUS / INVENTORY", &headerRect);
	const bool hasLevel = DragonFindReportedTextContaining("Lv.18", &levelRect);
	const bool hasArea = DragonFindReportedTextContaining("Area Eleusis Caves", &areaRect);
	const bool hasWeapon = DragonFindReportedTextContaining("Weapon: Iron Blade", &weaponRect);
	const bool hasArmor = DragonFindReportedTextContaining("Armor: Leather Armor", &armorRect);
	const bool hasRelic = DragonFindReportedTextContaining("Relic: None", &relicRect);
	const bool hasItem = DragonFindReportedTextLineContaining(expectedItem, &itemLine, &itemRect)
		&& itemLine.Contains(EString("x1"));
	const bool hasItemDetail = DragonCountReportedTextContaining(expectedItem) >= 2
		&& DragonFindReportedTextContaining("Sell");
	const bool hasPrev = DragonFindReportedTextContaining("PREV", &prevRect);
	const bool hasNext = DragonFindReportedTextContaining("NEXT", &nextRect);
	const bool hasAction = DragonFindReportedTextContaining(expectedAction, &actionRect);
	const bool hasSell = DragonFindReportedTextContaining("SELL", &sellRect);
	const bool hasBack = DragonFindReportedTextContaining("BACK", &backRect);

	int matched = 0;
	if(hasHeader) matched++;
	if(hasLevel) matched++;
	if(hasArea) matched++;
	if(hasWeapon) matched++;
	if(hasArmor) matched++;
	if(hasRelic) matched++;
	if(hasItem) matched++;
	if(hasItemDetail) matched++;
	if(hasPrev) matched++;
	if(hasNext) matched++;
	if(hasAction) matched++;
	if(hasSell) matched++;
	if(hasBack) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	return hasHeader
		&& hasLevel
		&& hasArea
		&& hasWeapon
		&& hasArmor
		&& hasRelic
		&& hasItem
		&& hasItemDetail
		&& hasPrev
		&& hasNext
		&& hasAction
		&& hasSell
		&& hasBack
		&& DragonRectInsideRect(safeRect, prevRect)
		&& DragonRectInsideRect(safeRect, nextRect)
		&& DragonRectInsideRect(safeRect, actionRect)
		&& DragonRectInsideRect(safeRect, sellRect)
		&& DragonRectInsideRect(safeRect, backRect);
}

static bool DragonValidateNewGameFilledSlotTelemetry () {
	ERect backRect;
	ERect slotRect;
	ERect actionRect;
	ERect deleteRect;
	ERect levelRect;
	ERect progressRect;
	const bool hasBack = DragonFindReportedTextContaining("BACK", &backRect);
	const bool hasSlot = DragonFindReportedTextContaining("SLOT 1", &slotRect);
	const bool hasAction = DragonFindReportedTextContaining("OPEN GAME", &actionRect);
	const bool hasDelete = DragonFindReportedTextContaining("DEL", &deleteRect);
	const bool hasLevel = DragonFindReportedTextContaining("Lv: 1", &levelRect);
	const bool hasProgress = DragonFindReportedTextContaining("Gold 10", &progressRect);

	int matched = 0;
	if(hasBack) matched++;
	if(hasSlot) matched++;
	if(hasAction) matched++;
	if(hasDelete) matched++;
	if(hasLevel) matched++;
	if(hasProgress) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	if(hasBack == false
		|| hasSlot == false
		|| hasAction == false
		|| hasDelete == false
		|| hasLevel == false
		|| hasProgress == false) {
		return false;
	}
	return true;
}

static bool DragonValidateNewGameProgressionSlotTelemetry () {
	ERect backRect;
	ERect slotRect;
	ERect actionRect;
	ERect deleteRect;
	ERect nameRect;
	ERect levelRect;
	ERect progressRect;
	const bool hasBack = DragonFindReportedTextContaining("BACK", &backRect);
	const bool hasSlot = DragonFindReportedTextContaining("SLOT 1", &slotRect);
	const bool hasAction = DragonFindReportedTextContaining("OPEN GAME", &actionRect);
	const bool hasDelete = DragonFindReportedTextContaining("DEL", &deleteRect);
	const bool hasName = DragonFindReportedTextContaining("LYRA", &nameRect);
	const bool hasLevel = DragonFindReportedTextContaining("Lv: 7", &levelRect);
	const bool hasProgress = DragonFindReportedTextContaining("Eleusis Caves  Gold 245  W9/L2", &progressRect);

	int matched = 0;
	if(hasBack) matched++;
	if(hasSlot) matched++;
	if(hasAction) matched++;
	if(hasDelete) matched++;
	if(hasName) matched++;
	if(hasLevel) matched++;
	if(hasProgress) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	return hasBack
		&& hasSlot
		&& hasAction
		&& hasDelete
		&& hasName
		&& hasLevel
		&& hasProgress;
}

static bool DragonValidateNewGameProgressionLoadoutSlotTelemetry () {
	ERect backRect;
	ERect slotRect;
	ERect actionRect;
	ERect deleteRect;
	ERect nameRect;
	ERect levelRect;
	ERect progressRect;
	const bool hasBack = DragonFindReportedTextContaining("BACK", &backRect);
	const bool hasSlot = DragonFindReportedTextContaining("SLOT 1", &slotRect);
	const bool hasAction = DragonFindReportedTextContaining("OPEN GAME", &actionRect);
	const bool hasDelete = DragonFindReportedTextContaining("DEL", &deleteRect);
	const bool hasName = DragonFindReportedTextContaining("ARIN", &nameRect);
	const bool hasLevel = DragonFindReportedTextContaining("Lv: 18", &levelRect);
	const bool hasProgress = DragonFindReportedTextContaining("Eleusis Caves  Gold 777  W12/L3", &progressRect);

	int matched = 0;
	if(hasBack) matched++;
	if(hasSlot) matched++;
	if(hasAction) matched++;
	if(hasDelete) matched++;
	if(hasName) matched++;
	if(hasLevel) matched++;
	if(hasProgress) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	return hasBack
		&& hasSlot
		&& hasAction
		&& hasDelete
		&& hasName
		&& hasLevel
		&& hasProgress;
}

static bool DragonValidateNewGameMultiSlotTelemetry () {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect backRect;
	ERect hintRect;
	ERect arinRect;
	ERect lyraRect;
	ERect thornRect;
	ERect level1Rect;
	ERect level7Rect;
	ERect level12Rect;
	ERect gold10Rect;
	ERect gold245Rect;
	ERect gold999Rect;
	const bool hasBack = DragonFindReportedTextContaining("BACK", &backRect);
	const bool hasHint = DragonFindReportedTextContaining("Tap a slot to load. Tap DEL to erase a save.", &hintRect);
	const bool hasSlot1 = DragonFindReportedTextContaining("SLOT 1");
	const bool hasSlot2 = DragonFindReportedTextContaining("SLOT 2");
	const bool hasSlot3 = DragonFindReportedTextContaining("SLOT 3");
	const bool hasArin = DragonFindReportedTextContaining("ARIN", &arinRect);
	const bool hasLyra = DragonFindReportedTextContaining("LYRA", &lyraRect);
	const bool hasThorn = DragonFindReportedTextContaining("THORN", &thornRect);
	const bool hasLevel1 = DragonFindReportedTextContaining("Lv: 1", &level1Rect);
	const bool hasLevel7 = DragonFindReportedTextContaining("Lv: 7", &level7Rect);
	const bool hasLevel12 = DragonFindReportedTextContaining("Lv: 12", &level12Rect);
	const bool hasGold10 = DragonFindReportedTextContaining("Eleusis  Gold 10  W0/L0", &gold10Rect);
	const bool hasGold245 = DragonFindReportedTextContaining("Eleusis Caves  Gold 245  W9/L2", &gold245Rect);
	const bool hasGold999 = DragonFindReportedTextContaining("The Peak  Gold 999  W21/L4", &gold999Rect);
	const int openCount = DragonCountReportedTextContaining("OPEN GAME");
	const int deleteCount = DragonCountReportedTextContaining("DEL");

	int matched = 0;
	if(hasBack) matched++;
	if(hasHint) matched++;
	if(hasSlot1) matched++;
	if(hasSlot2) matched++;
	if(hasSlot3) matched++;
	if(hasArin) matched++;
	if(hasLyra) matched++;
	if(hasThorn) matched++;
	if(hasLevel1) matched++;
	if(hasLevel7) matched++;
	if(hasLevel12) matched++;
	if(hasGold10) matched++;
	if(hasGold245) matched++;
	if(hasGold999) matched++;
	matched += std::min(openCount, 3);
	matched += std::min(deleteCount, 3);
	gDragonValidation.matchedTextDrawCount = matched;

	return hasBack
		&& hasHint
		&& hasSlot1
		&& hasSlot2
		&& hasSlot3
		&& hasArin
		&& hasLyra
		&& hasThorn
		&& hasLevel1
		&& hasLevel7
		&& hasLevel12
		&& hasGold10
		&& hasGold245
		&& hasGold999
		&& openCount >= 3
		&& deleteCount >= 3
		&& DragonRectInsideRect(safeRect, backRect)
		&& DragonRectInsideRect(safeRect, hintRect);
}

static bool DragonValidateNewGameDeleteConfirmTelemetry () {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect promptRect;
	ERect deleteRect;
	ERect cancelRect;
	const bool hasPrompt = DragonFindReportedTextContaining("Delete slot 2? This cannot be undone.", &promptRect);
	const bool hasDelete = DragonFindReportedTextContaining("DELETE", &deleteRect);
	const bool hasCancel = DragonFindReportedTextContaining("CANCEL", &cancelRect);

	int matched = 0;
	if(hasPrompt) matched++;
	if(hasDelete) matched++;
	if(hasCancel) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	return hasPrompt
		&& hasDelete
		&& hasCancel
		&& DragonRectInsideRect(safeRect, promptRect)
		&& DragonRectInsideRect(safeRect, deleteRect)
		&& DragonRectInsideRect(safeRect, cancelRect);
}

static bool DragonValidateNewGameDeletedSlotTelemetry () {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect hintRect;
	ERect messageRect;
	const bool hasHint = DragonFindReportedTextContaining("Tap a slot to load. Tap DEL to erase a save.", &hintRect);
	const bool hasMessage = DragonFindReportedTextContaining("Slot deleted.", &messageRect);
	const bool hasArin = DragonFindReportedTextContaining("ARIN");
	const bool hasThorn = DragonFindReportedTextContaining("THORN");
	const bool missingLyra = DragonFindReportedTextContaining("LYRA") == false;
	const bool hasSlot2 = DragonFindReportedTextContaining("SLOT 2");
	const int openCount = DragonCountReportedTextContaining("OPEN GAME");
	const int deleteCount = DragonCountReportedTextContaining("DEL");
	const int newGameCount = DragonCountReportedTextContaining("NEW GAME");

	int matched = 0;
	if(hasHint) matched++;
	if(hasMessage) matched++;
	if(hasArin) matched++;
	if(hasThorn) matched++;
	if(missingLyra) matched++;
	if(hasSlot2) matched++;
	matched += std::min(openCount, 2);
	matched += std::min(deleteCount, 2);
	matched += std::min(newGameCount, 1);
	gDragonValidation.matchedTextDrawCount = matched;

	return hasHint
		&& hasMessage
		&& hasArin
		&& hasThorn
		&& missingLyra
		&& hasSlot2
		&& openCount >= 2
		&& deleteCount >= 2
		&& newGameCount >= 1
		&& DragonRectInsideRect(safeRect, hintRect)
		&& DragonRectInsideRect(safeRect, messageRect);
}

static bool DragonValidateNewGameCorruptRecoveryTelemetry () {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect hintRect;
	ERect messageRect;
	const bool hasHint = DragonFindReportedTextContaining("Tap a slot to load. Tap DEL to erase a save.", &hintRect);
	const bool hasMessage = DragonFindReportedTextContaining("Recovered corrupted save in slot 1.", &messageRect);
	const bool hasSlot1 = DragonFindReportedTextContaining("SLOT 1");
	const bool hasSlot2 = DragonFindReportedTextContaining("SLOT 2");
	const bool hasLyra = DragonFindReportedTextContaining("LYRA");
	const bool missingArin = DragonFindReportedTextContaining("ARIN") == false;
	const int openCount = DragonCountReportedTextContaining("OPEN GAME");
	const int deleteCount = DragonCountReportedTextContaining("DEL");
	const int newGameCount = DragonCountReportedTextContaining("NEW GAME");

	int matched = 0;
	if(hasHint) matched++;
	if(hasMessage) matched++;
	if(hasSlot1) matched++;
	if(hasSlot2) matched++;
	if(hasLyra) matched++;
	if(missingArin) matched++;
	matched += std::min(openCount, 1);
	matched += std::min(deleteCount, 1);
	matched += std::min(newGameCount, 2);
	gDragonValidation.matchedTextDrawCount = matched;

	return hasHint
		&& hasMessage
		&& hasSlot1
		&& hasSlot2
		&& hasLyra
		&& missingArin
		&& openCount >= 1
		&& deleteCount >= 1
		&& newGameCount >= 2
		&& DragonRectInsideRect(safeRect, hintRect)
		&& DragonRectInsideRect(safeRect, messageRect);
}

static bool DragonValidateShopTelemetry (int expectedOffset) {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect headerRect;
	ERect panelRect;
	ERect rowSampleRect;
	ERect prevRect;
	ERect doneRect;
	ERect nextRect;
	ERect offsetRect;
	ERect totalRect;
	const bool hasHeader = DragonFindReportedTextContaining("MERCHANT", &headerRect);
	const bool hasPanel = DragonFindReportedTextContaining("Shop panel bounds", &panelRect);
	const bool hasRowSample = DragonFindReportedTextContaining("Shop row sample", &rowSampleRect);
	const bool hasPrev = DragonFindReportedTextContaining("PREV", &prevRect);
	const bool hasDone = DragonFindReportedTextContaining("DONE", &doneRect);
	const bool hasNext = DragonFindReportedTextContaining("NEXT", &nextRect);
	const bool hasOffset = DragonFindReportedTextContaining(EString().Format("Shop page offset: %d", expectedOffset), &offsetRect);
	const bool hasTotal = DragonFindReportedTextContaining("Shop total offers:", &totalRect);
	const int minimumGoldLines = 5;
	const int goldLineCount = DragonCountReportedTextContaining("gold");

	int matched = 0;
	if(hasHeader) matched++;
	if(hasPanel) matched++;
	if(hasRowSample) matched++;
	if(hasPrev) matched++;
	if(hasDone) matched++;
	if(hasNext) matched++;
	if(hasOffset) matched++;
	if(hasTotal) matched++;
	matched += std::min(goldLineCount, minimumGoldLines);
	gDragonValidation.matchedTextDrawCount = matched;

	if(hasHeader == false
		|| hasPanel == false
		|| hasRowSample == false
		|| hasPrev == false
		|| hasDone == false
		|| hasNext == false
		|| hasOffset == false
		|| hasTotal == false
		|| goldLineCount < minimumGoldLines) {
		return false;
	}
	if(DragonRectInsideRect(safeRect, panelRect) == false
		|| DragonRectInsideRect(safeRect, rowSampleRect) == false
		|| DragonRectInsideRect(safeRect, prevRect) == false
		|| DragonRectInsideRect(safeRect, doneRect) == false
		|| DragonRectInsideRect(safeRect, nextRect) == false
		|| DragonRectInsideRect(safeRect, offsetRect) == false
		|| DragonRectInsideRect(safeRect, totalRect) == false) {
		return false;
	}
	const int minPanelWidth = std::max(320, (safeRect.width * 34) / 100);
	const int minPanelHeight = std::max(220, (safeRect.height * 32) / 100);
	const int minRowHeight = std::max(28, (safeRect.height * 5) / 100);
	if(panelRect.width < minPanelWidth
		|| panelRect.height < minPanelHeight
		|| rowSampleRect.height < minRowHeight) {
		return false;
	}
	return true;
}

static bool DragonValidateShopPurchaseTelemetry () {
	ERect messageRect;
	ERect ownedRect;
	const bool baseValid = DragonValidateShopTelemetry(6);
	const bool hasMessage = DragonFindReportedTextContaining("Purchased Hunter Bow for 52 gold. 947 left.", &messageRect);
	const bool hasOwned = DragonFindReportedTextContaining("Hunter Bow (OWNED)", &ownedRect);
	if(baseValid)
		gDragonValidation.matchedTextDrawCount += (hasMessage ? 1 : 0) + (hasOwned ? 1 : 0);
	return baseValid && hasMessage && hasOwned;
}

static bool DragonValidateShopInsufficientGoldTelemetry () {
	ERect messageRect;
	ERect hunterBowRect;
	const bool baseValid = DragonValidateShopTelemetry(6);
	const bool hasMessage = DragonFindReportedTextContaining("Need 52 gold to purchase.", &messageRect);
	const bool hasHunterBow = DragonFindReportedTextContaining("Hunter Bow", &hunterBowRect);
	const bool hasOwned = DragonFindReportedTextContaining("Hunter Bow (OWNED)") == false;
	if(baseValid)
		gDragonValidation.matchedTextDrawCount += (hasMessage ? 1 : 0) + (hasHunterBow ? 1 : 0) + (hasOwned ? 1 : 0);
	return baseValid && hasMessage && hasHunterBow && hasOwned;
}

static bool DragonValidateShopDuplicateOwnedTelemetry () {
	ERect messageRect;
	ERect ownedRect;
	const bool baseValid = DragonValidateShopTelemetry(6);
	const bool hasMessage = DragonFindReportedTextContaining("You already have this gear.", &messageRect);
	const bool hasOwned = DragonFindReportedTextContaining("Hunter Bow (OWNED)", &ownedRect);
	if(baseValid)
		gDragonValidation.matchedTextDrawCount += (hasMessage ? 1 : 0) + (hasOwned ? 1 : 0);
	return baseValid && hasMessage && hasOwned;
}

static bool DragonValidateShopInventoryFullTelemetry () {
	ERect messageRect;
	ERect hunterBowRect;
	const bool baseValid = DragonValidateShopTelemetry(6);
	const bool hasMessage = DragonFindReportedTextContaining("Inventory is full.", &messageRect);
	const bool hasHunterBow = DragonFindReportedTextContaining("Hunter Bow", &hunterBowRect);
	const bool hasOwned = DragonFindReportedTextContaining("Hunter Bow (OWNED)") == false;
	if(baseValid)
		gDragonValidation.matchedTextDrawCount += (hasMessage ? 1 : 0) + (hasHunterBow ? 1 : 0) + (hasOwned ? 1 : 0);
	return baseValid && hasMessage && hasHunterBow && hasOwned;
}

static bool DragonValidateShopConsumablePurchaseTelemetry (const char* expectedMessage) {
	ERect messageRect;
	const bool baseValid = DragonValidateShopTelemetry(0);
	const bool hasMessage = DragonFindReportedTextContaining(
		expectedMessage != nullptr ? expectedMessage : "Purchased",
		&messageRect
	);
	if(baseValid)
		gDragonValidation.matchedTextDrawCount += (hasMessage ? 1 : 0);
	return baseValid && hasMessage;
}

static bool DragonValidateBattleTelemetry (int expectedSelectedIndex) {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect attackRect;
	ERect defendRect;
	ERect magicRect;
	ERect techRect;
	ERect runRect;
	ERect enemyCountRect;
	ERect selectedRect;
	ERect targetRect;
	const bool hasAttack = DragonFindReportedTextContaining("ATTACK", &attackRect);
	const bool hasDefend = DragonFindReportedTextContaining("DEFEND", &defendRect);
	const bool hasMagic = DragonFindReportedTextContaining("MAGIC", &magicRect);
	const bool hasTech = DragonFindReportedTextContaining("TECH", &techRect);
	const bool hasRun = DragonFindReportedTextContaining("RUN", &runRect) || DragonFindReportedTextContaining("NO RUN", &runRect);
	const bool hasEnemyCount = DragonFindReportedTextContaining("Enemy count: 4", &enemyCountRect);
	const bool hasSelected = DragonFindReportedTextContaining(
		EString().Format("Selected target index: %d", expectedSelectedIndex),
		&selectedRect
	);
	const bool hasTarget = DragonFindReportedTextContaining(
		EString().Format("Enemy target %d:", expectedSelectedIndex),
		&targetRect
	);
	const int targetCount = DragonCountReportedTextContaining("Enemy target ");

	int matched = 0;
	if(hasAttack) matched++;
	if(hasDefend) matched++;
	if(hasMagic) matched++;
	if(hasTech) matched++;
	if(hasRun) matched++;
	if(hasEnemyCount) matched++;
	if(hasSelected) matched++;
	if(hasTarget) matched++;
	matched += std::min(targetCount, 4);
	gDragonValidation.matchedTextDrawCount = matched;

	if(hasAttack == false
		|| hasDefend == false
		|| hasMagic == false
		|| hasTech == false
		|| hasRun == false
		|| hasEnemyCount == false
		|| hasSelected == false
		|| hasTarget == false
		|| targetCount < 4) {
		return false;
	}
	if(DragonRectInsideRect(safeRect, attackRect) == false
		|| DragonRectInsideRect(safeRect, defendRect) == false
		|| DragonRectInsideRect(safeRect, magicRect) == false
		|| DragonRectInsideRect(safeRect, techRect) == false
		|| DragonRectInsideRect(safeRect, runRect) == false
		|| DragonRectInsideRect(safeRect, targetRect) == false) {
		return false;
	}
	return true;
}

static bool DragonValidateBattlePendingTelemetry (const char* expectedMode) {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect pendingRect;
	ERect targetRect;
	const bool baseValid = DragonValidateBattleTelemetry(0);
	const bool hasPending = DragonFindReportedTextContaining(
		EString().Format("Pending target mode: %s", expectedMode != nullptr ? expectedMode : "ATTACK"),
		&pendingRect
	);
	const bool hasTarget = DragonFindReportedTextContaining("Enemy target 2:", &targetRect);
	if(baseValid)
		gDragonValidation.matchedTextDrawCount += (hasPending ? 1 : 0) + (hasTarget ? 1 : 0);
	return baseValid
		&& hasPending
		&& hasTarget
		&& DragonRectInsideRect(safeRect, pendingRect)
		&& DragonRectInsideRect(safeRect, targetRect);
}

static bool DragonValidateBattlePendingTargetTelemetry (const char* expectedMode, int expectedTargetIndex) {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect pendingRect;
	ERect targetRect;
	const bool hasPending = DragonFindReportedTextContaining(
		EString().Format("Pending target mode: %s", expectedMode != nullptr ? expectedMode : "ATTACK"),
		&pendingRect
	);
	const bool hasSelected = DragonFindReportedTextContaining(
		EString().Format("Selected target index: %d", expectedTargetIndex),
		&targetRect
	);
	const bool hasTarget = DragonFindReportedTextContaining(
		EString().Format("Enemy target %d:", expectedTargetIndex),
		&targetRect
	);
	const bool hasEnemyCount = DragonCountReportedTextContaining("Enemy count: 4") > 0;

	int matched = 0;
	if(hasPending) matched++;
	if(hasSelected) matched++;
	if(hasTarget) matched++;
	if(hasEnemyCount) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	return hasPending
		&& hasSelected
		&& hasTarget
		&& hasEnemyCount
		&& DragonRectInsideRect(safeRect, pendingRect)
		&& DragonRectInsideRect(safeRect, targetRect);
}

static bool DragonValidateBattleActionTelemetry () {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect attackRect;
	ERect serialRect;
	ERect resultRect;
	EString resultLine;
	const bool hasAttack = DragonFindReportedTextContaining("ATTACK", &attackRect);
	const bool hasSerial = DragonFindReportedTextContaining("Player action serial: 1", &serialRect);
	const bool hasResult = DragonFindReportedTextLineContaining("Player action result:", &resultLine, &resultRect);
	const bool hasEnemyCount = DragonCountReportedTextContaining("Enemy count: ") > 0;
	const bool hasResultBody = hasResult && resultLine.GetLength() > (int)std::strlen("Player action result: ");

	int matched = 0;
	if(hasAttack) matched++;
	if(hasSerial) matched++;
	if(hasResult) matched++;
	if(hasEnemyCount) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	return hasAttack
		&& hasSerial
		&& hasResult
		&& hasResultBody
		&& hasEnemyCount
		&& DragonRectInsideRect(safeRect, attackRect)
		&& DragonRectInsideRect(safeRect, serialRect)
		&& DragonRectInsideRect(safeRect, resultRect);
}

static bool DragonReadBattleEnemyCountTelemetry (int& outEnemyCount);
static bool DragonValidateBattleEncounterTelemetry (int minimumEnemyCount, bool expectNoRun, const char* expectedActionText, bool requireTargetRectsInsideSafe);
static bool DragonBuildBattleCommandRect (int commandIndex, ERect& outRect);

static bool DragonParseBattleHealthTelemetry (const char* expectedFragment, int& outCurrentHealth, int& outMaxHealth, ERect* outRect = nullptr) {
	EString healthLine;
	ERect healthRect;
	if(DragonFindReportedTextLineContaining(expectedFragment != nullptr ? expectedFragment : "HP ", &healthLine, &healthRect) == false)
		return false;
	if(std::sscanf((const char*)healthLine, "%*[^0-9]%d/%d", &outCurrentHealth, &outMaxHealth) != 2)
		return false;
	if(outRect != nullptr)
		*outRect = healthRect;
	return true;
}

static bool DragonValidateBattleLegacyMenuTelemetry (const char* expectedHeader, const char* expectedPrimary, const char* expectedSecondary) {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect headerRect;
	ERect selectedRect;
	ERect targetRect;
	ERect primaryRect;
	ERect secondaryRect;
	ERect cancelRect;
	ERect commandLeftRect;
	ERect commandRightRect;
	int enemyCount = 0;
	const bool hasEnemyCount = DragonReadBattleEnemyCountTelemetry(enemyCount);
	const bool enoughEnemies = hasEnemyCount && enemyCount >= 1;
	const bool hasHeader = DragonFindReportedTextContaining(expectedHeader != nullptr ? expectedHeader : "MAGIC", &headerRect);
	const bool hasSelected = DragonFindReportedTextContaining("Selected target index:", &selectedRect);
	const bool hasTarget = DragonFindReportedTextContaining("Enemy target 0:", &targetRect);
	const bool hasPrimary = DragonFindReportedTextContaining(expectedPrimary != nullptr ? expectedPrimary : "Cure", &primaryRect);
	const bool hasSecondary = DragonFindReportedTextContaining(expectedSecondary != nullptr ? expectedSecondary : "Guard Heal", &secondaryRect);
	const bool hasCancel = DragonFindReportedTextContaining("CANCEL", &cancelRect);
	const bool hasCommandLeft = DragonBuildBattleCommandRect(0, commandLeftRect);
	const bool hasCommandRight = DragonBuildBattleCommandRect(4, commandRightRect);

	const int headerCenterX = headerRect.x + headerRect.width / 2;
	const int primaryCenterX = primaryRect.x + primaryRect.width / 2;
	const int secondaryCenterX = secondaryRect.x + secondaryRect.width / 2;
	const int cancelCenterX = cancelRect.x + cancelRect.width / 2;
	const int safeCenterX = safeRect.x + safeRect.width / 2;
	const bool menuCentered = std::abs(headerCenterX - safeCenterX) <= 12
		&& std::abs(primaryCenterX - safeCenterX) <= 12
		&& std::abs(secondaryCenterX - safeCenterX) <= 12
		&& std::abs(cancelCenterX - safeCenterX) <= 12;
	const bool menuVerticalOrder = headerRect.y < primaryRect.y
		&& primaryRect.y < secondaryRect.y
		&& secondaryRect.y < cancelRect.y;
	const int commandLaneTop = hasCommandLeft && hasCommandRight
		? std::min(commandLeftRect.y, commandRightRect.y)
		: safeRect.y + safeRect.height;
	const bool menuAboveCommandLane = cancelRect.y + cancelRect.height <= commandLaneTop - 8;

	int matched = 0;
	if(enoughEnemies) matched++;
	if(hasHeader) matched++;
	if(hasSelected) matched++;
	if(hasTarget) matched++;
	if(hasPrimary) matched++;
	if(hasSecondary) matched++;
	if(hasCancel) matched++;
	if(menuCentered) matched++;
	if(menuVerticalOrder) matched++;
	if(menuAboveCommandLane) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	return enoughEnemies
		&& hasHeader
		&& hasSelected
		&& hasTarget
		&& hasPrimary
		&& hasSecondary
		&& hasCancel
		&& hasCommandLeft
		&& hasCommandRight
		&& menuCentered
		&& menuVerticalOrder
		&& menuAboveCommandLane
		&& DragonRectInsideRect(safeRect, headerRect)
		&& DragonRectInsideRect(safeRect, primaryRect)
		&& DragonRectInsideRect(safeRect, secondaryRect)
		&& DragonRectInsideRect(safeRect, cancelRect);
}

static bool DragonValidateBattleElementalOutcomeTelemetry (const char* expectedResultFragment, const char* expectedTargetName, int baselineCurrentHealth, int expectedMaxHealth, bool expectIncrease) {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect serialRect;
	ERect resultRect;
	ERect nameRect;
	ERect targetRect;
	int targetCurrentHealth = 0;
	int targetMaxHealth = 0;
	const bool baseValid = DragonValidateBattleEncounterTelemetry(1, false, nullptr, false);
	const bool hasSerial = DragonFindReportedTextContaining("Player action serial: 1", &serialRect);
	const bool hasResult = DragonFindReportedTextContaining(expectedResultFragment != nullptr ? expectedResultFragment : "Player action result:", &resultRect);
	const bool hasTargetName = DragonFindReportedTextContaining(expectedTargetName != nullptr ? expectedTargetName : "Enemy", &nameRect);
	const bool hasTargetHealth = DragonParseBattleHealthTelemetry("HP ", targetCurrentHealth, targetMaxHealth, &targetRect);
	const bool targetHealthDirectionValid = hasTargetHealth == false
		|| (hasTargetName
			&& targetCurrentHealth >= 0
			&& targetCurrentHealth <= targetMaxHealth
			&& targetMaxHealth == expectedMaxHealth
			&& (expectIncrease ? (targetCurrentHealth > baselineCurrentHealth) : (targetCurrentHealth < baselineCurrentHealth)));
	if(baseValid)
		gDragonValidation.matchedTextDrawCount += (hasSerial ? 1 : 0) + (hasResult ? 1 : 0) + (hasTargetName ? 1 : 0) + (targetHealthDirectionValid ? 1 : 0);
	return baseValid
		&& hasSerial
		&& hasResult
		&& hasTargetName
		&& targetHealthDirectionValid
		&& DragonRectInsideRect(safeRect, serialRect)
		&& DragonRectInsideRect(safeRect, resultRect)
		&& DragonRectInsideRect(safeRect, nameRect)
		&& (hasTargetHealth == false || DragonRectInsideRect(safeRect, targetRect));
}

static bool DragonValidateBattleCompletionTelemetry (const char* expectedHeader, const char* expectedResultFragment = nullptr) {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect headerRect;
	ERect continueRect;
	ERect resultRect;
	const bool hasHeader = DragonFindReportedTextContaining(
		expectedHeader != nullptr ? expectedHeader : "VICTORY",
		&headerRect
	);
	const bool hasContinue = DragonFindReportedTextContaining("CONTINUE", &continueRect);
	const bool hasResult = expectedResultFragment == nullptr
		|| expectedResultFragment[0] == '\0'
		|| DragonFindReportedTextContaining(expectedResultFragment, &resultRect);

	int matched = 0;
	if(hasHeader) matched++;
	if(hasContinue) matched++;
	if(hasResult) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	return hasHeader
		&& hasContinue
		&& hasResult
		&& DragonRectInsideRect(safeRect, headerRect)
		&& DragonRectInsideRect(safeRect, continueRect)
		&& (expectedResultFragment == nullptr
			|| expectedResultFragment[0] == '\0'
			|| DragonRectInsideRect(safeRect, resultRect));
}

static bool DragonValidateBattleSeededOutcomeTelemetry (const char* expectedResult, const char* expectedTargetHealth) {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect serialRect;
	ERect resultRect;
	ERect targetHealthRect;
	const bool requireTargetHealth = expectedTargetHealth != nullptr && expectedTargetHealth[0] != '\0';
	const bool baseValid = DragonValidateBattleTelemetry(2);
	const bool hasSerial = DragonFindReportedTextContaining("Player action serial: 1", &serialRect);
	const bool hasResult = DragonFindReportedTextContaining(expectedResult != nullptr ? expectedResult : "Player action result:", &resultRect);
	const bool hasTargetHealth = requireTargetHealth == false
		|| DragonFindReportedTextContaining(expectedTargetHealth, &targetHealthRect);
	if(baseValid)
		gDragonValidation.matchedTextDrawCount += (hasSerial ? 1 : 0) + (hasResult ? 1 : 0) + (hasTargetHealth ? 1 : 0);
	return baseValid
		&& hasSerial
		&& hasResult
		&& hasTargetHealth
		&& DragonRectInsideRect(safeRect, serialRect)
		&& DragonRectInsideRect(safeRect, resultRect)
		&& (requireTargetHealth == false || DragonRectInsideRect(safeRect, targetHealthRect));
}

static bool DragonValidateBattleSelfEffectTelemetry (const char* expectedResult, const char* expectedPlayerHealth) {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect serialRect;
	ERect resultRect;
	ERect playerHealthRect;
	const bool requirePlayerHealth = expectedPlayerHealth != nullptr && expectedPlayerHealth[0] != '\0';
	const bool baseValid = DragonValidateBattleEncounterTelemetry(4, false, nullptr, false);
	const bool hasSerial = DragonFindReportedTextContaining("Player action serial: 1", &serialRect);
	const bool hasResult = DragonFindReportedTextContaining(expectedResult != nullptr ? expectedResult : "Player action result:", &resultRect);
	const bool hasPlayerHealth = requirePlayerHealth == false
		|| DragonFindReportedTextContaining(expectedPlayerHealth, &playerHealthRect);
	if(baseValid)
		gDragonValidation.matchedTextDrawCount += (hasSerial ? 1 : 0) + (hasResult ? 1 : 0) + (hasPlayerHealth ? 1 : 0);
	return baseValid
		&& hasSerial
		&& hasResult
		&& hasPlayerHealth
		&& DragonRectInsideRect(safeRect, serialRect)
		&& DragonRectInsideRect(safeRect, resultRect)
		&& (requirePlayerHealth == false || DragonRectInsideRect(safeRect, playerHealthRect));
}

static bool DragonValidateBattleMultiEnemyContinueTelemetry () {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect serialRect;
	ERect resultRect;
	ERect countRect;
	ERect selectedRect;
	ERect targetRect;
	const bool hasSerial = DragonFindReportedTextContaining("Player action serial: 1", &serialRect);
	const bool hasResult = DragonFindReportedTextContaining("Player action result: Attack deals", &resultRect)
		|| DragonFindReportedTextContaining("Player action result: Critical hit!", &resultRect);
	const bool hasEnemyCount = DragonFindReportedTextContaining("Enemy count: 3", &countRect);
	const bool hasSelected = DragonFindReportedTextContaining("Selected target index: 1", &selectedRect);
	const bool hasTarget = DragonFindReportedTextContaining("Enemy target 1:", &targetRect);
	const bool enoughTargets = DragonCountReportedTextContaining("Enemy target ") >= 3;

	int matched = 0;
	if(hasSerial) matched++;
	if(hasResult) matched++;
	if(hasEnemyCount) matched++;
	if(hasSelected) matched++;
	if(hasTarget) matched++;
	if(enoughTargets) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	return hasSerial
		&& hasResult
		&& hasEnemyCount
		&& hasSelected
		&& hasTarget
		&& enoughTargets
		&& DragonRectInsideRect(safeRect, serialRect)
		&& DragonRectInsideRect(safeRect, resultRect)
		&& DragonRectInsideRect(safeRect, countRect)
		&& DragonRectInsideRect(safeRect, selectedRect)
		&& DragonRectInsideRect(safeRect, targetRect);
}

static bool DragonValidateBattleEnemyActionTelemetry (const char* expectedResult, const char* expectedPlayerHealth = nullptr, const char* expectedEnemyHealth = nullptr) {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect serialRect;
	ERect resultRect;
	ERect playerHealthRect;
	ERect enemyHealthRect;
	const bool baseValid = DragonValidateBattleEncounterTelemetry(1, false, nullptr, false);
	const bool hasSerial = DragonFindReportedTextContaining("Enemy action serial: 1", &serialRect);
	const bool hasResult = DragonFindReportedTextContaining(expectedResult != nullptr ? expectedResult : "Enemy action result:", &resultRect);
	const bool hasPlayerHealth = expectedPlayerHealth == nullptr
		|| expectedPlayerHealth[0] == '\0'
		|| DragonFindReportedTextContaining(expectedPlayerHealth, &playerHealthRect);
	const bool hasEnemyHealth = expectedEnemyHealth == nullptr
		|| expectedEnemyHealth[0] == '\0'
		|| DragonFindReportedTextContaining(expectedEnemyHealth, &enemyHealthRect);
	if(baseValid)
		gDragonValidation.matchedTextDrawCount += (hasSerial ? 1 : 0) + (hasResult ? 1 : 0) + (hasPlayerHealth ? 1 : 0) + (hasEnemyHealth ? 1 : 0);
	return baseValid
		&& hasSerial
		&& hasResult
		&& hasPlayerHealth
		&& hasEnemyHealth
		&& DragonRectInsideRect(safeRect, serialRect)
		&& DragonRectInsideRect(safeRect, resultRect)
		&& (expectedPlayerHealth == nullptr || expectedPlayerHealth[0] == '\0' || DragonRectInsideRect(safeRect, playerHealthRect))
		&& (expectedEnemyHealth == nullptr || expectedEnemyHealth[0] == '\0' || DragonRectInsideRect(safeRect, enemyHealthRect));
}

static bool DragonReadBattleEnemyCountTelemetry (int& outEnemyCount) {
	EString enemyCountLine;
	if(DragonFindReportedTextLineContaining("Enemy count:", &enemyCountLine) == false)
		return false;
	return std::sscanf((const char*)enemyCountLine, "Enemy count: %d", &outEnemyCount) == 1;
}

static bool DragonValidateBattleEncounterTelemetry (int minimumEnemyCount, bool expectNoRun, const char* expectedActionText = nullptr, bool requireTargetRectsInsideSafe = true) {
	const ERect safeRect = ESystem::GetSafeRect();
	ERect attackRect;
	ERect defendRect;
	ERect magicRect;
	ERect techRect;
	ERect runRect;
	ERect selectedRect;
	ERect targetRect;
	ERect actionTextRect;
	int enemyCount = 0;
	const bool hasAttack = DragonFindReportedTextContaining("ATTACK", &attackRect);
	const bool hasDefend = DragonFindReportedTextContaining("DEFEND", &defendRect);
	const bool hasMagic = DragonFindReportedTextContaining("MAGIC", &magicRect);
	const bool hasTech = DragonFindReportedTextContaining("TECH", &techRect);
	const bool hasRun = DragonFindReportedTextContaining(expectNoRun ? "NO RUN" : "RUN", &runRect);
	const bool hasEnemyCount = DragonReadBattleEnemyCountTelemetry(enemyCount);
	const bool enoughEnemies = hasEnemyCount && enemyCount >= std::max(1, minimumEnemyCount);
	const bool hasSelected = DragonFindReportedTextContaining("Selected target index:", &selectedRect);
	const bool hasTarget = DragonFindReportedTextContaining("Enemy target 0:", &targetRect);
	const bool hasActionText = expectedActionText == nullptr
		|| expectedActionText[0] == '\0'
		|| DragonFindReportedTextContaining(expectedActionText, &actionTextRect);

	int matched = 0;
	if(hasAttack) matched++;
	if(hasDefend) matched++;
	if(hasMagic) matched++;
	if(hasTech) matched++;
	if(hasRun) matched++;
	if(enoughEnemies) matched++;
	if(hasSelected) matched++;
	if(hasTarget) matched++;
	if(hasActionText) matched++;
	gDragonValidation.matchedTextDrawCount = matched;

	return hasAttack
		&& hasDefend
		&& hasMagic
		&& hasTech
		&& hasRun
		&& enoughEnemies
		&& hasSelected
		&& hasTarget
		&& hasActionText
		&& DragonRectInsideRect(safeRect, attackRect)
		&& DragonRectInsideRect(safeRect, defendRect)
		&& DragonRectInsideRect(safeRect, magicRect)
		&& DragonRectInsideRect(safeRect, techRect)
		&& DragonRectInsideRect(safeRect, runRect)
		&& (requireTargetRectsInsideSafe == false || DragonRectInsideRect(safeRect, targetRect))
		&& (requireTargetRectsInsideSafe == false || DragonRectInsideRect(safeRect, selectedRect))
		&& (expectedActionText == nullptr
			|| expectedActionText[0] == '\0'
			|| DragonRectInsideRect(safeRect, actionTextRect));
}

static void DragonInjectTap (const ERect& rect) {
	const int touchX = rect.x + rect.width / 2;
	const int touchY = rect.y + rect.height / 2;
	ESystem::RunTouchCallbacks(touchX, touchY);
	ESystem::RunTouchUpCallbacks(touchX, touchY);
}

static void DragonInjectPoint (int x, int y) {
	ESystem::RunTouchCallbacks(x, y);
	ESystem::RunTouchUpCallbacks(x, y);
}

static bool DragonBuildBattleCommandRect (int commandIndex, ERect& outRect) {
	if(commandIndex < 0 || commandIndex >= 5)
		return false;

	const ERect preferredView = MakePreferredViewRect(ESystem::GetSafeRect(), DESIGN_PREFERRED_WIDTH, DESIGN_PREFERRED_HEIGHT);
	const LegacyCanvas canvas = MakeLegacyCanvas(preferredView, 800, 600);
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
	outRect = ClipRectToBounds(ERect(startX + commandIndex * (buttonWidth + gap), buttonY, buttonWidth, buttonHeight), safe);
	return outRect.width > 0 && outRect.height > 0;
}

static bool DragonInjectBattleCommandTap (int commandIndex) {
	ERect rect;
	if(DragonBuildBattleCommandRect(commandIndex, rect) == false)
		return false;
	DragonInjectPoint(rect.x + rect.width / 2, rect.y + rect.height / 2);
	return true;
}

static bool DragonInjectReportedTextTapContaining (const char* fragment) {
	ERect rect;
	if(DragonFindReportedTextContaining(fragment, &rect) == false)
		return false;
	DragonInjectTap(rect);
	return true;
}

static bool DragonInjectReportedTextBottomTapContaining (const char* fragment) {
	ERect rect;
	if(DragonFindReportedTextContaining(fragment, &rect) == false)
		return false;
	const int touchX = rect.x + rect.width / 2;
	const int touchY = rect.y + std::max(1, rect.height - 2);
	DragonInjectPoint(touchX, touchY);
	return true;
}

static void DragonInjectSplashAboutTap () {
	if(DragonInjectReportedTextTapContaining("ABOUT DRAGON ALPHA"))
		return;
	const LegacyCanvas canvas = MakeLegacyCanvas(ESystem::GetSafeRect(), 800, 600);
	const ERect aboutRect = LegacyRect(canvas, 8, 230, 296, 30);
	DragonInjectTap(aboutRect);
}

static void DragonInjectSplashNewGameTap () {
	if(DragonInjectReportedTextTapContaining("NEW GAME"))
		return;
	const LegacyCanvas canvas = MakeLegacyCanvas(ESystem::GetSafeRect(), 800, 600);
	const ERect newGameRect = LegacyRect(canvas, 8, 329, 296, 30);
	DragonInjectTap(newGameRect);
}

static void DragonInjectSplashOpenGameTap () {
	if(DragonInjectReportedTextTapContaining("OPEN GAME..."))
		return;
	const LegacyCanvas canvas = MakeLegacyCanvas(ESystem::GetSafeRect(), 800, 600);
	const ERect openGameRect = LegacyRect(canvas, 8, 370, 296, 30);
	DragonInjectTap(openGameRect);
}

static void DragonBuildStatusLayoutRects (ERect* outWeaponRect = nullptr, ERect* outArmorRect = nullptr, ERect* outRelicRect = nullptr, ERect* outActionRect = nullptr, ERect* outSellRect = nullptr, ERect* outBackRect = nullptr) {
	const LegacyCanvas canvas = MakeLegacyCanvas(ESystem::GetSafeRect(), 800, 600);
	const ERect safe = canvas.frame;
	const ERect statsRect = ClipRectToBounds(LegacyRect(canvas, 194, 120, 570, 118), safe);
	const int gearRight = statsRect.x + statsRect.width - 84;
	const int gearWidth = std::max(200, gearRight - (statsRect.x + 8));
	const int gearLabelHeight = 22;
	const ERect weaponRect(statsRect.x + 8, statsRect.y + 66, gearWidth, gearLabelHeight);
	const ERect armorRect(statsRect.x + 8, weaponRect.y + weaponRect.height + 2, (gearWidth - 8) / 2, gearLabelHeight);
	const ERect relicRect(armorRect.x + armorRect.width + 8, armorRect.y, armorRect.width, gearLabelHeight);

	const int buttonY = std::min(
		LegacyRect(canvas, 0, 500, 0, 0).y,
		safe.y + safe.height - std::max(28, LegacyRect(canvas, 0, 0, 0, 34).height) - 2
	);
	const int buttonGap = std::max(8, LegacyRect(canvas, 0, 0, 8, 0).width);
	const int buttonHeight = std::max(28, LegacyRect(canvas, 0, 0, 0, 34).height);
	const int buttonWidth = (LegacyRect(canvas, 194, 0, 570, 0).width - buttonGap * 4) / 5;
	const int buttonStartX = LegacyRect(canvas, 194, 0, 0, 0).x;
	const ERect previousRect(buttonStartX, buttonY, buttonWidth, buttonHeight);
	const ERect nextRect(previousRect.x + previousRect.width + buttonGap, buttonY, buttonWidth, buttonHeight);
	const ERect actionRect(nextRect.x + nextRect.width + buttonGap, buttonY, buttonWidth, buttonHeight);
	const ERect sellRect(actionRect.x + actionRect.width + buttonGap, buttonY, buttonWidth, buttonHeight);
	const ERect backRect(sellRect.x + sellRect.width + buttonGap, buttonY, buttonWidth, buttonHeight);

	if(outWeaponRect != nullptr)
		*outWeaponRect = weaponRect;
	if(outArmorRect != nullptr)
		*outArmorRect = armorRect;
	if(outRelicRect != nullptr)
		*outRelicRect = relicRect;
	if(outActionRect != nullptr)
		*outActionRect = actionRect;
	if(outSellRect != nullptr)
		*outSellRect = sellRect;
	if(outBackRect != nullptr)
		*outBackRect = backRect;
}

static void DragonInjectStatusArmorTap () {
	ERect armorRect;
	DragonBuildStatusLayoutRects(nullptr, &armorRect);
	DragonInjectPoint(armorRect.x + armorRect.width / 2, armorRect.y + std::max(1, armorRect.height - 2));
}

static void DragonInjectStatusRelicTap () {
	ERect relicRect;
	DragonBuildStatusLayoutRects(nullptr, nullptr, &relicRect);
	DragonInjectPoint(relicRect.x + relicRect.width / 2, relicRect.y + std::max(1, relicRect.height - 2));
}

static void DragonInjectStatusActionTap () {
	ERect actionRect;
	DragonBuildStatusLayoutRects(nullptr, nullptr, nullptr, &actionRect);
	DragonInjectTap(actionRect);
}

static ERect DragonBuildNewGameSlotRect (int slotIndex) {
	const LegacyCanvas canvas = MakeLegacyCanvas(ESystem::GetSafeRect(), 800, 600);
	const int gap = std::max(10, LegacyRect(canvas, 0, 0, 12, 0).width);
	const int cardsX = LegacyRect(canvas, 194, 0, 0, 0).x;
	const int cardsY = LegacyRect(canvas, 136, 0, 0, 0).y;
	const int cardsWidth = LegacyRect(canvas, 194, 0, 570, 0).width;
	const int cardWidth = (cardsWidth - gap * 2) / 3;
	const int cardHeight = std::max(108, LegacyRect(canvas, 0, 0, 0, 138).height);
	const int normalizedSlot = std::max(0, std::min(slotIndex, 2));
	return ERect(cardsX + normalizedSlot * (cardWidth + gap), cardsY, cardWidth, cardHeight);
}

static ERect DragonBuildNewGameDeleteRect (int slotIndex) {
	const ERect slotRect = DragonBuildNewGameSlotRect(slotIndex);
	const int deleteWidth = std::max(42, std::min(64, slotRect.width / 4));
	const int deleteHeight = std::max(24, std::min(30, slotRect.height / 3));
	return ERect(slotRect.x + slotRect.width - deleteWidth - 8, slotRect.y + 8, deleteWidth, deleteHeight);
}

static void DragonInjectNewGameSlotTap (int slotIndex) {
	DragonInjectTap(DragonBuildNewGameSlotRect(slotIndex));
}

static void DragonInjectNewGameDeleteTap (int slotIndex) {
	DragonInjectTap(DragonBuildNewGameDeleteRect(slotIndex));
}

static void DragonInjectNewAvatarClassTap (int avatarIndex) {
	const LegacyCanvas canvas = MakeLegacyCanvas(ESystem::GetSafeRect(), 800, 600);
	const int gap = std::max(10, LegacyRect(canvas, 0, 0, 12, 0).width);
	const int cardsX = LegacyRect(canvas, 194, 0, 0, 0).x;
	const int cardsY = LegacyRect(canvas, 140, 0, 0, 0).y;
	const int cardsWidth = LegacyRect(canvas, 194, 0, 570, 0).width;
	const int cardWidth = (cardsWidth - gap * 2) / 3;
	const int cardHeight = std::max(132, LegacyRect(canvas, 0, 0, 0, 188).height);
	const int normalizedAvatar = std::max(0, std::min(avatarIndex, 2));
	const ERect avatarRect(cardsX + normalizedAvatar * (cardWidth + gap), cardsY, cardWidth, cardHeight);
	DragonInjectTap(avatarRect);
}

static void DragonInjectNewAvatarBeginGameTap () {
	if(DragonInjectReportedTextTapContaining("BEGIN GAME"))
		return;
	const LegacyCanvas canvas = MakeLegacyCanvas(ESystem::GetSafeRect(), 800, 600);
	const int gap = std::max(10, LegacyRect(canvas, 0, 0, 12, 0).width);
	const ERect nextNameRect = LegacyRect(canvas, 194, 430, 570, 34);
	ERect createRect = LegacyRect(canvas, 194, 472, 0, 34);
	createRect.width = (nextNameRect.width - gap) / 2;
	DragonInjectTap(createRect);
}

static void DragonInjectWorldMapStatusTap () {
	if(DragonInjectReportedTextTapContaining("EQUIPMENT"))
		return;
	const LegacyCanvas canvas = MakeLegacyCanvas(ESystem::GetSafeRect(), 800, 600);
	const ERect statusRect = LegacyRect(canvas, 8, 329, 148, 30);
	DragonInjectTap(statusRect);
}

static void DragonInjectWorldMapActionTap () {
	const LegacyCanvas canvas = MakeLegacyCanvas(ESystem::GetSafeRect(), 800, 600);
	const ERect actionRect = LegacyRect(canvas, 8, 370, 148, 30);
	DragonInjectTap(actionRect);
}

static void DragonInjectWorldMapTechTap () {
	const LegacyCanvas canvas = MakeLegacyCanvas(ESystem::GetSafeRect(), 800, 600);
	const ERect techRect = LegacyRect(canvas, 8, 411, 148, 30);
	DragonInjectTap(techRect);
}

static void DragonInjectWorldMapSaveTap () {
	const LegacyCanvas canvas = MakeLegacyCanvas(ESystem::GetSafeRect(), 800, 600);
	const ERect saveRect = LegacyRect(canvas, 8, 271, 148, 30);
	DragonInjectTap(saveRect);
}

static void DragonInjectWorldMapMenuTap () {
	const LegacyCanvas canvas = MakeLegacyCanvas(ESystem::GetSafeRect(), 800, 600);
	const ERect menuRect = LegacyRect(canvas, 8, 230, 148, 30);
	DragonInjectTap(menuRect);
}

static bool DragonBuildWorldMapCellRect (int playerMapX, int playerMapY, int targetMapX, int targetMapY, ERect& outRect) {
	static constexpr int kViewCols = 21;
	static constexpr int kViewRows = 16;
	static constexpr int kFocusCol = 10;
	static constexpr int kFocusRow = 7;
	const LegacyCanvas canvas = MakeLegacyCanvas(ESystem::GetSafeRect(), 800, 600);
	const ERect gridFrameRect = LegacyRect(canvas, 128, 88, 672, 512);
	const int visibleCols = std::min(DRAGON_ALPHA_MAP_WIDTH, kViewCols);
	const int visibleRows = std::min(DRAGON_ALPHA_MAP_HEIGHT, kViewRows);
	const int visibleStartX = std::max(0, std::min(DRAGON_ALPHA_MAP_WIDTH - visibleCols, playerMapX - std::max(0, std::min(kFocusCol, visibleCols - 1))));
	const int visibleStartY = std::max(0, std::min(DRAGON_ALPHA_MAP_HEIGHT - visibleRows, playerMapY - std::max(0, std::min(kFocusRow, visibleRows - 1))));
	if(targetMapX < visibleStartX
		|| targetMapX >= visibleStartX + visibleCols
		|| targetMapY < visibleStartY
		|| targetMapY >= visibleStartY + visibleRows) {
		return false;
	}

	const int tileWidth = std::max(1, gridFrameRect.width / visibleCols);
	const int tileHeight = std::max(1, gridFrameRect.height / visibleRows);
	const int tileSize = std::max(1, std::min(tileWidth, tileHeight));
	const int mapDrawWidth = tileSize * visibleCols;
	const int mapDrawHeight = tileSize * visibleRows;
	const ERect mapTileFrameRect(
		gridFrameRect.x + (gridFrameRect.width - mapDrawWidth) / 2,
		gridFrameRect.y + (gridFrameRect.height - mapDrawHeight) / 2,
		mapDrawWidth,
		mapDrawHeight
	);
	outRect = ERect(
		mapTileFrameRect.x + (targetMapX - visibleStartX) * tileSize,
		mapTileFrameRect.y + (targetMapY - visibleStartY) * tileSize,
		tileSize,
		tileSize
	);
	return true;
}

static bool DragonInjectWorldMapCellTap (int targetMapX, int targetMapY) {
	int playerMapX = 0;
	int playerMapY = 0;
	if(DragonReadWorldMapPositionTelemetry(playerMapX, playerMapY) == false)
		return false;
	ERect targetRect;
	if(DragonBuildWorldMapCellRect(playerMapX, playerMapY, targetMapX, targetMapY, targetRect) == false)
		return false;
	DragonInjectTap(targetRect);
	return true;
}

static bool DragonInjectWorldMapEventTap (int eventIndex) {
	int targetMapX = 0;
	int targetMapY = 0;
	if(DragonReadWorldMapEventPositionTelemetry(eventIndex, targetMapX, targetMapY) == false)
		return false;
	return DragonInjectWorldMapCellTap(targetMapX, targetMapY);
}

static bool DragonInjectWorldMapEventTapByType (const char* eventType) {
	int eventIndex = -1;
	int targetMapX = 0;
	int targetMapY = 0;
	if(DragonReadWorldMapEventPositionByTypeTelemetry(eventType, eventIndex, targetMapX, targetMapY) == false)
		return false;
	return DragonInjectWorldMapCellTap(targetMapX, targetMapY);
}

static bool DragonPrepareValidationSlot (int avatarType, const char* name, int gold) {
	if(DragonCreateSlot(1, avatarType, name) == false)
		return false;
	gSave.gold = gold;
	gSave.health = (int16_t)DragonGetMaxHealth();
	gSelectedArea = std::max(0, std::min((int)gSave.areaIndex, DRAGON_ALPHA_AREA_COUNT - 1));
	return DragonSaveCurrentSlot() && DragonSaveWorldState();
}

static bool DragonPrepareBattleInjuredValidationSlot (int health) {
	if(DragonPrepareValidationSlot(DRAGON_AVATAR_WARRIOR, "ARIN", 150) == false)
		return false;
	const int maxHealth = DragonGetMaxHealth();
	gSave.health = (int16_t)std::max(1, std::min(maxHealth, health));
	return DragonSaveCurrentSlot() && DragonSaveWorldState();
}

static bool DragonPrepareStatusValidationSlot (int health, int gold) {
	if(DragonPrepareValidationSlot(DRAGON_AVATAR_WARRIOR, "ARIN", gold) == false)
		return false;
	const int maxHealth = DragonGetMaxHealth();
	gSave.health = (int16_t)std::max(1, std::min(maxHealth, health));
	return DragonSaveCurrentSlot() && DragonSaveWorldState();
}

static bool DragonPrepareStatusValidationSlotForAvatar (int avatarType, const char* name, int health, int gold) {
	if(DragonPrepareValidationSlot(avatarType, name, gold) == false)
		return false;
	const int maxHealth = DragonGetMaxHealth();
	gSave.health = (int16_t)std::max(1, std::min(maxHealth, health));
	return DragonSaveCurrentSlot() && DragonSaveWorldState();
}

static bool DragonPrepareStatusEquipValidationSlot () {
	if(DragonPrepareStatusValidationSlot(125, 10) == false)
		return false;
	if(DragonUnequipSlot(DRAGON_SLOT_WEAPON) == false)
		return false;
	return DragonSaveCurrentSlot() && DragonSaveWorldState();
}

static bool DragonPrepareStatusEquipArmorValidationSlot () {
	if(DragonPrepareStatusValidationSlot(125, 10) == false)
		return false;
	if(DragonUnequipSlot(DRAGON_SLOT_ARMOR) == false)
		return false;
	return DragonSaveCurrentSlot() && DragonSaveWorldState();
}

static bool DragonPrepareStatusRelicValidationSlot () {
	return DragonPrepareStatusValidationSlotForAvatar(DRAGON_AVATAR_SORCERER, "LYRA", 96, 10);
}

static bool DragonPrepareStatusEquipRelicValidationSlot () {
	if(DragonPrepareStatusRelicValidationSlot() == false)
		return false;
	if(DragonUnequipSlot(DRAGON_SLOT_RELIC) == false)
		return false;
	return DragonSaveCurrentSlot() && DragonSaveWorldState();
}

static bool DragonPrepareCustomValidationSlot (int slot, int avatarType, const char* name, int level, int gold, int areaIndex, int battlesWon, int battlesLost);

static bool DragonPrepareStatusSellEquippedWeaponValidationSlot () {
	if(DragonPrepareStatusValidationSlot(125, 10) == false)
		return false;
	if(DragonInventoryAdd(DRAGON_ITEM_IRON_BLADE, 1) == false)
		return false;
	return DragonSaveCurrentSlot() && DragonSaveWorldState();
}

static bool DragonPrepareStatusFullInventoryValidationSlot () {
	static const uint8_t kInventoryTypes[DRAGON_ALPHA_INVENTORY_CAPACITY] = {
		DRAGON_ITEM_ADEPT_WAND,
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
		DRAGON_ITEM_ADEPT_WAND,
		DRAGON_ITEM_STEEL_SABER,
		DRAGON_ITEM_WAR_HAMMER,
		DRAGON_ITEM_MYSTIC_TOME,
		DRAGON_ITEM_LEATHER_ARMOR,
		DRAGON_ITEM_TRAVELER_CLOAK,
		DRAGON_ITEM_TOWER_SHIELD,
		DRAGON_ITEM_SCOUT_CHARM,
		DRAGON_ITEM_SEER_CHARM,
		DRAGON_ITEM_HEALTH_POTION,
		DRAGON_ITEM_FIRE_SCROLL,
	};

	if(DragonPrepareStatusValidationSlot(125, 10) == false)
		return false;
	gSave.inventoryCount = DRAGON_ALPHA_INVENTORY_CAPACITY;
	for(int i = 0; i < DRAGON_ALPHA_INVENTORY_CAPACITY; i++) {
		gSave.inventory[i].type = kInventoryTypes[i];
		gSave.inventory[i].count = 1;
	}
	return DragonSaveCurrentSlot() && DragonSaveWorldState();
}

static bool DragonPrepareStatusLargeInventoryValidationSlot () {
	static const uint8_t kInventoryTypes[DRAGON_ALPHA_INVENTORY_CAPACITY] = {
		DRAGON_ITEM_ADEPT_WAND,
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
		DRAGON_ITEM_ADEPT_WAND,
		DRAGON_ITEM_STEEL_SABER,
		DRAGON_ITEM_WAR_HAMMER,
		DRAGON_ITEM_MYSTIC_TOME,
		DRAGON_ITEM_LEATHER_ARMOR,
		DRAGON_ITEM_TRAVELER_CLOAK,
		DRAGON_ITEM_TOWER_SHIELD,
		DRAGON_ITEM_SCOUT_CHARM,
		DRAGON_ITEM_SEER_CHARM,
		DRAGON_ITEM_HEALTH_POTION,
		DRAGON_ITEM_FIRE_SCROLL,
	};

	if(DragonPrepareCustomValidationSlot(1, DRAGON_AVATAR_WARRIOR, "ARIN", 18, 777, DRAGON_AREA_CAVES, 12, 3) == false)
		return false;
	gSave.inventoryCount = DRAGON_ALPHA_INVENTORY_CAPACITY;
	for(int i = 0; i < DRAGON_ALPHA_INVENTORY_CAPACITY; i++) {
		gSave.inventory[i].type = kInventoryTypes[i];
		gSave.inventory[i].count = 1;
	}
	gSave.health = (int16_t)DragonGetMaxHealth();
	return DragonSaveCurrentSlot() && DragonSaveWorldState();
}

static bool DragonPrepareShopDuplicateOwnedValidationSlot () {
	if(DragonPrepareValidationSlot(DRAGON_AVATAR_WARRIOR, "ARIN", 999) == false)
		return false;
	if(DragonInventoryAdd(DRAGON_ITEM_HUNTER_BOW, 1) == false)
		return false;
	return DragonSaveCurrentSlot();
}

static bool DragonPrepareShopInventoryFullValidationSlot () {
	static const uint8_t kInventoryTypes[DRAGON_ALPHA_INVENTORY_CAPACITY] = {
		DRAGON_ITEM_IRON_BLADE,
		DRAGON_ITEM_ADEPT_WAND,
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
		DRAGON_ITEM_ADEPT_WAND,
		DRAGON_ITEM_STEEL_SABER,
		DRAGON_ITEM_WAR_HAMMER,
		DRAGON_ITEM_MYSTIC_TOME,
		DRAGON_ITEM_TRAVELER_CLOAK,
		DRAGON_ITEM_TOWER_SHIELD,
		DRAGON_ITEM_SCOUT_CHARM,
		DRAGON_ITEM_SEER_CHARM,
		DRAGON_ITEM_HEALTH_POTION,
		DRAGON_ITEM_FIRE_SCROLL,
	};

	if(DragonPrepareValidationSlot(DRAGON_AVATAR_WARRIOR, "ARIN", 999) == false)
		return false;
	gSave.inventoryCount = DRAGON_ALPHA_INVENTORY_CAPACITY;
	for(int i = 0; i < DRAGON_ALPHA_INVENTORY_CAPACITY; i++) {
		gSave.inventory[i].type = kInventoryTypes[i];
		gSave.inventory[i].count = 1;
	}
	return DragonSaveCurrentSlot();
}

static bool DragonPrepareCustomValidationSlot (int slot, int avatarType, const char* name, int level, int gold, int areaIndex, int battlesWon, int battlesLost) {
	if(DragonCreateSlot(slot, avatarType, name) == false)
		return false;

	gSave.level = (uint8_t)std::max(1, std::min(level, DRAGON_RUNTIME_LEVEL_CAP));
	gSave.areaIndex = (uint8_t)std::max(0, std::min(areaIndex, DRAGON_ALPHA_AREA_COUNT - 1));
	gSave.gold = gold;
	gSave.xp = (uint32_t)std::max(0, DragonGetLevelXPRequirement(std::max(1, (int)gSave.level)) - 1);
	gSave.battlesWon = (uint8_t)std::max(0, std::min(battlesWon, 255));
	gSave.battlesLost = (uint8_t)std::max(0, std::min(battlesLost, 255));
	gSave.discoveredAreaMask = (uint8_t)((1u << (gSave.areaIndex + 1)) - 1u);
	gSelectedArea = gSave.areaIndex;
	gWorldState.slotIndex = gSave.slotIndex;
	gWorldState.areaMapX[gSave.areaIndex] = (uint8_t)std::min(83, 12 + slot * 7);
	gWorldState.areaMapY[gSave.areaIndex] = (uint8_t)std::min(63, 10 + slot * 5);
	gSave.health = (int16_t)DragonGetMaxHealth();
	return DragonSaveCurrentSlot();
}

struct DragonWorldMapFixture {
	int avatarType;
	const char* name;
	int level;
	int health;
	int gold;
	int areaIndex;
	uint16_t pluginId;
	uint16_t mapId;
	int mapX;
	int mapY;
	uint8_t discoveredMask;
	uint16_t areaFlags[DRAGON_ALPHA_AREA_COUNT];
};

static void DragonClearValidationSlots ();
static constexpr uint16_t kProgressionTreasureClaimed = 0x0001;
static constexpr uint16_t kProgressionGateCleared = 0x0002;
static constexpr uint16_t kProgressionWarpOpened = 0x0004;
static constexpr uint16_t kProgressionChallengeOne = 0x0020;
static constexpr uint16_t kProgressionChallengeTwo = 0x0040;

static bool DragonPrepareWorldMapFixture (const DragonWorldMapFixture& fixture) {
	DragonClearValidationSlots();
	if(DragonPrepareCustomValidationSlot(1, fixture.avatarType, fixture.name, fixture.level, fixture.gold, fixture.areaIndex, 0, 0) == false)
		return false;

	gSave.areaIndex = (uint8_t)std::max(0, std::min(fixture.areaIndex, DRAGON_ALPHA_AREA_COUNT - 1));
	gSave.discoveredAreaMask = fixture.discoveredMask != 0
		? fixture.discoveredMask
		: (uint8_t)((1u << (gSave.areaIndex + 1)) - 1u);
	gSelectedArea = gSave.areaIndex;
	gWorldState.slotIndex = gSave.slotIndex;
	gWorldState.version = 2;
	if(fixture.pluginId != 0 && fixture.mapId != 0) {
		const uint32_t packed = ((uint32_t)fixture.pluginId & 0x07FFu) | (((uint32_t)fixture.mapId & 0x07FFu) << 11);
		gWorldState.reserved[0] = (uint8_t)(packed & 0xFFu);
		gWorldState.reserved[1] = (uint8_t)((packed >> 8) & 0xFFu);
		gWorldState.reserved[2] = (uint8_t)((packed >> 16) & 0xFFu);
	}
	else {
		gWorldState.reserved[0] = 0;
		gWorldState.reserved[1] = 0;
		gWorldState.reserved[2] = 0;
	}
	gWorldState.areaMapX[gSave.areaIndex] = (uint8_t)std::max(0, std::min(fixture.mapX, DRAGON_ALPHA_MAP_WIDTH - 1));
	gWorldState.areaMapY[gSave.areaIndex] = (uint8_t)std::max(0, std::min(fixture.mapY, DRAGON_ALPHA_MAP_HEIGHT - 1));
	for(int area = 0; area < DRAGON_ALPHA_AREA_COUNT; area++)
		gWorldState.areaEventFlags[area] = fixture.areaFlags[area];

	const int maxHealth = DragonGetMaxHealth();
	gSave.health = (int16_t)std::max(1, std::min(maxHealth, fixture.health));
	return DragonSaveCurrentSlot() && DragonSaveWorldState();
}

static bool DragonPrepareWorldMapTalkValidationSlot () {
	const DragonWorldMapFixture fixture = {
		DRAGON_AVATAR_WARRIOR, "ARIN", 1, 125, 10, DRAGON_AREA_ELEUSIS, 0, 0, 21, 17, 0x01, {0}
	};
	return DragonPrepareWorldMapFixture(fixture);
}

static bool DragonPrepareWorldMapHealValidationSlot () {
	const DragonWorldMapFixture fixture = {
		DRAGON_AVATAR_WARRIOR, "ARIN", 1, 45, 10, DRAGON_AREA_MOUNTAINS, 3, 128, 40, 30, 0x1F, {0}
	};
	return DragonPrepareWorldMapFixture(fixture);
}

static bool DragonPrepareWorldMapTrainValidationSlot () {
	const DragonWorldMapFixture fixture = {
		DRAGON_AVATAR_WARRIOR, "ARIN", 1, 125, 999, DRAGON_AREA_ELEUSIS, 1, 129, 15, 9, 0x01, {0}
	};
	return DragonPrepareWorldMapFixture(fixture);
}

static bool DragonPrepareWorldMapChallengeValidationSlot () {
	const DragonWorldMapFixture fixture = {
		DRAGON_AVATAR_WARRIOR, "ARIN", 1, 125, 999, DRAGON_AREA_ELEUSIS, 1, 129, 23, 59, 0x01, {0}
	};
	return DragonPrepareWorldMapFixture(fixture);
}

static bool DragonPrepareWorldMapShopEntryValidationSlot () {
	const DragonWorldMapFixture fixture = {
		DRAGON_AVATAR_WARRIOR, "ARIN", 1, 125, 999, DRAGON_AREA_ELEUSIS, 0, 0, 23, 59, 0x01, {0}
	};
	return DragonPrepareWorldMapFixture(fixture);
}

static bool DragonAlignValidationSlotToShopEntry () {
	const int areaIndex = DRAGON_AREA_ELEUSIS;
	gSave.areaIndex = (uint8_t)areaIndex;
	gSave.discoveredAreaMask |= (uint8_t)(1u << areaIndex);
	gSelectedArea = areaIndex;
	gWorldState.slotIndex = gSave.slotIndex;
	gWorldState.areaMapX[areaIndex] = 23;
	gWorldState.areaMapY[areaIndex] = 59;
	return DragonSaveCurrentSlot() && DragonSaveWorldState();
}

static bool DragonPrepareWorldMapWarpValidationSlot () {
	const DragonWorldMapFixture fixture = {
		DRAGON_AVATAR_WARRIOR, "ARIN", 1, 125, 10, DRAGON_AREA_ELEUSIS, 1, 129, 12, 18, 0x01, {0}
	};
	return DragonPrepareWorldMapFixture(fixture);
}

static bool DragonPrepareWorldMapAutoPathTreasureValidationSlot () {
	const DragonWorldMapFixture fixture = {
		DRAGON_AVATAR_WARRIOR, "ARIN", 1, 125, 10, DRAGON_AREA_ELEUSIS, 0, 0, 77, 40, 0x01, {0}
	};
	return DragonPrepareWorldMapFixture(fixture);
}

static bool DragonPrepareWorldMapRouteCancelValidationSlot () {
	const DragonWorldMapFixture fixture = {
		DRAGON_AVATAR_WARRIOR, "ARIN", 1, 125, 10, DRAGON_AREA_ELEUSIS, 0, 0, 77, 40, 0x01, {0}
	};
	return DragonPrepareWorldMapFixture(fixture);
}

static bool DragonPrepareWorldMapActMissValidationSlot () {
	const DragonWorldMapFixture fixture = {
		DRAGON_AVATAR_WARRIOR, "ARIN", 1, 125, 10, DRAGON_AREA_ELEUSIS, 0, 0, 25, 23, 0x01, {0}
	};
	return DragonPrepareWorldMapFixture(fixture);
}

static bool DragonPrepareWorldMapStrongholdShopValidationSlot (int mapX, int mapY, const char* heroName) {
	const DragonWorldMapFixture fixture = {
		DRAGON_AVATAR_WARRIOR,
		(heroName != nullptr and heroName[0] != '\0') ? heroName : "ARIN",
		1,
		125,
		10,
		DRAGON_AREA_ELEUSIS,
		1,
		129,
		mapX,
		mapY,
		0x01,
		{0}
	};
	return DragonPrepareWorldMapFixture(fixture);
}

static bool DragonPrepareWorldMapGateRouteValidationSlot () {
	DragonWorldMapFixture fixture = {
		DRAGON_AVATAR_SORCERER, "LYRA", 7, 117, 245, DRAGON_AREA_CAVES, 2, 128, 66, 19, 0x0F, {0}
	};
	fixture.areaFlags[DRAGON_AREA_ELEUSIS] = kProgressionTreasureClaimed | kProgressionGateCleared | kProgressionWarpOpened;
	fixture.areaFlags[DRAGON_AREA_MEADOWS] = kProgressionGateCleared | kProgressionWarpOpened;
	fixture.areaFlags[DRAGON_AREA_FORESTS] = kProgressionGateCleared | kProgressionWarpOpened;
	return DragonPrepareWorldMapFixture(fixture);
}

static bool DragonPrepareWorldMapPeakGateValidationSlot () {
	DragonWorldMapFixture fixture = {
		DRAGON_AVATAR_WARRIOR, "ARIN", 24, 240, 9999, DRAGON_AREA_PEAK, 3, 133, 41, 48, 0x3F, {0}
	};
	for(int area = 0; area < DRAGON_AREA_PEAK; area++)
		fixture.areaFlags[area] = kProgressionGateCleared | kProgressionWarpOpened;
	return DragonPrepareWorldMapFixture(fixture);
}

static void DragonClearValidationSlots () {
	for(int slot = 1; slot <= DRAGON_ALPHA_SLOT_COUNT; slot++)
		DragonDeleteSlot(slot);
}

static bool DragonPrepareNewGameFilledSlotsFixture () {
	DragonClearValidationSlots();
	if(DragonPrepareCustomValidationSlot(1, DRAGON_AVATAR_WARRIOR, "ARIN", 1, 10, DRAGON_AREA_ELEUSIS, 0, 0) == false)
		return false;
	if(DragonPrepareCustomValidationSlot(2, DRAGON_AVATAR_SORCERER, "LYRA", 7, 245, DRAGON_AREA_CAVES, 9, 2) == false)
		return false;
	if(DragonPrepareCustomValidationSlot(3, DRAGON_AVATAR_RANGER, "THORN", 12, 999, DRAGON_AREA_PEAK, 21, 4) == false)
		return false;
	return DragonLoadSlot(1);
}

static bool DragonPrepareNewGameCorruptRecoveryFixture () {
	DragonClearValidationSlots();
	if(DragonPrepareCustomValidationSlot(2, DRAGON_AVATAR_SORCERER, "LYRA", 7, 245, DRAGON_AREA_CAVES, 9, 2) == false)
		return false;
	if(DragonWriteCorruptedSlotForTesting(1) == false)
		return false;
	return DragonLoadSlot(2);
}

static bool DragonPrepareProgressionValidationSlot () {
	DragonClearValidationSlots();
	if(DragonPrepareCustomValidationSlot(1, DRAGON_AVATAR_SORCERER, "LYRA", 7, 245, DRAGON_AREA_CAVES, 9, 2) == false)
		return false;
	gSave.discoveredAreaMask = 0x3F;
	gSelectedArea = DRAGON_AREA_CAVES;
	gSave.areaIndex = DRAGON_AREA_CAVES;
	gWorldState.slotIndex = gSave.slotIndex;
	gWorldState.version = 2;
	gWorldState.areaMapX[DRAGON_AREA_CAVES] = 30;
	gWorldState.areaMapY[DRAGON_AREA_CAVES] = 18;
	gWorldState.areaEventFlags[DRAGON_AREA_ELEUSIS] = kProgressionTreasureClaimed | kProgressionGateCleared | kProgressionWarpOpened;
	gWorldState.areaEventFlags[DRAGON_AREA_MEADOWS] = kProgressionGateCleared | kProgressionWarpOpened;
	gWorldState.areaEventFlags[DRAGON_AREA_FORESTS] = kProgressionGateCleared | kProgressionWarpOpened;
	gWorldState.areaEventFlags[DRAGON_AREA_CAVES] = kProgressionGateCleared | kProgressionWarpOpened;
	gWorldState.areaEventFlags[DRAGON_AREA_MOUNTAINS] = kProgressionGateCleared | kProgressionWarpOpened;
	gWorldState.areaEventFlags[DRAGON_AREA_PEAK] = kProgressionWarpOpened | kProgressionChallengeOne | kProgressionChallengeTwo;
	gSave.health = (int16_t)DragonGetMaxHealth();
	return DragonSaveCurrentSlot() && DragonSaveWorldState();
}

static bool DragonPrepareProgressionLoadoutValidationSlot () {
	static const uint8_t kInventoryTypes[DRAGON_ALPHA_INVENTORY_CAPACITY] = {
		DRAGON_ITEM_ADEPT_WAND,
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
		DRAGON_ITEM_ADEPT_WAND,
		DRAGON_ITEM_STEEL_SABER,
		DRAGON_ITEM_WAR_HAMMER,
		DRAGON_ITEM_MYSTIC_TOME,
		DRAGON_ITEM_LEATHER_ARMOR,
		DRAGON_ITEM_TRAVELER_CLOAK,
		DRAGON_ITEM_TOWER_SHIELD,
		DRAGON_ITEM_SCOUT_CHARM,
		DRAGON_ITEM_SEER_CHARM,
		DRAGON_ITEM_HEALTH_POTION,
		DRAGON_ITEM_FIRE_SCROLL,
	};

	DragonClearValidationSlots();
	if(DragonPrepareCustomValidationSlot(1, DRAGON_AVATAR_WARRIOR, "ARIN", 18, 777, DRAGON_AREA_CAVES, 12, 3) == false)
		return false;
	gSave.discoveredAreaMask = 0x3F;
	gSelectedArea = DRAGON_AREA_CAVES;
	gSave.areaIndex = DRAGON_AREA_CAVES;
	gWorldState.slotIndex = gSave.slotIndex;
	gWorldState.version = 2;
	gWorldState.areaMapX[DRAGON_AREA_CAVES] = 30;
	gWorldState.areaMapY[DRAGON_AREA_CAVES] = 18;
	gWorldState.areaEventFlags[DRAGON_AREA_ELEUSIS] = kProgressionTreasureClaimed | kProgressionGateCleared | kProgressionWarpOpened;
	gWorldState.areaEventFlags[DRAGON_AREA_MEADOWS] = kProgressionGateCleared | kProgressionWarpOpened;
	gWorldState.areaEventFlags[DRAGON_AREA_FORESTS] = kProgressionGateCleared | kProgressionWarpOpened;
	gWorldState.areaEventFlags[DRAGON_AREA_CAVES] = kProgressionGateCleared | kProgressionWarpOpened;
	gWorldState.areaEventFlags[DRAGON_AREA_MOUNTAINS] = kProgressionGateCleared | kProgressionWarpOpened;
	gWorldState.areaEventFlags[DRAGON_AREA_PEAK] = kProgressionWarpOpened | kProgressionChallengeOne | kProgressionChallengeTwo;
	gSave.inventoryCount = DRAGON_ALPHA_INVENTORY_CAPACITY;
	for(int i = 0; i < DRAGON_ALPHA_INVENTORY_CAPACITY; i++) {
		gSave.inventory[i].type = kInventoryTypes[i];
		gSave.inventory[i].count = 1;
	}
	gSave.health = (int16_t)DragonGetMaxHealth();
	return DragonSaveCurrentSlot() && DragonSaveWorldState();
}

static void DragonValidationOnStartup () {
	if(DragonCaseIs("Validation_NewGame_FilledSlots")
		|| DragonCaseIs("Validation_NewGame_DeleteConfirm")
		|| DragonCaseIs("Validation_Splash_OpenGame_LoadSlot")) {
		if(DragonPrepareNewGameFilledSlotsFixture() == false) {
			DragonValidationFail("Failed to prepare the filled-slot NewGame validation save fixture.");
			return;
		}
		gDragonValidation.entryMode = "boot-fixture";
		return;
	}
	if(DragonCaseIs("Validation_NewGame_CorruptRecovery")) {
		if(DragonPrepareNewGameCorruptRecoveryFixture() == false) {
			DragonValidationFail("Failed to prepare the corrupted-slot NewGame validation save fixture.");
			return;
		}
		gDragonValidation.entryMode = "boot-fixture";
		return;
	}
	if(DragonCaseIs("Validation_SaveLoad_ProgressionState")) {
		if(DragonPrepareProgressionValidationSlot() == false) {
			DragonValidationFail("Failed to prepare the later-game save/load progression validation fixture.");
			return;
		}
		gDragonValidation.entryMode = "startup-fixture";
		ENode::SetAutoRun("WorldMap");
		return;
	}
	if(DragonCaseIs("Validation_SaveLoad_LaterGameLoadoutState")) {
		if(DragonPrepareProgressionLoadoutValidationSlot() == false) {
			DragonValidationFail("Failed to prepare the later-game save/load loadout validation fixture.");
			return;
		}
		gDragonValidation.entryMode = "startup-fixture";
		ENode::SetAutoRun("WorldMap");
		return;
	}
	if(DragonCaseIs("Validation_WorldMap_RandomBattleEntry")) {
		if(DragonPrepareProgressionValidationSlot() == false) {
			DragonValidationFail("Failed to prepare the world-map random-battle validation fixture.");
			return;
		}
		gDragonValidation.entryMode = "startup-fixture";
		ENode::SetRandomSeed(0xDA7A1002u);
		ENode::SetAutoRun("WorldMap");
		return;
	}
	if(DragonCaseIs("Validation_WorldMap_RandomBattleRetreat")) {
		if(DragonPrepareProgressionValidationSlot() == false) {
			DragonValidationFail("Failed to prepare the world-map random-battle retreat validation fixture.");
			return;
		}
		gDragonValidation.entryMode = "startup-fixture";
		ENode::SetRandomSeed(0xDA7A1003u);
		ENode::SetAutoRun("WorldMap");
		return;
	}
		if(DragonCaseIs("Validation_WorldMap_ActMissNoAdjacentInteract")
			|| DragonCaseIs("Validation_WorldMap_ActMissThrottleRepeat")
			|| DragonCaseIs("Validation_WorldMap_RouteCancelImmediateAct")
			|| DragonCaseIs("Validation_WorldMap_RouteCancelDirectionalRetarget")) {
			if(DragonPrepareWorldMapActMissValidationSlot() == false) {
				DragonValidationFail("Failed to prepare the world-map no-adjacent-interact validation save fixture.");
				return;
		}
		gDragonValidation.entryMode = "startup-fixture";
		ENode::SetAutoRun("WorldMap");
		return;
	}
	if(DragonCaseIs("Validation_WorldMap_ActionTalk")) {
		if(DragonPrepareWorldMapTalkValidationSlot() == false) {
			DragonValidationFail("Failed to prepare the world-map talk validation save fixture.");
			return;
		}
		gDragonValidation.entryMode = "startup-fixture";
		ENode::SetAutoRun("WorldMap");
		return;
	}
	if(DragonCaseIs("Validation_WorldMap_HealEvent")) {
		if(DragonPrepareWorldMapHealValidationSlot() == false) {
			DragonValidationFail("Failed to prepare the world-map heal validation save fixture.");
			return;
		}
		gDragonValidation.entryMode = "startup-fixture";
		ENode::SetAutoRun("WorldMap");
		return;
	}
	if(DragonCaseIs("Validation_WorldMap_TrainEvent")
		|| DragonCaseIs("Validation_WorldMap_TrainStatusGrowth")
		|| DragonCaseIs("Validation_WorldMap_TrainToShopSequence")) {
		if(DragonPrepareWorldMapTrainValidationSlot() == false) {
			DragonValidationFail("Failed to prepare the world-map train validation save fixture.");
			return;
		}
		gDragonValidation.entryMode = "startup-fixture";
		ENode::SetAutoRun("WorldMap");
		return;
	}
	if(DragonCaseIs("Validation_WorldMap_TrainDecline")) {
		if(DragonPrepareWorldMapTrainValidationSlot() == false) {
			DragonValidationFail("Failed to prepare the world-map train-decline validation save fixture.");
			return;
		}
		gDragonValidation.entryMode = "startup-fixture";
		ENode::SetAutoRun("WorldMap");
		return;
	}
	if(DragonCaseIs("Validation_WorldMap_ChallengeEvent")
		|| DragonCaseIs("Validation_WorldMap_ChallengeDecline")
		|| DragonCaseIs("Validation_WorldMap_ChallengeDefeatPanel")
		|| DragonCaseIs("Validation_WorldMap_ChallengeVictoryProgression")) {
		if(DragonPrepareWorldMapChallengeValidationSlot() == false) {
			DragonValidationFail("Failed to prepare the world-map challenge validation save fixture.");
			return;
		}
		gDragonValidation.entryMode = "startup-fixture";
		if(DragonCaseIs("Validation_WorldMap_ChallengeVictoryProgression"))
			ENode::SetRandomSeed(0xDA7A1008u);
		ENode::SetAutoRun("WorldMap");
		return;
	}
	if(DragonCaseIs("Validation_WorldMap_ShopEntry")) {
		if(DragonPrepareWorldMapShopEntryValidationSlot() == false) {
			DragonValidationFail("Failed to prepare the world-map shop-entry validation save fixture.");
			return;
		}
		gDragonValidation.entryMode = "startup-fixture";
		ENode::SetAutoRun("WorldMap");
		return;
	}
	if(DragonCaseIs("Validation_WorldMap_WarpTransition")) {
		if(DragonPrepareWorldMapWarpValidationSlot() == false) {
			DragonValidationFail("Failed to prepare the world-map warp validation save fixture.");
			return;
		}
		gDragonValidation.entryMode = "startup-fixture";
		ENode::SetAutoRun("WorldMap");
		return;
	}
	if(DragonCaseIs("Validation_WorldMap_AutoPathTreasure")) {
		if(DragonPrepareWorldMapAutoPathTreasureValidationSlot() == false) {
			DragonValidationFail("Failed to prepare the world-map auto-path treasure validation save fixture.");
			return;
		}
		gDragonValidation.entryMode = "startup-fixture";
		ENode::SetAutoRun("WorldMap");
		return;
	}
	if(DragonCaseIs("Validation_WorldMap_CurrentTileRouteCancel")) {
		if(DragonPrepareWorldMapRouteCancelValidationSlot() == false) {
			DragonValidationFail("Failed to prepare the world-map route-cancel validation save fixture.");
			return;
		}
		gDragonValidation.entryMode = "startup-fixture";
		ENode::SetAutoRun("WorldMap");
		return;
	}
	if(DragonCaseIs("Validation_WorldMap_GateRouteMagnet")) {
		if(DragonPrepareWorldMapGateRouteValidationSlot() == false) {
			DragonValidationFail("Failed to prepare the world-map gate-route validation save fixture.");
			return;
		}
		gDragonValidation.entryMode = "startup-fixture";
		ENode::SetAutoRun("WorldMap");
		return;
	}
	if(DragonCaseIs("Validation_WorldMap_GateBattleEntry")
		|| DragonCaseIs("Validation_WorldMap_GateBattleProgression")) {
		if(DragonPrepareWorldMapPeakGateValidationSlot() == false) {
			DragonValidationFail("Failed to prepare the world-map gate-battle validation save fixture.");
			return;
		}
		gDragonValidation.entryMode = "startup-fixture";
		if(DragonCaseIs("Validation_WorldMap_GateBattleProgression"))
			ENode::SetRandomSeed(0xDA7A1009u);
		ENode::SetAutoRun("WorldMap");
		return;
	}
	if(DragonCaseIs("Validation_Shop_Default")
		|| DragonCaseIs("Validation_Shop_ReturnToWorldMap")
		|| DragonCaseIs("Validation_Shop_Purchase")
		|| DragonCaseIs("Validation_Shop_InsufficientGold")
		|| DragonCaseIs("Validation_Shop_DuplicateOwned")
		|| DragonCaseIs("Validation_Shop_InventoryFull")
		|| DragonCaseIs("Validation_Shop_MultiPurchase")) {
		if(DragonCaseIs("Validation_Shop_DuplicateOwned")) {
			if(DragonPrepareShopDuplicateOwnedValidationSlot() == false) {
				DragonValidationFail("Failed to prepare the duplicate-owned merchant validation save fixture.");
				return;
			}
		}
		else if(DragonCaseIs("Validation_Shop_InventoryFull")) {
			if(DragonPrepareShopInventoryFullValidationSlot() == false) {
				DragonValidationFail("Failed to prepare the inventory-full merchant validation save fixture.");
				return;
			}
		}
		else if(DragonPrepareValidationSlot(DRAGON_AVATAR_WARRIOR, "ARIN", 999) == false) {
			DragonValidationFail("Failed to prepare the merchant validation save fixture.");
			return;
		}
		if(DragonCaseIs("Validation_Shop_InsufficientGold")) {
			gSave.gold = 40;
			if(DragonSaveCurrentSlot() == false) {
				DragonValidationFail("Failed to persist the low-gold merchant validation save fixture.");
				return;
			}
		}
		if(DragonAlignValidationSlotToShopEntry() == false) {
			DragonValidationFail("Failed to align the merchant validation slot to the shop-entry fixture.");
			return;
		}
		gDragonValidation.entryMode = "startup-fixture";
		ENode::SetAutoRun("WorldMap");
		return;
	}
	if(DragonCaseIs("Validation_Battle_TargetSelection")
		|| DragonCaseIs("Validation_Battle_CommandResolution")
		|| DragonCaseIs("Validation_Battle_SeededOutcome")
		|| DragonCaseIs("Validation_Battle_MultiEnemyContinue")
		|| DragonCaseIs("Validation_Battle_EnemySpecialResponse")
		|| DragonCaseIs("Validation_Battle_EnemyHealResponse")
		|| DragonCaseIs("Validation_Battle_NoDamageResponse")
		|| DragonCaseIs("Validation_Battle_VictoryPanel")
		|| DragonCaseIs("Validation_Battle_Magic_SeededOutcome")
		|| DragonCaseIs("Validation_Battle_Tech_SeededOutcome")
		|| DragonCaseIs("Validation_Battle_ElementalNeutralOutcome")
		|| DragonCaseIs("Validation_Battle_ElementalResistOutcome")) {
		const bool prepared = DragonCaseIs("Validation_Battle_Magic_SeededOutcome")
			? DragonPrepareBattleInjuredValidationSlot(50)
			: (DragonCaseIs("Validation_Battle_Tech_SeededOutcome")
				? DragonPrepareBattleInjuredValidationSlot(80)
				: ((DragonCaseIs("Validation_Battle_ElementalNeutralOutcome")
					|| DragonCaseIs("Validation_Battle_ElementalResistOutcome"))
					? DragonPrepareValidationSlot(DRAGON_AVATAR_RANGER, "THORN", 150)
					: DragonPrepareValidationSlot(DRAGON_AVATAR_WARRIOR, "ARIN", 150)));
		if(prepared == false) {
			DragonValidationFail("Failed to prepare the battle validation save fixture.");
			return;
		}
		if(DragonCaseIs("Validation_Battle_SeededOutcome"))
			ENode::SetRandomSeed(0xDA7A1001u);
		else if(DragonCaseIs("Validation_Battle_MultiEnemyContinue"))
			ENode::SetRandomSeed(0xDA7A100Au);
		else if(DragonCaseIs("Validation_Battle_EnemySpecialResponse"))
			ENode::SetRandomSeed(0xDA7A100Bu);
		else if(DragonCaseIs("Validation_Battle_EnemyHealResponse"))
			ENode::SetRandomSeed(0xDA7A100Cu);
		else if(DragonCaseIs("Validation_Battle_NoDamageResponse"))
			ENode::SetRandomSeed(0xDA7A100Du);
		else if(DragonCaseIs("Validation_Battle_VictoryPanel"))
			ENode::SetRandomSeed(0xDA7A1007u);
		else if(DragonCaseIs("Validation_Battle_Magic_SeededOutcome"))
			ENode::SetRandomSeed(0xDA7A1004u);
		else if(DragonCaseIs("Validation_Battle_Tech_SeededOutcome"))
			ENode::SetRandomSeed(0xDA7A1005u);
		else if(DragonCaseIs("Validation_Battle_ElementalNeutralOutcome"))
			ENode::SetRandomSeed(0xDA7A100Eu);
		else if(DragonCaseIs("Validation_Battle_ElementalResistOutcome"))
			ENode::SetRandomSeed(0xDA7A100Fu);
		if(DragonCaseIs("Validation_Battle_ElementalNeutralOutcome"))
			DragonQueueLegacyGroupBattle(DRAGON_AREA_MOUNTAINS, DRAGON_BATTLE_RANDOM, 3, 128, 130, false, false, 0, -1, 0, 0, NULL);
		else if(DragonCaseIs("Validation_Battle_ElementalResistOutcome"))
			DragonQueueLegacyGroupBattle(DRAGON_AREA_MOUNTAINS, DRAGON_BATTLE_RANDOM, 3, 128, 144, false, false, 0, -1, 0, 0, NULL);
		else
			DragonQueueLegacyGroupBattle(DRAGON_AREA_CAVES, DRAGON_BATTLE_RANDOM, 2, 128, 129, false, false, 0, -1, 0, 0, NULL);
		gDragonValidation.entryMode = "startup-fixture";
		ENode::SetAutoRun("Battle");
		return;
	}
	if(DragonCaseIs("Validation_Status_UsePotion")) {
		if(DragonPrepareStatusValidationSlot(60, 10) == false) {
			DragonValidationFail("Failed to prepare the status potion validation save fixture.");
			return;
		}
		gDragonValidation.entryMode = "startup-fixture";
		ENode::SetAutoRun("Status");
		return;
	}
	if(DragonCaseIs("Validation_Status_SellScroll")) {
		if(DragonPrepareStatusValidationSlot(125, 10) == false) {
			DragonValidationFail("Failed to prepare the status sell validation save fixture.");
			return;
		}
		gDragonValidation.entryMode = "startup-fixture";
		ENode::SetAutoRun("Status");
		return;
	}
	if(DragonCaseIs("Validation_Status_UnequipWeapon")) {
		if(DragonPrepareStatusValidationSlot(125, 10) == false) {
			DragonValidationFail("Failed to prepare the status unequip validation save fixture.");
			return;
		}
		gDragonValidation.entryMode = "startup-fixture";
		ENode::SetAutoRun("Status");
		return;
	}
	if(DragonCaseIs("Validation_Status_UnequipArmor")) {
		if(DragonPrepareStatusValidationSlot(125, 10) == false) {
			DragonValidationFail("Failed to prepare the status armor-unequip validation save fixture.");
			return;
		}
		gDragonValidation.entryMode = "startup-fixture";
		ENode::SetAutoRun("Status");
		return;
	}
	if(DragonCaseIs("Validation_Status_UnequipRelic")) {
		if(DragonPrepareStatusRelicValidationSlot() == false) {
			DragonValidationFail("Failed to prepare the status relic-unequip validation save fixture.");
			return;
		}
		gDragonValidation.entryMode = "startup-fixture";
		ENode::SetAutoRun("Status");
		return;
	}
	if(DragonCaseIs("Validation_Status_EquipWeapon")) {
		if(DragonPrepareStatusEquipValidationSlot() == false) {
			DragonValidationFail("Failed to prepare the status equip validation save fixture.");
			return;
		}
		gDragonValidation.entryMode = "startup-fixture";
		ENode::SetAutoRun("Status");
		return;
	}
	if(DragonCaseIs("Validation_Status_EquipArmor")) {
		if(DragonPrepareStatusEquipArmorValidationSlot() == false) {
			DragonValidationFail("Failed to prepare the status armor-equip validation save fixture.");
			return;
		}
		gDragonValidation.entryMode = "startup-fixture";
		ENode::SetAutoRun("Status");
		return;
	}
	if(DragonCaseIs("Validation_Status_EquipRelic")) {
		if(DragonPrepareStatusEquipRelicValidationSlot() == false) {
			DragonValidationFail("Failed to prepare the status relic-equip validation save fixture.");
			return;
		}
		gDragonValidation.entryMode = "startup-fixture";
		ENode::SetAutoRun("Status");
		return;
	}
	if(DragonCaseIs("Validation_Status_SellEquippedWeapon")) {
		if(DragonPrepareStatusSellEquippedWeaponValidationSlot() == false) {
			DragonValidationFail("Failed to prepare the status spare-equipped-weapon sell validation save fixture.");
			return;
		}
		gDragonValidation.entryMode = "startup-fixture";
		ENode::SetAutoRun("Status");
		return;
	}
	if(DragonCaseIs("Validation_Status_UnequipWeaponInventoryFull")) {
		if(DragonPrepareStatusFullInventoryValidationSlot() == false) {
			DragonValidationFail("Failed to prepare the status inventory-full validation save fixture.");
			return;
		}
		gDragonValidation.entryMode = "startup-fixture";
		ENode::SetAutoRun("Status");
		return;
	}
	if(DragonCaseIs("Validation_Status_LargeInventoryPaging")) {
		if(DragonPrepareStatusLargeInventoryValidationSlot() == false) {
			DragonValidationFail("Failed to prepare the status large-inventory validation save fixture.");
			return;
		}
		gDragonValidation.entryMode = "startup-fixture";
		ENode::SetAutoRun("Status");
		return;
	}
}

static int DragonGetCaseStepCount () {
	if(DragonCaseIs("Validation_NewGame_FilledSlots"))
		return 1;
	if(DragonCaseIs("Validation_NewGame_DeleteConfirm"))
		return 3;
	if(DragonCaseIs("Validation_NewGame_CorruptRecovery"))
		return 1;
	if(DragonCaseIs("Validation_Splash_OpenGame_LoadSlot"))
		return 3;
	if(DragonCaseIs("Validation_NewAvatar_Default"))
		return 3;
	if(DragonCaseIs("Validation_NewAvatar_MageStatus"))
		return 5;
	if(DragonCaseIs("Validation_NewAvatar_ThiefStatus"))
		return 5;
	if(DragonCaseIs("Validation_WorldMap_Default"))
		return 3;
	if(DragonCaseIs("Validation_WorldMap_MagicButton"))
		return 4;
	if(DragonCaseIs("Validation_WorldMap_TechButton"))
		return 4;
	if(DragonCaseIs("Validation_WorldMap_FrontDoorTalkRoute"))
		return 6;
	if(DragonCaseIs("Validation_WorldMap_FrontDoorShopNpcTalk"))
		return 10;
	if(DragonCaseIs("Validation_WorldMap_ActMissNoAdjacentInteract"))
		return 2;
	if(DragonCaseIs("Validation_WorldMap_ActMissThrottleRepeat"))
		return 3;
	if(DragonCaseIs("Validation_WorldMap_FrontDoorShopNpcActSouth"))
		return 9;
	if(DragonCaseIs("Validation_WorldMap_FrontDoorShopNpcActNorth"))
		return 9;
	if(DragonCaseIs("Validation_WorldMap_FrontDoorShopNpcActEast"))
		return 9;
	if(DragonCaseIs("Validation_WorldMap_FrontDoorShopNpcActWest"))
		return 9;
	if(DragonCaseIs("Validation_WorldMap_FrontDoorFacingPriority"))
		return 8;
	if(DragonCaseIs("Validation_WorldMap_FrontDoorFacingPriorityRetarget"))
		return 9;
	if(DragonCaseIs("Validation_WorldMap_FrontDoorRouteCancel"))
		return 5;
	if(DragonCaseIs("Validation_WorldMap_RouteCancelImmediateAct"))
		return 4;
	if(DragonCaseIs("Validation_WorldMap_RouteCancelDirectionalRetarget"))
		return 7;
	if(DragonCaseIs("Validation_WorldMap_RouteTargetRetapThrottle"))
		return 9;
	if(DragonCaseIs("Validation_WorldMap_ActionTalk"))
		return 3;
	if(DragonCaseIs("Validation_WorldMap_HealEvent"))
		return 2;
	if(DragonCaseIs("Validation_WorldMap_TrainEvent"))
		return 3;
	if(DragonCaseIs("Validation_WorldMap_TrainStatusGrowth"))
		return 4;
	if(DragonCaseIs("Validation_WorldMap_TrainDecline"))
		return 3;
	if(DragonCaseIs("Validation_WorldMap_TrainToShopSequence"))
		return 6;
	if(DragonCaseIs("Validation_WorldMap_ChallengeEvent"))
		return 4;
	if(DragonCaseIs("Validation_WorldMap_ChallengeDecline"))
		return 3;
	if(DragonCaseIs("Validation_WorldMap_ChallengeDefeatPanel"))
		return 4;
	if(DragonCaseIs("Validation_WorldMap_ChallengeVictoryProgression"))
		return 5;
	if(DragonCaseIs("Validation_WorldMap_ShopEntry"))
		return 3;
	if(DragonCaseIs("Validation_WorldMap_WarpTransition"))
		return 2;
	if(DragonCaseIs("Validation_WorldMap_AutoPathTreasure"))
		return 2;
	if(DragonCaseIs("Validation_WorldMap_CurrentTileRouteCancel"))
		return 3;
	if(DragonCaseIs("Validation_WorldMap_GateRouteMagnet"))
		return 2;
	if(DragonCaseIs("Validation_WorldMap_GateBattleEntry"))
		return 4;
	if(DragonCaseIs("Validation_WorldMap_GateBattleProgression"))
		return 5;
	if(DragonCaseIs("Validation_WorldMap_RandomBattleEntry"))
		return 2;
	if(DragonCaseIs("Validation_WorldMap_RandomBattleRetreat"))
		return 4;
	if(DragonCaseIs("Validation_Status_Default"))
		return 4;
	if(DragonCaseIs("Validation_Status_UsePotion"))
		return 2;
	if(DragonCaseIs("Validation_Status_SellScroll"))
		return 3;
	if(DragonCaseIs("Validation_Status_UnequipWeapon"))
		return 2;
	if(DragonCaseIs("Validation_Status_UnequipArmor"))
		return 2;
	if(DragonCaseIs("Validation_Status_UnequipRelic"))
		return 2;
	if(DragonCaseIs("Validation_Status_EquipWeapon"))
		return 2;
	if(DragonCaseIs("Validation_Status_EquipArmor"))
		return 2;
	if(DragonCaseIs("Validation_Status_EquipRelic"))
		return 2;
	if(DragonCaseIs("Validation_Status_SellEquippedWeapon"))
		return 2;
	if(DragonCaseIs("Validation_Status_UnequipWeaponInventoryFull"))
		return 2;
	if(DragonCaseIs("Validation_Status_LargeInventoryPaging"))
		return 12;
	if(DragonCaseIs("Validation_SaveLoad_RoundTrip"))
		return 7;
	if(DragonCaseIs("Validation_SaveLoad_ProgressionState"))
		return 4;
	if(DragonCaseIs("Validation_SaveLoad_LaterGameLoadoutState"))
		return 5;
	if(DragonCaseIs("Validation_Shop_Default"))
		return 2;
	if(DragonCaseIs("Validation_Shop_ReturnToWorldMap"))
		return 3;
	if(DragonCaseIs("Validation_Shop_Purchase"))
		return 3;
	if(DragonCaseIs("Validation_Shop_InsufficientGold"))
		return 3;
	if(DragonCaseIs("Validation_Shop_DuplicateOwned"))
		return 3;
	if(DragonCaseIs("Validation_Shop_InventoryFull"))
		return 3;
	if(DragonCaseIs("Validation_Shop_MultiPurchase"))
		return 3;
	if(DragonCaseIs("Validation_Battle_TargetSelection"))
		return 3;
	if(DragonCaseIs("Validation_Battle_CommandResolution"))
		return 3;
	if(DragonCaseIs("Validation_Battle_SeededOutcome"))
		return 3;
	if(DragonCaseIs("Validation_Battle_MultiEnemyContinue"))
		return 3;
	if(DragonCaseIs("Validation_Battle_EnemySpecialResponse"))
		return 2;
	if(DragonCaseIs("Validation_Battle_EnemyHealResponse"))
		return 2;
	if(DragonCaseIs("Validation_Battle_NoDamageResponse"))
		return 2;
	if(DragonCaseIs("Validation_Battle_VictoryPanel"))
		return 2;
	if(DragonCaseIs("Validation_Battle_Magic_SeededOutcome"))
		return 3;
	if(DragonCaseIs("Validation_Battle_Tech_SeededOutcome"))
		return 3;
	if(DragonCaseIs("Validation_Battle_ElementalNeutralOutcome"))
		return 3;
	if(DragonCaseIs("Validation_Battle_ElementalResistOutcome"))
		return 3;
	return 1;
}

static bool DragonValidateCurrentCaseTelemetry () {
	if(DragonCaseIs("Validation_Splash_Default") && gDragonValidation.stepIndex == 0) {
		gDragonValidation.sceneName = "Splash";
		return DragonValidateSplashTelemetry();
	}
	if(DragonCaseIs("Validation_NewGame_Default") && gDragonValidation.stepIndex == 0) {
		gDragonValidation.sceneName = "NewGame";
		return DragonValidateNewGameTelemetry();
	}
	if(DragonCaseIs("Validation_NewGame_FilledSlots") && gDragonValidation.stepIndex == 0) {
		gDragonValidation.sceneName = "NewGame";
		return DragonValidateNewGameMultiSlotTelemetry();
	}
	if(DragonCaseIs("Validation_NewGame_DeleteConfirm")) {
		gDragonValidation.sceneName = "NewGame";
		if(gDragonValidation.stepIndex == 0)
			return DragonValidateNewGameMultiSlotTelemetry();
		if(gDragonValidation.stepIndex == 1)
			return DragonValidateNewGameDeleteConfirmTelemetry();
		if(gDragonValidation.stepIndex == 2)
			return DragonValidateNewGameDeletedSlotTelemetry();
	}
	if(DragonCaseIs("Validation_NewGame_CorruptRecovery") && gDragonValidation.stepIndex == 0) {
		gDragonValidation.sceneName = "NewGame";
		return DragonValidateNewGameCorruptRecoveryTelemetry();
	}
	if(DragonCaseIs("Validation_Splash_OpenGame_LoadSlot")) {
		if(gDragonValidation.stepIndex == 0) {
			gDragonValidation.sceneName = "NewGame";
			return DragonValidateNewGameFilledSlotTelemetry();
		}
		if(gDragonValidation.stepIndex == 1) {
			gDragonValidation.sceneName = "WorldMap";
			return DragonValidateWorldMapTelemetry();
		}
		if(gDragonValidation.stepIndex == 2) {
			gDragonValidation.sceneName = "WorldMap";
			return DragonValidateWorldMapTelemetry();
		}
	}
	if(DragonCaseIs("Validation_NewAvatar_Default")) {
		if(gDragonValidation.stepIndex == 0) {
			gDragonValidation.sceneName = "NewGame";
			return DragonValidateNewGameTelemetry();
		}
		if(gDragonValidation.stepIndex == 1) {
			gDragonValidation.sceneName = "NewAvatar";
			return DragonValidateNewAvatarTelemetry("Soldier");
		}
		if(gDragonValidation.stepIndex == 2) {
			gDragonValidation.sceneName = "NewAvatar";
			return DragonValidateNewAvatarTelemetry("Mage");
		}
	}
	if(DragonCaseIs("Validation_NewAvatar_MageStatus")) {
		if(gDragonValidation.stepIndex == 0) {
			gDragonValidation.sceneName = "NewGame";
			return DragonValidateNewGameTelemetry();
		}
		if(gDragonValidation.stepIndex == 1) {
			gDragonValidation.sceneName = "NewAvatar";
			return DragonValidateNewAvatarTelemetry("Soldier");
		}
		if(gDragonValidation.stepIndex == 2) {
			gDragonValidation.sceneName = "NewAvatar";
			return DragonValidateNewAvatarTelemetry("Mage");
		}
		if(gDragonValidation.stepIndex == 3) {
			gDragonValidation.sceneName = "WorldMap";
			return DragonValidateWorldMapTelemetry();
		}
		if(gDragonValidation.stepIndex == 4) {
			gDragonValidation.sceneName = "Status";
			return DragonValidateStatusStarterClassTelemetry(
				"HP 99/99  Gold 10  Area Eleusis",
				"STR 8  DEF 8  SPD 9  MAG 26",
				"Weapon: Adept Wand",
				"Armor: Traveler Cloak",
				"Relic: Seer Charm"
			);
		}
	}
	if(DragonCaseIs("Validation_NewAvatar_ThiefStatus")) {
		if(gDragonValidation.stepIndex == 0) {
			gDragonValidation.sceneName = "NewGame";
			return DragonValidateNewGameTelemetry();
		}
		if(gDragonValidation.stepIndex == 1) {
			gDragonValidation.sceneName = "NewAvatar";
			return DragonValidateNewAvatarTelemetry("Soldier");
		}
		if(gDragonValidation.stepIndex == 2) {
			gDragonValidation.sceneName = "NewAvatar";
			return DragonValidateNewAvatarTelemetry("Thief");
		}
		if(gDragonValidation.stepIndex == 3) {
			gDragonValidation.sceneName = "WorldMap";
			return DragonValidateWorldMapTelemetry();
		}
		if(gDragonValidation.stepIndex == 4) {
			gDragonValidation.sceneName = "Status";
			return DragonValidateStatusStarterClassTelemetry(
				"HP 111/111  Gold 10  Area Eleusis",
				"STR 14  DEF 12  SPD 17  MAG 6",
				"Weapon: Hunter Bow",
				"Armor: Leather Armor",
				"Relic: Scout Charm"
			);
		}
	}
		if(DragonCaseIs("Validation_WorldMap_Default")) {
			if(gDragonValidation.stepIndex == 0) {
				gDragonValidation.sceneName = "NewGame";
				return DragonValidateNewGameTelemetry();
		}
		if(gDragonValidation.stepIndex == 1) {
			gDragonValidation.sceneName = "NewAvatar";
			return DragonValidateNewAvatarTelemetry("Soldier");
		}
		if(gDragonValidation.stepIndex == 2) {
			gDragonValidation.sceneName = "WorldMap";
			return DragonValidateWorldMapTelemetry();
			}
		}
		if(DragonCaseIs("Validation_WorldMap_MagicButton")) {
			if(gDragonValidation.stepIndex == 0) {
				gDragonValidation.sceneName = "NewGame";
				return DragonValidateNewGameTelemetry();
			}
			if(gDragonValidation.stepIndex == 1) {
				gDragonValidation.sceneName = "NewAvatar";
				return DragonValidateNewAvatarTelemetry("Soldier");
			}
			if(gDragonValidation.stepIndex == 2) {
				gDragonValidation.sceneName = "WorldMap";
				return DragonValidateWorldMapTelemetry();
			}
			if(gDragonValidation.stepIndex == 3) {
				gDragonValidation.sceneName = "Status";
				return DragonValidateStatusFieldCommandTelemetry("MAGIC / COMMANDS", "Cure", "Guard Heal", "CAST");
			}
		}
		if(DragonCaseIs("Validation_WorldMap_TechButton")) {
			if(gDragonValidation.stepIndex == 0) {
				gDragonValidation.sceneName = "NewGame";
				return DragonValidateNewGameTelemetry();
			}
			if(gDragonValidation.stepIndex == 1) {
				gDragonValidation.sceneName = "NewAvatar";
				return DragonValidateNewAvatarTelemetry("Soldier");
			}
			if(gDragonValidation.stepIndex == 2) {
				gDragonValidation.sceneName = "WorldMap";
				return DragonValidateWorldMapTelemetry();
			}
			if(gDragonValidation.stepIndex == 3) {
				gDragonValidation.sceneName = "Status";
				return DragonValidateStatusFieldCommandTelemetry("TECH / COMMANDS", "Rally", "Power Strike", "USE");
			}
		}
		if(DragonCaseIs("Validation_WorldMap_FrontDoorTalkRoute")) {
			if(gDragonValidation.stepIndex == 0) {
				gDragonValidation.sceneName = "NewGame";
				return DragonValidateNewGameTelemetry();
			}
			if(gDragonValidation.stepIndex == 1) {
				gDragonValidation.sceneName = "NewAvatar";
				return DragonValidateNewAvatarTelemetry("Soldier");
			}
			gDragonValidation.sceneName = "WorldMap";
			if(gDragonValidation.stepIndex == 2)
				return DragonValidateWorldMapStateTelemetry("Position: 25,23", "Lv 1", "HP 125/125", "Gold 10")
					&& DragonFindReportedTextContaining("Map: Eleusis");
			if(gDragonValidation.stepIndex == 3)
				return DragonValidateWorldMapNearPositionTelemetry(22, 18, 1, "Lv 1", "HP 125/125", "Gold 10")
					&& DragonFindReportedTextContaining("Map: Eleusis")
					&& DragonFindReportedTextContaining("Event 0: Talk 22,17");
			if(gDragonValidation.stepIndex == 4)
				return DragonValidateWorldMapTalkTelemetry()
					|| (DragonValidateWorldMapNearPositionTelemetry(22, 18, 1, "Lv 1", "HP 125/125", "Gold 10")
						&& DragonFindReportedTextContaining("Map: Eleusis")
						&& DragonFindReportedTextContaining("Event 0: Talk 22,17"));
			if(gDragonValidation.stepIndex == 5)
				return DragonValidateWorldMapNearPositionTelemetry(22, 18, 1, "Lv 1", "HP 125/125", "Gold 10")
					&& DragonFindReportedTextContaining("Map: Eleusis")
					&& DragonFindReportedTextContaining("Event 0: Talk 22,17")
					&& DragonFindReportedTextContaining("CONTINUE") == false;
		}
			if(DragonCaseIs("Validation_WorldMap_FrontDoorShopNpcTalk")) {
			if(gDragonValidation.stepIndex == 0) {
				gDragonValidation.sceneName = "NewGame";
				return DragonValidateNewGameTelemetry();
			}
			if(gDragonValidation.stepIndex == 1) {
				gDragonValidation.sceneName = "NewAvatar";
				return DragonValidateNewAvatarTelemetry("Soldier");
			}
			gDragonValidation.sceneName = "WorldMap";
			if(gDragonValidation.stepIndex == 2)
				return DragonValidateWorldMapStateTelemetry("Position: 25,23", "Lv 1", "HP 125/125", "Gold 10")
					&& DragonFindReportedTextContaining("Map: Eleusis");
			if(gDragonValidation.stepIndex == 3)
				return DragonValidateWorldMapNearPositionTelemetry(12, 19, 2, "Lv 1", "HP 125/125", "Gold 10")
					&& DragonFindReportedTextContaining("Map: Eleusis Stronghold");
			if(gDragonValidation.stepIndex == 4)
				return DragonValidateWorldMapNearPositionTelemetry(21, 11, 2, "Lv 1", "HP 125/125", "Gold 10")
					&& DragonFindReportedTextContaining("Map: Eleusis Stronghold");
			if(gDragonValidation.stepIndex == 5)
				return DragonValidateWorldMapNearPositionTelemetry(81, 16, 2, "Lv 1", "HP 125/125", "Gold 10")
					&& DragonFindReportedTextContaining("Map: Eleusis Stronghold");
			if(gDragonValidation.stepIndex == 6)
				return DragonValidateWorldMapNearPositionTelemetry(74, 12, 2, "Lv 1", "HP 125/125", "Gold 10")
					&& DragonFindReportedTextContaining("Map: Eleusis Stronghold");
				if(gDragonValidation.stepIndex == 7)
					return DragonValidateWorldMapNearPositionTelemetry(74, 12, 2, "Lv 1", "HP 125/125", "Gold 10")
						&& DragonFindReportedTextContaining("Map: Eleusis Stronghold");
				if(gDragonValidation.stepIndex == 8)
					return DragonValidateWorldMapTalkTelemetry("rare items")
						&& DragonFindReportedTextContaining("MERCHANT") == false;
				if(gDragonValidation.stepIndex == 9)
					return DragonValidateShopTelemetry(0)
						&& DragonFindReportedTextContaining("Merchant open.")
						&& DragonFindReportedTextContaining("Talk:") == false;
			}
				if(DragonCaseIs("Validation_WorldMap_ActMissNoAdjacentInteract")) {
					gDragonValidation.sceneName = "WorldMap";
					if(gDragonValidation.stepIndex == 0)
						return DragonValidateWorldMapNearPositionTelemetry(25, 23, 2, "Lv 1", "HP 125/125", "Gold 10")
							&& DragonFindReportedTextContaining("Map: Eleusis");
					if(gDragonValidation.stepIndex == 1)
						return DragonValidateWorldMapNoRouteTelemetry("There is nothing here.")
							&& DragonFindReportedTextContaining("Map: Eleusis")
							&& DragonFindReportedTextContaining("Talk:") == false
							&& DragonFindReportedTextContaining("MERCHANT") == false;
				}
				if(DragonCaseIs("Validation_WorldMap_ActMissThrottleRepeat")) {
					gDragonValidation.sceneName = "WorldMap";
					if(gDragonValidation.stepIndex == 0)
						return DragonValidateWorldMapNearPositionTelemetry(25, 23, 2, "Lv 1", "HP 125/125", "Gold 10")
							&& DragonFindReportedTextContaining("Map: Eleusis");
					if(gDragonValidation.stepIndex == 1)
						return DragonValidateWorldMapNoRouteTelemetry("There is nothing here.")
							&& DragonFindReportedTextContaining("Map: Eleusis")
							&& DragonFindReportedTextContaining("Talk:") == false
							&& DragonFindReportedTextContaining("MERCHANT") == false;
					if(gDragonValidation.stepIndex == 2)
						return DragonValidateWorldMapNoRouteTelemetry("There is nothing here.")
							&& DragonFindReportedTextContaining("Map: Eleusis")
							&& DragonFindReportedTextContaining("Talk:") == false
							&& DragonFindReportedTextContaining("MERCHANT") == false;
				}
			if(DragonCaseIs("Validation_WorldMap_FrontDoorShopNpcActSouth")
				|| DragonCaseIs("Validation_WorldMap_FrontDoorShopNpcActNorth")
				|| DragonCaseIs("Validation_WorldMap_FrontDoorShopNpcActEast")
				|| DragonCaseIs("Validation_WorldMap_FrontDoorShopNpcActWest")) {
				if(gDragonValidation.stepIndex == 0) {
					gDragonValidation.sceneName = "NewGame";
					return DragonValidateNewGameTelemetry();
				}
				if(gDragonValidation.stepIndex == 1) {
					gDragonValidation.sceneName = "NewAvatar";
					return DragonValidateNewAvatarTelemetry("Soldier");
				}
				gDragonValidation.sceneName = "WorldMap";
				if(gDragonValidation.stepIndex == 2)
					return DragonValidateWorldMapStateTelemetry("Position: 25,23", "Lv 1", "HP 125/125", "Gold 10")
						&& DragonFindReportedTextContaining("Map: Eleusis");
				if(gDragonValidation.stepIndex == 3)
					return DragonValidateWorldMapNearPositionTelemetry(12, 19, 2, "Lv 1", "HP 125/125", "Gold 10")
						&& DragonFindReportedTextContaining("Map: Eleusis Stronghold");
				if(gDragonValidation.stepIndex == 4)
					return DragonValidateWorldMapNearPositionTelemetry(21, 11, 2, "Lv 1", "HP 125/125", "Gold 10")
						&& DragonFindReportedTextContaining("Map: Eleusis Stronghold");
				if(gDragonValidation.stepIndex == 5)
					return DragonValidateWorldMapNearPositionTelemetry(81, 16, 2, "Lv 1", "HP 125/125", "Gold 10")
						&& DragonFindReportedTextContaining("Map: Eleusis Stronghold");
				if(gDragonValidation.stepIndex == 6)
					return DragonValidateWorldMapNearPositionTelemetry(74, 12, 2, "Lv 1", "HP 125/125", "Gold 10")
						&& DragonFindReportedTextContaining("Map: Eleusis Stronghold");

				if(gDragonValidation.stepIndex == 7)
					return DragonValidateWorldMapTalkTelemetry("rare items")
						&& DragonFindReportedTextContaining("MERCHANT") == false;
				if(gDragonValidation.stepIndex == 8)
					return DragonValidateShopTelemetry(0)
						&& DragonFindReportedTextContaining("Merchant open.")
						&& DragonFindReportedTextContaining("Talk:") == false;
			}
				if(DragonCaseIs("Validation_WorldMap_FrontDoorFacingPriority")) {
				if(gDragonValidation.stepIndex == 0) {
					gDragonValidation.sceneName = "NewGame";
					return DragonValidateNewGameTelemetry();
				}
				if(gDragonValidation.stepIndex == 1) {
					gDragonValidation.sceneName = "NewAvatar";
					return DragonValidateNewAvatarTelemetry("Soldier");
				}
				gDragonValidation.sceneName = "WorldMap";
				if(gDragonValidation.stepIndex == 2)
					return DragonValidateWorldMapStateTelemetry("Position: 25,23", "Lv 1", "HP 125/125", "Gold 10")
						&& DragonFindReportedTextContaining("Map: Eleusis");
				if(gDragonValidation.stepIndex == 3)
					return DragonValidateWorldMapNearPositionTelemetry(12, 19, 2, "Lv 1", "HP 125/125", "Gold 10")
						&& DragonFindReportedTextContaining("Map: Eleusis Stronghold");
				if(gDragonValidation.stepIndex == 4)
					return DragonValidateWorldMapNearPositionTelemetry(21, 11, 2, "Lv 1", "HP 125/125", "Gold 10")
						&& DragonFindReportedTextContaining("Map: Eleusis Stronghold");
				if(gDragonValidation.stepIndex == 5)
					return DragonValidateWorldMapNearPositionTelemetry(81, 16, 2, "Lv 1", "HP 125/125", "Gold 10")
						&& DragonFindReportedTextContaining("Map: Eleusis Stronghold");
				if(gDragonValidation.stepIndex == 6)
					return DragonValidateWorldMapNearPositionTelemetry(74, 12, 2, "Lv 1", "HP 125/125", "Gold 10")
						&& DragonFindReportedTextContaining("Map: Eleusis Stronghold");
					if(gDragonValidation.stepIndex == 7)
						return DragonValidateWorldMapTalkTelemetry("rare items")
							&& DragonFindReportedTextContaining("Lord of Eleusis Stronghold") == false
							&& DragonFindReportedTextContaining("MERCHANT") == false;
				}
				if(DragonCaseIs("Validation_WorldMap_FrontDoorFacingPriorityRetarget")) {
					if(gDragonValidation.stepIndex == 0) {
						gDragonValidation.sceneName = "NewGame";
						return DragonValidateNewGameTelemetry();
					}
					if(gDragonValidation.stepIndex == 1) {
						gDragonValidation.sceneName = "NewAvatar";
						return DragonValidateNewAvatarTelemetry("Soldier");
					}
					gDragonValidation.sceneName = "WorldMap";
					if(gDragonValidation.stepIndex == 2)
						return DragonValidateWorldMapStateTelemetry("Position: 25,23", "Lv 1", "HP 125/125", "Gold 10")
							&& DragonFindReportedTextContaining("Map: Eleusis");
					if(gDragonValidation.stepIndex == 3)
						return DragonValidateWorldMapNearPositionTelemetry(12, 19, 2, "Lv 1", "HP 125/125", "Gold 10")
							&& DragonFindReportedTextContaining("Map: Eleusis Stronghold");
					if(gDragonValidation.stepIndex == 4)
						return DragonValidateWorldMapNearPositionTelemetry(21, 11, 2, "Lv 1", "HP 125/125", "Gold 10")
							&& DragonFindReportedTextContaining("Map: Eleusis Stronghold");
					if(gDragonValidation.stepIndex == 5)
						return DragonValidateWorldMapNearPositionTelemetry(81, 16, 2, "Lv 1", "HP 125/125", "Gold 10")
							&& DragonFindReportedTextContaining("Map: Eleusis Stronghold");
					if(gDragonValidation.stepIndex == 6)
						return DragonValidateWorldMapNearPositionTelemetry(74, 12, 2, "Lv 1", "HP 125/125", "Gold 10")
							&& DragonFindReportedTextContaining("Map: Eleusis Stronghold");
					if(gDragonValidation.stepIndex == 7)
						return (DragonValidateWorldMapMessageTelemetry()
							&& DragonFindReportedTextContaining("Tap ACT or tap the target tile again.")
							&& DragonFindReportedTextContaining("Map: Eleusis Stronghold")
							&& DragonFindReportedTextContaining("Talk:") == false)
							|| (DragonValidateWorldMapTalkTelemetry("rare items")
								&& DragonFindReportedTextContaining("Lord of Eleusis Stronghold") == false
								&& DragonFindReportedTextContaining("MERCHANT") == false);
					if(gDragonValidation.stepIndex == 8)
						return (DragonValidateWorldMapTalkTelemetry("rare items")
							&& DragonFindReportedTextContaining("Lord of Eleusis Stronghold") == false
							&& DragonFindReportedTextContaining("MERCHANT") == false)
							|| (DragonValidateShopTelemetry(0)
								&& DragonFindReportedTextContaining("Merchant open.")
								&& DragonFindReportedTextContaining("Talk:") == false);
				}
				if(DragonCaseIs("Validation_WorldMap_FrontDoorRouteCancel")) {
				if(gDragonValidation.stepIndex == 0) {
					gDragonValidation.sceneName = "NewGame";
				return DragonValidateNewGameTelemetry();
			}
			if(gDragonValidation.stepIndex == 1) {
				gDragonValidation.sceneName = "NewAvatar";
				return DragonValidateNewAvatarTelemetry("Soldier");
			}
			gDragonValidation.sceneName = "WorldMap";
				if(gDragonValidation.stepIndex == 2)
					return DragonValidateWorldMapStateTelemetry("Position: 25,23", "Lv 1", "HP 125/125", "Gold 10")
						&& DragonFindReportedTextContaining("Map: Eleusis")
						&& DragonFindReportedTextContaining("Event 0: Talk 22,17");
				if(gDragonValidation.stepIndex == 3)
					return DragonValidateWorldMapRouteTelemetry(nullptr, "Route requested: 22,17")
						&& DragonFindReportedTextContaining("Map: Eleusis");
				if(gDragonValidation.stepIndex == 4)
					return DragonValidateWorldMapStateTelemetry(nullptr, "Lv 1", "HP 125/125", "Gold 10", nullptr, false)
						&& DragonFindReportedTextContaining("Map: Eleusis")
						&& DragonFindReportedTextContaining("Route target:") == false
						&& DragonFindReportedTextContaining("Route requested:") == false
						&& DragonFindReportedTextContaining("Talk:") == false
						&& DragonFindReportedTextContaining("CONTINUE") == false;
				}
				if(DragonCaseIs("Validation_WorldMap_RouteTargetRetapThrottle")) {
					if(gDragonValidation.stepIndex == 0) {
						gDragonValidation.sceneName = "NewGame";
						return DragonValidateNewGameTelemetry();
					}
					if(gDragonValidation.stepIndex == 1) {
						gDragonValidation.sceneName = "NewAvatar";
						return DragonValidateNewAvatarTelemetry("Soldier");
					}
					gDragonValidation.sceneName = "WorldMap";
					if(gDragonValidation.stepIndex == 2)
						return DragonValidateWorldMapStateTelemetry("Position: 25,23", "Lv 1", "HP 125/125", "Gold 10")
							&& DragonFindReportedTextContaining("Map: Eleusis");
					if(gDragonValidation.stepIndex == 3)
						return DragonValidateWorldMapNearPositionTelemetry(12, 19, 2, "Lv 1", "HP 125/125", "Gold 10")
							&& DragonFindReportedTextContaining("Map: Eleusis Stronghold");
					if(gDragonValidation.stepIndex == 4)
						return DragonValidateWorldMapNearPositionTelemetry(21, 11, 2, "Lv 1", "HP 125/125", "Gold 10")
							&& DragonFindReportedTextContaining("Map: Eleusis Stronghold");
					if(gDragonValidation.stepIndex == 5)
						return DragonValidateWorldMapNearPositionTelemetry(81, 16, 2, "Lv 1", "HP 125/125", "Gold 10")
							&& DragonFindReportedTextContaining("Map: Eleusis Stronghold");
					if(gDragonValidation.stepIndex == 6)
						return DragonValidateWorldMapNearPositionTelemetry(74, 12, 2, "Lv 1", "HP 125/125", "Gold 10")
							&& DragonFindReportedTextContaining("Map: Eleusis Stronghold");
					if(gDragonValidation.stepIndex == 7)
						return (DragonValidateWorldMapMessageTelemetry()
							&& DragonFindReportedTextContaining("Tap ACT or tap the target tile again.")
							&& DragonFindReportedTextContaining("Map: Eleusis Stronghold")
							&& DragonFindReportedTextContaining("Talk:") == false)
							|| DragonValidateWorldMapTalkTelemetry("rare items");
					if(gDragonValidation.stepIndex == 8)
						return (DragonValidateWorldMapMessageTelemetry()
							&& DragonFindReportedTextContaining("Tap ACT or tap the target tile again.")
							&& DragonFindReportedTextContaining("Map: Eleusis Stronghold")
							&& DragonFindReportedTextContaining("Talk:") == false
							&& DragonCountReportedTextContaining("Tap ACT or tap the target tile again.") == 1)
							|| (DragonValidateWorldMapTalkTelemetry("rare items")
								&& DragonCountReportedTextContaining("Talk:") == 1);
				}
				if(DragonCaseIs("Validation_WorldMap_RouteCancelImmediateAct")) {
				gDragonValidation.sceneName = "WorldMap";
				if(gDragonValidation.stepIndex == 0)
					return DragonValidateWorldMapNearPositionTelemetry(25, 23, 2, "Lv 1", "HP 125/125", "Gold 10")
						&& DragonFindReportedTextContaining("Map: Eleusis")
						&& DragonFindReportedTextContaining("Event 0: Talk 22,17");
				if(gDragonValidation.stepIndex == 1)
					return DragonValidateWorldMapRouteTelemetry(nullptr, "Route requested: 30,23")
						&& DragonFindReportedTextContaining("Map: Eleusis");
				if(gDragonValidation.stepIndex == 2)
					return DragonValidateWorldMapNoRouteTelemetry()
						&& DragonFindReportedTextContaining("Map: Eleusis")
						&& DragonFindReportedTextContaining("Talk:") == false
						&& DragonFindReportedTextContaining("MERCHANT") == false;
					if(gDragonValidation.stepIndex == 3)
						return DragonValidateWorldMapNoRouteTelemetry()
							&& DragonFindReportedTextContaining("Map: Eleusis")
							&& DragonFindReportedTextContaining("Talk:") == false
							&& DragonFindReportedTextContaining("MERCHANT") == false;
				}
				if(DragonCaseIs("Validation_WorldMap_RouteCancelDirectionalRetarget")) {
					gDragonValidation.sceneName = "WorldMap";
					if(gDragonValidation.stepIndex == 0)
						return DragonValidateWorldMapNearPositionTelemetry(25, 23, 2, "Lv 1", "HP 125/125", "Gold 10")
							&& DragonFindReportedTextContaining("Map: Eleusis")
							&& DragonFindReportedTextContaining("Event 0: Talk 22,17");
					if(gDragonValidation.stepIndex == 1)
						return DragonValidateWorldMapNearPositionTelemetry(25, 18, 3, "Lv 1", "HP 125/125", "Gold 10")
							&& DragonFindReportedTextContaining("Map: Eleusis");
					if(gDragonValidation.stepIndex == 2)
						return DragonValidateWorldMapNoRouteTelemetry()
							&& DragonFindReportedTextContaining("Map: Eleusis");
					if(gDragonValidation.stepIndex == 3)
						return DragonValidateWorldMapNearPositionTelemetry(30, 23, 3, "Lv 1", "HP 125/125", "Gold 10")
							&& DragonFindReportedTextContaining("Map: Eleusis");
					if(gDragonValidation.stepIndex == 4)
						return DragonValidateWorldMapNoRouteTelemetry()
							&& DragonFindReportedTextContaining("Map: Eleusis");
					if(gDragonValidation.stepIndex == 5)
						return DragonValidateWorldMapNearPositionTelemetry(24, 23, 3, "Lv 1", "HP 125/125", "Gold 10")
							&& DragonFindReportedTextContaining("Map: Eleusis");
					if(gDragonValidation.stepIndex == 6)
						return DragonValidateWorldMapNoRouteTelemetry()
							&& DragonFindReportedTextContaining("Map: Eleusis");
				}
				if(DragonCaseIs("Validation_WorldMap_ActionTalk")) {
					gDragonValidation.sceneName = "WorldMap";
					if(gDragonValidation.stepIndex == 0)
						return DragonValidateWorldMapPendingEventTelemetry(0, "Lv 1", "HP 125/125", "Gold 10")
						&& DragonFindReportedTextContaining("Map: Eleusis");
				if(gDragonValidation.stepIndex == 1)
					return DragonValidateWorldMapTalkTelemetry();
				if(gDragonValidation.stepIndex == 2)
					return DragonValidateWorldMapTelemetry();
		}
		if(DragonCaseIs("Validation_WorldMap_HealEvent")) {
			gDragonValidation.sceneName = "WorldMap";
			if(gDragonValidation.stepIndex == 0)
				return DragonValidateWorldMapStateTelemetry(nullptr, "Lv 1", "HP 45/125", "Gold 10", nullptr, false)
					&& DragonFindReportedTextContaining("Map: The Mountains")
					&& DragonFindReportedTextContaining("Pending event 0: Heal")
					&& DragonFindReportedTextContaining("Event 0: Heal");
			if(gDragonValidation.stepIndex == 1)
				return DragonValidateWorldMapStateTelemetry(nullptr, "Lv 1", "HP 125/125", "Gold 10", "Rest and heal your wounds.", false)
					&& DragonFindReportedTextContaining("Map: The Mountains")
					&& DragonFindReportedTextContaining("Pending event 0: Heal")
					&& DragonFindReportedTextContaining("Event 0: Heal");
		}
		if(DragonCaseIs("Validation_WorldMap_TrainEvent")) {
			gDragonValidation.sceneName = "WorldMap";
			if(gDragonValidation.stepIndex == 0)
				return DragonValidateWorldMapStateTelemetry("Position: 15,9", "Lv 1", "HP 125/125", "Gold 999");
			if(gDragonValidation.stepIndex == 1)
				return DragonValidateWorldMapPromptTelemetry("For 200 gold, I can train you to level 2. Proceed?");
			if(gDragonValidation.stepIndex == 2)
				return DragonValidateWorldMapStateTelemetry("Position: 15,9", "Lv 2", "HP 138/138", "Gold 799", nullptr, false);
		}
		if(DragonCaseIs("Validation_WorldMap_TrainStatusGrowth")) {
			if(gDragonValidation.stepIndex == 0) {
				gDragonValidation.sceneName = "WorldMap";
				return DragonValidateWorldMapStateTelemetry("Position: 15,9", "Lv 1", "HP 125/125", "Gold 999");
			}
			if(gDragonValidation.stepIndex == 1) {
				gDragonValidation.sceneName = "WorldMap";
				return DragonValidateWorldMapPromptTelemetry("For 200 gold, I can train you to level 2. Proceed?");
			}
			if(gDragonValidation.stepIndex == 2) {
				gDragonValidation.sceneName = "WorldMap";
				return DragonValidateWorldMapStateTelemetry("Position: 15,9", "Lv 2", "HP 138/138", "Gold 799", nullptr, false);
			}
			if(gDragonValidation.stepIndex == 3) {
				gDragonValidation.sceneName = "Status";
				return DragonValidateStatusWarriorLevelTwoTelemetry();
			}
		}
		if(DragonCaseIs("Validation_WorldMap_TrainDecline")) {
			gDragonValidation.sceneName = "WorldMap";
			if(gDragonValidation.stepIndex == 0)
				return DragonValidateWorldMapStateTelemetry("Position: 15,9", "Lv 1", "HP 125/125", "Gold 999");
			if(gDragonValidation.stepIndex == 1)
				return DragonValidateWorldMapPromptTelemetry("For 200 gold, I can train you to level 2. Proceed?");
			if(gDragonValidation.stepIndex == 2)
				return DragonValidateWorldMapStateTelemetry("Position: 15,9", "Lv 1", "HP 125/125", "Gold 999", "You decline the training.", false);
		}
		if(DragonCaseIs("Validation_WorldMap_TrainToShopSequence")) {
			gDragonValidation.sceneName = "WorldMap";
			if(gDragonValidation.stepIndex == 0)
				return DragonValidateWorldMapStateTelemetry("Position: 15,9", "Lv 1", "HP 125/125", "Gold 999")
					&& DragonFindReportedTextContaining("Map: Eleusis Stronghold");
			if(gDragonValidation.stepIndex == 1)
				return DragonValidateWorldMapPromptTelemetry("For 200 gold, I can train you to level 2. Proceed?");
			if(gDragonValidation.stepIndex == 2)
				return DragonValidateWorldMapStateTelemetry("Position: 15,9", "Lv 2", "HP 138/138", "Gold 799", nullptr, false)
					&& DragonFindReportedTextContaining("Map: Eleusis Stronghold");
			if(gDragonValidation.stepIndex == 3)
				return DragonValidateWorldMapNearPositionTelemetry(12, 17, 1, "Lv 2", "HP 138/138", "Gold 799")
					&& DragonFindReportedTextContaining("Map: Eleusis Stronghold");
				if(gDragonValidation.stepIndex == 4)
					return DragonValidateWorldMapNearPositionTelemetry(24, 17, 1, "Lv 2", "HP 138/138", "Gold 799")
						&& DragonFindReportedTextContaining("Map: Eleusis");
			if(gDragonValidation.stepIndex == 5)
				return DragonValidateWorldMapNearPositionTelemetry(24, 17, 1, "Lv 2", "HP 138/138", "Gold 799")
					&& DragonFindReportedTextContaining("Map: Eleusis")
					&& (DragonFindReportedTextContaining("Event 3: Shop 24,59")
						|| DragonFindReportedTextContaining("Pending event 3: Shop 24,59"));
		}
		if(DragonCaseIs("Validation_WorldMap_ChallengeEvent")) {
			if(gDragonValidation.stepIndex == 0) {
				gDragonValidation.sceneName = "WorldMap";
				return DragonValidateWorldMapPendingEventTelemetry(3, "Lv 1", "HP 125/125", "Gold 999")
					&& DragonFindReportedTextContaining("Map: Eleusis Stronghold");
			}
			if(gDragonValidation.stepIndex == 1) {
				gDragonValidation.sceneName = "WorldMap";
				return DragonValidateWorldMapPromptTelemetry("Would you like to test your skills against the tip of my spear?");
			}
			if(gDragonValidation.stepIndex == 2) {
				gDragonValidation.sceneName = "Battle";
				return DragonValidateBattleEncounterTelemetry(1, false);
			}
			if(gDragonValidation.stepIndex == 3) {
				gDragonValidation.sceneName = "Battle";
				return DragonValidateBattleActionTelemetry()
					|| DragonValidateBattleCompletionTelemetry("DEFEAT")
					|| DragonValidateBattleCompletionTelemetry("VICTORY");
			}
		}
		if(DragonCaseIs("Validation_WorldMap_ChallengeDecline")) {
			gDragonValidation.sceneName = "WorldMap";
			if(gDragonValidation.stepIndex == 0)
				return DragonValidateWorldMapPendingEventTelemetry(3, "Lv 1", "HP 125/125", "Gold 999")
					&& DragonFindReportedTextContaining("Map: Eleusis Stronghold");
			if(gDragonValidation.stepIndex == 1)
				return DragonValidateWorldMapPromptTelemetry("Would you like to test your skills against the tip of my spear?");
			if(gDragonValidation.stepIndex == 2)
				return DragonValidateWorldMapNearPositionTelemetry(24, 59, 1, "Lv 1", "HP 125/125", "Gold 999", "You decline the challenge for now.")
					&& DragonFindReportedTextContaining("Map: Eleusis Stronghold");
		}
		if(DragonCaseIs("Validation_WorldMap_ChallengeDefeatPanel")) {
			if(gDragonValidation.stepIndex == 0) {
				gDragonValidation.sceneName = "WorldMap";
				return DragonValidateWorldMapPendingEventTelemetry(3, "Lv 1", "HP 125/125", "Gold 999")
					&& DragonFindReportedTextContaining("Map: Eleusis Stronghold");
			}
			if(gDragonValidation.stepIndex == 1) {
				gDragonValidation.sceneName = "WorldMap";
				return DragonValidateWorldMapPromptTelemetry("Would you like to test your skills against the tip of my spear?");
			}
			if(gDragonValidation.stepIndex == 2) {
				gDragonValidation.sceneName = "Battle";
				return DragonValidateBattleEncounterTelemetry(1, false, nullptr, false);
			}
			if(gDragonValidation.stepIndex == 3) {
				gDragonValidation.sceneName = "Battle";
				return DragonValidateBattleCompletionTelemetry("DEFEAT");
			}
		}
		if(DragonCaseIs("Validation_WorldMap_ChallengeVictoryProgression")) {
			if(gDragonValidation.stepIndex == 0) {
				gDragonValidation.sceneName = "WorldMap";
				return DragonValidateWorldMapPendingEventTelemetry(3, "Lv 1", "HP 125/125", "Gold 999")
					&& DragonFindReportedTextContaining("Map: Eleusis Stronghold");
			}
			if(gDragonValidation.stepIndex == 1) {
				gDragonValidation.sceneName = "WorldMap";
				return DragonValidateWorldMapPromptTelemetry("Would you like to test your skills against the tip of my spear?");
			}
			if(gDragonValidation.stepIndex == 2) {
				gDragonValidation.sceneName = "Battle";
				return DragonValidateBattleEncounterTelemetry(1, false, nullptr, false);
			}
			if(gDragonValidation.stepIndex == 3) {
				gDragonValidation.sceneName = "Battle";
				return DragonValidateBattleCompletionTelemetry("VICTORY");
			}
			if(gDragonValidation.stepIndex == 4) {
				gDragonValidation.sceneName = "WorldMap";
				return DragonValidateWorldMapNearPositionTelemetry(23, 59, 1, "Lv 1", "HP 125/125")
					&& DragonFindReportedTextContaining("Map: Eleusis Stronghold")
					&& DragonFindReportedTextContaining("Pending event 3: Talk 24,59")
					&& DragonFindReportedTextContaining("CONTINUE") == false
					&& DragonFindReportedTextContaining("VICTORY") == false;
			}
		}
		if(DragonCaseIs("Validation_WorldMap_ShopEntry")) {
			if(gDragonValidation.stepIndex == 0) {
				gDragonValidation.sceneName = "WorldMap";
				return DragonValidateWorldMapStateTelemetry("Position: 23,59", "Lv 1", "HP 125/125", "Gold 999");
			}
			if(gDragonValidation.stepIndex == 1) {
				gDragonValidation.sceneName = "WorldMap";
				return DragonValidateWorldMapNearPositionTelemetry(23, 59, 1, "Lv 1", "HP 125/125", "Gold 999")
					&& (DragonFindReportedTextContaining("Event 3: Shop 24,59")
						|| DragonFindReportedTextContaining("Pending event 3: Shop 24,59"));
			}
			if(gDragonValidation.stepIndex == 2) {
				gDragonValidation.sceneName = "WorldMap";
				return DragonValidateWorldMapPromptOrShopTelemetry()
					|| (DragonValidateWorldMapNearPositionTelemetry(23, 59, 1, "Lv 1", "HP 125/125", "Gold 999")
						&& (DragonFindReportedTextContaining("Event 3: Shop 24,59")
							|| DragonFindReportedTextContaining("Pending event 3: Shop 24,59")));
			}
		}
			if(DragonCaseIs("Validation_WorldMap_WarpTransition")) {
				gDragonValidation.sceneName = "WorldMap";
				if(gDragonValidation.stepIndex == 0)
					return DragonValidateWorldMapNearPositionTelemetry(12, 18, 1, "Lv 1", "HP 125/125", "Gold 10")
						&& DragonFindReportedTextContaining("Map: Eleusis Stronghold");
				if(gDragonValidation.stepIndex == 1)
					return DragonValidateWorldMapNearPositionTelemetry(24, 17, 1, "Lv 1", "HP 125/125", "Gold 10")
						&& DragonFindReportedTextContaining("Map: Eleusis");
			}
			if(DragonCaseIs("Validation_WorldMap_AutoPathTreasure")) {
				gDragonValidation.sceneName = "WorldMap";
				if(gDragonValidation.stepIndex == 0)
					return DragonValidateWorldMapStateTelemetry(nullptr, "Lv 1", "HP 125/125", "Gold 10")
						&& DragonFindReportedTextContaining("Map: Eleusis")
						&& DragonFindReportedTextContaining("Treasure");
				if(gDragonValidation.stepIndex == 1)
					return DragonValidateWorldMapGoldAboveTelemetry(10, "Lv 1", "HP 125/125")
						&& DragonFindReportedTextContaining("Map: Eleusis");
			}
			if(DragonCaseIs("Validation_WorldMap_CurrentTileRouteCancel")) {
			gDragonValidation.sceneName = "WorldMap";
			if(gDragonValidation.stepIndex == 0)
				return DragonValidateWorldMapStateTelemetry(nullptr, "Lv 1", "HP 125/125", "Gold 10")
					&& DragonFindReportedTextContaining("Map: Eleusis");
				if(gDragonValidation.stepIndex == 1)
					return DragonValidateWorldMapRouteTelemetry();
			if(gDragonValidation.stepIndex == 2)
				return DragonValidateWorldMapStateTelemetry(nullptr, "Lv 1", "HP 125/125", nullptr, nullptr, false)
					&& DragonFindReportedTextContaining("Route target:") == false
					&& DragonFindReportedTextContaining("Route requested:") == false;
		}
		if(DragonCaseIs("Validation_WorldMap_GateRouteMagnet")) {
			gDragonValidation.sceneName = "WorldMap";
			if(gDragonValidation.stepIndex == 0)
				return DragonValidateWorldMapNearPositionTelemetry(66, 19, 1, "Lv 7", "HP 117/117", "Gold 245")
					&& DragonFindReportedTextContaining("Map: Eleusis Caves");
			if(gDragonValidation.stepIndex == 1)
				return DragonValidateWorldMapRouteTelemetry("Route target: 64,17", "Route requested: 63,17")
					|| DragonValidateBattleEncounterTelemetry(1, false);
		}
			if(DragonCaseIs("Validation_WorldMap_GateBattleEntry")) {
				if(gDragonValidation.stepIndex == 0) {
					gDragonValidation.sceneName = "WorldMap";
					return DragonValidateWorldMapNearPositionTelemetry(41, 48, 2, "Lv 24", nullptr, "Gold 9999")
						&& DragonFindReportedTextContaining("Map: The Peak");
				}
			if(gDragonValidation.stepIndex == 1) {
				gDragonValidation.sceneName = "Battle";
				return DragonValidateBattleEncounterTelemetry(1, true, nullptr, false);
			}
			if(gDragonValidation.stepIndex == 2) {
				gDragonValidation.sceneName = "Battle";
				return DragonValidateBattleEncounterTelemetry(1, true, nullptr, false);
			}
			if(gDragonValidation.stepIndex == 3) {
				gDragonValidation.sceneName = "Battle";
				return DragonValidateBattleActionTelemetry();
			}
		}
		if(DragonCaseIs("Validation_WorldMap_GateBattleProgression")) {
			if(gDragonValidation.stepIndex == 0) {
				gDragonValidation.sceneName = "WorldMap";
				return DragonValidateWorldMapNearPositionTelemetry(41, 48, 2, nullptr, nullptr, "Gold 9999")
					&& DragonFindReportedTextContaining("Map: The Peak");
			}
			if(gDragonValidation.stepIndex == 1) {
				gDragonValidation.sceneName = "Battle";
				return DragonValidateBattleEncounterTelemetry(1, true, nullptr, false);
			}
			if(gDragonValidation.stepIndex == 2) {
				gDragonValidation.sceneName = "Battle";
				return DragonValidateBattleEncounterTelemetry(1, true, nullptr, false);
			}
			if(gDragonValidation.stepIndex == 3) {
				gDragonValidation.sceneName = "Battle";
				return DragonValidateBattleCompletionTelemetry("VICTORY");
			}
			if(gDragonValidation.stepIndex == 4) {
				gDragonValidation.sceneName = "WorldMap";
				return DragonValidateWorldMapTalkTelemetry("Who dares to destroy my masters creation.")
					&& DragonValidateWorldMapNearPositionTelemetry(41, 48, 2);
			}
		}
		if(DragonCaseIs("Validation_WorldMap_RandomBattleEntry")) {
			if(gDragonValidation.stepIndex == 0) {
				gDragonValidation.sceneName = "WorldMap";
				return DragonValidateWorldMapNearPositionTelemetry(30, 18, 0, "Lv 7", "HP 117/117", "Gold 245")
					&& DragonFindReportedTextContaining("Map: Eleusis Caves");
			}
			if(gDragonValidation.stepIndex == 1) {
				gDragonValidation.sceneName = "Battle";
				return DragonValidateBattleEncounterTelemetry(1, false);
			}
		}
		if(DragonCaseIs("Validation_WorldMap_RandomBattleRetreat")) {
			if(gDragonValidation.stepIndex == 0) {
				gDragonValidation.sceneName = "WorldMap";
				return DragonValidateWorldMapNearPositionTelemetry(30, 18, 0, "Lv 7", "HP 117/117", "Gold 245")
					&& DragonFindReportedTextContaining("Map: Eleusis Caves");
			}
			if(gDragonValidation.stepIndex == 1) {
				gDragonValidation.sceneName = "Battle";
				return DragonValidateBattleEncounterTelemetry(1, false);
			}
			if(gDragonValidation.stepIndex == 2) {
				gDragonValidation.sceneName = "Battle";
				return DragonValidateBattleCompletionTelemetry("RETREATED");
			}
			if(gDragonValidation.stepIndex == 3) {
				gDragonValidation.sceneName = "WorldMap";
				return DragonValidateWorldMapNearPositionTelemetry(31, 18, 0, "Lv 7", nullptr, "Gold 245")
					&& DragonFindReportedTextContaining("Map: Eleusis Caves")
					&& DragonFindReportedTextContaining("CONTINUE") == false
					&& DragonFindReportedTextContaining("RETREATED") == false;
			}
		}
		if(DragonCaseIs("Validation_Status_Default")) {
			if(gDragonValidation.stepIndex == 0) {
				gDragonValidation.sceneName = "NewGame";
				return DragonValidateNewGameTelemetry();
		}
		if(gDragonValidation.stepIndex == 1) {
			gDragonValidation.sceneName = "NewAvatar";
			return DragonValidateNewAvatarTelemetry("Soldier");
		}
		if(gDragonValidation.stepIndex == 2) {
			gDragonValidation.sceneName = "WorldMap";
			return DragonValidateWorldMapTelemetry();
		}
		if(gDragonValidation.stepIndex == 3) {
			gDragonValidation.sceneName = "Status";
			return DragonValidateStatusTelemetry();
		}
	}
	if(DragonCaseIs("Validation_Status_UsePotion")) {
		gDragonValidation.sceneName = "Status";
		if(gDragonValidation.stepIndex == 0)
			return DragonValidateStatusTelemetry();
		if(gDragonValidation.stepIndex == 1)
			return DragonValidateStatusUsePotionTelemetry();
	}
	if(DragonCaseIs("Validation_Status_SellScroll")) {
		gDragonValidation.sceneName = "Status";
		if(gDragonValidation.stepIndex == 0)
			return DragonValidateStatusTelemetry();
		if(gDragonValidation.stepIndex == 1)
			return DragonValidateStatusFireScrollSelectedTelemetry();
		if(gDragonValidation.stepIndex == 2)
			return DragonValidateStatusSellScrollTelemetry();
	}
	if(DragonCaseIs("Validation_Status_UnequipWeapon")) {
		gDragonValidation.sceneName = "Status";
		if(gDragonValidation.stepIndex == 0)
			return DragonValidateStatusTelemetry();
		if(gDragonValidation.stepIndex == 1)
			return DragonValidateStatusUnequipWeaponTelemetry();
	}
	if(DragonCaseIs("Validation_Status_UnequipArmor")) {
		gDragonValidation.sceneName = "Status";
		if(gDragonValidation.stepIndex == 0)
			return DragonValidateStatusTelemetry();
		if(gDragonValidation.stepIndex == 1)
			return DragonValidateStatusUnequipArmorTelemetry();
	}
	if(DragonCaseIs("Validation_Status_UnequipRelic")) {
		gDragonValidation.sceneName = "Status";
		if(gDragonValidation.stepIndex == 0)
			return DragonValidateStatusSorcererTelemetry();
		if(gDragonValidation.stepIndex == 1)
			return DragonValidateStatusUnequipRelicTelemetry();
	}
	if(DragonCaseIs("Validation_Status_EquipWeapon")) {
		gDragonValidation.sceneName = "Status";
		if(gDragonValidation.stepIndex == 0)
			return DragonValidateStatusEquipWeaponReadyTelemetry();
		if(gDragonValidation.stepIndex == 1)
			return DragonValidateStatusEquipWeaponTelemetry();
	}
	if(DragonCaseIs("Validation_Status_EquipArmor")) {
		gDragonValidation.sceneName = "Status";
		if(gDragonValidation.stepIndex == 0)
			return DragonValidateStatusEquipArmorReadyTelemetry();
		if(gDragonValidation.stepIndex == 1)
			return DragonValidateStatusEquipArmorTelemetry();
	}
	if(DragonCaseIs("Validation_Status_EquipRelic")) {
		gDragonValidation.sceneName = "Status";
		if(gDragonValidation.stepIndex == 0)
			return DragonValidateStatusEquipRelicReadyTelemetry();
		if(gDragonValidation.stepIndex == 1)
			return DragonValidateStatusEquipRelicTelemetry();
	}
	if(DragonCaseIs("Validation_Status_SellEquippedWeapon")) {
		gDragonValidation.sceneName = "Status";
		if(gDragonValidation.stepIndex == 0)
			return DragonValidateStatusSellEquippedWeaponReadyTelemetry();
		if(gDragonValidation.stepIndex == 1)
			return DragonValidateStatusSellEquippedWeaponTelemetry();
	}
	if(DragonCaseIs("Validation_Status_UnequipWeaponInventoryFull")) {
		gDragonValidation.sceneName = "Status";
		if(gDragonValidation.stepIndex == 0)
			return DragonValidateStatusInventoryFullReadyTelemetry();
		if(gDragonValidation.stepIndex == 1)
			return DragonValidateStatusInventoryFullBlockedTelemetry();
	}
	if(DragonCaseIs("Validation_Status_LargeInventoryPaging")) {
		static const char* kExpectedItems[] = {
			"Adept Wand",
			"Steel Saber",
			"War Hammer",
			"Mystic Tome",
			"Leather Armor",
			"Traveler Cloak",
			"Tower Shield",
			"Scout Charm",
			"Seer Charm",
			"Dragon Talisman",
			"Guardian Ring",
			"Health Potion",
		};
		static const char* kExpectedActions[] = {
			"EQUIP",
			"EQUIP",
			"EQUIP",
			"EQUIP",
			"EQUIP",
			"EQUIP",
			"EQUIP",
			"EQUIP",
			"EQUIP",
			"EQUIP",
			"EQUIP",
			"USE",
		};
		gDragonValidation.sceneName = "Status";
		if(gDragonValidation.stepIndex >= 0 && gDragonValidation.stepIndex < (int)(sizeof(kExpectedItems) / sizeof(kExpectedItems[0])))
			return DragonValidateStatusLargeInventoryTelemetry(kExpectedItems[gDragonValidation.stepIndex], kExpectedActions[gDragonValidation.stepIndex]);
	}
	if(DragonCaseIs("Validation_SaveLoad_RoundTrip")) {
		if(gDragonValidation.stepIndex == 0) {
			gDragonValidation.sceneName = "NewGame";
			return DragonValidateNewGameTelemetry();
		}
		if(gDragonValidation.stepIndex == 1) {
			gDragonValidation.sceneName = "NewAvatar";
			return DragonValidateNewAvatarTelemetry("Soldier");
		}
		if(gDragonValidation.stepIndex == 2) {
			gDragonValidation.sceneName = "WorldMap";
			return DragonValidateWorldMapTelemetry();
		}
		if(gDragonValidation.stepIndex == 3) {
			gDragonValidation.sceneName = "WorldMap";
			return DragonValidateWorldMapSavedTelemetry();
		}
		if(gDragonValidation.stepIndex == 4) {
			gDragonValidation.sceneName = "NewGame";
			return DragonValidateNewGameFilledSlotTelemetry();
		}
		if(gDragonValidation.stepIndex == 5) {
			gDragonValidation.sceneName = "WorldMap";
			return DragonValidateWorldMapRoundTripTelemetry();
		}
		if(gDragonValidation.stepIndex == 6) {
			gDragonValidation.sceneName = "Status";
			return DragonValidateStatusTelemetry();
		}
	}
	if(DragonCaseIs("Validation_SaveLoad_ProgressionState")) {
		gDragonValidation.sceneName = "WorldMap";
		if(gDragonValidation.stepIndex == 0)
			return DragonValidateWorldMapProgressionTelemetry();
		if(gDragonValidation.stepIndex == 1)
			return DragonValidateWorldMapSavedProgressionTelemetry();
		if(gDragonValidation.stepIndex == 2) {
			gDragonValidation.sceneName = "NewGame";
			return DragonValidateNewGameProgressionSlotTelemetry();
		}
		if(gDragonValidation.stepIndex == 3) {
			gDragonValidation.sceneName = "WorldMap";
			return DragonValidateWorldMapProgressionTelemetry();
		}
	}
	if(DragonCaseIs("Validation_SaveLoad_LaterGameLoadoutState")) {
		gDragonValidation.sceneName = "WorldMap";
		if(gDragonValidation.stepIndex == 0)
			return DragonValidateWorldMapProgressionLoadoutTelemetry();
		if(gDragonValidation.stepIndex == 1)
			return DragonValidateWorldMapSavedProgressionLoadoutTelemetry();
		if(gDragonValidation.stepIndex == 2) {
			gDragonValidation.sceneName = "NewGame";
			return DragonValidateNewGameProgressionLoadoutSlotTelemetry();
		}
		if(gDragonValidation.stepIndex == 3) {
			gDragonValidation.sceneName = "WorldMap";
			return DragonValidateWorldMapProgressionLoadoutTelemetry();
		}
		if(gDragonValidation.stepIndex == 4) {
			gDragonValidation.sceneName = "Status";
			return DragonValidateStatusLargeInventoryTelemetry("Adept Wand", "EQUIP");
		}
	}
	if(DragonCaseIs("Validation_Shop_Default")) {
		gDragonValidation.sceneName = "Shop";
		if(gDragonValidation.stepIndex == 0)
			return DragonValidateShopTelemetry(0);
		if(gDragonValidation.stepIndex == 1)
			return DragonValidateShopTelemetry(6);
	}
	if(DragonCaseIs("Validation_Shop_ReturnToWorldMap")) {
		gDragonValidation.sceneName = "Shop";
		if(gDragonValidation.stepIndex == 0)
			return DragonValidateShopTelemetry(0);
		if(gDragonValidation.stepIndex == 1)
			return DragonValidateShopTelemetry(6);
		if(gDragonValidation.stepIndex == 2) {
			gDragonValidation.sceneName = "WorldMap";
			return DragonValidateWorldMapPostShopTelemetry();
		}
	}
	if(DragonCaseIs("Validation_Shop_Purchase")) {
		gDragonValidation.sceneName = "Shop";
		if(gDragonValidation.stepIndex == 0)
			return DragonValidateShopTelemetry(0);
		if(gDragonValidation.stepIndex == 1)
			return DragonValidateShopTelemetry(6);
		if(gDragonValidation.stepIndex == 2)
			return DragonValidateShopPurchaseTelemetry();
	}
	if(DragonCaseIs("Validation_Shop_InsufficientGold")) {
		gDragonValidation.sceneName = "Shop";
		if(gDragonValidation.stepIndex == 0)
			return DragonValidateShopTelemetry(0);
		if(gDragonValidation.stepIndex == 1)
			return DragonValidateShopTelemetry(6);
		if(gDragonValidation.stepIndex == 2)
			return DragonValidateShopInsufficientGoldTelemetry();
	}
	if(DragonCaseIs("Validation_Shop_DuplicateOwned")) {
		gDragonValidation.sceneName = "Shop";
		if(gDragonValidation.stepIndex == 0)
			return DragonValidateShopTelemetry(0);
		if(gDragonValidation.stepIndex == 1)
			return DragonValidateShopTelemetry(6);
		if(gDragonValidation.stepIndex == 2)
			return DragonValidateShopDuplicateOwnedTelemetry();
	}
	if(DragonCaseIs("Validation_Shop_InventoryFull")) {
		gDragonValidation.sceneName = "Shop";
		if(gDragonValidation.stepIndex == 0)
			return DragonValidateShopTelemetry(0);
		if(gDragonValidation.stepIndex == 1)
			return DragonValidateShopTelemetry(6);
		if(gDragonValidation.stepIndex == 2)
			return DragonValidateShopInventoryFullTelemetry();
	}
	if(DragonCaseIs("Validation_Shop_MultiPurchase")) {
		gDragonValidation.sceneName = "Shop";
		if(gDragonValidation.stepIndex == 0)
			return DragonValidateShopTelemetry(0);
		if(gDragonValidation.stepIndex == 1)
			return DragonValidateShopConsumablePurchaseTelemetry("Purchased Traveler Cloak for 50 gold. 949 left.");
		if(gDragonValidation.stepIndex == 2)
			return DragonValidateShopConsumablePurchaseTelemetry("Purchased Scout Charm for 70 gold. 879 left.");
	}
	if(DragonCaseIs("Validation_Battle_TargetSelection")) {
		gDragonValidation.sceneName = "Battle";
		if(gDragonValidation.stepIndex == 0)
			return DragonValidateBattleTelemetry(0);
		if(gDragonValidation.stepIndex == 1)
			return DragonValidateBattleTelemetry(2);
		if(gDragonValidation.stepIndex == 2)
			return DragonValidateBattleTelemetry(3);
	}
	if(DragonCaseIs("Validation_Battle_CommandResolution")) {
		gDragonValidation.sceneName = "Battle";
		if(gDragonValidation.stepIndex == 0)
			return DragonValidateBattleTelemetry(0);
		if(gDragonValidation.stepIndex == 1)
			return DragonValidateBattlePendingTelemetry("ATTACK");
		if(gDragonValidation.stepIndex == 2)
			return DragonValidateBattleActionTelemetry();
	}
	if(DragonCaseIs("Validation_Battle_SeededOutcome")) {
		gDragonValidation.sceneName = "Battle";
		if(gDragonValidation.stepIndex == 0)
			return DragonValidateBattleTelemetry(0);
		if(gDragonValidation.stepIndex == 1)
			return DragonValidateBattlePendingTelemetry("ATTACK");
		if(gDragonValidation.stepIndex == 2)
			return DragonValidateBattleSeededOutcomeTelemetry("Player action result: Attack deals 105.", nullptr);
	}
	if(DragonCaseIs("Validation_Battle_MultiEnemyContinue")) {
		gDragonValidation.sceneName = "Battle";
		if(gDragonValidation.stepIndex == 0)
			return DragonValidateBattleTelemetry(0);
		if(gDragonValidation.stepIndex == 1)
			return DragonValidateBattlePendingTargetTelemetry("ATTACK", 0);
		if(gDragonValidation.stepIndex == 2)
			return DragonValidateBattleMultiEnemyContinueTelemetry();
	}
	if(DragonCaseIs("Validation_Battle_EnemySpecialResponse")) {
		gDragonValidation.sceneName = "Battle";
		if(gDragonValidation.stepIndex == 0)
			return DragonValidateBattleEncounterTelemetry(1, false, nullptr, false);
		if(gDragonValidation.stepIndex == 1)
			return DragonValidateBattleEnemyActionTelemetry("Enemy action result: Goblin Peasant used Dark Jab for 84.");
	}
	if(DragonCaseIs("Validation_Battle_EnemyHealResponse")) {
		gDragonValidation.sceneName = "Battle";
		if(gDragonValidation.stepIndex == 0)
			return DragonValidateBattleEncounterTelemetry(1, false, nullptr, false);
		if(gDragonValidation.stepIndex == 1)
			return DragonValidateBattleEnemyActionTelemetry("Enemy action result: Goblin Peasant used Goblin Courage and recovered 15 HP.");
	}
	if(DragonCaseIs("Validation_Battle_NoDamageResponse")) {
		gDragonValidation.sceneName = "Battle";
		if(gDragonValidation.stepIndex == 0)
			return DragonValidateBattleEncounterTelemetry(1, false, nullptr, false);
		if(gDragonValidation.stepIndex == 1)
			return DragonValidateBattleEnemyActionTelemetry("Enemy action result: Goblin Peasant used Dark Jab, but it dealt no damage.");
	}
	if(DragonCaseIs("Validation_Battle_VictoryPanel")) {
		gDragonValidation.sceneName = "Battle";
		if(gDragonValidation.stepIndex == 0)
			return DragonValidateBattleEncounterTelemetry(1, false, nullptr, false);
		if(gDragonValidation.stepIndex == 1)
			return DragonValidateBattleCompletionTelemetry("VICTORY");
	}
	if(DragonCaseIs("Validation_Battle_Magic_SeededOutcome")) {
		gDragonValidation.sceneName = "Battle";
		if(gDragonValidation.stepIndex == 0)
			return DragonValidateBattleTelemetry(0);
		if(gDragonValidation.stepIndex == 1)
			return DragonValidateBattleLegacyMenuTelemetry("MAGIC", "Cure", "Guard Heal");
		if(gDragonValidation.stepIndex == 2)
			return DragonValidateBattleSelfEffectTelemetry("Player action result: Cure restores 75 HP.", nullptr);
	}
	if(DragonCaseIs("Validation_Battle_Tech_SeededOutcome")) {
		gDragonValidation.sceneName = "Battle";
		if(gDragonValidation.stepIndex == 0)
			return DragonValidateBattleTelemetry(0);
		if(gDragonValidation.stepIndex == 1)
			return DragonValidateBattleLegacyMenuTelemetry("TECH", "Rally", "Power Strike");
		if(gDragonValidation.stepIndex == 2)
			return DragonValidateBattleSelfEffectTelemetry("Player action result: Rally restores 45 HP.", nullptr);
	}
	if(DragonCaseIs("Validation_Battle_ElementalNeutralOutcome")) {
		gDragonValidation.sceneName = "Battle";
		if(gDragonValidation.stepIndex == 0)
			return DragonValidateBattleEncounterTelemetry(1, false, nullptr, false);
		if(gDragonValidation.stepIndex == 1)
			return DragonValidateBattleLegacyMenuTelemetry("TECH", "Pin Shot", "Volley");
		if(gDragonValidation.stepIndex == 2)
			return DragonValidateBattleElementalOutcomeTelemetry("Player action result: Pin Shot deals ", "Dead Fighter", 380, 380, false);
	}
	if(DragonCaseIs("Validation_Battle_ElementalResistOutcome")) {
		gDragonValidation.sceneName = "Battle";
		if(gDragonValidation.stepIndex == 0)
			return DragonValidateBattleEncounterTelemetry(1, false, nullptr, false);
		if(gDragonValidation.stepIndex == 1)
			return DragonValidateBattleLegacyMenuTelemetry("TECH", "Pin Shot", "Volley");
		if(gDragonValidation.stepIndex == 2)
			return DragonValidateBattleElementalOutcomeTelemetry("Player action result: Sky Dragon absorbed Pin Shot.", "Sky Dragon", 950, 1900, true);
	}
	return false;
}

static bool DragonInjectCurrentCaseInput () {
	if(DragonCaseIs("Validation_Splash_Default") && gDragonValidation.stepIndex == 0) {
		DragonInjectSplashAboutTap();
		return true;
	}
	if(DragonCaseIs("Validation_NewGame_Default") && gDragonValidation.stepIndex == 0) {
		DragonInjectSplashNewGameTap();
		return true;
	}
	if(DragonCaseIs("Validation_NewGame_FilledSlots") && gDragonValidation.stepIndex == 0) {
		DragonInjectSplashNewGameTap();
		return true;
	}
	if(DragonCaseIs("Validation_NewGame_DeleteConfirm")) {
		if(gDragonValidation.stepIndex == 0) {
			DragonInjectSplashNewGameTap();
			return true;
		}
		if(gDragonValidation.stepIndex == 1) {
			DragonInjectNewGameDeleteTap(1);
			return true;
		}
		if(gDragonValidation.stepIndex == 2)
			return DragonInjectReportedTextTapContaining("DELETE");
	}
	if(DragonCaseIs("Validation_NewGame_CorruptRecovery") && gDragonValidation.stepIndex == 0) {
		DragonInjectSplashNewGameTap();
		return true;
	}
	if(DragonCaseIs("Validation_Splash_OpenGame_LoadSlot")) {
		if(gDragonValidation.stepIndex == 0) {
			DragonInjectSplashOpenGameTap();
			return true;
		}
		if(gDragonValidation.stepIndex == 1) {
			DragonInjectNewGameSlotTap(0);
			return true;
		}
		if(gDragonValidation.stepIndex == 2)
			return true;
	}
	if(DragonCaseIs("Validation_NewAvatar_Default")) {
		if(gDragonValidation.stepIndex == 0) {
			DragonInjectSplashNewGameTap();
			return true;
		}
		if(gDragonValidation.stepIndex == 1) {
			DragonInjectNewGameSlotTap(0);
			return true;
		}
		if(gDragonValidation.stepIndex == 2) {
			DragonInjectNewAvatarClassTap(1);
			return true;
		}
	}
	if(DragonCaseIs("Validation_NewAvatar_MageStatus")) {
		if(gDragonValidation.stepIndex == 0) {
			DragonInjectSplashNewGameTap();
			return true;
		}
		if(gDragonValidation.stepIndex == 1) {
			DragonInjectNewGameSlotTap(0);
			return true;
		}
		if(gDragonValidation.stepIndex == 2) {
			DragonInjectNewAvatarClassTap(1);
			return true;
		}
		if(gDragonValidation.stepIndex == 3) {
			DragonInjectNewAvatarBeginGameTap();
			return true;
		}
		if(gDragonValidation.stepIndex == 4) {
			DragonInjectWorldMapStatusTap();
			return true;
		}
	}
	if(DragonCaseIs("Validation_NewAvatar_ThiefStatus")) {
		if(gDragonValidation.stepIndex == 0) {
			DragonInjectSplashNewGameTap();
			return true;
		}
		if(gDragonValidation.stepIndex == 1) {
			DragonInjectNewGameSlotTap(0);
			return true;
		}
		if(gDragonValidation.stepIndex == 2) {
			DragonInjectNewAvatarClassTap(2);
			return true;
		}
		if(gDragonValidation.stepIndex == 3) {
			DragonInjectNewAvatarBeginGameTap();
			return true;
		}
		if(gDragonValidation.stepIndex == 4) {
			DragonInjectWorldMapStatusTap();
			return true;
		}
	}
		if(DragonCaseIs("Validation_WorldMap_Default")) {
			if(gDragonValidation.stepIndex == 0) {
				DragonInjectSplashNewGameTap();
				return true;
		}
		if(gDragonValidation.stepIndex == 1) {
			DragonInjectNewGameSlotTap(0);
			return true;
		}
		if(gDragonValidation.stepIndex == 2) {
			DragonInjectNewAvatarBeginGameTap();
			return true;
			}
		}
		if(DragonCaseIs("Validation_WorldMap_MagicButton")) {
			if(gDragonValidation.stepIndex == 0) {
				DragonInjectSplashNewGameTap();
				return true;
			}
			if(gDragonValidation.stepIndex == 1) {
				DragonInjectNewGameSlotTap(0);
				return true;
			}
			if(gDragonValidation.stepIndex == 2) {
				DragonInjectNewAvatarBeginGameTap();
				return true;
			}
			if(gDragonValidation.stepIndex == 3) {
				DragonInjectWorldMapActionTap();
				return true;
			}
		}
		if(DragonCaseIs("Validation_WorldMap_TechButton")) {
			if(gDragonValidation.stepIndex == 0) {
				DragonInjectSplashNewGameTap();
				return true;
			}
			if(gDragonValidation.stepIndex == 1) {
				DragonInjectNewGameSlotTap(0);
				return true;
			}
			if(gDragonValidation.stepIndex == 2) {
				DragonInjectNewAvatarBeginGameTap();
				return true;
			}
			if(gDragonValidation.stepIndex == 3) {
				DragonInjectWorldMapTechTap();
				return true;
			}
		}
		if(DragonCaseIs("Validation_WorldMap_FrontDoorTalkRoute")) {
			if(gDragonValidation.stepIndex == 0) {
				DragonInjectSplashNewGameTap();
				return true;
			}
			if(gDragonValidation.stepIndex == 1) {
				DragonInjectNewGameSlotTap(0);
				return true;
			}
				if(gDragonValidation.stepIndex == 2) {
					DragonInjectNewAvatarBeginGameTap();
					return true;
				}
				if(gDragonValidation.stepIndex == 3)
					return DragonInjectWorldMapEventTap(0);
				if(gDragonValidation.stepIndex == 4)
					return DragonInjectWorldMapEventTap(0);
				if(gDragonValidation.stepIndex == 5) {
					if(DragonFindReportedTextContaining("CONTINUE"))
						return DragonInjectReportedTextBottomTapContaining("CONTINUE");
					return true;
				}
			}
		if(DragonCaseIs("Validation_WorldMap_FrontDoorShopNpcTalk")) {
			if(gDragonValidation.stepIndex == 0) {
				DragonInjectSplashNewGameTap();
				return true;
			}
			if(gDragonValidation.stepIndex == 1) {
				DragonInjectNewGameSlotTap(0);
				return true;
			}
			if(gDragonValidation.stepIndex == 2) {
				DragonInjectNewAvatarBeginGameTap();
				return true;
			}
			if(gDragonValidation.stepIndex == 3)
				return DragonInjectWorldMapCellTap(25, 16);
			if(gDragonValidation.stepIndex == 4)
				return DragonInjectWorldMapCellTap(21, 11);
			if(gDragonValidation.stepIndex == 5)
				return DragonInjectWorldMapCellTap(23, 7);
			if(gDragonValidation.stepIndex == 6)
				return DragonInjectWorldMapCellTap(74, 12);
			if(gDragonValidation.stepIndex == 7) {
				DragonInjectWorldMapActionTap();
				return true;
			}
			if(gDragonValidation.stepIndex == 8)
				return true;
				if(gDragonValidation.stepIndex == 9) {
					if(DragonFindReportedTextContaining("CONTINUE"))
						return DragonInjectReportedTextBottomTapContaining("CONTINUE");
					return true;
				}
			}
				if(DragonCaseIs("Validation_WorldMap_ActMissNoAdjacentInteract")) {
					if(gDragonValidation.stepIndex == 0)
						return true;
					if(gDragonValidation.stepIndex == 1)
						return DragonInjectReportedTextTapContaining("Player tile:");
				}
				if(DragonCaseIs("Validation_WorldMap_ActMissThrottleRepeat")) {
					if(gDragonValidation.stepIndex == 0)
						return true;
					if(gDragonValidation.stepIndex == 1)
						return DragonInjectReportedTextTapContaining("Player tile:");
					if(gDragonValidation.stepIndex == 2)
						return DragonInjectReportedTextTapContaining("Player tile:");
				}
			if(DragonCaseIs("Validation_WorldMap_FrontDoorShopNpcActSouth")
				|| DragonCaseIs("Validation_WorldMap_FrontDoorShopNpcActNorth")
				|| DragonCaseIs("Validation_WorldMap_FrontDoorShopNpcActEast")
				|| DragonCaseIs("Validation_WorldMap_FrontDoorShopNpcActWest")) {
				if(gDragonValidation.stepIndex == 0) {
					DragonInjectSplashNewGameTap();
					return true;
				}
				if(gDragonValidation.stepIndex == 1) {
					DragonInjectNewGameSlotTap(0);
					return true;
				}
				if(gDragonValidation.stepIndex == 2) {
					DragonInjectNewAvatarBeginGameTap();
					return true;
				}
				if(gDragonValidation.stepIndex == 3)
					return DragonInjectWorldMapCellTap(25, 16);
				if(gDragonValidation.stepIndex == 4)
					return DragonInjectWorldMapCellTap(21, 11);
				if(gDragonValidation.stepIndex == 5)
					return DragonInjectWorldMapCellTap(23, 7);
				if(gDragonValidation.stepIndex == 6)
					return DragonInjectWorldMapCellTap(74, 12);
				if(gDragonValidation.stepIndex == 7) {
					if(DragonCaseIs("Validation_WorldMap_FrontDoorShopNpcActSouth")) {
						DragonInjectWorldMapActionTap();
						return true;
					}
					if(DragonCaseIs("Validation_WorldMap_FrontDoorShopNpcActNorth"))
						return DragonInjectWorldMapCellTap(74, 10);
					if(DragonCaseIs("Validation_WorldMap_FrontDoorShopNpcActEast"))
						return DragonInjectWorldMapCellTap(75, 11);
					if(DragonCaseIs("Validation_WorldMap_FrontDoorShopNpcActWest"))
						return DragonInjectWorldMapCellTap(73, 11);
					return true;
				}
				if(gDragonValidation.stepIndex == 8) {
					if(DragonFindReportedTextContaining("CONTINUE"))
						return DragonInjectReportedTextBottomTapContaining("CONTINUE");
					return true;
				}
				if(gDragonValidation.stepIndex == 9) {
					return true;
				}
			}
				if(DragonCaseIs("Validation_WorldMap_FrontDoorFacingPriority")) {
				if(gDragonValidation.stepIndex == 0) {
					DragonInjectSplashNewGameTap();
					return true;
				}
				if(gDragonValidation.stepIndex == 1) {
					DragonInjectNewGameSlotTap(0);
					return true;
				}
				if(gDragonValidation.stepIndex == 2) {
					DragonInjectNewAvatarBeginGameTap();
					return true;
				}
				if(gDragonValidation.stepIndex == 3)
					return DragonInjectWorldMapCellTap(25, 16);
				if(gDragonValidation.stepIndex == 4)
					return DragonInjectWorldMapCellTap(21, 11);
				if(gDragonValidation.stepIndex == 5)
					return DragonInjectWorldMapCellTap(23, 7);
				if(gDragonValidation.stepIndex == 6)
					return DragonInjectWorldMapCellTap(74, 12);
					if(gDragonValidation.stepIndex == 7) {
						DragonInjectWorldMapActionTap();
						return true;
					}
				}
				if(DragonCaseIs("Validation_WorldMap_FrontDoorFacingPriorityRetarget")) {
					if(gDragonValidation.stepIndex == 0) {
						DragonInjectSplashNewGameTap();
						return true;
					}
					if(gDragonValidation.stepIndex == 1) {
						DragonInjectNewGameSlotTap(0);
						return true;
					}
					if(gDragonValidation.stepIndex == 2) {
						DragonInjectNewAvatarBeginGameTap();
						return true;
					}
					if(gDragonValidation.stepIndex == 3)
						return DragonInjectWorldMapCellTap(25, 16);
					if(gDragonValidation.stepIndex == 4)
						return DragonInjectWorldMapCellTap(21, 11);
					if(gDragonValidation.stepIndex == 5)
						return DragonInjectWorldMapCellTap(23, 7);
					if(gDragonValidation.stepIndex == 6)
						return DragonInjectWorldMapCellTap(74, 12);
					if(gDragonValidation.stepIndex == 7)
						return DragonInjectWorldMapCellTap(74, 10);
					if(gDragonValidation.stepIndex == 8) {
						DragonInjectWorldMapActionTap();
						return true;
					}
				}
				if(DragonCaseIs("Validation_WorldMap_FrontDoorRouteCancel")) {
			if(gDragonValidation.stepIndex == 0) {
				DragonInjectSplashNewGameTap();
				return true;
			}
			if(gDragonValidation.stepIndex == 1) {
				DragonInjectNewGameSlotTap(0);
				return true;
			}
				if(gDragonValidation.stepIndex == 2) {
					DragonInjectNewAvatarBeginGameTap();
					return true;
				}
						if(gDragonValidation.stepIndex == 3)
							return DragonInjectWorldMapEventTap(0);
						if(gDragonValidation.stepIndex == 4)
							return DragonInjectReportedTextTapContaining("Player tile:");
					}
				if(DragonCaseIs("Validation_WorldMap_RouteTargetRetapThrottle")) {
					if(gDragonValidation.stepIndex == 0) {
						DragonInjectSplashNewGameTap();
						return true;
					}
					if(gDragonValidation.stepIndex == 1) {
						DragonInjectNewGameSlotTap(0);
						return true;
					}
					if(gDragonValidation.stepIndex == 2) {
						DragonInjectNewAvatarBeginGameTap();
						return true;
					}
					if(gDragonValidation.stepIndex == 3)
						return DragonInjectWorldMapCellTap(25, 16);
					if(gDragonValidation.stepIndex == 4)
						return DragonInjectWorldMapCellTap(21, 11);
					if(gDragonValidation.stepIndex == 5)
						return DragonInjectWorldMapCellTap(23, 7);
					if(gDragonValidation.stepIndex == 6)
						return DragonInjectWorldMapCellTap(74, 12);
					if(gDragonValidation.stepIndex == 7)
						return DragonInjectWorldMapCellTap(74, 10);
					if(gDragonValidation.stepIndex == 8)
						return DragonInjectWorldMapCellTap(74, 10);
				}
				if(DragonCaseIs("Validation_WorldMap_RouteCancelImmediateAct")) {
					if(gDragonValidation.stepIndex == 0)
						return true;
				if(gDragonValidation.stepIndex == 1)
					return DragonInjectWorldMapCellTap(30, 23);
				if(gDragonValidation.stepIndex == 2)
					return DragonInjectReportedTextTapContaining("Player tile:");
					if(gDragonValidation.stepIndex == 3)
						return DragonInjectReportedTextTapContaining("Player tile:");
				}
				if(DragonCaseIs("Validation_WorldMap_RouteCancelDirectionalRetarget")) {
					if(gDragonValidation.stepIndex == 0)
						return true;
					if(gDragonValidation.stepIndex == 1)
						return DragonInjectWorldMapCellTap(25, 18);
					if(gDragonValidation.stepIndex == 2)
						return DragonInjectReportedTextTapContaining("Player tile:");
					if(gDragonValidation.stepIndex == 3)
						return DragonInjectWorldMapCellTap(30, 23);
					if(gDragonValidation.stepIndex == 4)
						return DragonInjectReportedTextTapContaining("Player tile:");
					if(gDragonValidation.stepIndex == 5)
						return DragonInjectWorldMapCellTap(24, 23);
					if(gDragonValidation.stepIndex == 6)
						return DragonInjectReportedTextTapContaining("Player tile:");
				}
			if(DragonCaseIs("Validation_WorldMap_ActionTalk")) {
			if(gDragonValidation.stepIndex == 0)
				return true;
			if(gDragonValidation.stepIndex == 1)
				return DragonInjectWorldMapEventTap(0);
			if(gDragonValidation.stepIndex == 2)
				return DragonInjectReportedTextBottomTapContaining("CONTINUE");
		}
		if(DragonCaseIs("Validation_WorldMap_HealEvent")) {
			if(gDragonValidation.stepIndex == 0)
				return true;
			if(gDragonValidation.stepIndex == 1) {
				DragonInjectWorldMapActionTap();
				return true;
			}
		}
		if(DragonCaseIs("Validation_WorldMap_TrainEvent")) {
			if(gDragonValidation.stepIndex == 0)
				return true;
			if(gDragonValidation.stepIndex == 1) {
				DragonInjectWorldMapActionTap();
				return true;
			}
			if(gDragonValidation.stepIndex == 2)
				return DragonInjectReportedTextTapContaining("YES");
		}
		if(DragonCaseIs("Validation_WorldMap_TrainStatusGrowth")) {
			if(gDragonValidation.stepIndex == 0)
				return true;
			if(gDragonValidation.stepIndex == 1) {
				DragonInjectWorldMapActionTap();
				return true;
			}
			if(gDragonValidation.stepIndex == 2)
				return DragonInjectReportedTextTapContaining("YES");
			if(gDragonValidation.stepIndex == 3) {
				DragonInjectWorldMapStatusTap();
				return true;
			}
		}
		if(DragonCaseIs("Validation_WorldMap_TrainDecline")) {
			if(gDragonValidation.stepIndex == 0)
				return true;
			if(gDragonValidation.stepIndex == 1) {
				DragonInjectWorldMapActionTap();
				return true;
			}
			if(gDragonValidation.stepIndex == 2)
				return DragonInjectReportedTextTapContaining("NO");
		}
		if(DragonCaseIs("Validation_WorldMap_TrainToShopSequence")) {
			if(gDragonValidation.stepIndex == 0)
				return true;
			if(gDragonValidation.stepIndex == 1) {
				DragonInjectWorldMapActionTap();
				return true;
			}
			if(gDragonValidation.stepIndex == 2)
				return DragonInjectReportedTextTapContaining("YES");
			if(gDragonValidation.stepIndex == 3)
				return DragonInjectWorldMapCellTap(12, 17);
			if(gDragonValidation.stepIndex == 4)
				return DragonInjectWorldMapCellTap(12, 19);
			if(gDragonValidation.stepIndex == 5)
				return true;
		}
		if(DragonCaseIs("Validation_WorldMap_ChallengeEvent")) {
			if(gDragonValidation.stepIndex == 0)
				return true;
			if(gDragonValidation.stepIndex == 1)
				return DragonInjectWorldMapEventTap(3);
			if(gDragonValidation.stepIndex == 2)
				return DragonInjectReportedTextTapContaining("YES");
			if(gDragonValidation.stepIndex == 3)
				return DragonInjectBattleCommandTap(0);
		}
		if(DragonCaseIs("Validation_WorldMap_ChallengeDecline")) {
			if(gDragonValidation.stepIndex == 0)
				return true;
			if(gDragonValidation.stepIndex == 1)
				return DragonInjectWorldMapEventTap(3);
			if(gDragonValidation.stepIndex == 2)
				return DragonInjectReportedTextTapContaining("NO");
		}
		if(DragonCaseIs("Validation_WorldMap_ChallengeDefeatPanel")) {
			if(gDragonValidation.stepIndex == 0)
				return true;
			if(gDragonValidation.stepIndex == 1)
				return DragonInjectWorldMapEventTap(3);
			if(gDragonValidation.stepIndex == 2)
				return DragonInjectReportedTextTapContaining("YES");
			if(gDragonValidation.stepIndex == 3)
				return DragonInjectBattleCommandTap(0);
		}
		if(DragonCaseIs("Validation_WorldMap_ChallengeVictoryProgression")) {
			if(gDragonValidation.stepIndex == 0)
				return true;
			if(gDragonValidation.stepIndex == 1)
				return DragonInjectWorldMapEventTap(3);
			if(gDragonValidation.stepIndex == 2)
				return DragonInjectReportedTextTapContaining("YES");
			if(gDragonValidation.stepIndex == 3)
				return DragonInjectBattleCommandTap(0);
			if(gDragonValidation.stepIndex == 4)
				return DragonInjectReportedTextBottomTapContaining("CONTINUE");
		}
			if(DragonCaseIs("Validation_WorldMap_ShopEntry")) {
				if(gDragonValidation.stepIndex == 0)
					return true;
				if(gDragonValidation.stepIndex == 1)
					return DragonInjectWorldMapEventTap(3);
				if(gDragonValidation.stepIndex == 2) {
					if(DragonFindReportedTextContaining("YES"))
						return DragonInjectReportedTextTapContaining("YES");
					return true;
				}
			}
		if(DragonCaseIs("Validation_WorldMap_WarpTransition")) {
			if(gDragonValidation.stepIndex == 0)
				return true;
			if(gDragonValidation.stepIndex == 1)
				return DragonInjectWorldMapCellTap(12, 19);
		}
			if(DragonCaseIs("Validation_WorldMap_AutoPathTreasure")) {
				if(gDragonValidation.stepIndex == 0)
					return true;
				if(gDragonValidation.stepIndex == 1)
					return DragonInjectWorldMapEventTapByType("Treasure");
			}
			if(DragonCaseIs("Validation_WorldMap_CurrentTileRouteCancel")) {
				if(gDragonValidation.stepIndex == 0)
					return true;
				if(gDragonValidation.stepIndex == 1)
					return DragonInjectWorldMapEventTapByType("Treasure");
				if(gDragonValidation.stepIndex == 2)
					return DragonInjectReportedTextTapContaining("Player tile:");
			}
		if(DragonCaseIs("Validation_WorldMap_GateRouteMagnet")) {
			if(gDragonValidation.stepIndex == 0)
				return true;
			if(gDragonValidation.stepIndex == 1)
				return DragonInjectWorldMapCellTap(63, 17);
		}
		if(DragonCaseIs("Validation_WorldMap_GateBattleEntry")) {
			if(gDragonValidation.stepIndex == 0)
				return true;
			if(gDragonValidation.stepIndex == 1)
				return DragonInjectWorldMapCellTap(41, 47);
			if(gDragonValidation.stepIndex == 2)
				return true;
			if(gDragonValidation.stepIndex == 3)
				return DragonInjectBattleCommandTap(0);
		}
		if(DragonCaseIs("Validation_WorldMap_GateBattleProgression")) {
			if(gDragonValidation.stepIndex == 0)
				return true;
			if(gDragonValidation.stepIndex == 1)
				return DragonInjectWorldMapCellTap(41, 47);
			if(gDragonValidation.stepIndex == 2)
				return true;
			if(gDragonValidation.stepIndex == 3)
				return DragonInjectBattleCommandTap(0);
			if(gDragonValidation.stepIndex == 4)
				return DragonInjectReportedTextBottomTapContaining("CONTINUE");
		}
		if(DragonCaseIs("Validation_WorldMap_RandomBattleEntry")) {
			if(gDragonValidation.stepIndex == 0)
				return true;
			if(gDragonValidation.stepIndex == 1)
				return DragonInjectWorldMapCellTap(31, 18);
		}
		if(DragonCaseIs("Validation_WorldMap_RandomBattleRetreat")) {
			if(gDragonValidation.stepIndex == 0)
				return true;
			if(gDragonValidation.stepIndex == 1)
				return DragonInjectWorldMapCellTap(31, 18);
			if(gDragonValidation.stepIndex == 2)
				return DragonInjectBattleCommandTap(4);
			if(gDragonValidation.stepIndex == 3)
				return DragonInjectReportedTextBottomTapContaining("CONTINUE");
		}
		if(DragonCaseIs("Validation_Status_Default")) {
			if(gDragonValidation.stepIndex == 0) {
				DragonInjectSplashNewGameTap();
				return true;
		}
		if(gDragonValidation.stepIndex == 1) {
			DragonInjectNewGameSlotTap(0);
			return true;
		}
		if(gDragonValidation.stepIndex == 2) {
			DragonInjectNewAvatarBeginGameTap();
			return true;
		}
		if(gDragonValidation.stepIndex == 3) {
			DragonInjectWorldMapStatusTap();
			return true;
		}
	}
	if(DragonCaseIs("Validation_Status_UsePotion")) {
		if(gDragonValidation.stepIndex == 0)
			return true;
		if(gDragonValidation.stepIndex == 1)
			return DragonInjectReportedTextTapContaining("USE");
	}
	if(DragonCaseIs("Validation_Status_SellScroll")) {
		if(gDragonValidation.stepIndex == 0)
			return true;
		if(gDragonValidation.stepIndex == 1)
			return DragonInjectReportedTextTapContaining("Fire Scroll");
		if(gDragonValidation.stepIndex == 2)
			return DragonInjectReportedTextTapContaining("SELL");
	}
	if(DragonCaseIs("Validation_Status_UnequipWeapon")) {
		if(gDragonValidation.stepIndex == 0)
			return true;
		if(gDragonValidation.stepIndex == 1)
			return DragonInjectReportedTextTapContaining("Weapon: Iron Blade");
	}
	if(DragonCaseIs("Validation_Status_UnequipArmor")) {
		if(gDragonValidation.stepIndex == 0)
			return true;
		if(gDragonValidation.stepIndex == 1)
		{
			DragonInjectStatusArmorTap();
			return true;
		}
	}
	if(DragonCaseIs("Validation_Status_UnequipRelic")) {
		if(gDragonValidation.stepIndex == 0)
			return true;
		if(gDragonValidation.stepIndex == 1)
		{
			DragonInjectStatusRelicTap();
			return true;
		}
	}
	if(DragonCaseIs("Validation_Status_EquipWeapon")) {
		if(gDragonValidation.stepIndex == 0)
			return true;
		if(gDragonValidation.stepIndex == 1)
		{
			DragonInjectStatusActionTap();
			return true;
		}
	}
	if(DragonCaseIs("Validation_Status_EquipArmor")) {
		if(gDragonValidation.stepIndex == 0)
			return true;
		if(gDragonValidation.stepIndex == 1)
		{
			DragonInjectStatusActionTap();
			return true;
		}
	}
	if(DragonCaseIs("Validation_Status_EquipRelic")) {
		if(gDragonValidation.stepIndex == 0)
			return true;
		if(gDragonValidation.stepIndex == 1)
		{
			DragonInjectStatusActionTap();
			return true;
		}
	}
	if(DragonCaseIs("Validation_Status_SellEquippedWeapon")) {
		if(gDragonValidation.stepIndex == 0)
			return true;
		if(gDragonValidation.stepIndex == 1)
			return DragonInjectReportedTextTapContaining("SELL");
	}
	if(DragonCaseIs("Validation_Status_UnequipWeaponInventoryFull")) {
		if(gDragonValidation.stepIndex == 0)
			return true;
		if(gDragonValidation.stepIndex == 1)
			return DragonInjectReportedTextTapContaining("Weapon: Iron Blade");
	}
	if(DragonCaseIs("Validation_Status_LargeInventoryPaging")) {
		if(gDragonValidation.stepIndex == 0)
			return true;
		if(gDragonValidation.stepIndex >= 1 && gDragonValidation.stepIndex <= 11)
			return DragonInjectReportedTextTapContaining("NEXT");
	}
	if(DragonCaseIs("Validation_SaveLoad_RoundTrip")) {
		if(gDragonValidation.stepIndex == 0) {
			DragonInjectSplashNewGameTap();
			return true;
		}
		if(gDragonValidation.stepIndex == 1) {
			DragonInjectNewGameSlotTap(0);
			return true;
		}
		if(gDragonValidation.stepIndex == 2) {
			DragonInjectNewAvatarBeginGameTap();
			return true;
		}
		if(gDragonValidation.stepIndex == 3) {
			DragonInjectWorldMapSaveTap();
			return true;
		}
		if(gDragonValidation.stepIndex == 4) {
			DragonInjectWorldMapMenuTap();
			return true;
		}
		if(gDragonValidation.stepIndex == 5) {
			DragonInjectNewGameSlotTap(0);
			return true;
		}
		if(gDragonValidation.stepIndex == 6) {
			DragonInjectWorldMapStatusTap();
			return true;
		}
	}
	if(DragonCaseIs("Validation_SaveLoad_ProgressionState")) {
		if(gDragonValidation.stepIndex == 0)
			return true;
		if(gDragonValidation.stepIndex == 1) {
			DragonInjectWorldMapSaveTap();
			return true;
		}
		if(gDragonValidation.stepIndex == 2) {
			DragonInjectWorldMapMenuTap();
			return true;
		}
		if(gDragonValidation.stepIndex == 3) {
			DragonInjectNewGameSlotTap(0);
			return true;
		}
	}
	if(DragonCaseIs("Validation_SaveLoad_LaterGameLoadoutState")) {
		if(gDragonValidation.stepIndex == 0)
			return true;
		if(gDragonValidation.stepIndex == 1) {
			DragonInjectWorldMapSaveTap();
			return true;
		}
		if(gDragonValidation.stepIndex == 2) {
			DragonInjectWorldMapMenuTap();
			return true;
		}
		if(gDragonValidation.stepIndex == 3) {
			DragonInjectNewGameSlotTap(0);
			return true;
		}
		if(gDragonValidation.stepIndex == 4) {
			DragonInjectWorldMapStatusTap();
			return true;
		}
	}
		if(DragonCaseIs("Validation_Shop_Default")) {
			if(gDragonValidation.stepIndex == 0)
				return true;
			if(gDragonValidation.stepIndex == 1)
				return DragonInjectReportedTextTapContaining("NEXT");
		}
		if(DragonCaseIs("Validation_Shop_ReturnToWorldMap")) {
			if(gDragonValidation.stepIndex == 0)
				return true;
			if(gDragonValidation.stepIndex == 1)
				return DragonInjectReportedTextTapContaining("NEXT");
			if(gDragonValidation.stepIndex == 2)
				return DragonInjectReportedTextTapContaining("DONE");
		}
		if(DragonCaseIs("Validation_Shop_Purchase")) {
			if(gDragonValidation.stepIndex == 0)
				return true;
			if(gDragonValidation.stepIndex == 1)
				return DragonInjectReportedTextTapContaining("NEXT");
			if(gDragonValidation.stepIndex == 2)
				return DragonInjectReportedTextTapContaining("Hunter Bow");
		}
		if(DragonCaseIs("Validation_Shop_InsufficientGold")) {
			if(gDragonValidation.stepIndex == 0)
				return true;
			if(gDragonValidation.stepIndex == 1)
				return DragonInjectReportedTextTapContaining("NEXT");
			if(gDragonValidation.stepIndex == 2)
				return DragonInjectReportedTextTapContaining("Hunter Bow");
		}
		if(DragonCaseIs("Validation_Shop_DuplicateOwned")) {
			if(gDragonValidation.stepIndex == 0)
				return true;
			if(gDragonValidation.stepIndex == 1)
				return DragonInjectReportedTextTapContaining("NEXT");
			if(gDragonValidation.stepIndex == 2)
				return DragonInjectReportedTextTapContaining("Hunter Bow");
		}
		if(DragonCaseIs("Validation_Shop_InventoryFull")) {
			if(gDragonValidation.stepIndex == 0)
				return true;
			if(gDragonValidation.stepIndex == 1)
				return DragonInjectReportedTextTapContaining("NEXT");
			if(gDragonValidation.stepIndex == 2)
				return DragonInjectReportedTextTapContaining("Hunter Bow");
		}
		if(DragonCaseIs("Validation_Shop_MultiPurchase")) {
			if(gDragonValidation.stepIndex == 0)
				return true;
			if(gDragonValidation.stepIndex == 1)
				return DragonInjectReportedTextTapContaining("Traveler Cloak");
			if(gDragonValidation.stepIndex == 2)
				return DragonInjectReportedTextTapContaining("Scout Charm");
		}
	if(DragonCaseIs("Validation_Battle_TargetSelection")) {
		if(gDragonValidation.stepIndex == 0)
			return true;
		if(gDragonValidation.stepIndex == 1 || gDragonValidation.stepIndex == 2)
			return DragonInjectReportedTextTapContaining("Enemy target 2:");
	}
		if(DragonCaseIs("Validation_Battle_CommandResolution")) {
			if(gDragonValidation.stepIndex == 0)
				return true;
			if(gDragonValidation.stepIndex == 1)
				return DragonInjectBattleCommandTap(0);
			if(gDragonValidation.stepIndex == 2)
				return DragonInjectReportedTextTapContaining("Enemy target 2:");
		}
		if(DragonCaseIs("Validation_Battle_SeededOutcome")) {
			if(gDragonValidation.stepIndex == 0)
				return true;
			if(gDragonValidation.stepIndex == 1)
				return DragonInjectBattleCommandTap(0);
			if(gDragonValidation.stepIndex == 2)
				return DragonInjectReportedTextTapContaining("Enemy target 2:");
		}
		if(DragonCaseIs("Validation_Battle_MultiEnemyContinue")) {
			if(gDragonValidation.stepIndex == 0)
				return true;
			if(gDragonValidation.stepIndex == 1)
				return DragonInjectBattleCommandTap(0);
			if(gDragonValidation.stepIndex == 2)
				return DragonInjectReportedTextTapContaining("Enemy target 0:");
		}
	if(DragonCaseIs("Validation_Battle_EnemySpecialResponse")) {
		if(gDragonValidation.stepIndex == 0)
			return true;
		if(gDragonValidation.stepIndex == 1)
			return DragonInjectBattleCommandTap(1);
	}
	if(DragonCaseIs("Validation_Battle_EnemyHealResponse")) {
		if(gDragonValidation.stepIndex == 0)
			return true;
		if(gDragonValidation.stepIndex == 1)
			return DragonInjectBattleCommandTap(1);
	}
	if(DragonCaseIs("Validation_Battle_NoDamageResponse")) {
		if(gDragonValidation.stepIndex == 0)
			return true;
		if(gDragonValidation.stepIndex == 1)
			return DragonInjectBattleCommandTap(1);
	}
	if(DragonCaseIs("Validation_Battle_VictoryPanel")) {
		if(gDragonValidation.stepIndex == 0)
			return true;
		if(gDragonValidation.stepIndex == 1)
			return DragonInjectBattleCommandTap(0);
	}
	if(DragonCaseIs("Validation_Battle_Magic_SeededOutcome")) {
		if(gDragonValidation.stepIndex == 0)
			return true;
		if(gDragonValidation.stepIndex == 1)
			return DragonInjectBattleCommandTap(2);
		if(gDragonValidation.stepIndex == 2)
			return DragonInjectReportedTextTapContaining("Cure");
	}
	if(DragonCaseIs("Validation_Battle_Tech_SeededOutcome")) {
		if(gDragonValidation.stepIndex == 0)
			return true;
		if(gDragonValidation.stepIndex == 1)
			return DragonInjectBattleCommandTap(3);
		if(gDragonValidation.stepIndex == 2)
			return DragonInjectReportedTextTapContaining("Rally");
	}
	if(DragonCaseIs("Validation_Battle_ElementalNeutralOutcome")) {
		if(gDragonValidation.stepIndex == 0)
			return true;
		if(gDragonValidation.stepIndex == 1)
			return DragonInjectBattleCommandTap(3);
		if(gDragonValidation.stepIndex == 2)
			return DragonInjectReportedTextTapContaining("Pin Shot");
	}
	if(DragonCaseIs("Validation_Battle_ElementalResistOutcome")) {
		if(gDragonValidation.stepIndex == 0)
			return true;
		if(gDragonValidation.stepIndex == 1)
			return DragonInjectBattleCommandTap(3);
		if(gDragonValidation.stepIndex == 2)
			return DragonInjectReportedTextTapContaining("Pin Shot");
	}
	return false;
}

static bool DragonHandleValidatedCaseStep () {
	if(DragonCaseIs("Validation_SaveLoad_RoundTrip") && gDragonValidation.stepIndex == 2)
		return DragonCaptureRoundTripWorldMapState();
	return true;
}

static void DragonWriteValidationReport (bool success) {
	if(gDragonValidation.reportPath.empty())
		return;
	if(DragonEnsureDirectoryExists(gDragonValidation.outputDir) == false)
		return;

	FILE* file = std::fopen(gDragonValidation.reportPath.c_str(), "wb");
	if(file == nullptr)
		return;

	const ERect screenRect = ESystem::GetScreenRect();
	const ERect safeRect = ESystem::GetSafeRect();
	const ERect designRect = ESystem::GetDesignRect();
	std::fprintf(file, "{\n");
	std::fprintf(file, "  \"case\": \"%s\",\n", DragonEscapeJSON(gDragonValidation.caseId).c_str());
	std::fprintf(file, "  \"entryMode\": \"%s\",\n", DragonEscapeJSON(gDragonValidation.entryMode).c_str());
	std::fprintf(file, "  \"scene\": \"%s\",\n", DragonEscapeJSON(gDragonValidation.sceneName).c_str());
	std::fprintf(file, "  \"validationMode\": \"%s\",\n", DragonEscapeJSON(gDragonValidation.validationMode).c_str());
	std::fprintf(file, "  \"platform\": \"macOS\",\n");
	std::fprintf(file, "  \"outputDir\": \"%s\",\n", DragonEscapeJSON(gDragonValidation.outputDir).c_str());
	std::fprintf(file, "  \"capture\": \"%s\",\n", DragonEscapeJSON(gDragonValidation.capturePath).c_str());
	std::fprintf(file, "  \"log\": \"%s\",\n", DragonEscapeJSON(gDragonValidation.logPath).c_str());
	std::fprintf(file, "  \"saveDir\": \"%s\",\n", DragonEscapeJSON(gDragonValidation.saveDir).c_str());
	std::fprintf(file, "  \"captureAvailable\": %s,\n", ESystem::CanCaptureFramePNG() ? "true" : "false");
	std::fprintf(file, "  \"width\": %d,\n", gDragonValidation.captureWidth);
	std::fprintf(file, "  \"height\": %d,\n", gDragonValidation.captureHeight);
	DragonWriteRectJSON(file, "screenRect", screenRect, true);
	DragonWriteRectJSON(file, "safeRect", safeRect, true);
	DragonWriteRectJSON(file, "designRect", designRect, true);
	std::fprintf(file, "  \"frames\": %d,\n", gDragonValidation.frameCount);
	std::fprintf(file, "  \"frameRendered\": %d,\n", gDragonValidation.captureFrame > 0 ? gDragonValidation.captureFrame : gDragonValidation.frameCount);
	std::fprintf(file, "  \"stepCount\": %d,\n", gDragonValidation.stepCount);
	std::fprintf(file, "  \"stepsCompleted\": %d,\n", gDragonValidation.stepIndex + (gDragonValidation.stepTelemetryValidated ? 1 : 0));
	std::fprintf(file, "  \"reportedTextDraws\": %d,\n", gDragonValidation.reportedTextDrawCount);
	std::fprintf(file, "  \"matchedTextDraws\": %d,\n", gDragonValidation.matchedTextDrawCount);
	DragonWriteSampledTextDrawsJSON(file, 64);
	std::fprintf(file, "  \"status\": \"%s\"", success ? "ok" : "error");
	if(success == false && gDragonValidation.error.empty() == false)
		std::fprintf(file, ",\n  \"error\": \"%s\"\n", DragonEscapeJSON(gDragonValidation.error).c_str());
	else
		std::fprintf(file, "\n");
	std::fprintf(file, "}\n");
	std::fclose(file);
}

static void DragonValidationCaptureComplete (bool success, int width, int height, const EString& path, const EString& error) {
	gDragonValidation.captureCompleted = true;
	gDragonValidation.captureSucceeded = success;
	gDragonValidation.validationMode = "png-capture";
	gDragonValidation.captureWidth = width;
	gDragonValidation.captureHeight = height;
	if(path.IsEmpty() == false)
		gDragonValidation.capturePath = path.String();
	if(success == false) {
		gDragonValidation.error = error.String();
		if(gDragonValidation.error.empty())
			gDragonValidation.error = "Frame capture failed.";
	}
	DragonWriteValidationReport(success);
	ESystem::RequestExit(success ? EXIT_SUCCESS : EXIT_FAILURE);
}

static void DragonValidationPassWithTelemetry () {
	gDragonValidation.captureCompleted = true;
	gDragonValidation.captureSucceeded = true;
	gDragonValidation.validationMode = "text-telemetry";
	DragonWriteValidationReport(true);
	ESystem::RequestExit(EXIT_SUCCESS);
}

static void DragonValidationFail (const char* message) {
	gDragonValidation.captureCompleted = true;
	gDragonValidation.captureSucceeded = false;
	gDragonValidation.error = message != nullptr ? message : "Validation failed.";
	DragonWriteValidationReport(false);
	ESystem::RequestExit(EXIT_FAILURE);
}

static void DragonValidationOnDraw () {
	if(gDragonValidation.active == false)
		return;

	gDragonValidation.frameCount++;
	if(gDragonValidation.firstDrawMilliseconds == 0)
		gDragonValidation.firstDrawMilliseconds = ESystem::GetMilliseconds();
	if(gDragonValidation.stepStartedMilliseconds == 0)
		gDragonValidation.stepStartedMilliseconds = gDragonValidation.firstDrawMilliseconds;
	if(gDragonValidation.captureCompleted)
		return;

	const int64_t elapsed = ESystem::GetMilliseconds() - gDragonValidation.firstDrawMilliseconds;
	const int64_t stepElapsed = ESystem::GetMilliseconds() - gDragonValidation.stepStartedMilliseconds;
	if(gDragonValidation.stepInputInjected == false) {
		if(stepElapsed < gDragonValidation.interactionDelayMs)
			return;

		if(DragonInjectCurrentCaseInput() == false) {
			DragonValidationFail(EString().Format(
				"Failed to inject scripted input for %s step %d.",
				gDragonValidation.caseId.c_str(),
				gDragonValidation.stepIndex + 1
			));
			return;
		}
		gDragonValidation.stepInputInjected = true;
		ESystem::ClearReportedTextDraws();
		return;
	}

	if(gDragonValidation.stepTelemetryValidated == false) {
		gDragonValidation.stepTelemetryValidated = DragonValidateCurrentCaseTelemetry();
		if(gDragonValidation.stepTelemetryValidated == false) {
			if(stepElapsed > gDragonValidation.timeoutMs) {
				DragonValidationFail(EString().Format(
					"Timed out waiting for %s step %d telemetry after scripted input.",
					gDragonValidation.caseId.c_str(),
					gDragonValidation.stepIndex + 1
				));
			}
			return;
		}
		if(DragonHandleValidatedCaseStep() == false) {
			DragonValidationFail("Validation step passed, but round-trip state capture failed.");
			return;
		}

		if(gDragonValidation.stepIndex + 1 < gDragonValidation.stepCount) {
			gDragonValidation.stepIndex++;
			gDragonValidation.stepInputInjected = false;
			gDragonValidation.stepTelemetryValidated = false;
			gDragonValidation.stepStartedMilliseconds = ESystem::GetMilliseconds();
			ESystem::ClearReportedTextDraws();
			return;
		}
	}

	if(gDragonValidation.captureRequested == false) {
		if(ESystem::CanCaptureFramePNG() == false) {
			DragonValidationPassWithTelemetry();
			return;
		}

		gDragonValidation.captureRequested = true;
		gDragonValidation.captureFrame = gDragonValidation.frameCount;
		gDragonValidation.validationMode = "png-capture";
		if(ESystem::RequestFrameCapturePNG(gDragonValidation.capturePath.c_str(), DragonValidationCaptureComplete) == false) {
			DragonValidationFail("Frame capture request was not accepted by the runtime.");
			return;
		}
		return;
	}

	if(elapsed > gDragonValidation.timeoutMs)
		DragonValidationFail(EString().Format(
			"Timed out waiting for %s to capture.",
			gDragonValidation.caseId.c_str()
		));
}

static void DragonValidationOnPreRun () {
	if(DragonHasArg("--automation") == false)
		return;

	const char* caseId = DragonFindArgValue("--case-id");
	const char* outputDir = DragonFindArgValue("--output-dir");
	if(caseId == nullptr || caseId[0] == '\0' || outputDir == nullptr || outputDir[0] == '\0') {
		ESystem::Print("[DRAGON_TEST] Missing required --case-id or --output-dir.\n");
		ESystem::RequestExit(EXIT_FAILURE);
		return;
	}
			if(std::strcmp(caseId, "Validation_Splash_Default") != 0
				&& std::strcmp(caseId, "Validation_NewGame_Default") != 0
				&& std::strcmp(caseId, "Validation_NewGame_FilledSlots") != 0
				&& std::strcmp(caseId, "Validation_NewGame_DeleteConfirm") != 0
				&& std::strcmp(caseId, "Validation_NewGame_CorruptRecovery") != 0
				&& std::strcmp(caseId, "Validation_Splash_OpenGame_LoadSlot") != 0
				&& std::strcmp(caseId, "Validation_NewAvatar_Default") != 0
			&& std::strcmp(caseId, "Validation_NewAvatar_MageStatus") != 0
			&& std::strcmp(caseId, "Validation_NewAvatar_ThiefStatus") != 0
			&& std::strcmp(caseId, "Validation_WorldMap_Default") != 0
				&& std::strcmp(caseId, "Validation_WorldMap_MagicButton") != 0
				&& std::strcmp(caseId, "Validation_WorldMap_TechButton") != 0
				&& std::strcmp(caseId, "Validation_WorldMap_FrontDoorTalkRoute") != 0
					&& std::strcmp(caseId, "Validation_WorldMap_FrontDoorShopNpcTalk") != 0
					&& std::strcmp(caseId, "Validation_WorldMap_ActMissNoAdjacentInteract") != 0
					&& std::strcmp(caseId, "Validation_WorldMap_ActMissThrottleRepeat") != 0
					&& std::strcmp(caseId, "Validation_WorldMap_FrontDoorShopNpcActSouth") != 0
					&& std::strcmp(caseId, "Validation_WorldMap_FrontDoorShopNpcActNorth") != 0
					&& std::strcmp(caseId, "Validation_WorldMap_FrontDoorShopNpcActEast") != 0
					&& std::strcmp(caseId, "Validation_WorldMap_FrontDoorShopNpcActWest") != 0
					&& std::strcmp(caseId, "Validation_WorldMap_FrontDoorFacingPriority") != 0
					&& std::strcmp(caseId, "Validation_WorldMap_FrontDoorFacingPriorityRetarget") != 0
					&& std::strcmp(caseId, "Validation_WorldMap_FrontDoorRouteCancel") != 0
					&& std::strcmp(caseId, "Validation_WorldMap_RouteCancelImmediateAct") != 0
					&& std::strcmp(caseId, "Validation_WorldMap_RouteCancelDirectionalRetarget") != 0
					&& std::strcmp(caseId, "Validation_WorldMap_RouteTargetRetapThrottle") != 0
					&& std::strcmp(caseId, "Validation_WorldMap_ActionTalk") != 0
			&& std::strcmp(caseId, "Validation_WorldMap_HealEvent") != 0
			&& std::strcmp(caseId, "Validation_WorldMap_TrainEvent") != 0
			&& std::strcmp(caseId, "Validation_WorldMap_TrainStatusGrowth") != 0
			&& std::strcmp(caseId, "Validation_WorldMap_TrainDecline") != 0
			&& std::strcmp(caseId, "Validation_WorldMap_TrainToShopSequence") != 0
			&& std::strcmp(caseId, "Validation_WorldMap_ChallengeEvent") != 0
			&& std::strcmp(caseId, "Validation_WorldMap_ChallengeDecline") != 0
			&& std::strcmp(caseId, "Validation_WorldMap_ChallengeDefeatPanel") != 0
			&& std::strcmp(caseId, "Validation_WorldMap_ChallengeVictoryProgression") != 0
			&& std::strcmp(caseId, "Validation_WorldMap_ShopEntry") != 0
			&& std::strcmp(caseId, "Validation_WorldMap_WarpTransition") != 0
			&& std::strcmp(caseId, "Validation_WorldMap_AutoPathTreasure") != 0
			&& std::strcmp(caseId, "Validation_WorldMap_CurrentTileRouteCancel") != 0
			&& std::strcmp(caseId, "Validation_WorldMap_GateRouteMagnet") != 0
			&& std::strcmp(caseId, "Validation_WorldMap_GateBattleEntry") != 0
			&& std::strcmp(caseId, "Validation_WorldMap_GateBattleProgression") != 0
			&& std::strcmp(caseId, "Validation_WorldMap_RandomBattleEntry") != 0
			&& std::strcmp(caseId, "Validation_WorldMap_RandomBattleRetreat") != 0
			&& std::strcmp(caseId, "Validation_Status_Default") != 0
		&& std::strcmp(caseId, "Validation_Status_UsePotion") != 0
		&& std::strcmp(caseId, "Validation_Status_SellScroll") != 0
		&& std::strcmp(caseId, "Validation_Status_UnequipWeapon") != 0
		&& std::strcmp(caseId, "Validation_Status_UnequipArmor") != 0
		&& std::strcmp(caseId, "Validation_Status_UnequipRelic") != 0
		&& std::strcmp(caseId, "Validation_Status_EquipWeapon") != 0
		&& std::strcmp(caseId, "Validation_Status_EquipArmor") != 0
		&& std::strcmp(caseId, "Validation_Status_EquipRelic") != 0
		&& std::strcmp(caseId, "Validation_Status_SellEquippedWeapon") != 0
		&& std::strcmp(caseId, "Validation_Status_UnequipWeaponInventoryFull") != 0
		&& std::strcmp(caseId, "Validation_Status_LargeInventoryPaging") != 0
		&& std::strcmp(caseId, "Validation_SaveLoad_RoundTrip") != 0
		&& std::strcmp(caseId, "Validation_SaveLoad_ProgressionState") != 0
		&& std::strcmp(caseId, "Validation_SaveLoad_LaterGameLoadoutState") != 0
		&& std::strcmp(caseId, "Validation_Shop_Default") != 0
		&& std::strcmp(caseId, "Validation_Shop_ReturnToWorldMap") != 0
		&& std::strcmp(caseId, "Validation_Shop_Purchase") != 0
		&& std::strcmp(caseId, "Validation_Shop_InsufficientGold") != 0
		&& std::strcmp(caseId, "Validation_Shop_DuplicateOwned") != 0
		&& std::strcmp(caseId, "Validation_Shop_InventoryFull") != 0
		&& std::strcmp(caseId, "Validation_Shop_MultiPurchase") != 0
		&& std::strcmp(caseId, "Validation_Battle_TargetSelection") != 0
		&& std::strcmp(caseId, "Validation_Battle_CommandResolution") != 0
		&& std::strcmp(caseId, "Validation_Battle_SeededOutcome") != 0
		&& std::strcmp(caseId, "Validation_Battle_MultiEnemyContinue") != 0
		&& std::strcmp(caseId, "Validation_Battle_EnemySpecialResponse") != 0
		&& std::strcmp(caseId, "Validation_Battle_EnemyHealResponse") != 0
		&& std::strcmp(caseId, "Validation_Battle_NoDamageResponse") != 0
		&& std::strcmp(caseId, "Validation_Battle_VictoryPanel") != 0
		&& std::strcmp(caseId, "Validation_Battle_Magic_SeededOutcome") != 0
		&& std::strcmp(caseId, "Validation_Battle_Tech_SeededOutcome") != 0
		&& std::strcmp(caseId, "Validation_Battle_ElementalNeutralOutcome") != 0
		&& std::strcmp(caseId, "Validation_Battle_ElementalResistOutcome") != 0) {
		ESystem::Print("[DRAGON_TEST] Unsupported case-id: %s\n", caseId);
		ESystem::RequestExit(EXIT_FAILURE);
		return;
	}

	gDragonValidation = DragonValidationRun();
	gDragonValidation.active = true;
	gDragonValidation.caseId = caseId;
	gDragonValidation.outputDir = outputDir;
	gDragonValidation.capturePath = gDragonValidation.outputDir + "/capture.png";
	gDragonValidation.reportPath = gDragonValidation.outputDir + "/report.json";
	gDragonValidation.logPath = gDragonValidation.outputDir + "/engine.log";
	gDragonValidation.saveDir = gDragonValidation.outputDir + "/saves";
	gDragonValidation.sceneName.clear();
	gDragonValidation.stepCount = DragonGetCaseStepCount();
	gDragonValidation.captureDelayMs = DragonParsePositiveIntArg("--capture-delay-ms", 2600);
	gDragonValidation.interactionDelayMs = std::max(120, std::min(600, gDragonValidation.captureDelayMs / 4));
	gDragonValidation.timeoutMs = std::max(
		gDragonValidation.captureDelayMs + 2000 + std::max(0, gDragonValidation.stepCount - 1) * 1000,
		DragonParsePositiveIntArg("--timeout-ms", 7000)
	);
	if(DragonEnsureDirectoryExists(gDragonValidation.outputDir) == false || DragonEnsureDirectoryExists(gDragonValidation.saveDir) == false) {
		ESystem::Print("[DRAGON_TEST] Failed to create output directory: %s\n", gDragonValidation.outputDir.c_str());
		ESystem::RequestExit(EXIT_FAILURE);
		return;
	}

	ESystem::Print(
		"[DRAGON_TEST] case=%s output=%s delayMs=%d timeoutMs=%d\n",
		gDragonValidation.caseId.c_str(),
		gDragonValidation.outputDir.c_str(),
		gDragonValidation.captureDelayMs,
		gDragonValidation.timeoutMs
	);
				if(DragonCaseIs("Validation_NewGame_FilledSlots")
					|| DragonCaseIs("Validation_NewGame_DeleteConfirm")
					|| DragonCaseIs("Validation_NewGame_CorruptRecovery")
					|| DragonCaseIs("Validation_Splash_OpenGame_LoadSlot")
				|| DragonCaseIs("Validation_Shop_Default")
			|| DragonCaseIs("Validation_Shop_ReturnToWorldMap")
			|| DragonCaseIs("Validation_Shop_Purchase")
			|| DragonCaseIs("Validation_Shop_InsufficientGold")
			|| DragonCaseIs("Validation_Shop_DuplicateOwned")
			|| DragonCaseIs("Validation_Shop_InventoryFull")
			|| DragonCaseIs("Validation_Shop_MultiPurchase")
			|| DragonCaseIs("Validation_Battle_TargetSelection")
			|| DragonCaseIs("Validation_Battle_CommandResolution")
			|| DragonCaseIs("Validation_Battle_SeededOutcome")
			|| DragonCaseIs("Validation_Battle_MultiEnemyContinue")
			|| DragonCaseIs("Validation_Battle_EnemySpecialResponse")
			|| DragonCaseIs("Validation_Battle_EnemyHealResponse")
			|| DragonCaseIs("Validation_Battle_NoDamageResponse")
			|| DragonCaseIs("Validation_Battle_VictoryPanel")
			|| DragonCaseIs("Validation_Battle_Magic_SeededOutcome")
			|| DragonCaseIs("Validation_Battle_Tech_SeededOutcome")
			|| DragonCaseIs("Validation_Battle_ElementalNeutralOutcome")
			|| DragonCaseIs("Validation_Battle_ElementalResistOutcome")
			|| DragonCaseIs("Validation_Status_UsePotion")
			|| DragonCaseIs("Validation_Status_SellScroll")
			|| DragonCaseIs("Validation_Status_UnequipWeapon")
			|| DragonCaseIs("Validation_Status_UnequipArmor")
			|| DragonCaseIs("Validation_Status_UnequipRelic")
			|| DragonCaseIs("Validation_Status_EquipWeapon")
			|| DragonCaseIs("Validation_Status_EquipArmor")
			|| DragonCaseIs("Validation_Status_EquipRelic")
			|| DragonCaseIs("Validation_Status_SellEquippedWeapon")
			|| DragonCaseIs("Validation_Status_UnequipWeaponInventoryFull")
			|| DragonCaseIs("Validation_Status_LargeInventoryPaging")
				|| DragonCaseIs("Validation_SaveLoad_ProgressionState")
				|| DragonCaseIs("Validation_SaveLoad_LaterGameLoadoutState")
					|| DragonCaseIs("Validation_WorldMap_ActionTalk")
					|| DragonCaseIs("Validation_WorldMap_ActMissNoAdjacentInteract")
					|| DragonCaseIs("Validation_WorldMap_ActMissThrottleRepeat")
					|| DragonCaseIs("Validation_WorldMap_FrontDoorShopNpcActSouth")
					|| DragonCaseIs("Validation_WorldMap_FrontDoorShopNpcActNorth")
					|| DragonCaseIs("Validation_WorldMap_FrontDoorShopNpcActEast")
					|| DragonCaseIs("Validation_WorldMap_FrontDoorShopNpcActWest")
					|| DragonCaseIs("Validation_WorldMap_FrontDoorFacingPriority")
					|| DragonCaseIs("Validation_WorldMap_FrontDoorFacingPriorityRetarget")
					|| DragonCaseIs("Validation_WorldMap_RouteCancelImmediateAct")
					|| DragonCaseIs("Validation_WorldMap_RouteCancelDirectionalRetarget")
					|| DragonCaseIs("Validation_WorldMap_RouteTargetRetapThrottle")
					|| DragonCaseIs("Validation_WorldMap_HealEvent")
			|| DragonCaseIs("Validation_WorldMap_TrainEvent")
			|| DragonCaseIs("Validation_WorldMap_TrainStatusGrowth")
			|| DragonCaseIs("Validation_WorldMap_TrainDecline")
			|| DragonCaseIs("Validation_WorldMap_TrainToShopSequence")
			|| DragonCaseIs("Validation_WorldMap_ChallengeEvent")
			|| DragonCaseIs("Validation_WorldMap_ChallengeDecline")
			|| DragonCaseIs("Validation_WorldMap_ChallengeDefeatPanel")
			|| DragonCaseIs("Validation_WorldMap_ChallengeVictoryProgression")
			|| DragonCaseIs("Validation_WorldMap_ShopEntry")
			|| DragonCaseIs("Validation_WorldMap_WarpTransition")
			|| DragonCaseIs("Validation_WorldMap_AutoPathTreasure")
			|| DragonCaseIs("Validation_WorldMap_CurrentTileRouteCancel")
			|| DragonCaseIs("Validation_WorldMap_GateRouteMagnet")
			|| DragonCaseIs("Validation_WorldMap_GateBattleEntry")
			|| DragonCaseIs("Validation_WorldMap_GateBattleProgression")
			|| DragonCaseIs("Validation_WorldMap_RandomBattleEntry")
			|| DragonCaseIs("Validation_WorldMap_RandomBattleRetreat"))
			ESystem::NewStartupCallback(DragonValidationOnStartup);
	ESystem::ClearReportedTextDraws();
	ESystem::NewDrawCallback(DragonValidationOnDraw);
}

#endif

struct DragonStartupConfig {
	DragonStartupConfig () {
		ESystem::SetLaunchDesignSize(DESIGN_PREFERRED_WIDTH, DESIGN_PREFERRED_HEIGHT);
#if defined(DRAGON_TEST)
		ESystem::NewPreRunCallback(DragonValidationOnPreRun);
#endif
	}
};

static DragonStartupConfig gDragonStartupConfig;

}

#if !defined(DRAGON_ENGINE_ENABLE_MAIN)
int main(int argc, char* argv[]) {
	ESystem::SetLaunchArgs(argc, argv);
	return ESystem::Run();
}
#endif

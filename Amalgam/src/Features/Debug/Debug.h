#pragma once
#include "../../SDK/SDK.h"

#define DEBUG_INFO
//#define DEBUG_TEXT

#ifdef DEBUG_INFO

// How long a line stays in the feed, and how long a panel row survives without being rewritten. The
// rows are refreshed every frame by whichever feature owns them, so half a second is enough to tell
// "still running" from "finished" without flickering between frames.
constexpr float DEBUG_LOG_TIME = 6.f;
constexpr float DEBUG_INFO_TIME = 0.5f;

// The feed only keeps the newest lines: it is a window onto what the cheat is doing right now, and a
// feature that logs every tick would otherwise push everything else out of it within one frame.
constexpr size_t DEBUG_LOG_MAX = 24;

// Rows drawn per panel. A section that reports per player - the resolver does - would otherwise be as
// long as the server is full, and the panels are laid out from the bottom of the screen.
constexpr size_t DEBUG_SECTION_MAX = 6;

// A line in the on-screen feed, top left. Written by everything that logs while debug logging is on,
// so a feature does not have to know this exists to be readable in game.
struct DebugLog_t
{
	std::string m_sText = "";
	Color_t m_tColor = { 255, 255, 255, 255 };
	float m_flTime = 0.f;
	int m_iCount = 1; // consecutive identical lines collapsed into one
};

// One row of a panel, bottom left. Keyed by section and name so a feature can rewrite the same row
// every frame instead of stacking duplicates of it.
struct DebugValue_t
{
	std::string m_sKey = "";
	std::string m_sValue = "";
	Color_t m_tColor = { 255, 255, 255, 255 };
	float m_flTime = 0.f;
};

struct DebugSection_t
{
	std::string m_sName = "";
	std::vector<DebugValue_t> m_vValues = {};
};

#ifdef DEBUG_TEXT
struct DebugText_t
{
	std::string m_sText = "";
	float m_flTime = 0.f;
	std::optional<Vec2> vPosition2D = std::nullopt;
	std::optional<Vec3> vPosition3D = std::nullopt;
	Color_t m_tColor = {};
};
#endif

class CDebug
{
private:
	std::deque<DebugLog_t> m_dLog = {};
	std::vector<DebugSection_t> m_vSections = {};

	DebugSection_t& GetSection(const char* sSection);

	// Both draw into one left-aligned column: the feed starts at the y it is handed and returns the y
	// the panels continue from.
	int DrawLog(int y);
	void DrawInfo(CTFPlayer* pLocal, int y);

#ifdef DEBUG_TEXT
	std::map<int, std::vector<DebugText_t>> m_mDebugText = {};
	void DrawText();
#endif

public:
	void Draw(CTFPlayer* pLocal);

	// Writes a line into the feed. SDK::Output calls this for every line that goes to the console while
	// debug logging is on, so existing logging shows up on screen without its call sites changing.
	void Log(const char* sFunction, const char* sLog, Color_t tColor = { 255, 255, 255, 255 });

	// Writes one row of a panel. The same section and key overwrite in place, which is what lets a
	// feature report per-frame values without the panel growing a row per frame.
	void Info(const char* sSection, const std::string& sKey, const std::string& sValue, Color_t tColor = { 255, 255, 255, 255 });

	void Clear();

#ifdef DEBUG_TEXT
	const std::map<int, std::vector<DebugText_t>>& GetText();
	const std::vector<DebugText_t>& GetText(uint32_t uType);
	void ClearText(uint32_t uType = 0);
	void PopText(uint32_t uType = 0);
	void AddText(const DebugText_t& sText);
	void AddText(const DebugText_t& sText, uint32_t uType);
	void AddText(const std::string& sString, Color_t tColor = Vars::Menu::Theme::Active.Value);
	void AddText(const std::string& sString, const Vec2& vPosition, Color_t tColor = Vars::Menu::Theme::Active.Value);
	void AddText(const std::string& sString, const Vec3& vPosition, Color_t tColor = Vars::Menu::Theme::Active.Value);
	void AddText(const std::string& sString, uint32_t uType, Color_t tColor = Vars::Menu::Theme::Active.Value);
	void AddText(const std::string& sString, uint32_t uType, const Vec2& vPosition, Color_t tColor = Vars::Menu::Theme::Active.Value);
	void AddText(const std::string& sString, uint32_t uType, const Vec3& vPosition, Color_t tColor = Vars::Menu::Theme::Active.Value);
	void AddText(const std::string& sString, float flTime, Color_t tColor = Vars::Menu::Theme::Active.Value);
	void AddText(const std::string& sString, float flTime, const Vec2& vPosition, Color_t tColor = Vars::Menu::Theme::Active.Value);
	void AddText(const std::string& sString, float flTime, const Vec3& vPosition, Color_t tColor = Vars::Menu::Theme::Active.Value);
	void AddText(const std::string& sString, float flTime, uint32_t uType, Color_t tColor = Vars::Menu::Theme::Active.Value);
	void AddText(const std::string& sString, float flTime, uint32_t uType, const Vec2& vPosition, Color_t tColor = Vars::Menu::Theme::Active.Value);
	void AddText(const std::string& sString, float flTime, uint32_t uType, const Vec3& vPosition, Color_t tColor = Vars::Menu::Theme::Active.Value);
#endif
};

ADD_FEATURE(CDebug, Debug);

#endif
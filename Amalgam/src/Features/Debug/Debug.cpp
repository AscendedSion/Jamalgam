#include "Debug.h"

#ifdef DEBUG_INFO

// Panels are laid out from the bottom of the screen upward in this order, and anything not named here
// after them, so a section keeps the place it was first seen in rather than moving around when another
// feature starts writing.
static const char* s_aSectionOrder[] = { "Local", "Resolver", "Nospread", "Strafing", "Bones", "Splashbot" };

DebugSection_t& CDebug::GetSection(const char* sSection)
{
	for (auto& tSection : m_vSections)
	{
		if (tSection.m_sName == sSection)
			return tSection;
	}

	return m_vSections.emplace_back(DebugSection_t{ sSection,{} });
}

void CDebug::Log(const char* sFunction, const char* sLog, Color_t tColor)
{
	if (!Vars::Debug::Logging.Value || !I::GlobalVars)
		return;

	const std::string sLine = sLog && *sLog ? std::format("[{}] {}", sFunction, sLog) : std::format("[{}]", sFunction);

	// A feature that logs the same thing every tick repeats itself, and the count is the interesting
	// part of a per-tick line anyway, so the repeat is folded into the line above it.
	if (!m_dLog.empty() && m_dLog.back().m_sText == sLine)
	{
		m_dLog.back().m_flTime = I::GlobalVars->curtime;
		m_dLog.back().m_iCount++;
		return;
	}

	m_dLog.push_back({ sLine, tColor, I::GlobalVars->curtime, 1 });
	while (m_dLog.size() > DEBUG_LOG_MAX)
		m_dLog.pop_front();
}

void CDebug::Info(const char* sSection, const std::string& sKey, const std::string& sValue, Color_t tColor)
{
	if (!Vars::Debug::Info.Value || !I::GlobalVars)
		return;

	auto& tSection = GetSection(sSection);
	for (auto& tValue : tSection.m_vValues)
	{
		if (tValue.m_sKey != sKey)
			continue;

		tValue.m_sValue = sValue;
		tValue.m_tColor = tColor;
		tValue.m_flTime = I::GlobalVars->curtime;
		return;
	}

	tSection.m_vValues.push_back({ sKey, sValue, tColor, I::GlobalVars->curtime });
}

void CDebug::Clear()
{
	m_dLog.clear();
	m_vSections.clear();
}

void CDebug::Draw(CTFPlayer* pLocal)
{
	if (!Vars::Debug::Info.Value && !Vars::Debug::Logging.Value
#ifdef DEBUG_TEXT
		&& m_mDebugText.empty()
#endif
		)
		return;

	// One column, top left: the feed first, then the panels underneath it. Splitting them into opposite
	// corners put two halves of the same readout as far apart as the screen allows.
	int y = H::Draw.Scale(6);
	if (Vars::Debug::Logging.Value)
		y = DrawLog(y);
	if (Vars::Debug::Info.Value)
		DrawInfo(pLocal, y + H::Draw.Scale(4));

#ifdef DEBUG_TEXT
	DrawText();
#endif
}

int CDebug::DrawLog(int y)
{
	const auto& fFont = H::Fonts.GetFont(FONT_INDICATORS);
	const int nTall = fFont.m_nTall + H::Draw.Scale(1);
	const int x = H::Draw.Scale(8);

	for (auto it = m_dLog.begin(); it != m_dLog.end();)
	{
		if (I::GlobalVars->curtime - it->m_flTime > DEBUG_LOG_TIME)
			it = m_dLog.erase(it);
		else
			++it;
	}

	for (auto& tLine : m_dLog)
	{
		y += nTall;
		if (tLine.m_iCount > 1)
			H::Draw.StringOutlined(fFont, x, y, tLine.m_tColor, Vars::Menu::Theme::Background.Value, ALIGN_TOPLEFT,
				std::format("{} (x{})", tLine.m_sText, tLine.m_iCount).c_str());
		else
			H::Draw.StringOutlined(fFont, x, y, tLine.m_tColor, Vars::Menu::Theme::Background.Value, ALIGN_TOPLEFT,
				tLine.m_sText.c_str());
	}

	return y;
}

void CDebug::DrawInfo(CTFPlayer* pLocal, int y)
{
	// The local readout is written here rather than by a feature, because the values only exist once
	// the frame is being painted.
	if (pLocal)
	{
		auto pCmd = !I::EngineClient->IsPlayingDemo() ? G::CurrentUserCmd : I::Input->GetUserCmd(I::ClientState->lastoutgoingcommand);
		if (pCmd)
			Info("Local", "command", std::format("tick {} num {}", pCmd->tick_count, pCmd->command_number));

		Vec3 vOrigin = pLocal->m_vecOrigin();
		Info("Local", "origin", std::format("{:.0f}, {:.0f}, {:.0f}", vOrigin.x, vOrigin.y, vOrigin.z));
		Info("Local", "velocity", std::format("{:.0f}", pLocal->m_vecVelocity().Length()));
		Info("Local", "tickbase", std::format("{}", pLocal->m_nTickBase()));
		Info("Local", "choke", std::format("{} ({})", G::Choking, I::ClientState->chokedcommands));
		Info("Local", "k/d", std::format("{} / {}", pLocal->m_iKills(), pLocal->m_iDeaths()));
	}

	// Drop whatever nothing refreshed: a row is only on screen while its feature is still writing it,
	// which is what makes the panels describe the current frame instead of the last interesting one.
	for (auto it = m_vSections.begin(); it != m_vSections.end();)
	{
		auto& vValues = it->m_vValues;
		for (auto it2 = vValues.begin(); it2 != vValues.end();)
		{
			if (I::GlobalVars->curtime - it2->m_flTime > DEBUG_INFO_TIME)
				it2 = vValues.erase(it2);
			else
				++it2;
		}

		if (vValues.empty())
			it = m_vSections.erase(it);
		else
			++it;
	}

	std::vector<DebugSection_t*> vSections = {};
	for (auto sName : s_aSectionOrder)
	{
		for (auto& tSection : m_vSections)
		{
			if (tSection.m_sName == sName)
				vSections.push_back(&tSection);
		}
	}
	for (auto& tSection : m_vSections)
	{
		if (std::find(vSections.begin(), vSections.end(), &tSection) == vSections.end())
			vSections.push_back(&tSection);
	}

	const auto& fFont = H::Fonts.GetFont(FONT_INDICATORS);
	const int nTall = fFont.m_nTall + H::Draw.Scale(1);
	const int x = H::Draw.Scale(8);

	// Every section is a header and its rows, one under the other, left aligned with the feed above.
	for (auto pSection : vSections)
	{
		H::Draw.StringOutlined(fFont, x, y += nTall, Vars::Menu::Theme::Accent.Value, Vars::Menu::Theme::Background.Value, ALIGN_TOPLEFT, pSection->m_sName.c_str());

		size_t iDrawn = 0;
		for (auto& tValue : pSection->m_vValues)
		{
			if (iDrawn++ == DEBUG_SECTION_MAX)
			{
				H::Draw.StringOutlined(fFont, x + H::Draw.Scale(6), y += nTall, Vars::Menu::Theme::Inactive.Value, Vars::Menu::Theme::Background.Value, ALIGN_TOPLEFT,
					std::format("+{} more", pSection->m_vValues.size() - DEBUG_SECTION_MAX).c_str());
				break;
			}

			H::Draw.StringOutlined(fFont, x + H::Draw.Scale(6), y += nTall, tValue.m_tColor, Vars::Menu::Theme::Background.Value, ALIGN_TOPLEFT,
				std::format("{}: {}", tValue.m_sKey, tValue.m_sValue).c_str());
		}
	}
}

#ifdef DEBUG_TEXT
void CDebug::DrawText()
{
	const auto& fFont = H::Fonts.GetFont(FONT_INDICATORS);
	const int nTall = fFont.m_nTall + H::Draw.Scale(1);
	const int x = H::Draw.Scale(8);
	int y = H::Draw.Scale(6);

	for (auto& [uType, vDebugText] : m_mDebugText)
	{
		for (auto it = vDebugText.begin(); it != vDebugText.end();)
		{
			if (it->m_flTime && it->m_flTime < I::GlobalVars->curtime)
				it = vDebugText.erase(it);
			else
				++it;
		}
		if (vDebugText.empty())
		{
			m_mDebugText.erase(uType);
			continue;
		}
		for (auto& [sString, flTime, vPosition2D, vPosition3D, tColor] : vDebugText)
		{
			if (vPosition3D)
			{
				if (Vec3 vScreen; SDK::W2S(*vPosition3D, vScreen))
					H::Draw.StringOutlined(fFont, vScreen.x, vScreen.y, tColor, Vars::Menu::Theme::Background.Value, ALIGN_TOPLEFT, sString.c_str());
			}
			else if (vPosition2D)
				H::Draw.StringOutlined(fFont, vPosition2D->x, vPosition2D->y, tColor, Vars::Menu::Theme::Background.Value, ALIGN_TOPLEFT, sString.c_str());
			else
				H::Draw.StringOutlined(fFont, x, y += nTall, tColor, Vars::Menu::Theme::Background.Value, ALIGN_TOPLEFT, sString.c_str());
		}
	}
}

const std::map<int, std::vector<DebugText_t>>& CDebug::GetText()
{
	return m_mDebugText;
}
const std::vector<DebugText_t>& CDebug::GetText(uint32_t uType)
{
	return m_mDebugText[uType];
}

void CDebug::ClearText(uint32_t uType)
{
	if (!uType)
		m_mDebugText.clear();
	else if (m_mDebugText.contains(uType))
		m_mDebugText.erase(uType);
}

void CDebug::PopText(uint32_t uType)
{
	if (m_mDebugText.contains(uType))
		return;

	auto& vDebugText = m_mDebugText[uType];
	if (!vDebugText.empty())
		return;

	vDebugText.erase(vDebugText.begin());
}

void CDebug::AddText(const DebugText_t& sText)
{
	m_mDebugText[0].push_back(sText);
}

void CDebug::AddText(const DebugText_t& sText, uint32_t uType)
{
	m_mDebugText[uType].push_back(sText);
}

void CDebug::AddText(const std::string& sString, Color_t tColor)
{
	m_mDebugText[0].emplace_back(sString, 0.f, std::nullopt, std::nullopt, tColor);
}

void CDebug::AddText(const std::string& sString, const Vec2& vPosition, Color_t tColor)
{
	m_mDebugText[0].emplace_back(sString, 0.f, vPosition, std::nullopt, tColor);
}

void CDebug::AddText(const std::string& sString, const Vec3& vPosition, Color_t tColor)
{
	m_mDebugText[0].emplace_back(sString, 0.f, std::nullopt, vPosition, tColor);
}

void CDebug::AddText(const std::string& sString, uint32_t uType, Color_t tColor)
{
	m_mDebugText[uType].emplace_back(sString, 0.f, std::nullopt, std::nullopt, tColor);
}

void CDebug::AddText(const std::string& sString, uint32_t uType, const Vec2& vPosition, Color_t tColor)
{
	m_mDebugText[uType].emplace_back(sString, 0.f, vPosition, std::nullopt, tColor);
}

void CDebug::AddText(const std::string& sString, uint32_t uType, const Vec3& vPosition, Color_t tColor)
{
	m_mDebugText[uType].emplace_back(sString, 0.f, std::nullopt, vPosition, tColor);
}

void CDebug::AddText(const std::string& sString, float flTime, Color_t tColor)
{
	m_mDebugText[0].emplace_back(sString, flTime, std::nullopt, std::nullopt, tColor);
}

void CDebug::AddText(const std::string& sString, float flTime, const Vec2& vPosition, Color_t tColor)
{
	m_mDebugText[0].emplace_back(sString, flTime, vPosition, std::nullopt, tColor);
}

void CDebug::AddText(const std::string& sString, float flTime, const Vec3& vPosition, Color_t tColor)
{
	m_mDebugText[0].emplace_back(sString, flTime, std::nullopt, vPosition, tColor);
}

void CDebug::AddText(const std::string& sString, float flTime, uint32_t uType, Color_t tColor)
{
	m_mDebugText[uType].emplace_back(sString, flTime, std::nullopt, std::nullopt, tColor);
}

void CDebug::AddText(const std::string& sString, float flTime, uint32_t uType, const Vec2& vPosition, Color_t tColor)
{
	m_mDebugText[uType].emplace_back(sString, flTime, vPosition, std::nullopt, tColor);
}

void CDebug::AddText(const std::string& sString, float flTime, uint32_t uType, const Vec3& vPosition, Color_t tColor)
{
	m_mDebugText[uType].emplace_back(sString, flTime, std::nullopt, vPosition, tColor);
}
#endif

#endif
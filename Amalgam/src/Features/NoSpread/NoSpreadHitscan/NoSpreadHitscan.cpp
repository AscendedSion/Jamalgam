#include "NoSpreadHitscan.h"

#include "../../Ticks/Ticks.h"
#include "../../AntiCheatCompatibility/AntiCheatCompatibility.h"
#include "../../Debug/Debug.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <limits>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

MAKE_SIGNATURE(CTFWeaponBaseGun_GetWeaponSpread, "client.dll", "48 89 5C 24 ? 57 48 83 EC ? 4C 63 91", 0x0);

namespace
{
    constexpr int MAX_SPREAD_PELLETS = 32;

    constexpr int MAX_SPREAD_ATTEMPTS = 256;

    constexpr int MAX_SOLVE_ITERATIONS = 8;

    struct SpreadBias_t
    {
        float m_flFlatness = 0.5f;
        float m_flOneMinusFlatness = 0.5f;
        float m_flShotBias = 1.f;
    };

    SpreadBias_t GetSpreadBias()
    {
        static auto ai_shot_bias_min = H::ConVars.FindVar("ai_shot_bias_min");
        static auto ai_shot_bias_max = H::ConVars.FindVar("ai_shot_bias_max");

        const float flMin = ai_shot_bias_min ? ai_shot_bias_min->GetFloat() : -1.f;
        const float flMax = ai_shot_bias_max ? ai_shot_bias_max->GetFloat() : 1.f;

        float flBias = 1.f;
        if (flBias > 1.f)
            flBias = 1.f;
        else if (flBias < 0.f)
            flBias = 0.f;

        SpreadBias_t tBias;
        tBias.m_flShotBias = (flMax - flMin) * flBias + flMin;
        tBias.m_flFlatness = fabsf(tBias.m_flShotBias) * 0.5f;
        tBias.m_flOneMinusFlatness = 1.f - tBias.m_flFlatness;
        return tBias;
    }

    bool GetSpreadSample(int iSeed, const SpreadBias_t& tBias, float& flOutX, float& flOutY)
    {
        SDK::RandomSeed(iSeed);

        for (int iAttempt = 0; iAttempt < MAX_SPREAD_ATTEMPTS; iAttempt++)
        {
            const float flA = SDK::RandomFloat(-1.f, 1.f);
            const float flB = SDK::RandomFloat(-1.f, 1.f);
            float flX = flA * tBias.m_flFlatness + flB * tBias.m_flOneMinusFlatness;

            const float flC = SDK::RandomFloat(-1.f, 1.f);
            const float flD = SDK::RandomFloat(-1.f, 1.f);
            float flY = flC * tBias.m_flFlatness + flD * tBias.m_flOneMinusFlatness;

            if (tBias.m_flShotBias < 0.f)
            {
                flX = flX >= 0.f ? 1.f - flX : -1.f - flX;
                flY = flY >= 0.f ? 1.f - flY : -1.f - flY;
            }

            if (flX * flX + flY * flY <= 1.f)
            {
                flOutX = flX;
                flOutY = flY;
                return true;
            }
        }

        return false;
    }

    // The direction the server fires for these view angles, for one pellet's spread sample.
    Vec3 GetServerShotDir(const Vec3& vAngles, float flX, float flY, const Vec3& vSpread)
    {
        Vec3 vForward, vRight, vUp;
        Math::AngleVectors(vAngles, &vForward);
        Math::VectorVectors(vForward, vRight, vUp);

        return (vForward + vRight * (flX * vSpread.x) + vUp * (flY * vSpread.y)).Normalized();
    }
}


void CNoSpreadHitscan::Reset()
{
    m_bWaitingForPlayerPerf = false;
    m_flServerTime = 0.f;
    m_vTimeDeltas.clear();
    m_vRTTs.clear();
    m_dTimeDelta = 0.0;
    m_dLastPerfTime = 0.0;
    m_dMinRTT = 0.0;
    m_dRTTSigma = 0.0;
    m_bHasEstimate = false;

    m_iSeed = 0;
    m_flMantissaStep = 0.f;

    m_bSynced = false;
}


bool CNoSpreadHitscan::ShouldRun(CTFWeaponBase* pWeapon)
{
    if (pWeapon
        ? G::PrimaryWeaponType != EWeaponType::HITSCAN || pWeapon->GetWeaponSpread() <= 0.f
        : !Vars::Aimbot::General::NoSpread.Value || !I::EngineClient->IsInGame())
        return false;

    if (F::AntiCheatCompatibility.Active())
        return false;

    return true;
}


int CNoSpreadHitscan::GetSeed(CUserCmd* pCmd)
{
    static auto sv_usercmd_custom_random_seed = H::ConVars.FindVar("sv_usercmd_custom_random_seed");
    if (!sv_usercmd_custom_random_seed || !sv_usercmd_custom_random_seed->GetBool())
        return pCmd->random_seed & 255;

    const double dNow = SDK::PlatFloatTime();

    double dServerSeconds;
    if (m_flServerTime > 0.f && m_dLastPerfTime > 0.0)
        dServerSeconds = m_flServerTime + (dNow - m_dLastPerfTime) + m_dTimeDelta;
    else
        dServerSeconds = dNow + m_dTimeDelta;

    double dServerMs = dServerSeconds * 1000.0;

    if (m_flMantissaStep > 1.f)
        dServerMs = std::round(dServerMs / m_flMantissaStep) * m_flMantissaStep;

    const float flTime = float(dServerMs);
    return *reinterpret_cast<const int*>((const char*)&flTime) & 255;
}

float CNoSpreadHitscan::CalcMantissaStep(float flV)
{
    const float flNextValue = std::nextafter(flV, std::numeric_limits<float>::infinity());
    const float flStepMs = (flNextValue - flV) * 1000.f;

    if (!(flStepMs > 0.f) || !std::isfinite(flStepMs))
        return 1.f;

    return std::exp2(std::ceil(std::log2(flStepMs)));
}


std::string CNoSpreadHitscan::GetFormat(int iServerTime)
{
    const int iDays = iServerTime / 86400;
    const int iHours = iServerTime / 3600 % 24;
    const int iMinutes = iServerTime / 60 % 60;
    const int iSeconds = iServerTime % 60;

    if (iDays)
        return std::format("{}d {}h", iDays, iHours);
    else if (iHours)
        return std::format("{}h {}m", iHours, iMinutes);
    else
        return std::format("{}m {}s", iMinutes, iSeconds);
}

void CNoSpreadHitscan::AskForPlayerPerf()
{
    if (!ShouldRun())
        return Reset();

    if (G::Choking)
        return;

    static Timer tTimer = {};
    static float flJitter = 0.f;

    const float flInterval = m_bWaitingForPlayerPerf
        ? Vars::Aimbot::General::NoSpreadBackupInterval.Value
        : Vars::Aimbot::General::NoSpreadInterval.Value;

    if (tTimer.Run(flInterval + flJitter))
    {
        // Small random jitter so our cadence is not perfectly uniform.
        flJitter = (float)std::rand() / (float)RAND_MAX * (flInterval * 0.30f)
            - (flInterval * 0.15f);

        I::ClientState->SendStringCmd("playerperf");
        m_bWaitingForPlayerPerf = true;
        m_dRequestTime = SDK::PlatFloatTime();
    }
}

bool CNoSpreadHitscan::ParsePlayerPerf(const std::string& sMsg)
{
    if (!ShouldRun())
        return false;

    std::vector<std::string> vTokens;
    vTokens.reserve(8);
    {
        std::istringstream iss(sMsg);
        std::string tok;
        while (iss >> tok)
            vTokens.push_back(std::move(tok));
    }

    if (vTokens.size() < 3)
        return false;

    float flNewServerTime = 0.f;
    {
        size_t uPos = 0;
        try { flNewServerTime = std::stof(vTokens[0], &uPos); }
        catch (...) { return false; }
        if (uPos != vTokens[0].size())
            return false;
    }

    m_bWaitingForPlayerPerf = false;

    const double dReceiveTime = SDK::PlatFloatTime();
    const double dCurrentRTT = dReceiveTime - m_dRequestTime;

    if (dCurrentRTT <= 0.0 || dCurrentRTT > 2.0)
        return false;

    if (flNewServerTime < m_flServerTime)
        return true;

    if (DebugJitter::g_dLastRTT > 0.0)
        DebugJitter::g_dJitter = std::abs(dCurrentRTT - DebugJitter::g_dLastRTT);
    DebugJitter::g_dLastRTT = dCurrentRTT;

    const bool bLoopback = SDK::IsLoopback();
    m_flServerTime = flNewServerTime;
    m_dLastPerfTime = dReceiveTime;

    if (bLoopback)
    {
        m_dTimeDelta = 0.0;
        m_dMinRTT = 0.0;
        m_dRTTSigma = 0.0;
        m_bHasEstimate = true;
    }
    else
    {

        const double dServerTimeAtReceive = flNewServerTime + dCurrentRTT * 0.5;
        const double dSampleDelta = dServerTimeAtReceive - dReceiveTime;

        if (m_dMinRTT <= 0.0 || dCurrentRTT < m_dMinRTT)
            m_dMinRTT = dCurrentRTT;

        m_vTimeDeltas.push_back(dSampleDelta);
        m_vRTTs.push_back(dCurrentRTT);

        const size_t uWindow = (size_t)std::max(1, Vars::Aimbot::General::NoSpreadAverage.Value);
        while (m_vTimeDeltas.size() > uWindow) m_vTimeDeltas.pop_front();
        while (m_vRTTs.size() > uWindow) m_vRTTs.pop_front();

        std::vector<double> vSorted(m_vTimeDeltas.begin(), m_vTimeDeltas.end());
        std::sort(vSorted.begin(), vSorted.end());
        const double dMedian = vSorted[vSorted.size() / 2];

        std::vector<double> vAbsDev;
        vAbsDev.reserve(vSorted.size());
        for (double d : vSorted)
            vAbsDev.push_back(std::abs(d - dMedian));
        std::sort(vAbsDev.begin(), vAbsDev.end());
        const double dMAD = vAbsDev[vAbsDev.size() / 2];

        const double dRejectBand = std::max(0.002, dMAD * 3.0);

        double dWSum = 0.0, dWAccum = 0.0;
        for (size_t i = 0; i < m_vTimeDeltas.size(); ++i)
        {
            const double dDelta = m_vTimeDeltas[i];
            if (std::abs(dDelta - dMedian) > dRejectBand)
                continue;

            const double dRTT = m_vRTTs[i];
            const double dW = 1.0 / (dRTT * dRTT);
            dWSum += dW;
            dWAccum += dW * dDelta;
        }
        m_dTimeDelta = (dWSum > 0.0) ? (dWAccum / dWSum) : dMedian;

        if (m_vRTTs.size() >= 2)
        {
            const double dMeanRTT = std::accumulate(m_vRTTs.begin(), m_vRTTs.end(), 0.0) / (double)m_vRTTs.size();
            double dVar = 0.0;
            for (double r : m_vRTTs)
                dVar += (r - dMeanRTT) * (r - dMeanRTT);
            dVar /= (double)(m_vRTTs.size() - 1);
            m_dRTTSigma = std::sqrt(dVar);
        }
        else
        {
            m_dRTTSigma = 0.0;
        }

        m_bHasEstimate = true;
    }

    m_dTimeDelta += TICKS_TO_TIME(Vars::Aimbot::General::NoSpreadOffset.Value);

    const float flMantissaStep = CalcMantissaStep(m_flServerTime);
    m_bSynced = (flMantissaStep >= 1.f) || bLoopback;

    if (flMantissaStep > m_flMantissaStep && (m_bSynced || !m_flMantissaStep))
    {
        SDK::Output("Seed Prediction",
            m_bSynced ? std::format("Synced ({:.3f})", m_dTimeDelta).c_str()
            : "Not synced, step too low",
            Vars::Menu::Theme::Accent.Value);
        SDK::Output("Seed Prediction",
            std::format("Age {}; Step {:.0f}", GetFormat(m_flServerTime), flMantissaStep).c_str(),
            Vars::Menu::Theme::Accent.Value);
    }
    m_flMantissaStep = flMantissaStep;

    return true;
}

void CNoSpreadHitscan::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
    auto fState = [](const char* sState, Color_t tColor = { 150, 150, 150, 255 })
        {
                F::Debug.Info("Nospread", "state", sState, tColor);
        };

    if (!ShouldRun(pWeapon))
    {
        fState("off for this weapon");
        return;
    }

    m_iSeed = GetSeed(pCmd);

        F::Debug.Info("Nospread", "seed", std::format("{}", m_iSeed));
        F::Debug.Info("Nospread", "sync", m_bSynced ? "synced" : "waiting for perf", m_bSynced ? Color_t(100, 255, 150, 255) : Color_t(255, 200, 0, 255));
        F::Debug.Info("Nospread", "delta", std::format("{:.3f}", m_dTimeDelta));
        F::Debug.Info("Nospread", "sigma", std::format("{:.2f}ms against step {:.0f}", m_dRTTSigma * 1000.0, m_flMantissaStep),
            m_dRTTSigma * 1000.0 >= m_flMantissaStep * 0.5 ? Color_t(255, 90, 90, 255) : Color_t(100, 255, 150, 255));
    

#ifdef SEEDPRED_DEBUG
    SDK::Output("CNoSpreadHitscan::Run",
        std::format("{:.6f}: {}", SDK::PlatFloatTime() + m_dTimeDelta, m_iSeed).c_str(),
        { 255, 0, 0 });
#endif

    if (!m_bSynced || !m_bHasEstimate)
    {
        fState(!m_bSynced ? "unsynced" : "no estimate yet", { 255, 200, 0, 255 });
        return;
    }

    if (m_dRTTSigma * 1000.0 >= m_flMantissaStep * 0.5)
    {
        fState("jitter above half a step", { 255, 200, 0, 255 });
        return;
    }

    const float flSpread = pWeapon->GetWeaponSpread();
    const int   iBulletsPerShot = pWeapon->GetBulletsPerShot();
    const float flFireRate = std::ceilf(pWeapon->GetFireRate() / TICK_INTERVAL) * TICK_INTERVAL;

    const int iTicks = F::Ticks.GetTicks();
    if (!iTicks || iTicks < TIME_TO_TICKS(flFireRate) * 2)
    {
        const float flTimeSinceLastShot = I::GlobalVars->curtime - pWeapon->m_flLastFireTime();
        if (flTimeSinceLastShot > (iBulletsPerShot > 1 ? 0.25f : 1.25f))
        {
            fState("weapon reset, shot already perfect", { 255, 200, 0, 255 });
            return;
        }
    }

    const Vec3 vSpread = { flSpread, flSpread, 0.f };
    const SpreadBias_t tBias = GetSpreadBias();
    const int iShots = std::clamp(iBulletsPerShot, 1, MAX_SPREAD_PELLETS);

    float aX[MAX_SPREAD_PELLETS], aY[MAX_SPREAD_PELLETS];
    for (int i = 0; i < iShots; i++)
    {
        if (!GetSpreadSample(m_iSeed + i, tBias, aX[i], aY[i]))
            return;
    }

    Vec3 vForward;
    Math::AngleVectors(pCmd->viewangles, &vForward);
    const Vec3 vAimAngles = Math::VectorAngles(vForward);

    Vec3 vAngles = pCmd->viewangles;
    for (int iIteration = 0; iIteration < MAX_SOLVE_ITERATIONS; iIteration++)
    {
        Vec3 vMean = {};
        for (int i = 0; i < iShots; i++)
            vMean += GetServerShotDir(vAngles, aX[i], aY[i], vSpread);

        if (!vMean.Normalize())
            return;

        const Vec3 vError = vAimAngles.DeltaAngle(Math::VectorAngles(vMean));
        if (vError.LengthSqr() < 1e-8f)
            break;

        vAngles += vError;
        vAngles.x = Math::NormalizeAngle(vAngles.x);
        vAngles.y = Math::NormalizeAngle(vAngles.y);
        vAngles.z = 0.f;
    }

    //what the correction actually moved the shot by, next to the state that let it through.
    F::Debug.Info("Nospread", "state", "applied", { 100, 255, 150, 255 });
    F::Debug.Info("Nospread", "shift", std::format("{:.2f} deg, {} pellet(s)", vAngles.DeltaAngle(pCmd->viewangles).Length(), iShots), { 100, 255, 150, 255 });
      

    pCmd->viewangles = vAngles;
    Math::ClampAngles(pCmd->viewangles);

    G::SilentAngles = true;
}


void CNoSpreadHitscan::Draw(CTFPlayer* pLocal)
{
    if (!(Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::SeedPrediction) || !ShouldRun() || !pLocal->IsAlive())
        return;

    int x = Vars::Menu::SeedPredictionDisplay.Value.x;
    int y = Vars::Menu::SeedPredictionDisplay.Value.y + 8;
    const auto& fFont = H::Fonts.GetFont(FONT_INDICATORS);
    const int nTall = fFont.m_nTall + H::Draw.Scale(1);

    EAlign align = ALIGN_TOP;
    if (x <= 100 + H::Draw.Scale(50, Scale_Round))
    {
        x -= H::Draw.Scale(42, Scale_Round);
        align = ALIGN_TOPLEFT;
    }
    else if (x >= H::Draw.m_nScreenW - 100 - H::Draw.Scale(50, Scale_Round))
    {
        x += H::Draw.Scale(42, Scale_Round);
        align = ALIGN_TOPRIGHT;
    }

    const auto& cColor = m_bSynced ? Vars::Menu::Theme::Active.Value : Vars::Menu::Theme::Inactive.Value;

    H::Draw.StringOutlined(fFont, x, y, cColor, Vars::Menu::Theme::Background.Value, align, std::format("UPTIME: {}", GetFormat(m_flServerTime)).c_str());
    H::Draw.StringOutlined(fFont, x, y += nTall, cColor, Vars::Menu::Theme::Background.Value, align, std::format("STEP: {}", m_flMantissaStep).c_str());
    if (Vars::Debug::Info.Value)
        H::Draw.StringOutlined(fFont, x, y += nTall, cColor, Vars::Menu::Theme::Background.Value, align, std::format("DELTA: {:.3f}", m_dTimeDelta).c_str());
}
#include "skillwheelstorage.h"

#include <tibia/world/game.h>
#include <tibia/world/localplayer.h>
#include <tibia/world/map.h>
#include <client/staticdata.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <numeric>

namespace tibia::skillwheel {

namespace {

using Args = std::vector<std::any>;
using client::gamecore::argInt;

template<class T>
const T* arg(const Args& args, const size_t index)
{
    return index < args.size() ? std::any_cast<T>(&args[index]) : nullptr;
}

uint64_t balance(const Otc::ResourceTypes_t type)
{
    const LocalPlayerPtr& player = g_game.getLocalPlayer();
    return player ? player->getResourceBalance(type) : 0;
}

QString toQString(const std::string& text) { return QString::fromStdString(text); }

}

TSkillWheelStorage::TSkillWheelStorage(QObject* parent) : QObject(parent)
{
    m_hooks.emplace_back("g_game", "onDestinyWheel", [this](LuaObject*, const Args& args) {
        onDestinyWheel(args);
        return std::any{};
    });
    m_hooks.emplace_back("g_game", "onResourcesBalanceChange", [this](LuaObject*, const Args& args) {
        const auto* type = arg<Otc::ResourceTypes_t>(args, 2);
        if (!type || (*type >= Otc::RESOURCE_LESSER_GEMS && *type <= Otc::RESOURCE_GREATER_FRAGMENT) || *type == Otc::RESOURCE_BANK_BALANCE
            || *type == Otc::RESOURCE_GOLD_EQUIPPED)
            Q_EMIT resourcesChanged();
        return std::any{};
    });
    m_hooks.emplace_back("g_game", "onPreset", [this](LuaObject*, const Args& args) {
        const auto* id = arg<uint32_t>(args, 0);
        if (id && *id > 0 && static_cast<int>(*id) <= m_presets.size())
            selectPreset(m_presets.at(static_cast<int>(*id) - 1).name);
        return std::any{};
    });
    m_hooks.emplace_back("g_game", "onGameEnd", [this](LuaObject*, const Args&) {
        clear();
        m_presetsOwner = 0;
        m_presets.clear();
        m_currentPreset.clear();
        return std::any{};
    });
}

TSkillWheelStorage::~TSkillWheelStorage() = default;

void TSkillWheelStorage::clear()
{
    m_hasData = false;
    m_playerId = 0;
    m_isLocal = false;
    m_isPreview = false;
    m_playerName.clear();
    m_changeState = 0;
    m_points = m_extraPoints = m_huntingTaskPoints = 0;
    m_hasMonkQuest = false;
    m_scrolls.clear();
    m_serverWheel = {};
    m_currentWheel = {};
    m_gems.clear();
    m_basicGrades.clear();
    m_supremeGrades.clear();
}

/// CIP protocol handler 0x100fa0290: the player id, view and change state,
/// vocation, points, slices, consumed scrolls, socketed and revealed gems and
/// the mod grades (qtclient parseOpenWheelWindow).
void TSkillWheelStorage::onDestinyWheel(const Args& args)
{
    const bool canView = argInt(args, 1) != 0;
    if (!canView) {
        Q_EMIT serverDataReceived(false);
        return;
    }
    clear();
    m_hasData = true;
    m_playerId = static_cast<uint32_t>(argInt(args, 0));
    const LocalPlayerPtr& local = g_game.getLocalPlayer();
    m_isLocal = local && local->getId() == m_playerId;
    if (m_isLocal)
        m_playerName = toQString(local->getName());
    else if (const CreaturePtr creature = g_map.getCreatureById(m_playerId))
        m_playerName = toQString(creature->getName());
    m_changeState = static_cast<int>(argInt(args, 2));
    m_vocation = std::clamp(static_cast<int>(argInt(args, 3)), 1, VocationCount);
    m_points = static_cast<int>(argInt(args, 4));
    m_extraPoints = static_cast<int>(argInt(args, 5));
    if (const auto* points = arg<std::vector<uint16_t>>(args, 6))
        for (size_t i = 0; i < points->size() && i < SlotCount; ++i)
            m_serverWheel.points[i] = (*points)[i];

    if (const auto* gems = arg<std::vector<GemData>>(args, 9)) {
        m_gems.reserve(gems->size());
        for (const GemData& g : *gems)
            m_gems.push_back({ g.gemID, g.locked != 0, std::clamp<int>(g.gemDomain, 0, DomainCount - 1), std::clamp<int>(g.gemType, 0, 2),
                               g.lesserBonus, g.regularBonus, g.supremeBonus });
    }
    if (const auto* equipped = arg<std::vector<uint16_t>>(args, 8))
        for (const uint16_t id : *equipped)
            if (const TGem* g = gem(id))
                m_serverWheel.gems[g->domain] = id;
    if (const auto* grades = arg<std::map<uint8_t, uint8_t>>(args, 10))
        for (const auto& [mod, grade] : *grades)
            m_basicGrades[mod] = grade;
    if (const auto* grades = arg<std::map<uint8_t, uint8_t>>(args, 11))
        for (const auto& [mod, grade] : *grades)
            m_supremeGrades[mod] = grade;

    const auto* scrolls = arg<std::vector<uint16_t>>(args, 7);
    const auto* scrollPoints = arg<std::vector<uint16_t>>(args, 13);
    if (scrolls)
        for (size_t i = 0; i < scrolls->size(); ++i)
            m_scrolls.emplace_back((*scrolls)[i], scrollPoints && i < scrollPoints->size() ? (*scrollPoints)[i] : 0);
    m_huntingTaskPoints = static_cast<int>(argInt(args, 14));
    m_hasMonkQuest = argInt(args, 15) != 0;

    m_currentWheel = m_serverWheel;
    if (m_isLocal)
        loadPresets();
    Q_EMIT serverDataReceived(true);
    Q_EMIT currentWheelChanged();
    Q_EMIT presetsChanged();
}

/// CIP 0x100fb3500: vt0x128 with change state 0, no points and no gems.
void TSkillWheelStorage::setPreview(const QString& name, const int vocation)
{
    clear();
    m_hasData = true;
    m_isPreview = true;
    m_isLocal = true;
    m_playerName = name;
    m_vocation = std::clamp(vocation, 1, VocationCount);
    if (const LocalPlayerPtr& local = g_game.getLocalPlayer())
        m_playerId = local->getId();
    Q_EMIT currentWheelChanged();
    Q_EMIT presetsChanged();
}

void TSkillWheelStorage::setCurrentWheel(const TWheel& wheel)
{
    if (m_currentWheel == wheel)
        return;
    m_currentWheel = wheel;
    Q_EMIT currentWheelChanged();
}

const TGem* TSkillWheelStorage::gem(const int id) const
{
    const auto it = std::ranges::find_if(m_gems, [id](const TGem& g) { return g.id == id; });
    return it != m_gems.end() ? &*it : nullptr;
}

int TSkillWheelStorage::modGradesPoints() const { return modGradePoints(m_basicGrades, m_supremeGrades); }

/// CIP 0x100de0a80: regular + special + mod grade points.
int TSkillWheelStorage::totalPoints() const { return m_points + m_extraPoints + modGradesPoints(); }

/// CIP 0x1011230a0.
int TSkillWheelStorage::spentPoints() const { return std::accumulate(m_currentWheel.points.begin(), m_currentWheel.points.end(), 0); }

int TSkillWheelStorage::unrevealedGems(const int quality) const
{
    switch (quality) {
        case 0: return static_cast<int>(balance(Otc::RESOURCE_LESSER_GEMS));
        case 1: return static_cast<int>(balance(Otc::RESOURCE_REGULAR_GEMS));
        case 2: return static_cast<int>(balance(Otc::RESOURCE_GREATER_GEMS));
        default: return 0;
    }
}

int TSkillWheelStorage::lesserFragments() const { return static_cast<int>(balance(Otc::RESOURCE_LESSER_FRAGMENT)); }
int TSkillWheelStorage::greaterFragments() const { return static_cast<int>(balance(Otc::RESOURCE_GREATER_FRAGMENT)); }
uint64_t TSkillWheelStorage::gold() const { return balance(Otc::RESOURCE_BANK_BALANCE) + balance(Otc::RESOURCE_GOLD_EQUIPPED); }

QString TSkillWheelStorage::presetsPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/characterdata/%1/skillWheel.json").arg(m_presetsOwner);
}

void TSkillWheelStorage::loadPresets()
{
    if (m_presetsOwner == m_playerId && !m_presets.isEmpty())
        return;
    m_presetsOwner = m_playerId;
    m_presets.clear();
    m_currentPreset.clear();
    QFile file(presetsPath());
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        for (const QJsonValue& value : root.value(QLatin1String("presets")).toArray()) {
            const QJsonObject object = value.toObject();
            TPreset preset{ object.value(QLatin1String("name")).toString(), {} };
            if (preset.name.isEmpty() || this->preset(preset.name) || m_presets.size() >= MaxPresets)
                continue;
            const QJsonArray points = object.value(QLatin1String("points")).toArray();
            for (int i = 0; i < SlotCount && i < points.size(); ++i)
                preset.wheel.points[i] = points.at(i).toInt();
            const QJsonArray gems = object.value(QLatin1String("gems")).toArray();
            for (int d = 0; d < DomainCount && d < gems.size(); ++d)
                preset.wheel.gems[d] = gems.at(d).toInt(NoGem);
            m_presets.append(preset);
        }
        m_currentPreset = root.value(QLatin1String("active")).toString();
    }
    if (m_presets.isEmpty())
        m_presets.append({ DefaultPresetName, m_serverWheel });
    if (!preset(m_currentPreset))
        m_currentPreset = m_presets.first().name;
}

void TSkillWheelStorage::savePresets() const
{
    if (m_presetsOwner == 0)
        return;
    QJsonArray presets;
    for (const TPreset& preset : m_presets) {
        QJsonArray points, gems;
        for (const int p : preset.wheel.points)
            points.append(p);
        for (const int g : preset.wheel.gems)
            gems.append(g);
        presets.append(QJsonObject{ { QLatin1String("name"), preset.name }, { QLatin1String("points"), points }, { QLatin1String("gems"), gems } });
    }
    const QJsonObject root{ { QLatin1String("active"), m_currentPreset }, { QLatin1String("presets"), presets } };
    const QString path = presetsPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson());
        file.commit();
    }
}

const TPreset* TSkillWheelStorage::preset(const QString& name) const
{
    const auto it = std::ranges::find_if(m_presets, [&](const TPreset& p) { return p.name == name; });
    return it != m_presets.end() ? &*it : nullptr;
}

bool TSkillWheelStorage::canManagePresets() const { return m_hasData && m_isLocal && !m_isPreview && m_changeState == 1; }

bool TSkillWheelStorage::isPresetDirty() const
{
    const TPreset* current = preset(m_currentPreset);
    return current && !(current->wheel == m_currentWheel);
}

TWheel TSkillWheelStorage::sanitized(const TWheel& wheel, bool* adjusted) const
{
    TWheel result = wheel;
    *adjusted = false;
    for (int d = 0; d < DomainCount; ++d) {
        const TGem* g = result.gems[d] == NoGem ? nullptr : gem(result.gems[d]);
        if (result.gems[d] != NoGem && (!g || g->domain != d)) {
            result.gems[d] = NoGem;
            *adjusted = true;
        }
    }
    const int spent = std::accumulate(result.points.begin(), result.points.end(), 0);
    if (!isValid(result.points) || spent > totalPoints()) {
        result.points = {};
        *adjusted = true;
    }
    return result;
}

bool TSkillWheelStorage::selectPreset(const QString& name)
{
    const TPreset* selected = preset(name);
    if (!selected)
        return true;
    bool adjusted = false;
    const TWheel wheel = sanitized(selected->wheel, &adjusted);
    m_currentPreset = name;
    savePresets();
    setCurrentWheel(wheel);
    Q_EMIT presetsChanged();
    return !adjusted;
}

void TSkillWheelStorage::saveCurrentPreset()
{
    const auto it = std::ranges::find_if(m_presets, [&](const TPreset& p) { return p.name == m_currentPreset; });
    if (it == m_presets.end())
        return;
    it->wheel = m_currentWheel;
    savePresets();
    Q_EMIT presetsChanged();
}

bool TSkillWheelStorage::addPreset(const QString& name, const TWheel& wheel)
{
    if (name.isEmpty() || preset(name) || m_presets.size() >= MaxPresets)
        return false;
    m_presets.append({ name, wheel });
    m_currentPreset = name;
    savePresets();
    setCurrentWheel(wheel);
    Q_EMIT presetsChanged();
    return true;
}

bool TSkillWheelStorage::renameCurrentPreset(const QString& name)
{
    if (name.isEmpty() || preset(name))
        return false;
    const auto it = std::ranges::find_if(m_presets, [&](const TPreset& p) { return p.name == m_currentPreset; });
    if (it == m_presets.end())
        return false;
    it->name = name;
    m_currentPreset = name;
    savePresets();
    Q_EMIT presetsChanged();
    return true;
}

bool TSkillWheelStorage::deleteCurrentPreset()
{
    if (m_presets.size() <= 1)
        return false;
    m_presets.removeIf([&](const TPreset& p) { return p.name == m_currentPreset; });
    m_currentPreset = m_presets.first().name;
    savePresets();
    Q_EMIT presetsChanged();
    return true;
}

}

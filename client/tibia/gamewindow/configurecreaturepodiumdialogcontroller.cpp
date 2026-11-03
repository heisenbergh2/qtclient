#include "configurecreaturepodiumdialogcontroller.h"

#include "input/gameaction.h"
#include "input/gameactionhandlers.h"

#include <tibia/world/game.h>
#include <tibia/assets/things.h>

namespace tibia::gamewindow {

namespace {

using LookDir = cip::tibia::qmlcomponents::TOutfitAppearanceInstanceQmlType::ECreatureLookDirection;
using Args = std::vector<std::any>;

QColor paletteColor(const int index)
{
    return qmlcomponents::outfitpalette::color(index);
}

LookDir lookDirectionFromProtocol(const uint8_t direction)
{
    switch (direction) {
        case 0: return LookDir::NORTH;
        case 1: return LookDir::EAST;
        case 3: return LookDir::WEST;
        default: return LookDir::SOUTH;
    }
}

uint8_t protocolFromLookDirection(const LookDir direction)
{
    switch (direction) {
        case LookDir::NORTH: return 0;
        case LookDir::EAST: return 1;
        case LookDir::WEST: return 3;
        default: return 2;
    }
}

TBossSkinsModel::Entry entryFromRace(const uint16_t raceId, const std::string& name, const Outfit& outfit)
{
    TBossSkinsModel::Entry entry;
    entry.raceId = raceId;
    if (!name.empty() || outfit.getId() != 0 || outfit.getAuxId() != 0) {
        entry.name = QString::fromStdString(name);
        entry.outfit = outfit;
        return entry;
    }
    const RaceType& race = g_things.getRaceData(raceId);
    entry.name = QString::fromStdString(race.name);
    entry.outfit = race.outfit;
    return entry;
}

}

void TBossSkinsModel::setEntries(QList<Entry> entries)
{
    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();
}

const TBossSkinsModel::Entry* TBossSkinsModel::entryAt(const int row) const
{
    if (row < 0 || row >= m_entries.size())
        return nullptr;
    return &m_entries[row];
}

int TBossSkinsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant TBossSkinsModel::data(const QModelIndex& index, const int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const Entry& entry = m_entries[index.row()];
    const Outfit& outfit = entry.outfit;
    const bool objectOutfit = outfit.getCategory() == ThingCategoryItem || outfit.getId() == 0;
    const int outfitId = objectOutfit ? outfit.getAuxId() : outfit.getId();
    switch (role) {
        case OutfitIDRole:
        case OtherGenderOutfitIDRole: return outfitId;
        case OutfitNameRole: return entry.name;
        case FirstAddOnRole: return (outfit.getAddons() & 1) != 0;
        case SecondAddOnRole: return (outfit.getAddons() & 2) != 0;
        case OutfitTypeRole: return 0;
        case RaceRole: return entry.raceId;
        case HeadColorRole: return paletteColor(outfit.getHead());
        case TorsoColorRole: return paletteColor(outfit.getBody());
        case LegsColorRole: return paletteColor(outfit.getLegs());
        case DetailColorRole: return paletteColor(outfit.getFeet());
        case IsObjectOutfitRole: return objectOutfit;
        default: return {};
    }
}

QHash<int, QByteArray> TBossSkinsModel::roleNames() const
{
    return {
        { OutfitIDRole, "outfitID" },
        { OutfitNameRole, "outfitName" },
        { FirstAddOnRole, "firstAddOn" },
        { SecondAddOnRole, "secondAddOn" },
        { OutfitTypeRole, "outfitType" },
        { OtherGenderOutfitIDRole, "otherGenderOutfitID" },
        { RaceRole, "race" },
        { HeadColorRole, "headColor" },
        { TorsoColorRole, "torsoColor" },
        { LegsColorRole, "legsColor" },
        { DetailColorRole, "detailColor" },
        { IsObjectOutfitRole, "isObjectOutfit" },
    };
}

TBossSkinsProxyModel::TBossSkinsProxyModel(TBossSkinsModel* source, QObject* parent)
    : QSortFilterProxyModel(parent)
{
    setSourceModel(source);
}

void TBossSkinsProxyModel::setOutfitNameFilter(const QString& v)
{
    if (v == m_nameFilter)
        return;
    beginFilterChange();
    m_nameFilter = v;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
    Q_EMIT filterChanged();
}

void TBossSkinsProxyModel::resetFilter()
{
    if (m_nameFilter.isEmpty())
        return;
    beginFilterChange();
    m_nameFilter.clear();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
    Q_EMIT filterChanged();
}

bool TBossSkinsProxyModel::filterAcceptsRow(const int sourceRow, const QModelIndex& sourceParent) const
{
    if (m_nameFilter.isEmpty())
        return true;
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    return index.data(TBossSkinsModel::OutfitNameRole).toString().contains(m_nameFilter, Qt::CaseInsensitive);
}

TConfigureCreaturePodiumDialogController::TConfigureCreaturePodiumDialogController(QObject* parent)
    : TDialogController(QStringLiteral("outfitdialog/ConfigureCreaturePodiumDialog.qml"), parent)
    , m_proxy(&m_skins)
    , m_preset(new qmlcomponents::TOutfitPresetQmlType(this))
{
    m_bossSkinsModel = &m_proxy;
    m_outfitPreset = m_preset;
    m_showFloor = true;
    m_showOutfit = true;
    m_showSocket = true;
    m_direction = LookDir::SOUTH;

    m_hooks.emplace_back("g_game", "onGameEnd", [this](LuaObject*, const Args&) {
        dialog()->destroy();
        return std::any{};
    });
}

void TConfigureCreaturePodiumDialogController::open(const Outfit& selected, const bool bossPodium,
                                                    const std::vector<RaceRow>& races, const Position& pos,
                                                    const uint16_t itemId, const uint8_t stackpos,
                                                    const bool podiumVisible, const bool monsterVisible,
                                                    const uint8_t direction)
{
    Q_UNUSED(bossPodium);
    m_position = pos;
    m_stackpos = stackpos;
    m_socketTypeID = itemId;
    m_showSocket = podiumVisible;
    m_showOutfit = monsterVisible;
    m_showFloor = true;
    m_direction = lookDirectionFromProtocol(direction);

    QList<TBossSkinsModel::Entry> entries;
    entries.reserve(static_cast<qsizetype>(races.size()));
    for (const auto& [raceId, name, outfit] : races)
        entries.append(entryFromRace(raceId, name, outfit));
    m_skins.setEntries(std::move(entries));
    m_proxy.resetFilter();

    int selectedRow = -1;
    const int selectedLook = selected.getId() != 0 ? selected.getId() : selected.getAuxId();
    if (selectedLook != 0) {
        for (int i = 0; i < m_skins.entries().size(); ++i) {
            const Outfit& outfit = m_skins.entries()[i].outfit;
            const int look = outfit.getId() != 0 ? outfit.getId() : outfit.getAuxId();
            if (look == selectedLook) {
                selectedRow = i;
                break;
            }
        }
    }
    if (selectedRow < 0 && !m_skins.entries().isEmpty() && monsterVisible)
        selectedRow = 0;

    if (selectedRow >= 0) {
        const TBossSkinsModel::Entry* entry = m_skins.entryAt(selectedRow);
        m_selectedIndex = selectedRow;
        if (entry)
            applyEntry(*entry);
    } else {
        clearSelection();
    }

    Q_EMIT showChanged();
    Q_EMIT selectedIndexChanged();
    dialog()->show();
}

void TConfigureCreaturePodiumDialogController::applyEntry(const TBossSkinsModel::Entry& entry)
{
    const Outfit& outfit = entry.outfit;
    const bool objectOutfit = outfit.getCategory() == ThingCategoryItem || outfit.getId() == 0;
    const int outfitId = objectOutfit ? outfit.getAuxId() : outfit.getId();
    m_chosenRace = entry.raceId;
    if (m_chosenOutfit != outfitId) {
        m_chosenOutfit = outfitId;
        Q_EMIT chosenOutfitChanged();
    }
    Q_EMIT chosenRaceChanged();

    m_preset->setName(entry.name);
    m_preset->setIsObjectOutfit(objectOutfit);
    m_preset->setOutfitID(outfitId);
    m_preset->setOutfitFirstAddOn((outfit.getAddons() & 1) != 0);
    m_preset->setOutfitSecondAddOn((outfit.getAddons() & 2) != 0);
    m_preset->setMountID(0);
    m_preset->setSummonID(0);
    qmlcomponents::TOutfitColorQmlType colors(outfit.getHead(), outfit.getBody(), outfit.getLegs(), outfit.getFeet());
    m_preset->outfitColor()->copyFrom(&colors);
    Q_EMIT selectedBossOutfitChanged();
}

void TConfigureCreaturePodiumDialogController::clearSelection()
{
    m_selectedIndex = -1;
    m_chosenRace = 0;
    if (m_chosenOutfit != 0) {
        m_chosenOutfit = 0;
        Q_EMIT chosenOutfitChanged();
    }
    Q_EMIT chosenRaceChanged();
    m_preset->setName({});
    m_preset->setIsObjectOutfit(false);
    m_preset->setOutfitID(0);
    m_preset->setOutfitFirstAddOn(false);
    m_preset->setOutfitSecondAddOn(false);
    m_preset->setMountID(0);
    m_preset->setSummonID(0);
    qmlcomponents::TOutfitColorQmlType colors;
    m_preset->outfitColor()->copyFrom(&colors);
    Q_EMIT selectedBossOutfitChanged();
}

void TConfigureCreaturePodiumDialogController::onIndexChanged(const int SelectedIndex, const int ChosenRace)
{
    m_selectedIndex = SelectedIndex;
    Q_EMIT selectedIndexChanged();

    if (SelectedIndex < 0) {
        clearSelection();
        return;
    }

    const QModelIndex proxyIndex = m_proxy.index(SelectedIndex, 0);
    const QModelIndex sourceIndex = m_proxy.mapToSource(proxyIndex);
    if (const TBossSkinsModel::Entry* entry = m_skins.entryAt(sourceIndex.row())) {
        applyEntry(*entry);
        if (ChosenRace != 0 && ChosenRace != m_chosenRace) {
            m_chosenRace = ChosenRace;
            Q_EMIT chosenRaceChanged();
        }
        return;
    }
    m_chosenRace = ChosenRace;
    Q_EMIT chosenRaceChanged();
}

void TConfigureCreaturePodiumDialogController::onCancelClicked()
{
    dialog()->close();
}

void TConfigureCreaturePodiumDialogController::onOkClicked()
{
    if (g_game.isOnline()) {
        const bool showCreature = m_showOutfit && m_chosenRace != 0;
        auto action = input::mutableClone(*input::makeAction(input::EGameAction::SET_CREATURE_PODIUM_CONFIGURATION));
        action->creatureId = showCreature ? static_cast<uint32_t>(m_chosenRace) : 0;
        action->position = m_position;
        action->parameter = m_socketTypeID;
        action->stackPosition = m_stackpos;
        action->lookDirection = protocolFromLookDirection(m_direction);
        action->enable = m_showSocket;
        action->showSocket = showCreature;
        input::publish(action);
    }
    dialog()->close();
}

void TConfigureCreaturePodiumDialogController::onControllerFinished()
{
    dialog()->close();
}

}

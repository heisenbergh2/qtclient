#include "outfitdialogcontroller.h"

#include "gamewindow/genericentertextdialogcontroller.h"
#include "input/gameaction.h"
#include "input/gameactionhandlers.h"
#include "qmlcomponents/outfittypes.h"

#include <tibia/world/game.h>
#include <tibia/world/localplayer.h>
#include <client/outfit.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

namespace tibia::gamewindow {

namespace {

using cip::tibia::qmlcomponents::TOutfitAppearanceInstanceQmlType;
using qmlcomponents::TClipboardHelperQmlService;
using qmlcomponents::TOutfitColorQmlType;
using qmlcomponents::TOutfitPresetQmlType;

constexpr int OwnedOutfitType = 0;
constexpr int StoreOutfitType = 1;

using Entries = QList<TCustomiseCharacterModel::Entry>;
using Gender = cip::tibia::gamewindow::TSelectOutfitDialogController::EOutfitGender;

/// CIP 0x100b414a0: the pair partner, or the id itself.
int otherGenderOutfit(const QList<QPair<int, int>>& pairs, const int id)
{
    for (const auto& [male, female] : pairs) {
        if (male == id)
            return female;
        if (female == id)
            return male;
    }
    return id;
}

/// CIP 0x100b41810 / 0x100b41850: male when first of a pair, female when second.
int outfitGender(const QList<QPair<int, int>>& pairs, const int id)
{
    for (const auto& pair : pairs)
        if (pair.first == id)
            return Gender::OutfitGenderMale;
    for (const auto& pair : pairs)
        if (pair.second == id)
            return Gender::OutfitGenderFemale;
    return Gender::OutfitGenderUnknown;
}

/// CIP 0x100b406f0: a store entry keeps its offer id.
void applyOffers(Entries& entries, const QList<int>& offers)
{
    for (qsizetype i = 0; i < entries.size(); ++i)
        entries[i].offerId = entries[i].type == StoreOutfitType ? offers.value(i) : 0;
}

}

void TCustomiseCharacterModel::setEntries(QList<Entry> entries)
{
    beginResetModel();
    m_entries = std::move(entries);
    m_count = m_entries.size();
    endResetModel();
    Q_EMIT rowCountChanged();
}

const TCustomiseCharacterModel::Entry* TCustomiseCharacterModel::ownedEntry(const int id) const
{
    for (const Entry& entry : m_entries)
        if (entry.id == id && entry.type == OwnedOutfitType)
            return &entry;
    return nullptr;
}

int TCustomiseCharacterModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant TCustomiseCharacterModel::data(const QModelIndex& index, const int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const Entry& entry = m_entries[index.row()];
    switch (role) {
        case Qt::DisplayRole:
        case OutfitNameRole: return entry.name;
        case OutfitIDRole: return entry.id;
        case OutfitSourceRole: return 0;
        case FirstAddOnRole: return entry.firstAddOn;
        case SecondAddOnRole: return entry.secondAddOn;
        case OutfitTypeRole: return entry.type;
        case CanBeColorizedRole: return entry.canBeColorized;
        case OfferIDRole: return entry.offerId;
        case OutfitGenderRole: return entry.gender;
        case OtherGenderOutfitIDRole: return entry.otherGenderId;
        default: return {};
    }
}

QHash<int, QByteArray> TCustomiseCharacterModel::roleNames() const
{
    return {
        { OutfitIDRole, "outfitID" },
        { OutfitNameRole, "outfitName" },
        { OutfitSourceRole, "outfitSource" },
        { FirstAddOnRole, "firstAddOn" },
        { SecondAddOnRole, "secondAddOn" },
        { OutfitTypeRole, "outfitType" },
        { CanBeColorizedRole, "canBeColorized" },
        { OfferIDRole, "offerID" },
        { OutfitGenderRole, "outfitGender" },
        { OtherGenderOutfitIDRole, "otherGenderOutfitID" },
    };
}

TCustomizeCharacterSortProxyModel::TCustomizeCharacterSortProxyModel(TCustomiseCharacterModel* source, QObject* parent)
    : cip::TCustomizeCharacterSortProxyModel(parent)
{
    setSourceModel(source);
}

void TCustomizeCharacterSortProxyModel::setShowOnlyOwnOutfits(const bool v)
{
    if (v == m_showOnlyOwnOutfits)
        return;
    beginFilterChange();
    m_showOnlyOwnOutfits = v;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
    Q_EMIT filterChanged();
}

void TCustomizeCharacterSortProxyModel::setOutfitNameFilter(const QString& v)
{
    if (v == m_outfitNameFilter)
        return;
    beginFilterChange();
    m_outfitNameFilter = v;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
    Q_EMIT filterChanged();
}

int TCustomizeCharacterSortProxyModel::numberOfOwnedOutfits() const
{
    const auto* source = static_cast<const TCustomiseCharacterModel*>(sourceModel());
    return static_cast<int>(std::ranges::count_if(source->entries(), [](const auto& entry) { return entry.type == OwnedOutfitType; }));
}

/// CIP 0x100be1b40.
void TCustomizeCharacterSortProxyModel::resetFilter()
{
    beginFilterChange();
    m_showOnlyOwnOutfits = false;
    m_outfitNameFilter.clear();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
    Q_EMIT filterChanged();
}

/// CIP 0x100be2140.
bool TCustomizeCharacterSortProxyModel::filterAcceptsRow(const int sourceRow, const QModelIndex& sourceParent) const
{
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    if (m_showOnlyOwnOutfits && index.data(TCustomiseCharacterModel::OutfitTypeRole).toInt() != OwnedOutfitType)
        return false;
    if (!m_outfitNameFilter.isEmpty()
        && !index.data(TCustomiseCharacterModel::OutfitNameRole).toString().contains(m_outfitNameFilter, Qt::CaseInsensitive))
        return false;
    return true;
}

void TOutfitPresetsModel::setPresets(QList<QJsonObject> presets, QList<bool> owned)
{
    beginResetModel();
    m_presets = std::move(presets);
    m_owned = std::move(owned);
    m_count = m_presets.size();
    endResetModel();
    Q_EMIT rowCountChanged();
}

int TOutfitPresetsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_presets.size();
}

QVariant TOutfitPresetsModel::data(const QModelIndex& index, const int role) const
{
    if (!index.isValid() || index.row() >= m_presets.size())
        return {};
    switch (role) {
        case JsonDataRole: return m_presets[index.row()];
        case EverythingOwnedRole: return m_owned.value(index.row());
        default: return {};
    }
}

QHash<int, QByteArray> TOutfitPresetsModel::roleNames() const
{
    return { { JsonDataRole, "jsondata" }, { EverythingOwnedRole, "everythingowned" } };
}

TSelectOutfitDialogController::TSelectOutfitDialogController(QObject* parent)
    : TDialogController(QStringLiteral("outfitdialog/OutfitDialog.qml"), parent)
    , m_outfitsProxy(&m_outfits)
    , m_mountsProxy(&m_mounts)
    , m_summonsProxy(&m_summons)
    , m_preset(new TOutfitPresetQmlType(this))
{
    // View options live in the controller for the session, no file (CIP ctor
    // 0x100bdc960 defaults; QML writes them back on every change).
    m_dialogMode = SelectOutfit;
    m_showMovement = false;
    m_showOutfit = true;
    m_showSummon = true;
    m_showFloor = true;
    m_direction = TOutfitAppearanceInstanceQmlType::ECreatureLookDirection::SOUTH;
    m_outfitsModel = &m_outfitsProxy;
    m_mountsModel = &m_mountsProxy;
    m_summonsModel = &m_summonsProxy;
    m_presetsModel = &m_presets;
    m_selectedOutfitPreset = m_preset;
    for (int i = 0; i < qmlcomponents::outfitpalette::Count; ++i)
        m_hsvColors.append(qmlcomponents::outfitpalette::color(i));
}

TSelectOutfitDialogController::~TSelectOutfitDialogController()
{
    savePresets();
}

void TSelectOutfitDialogController::setWindowOptions(WindowOptions options)
{
    m_isMounted = options.mounted;
    m_randomMount = options.randomMount;
    m_windowOptions = std::move(options);
    if (m_windowOptions.tryOutfitMode == 5) {
        m_dialogMode = ConfigureShowOffSocket;
        m_showSocket = m_windowOptions.podiumVisible;
        m_showOutfit = m_windowOptions.podiumShowOutfit;
        switch (m_windowOptions.podiumDirection) {
            case 0: m_direction = TOutfitAppearanceInstanceQmlType::ECreatureLookDirection::NORTH; break;
            case 1: m_direction = TOutfitAppearanceInstanceQmlType::ECreatureLookDirection::EAST; break;
            case 3: m_direction = TOutfitAppearanceInstanceQmlType::ECreatureLookDirection::WEST; break;
            default: m_direction = TOutfitAppearanceInstanceQmlType::ECreatureLookDirection::SOUTH; break;
        }
    } else {
        m_dialogMode = SelectOutfit;
    }
    Q_EMIT dialogModeChanged();
    Q_EMIT showChanged();
}

void TSelectOutfitDialogController::open(const Outfit& current, QList<TCustomiseCharacterModel::Entry> outfits,
                                         QList<TCustomiseCharacterModel::Entry> mounts, QList<TCustomiseCharacterModel::Entry> summons)
{
    if (dialog()->isShown())
        dialog()->close();

    const LocalPlayerPtr& player = g_game.getLocalPlayer();
    const quint32 characterId = player ? player->getId() : 0;
    if (characterId != m_characterId) {
        savePresets();
        m_characterId = characterId;
        loadPresets();
    }

    const auto& pairs = m_windowOptions.genderPairs;
    for (auto& entry : outfits) {
        entry.otherGenderId = otherGenderOutfit(pairs, entry.id);
        entry.gender = entry.otherGenderId != 0 ? outfitGender(pairs, entry.id) : int(Gender::OutfitGenderUnknown);
    }
    for (Entries* list : { &mounts, &summons })
        for (auto& entry : *list)
            entry.otherGenderId = entry.id;
    applyOffers(outfits, m_windowOptions.outfitOffers);
    applyOffers(mounts, m_windowOptions.mountOffers);
    applyOffers(summons, m_windowOptions.summonOffers);

    // CIP 0x100b406f0: a current outfit (or its other-gender twin), mount or
    // summon missing from its list falls back to the list's first entry
    // (outfit add-ons cleared), or none.
    const auto contains = [](const Entries& list, const int id, const int twin) {
        return std::ranges::any_of(list, [&](const auto& entry) { return entry.id == id || entry.id == twin; });
    };
    int outfitId = current.getId();
    int addons = current.getAddons();
    if (!contains(outfits, outfitId, otherGenderOutfit(pairs, outfitId))) {
        outfitId = outfits.isEmpty() ? 0 : outfits.first().id;
        addons = 0;
    }
    int mountId = current.getMount();
    if (!contains(mounts, mountId, mountId))
        mountId = mounts.isEmpty() ? 0 : mounts.first().id;
    int summonId = current.getFamiliar();
    if (!contains(summons, summonId, summonId))
        summonId = summons.isEmpty() ? 0 : summons.first().id;

    m_outfits.setEntries(std::move(outfits));
    m_mounts.setEntries(std::move(mounts));
    m_summons.setEntries(std::move(summons));
    m_outfitsProxy.resetFilter();
    m_mountsProxy.resetFilter();
    m_summonsProxy.resetFilter();

    m_preset->setName({});
    m_preset->setIsObjectOutfit(false);
    m_preset->setOutfitID(outfitId);
    m_preset->setOutfitFirstAddOn((addons & 1) != 0);
    m_preset->setOutfitSecondAddOn((addons & 2) != 0);
    TOutfitColorQmlType outfitColor(current.getHead(), current.getBody(), current.getLegs(), current.getFeet());
    m_preset->outfitColor()->copyFrom(&outfitColor);
    m_preset->setMountID(mountId);
    TOutfitColorQmlType mountColor(current.getMountHead(), current.getMountBody(), current.getMountLegs(), current.getMountFeet());
    m_preset->mountColor()->copyFrom(&mountColor);
    m_preset->setSummonID(summonId);

    onCharacterPresetsChanged();
    dialog()->show();
}

/// CIP 0x100bdebd0.
void TSelectOutfitDialogController::onCancelClicked()
{
    dialog()->close();
}

/// CIP 0x100bdebe0: closes, then opens the store on the offer (open action 4).
void TSelectOutfitDialogController::onClickedOpenStoreOffer(const int offerID)
{
    dialog()->close();
    auto action = input::mutableClone(*input::makeAction(input::EGameAction::OPEN_STORE));
    action->parameter = 4;
    action->instance = offerID;
    input::publish(action);
}

void TSelectOutfitDialogController::onControllerFinished()
{
    dialog()->close();
}

/// CIP 0x100bdd460: a preset counts as owned when its outfit and mount are.
void TSelectOutfitDialogController::onCharacterPresetsChanged()
{
    QList<QJsonObject> presets;
    QList<bool> owned;
    TOutfitPresetQmlType preset;
    for (const QJsonValue& value : std::as_const(m_presetArray)) {
        const QJsonObject json = value.toObject();
        if (!preset.canBeFilledFromJsonObject(json))
            continue;
        preset.fromJsonObject(json);
        bool everythingOwned = true;
        if (preset.outfitID() != 0) {
            const auto* outfit = m_outfits.ownedEntry(preset.outfitID());
            everythingOwned = outfit && (!preset.outfitFirstAddOn() || outfit->firstAddOn) && (!preset.outfitSecondAddOn() || outfit->secondAddOn);
        }
        if (preset.mountID() != 0)
            everythingOwned = everythingOwned && m_mounts.ownedEntry(preset.mountID());
        presets.append(json);
        owned.append(everythingOwned);
    }
    m_presets.setPresets(std::move(presets), std::move(owned));
}

/// CIP 0x100bdf0d0 (SelectOutfit): publishes SET_PLAYER_OUTFIT, then closes.
/// Mode 5 (ConfigureShowOffSocket): publishes SET_SHOW_OFF_SOCKET_CONFIGURATION.
void TSelectOutfitDialogController::setCharacterConfiguration(cip::tibia::qmlcomponents::TOutfitPresetQmlType* pOutfitPreset,
                                                              const int LookDirectionRaw, const bool ShowSocket,
                                                              const bool MountCharacter)
{
    if (pOutfitPreset && g_game.isOnline()) {
        const auto* outfitColor = pOutfitPreset->outfitColor();
        const auto* mountColor = pOutfitPreset->mountColor();
        Outfit outfit;
        outfit.setCategory(ThingCategoryCreature);
        outfit.setId(pOutfitPreset->outfitID());
        outfit.setHead(outfitColor->headColorIndex());
        outfit.setBody(outfitColor->torsoColorIndex());
        outfit.setLegs(outfitColor->legsColorIndex());
        outfit.setFeet(outfitColor->detailColorIndex());
        outfit.setAddons((pOutfitPreset->outfitFirstAddOn() ? 1 : 0) + (pOutfitPreset->outfitSecondAddOn() ? 2 : 0));
        outfit.setMount(pOutfitPreset->mountID());
        outfit.setMountColors(mountColor->headColorIndex(), mountColor->torsoColorIndex(), mountColor->legsColorIndex(),
                              mountColor->detailColorIndex());
        outfit.setFamiliar(pOutfitPreset->summonID());
        if (m_dialogMode == ConfigureShowOffSocket) {
            auto action = input::mutableClone(*input::makeAction(input::EGameAction::SET_SHOW_OFF_SOCKET_CONFIGURATION));
            action->outfit = outfit;
            action->position = m_windowOptions.podiumPosition;
            action->parameter = m_windowOptions.podiumItemId;
            action->stackPosition = m_windowOptions.podiumStackpos;
            action->lookDirection = LookDirectionRaw;
            action->showSocket = ShowSocket;
            action->enable = ShowSocket;
            input::publish(action);
        } else {
            auto action = input::mutableClone(*input::makeAction(input::EGameAction::SET_PLAYER_OUTFIT));
            action->outfit = outfit;
            action->mountCharacter = MountCharacter;
            action->randomMount = m_randomMount;
            action->showSocket = ShowSocket;
            action->lookDirection = LookDirectionRaw;
            input::publish(action);
        }
    }
    dialog()->close();
}

/// CIP 0x100bdf470: -1 appends.
void TSelectOutfitDialogController::outfitPresetsSaveIndex(const QJsonObject& JsonObject, const int Index)
{
    if (Index >= 0 && Index < m_presetArray.size())
        m_presetArray.replace(Index, JsonObject);
    else
        m_presetArray.append(JsonObject);
    savePresets();
    onCharacterPresetsChanged();
}

/// CIP 0x100bdf4b0.
void TSelectOutfitDialogController::outfitPresetsDeleteIndex(const int Index)
{
    if (Index < 0 || Index >= m_presetArray.size())
        return;
    m_presetArray.removeAt(Index);
    savePresets();
    onCharacterPresetsChanged();
}

/// CIP 0x100bdf500: asks for the new name in a GenericEnterTextDialog.
void TSelectOutfitDialogController::outfitPresetsRenameIndex(const int Index)
{
    if (Index < 0 || Index >= m_presetArray.size())
        return;
    m_renameIndex = Index;
    if (!m_renameDialog) {
        m_renameDialog = new TGenericEnterTextDialogController(this);
        m_renameDialog->setCaption(qtTrId("outfit_preset_rename_dialog_caption"));
        connect(m_renameDialog, &TGenericEnterTextDialogController::textEntered, this, [this](const QString& name) {
            if (m_renameIndex < 0 || m_renameIndex >= m_presetArray.size() || name.trimmed().isEmpty())
                return;
            QJsonObject json = m_presetArray.at(m_renameIndex).toObject();
            json.insert(QStringLiteral("name"), name.trimmed());
            m_presetArray.replace(m_renameIndex, json);
            savePresets();
            onCharacterPresetsChanged();
        });
    }
    m_renameDialog->setText(m_presetArray.at(Index).toObject().value(QStringLiteral("name")).toString());
    m_renameDialog->dialog()->show();
}

/// CIP 0x100bdf720: a preset or a colour set.
void TSelectOutfitDialogController::copyToClipboard(QObject* pData)
{
    QJsonObject json;
    if (auto* preset = qobject_cast<cip::tibia::qmlcomponents::TOutfitPresetQmlType*>(pData))
        json = preset->toJsonObject();
    else if (auto* color = qobject_cast<cip::tibia::qmlcomponents::TOutfitColorQmlType*>(pData))
        json = color->toJsonObject();
    else
        return;
    TClipboardHelperQmlService clipboard;
    clipboard.setJsonObjectAsBase64String(json);
}

/// CIP 0x100bdf7e0: a JS-owned OutfitColor or OutfitPreset, or null.
QObject* TSelectOutfitDialogController::createFromClipboard()
{
    TClipboardHelperQmlService clipboard;
    const QJsonObject json = clipboard.getBase64StringAsJsonObject();
    QObject* result = nullptr;
    TOutfitColorQmlType color;
    TOutfitPresetQmlType preset;
    if (color.canBeFilledFromJsonObject(json)) {
        auto* created = new TOutfitColorQmlType();
        created->fromJsonObject(json);
        result = created;
    } else if (preset.canBeFilledFromJsonObject(json)) {
        auto* created = new TOutfitPresetQmlType();
        created->fromJsonObject(json);
        result = created;
    }
    if (result)
        QJSEngine::setObjectOwnership(result, QJSEngine::JavaScriptOwnership);
    return result;
}

QString TSelectOutfitDialogController::presetsFilePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/characterdata/%1/outfitPresets.json").arg(m_characterId);
}

void TSelectOutfitDialogController::loadPresets()
{
    m_presetArray = {};
    if (m_characterId == 0)
        return;
    QFile file(presetsFilePath());
    if (file.open(QIODevice::ReadOnly))
        m_presetArray = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("presets")).toArray();
}

void TSelectOutfitDialogController::savePresets()
{
    if (m_characterId == 0)
        return;
    const QString path = presetsFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(QJsonObject{ { QStringLiteral("presets"), m_presetArray } }).toJson());
        file.commit();
    }
}

}

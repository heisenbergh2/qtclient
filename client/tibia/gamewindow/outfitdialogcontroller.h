#pragma once

#include "gamewindow/dialogmanager.h"

#include <cip_gen.h>
#include <client/position.h>

#include <QJsonArray>

class Outfit;

namespace tibia::qmlcomponents {
class TOutfitPresetQmlType;
}

namespace tibia::gamewindow {

class TGenericEnterTextDialogController;

/// Outfits, mounts or summons offered by the outfit window (CIP:
/// TCustomiseCharacterModel).
class TCustomiseCharacterModel final : public cip::TCustomiseCharacterModel
{
    Q_OBJECT
public:
    enum ERole {
        OutfitIDRole = Qt::UserRole + 1,
        OutfitNameRole,
        OutfitSourceRole,
        FirstAddOnRole,
        SecondAddOnRole,
        OutfitTypeRole,
        CanBeColorizedRole,
        OfferIDRole,
        OutfitGenderRole,
        OtherGenderOutfitIDRole,
    };

    struct Entry
    {
        int id = 0;
        QString name;
        bool firstAddOn = false;
        bool secondAddOn = false;
        /// TibiaEnums.EOutfitType; the server's mode byte.
        int type = 0;
        bool canBeColorized = false;
        int offerId = 0;
        /// TSelectOutfitDialogController::EOutfitGender.
        int gender = 2;
        int otherGenderId = 0;
    };

    using cip::TCustomiseCharacterModel::TCustomiseCharacterModel;

    void setEntries(QList<Entry> entries);
    const QList<Entry>& entries() const { return m_entries; }
    /// Owned entry with `id`, or nullptr.
    const Entry* ownedEntry(int id) const;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    QList<Entry> m_entries;
};

/// CIP: TCustomizeCharacterSortProxyModel (filterAcceptsRow 0x100be2140).
class TCustomizeCharacterSortProxyModel final : public cip::TCustomizeCharacterSortProxyModel
{
    Q_OBJECT
public:
    explicit TCustomizeCharacterSortProxyModel(TCustomiseCharacterModel* source, QObject* parent = nullptr);

    void setShowOnlyOwnOutfits(bool v) override;
    void setOutfitNameFilter(const QString& v) override;
    int numberOfOwnedOutfits() const override;
    void resetFilter() override;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
};

/// The character's saved outfit presets (CIP: TOutfitPresetsModel).
class TOutfitPresetsModel final : public cip::TOutfitPresetsModel
{
    Q_OBJECT
public:
    enum ERole { JsonDataRole = Qt::UserRole + 1, EverythingOwnedRole };

    using cip::TOutfitPresetsModel::TOutfitPresetsModel;

    void setPresets(QList<QJsonObject> presets, QList<bool> owned);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    QList<QJsonObject> m_presets;
    QList<bool> m_owned;
};

/// The outfit window, outfitdialog/OutfitDialog.qml (CIP:
/// TSelectOutfitDialogController). Only SelectOutfit mode is served.
class TSelectOutfitDialogController final : public TDialogController<cip::tibia::gamewindow::TSelectOutfitDialogController>
{
    Q_OBJECT
public:
    explicit TSelectOutfitDialogController(QObject* parent = nullptr);
    ~TSelectOutfitDialogController() override;

    struct WindowOptions
    {
        bool mounted = false;
        bool randomMount = false;
        /// Soft ChooseOutfit tryOutfitMode (0 own, 3 hireling, 5 show-off podium).
        int tryOutfitMode = 0;
        /// Male/female outfit pairs (only sent with the both-genders modes).
        QList<QPair<int, int>> genderPairs;
        /// Store offer id per outfit / mount / summon of the message.
        QList<int> outfitOffers;
        QList<int> mountOffers;
        QList<int> summonOffers;
        /// Mode 5 (ConfigureShowOffSocket) podium target.
        Position podiumPosition;
        uint16_t podiumItemId = 0;
        uint8_t podiumStackpos = 0;
        bool podiumVisible = true;
        bool podiumShowOutfit = true;
        uint8_t podiumDirection = 2;
    };

    /// The trailing options of the outfit window message.
    void setWindowOptions(WindowOptions options);
    /// Fills the models from the outfit window message and shows the dialog.
    void open(const Outfit& current, QList<TCustomiseCharacterModel::Entry> outfits, QList<TCustomiseCharacterModel::Entry> mounts,
              QList<TCustomiseCharacterModel::Entry> summons);

    void onCancelClicked() override;
    void onClickedOpenStoreOffer(int offerID) override;
    void onControllerFinished() override;
    void onCharacterPresetsChanged() override;

    void setCharacterConfiguration(cip::tibia::qmlcomponents::TOutfitPresetQmlType* pOutfitPreset, int LookDirectionRaw, bool ShowSocket,
                                   bool MountCharacter) override;
    void outfitPresetsSaveIndex(const QJsonObject& JsonObject, int Index) override;
    void outfitPresetsDeleteIndex(int Index) override;
    void outfitPresetsRenameIndex(int Index) override;
    void copyToClipboard(QObject* pData) override;
    QObject* createFromClipboard() override;

private:
    QString presetsFilePath() const;
    void loadPresets();
    void savePresets();

    TCustomiseCharacterModel m_outfits;
    TCustomiseCharacterModel m_mounts;
    TCustomiseCharacterModel m_summons;
    TCustomizeCharacterSortProxyModel m_outfitsProxy;
    TCustomizeCharacterSortProxyModel m_mountsProxy;
    TCustomizeCharacterSortProxyModel m_summonsProxy;
    TOutfitPresetsModel m_presets;
    tibia::qmlcomponents::TOutfitPresetQmlType* m_preset;
    TGenericEnterTextDialogController* m_renameDialog = nullptr;
    int m_renameIndex = -1;
    quint32 m_characterId = 0;
    QJsonArray m_presetArray;
    WindowOptions m_windowOptions;
};

}

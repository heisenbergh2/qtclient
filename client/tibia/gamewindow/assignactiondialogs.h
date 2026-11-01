#pragma once

#include "config/hotkeyoptions.h"
#include "gamewindow/dialogmanager.h"

#include <cip_gen.h>

#include <QPointer>

namespace tibia::gamewindow {

class TActionBarController;

/// The spells of spells.json for SpellList.qml (CIP: TSpellListDataModel, the
/// roles enumId, name, formulaRaw, iconUrl, iconUrlSmall, castPossible,
/// parameterHints, isCrossHairSpell, groupPrimaryIconUrl,
/// minimumCasterLevel), filtered by filterText.
class TSpellListDataModel final : public cip::tibia::gamewindow::TSpellListDataModel
{
    Q_OBJECT
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
public:
    explicit TSpellListDataModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString filterText() const { return m_filterText; }
    void setFilterText(const QString& text);
    int rowOf(int spellId) const;

Q_SIGNALS:
    void filterTextChanged();

private:
    void rebuild();

    QString m_filterText;
    std::vector<int> m_rows;
};

/// What the assign dialogs share (CIP: TAssignActionDialogControllerBase):
/// the button they write to, "Action Button <bar>.<button>".
template <class Base>
class TAssignActionDialog : public TDialogController<Base>
{
public:
    TAssignActionDialog(const QString& qmlFile, TActionBarController* bar, int buttonId, int multiActionIndex, QObject* parent);

protected:
    config::TActionSetting current() const;
    void assign(const config::TActionSetting& setting);

    QPointer<TActionBarController> m_bar;
    int m_multiActionIndex;
};

/// AssignSpellActionDialog.qml (CIP: TAssignSpellActionDialogController,
/// v37 0x100c928b0). The cast type is kept in the setting; casting only uses
/// None (qtclient has no crosshair or cursor-cast spells).
class TAssignSpellActionDialogController final : public TAssignActionDialog<cip::tibia::gamewindow::TAssignSpellActionDialogController>
{
    Q_OBJECT
public:
    TAssignSpellActionDialogController(TActionBarController* bar, int buttonId, int multiActionIndex, int preselectedSpellId, QObject* parent = nullptr);

public Q_SLOTS:
    void onOkClicked(const QString& Parameters, ::cip::tibia::qmlcomponents::enums::ESpellCastOptionalTargetType CastType) override;
    void onApplyClicked(const QString& Parameters, ::cip::tibia::qmlcomponents::enums::ESpellCastOptionalTargetType CastType) override;
    void spellSelected(uint SpellEnumId) override;
    void onCloseClicked() override;

private:
    TSpellListDataModel m_model;
    int m_spellId = 0;
};

/// AssignObjectActionDialog.qml (CIP: TAssignObjectActionDialogController).
class TAssignObjectActionDialogController final : public TAssignActionDialog<cip::tibia::gamewindow::TAssignObjectActionDialogController>
{
    Q_OBJECT
public:
    TAssignObjectActionDialogController(TActionBarController* bar, int buttonId, int multiActionIndex, QObject* parent = nullptr);

    /// Shows `objectId` with CIP's default use for it.
    void setObject(int objectId, int upgradeTier);

public Q_SLOTS:
    void onOkClicked(uint UseObjectTypeEnumId, bool UseEquipSmartModeEnabled) override;
    void onApplyClicked(uint UseObjectTypeEnumId, bool UseEquipSmartModeEnabled) override;
    /// "Select Object": the next click on the map or an inventory slot picks it.
    void startTargetSelection() override;
    void onCloseClicked() override;
};

/// AssignTextActionDialog.qml (CIP: TAssignTextActionDialogController).
class TAssignTextActionDialogController final : public TAssignActionDialog<cip::tibia::gamewindow::TAssignTextActionDialogController>
{
    Q_OBJECT
public:
    TAssignTextActionDialogController(TActionBarController* bar, int buttonId, int multiActionIndex, QObject* parent = nullptr);

public Q_SLOTS:
    void onOkClicked(const QString& Text, bool SendAutomatically) override;
    void onApplyClicked(const QString& Text, bool SendAutomatically) override;
    void onCloseClicked() override;
};

/// AssignPassiveAbilityActionDialog.qml (CIP:
/// TAssignPassiveAbilityActionDialogController, v37 0x100c93120). qtclient
/// knows no passive abilities: the list is empty.
class TAssignPassiveAbilityActionDialogController final
    : public TAssignActionDialog<cip::tibia::gamewindow::TAssignPassiveAbilityActionDialogController>
{
    Q_OBJECT
public:
    TAssignPassiveAbilityActionDialogController(TActionBarController* bar, int buttonId, QObject* parent = nullptr);

public Q_SLOTS:
    void onOkClicked() override;
    void onApplyClicked() override;
    void passiveAbilitySelected(uint PassiveAbilityEnumId) override;
    void onCloseClicked() override;

private:
    int m_passiveAbility = 0;
};

/// RecordKeyboardShortcutDialog.qml (CIP: TRecordKeyboardShortcutDialogController
/// with TRecordKeyboardShortcutHandler): records the next key while shown;
/// OK binds it to TriggerActionButton_<bar>.<button> in the current chat mode,
/// or only emits keySequenceSelected when constructed for the options page.
class TRecordKeyboardShortcutDialogController final : public TDialogController<cip::tibia::gamewindow::TRecordKeyboardShortcutDialogController>
{
    Q_OBJECT
public:
    TRecordKeyboardShortcutDialogController(int actionBarId, int buttonId, bool chatModeOn, QObject* parent = nullptr);
    /// Options-page capture (CIP +0x3a8): OK emits keySequenceSelected, does not write hotkeys.
    TRecordKeyboardShortcutDialogController(const QString& description, bool chatModeOn, const QKeySequence& current, QObject* parent = nullptr);
    ~TRecordKeyboardShortcutDialogController() override;

public Q_SLOTS:
    void onKeySequenceEntered(const QKeySequence& NewKeySequence) override;
    void onOkButtonClicked() override;
    void onClearButtonClicked() override;
    void onCancelButtonClicked() override;
    void requestClose() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    int m_actionBarId = 0;
    int m_buttonId = 0;
    bool m_bindOnOk = true;
    QString m_actionName;
    QKeySequence m_keys;
};

}

#pragma once

#include "config/hotkeyoptions.h"
#include "game/cooldownstorage.h"

#include <cip_gen.h>

namespace tibia::gamewindow {

class TActionBarController;

/// One button of an action bar, or one slot of a multi-action button
/// (CIP: TActionButtonController; setters FUN_100c8d480 spell, FUN_100c8d810
/// text, FUN_100c8d6f0 object, FUN_100c8d8c0 passive ability; reset
/// FUN_100c8d1d0 / FUN_100c8d530). Passive abilities show their CIP icon but
/// qtclient has no passive ability cooldowns or counters.
class TActionButtonController final : public cip::tibia::gamewindow::TActionButtonController
{
    Q_OBJECT
public:
    /// `multiActionIndex` -1 for a bar button.
    TActionButtonController(TActionBarController* bar, int buttonId, int multiActionIndex, QObject* parent = nullptr);

    TActionBarController* bar() const { return m_bar; }
    bool isMultiActionChild() const { return m_multiActionIndex >= 0; }
    const config::TActionSetting& setting() const { return m_setting; }
    /// A multi-action button's slot, or null.
    TActionButtonController* multiAction(int index) const
    {
        return index >= 0 && index < int(m_children.size()) ? m_children[size_t(index)] : nullptr;
    }

    /// CIP FUN_100c80fc0's per-button branch: shows `setting`; `hotkey` is the
    /// key of TriggerActionButton_<bar>.<button> in the current chat mode.
    void setSetting(const config::TActionSetting& setting, const QKeySequence& hotkey);
    /// CIP FUN_100c90bd0 (+0xd8): every key of the button, listed by the tooltip.
    void setHotkeys(const QList<QKeySequence>& hotkeys) { m_hotkeys = hotkeys; }
    /// The object's amount in the inventory and whether it is worn (CIP:
    /// onInventoryChanged).
    void refreshObject();
    /// CIP FUN_100c80300's per-button part with FUN_100c90db0 / FUN_100c91140:
    /// the remaining cooldown at `now`. Returns whether one runs.
    bool refreshCooldown(qint64 now);
    /// CIP FUN_100c90580 / FUN_100e99e20: the slot of a multi-action button
    /// that runs at `now` (the first usable slot without cooldown, else the one
    /// whose cooldown ends first by more than min(ping / 2, 400) ms, else the
    /// first unusable one), or null.
    TActionButtonController* currentMultiAction(qint64 now) const;
    /// CIP FUN_100c83620's emptiness test (FUN_100c8d9a0: the shown type).
    bool isShownEmpty() const { return type() == EMPTY; }

    /// CIP FUN_100961580: "S.t" below 11 s, "S" below a minute, "M:SS" below
    /// 11 minutes, then "%1m", "%1h%2", "%1h".
    static QString cooldownText(qint64 remainingMs);

    // A multi-action button shows its current slot (CIP: every getter walks
    // FUN_100c90580, e.g. FUN_100c8d9a0 type, FUN_100c8f690 object).
    EQmlActionButtonType type() const override { return shown()->m_type; }
    EQmlActionButtonProgressType progressType() const override { return shown()->m_progressType; }
    QString spellIconUrl() const override { return shown()->m_spellIconUrl; }
    QString spellParameters() const override { return shown()->m_spellParameters; }
    QString text() const override { return shown()->m_text; }
    uint objectID() const override { return shown()->m_objectID; }
    uint objectCount() const override { return shown()->m_objectCount; }
    uint objectUpgradeTier() const override { return shown()->m_objectUpgradeTier; }
    cip::tibia::qmlcomponents::TObjectAppearanceInstanceInfoQmlType::ELiquidType liquidType() const override { return shown()->m_liquidType; }
    cip::tibia::qmlcomponents::TObjectAppearanceInstanceInfoQmlType::EHookDirection hookDirection() const override
    {
        return shown()->m_hookDirection;
    }
    bool objectIsEquiped() const override { return shown()->m_objectIsEquiped; }
    bool isEquipeAction() const override { return shown()->m_isEquipeAction; }
    bool isEnabled() const override { return shown()->m_isEnabled; }
    QString passiveAbilityIconUrl() const override { return shown()->m_passiveAbilityIconUrl; }
    uint cooldownRemainingPercent() const override { return shown()->m_cooldownRemainingPercent; }
    QString cooldownText() const override { return shown()->m_cooldownText; }
    /// CIP FUN_100c8da40.
    QString tooltip() const override;

public Q_SLOTS:
    /// CIP 0x100c91270: publishes ACTION_BUTTON_TRIGGERED.
    void onTriggered() override;
    /// CIP FUN_100c7ad10.
    void contextMenuRequested() override;
    void onTargetSelected() override;
    void onDragChanged(bool Dragged) override;
    void setMultiActionExpanded(bool v) override;

private:
    void clear();
    const TActionButtonController* shown() const { return m_shownSlot ? m_shownSlot : this; }
    /// Re-selects the shown slot; announces a change.
    void refreshShownSlot(qint64 now);
    /// CIP FUN_100c8dcc0: the tooltip's table rows for this button or slot.
    QString tooltipRows() const;
    /// CIP FUN_100e99ef0.
    bool isUsable() const;
    /// CIP FUN_100e9a0a0: the cooldown left at `now` in ms.
    qint64 remainingCooldown(qint64 now) const;
    game::TCooldown currentCooldown() const;
    template <class T>
    void set(T& member, const T& value, bool& changed);

    TActionBarController* m_bar;
    config::TActionSetting m_setting;
    QKeySequence m_hotkey;
    QList<QKeySequence> m_hotkeys;
    int m_spellId = 0;
    bool m_showsCooldown = false;
    std::vector<TActionButtonController*> m_children;
    TActionButtonController* m_shownSlot = nullptr;
};

}

#pragma once

#include "client/gamecore.h"

#include <cip_gen.h>

#include <QPointer>
#include <QTimer>

#include <map>
#include <vector>

class QQuickItem;

namespace tibia::gamewindow {

/// One premium feature line (CIP: game::TPremiumMessage, ctor 0x100b576c0):
/// "premium_feature_*" / "premium_*" text and its premium-icons-20x20 cell.
class TPremiumMessage : public cip::tibia::game::TPremiumMessage
{
    Q_OBJECT
public:
    TPremiumMessage(int id, QObject* parent);
};

/// The premium time line, feature 13 (CIP: game::TPremiumMessageRenewPremium,
/// message 0x100b5ab10: "premium_time_expires(_plural)" with the days left).
class TPremiumMessageRenewPremium final : public cip::tibia::game::TPremiumMessageRenewPremium
{
    Q_OBJECT
public:
    explicit TPremiumMessageRenewPremium(QObject* parent);
    /// CIP 0x100b5abd0.
    void setDaysLeft(int days);
    QString message() const override;

private:
    int m_daysLeft = 0;
};

/// PremiumPanel.qml (CIP: gamewindow::TPremiumPanelController, ctor
/// 0x100c482c0, item 0x100c49130): the features the server says a free
/// account just missed (premium trigger message), highlighted until the
/// player looks at them. Shown at login when advertisePremium() holds,
/// toggled by TOGGLE_PREMIUM_PANEL (74) and hidden while the tutorial mode is
/// on (CHANGE_TUTORIAL_MODE_ENABLED, 223).
///
/// The premium time is CIP's game::TPremiumTimeStorage (vtable 0x10262b368),
/// fed from the player basic data (premium flag and expiration; CIP's /// parsePlayerInfo passes them as g_game.onPremiumExpiration).
class TPremiumPanelController final : public cip::tibia::gamewindow::TPremiumPanelController
{
    Q_OBJECT
public:
    TPremiumPanelController();
    ~TPremiumPanelController() override;

    void setPanelItem(QQuickItem* item);

public Q_SLOTS:
    /// CIP 0x100c48600: the highlight timer ran out.
    void stopHighlight() override;
    /// CIP 0x100c48fd0: the renew line's days left.
    void onPremiumTimeChanged() override;
    /// CIP 0x100c48750.
    void onIsVisibleChanged() override;

public:
    /// CIP 0x100c487f0 (session set): CIP's premium time storage already holds
    /// the login service's premium state here; the in-game premium packet
    /// arrives after the game start.
    void onSessionStarted(bool premium, qint64 premiumUntil);

    Q_INVOKABLE void maximizePanel() override;
    Q_INVOKABLE void minimizePanel() override;

private:
    /// CIP 0x100c48b40: the triggered features become the messages.
    void setTriggers(const std::vector<uint8_t>& features);
    /// CIP 0x100c48d70, the store's SetStoreButtonDeeplink (0xA8): a deep
    /// link to anything but the home page or the XP boost (detail 1)
    /// highlights the maximize button for 10 minutes.
    void onStoreDeeplinkChanged(int openAction, int detail);
    /// CIP 0x100c48e70.
    void setMaximized(bool maximized);
    /// CIP 0x100c49240: highlights unless maximized or already highlighted
    /// for longer than `minimumMs`.
    void startHighlight(int minimumMs);
    void setHighlight(bool highlight);

    /// CIP TPremiumTimeStorage v0x60 (0x100b47bf0).
    bool isPremium() const { return m_premium; }
    /// v0x68 (0x100b47c10): premium without an expiration.
    bool hasUnlimitedPremium() const { return m_premiumExpiration == 0 && m_premium; }
    /// v0x70 (0x100b47c30): premium, or an expiration is known.
    bool hasPremiumTime() const { return m_premiumExpiration != 0 || m_premium; }
    /// v0x78 (0x100b47c50): the state (v0x88, 0x100b47c70: 2 unlimited,
    /// else the premium flag) is 0.
    bool isFreeAccount() const { return !hasUnlimitedPremium() && !m_premium; }
    /// v0x80 (0x100b47ca0): whole days from when the time was set to the
    /// expiration.
    int premiumDaysLeft() const;
    /// v0x90 (0x100b47cd0) / v0x98 (0x100b47d50).
    void setPremiumTime(bool premium, uint32_t expiration);
    void resetPremiumTime();

    /// CIP 0x100c48df0 (inlined in 0x100c487f0 and 0x100c49550): a free
    /// account that never had premium, or premium ending in under 4 days.
    bool advertisePremium() const;
    /// CIP 0x100c487f0 at a session start: collapses and shows the panel when
    /// advertisePremium(); hidden without a game.
    void applySessionVisibility();
    /// CIP 0x100c493a0 (TOGGLE_PREMIUM_PANEL): hides a shown panel, collapses
    /// and shows a hidden one; a free account that never had premium cannot
    /// toggle it.
    void togglePanel();
    /// CIP 0x100c49550, action 223: the tutorial mode hides the panel; leaving
    /// it shows the panel again when advertisePremium().
    void onTutorialModeChanged(bool enabled);
    void showCollapsed();
    void setPanelVisible(bool visible);

    bool m_premium = false;
    uint32_t m_premiumExpiration = 0;
    uint32_t m_premiumTimeSetAt = 0;
    std::map<int, cip::tibia::game::TPremiumMessage*> m_messages;
    TPremiumMessageRenewPremium* m_renewPremium = nullptr;
    QPointer<QQuickItem> m_item;
    QTimer m_highlightTimer;
    std::vector<client::gamecore::Hook> m_hooks;
};

}

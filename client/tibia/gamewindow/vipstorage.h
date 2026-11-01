#pragma once

#include "client/gamecore.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace tibia::gamewindow {

/// The VIP list and its groups (CIP: TVipStorage, fed by the VIP protocol
/// messages, plus TVipGameActionHandler's VIP actions). VIPs come from
/// g_game's list; the groups only arrive with onVipGroupChange, so they are
/// kept here.
class TVipStorage final : public QObject
{
    Q_OBJECT
public:
    /// CIP EVipState (creaturetypes.h): 0 offline, 1 online, 2 pending,
    /// 3 exercise weapon training, 4 online, 5 offline. The server sends 0 to 2.
    struct Vip
    {
        uint32_t id = 0;
        QString name;
        int state = 0;
        QString description;
        int iconId = 0;
        bool notifyLogin = false;
        std::vector<uint8_t> groups;
    };
    struct Group
    {
        uint8_t id = 0;
        QString name;
        bool editable = false;
    };

    explicit TVipStorage(QObject* parent = nullptr);
    ~TVipStorage() override;

    std::vector<Vip> vips() const;
    std::optional<Vip> vipByName(const QString& name) const;
    const std::vector<Group>& groups() const { return m_groups; }
    std::optional<Group> groupByName(const QString& name) const;
    /// How many more user-defined groups the account may create.
    int groupsLeft() const { return m_groupsLeft; }

    void addVip(const QString& name);
    void removeVip(uint32_t id);
    /// REQUST_VIP_DATA_UPDATE (CIP 0x100be72f0's request).
    void editVip(uint32_t id, const QString& description, int iconId, bool notifyLogin, const std::vector<uint8_t>& groups);
    void addGroup(const QString& name);
    void editGroup(uint8_t id, const QString& name);
    void removeGroup(uint8_t id);

Q_SIGNALS:
    void vipsChanged();
    void groupsChanged();
    /// The server changed a known VIP's state (CIP TVipStorage's state signal,
    /// handled by 0x100bea840); CIP's list already holds `state`.
    void vipStateChanged(uint32_t id, int previousState, int state);

private:
    std::vector<client::gamecore::Hook> m_hooks;
    std::unordered_map<uint32_t, int> m_states;
    std::vector<Group> m_groups;
    int m_groupsLeft = 0;
    QTimer m_vipsChangedTimer;
};

}

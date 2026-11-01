#pragma once

#include "client/gamecore.h"

#include <cip_gen.h>

#include <client/declarations.h>

#include <QString>

#include <vector>

namespace tibia::trade {

/// The trade between two players (CIP: TPlayerTradeStorage, ctor
/// 0x101040270, vtable 0x1026427d8) fed by CIP's OwnTrade /
/// CounterTrade / CloseTrade (CIP's TPlayerTradeProtocolMessageHandler,
/// ctor 0x10103f5e0, vtable 0x102642718).
class TPlayerTradeStorage final : public cip::tibia::trade::TPlayerTradeStorage
{
    Q_OBJECT
public:
    explicit TPlayerTradeStorage(QObject* parent = nullptr);

    bool isOpen() const { return m_open; }
    /// v0x68 / v0x70.
    const QString& ownName() const { return m_ownName; }
    const std::vector<ItemPtr>& ownOffer() const { return m_ownOffer; }
    /// v0x80 / v0x88.
    const QString& counterName() const { return m_counterName; }
    const std::vector<ItemPtr>& counterOffer() const { return m_counterOffer; }

    /// CIP 0x10103f6e0 / 0x10103f730 (rejecting also forgets the trade) /
    /// 0x10103f790.
    void accept() const;
    void reject();
    void look(bool counterOffer, int index) const;

Q_SIGNALS:
    /// CIP TPlayerTradeProtocolMessageHandler::requestOpenPlayerTradeWidget /
    /// requestClosePlayerTradeWidget.
    void requestOpenPlayerTradeWidget();
    void requestClosePlayerTradeWidget();

private:
    /// v0x60 (CIP 0x1010402f0).
    void reset();
    void close();

    bool m_open = false;
    QString m_ownName;
    std::vector<ItemPtr> m_ownOffer;
    QString m_counterName;
    std::vector<ItemPtr> m_counterOffer;
    std::vector<client::gamecore::Hook> m_hooks;
};

}

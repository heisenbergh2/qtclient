#pragma once

#include "client/gamecore.h"

#include <cip_gen.h>

#include <client/declarations.h>

#include <QHash>
#include <QString>

#include <vector>

namespace tibia::trade {

/// One good of the NPC's offer (CIP: the 0x38-byte entry of the open trade
/// message: type +0x00, name +0x10, sell price +0x28, buy price +0x2c,
/// weight +0x30). Prices are what the NPC pays (sell) and what the player
/// pays (buy); weight is in 1/100 oz.
struct TNPCTradeGood
{
    ItemPtr object;
    QString name;
    uint32_t sellPrice = 0;
    uint32_t buyPrice = 0;
    uint32_t weight = 0;

    uint16_t typeId() const;
    /// The liquid of fluid containers and splashes, otherwise 0.
    uint8_t liquidType() const;
    bool isStackable() const;
    /// 0 none, 1 south, 2 east (EHookDirection).
    uint8_t hookDirection() const;
};

enum class ENPCTradeDirection { BUY = 0, SELL = 1 };

/// CIP 0x10103aea0: what the NPC pays for `amount`.
uint64_t sellPrice(const TNPCTradeGood& good, int amount);
/// CIP 0x10103aec0: what `amount` costs, 20 gold per backpack (one per 20
/// objects or 20 stacks of 100) when bought in backpacks.
uint64_t buyPrice(const TNPCTradeGood& good, int amount, bool withBackpacks);
/// CIP 0x10103afc0: the most the player can buy with `money` and `capacity`.
int maximumBuyAmount(const TNPCTradeGood& good, uint64_t money, uint32_t capacity, bool ignoreCapacity, bool withBackpacks);
/// CIP 0x10103af50: the most the player can sell of `owned` objects.
int maximumSellAmount(const TNPCTradeGood& good, uint32_t owned);

/// The open NPC trade (CIP: TNPCTradeStorage, ctor 0x10103d1e0, vtable
/// 0x102642500) fed by CIP's OpenNPCTrade / PlayerGoods / CloseNPCTrade
/// (CIP's TNPCTradeProtocolMessageHandler 0x10103bb60).
class TNPCTradeStorage final : public cip::tibia::trade::TNPCTradeStorage
{
    Q_OBJECT
public:
    explicit TNPCTradeStorage(QObject* parent = nullptr);

    bool isOpen() const { return m_open; }
    const QString& npcName() const { return m_npcName; }

    /// v0x68 (FUN_10103b3d0): the goods the NPC sells (BUY) or buys (SELL).
    std::vector<TNPCTradeGood> goods(ENPCTradeDirection direction) const;
    /// v0x88 / v0x90.
    ENPCTradeDirection direction() const { return m_direction; }
    void setDirection(ENPCTradeDirection direction);
    /// v0x70: the player's free capacity in 1/100 oz.
    uint32_t freeCapacity() const;
    /// v0x80: the player's objects of the good's type, equipped ones only
    /// with `includeEquipped`.
    uint32_t ownedCount(const TNPCTradeGood& good, bool includeEquipped) const;

    /// v0xa8 / v0xb0 / v0xb8: a currency other than gold, as an object type
    /// or a name.
    bool hasCurrency() const { return currencyIsItem() || currencyIsName(); }
    bool currencyIsItem() const { return m_currencyTypeId != 0; }
    bool currencyIsName() const { return m_currencyTypeId == 0 && !m_currencyName.isEmpty(); }
    uint16_t currencyTypeId() const { return m_currencyTypeId; }
    const QString& currencyName() const { return m_currencyName; }
    /// CIP 0x100bef8d0: gold (bank and inventory), the currency objects
    /// carried, or the named resource.
    uint64_t money() const;
    uint64_t bankBalance() const;
    uint64_t inventoryBalance() const;

    /// CIP 0x10103be60 / 0x10103bfe0 / 0x10103c100 / 0x10103bc60.
    void look(const TNPCTradeGood& good) const;
    void buy(const TNPCTradeGood& good, int amount, bool ignoreCapacity, bool withBackpacks) const;
    void sell(const TNPCTradeGood& good, int amount, bool ignoreEquipped) const;
    /// Sends CloseNPCTrade and closes the trade without waiting for the
    /// server's answer.
    void close();

Q_SIGNALS:
    /// CIP TNPCTradeProtocolMessageHandler::requestOpenNpcTrade /
    /// requestCloseNpcTrade.
    void requestOpenNpcTrade();
    void requestCloseNpcTrade();

private:
    void reset();

    bool m_open = false;
    QString m_npcName;
    QString m_pendingNpcName;
    uint16_t m_pendingCurrencyTypeId = 0;
    QString m_pendingCurrencyName;
    std::vector<TNPCTradeGood> m_goods;
    ENPCTradeDirection m_direction = ENPCTradeDirection::BUY;
    uint16_t m_currencyTypeId = 0;
    QString m_currencyName;
    uint64_t m_goldTotal = 0;
    QHash<uint16_t, uint32_t> m_playerGoods;
    std::vector<client::gamecore::Hook> m_hooks;
};

}

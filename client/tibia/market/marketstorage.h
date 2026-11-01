#pragma once

#include "client/gamecore.h"

#include <cip_gen.h>

#include <QString>

#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

namespace tibia::market {

/// CIP: TMarketOffer (CIP's MarketOffer).
struct TMarketOffer
{
    uint32_t timestamp = 0;
    uint16_t counter = 0;
    /// 0 buy, 1 sell.
    uint8_t type = 0;
    uint16_t typeId = 0;
    uint8_t tier = 0;
    uint16_t amount = 0;
    uint64_t price = 0;
    QString bidder;
    /// 0 active, 1 cancelled, 2 expired, 3 accepted.
    uint8_t state = 0;
};

/// The statistics of one offer type (CIP 0x100ea5410's aggregation of the
/// daily entries).
struct TMarketStatistics
{
    uint64_t transactions = 0;
    uint64_t total = 0;
    uint64_t highest = 0;
    uint64_t lowest = 0;
    /// Days with at least one transaction.
    int days = 0;
};

/// CIP: TMarketItemDetails.
struct TMarketItemDetails
{
    uint16_t typeId = 0;
    uint8_t tier = 0;
    bool hasTier = false;
    /// CIP's Otc::MarketItemDescription -> text.
    std::unordered_map<int, QString> descriptions;
    TMarketStatistics buy;
    TMarketStatistics sell;
};

/// The market's game data (CIP: TMarketStorage, a TObjectCountStorage of the
/// depot counts; TMarketProtocolMessageHandler's protocol hooks).
class TMarketStorage final : public cip::tibia::market::TMarketStorage
{
    Q_OBJECT
public:
    /// lastBrowse of the history and own offers (CIP: -1 / -2; item browses
    /// use the type id).
    static constexpr int HistoryBrowse = -1;
    static constexpr int OwnOffersBrowse = -2;
    /// CIP 0x100e9c160: canCreateNewOffer below 100 active offers.
    static constexpr int MaximumActiveOffers = 100;
    /// CIP 0x100ea1720: the most of the type one offer can hold.
    static int maximumOfferAmount(uint16_t typeId);

    explicit TMarketStorage(QObject* parent = nullptr);

    /// The depot count of the type (CIP: TObjectCountStorage, keyed by type and tier).
    int objectCount(uint16_t typeId, uint8_t tier) const;
    int activeOfferCount() const { return m_activeOfferCount; }
    const std::vector<TMarketOffer>& offers() const { return m_offers; }
    const std::vector<TMarketOffer>& history() const { return m_history; }
    const std::vector<TMarketOffer>& ownOffers() const { return m_ownOffers; }
    const TMarketItemDetails& itemDetails() const { return m_details; }
    /// forgeData's classes: classification -> tiers (sorted).
    const std::map<int, std::vector<int>>& classificationTiers() const { return m_classTiers; }
    int coinsPacketSize() const { return m_coinsPacketSize; }

    void browseItem(uint16_t typeId, uint8_t tier);
    void browseHistory();
    void browseOwnOffers();
    void leave();

Q_SIGNALS:
    void requestOpenMarketDialog();
    void requestCloseMarketDialog();
    void requestMarketMessageDialog(const QString& Message);
    void itemDetailsChanged();
    void offersChanged();
    void historyChanged();
    void ownOffersChanged();
    void activeOfferCountChanged();
    void classificationsChanged();

private:
    void onMarketEnter(const std::vector<std::vector<uint16_t>>& depotItems, int offers);
    void onMarketDetail(const std::vector<std::any>& args);
    void onMarketBrowse(const std::vector<std::vector<uint64_t>>& offers, const std::vector<std::string>& names);
    void onMarketReadOffer(const std::vector<std::any>& args);
    /// Replaces the list on a new browse, merges on the same one (by
    /// timestamp and counter; amount 0 removes).
    static void merge(std::vector<TMarketOffer>& list, std::vector<TMarketOffer> received, bool replace);

    std::unordered_map<uint32_t, int> m_objectCounts;
    int m_activeOfferCount = 0;
    std::vector<TMarketOffer> m_offers;
    std::vector<TMarketOffer> m_history;
    std::vector<TMarketOffer> m_ownOffers;
    TMarketItemDetails m_details;
    std::map<int, std::vector<int>> m_classTiers;
    int m_coinsPacketSize = 1;
    int m_lastBrowse = 0;
    int m_requestedBrowse = 0;
    uint8_t m_requestedTier = 0;
    bool m_open = false;
    std::vector<client::gamecore::Hook> m_hooks;
};

}

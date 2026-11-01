#pragma once

#include "client/gamecore.h"

#include <cip_gen.h>

#include <QString>
#include <QStringList>
#include <QUrl>

#include <optional>
#include <vector>

namespace tibia::store {

/// One price of an offer (CIP: the 0x40-byte quantity configuration of
/// TStoreOffer; CIP's SubOffer).
struct TStoreQuantity
{
    uint32_t offerId = 0;
    uint16_t quantity = 0;
    uint32_t price = 0;
    /// 0 Tibia coins, 1 transferable, 2 tournament.
    uint8_t coinType = 0;
    bool disabled = false;
    QStringList disabledReasons;
    /// 0 none, 1 new, 2 sale, 3 timed.
    uint8_t state = 0;
    uint32_t validUntil = 0;
    uint32_t basePrice = 0;
};

/// What an offer shows (CIP: TStoreVisualisation; CIP's /// GameStoreInfoType_t, numbered like EQmlVisualisationType).
struct TStoreVisual
{
    enum Type : uint8_t { ImageIdentifier = 0, Mount = 1, Outfit = 2, Object = 3, BothGenderOutfits = 4 };

    Type type = ImageIdentifier;
    QString imageIdentifier;
    int typeId = 0;
    int maleTypeId = 0;
    int femaleTypeId = 0;
    uint8_t head = 0;
    uint8_t body = 0;
    uint8_t legs = 0;
    uint8_t feet = 0;
};

/// One product of a bundle offer (CIP: TStoreProduct, translator v139).
struct TStoreProduct
{
    QString name;
    TStoreVisual visual;
};

/// CIP: TStoreOffer.
struct TStoreOfferData
{
    QString name;
    std::vector<TStoreQuantity> quantities;
    TStoreVisual visual;
    std::vector<TStoreProduct> products;
    bool needsUserConfiguration = false;
    uint8_t tryOnType = 0;
    uint16_t popularity = 0;
    uint32_t newUntil = 0;
    QString description;

    bool hasQuantity(uint32_t offerId) const;
};

/// CIP: TStoreCategory.
struct TStoreCategoryData
{
    QString name;
    QStringList imageIdentifiers;
    std::vector<TStoreCategoryData> subcategories;
};

/// One line of the transaction history (CIP's onParseStoreGetHistory
/// tuple).
struct TStoreHistoryEntry
{
    uint32_t time = 0;
    /// 0 normal, 1 gift, 2 refund.
    uint8_t mode = 0;
    int32_t amount = 0;
    uint8_t coinType = 0;
    QString description;
    uint32_t transactionId = 0;
    bool detailsAvailable = false;
    QString details;
};

/// The store's data (CIP: TStoreStorage, ctor 0x100ff9660, metacall
/// 0x10086fa20) fed by CIP's store events, and the store requests to the
/// server.
class TStoreStorage final : public cip::tibia::store::TStoreStorage
{
    Q_OBJECT
public:
    explicit TStoreStorage(QObject* parent = nullptr);

    const std::vector<TStoreCategoryData>& categories() const { return m_categories; }
    const std::vector<TStoreOfferData>& offers() const { return m_offers; }
    const QString& offersCategoryName() const { return m_offersCategoryName; }
    /// The home page's banner images, relative to the image base URL.
    const QStringList& banners() const { return m_banners; }
    /// The offer each banner shows.
    const QList<uint32_t>& bannerOfferIds() const { return m_bannerOfferIds; }
    int bannerDelaySeconds() const { return m_bannerDelaySeconds; }
    /// v200: the offer one of whose quantities is `offerId`.
    const TStoreOfferData* offerById(uint32_t offerId) const;

    bool balanceValid() const { return m_balanceValid; }
    bool balanceUpdating() const { return m_balanceUpdating; }
    /// v0x120.
    void setBalanceUpdating(bool updating);
    uint64_t coins() const { return m_coins; }
    uint64_t transferableCoins() const { return m_transferableCoins; }
    uint64_t reservedCoins() const { return m_reservedCoins; }

    uint32_t historyPage() const { return m_historyPage; }
    uint32_t historyPageCount() const { return m_historyPageCount; }
    const std::vector<TStoreHistoryEntry>& history() const { return m_history; }

    /// The protocol requests (CIP: TStoreGameActionHandler, v0x60 with the
    /// request types 0 home, 2 category, 4 offer, 5 search).
    void openStore();
    void requestHome();
    void requestCategory(const QString& name);
    void requestOffer(uint32_t offerId);
    void requestSearch(const QString& text);
    /// Request type 1 (detail 1 the boosts, else premium time). qtclient only
    /// sends the boosts variant, so premium asks for the server's premium
    /// category by name (Crystal: "VIP Shop" with its VIP system, else
    /// "Premium Time").
    void requestPremiumOrBoosts(bool boosts);
    /// Request type 3: the useful-things offer `subAction` (Crystal
    /// GameStore.SubActions: prey slot, wildcards, blessings...).
    void requestUsefulThing(int subAction);
    /// v0x68.
    void buy(uint32_t offerId);
    /// v0x70 / v0x90: the buy with its purchase data (1 a new character name,
    /// 3 a hireling's name and sex).
    void buyConfigured(uint32_t offerId, uint8_t purchaseType, const QString& name, uint8_t sex = 0);
    /// v0x98 / v0xa0 (26 entries per page).
    void openTransactionHistory();
    void requestTransactionHistoryPage(uint32_t page);
    /// The coin transfer (Tibia's TransferCurrency, 0xEF).
    void transferCoins(const QString& recipient, uint32_t amount);

Q_SIGNALS:
    /// The offers of a category (or the search results) arrived; the server
    /// asks to show `offerId` when it is not 0.
    void offersChanged(int offerId);
    void offerDescriptionChanged(int offerId);

private:
    void setOffers(const std::vector<TStoreOfferData>& offers, const QString& categoryName);
    void readBalance();

    std::vector<client::gamecore::Hook> m_hooks;
    std::vector<TStoreCategoryData> m_categories;
    std::vector<TStoreOfferData> m_offers;
    QString m_offersCategoryName;
    QStringList m_banners;
    QList<uint32_t> m_bannerOfferIds;
    int m_bannerDelaySeconds = 0;
    bool m_balanceValid = false;
    bool m_balanceUpdating = false;
    uint64_t m_coins = 0;
    uint64_t m_transferableCoins = 0;
    uint64_t m_reservedCoins = 0;
    uint32_t m_historyPage = 0;
    uint32_t m_historyPageCount = 0;
    std::vector<TStoreHistoryEntry> m_history;
};

/// The store's server notifications (CIP: TStoreProtocolMessageHandler,
/// ctor 0x1010004d0): the image base URL of the login message, purchase
/// results, store errors and the store button indicators (slot 35,
/// 0x101004a50: sale then new).
class TStoreProtocolMessageHandler final : public cip::tibia::store::TStoreProtocolMessageHandler
{
    Q_OBJECT
public:
    explicit TStoreProtocolMessageHandler(QObject* parent = nullptr);

    const QUrl& imageBaseUrl() const { return m_imageBaseUrl; }
    /// The login message's coin packet size: coins transfer in multiples of it.
    qint64 creditPacketSize() const { return m_creditPacketSize; }

Q_SIGNALS:
    /// requestCharacterNameChangeInputDialog / requestHirelingInputDialog.
    void characterNameChangeRequested(int offerId);
    void hirelingConfigurationRequested(int offerId);
    /// CIP HirelingNameChange (S 0xDB) → enter name → SetHirelingName (C 0xEC).
    void hirelingNameChangeRequested(quint32 creatureId, quint32 objectId);

private:
    std::vector<client::gamecore::Hook> m_hooks;
    QUrl m_imageBaseUrl;
    qint64 m_creditPacketSize = 1;
};

}

#pragma once

#include "client/gamecore.h"
#include "exaltation/exaltationstorage.h"
#include "store/storestorage.h"

#include <cip_gen.h>

#include <QPointer>

#include <vector>

namespace tibia::gamewindow {

/// The `storeAndResourceBalanceHelper` context property (CIP:
/// TStoreAndResourceBalanceHelperController, ctor 0x100d61af0, metacall
/// 0x100844920): the gold, coins and other currencies the dialogs show. Read
/// from CIP's resource balances (guild bank = ResourceBalance type 0x02).
/// The dust maximum is 100 until an exaltation storage is set.
class TStoreAndResourceBalanceHelperController final : public cip::tibia::gamewindow::TStoreAndResourceBalanceHelperController
{
    Q_OBJECT
public:
    explicit TStoreAndResourceBalanceHelperController(store::TStoreStorage* storage, QObject* parent = nullptr);

    /// CIP 0x100d633a0.
    Q_INVOKABLE bool canAffordWithTotalGoldBalance(int Price) override;

    /// CIP 0x100d62c70: exaltedDustMaximum is the storage's current dust limit.
    void setExaltationStorage(exaltation::TExaltationStorage* storage);

private:
    /// CIP 0x100d627b0.
    void updateGold();
    /// CIP 0x100d629b0.
    void updateStoreBalance();
    /// CIP 0x100d62b10 / 0x100d62bc0 / 0x100d62c70 / 0x100d62db0 / 0x100d62fd0.
    void updateResources();

    QPointer<store::TStoreStorage> m_storage;
    QPointer<exaltation::TExaltationStorage> m_exaltation;
    std::vector<client::gamecore::Hook> m_hooks;
};

}

#pragma once

#include "client/gamecore.h"

#include <cip_gen.h>

#include <client/declarations.h>

#include <QPointer>
#include <QTimer>

#include <map>
#include <set>
#include <vector>

class QQmlEngine;
class QQuickItem;

namespace tibia::config {
class TSidebarOptions;
}

namespace tibia::gamewindow {

class TContainerController;
class TSidebarManager;
class TSidebarWidget;

/// The open containers' widgets (CIP: TContainerStorageController, vtable
/// 0x10263c7a8): one TContainerController per container id, which a newly
/// opened container's widget joins the sidebars from the next event loop
/// pass (CIP 0x100b84410). Until the login ramp-up finishes they wait.
/// The sidebar widgets manager places them in their saved order
/// (OPEN_CONTAINER_IN_SPECIFIC_SIDEBAR). CIP creates the controller of a
/// container that is not open yet; here its place waits for the container
/// (servers that do not reopen containers on login would leave it empty).
class TContainerStorageController final : public cip::tibia::gamewindow::TContainerStorageController
{
    Q_OBJECT
public:
    TContainerStorageController(TSidebarManager* sidebars, QQuickItem* gameWindowItem, QQmlEngine* engine, QObject* parent = nullptr);
    ~TContainerStorageController() override;

    void setControlScheme(int controlScheme);
    void setLootScheme(int lootScheme);
    void setSidebarOptions(config::TSidebarOptions* options);
    /// When set (not 0xff) and that sidebar exists, new bags open there (product HudContainers).
    void setForcedSidebarId(uint sidebarId);
    uint forcedSidebarId() const { return m_forcedSidebarId; }

    /// A place waiting for its container: the sidebar, the index in its
    /// saved widget list (-1: appended) and the sidebar identifier reserved
    /// for it (0: none), so it opens where CIP's placeholder widget stood.
    struct PendingPlace
    {
        uint sidebarId;
        int position;
        uint identifier = 0;
    };

    /// CIP 0x100b83e40: the container's widget moves to `sidebarId` (0xff or
    /// a missing sidebar: the first with room).
    void openInSidebar(int containerId, uint sidebarId, int position = -1);
    /// Close every open container currently parented in `sidebarId` (product inventory tabs).
    void closeInSidebar(uint sidebarId);
    /// True when any container widget is parented in `sidebarId`.
    bool hasWidgetsInSidebar(uint sidebarId) const;
    /// The open containers' widgets by container id.
    std::vector<std::pair<int, TSidebarWidget*>> widgets() const;
    /// Places of containers the server has not opened, by container id. CIP
    /// shows an empty widget there (FUN_100b83f80); here they only stay in
    /// the saved sidebars until the container opens or the game ends.
    const std::map<int, PendingPlace>& pendingPlaces() const { return m_pendingPlaces; }
    /// CIP restore (0x100bc6b90): every container widget leaves its sidebar
    /// until openInSidebar places it or endRestore queues it again.
    void beginRestore();
    void endRestore();

public Q_SLOTS:
    void onLoginRampUpFinished() override;

Q_SIGNALS:
    /// Fired when a container widget is added or removed (product floats them in fullscreen).
    void containerWidgetsChanged();

private:
    void onContainerOpened(const ContainerPtr& container);
    /// qtclient closes a replaced container after opening its successor.
    void onContainerClosed(int containerId);
    void scheduleUpdate(int containerId);
    void flushUpdates();
    void addQueuedWidgets();
    void clear();

    TSidebarManager* m_sidebars;
    QQuickItem* m_gameWindowItem;
    QQmlEngine* m_engine;
    QPointer<config::TSidebarOptions> m_options;
    int m_controlScheme = 0;
    int m_lootScheme = 1;
    uint m_forcedSidebarId = 0xff;
    bool m_loginRampUpFinished = false;
    bool m_manualSort = false;
    std::map<int, TContainerController*> m_controllers;
    std::vector<int> m_queuedWidgets;
    std::map<int, PendingPlace> m_pendingPlaces;
    std::set<int> m_unplaced;
    QTimer m_queueTimer;
    std::set<int> m_dirty;
    QTimer m_updateTimer;
    std::vector<client::gamecore::Hook> m_hooks;
};

}

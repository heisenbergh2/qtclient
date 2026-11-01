#pragma once

#include "gamewindow/appearanceinstancelistmodel.h"
#include "gamewindow/sidebarwidgetcontrollerbase.h"
#include "input/gameaction.h"
#include "input/gameactionmapper.h"

#include <cip_gen.h>

#include <client/declarations.h>

#include <QPointer>
#include <QTimer>

namespace tibia::config {
class TSidebarOptions;
}

namespace tibia::gamewindow {

/// One open container's sidebar widget, container.qml (CIP:
/// TContainerController, vtable 0x102637588). CIP's protocol carries
/// no depot search, manual sort, view filter or "more pages" flags, so their
/// buttons stay hidden (qtclient protocol gaps).
class TContainerController final : public TSidebarWidgetController<cip::tibia::gamewindow::TContainerController>
{
    Q_OBJECT
public:
    TContainerController(int containerId, QQuickItem* gameWindowItem, QQmlEngine* engine, QObject* parent = nullptr);

    int containerId() const { return m_containerId; }
    void setControlScheme(int controlScheme) { m_mapper.setControlScheme(controlScheme); }
    void setLootScheme(int lootScheme) { m_mapper.setLootScheme(lootScheme); }
    /// CIP TOGGLE_MANUAL_SORT_MODE: orange caption + slot drop markers.
    void setManualSortMode(bool enabled)
    {
        if (m_isInManualSortMode == enabled)
            return;
        m_isInManualSortMode = enabled;
        Q_EMIT containerPropertyChanged();
    }
    /// CIP 0x100b86350: restores the widget's size from the character's
    /// containersOptions and follows them.
    void setSidebarOptions(config::TSidebarOptions* options);
    /// CIP 0x100b86650: the container's state, the scroll view back at the
    /// top when it shows another container.
    void update(const ContainerPtr& container, bool resetScrollView);

public Q_SLOTS:
    /// CIP 0x100b8adb0.
    void onRequestClose() override;
    /// CIP 0x100b8ae50.
    void onLevelUpClicked() override;
    /// CIP 0x100b87d00.
    void saveWidgetOptions() override;
    /// CIP 0x100b87f20: a closed container forgets its size.
    void removeThisContainersOptions() override;

private Q_SLOTS:
    void onSlotClicked(int slotId, int mouseButton, int keyboardModifiers);
    void onSlotEntered(int slotId);
    void onSlotExited(int slotId);
    void onSlotStartDrag(int slotId, QObject* source);
    void onSlotDragDropped(int slotId);
    void onSlotTargetSelected(int slotId);
    /// CIP 0x100b8aee0 / 0x100b8af60.
    void onPreviousPageClicked();
    void onNextPageClicked();
    /// CIP 0x100b8afe0.
    void onSearchButtonClicked();
    /// CIP 0x100b88050.
    void applyWidgetOptions();

private:
    ContainerPtr container() const;
    ItemPtr slotItem(int slotId) const;
    /// CIP's container game action provider: the slot's object's actions.
    input::TGameActionList slotActions(int slotId) const;
    void updateMouseCursorForCurrentSlot();
    void readExpireOptions();
    template <class T>
    void setProperty(T& member, const T& value);
    void setIcon(const ItemPtr& item);

    int m_containerId;
    TAppearanceInstanceListModel m_model;
    input::TGameActionMapper m_mapper;
    QPointer<config::TSidebarOptions> m_options;
    QTimer m_propertyTimer;
    QTimer m_clockTimer;
    int m_hoveredSlot = -1;
};

}

#pragma once

#include "gamewindow/sidebarwidgetcontrollerbase.h"

#include <cip_gen.h>

#include <QAbstractListModel>
#include <QPointer>

#include <vector>

namespace tibia::config {
class TSidebarOptions;
}

namespace tibia::prey {
class TPreyStorage;
}

namespace tibia::gamewindow {

/// CIP `TKillTrackerWidgetController::TTaskMonsterModel` (ledger short name
/// TTaskMonsterModel) — bounty/weekly task monster rows.
class TTaskMonsterModel final : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role { NameRole = Qt::UserRole + 1, CountRole, RaceIdRole };

    struct Entry {
        QString name;
        int count = 0;
        int raceId = 0;
    };

    explicit TTaskMonsterModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setEntries(std::vector<Entry> entries);

private:
    std::vector<Entry> m_entries;
};

/// The kill tracker widget, prey/KillTrackerWidget.qml (CIP:
/// TKillTrackerWidgetController, ctor 0x100c00860, init 0x100c00e20): one
/// render info per prey slot, built once from the storage, plus the bounty and
/// weekly task lists. Not ported: the task lists stay empty (no task board
/// data in qtclient).
class TKillTrackerWidgetController final : public TSidebarWidgetController<cip::tibia::gamewindow::TKillTrackerWidgetController>
{
    Q_OBJECT
public:
    TKillTrackerWidgetController(prey::TPreyStorage* storage, QQuickItem* gameWindowItem, QQmlEngine* engine, QObject* parent = nullptr);

    /// preyWidgetOptions: contentHeight, contentMaximized.
    void setSidebarOptions(config::TSidebarOptions* options);

    /// CIP 0x100c01eb0: the storage's bonusRerollPossible.
    bool bonusRerollpossible() const override;
    /// CIP 0x100c01ed0: the storage's choosePreyPossible.
    bool lockPreyPossible() const override;

public Q_SLOTS:
    /// CIP 0x100c01f10: OPEN_PREY_DIALOG.
    void requestOpenPreyDialog() override;
    /// CIP 0x100c021f0.
    void saveWidgetOptions() override;

private:
    prey::TPreyStorage* m_storage;
    QPointer<config::TSidebarOptions> m_options;
};

/// Owns the kill tracker widget while it is open. The sidebar widgets manager
/// opens and closes it (EWidgetType 5) through open / close / current.
class TKillTrackerWidgetManager final : public QObject
{
    Q_OBJECT
public:
    TKillTrackerWidgetManager(prey::TPreyStorage* storage, QQuickItem* gameWindowItem, QQmlEngine* engine, QObject* parent = nullptr);
    ~TKillTrackerWidgetManager() override;

    /// Makes the widget (if needed) with the session's sidebar options and returns it.
    TSidebarWidget* open(config::TSidebarOptions* options);
    /// Saves its options and deletes it.
    void close();
    TSidebarWidget* current() const;

private:
    prey::TPreyStorage* m_storage;
    QQuickItem* m_gameWindowItem;
    QQmlEngine* m_engine;
    QPointer<TKillTrackerWidgetController> m_controller;
};

}

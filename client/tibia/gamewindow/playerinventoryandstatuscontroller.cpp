#include "playerinventoryandstatuscontroller.h"

#include "app/clientoptions.h"
#include "container/containerslotinformation.h"
#include "gamewindow/contextmenucontroller.h"
#include "gamewindow/draganddropcontroller.h"
#include "gamewindow/mousecursorcontroller.h"
#include "input/gameactionhandlers.h"
#include "input/genericobjectgameactionprovider.h"
#include "worldmap/worldmapgameactionprovider.h"

#include <tibia/world/world.h>
#include <client/item.h>
#include <tibia/world/localplayer.h>
#include <tibia/assets/thingtype.h>

#include <QGuiApplication>
#include <QQuickItem>

namespace tibia::gamewindow {

namespace {

using Args = std::vector<std::any>;
using A = input::EGameAction;
using Cursor = TMouseCursorController;

constexpr int ContainerContext = input::TGameActionMapper::ContainerContext;

/// CIP 0x100b0f030: the actions the inventory offers on its objects.
constexpr A SlotActionTypes[] = {
    A::LOOK, A::INSPECT_OBJECT, A::OPEN_CYCLOPEDIA, A::OPEN_WEAPON_PROFICIENCY_DIALOG, A::USE, A::USE_WITH, A::OPEN, A::OPEN_IN_NEW_WINDOW,
    A::MOVE_OBJECT, A::TRADE_ITEM, A::TARGET_SELECTED, A::SMART_CLICK, A::OPEN_MANGE_LOOT_CONTAINER, A::LOOT_ADD_TO_BLACKWHITELIST,
    A::LOOT_REMOVE_FROM_BLACKWHITELIST, A::STASH_STOW_CONTAINER_CONTENT, A::STASH_OPEN_STOW_STACK_DIALOG, A::MARKET_BROWSE,
    A::GET_TEXT_FOR_REPORT,
};

/// CIP 0x100b79fc0 (DAT_1022d037c): a click's candidates, in context-menu order.
constexpr A ClickActionTypes[] = {
    A::LOOK, A::INSPECT_OBJECT, A::OPEN_CYCLOPEDIA, A::OPEN_WEAPON_PROFICIENCY_DIALOG, A::USE, A::USE_WITH, A::OPEN_IN_NEW_WINDOW,
    A::CONTEXT_MENU, A::CONTEXT_MENU_SEPARATOR, A::TRADE_ITEM, A::MARKET_BROWSE, A::SMART_CLICK, A::CONTEXT_MENU_SEPARATOR,
    A::OPEN_MANGE_LOOT_CONTAINER, A::LOOT_ADD_TO_BLACKWHITELIST, A::LOOT_REMOVE_FROM_BLACKWHITELIST, A::CONTEXT_MENU_SEPARATOR,
    A::STASH_OPEN_STOW_STACK_DIALOG, A::STASH_STOW_CONTAINER_CONTENT, A::CONTEXT_MENU_SEPARATOR, A::GET_TEXT_FOR_REPORT,
};

/// The store inbox's slot.
constexpr int StoreInboxSlot = 11;
constexpr int RightHandSlot = 5;
constexpr int LeftHandSlot = 6;

/// The slots' background images, by slot id (0 and the store inbox have none).
const char* const SlotBackgrounds[] = {
    nullptr,
    "/images/inventory-head.png",
    "/images/inventory-neck.png",
    "/images/inventory-back.png",
    "/images/inventory-torso.png",
    "/images/inventory-right-hand.png",
    "/images/inventory-left-hand.png",
    "/images/inventory-legs.png",
    "/images/inventory-feet.png",
    "/images/inventory-finger.png",
    "/images/inventory-hip.png",
};

/// The blessings' bits, lowest first (CIP: TBlessingsStorage names).
const char* const BlessingNames[] = {
    "blessing_adventurers_blessing", "blessing_twist_of_fate",      "blessing_wisdom_of_solitude",
    "blessing_spark_of_the_phoenix", "blessing_fire_of_the_suns",   "blessing_spritual_shielding",
    "blessing_embrace_of_tibia",     "blessing_heart_of_the_mountain", "blessing_blood_of_the_mountain",
};

/// The blessing button's colour for the server's visual state, from 1.
const char* const BlessingColors[] = { "grey", "gold", "green" };

constexpr auto ShowExpireOption = "showExpireInInventory";
constexpr auto ShowExpireWhenUnusedOption = "showExpireWhenUnused";
constexpr auto MoveAllOption = "dragAndDropDefaultActionIsMoveAll";
constexpr auto AutoChaseOption = "autoChaseEnabled";

LocalPlayerPtr player() { return tibia::world::player(); }

ItemPtr slotItem(const int slotId)
{
    const LocalPlayerPtr p = player();
    if (!p || slotId < Otc::InventorySlotHead || slotId >= Otc::LastInventorySlot)
        return nullptr;
    return p->getInventoryItem(static_cast<Otc::InventorySlot>(slotId));
}

input::TGameActionPtr firstOfType(const input::TGameActionList& actions, const A type)
{
    for (const input::TGameActionPtr& action : actions) {
        if (action->type == type)
            return action;
    }
    return nullptr;
}

}

TPlayerInventoryAndStatusController::TPlayerInventoryAndStatusController(QObject* parent)
    : cip::tibia::gamewindow::TPlayerInventoryAndStatusController(parent)
    , m_states(config::SpecialConditionsFilter::Bar)
{
    m_playerStates = m_states.model();
    m_showPlayerStatesInBar = m_states.options().showInBar;
    connect(&m_states, &game::TPlayerStatesTracker::optionsChanged, this,
            [this] { setShowPlayerStatesInBar(m_states.options().showInBar); });
    m_blessingButtonColor = QString::fromLatin1(BlessingColors[0]);
    for (int i = 0; i < SlotCount; ++i) {
        auto* slot = new container::TContainerSlotQmlInformation(this);
        slot->assign({ .slotId = static_cast<uint>(i) });
        if (SlotBackgrounds[i])
            slot->setBackgroundImage(QString::fromLatin1(SlotBackgrounds[i]));
        m_slots[i] = slot;
    }
    readExpireOptions();

    const auto onPlayer = [this](auto method) {
        return [this, method](LuaObject* self, const Args&) {
            if (const LocalPlayerPtr& p = player(); p && self == p.get())
                (this->*method)();
            return std::any{};
        };
    };
    m_hooks.emplace_back("object", "onInventoryChange", [this](LuaObject* self, const Args& args) {
        if (const LocalPlayerPtr& p = player(); p && self == p.get()) {
            const auto* slot = args.empty() ? nullptr : std::any_cast<Otc::InventorySlot>(&args[0]);
            refreshSlot(slot ? static_cast<int>(*slot) : static_cast<int>(client::gamecore::argInt(args, 0)));
            refreshDualWielding();
            updateMouseCursorForCurrentSlot();
        }
        return std::any{};
    });
    m_hooks.emplace_back("object", "onSoulChange", onPlayer(&TPlayerInventoryAndStatusController::refreshSoul));
    for (const char* event : { "onFreeCapacityChange", "onTotalCapacityChange", "onBaseCapacityChange" })
        m_hooks.emplace_back("object", event, onPlayer(&TPlayerInventoryAndStatusController::refreshCapacity));
    m_hooks.emplace_back("object", "onBlessingsChange", onPlayer(&TPlayerInventoryAndStatusController::refreshBlessings));
    for (const char* event : { "onChaseModeChange", "onSafeFightChange", "onPVPModeChange" })
        m_hooks.emplace_back("g_game", event, [this](LuaObject*, const Args&) {
            refreshTactics();
            return std::any{};
        });
    m_hooks.emplace_back("g_game", "onGameStart", [this](LuaObject*, const Args&) {
        refreshExpertMode();
        return std::any{};
    });
    m_hooks.emplace_back("g_game", "onGameEnd", [this](LuaObject*, const Args&) {
        m_hoveredSlot = -1;
        refreshSlots();
        Q_EMIT gamesessionDisconnected();
        return std::any{};
    });

    connect(app::ClientOptionsNotifier::instance(), &app::ClientOptionsNotifier::optionChanged, this, [this](const QString& name) {
        if (name == QLatin1String(ShowExpireOption) || name == QLatin1String(ShowExpireWhenUnusedOption)) {
            readExpireOptions();
            refreshSlots();
        }
        if (name == QLatin1String(AutoChaseOption))
            m_autoChaseEnabled = app::ClientOptions::load().options().value(name).toBool(false);
    });
    m_autoChaseEnabled = app::ClientOptions::load().options().value(QLatin1String(AutoChaseOption)).toBool(false);

    m_clockTimer.setInterval(1000);
    connect(&m_clockTimer, &QTimer::timeout, this, &TPlayerInventoryAndStatusController::refreshClocks);
    m_clockTimer.start();

    using input::setActionHandler;
    setActionHandler(A::SET_PVP_MODE, [this](const input::TGameAction& action) { return handleTacticsAction(action); });
    setActionHandler(A::SET_CHASE_OPPONENTS, [this](const input::TGameAction& action) { return handleTacticsAction(action); });
    setActionHandler(A::TOGGLE_CHASE_OPPONENTS, [this](const input::TGameAction& action) { return handleTacticsAction(action); });
    setActionHandler(A::TOGGLE_SECURE_MODE, [this](const input::TGameAction& action) { return handleTacticsAction(action); });
    setActionHandler(A::SET_SECURE_MODE, [this](const input::TGameAction& action) { return handleTacticsAction(action); });
    setActionHandler(A::SHOW_INVENTORY_CAPACITY_WARNING, [this](const input::TGameAction&) {
        if (const LocalPlayerPtr p = player(); p && !p->hasReachedMain())
            Q_EMIT showCapacityWarning();
        return true;
    });
    // CIP handleGameAction 0x100b78710 case 0x94: walking turns chasing off
    // unless auto chase is enabled.
    setActionHandler(A::APPLY_AUTOCHASE_SETTING, [this](const input::TGameAction&) {
        if (!m_autoChaseEnabled && m_chaseEnabled)
            onChaseModeChangeRequested(false);
        return true;
    });
}

TPlayerInventoryAndStatusController::~TPlayerInventoryAndStatusController()
{
    for (const A type : { A::SET_PVP_MODE, A::SET_CHASE_OPPONENTS, A::TOGGLE_CHASE_OPPONENTS, A::TOGGLE_SECURE_MODE, A::SET_SECURE_MODE,
                          A::SHOW_INVENTORY_CAPACITY_WARNING, A::APPLY_AUTOCHASE_SETTING })
        input::setActionHandler(type, nullptr);
}

void TPlayerInventoryAndStatusController::setPanelItem(QQuickItem* panel)
{
    m_panel = panel;
    if (!panel)
        return;
    connect(panel, SIGNAL(slotClicked(int, int, int)), this, SLOT(onSlotClicked(int, int, int)));
    connect(panel, SIGNAL(slotEntered(int)), this, SLOT(onSlotEntered(int)));
    connect(panel, SIGNAL(slotExited(int)), this, SLOT(onSlotExited(int)));
    connect(panel, SIGNAL(slotStartDrag(int, QObject*)), this, SLOT(onSlotStartDrag(int, QObject*)));
    connect(panel, SIGNAL(slotDragDropped(int)), this, SLOT(onSlotDragDropped(int)));
    connect(panel, SIGNAL(slotTargetSelected(int)), this, SLOT(onSlotTargetSelected(int)));
    connect(panel, SIGNAL(stopClicked()), this, SLOT(onStopClicked()));
    QVariantList model;
    for (container::TContainerSlotQmlInformation* slot : m_slots)
        model.append(QVariant::fromValue<QObject*>(slot));
    panel->setProperty("inventoryContainerSlotsModel", model);
}

void TPlayerInventoryAndStatusController::setSidebarOptions(config::TSidebarOptions* options)
{
    m_options = options;
    if (!options)
        return;
    specialConditionsOptionsChanged();
    refreshSlots();
    refreshSoul();
    refreshCapacity();
    refreshBlessings();
    refreshExpertMode();
    refreshTactics();
    applyPlayerTacticsAndInventorySizeFromOptions();
}

bool TPlayerInventoryAndStatusController::inventoryMinimized() const
{
    return m_options && m_options->sidebarPanelsOptions().minimizeInventory;
}

void TPlayerInventoryAndStatusController::setInventoryMinimized(const bool minimized)
{
    if (!m_options || m_options->sidebarPanelsOptions().minimizeInventory == minimized)
        return;
    config::SidebarPanelsOptions panels = m_options->sidebarPanelsOptions();
    panels.minimizeInventory = minimized;
    m_options->setSidebarPanelsOptions(panels);
    Q_EMIT inventoryMinimizedChanged();
}

void TPlayerInventoryAndStatusController::applyPlayerTacticsAndInventorySizeFromOptions()
{
    if (m_options)
        m_chaseEnabled = m_options->sidebarPanelsOptions().chaseModeEnabled;
    Q_EMIT inventoryMinimizedChanged();
    Q_EMIT chaseModeChanged();
    // CIP's login carries the tactics; CIP's server keeps its own until told.
    if (tibia::world::session().isOnline() && m_chaseEnabled != (tibia::world::session().getChaseMode() == Otc::ChaseOpponent))
        publishTactics();
}

void TPlayerInventoryAndStatusController::specialConditionsOptionsChanged()
{
    m_states.reloadOptions();
    setShowPlayerStatesInBar(m_states.options().showInBar);
}

void TPlayerInventoryAndStatusController::setShowPlayerStatesInBar(const bool show)
{
    if (show == m_showPlayerStatesInBar)
        return;
    m_showPlayerStatesInBar = show;
    Q_EMIT showPlayerStatesInBarChanged();
}

input::TGameActionList TPlayerInventoryAndStatusController::slotActions(const int slotId) const
{
    const ItemPtr item = slotItem(slotId);
    if (!item)
        return {};
    const Position position(UINT16_MAX, static_cast<uint16_t>(slotId), 0);
    input::TGameActionList actions =
        input::filterActions(input::TGenericObjectGameActionProvider::objectActions(item, position, 0), SlotActionTypes);
    actions.push_back(input::makeAction(A::CONTEXT_MENU));
    return actions;
}

void TPlayerInventoryAndStatusController::refreshSlot(const int slotId)
{
    if (slotId < 0 || slotId >= SlotCount)
        return;
    const ItemPtr item = slotItem(slotId);
    container::TSlotInfo info;
    info.slotId = static_cast<uint>(slotId);
    info.object = item;
    info.text = container::slotText(item, m_showExpire, m_showExpireWhenUnused);
    info.managedContainerTooltip = container::managedContainerTooltip(item);
    m_slots[slotId]->assign(info);
}

void TPlayerInventoryAndStatusController::refreshSlots()
{
    for (int i = 0; i < SlotCount; ++i)
        refreshSlot(i);
    refreshDualWielding();
}

void TPlayerInventoryAndStatusController::refreshClocks()
{
    if (!m_showExpire)
        return;
    for (container::TContainerSlotQmlInformation* slot : m_slots) {
        if (container::hasClock(slot->object()))
            slot->setText(container::slotText(slot->object(), m_showExpire, m_showExpireWhenUnused));
    }
}

void TPlayerInventoryAndStatusController::readExpireOptions()
{
    const QJsonObject options = app::ClientOptions::load().options();
    m_showExpire = options.value(QLatin1String(ShowExpireOption)).toBool(true);
    m_showExpireWhenUnused = options.value(QLatin1String(ShowExpireWhenUnusedOption)).toBool(false);
}

void TPlayerInventoryAndStatusController::refreshSoul()
{
    const int soul = player() ? player()->getSoul() : 0;
    if (soul == m_soulpoints)
        return;
    m_soulpoints = soul;
    Q_EMIT soulpointsChanged();
}

void TPlayerInventoryAndStatusController::refreshCapacity()
{
    const LocalPlayerPtr p = player();
    const int capacity = p ? static_cast<int>(p->getFreeCapacity()) : 0;
    const bool zero = p && capacity == 0;
    const bool boost = p && p->getBaseCapacity() > 0 && p->getBaseCapacity() < p->getTotalCapacity();
    if (capacity == m_capacity && zero == m_hasZeroCapacity && boost == m_hasCapacityBoost)
        return;
    m_capacity = capacity;
    m_hasZeroCapacity = zero;
    m_hasCapacityBoost = boost;
    Q_EMIT capacityChanged();
}

void TPlayerInventoryAndStatusController::refreshBlessings()
{
    const LocalPlayerPtr p = player();
    const uint blessings = p ? p->getBlessings() : 0;
    const int visualState = p ? p->getBlessVisualState() : 0;

    QStringList names;
    for (size_t bit = 0; bit < std::size(BlessingNames); ++bit) {
        if (blessings & (1u << bit))
            names.append(qtTrId(BlessingNames[bit]));
    }
    const QString tooltip =
        names.isEmpty() ? qtTrId("inventory_blessing_tooltip_empty") : qtTrId("inventory_blessing_tooltip").arg(names.join(QLatin1Char('\n')));
    const int colorIndex = visualState >= 1 && visualState <= static_cast<int>(std::size(BlessingColors)) ? visualState - 1 : 0;
    const QString color = QString::fromLatin1(BlessingColors[colorIndex]);
    const bool adventurers = (blessings & 1) != 0;
    if (tooltip == m_blessings && color == m_blessingButtonColor && adventurers == m_adventurersBlessing)
        return;
    m_blessings = tooltip;
    m_blessingButtonColor = color;
    m_adventurersBlessing = adventurers;
    Q_EMIT blessingChanged();
}

void TPlayerInventoryAndStatusController::refreshTactics()
{
    const bool chase = tibia::world::session().getChaseMode() == Otc::ChaseOpponent;
    const bool secure = tibia::world::session().isSafeFight();
    const int pvpMode = tibia::world::session().getPVPMode();
    if (chase != m_chaseEnabled) {
        m_chaseEnabled = chase;
        Q_EMIT chaseModeChanged();
    }
    if (secure != m_secureModeEnabled) {
        m_secureModeEnabled = secure;
        Q_EMIT secureModeEnabledChanged();
    }
    if (pvpMode != m_pvpMode || m_pvpModeDove != (pvpMode == 0)) {
        m_pvpMode = pvpMode;
        m_pvpModeDove = pvpMode == 0;
        m_pvpModeWhiteHand = pvpMode == 1;
        m_pvpModeYellowHand = pvpMode == 2;
        m_pvpModeRedFist = pvpMode == 3;
        Q_EMIT pvpModeChanged();
    }
}

void TPlayerInventoryAndStatusController::refreshDualWielding()
{
    const ItemPtr left = slotItem(LeftHandSlot);
    const bool dualWield = left && left->isDualWield();
    const bool hasDualWielding = dualWield && !slotItem(RightHandSlot);
    const int objectId = dualWield ? static_cast<int>(left->getId()) : 0;
    if (hasDualWielding == m_hasDualWielding && objectId == m_dualWieldingObjectID)
        return;
    m_hasDualWielding = hasDualWielding;
    m_dualWieldingObjectID = objectId;
    Q_EMIT dualWieldingChanged();
}

void TPlayerInventoryAndStatusController::refreshExpertMode()
{
    const bool expert = tibia::world::session().getExpertPvpMode();
    if (expert == m_exportModeButtonEnable)
        return;
    m_exportModeButtonEnable = expert;
    Q_EMIT exportModeButtonEnableChanged();
}

void TPlayerInventoryAndStatusController::publishTactics()
{
    auto action = input::mutableClone(*input::makeAction(A::SET_TACTICS));
    action->chase = m_chaseEnabled;
    action->secure = m_secureModeEnabled;
    action->pvpMode = m_pvpMode;
    input::publish(action);
}

void TPlayerInventoryAndStatusController::onChaseModeChangeRequested(const bool chaseOpponents)
{
    if (chaseOpponents != m_chaseEnabled) {
        m_chaseEnabled = chaseOpponents;
        if (m_options) {
            config::SidebarPanelsOptions panels = m_options->sidebarPanelsOptions();
            panels.chaseModeEnabled = chaseOpponents;
            m_options->setSidebarPanelsOptions(panels);
        }
        Q_EMIT chaseModeChanged();
        publishTactics();
        return;
    }
    Q_EMIT chaseModeChanged();
}

void TPlayerInventoryAndStatusController::onToggleChaseMode()
{
    onChaseModeChangeRequested(!m_chaseEnabled);
}

void TPlayerInventoryAndStatusController::onSecureModeEnabledRequested(const bool secureModeEnabled)
{
    if (secureModeEnabled != m_secureModeEnabled) {
        m_secureModeEnabled = secureModeEnabled;
        Q_EMIT secureModeEnabledChanged();
        publishTactics();
        return;
    }
    Q_EMIT secureModeEnabledChanged();
}

void TPlayerInventoryAndStatusController::requestPvpMode(const int mode)
{
    if (mode == m_pvpMode)
        return;
    m_pvpMode = mode;
    m_pvpModeDove = mode == 0;
    m_pvpModeWhiteHand = mode == 1;
    m_pvpModeYellowHand = mode == 2;
    m_pvpModeRedFist = mode == 3;
    Q_EMIT pvpModeChanged();
    publishTactics();
}

bool TPlayerInventoryAndStatusController::handleTacticsAction(const input::TGameAction& action)
{
    switch (action.type) {
    case A::SET_PVP_MODE:
        requestPvpMode(action.pvpMode);
        break;
    case A::SET_CHASE_OPPONENTS:
        onChaseModeChangeRequested(action.chase);
        break;
    case A::TOGGLE_CHASE_OPPONENTS:
        onToggleChaseMode();
        break;
    case A::TOGGLE_SECURE_MODE:
        onSecureModeEnabledRequested(!m_secureModeEnabled);
        break;
    case A::SET_SECURE_MODE:
        onSecureModeEnabledRequested(action.secure);
        break;
    default:
        return false;
    }
    return true;
}

void TPlayerInventoryAndStatusController::onBlessingsButtonClicked()
{
    tibia::world::session().requestBless();
}

void TPlayerInventoryAndStatusController::onStoreInboxButtonClicked()
{
    if (const input::TGameActionPtr open = firstOfType(slotActions(StoreInboxSlot), A::OPEN_IN_NEW_WINDOW))
        input::publish(open);
}

void TPlayerInventoryAndStatusController::onSlotClicked(const int slotId, const int mouseButton, const int keyboardModifiers)
{
    const input::TGameActionList candidates = input::filterActions(slotActions(slotId), ClickActionTypes);
    const input::TGameActionPtr action =
        m_mapper.map(candidates, ContainerContext, static_cast<Qt::MouseButton>(mouseButton), keyboardModifiers);
    if (!action)
        return;
    if (action->type != A::CONTEXT_MENU) {
        input::publish(action);
        return;
    }
    TContextMenuController* menu = TContextMenuController::instance();
    if (!menu || candidates.empty())
        return;
    std::vector<A> order(std::begin(ClickActionTypes), std::end(ClickActionTypes));
    std::erase(order, A::CONTEXT_MENU);
    std::erase(order, A::SMART_CLICK);
    if (const input::TGameActionList entries = input::contextMenuActions(candidates, order); !entries.empty())
        menu->showGameActions(entries, ContainerContext, m_mapper);
}

void TPlayerInventoryAndStatusController::onSlotEntered(const int slotId)
{
    m_hoveredSlot = slotId;
    if (Cursor* cursor = Cursor::instance())
        cursor->onContainerEntered();
    updateMouseCursorForCurrentSlot();
}

void TPlayerInventoryAndStatusController::onSlotExited(int)
{
    m_hoveredSlot = -1;
    if (Cursor* cursor = Cursor::instance())
        cursor->onContainerExited();
}

void TPlayerInventoryAndStatusController::updateMouseCursorForCurrentSlot()
{
    Cursor* cursor = Cursor::instance();
    if (m_hoveredSlot == -1 || !cursor)
        return;
    const int modifiers = static_cast<int>(QGuiApplication::queryKeyboardModifiers());
    const input::TGameActionPtr action = m_mapper.map(slotActions(m_hoveredSlot), ContainerContext, Qt::LeftButton, modifiers);
    cursor->setLayerShape(Cursor::Container, action ? worldmap::TWorldMapGameActionProvider::cursorShapeOf(action) : Cursor::Default);
}

void TPlayerInventoryAndStatusController::onSlotStartDrag(const int slotId, QObject* source)
{
    TDragAndDropController* dnd = TDragAndDropController::instance();
    if (!dnd)
        return;
    if (const input::TGameActionPtr move = firstOfType(slotActions(slotId), A::MOVE_OBJECT))
        dnd->startDrag(move, source);
}

void TPlayerInventoryAndStatusController::onSlotDragDropped(const int slotId)
{
    TDragAndDropController* dnd = TDragAndDropController::instance();
    if (!dnd)
        return;
    const input::TGameActionPtr move = dnd->takeAction();
    if (!move)
        return;
    const bool moveAll = app::ClientOptions::load().options().value(QLatin1String(MoveAllOption)).toBool(false);
    const Position destination(UINT16_MAX, static_cast<uint16_t>(std::max(0, slotId)), 0);
    input::publish(input::finalizeDrop(move, destination, static_cast<int>(dnd->modifiers()), moveAll));
}

void TPlayerInventoryAndStatusController::onSlotTargetSelected(const int slotId)
{
    if (const input::TGameActionPtr target = firstOfType(slotActions(slotId), A::TARGET_SELECTED))
        input::publish(target);
}

void TPlayerInventoryAndStatusController::onStopClicked()
{
    input::publish(input::makeAction(A::STOP_ATTACKING_FOLLOWING_WALKING));
}

}

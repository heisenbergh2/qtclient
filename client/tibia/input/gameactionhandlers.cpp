#include "gameactionhandlers.h"

#include "app/clientoptions.h"
#include "gamewindow/gamewindowcontroller.h"
#include "gamewindow/mousecursorcontroller.h"
#include "gamewindow/remainingdialogs.h"
#include "renderer/spritetextureprovider.h"

#include <tibia/world/container.h>
#include <tibia/world/creature.h>
#include <tibia/world/game.h>
#include <client/item.h>
#include <tibia/world/localplayer.h>
#include <tibia/world/map.h>
#include <client/outfit.h>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QProcess>
#include <QQuickWindow>
#include <QWindow>

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

Q_LOGGING_CATEGORY(lcGameActions, "tibia.gameactions")

namespace tibia::input {

namespace {

using A = EGameAction;

std::function<void(const TGameAction&)>& minimapActionHandler()
{
    static std::function<void(const TGameAction&)> handler;
    return handler;
}

std::unordered_map<EGameAction, ActionHandler>& actionHandlers()
{
    static std::unordered_map<EGameAction, ActionHandler> handlers;
    return handlers;
}

TargetHandler& targetHandler()
{
    static TargetHandler handler;
    return handler;
}

std::function<void(bool)>& targetSelectionListener()
{
    static std::function<void(bool)> listener;
    return listener;
}

std::map<int, std::pair<EGameAction, ActionListener>>& actionListeners()
{
    static std::map<int, std::pair<EGameAction, ActionListener>> listeners;
    return listeners;
}

bool notifyListeners(const TGameAction& action)
{
    std::vector<ActionListener> matching;
    for (const auto& [id, listener] : actionListeners())
        if (listener.first == action.type)
            matching.push_back(listener.second);
    for (const ActionListener& listener : matching)
        if (listener(action))
            return true;
    return false;
}

/// A dragged object's count: the split dialog's amount, else the whole stack.
int moveCount(const TGameAction& action)
{
    if (!action.object || !action.object->isItem())
        return 1;
    const ItemPtr item = action.object->static_self_cast<Item>();
    if (!item->isStackable())
        return 1;
    const int count = std::max<int>(1, item->getCountOrSubType());
    return action.amount > 0 ? std::min(action.amount, count) : count;
}

void moveObject(const TGameAction& action)
{
    if (action.object)
        g_game.move(action.object, action.destination, moveCount(action));
}

/// The open container an object lies in: OPEN shows a container's object in
/// that container's window (CIP: TContainerGameActionHandler).
ContainerPtr parentContainer(const TGameAction& action)
{
    const Position& position = action.position;
    if (position.x != UINT16_MAX || !(position.y & 0x40))
        return nullptr;
    return g_game.getContainer(position.y & 0x3F);
}

CreaturePtr creatureOf(const TGameAction& action)
{
    return action.creatureId ? g_map.getCreatureById(action.creatureId) : nullptr;
}

/// CIP's NPC talk handler (0x100a478c0): greet NPCs closer than four fields
/// on the player's floor.
void startTalking(const TGameAction& action)
{
    const CreaturePtr npc = creatureOf(action);
    const LocalPlayerPtr& player = g_game.getLocalPlayer();
    if (!npc || !player)
        return;
    const Position& from = player->getPosition();
    const Position& to = npc->getPosition();
    if (from.z != to.z || std::abs(from.x - to.x) >= 4 || std::abs(from.y - to.y) >= 4)
        return;
    g_game.greetNpc(npc->getId());
}

void openLauncher()
{
    // Quit closes the window. That must not raise Exit / Logout / Cancel.
    gamewindow::TGameWindowController::allowProcessExit();
#ifndef Q_OS_WASM
    QProcess::startDetached(QCoreApplication::applicationFilePath(), QCoreApplication::arguments().mid(1));
#endif
    QCoreApplication::quit();
}

/// Focused QML/text field helpers (CIP REQUEST_SELECT_ALL_TEXT / COPY / CONFIRM_COPY).
QObject* focusedTextField()
{
    return QGuiApplication::focusObject();
}

/// CIP InspectPlayer action byte (0xCE). Crystal services 1–5 as character
/// inspection tabs; handshake values are still sent for CIP-compatible servers.
uint8_t inspectPlayerAction(const EGameAction type)
{
    switch (type) {
    case A::ALLOW_ALL_TO_INSPECT_PLAYER:
    case A::WITHDRAW_ALLOW_ALL_TO_INSPECT_PLAYER:
        return 0;
    case A::INVITE_TO_INSPECT_PLAYER:
        return 1;
    case A::ALLOW_INSPECT_PLAYER:
        return 2;
    case A::ASK_TO_INSPECT_PLAYER:
        return 3;
    case A::REVOKE_INSPECT_PLAYER:
        return 4;
    case A::INSPECT_PLAYER:
    default:
        return 5;
    }
}

void setLenshelp(const bool enabled)
{
    if (gamewindow::TMouseCursorController* cursor = gamewindow::TMouseCursorController::instance())
        cursor->setLenshelp(enabled);
    if (gamewindow::TLenshelpController* lenshelp = gamewindow::TLenshelpController::instance()) {
        if (enabled)
            lenshelp->showHelp(QCoreApplication::translate("cip", "Lenshelp"),
                               QCoreApplication::translate("cip", "Click an object for more information."));
        else
            lenshelp->hideHelp();
    }
}

void clearSpriteCaches()
{
    // CIP REMOVE_SPRITEIDENTIFIER_FROM_CACHE evicts one id; the atlas cache only
    // exposes clear(). Drop every window's shared cache when a QGuiApplication
    // top-level window is available.
    const QWindowList windows = QGuiApplication::allWindows();
    for (QWindow* window : windows) {
        if (auto* quick = qobject_cast<QQuickWindow*>(window))
            renderer::TSpriteTextureProviderManager::sharedCache(quick).clear();
    }
}

}

void publish(const TGameActionPtr& action)
{
    if (!action)
        return;
    // Clipboard COPY_* / REQUEST_COPY_* are owned by TClipboardGameActionHandler.
    // A few actions must work before login (sound helper, mute, HUD toggle, relaunch,
    // character-selection shell, focused-text clipboard helpers).
    static constexpr A OfflineActions[] = {
        A::SOUND_TRIGGER, A::TOGGLE_SOUND, A::TOGGLE_CREATURE_HUDS_VISIBLE, A::OPEN_LAUNCHER,
        A::TOGGLE_FULLSCREEN, A::QUIT_GAME, A::OPEN_OPTIONS_MENU_DIALOG, A::REQUEST_SELECT_ALL_TEXT,
        A::COPY_TEXT_TO_CLIPBOARD, A::COPY_TRUSTED_TEXT_TO_CLIPBOARD, A::COPY_CHAT_ENTRY_TO_CLIPBOARD,
        A::COPY_CREATURE_NAME_TO_CLIPBOARD, A::COPY_CREATURE_WITH_ID_NAME_TO_CLIPBOARD,
        A::REQUEST_COPY_SELECTED_TEXT_TO_CLIPBOARD, A::REQUEST_CONFORMATION_FOR_COPYING_TEXT_TO_CLIPBOARD,
        A::SHOW_CREATE_CHARACTER_DIALOG, A::CHARACER_SELECTION_AUTO_CLOSE, A::LENSHELP_SHOW, A::LENSHELP_HIDE,
        A::LENSHELP_ACTIVATE, A::REMOVE_SPRITEIDENTIFIER_FROM_CACHE, A::RESOLVE_SPRITE, A::SPRITE_RESOLVED,
        A::OPEN_SHOW_IMAGE_DIALOG,
        // Login-mask Manage Account / logos / Create Account → PortalUrls handlers.
        A::OPEN_URL, A::OPEN_URL_GET_PREMIUM, A::OPEN_URL_GET_COINS, A::OPEN_URL_TIBIA_PAGE,
        A::OPEN_URL_CREATE_ACCOUNT, A::OPEN_URL_ACCESS_ACCOUNT, A::OPEN_URL_CIPSOFT, A::OPEN_URL_MANUAL,
        A::OPEN_URL_FAQ, A::OPEN_URL_SURVEY, A::OPEN_URL_LOST_ACCOUNT, A::OPEN_URL_PREMIUM_FEATURES,
        A::OPEN_URL_TWITCH_TIBIA, A::OPEN_URL_YOUTUBE_TIBIA,
        // Tutorial / sessiondump cinematic (may fire before g_game reports online).
        A::SHOW_TUTORIAL_HINT, A::CONFIRM_TUTORIAL_HINT, A::ADD_GUI_TUTORIAL_MARKER,
        A::REMOVE_ALL_TUTORIAL_MARKERS, A::ADD_HINT_AT_TUTORIAL_MARKER,
        A::ADD_MOUSE_TRAIL_HINT_AT_TUTORIAL_MARKER, A::FINISHED_TUTORIAL_FOR_CHARACTER, A::TUTORIAL_PROGRESS,
        A::CHANGE_CONTEXT_MENU_ENABLED, A::SESSIONDUMP_START, A::SESSIONDUMP_PAUSE, A::SESSIONDUMP_RESUME,
        A::SESSIONDUMP_SKIP_TO,
    };
    const bool offlineOk = std::find(std::begin(OfflineActions), std::end(OfflineActions), action->type)
        != std::end(OfflineActions);
    if (!g_game.isOnline() && !offlineOk)
        return;
    if (notifyListeners(*action))
        return;
    if (const auto it = actionHandlers().find(action->type); it != actionHandlers().end() && it->second(*action))
        return;
    switch (action->type) {
    case A::SMART_CLICK:
        publish(action->inner);
        break;
    case A::LOOK:
        if (action->object)
            g_game.look(action->object);
        break;
    case A::LOOK_AT_CREATURE:
        if (const CreaturePtr creature = creatureOf(*action))
            g_game.look(creature);
        break;
    case A::USE:
        if (action->object)
            g_game.use(action->object);
        break;
    case A::TRY_TO_EQUIP:
        if (action->object && action->object->isItem())
            g_game.equipItem(action->object->static_self_cast<Item>());
        break;
    case A::OPEN:
    case A::OPEN_IN_NEW_WINDOW:
        if (action->object && action->object->isItem())
            g_game.open(action->object->static_self_cast<Item>(), action->type == A::OPEN ? parentContainer(*action) : nullptr);
        break;
    case A::OPEN_PARENT_CONTAINER:
        if (const ContainerPtr container = parentContainer(*action))
            g_game.openParent(container);
        else if (action->object && action->object->isItem()) {
            // Depot-search parent: the object's field (CIP open parent from search).
            g_game.openParentContainerFromDepotSearch(action->position);
        }
        break;
    case A::BROWSE_FIELD:
        if (action->position.isValid())
            g_game.browseField(action->position);
        break;
    case A::GATHER_ALL:
        // Handled by TGatherAllGameActionHandler when registered.
        break;
    case A::ROTATE_OBJECT:
        if (action->object)
            g_game.rotate(action->object);
        break;
    case A::INSPECT_OBJECT:
        if (action->position.isValid())
            g_game.inspectionNormalObject(action->position);
        break;
    case A::INSPECT_PLAYER:
    case A::INVITE_TO_INSPECT_PLAYER:
    case A::ASK_TO_INSPECT_PLAYER:
    case A::ALLOW_INSPECT_PLAYER:
    case A::REVOKE_INSPECT_PLAYER:
        if (action->creatureId)
            g_game.inspectCharacter(action->creatureId, inspectPlayerAction(action->type));
        break;
    case A::ALLOW_ALL_TO_INSPECT_PLAYER:
        app::ClientOptions::store(QStringLiteral("inspectPlayerAllowAllEnabled"), true);
        // CIP InspectPlayer action 0 (no creature): allow everyone. Crystal no-ops today.
        g_game.inspectCharacter(0, inspectPlayerAction(A::ALLOW_ALL_TO_INSPECT_PLAYER));
        break;
    case A::WITHDRAW_ALLOW_ALL_TO_INSPECT_PLAYER:
        app::ClientOptions::store(QStringLiteral("inspectPlayerAllowAllEnabled"), false);
        g_game.inspectCharacter(0, inspectPlayerAction(A::WITHDRAW_ALLOW_ALL_TO_INSPECT_PLAYER));
        break;
    case A::INVITE_TO_PARTY:
        if (action->creatureId)
            g_game.partyInvite(action->creatureId);
        break;
    case A::JOIN_PARTY:
        if (action->creatureId)
            g_game.partyJoin(action->creatureId);
        break;
    case A::REVOKE_PARTY_INVITATION:
        if (action->creatureId)
            g_game.partyRevokeInvitation(action->creatureId);
        break;
    case A::PASS_PARTY_LEADERSHIP:
        if (action->creatureId)
            g_game.partyPassLeadership(action->creatureId);
        break;
    case A::LEAVE_PARTY:
        if (const LocalPlayerPtr self = g_game.getLocalPlayer(); self && (self->getShield() == 1 || self->getShield() == 3))
            g_game.partyDisband();
        else
            g_game.partyLeave();
        break;
    case A::SET_SHARED_EXPERIENCE:
        g_game.partyShareExperience(action->enable);
        break;
    case A::REQUEST_DEPOT_SEARCH:
        g_game.openDepotSearch();
        break;
    case A::DEPOT_SEARCH_CLOSED:
        g_game.closeDepotSearchRequest();
        break;
    case A::REQUEST_SEARCH_TYPE_IN_DEPOT:
        if (action->object)
            g_game.requestDepotSearchItem(static_cast<uint16_t>(action->object->getId()),
                                          static_cast<uint8_t>(std::max(0, action->parameter)));
        else if (action->parameter > 0)
            g_game.requestDepotSearchItem(static_cast<uint16_t>(action->parameter), 0);
        break;
    case A::DEPOT_SERACH_RETRIEVE:
        g_game.retrieveDepotSearch(static_cast<uint16_t>(action->exaltationTypeId ? action->exaltationTypeId : action->parameter),
                                   action->exaltationTier, static_cast<uint8_t>(action->category >= 0 ? action->category : 0));
        break;
    case A::CANCEL_USE_WITH:
    case A::CANCEL_CROSSHAIR_SPELL:
        finishTargetSelection(nullptr);
        break;
    case A::TOGGLE_CREATURE_HUDS_VISIBLE:
    case A::TOGGLE_SOUND:
        // Handled by TGenericGameActionHandler when registered.
        break;
    case A::OPEN_LAUNCHER:
        openLauncher();
        break;
    case A::WRAP:
    case A::UNWRAP:
        if (action->object)
            g_game.wrap(action->object);
        break;
    case A::ATTACK_CREATURE:
        if (const CreaturePtr creature = creatureOf(*action))
            g_game.attack(creature);
        break;
    case A::STOP_ATTACKING_CREATURE:
        g_game.cancelAttack();
        break;
    case A::FOLLOW_CREATURE:
        if (const CreaturePtr creature = creatureOf(*action))
            g_game.follow(creature);
        break;
    case A::STOP_FOLLOWING_CREATURE:
        g_game.cancelFollow();
        break;
    case A::COPY_CREATURE_WITH_ID_NAME_TO_CLIPBOARD:
    case A::COPY_CREATURE_NAME_TO_CLIPBOARD:
        // Handled by TClipboardGameActionHandler when registered.
        break;
    case A::SET_PLAYER_MOVEMENT_INTENT:
        // Handled by TPlayerMovementGameActionHandler when registered.
        break;
    case A::START_TALKING_TO_NPC:
        startTalking(*action);
        break;
    case A::MOUNT_PLAYER:
        g_game.mount(true);
        break;
    case A::UNMOUNT_PLAYER:
        g_game.mount(false);
        break;
    case A::TOGGLE_MOUNTED:
        if (const LocalPlayerPtr& player = g_game.getLocalPlayer())
            g_game.mount(!player->getOutfit().hasMount());
        break;
    case A::MOVE_OBJECT:
    case A::MOVE_UP:
        moveObject(*action);
        break;
    case A::SHOW_SPLIT_STACK_DIALOG:
        if (action->inner)
            moveObject(*action->inner);
        break;
    case A::STOP_ATTACKING_FOLLOWING_WALKING:
        g_game.stop();
        break;
    case A::SET_TACTICS:
        g_game.setTactics(action->chase ? Otc::ChaseOpponent : Otc::DontChase, action->secure,
                          static_cast<Otc::PVPModes>(action->pvpMode));
        break;
    case A::GOD_TELEPORT:
        // CIP: TGameActionTeleportToCoordinate -> GameclientMessageTeleport.
        g_game.sendTeleport(action->position);
        break;
    case A::REQUEST_RESOURCE_BALANCE:
        // CIP 0x100e669e0: GameclientMessageRequestResourceBalance with the action's resource type.
        g_game.requestResourceBalance(static_cast<uint8_t>(action->parameter));
        break;
    case A::CONFIGURE_SHOW_OFF_SOCKET:
        if (action->object && action->object->isItem()) {
            const ItemPtr item = action->object->static_self_cast<Item>();
            const uint8_t stackpos = action->stackPosition >= 0
                ? static_cast<uint8_t>(action->stackPosition)
                : static_cast<uint8_t>(item->getStackPos());
            g_game.configureShowOffSocket(action->position, item->getId(), stackpos);
        }
        break;
    case A::SET_PLAYER_OUTFIT:
        if (action->outfit.getId() != 0)
            g_game.changeOutfitAndMount(action->outfit, action->mountCharacter, action->randomMount);
        break;
    case A::SET_HIRELING_OUTFIT:
        if (action->outfit.getId() != 0)
            g_game.setHirelingOutfit(action->outfit, action->creatureId);
        break;
    case A::SET_SHOW_OFF_SOCKET_CONFIGURATION: {
        // Outfit id 0 is valid (clear the displayed look / hide outfit).
        const uint16_t itemId = action->object && action->object->isItem()
            ? static_cast<uint16_t>(action->object->getId())
            : static_cast<uint16_t>(action->parameter);
        if (itemId != 0) {
            const uint8_t stackpos = action->stackPosition >= 0
                ? static_cast<uint8_t>(action->stackPosition)
                : (action->object && action->object->isItem()
                       ? static_cast<uint8_t>(action->object->static_self_cast<Item>()->getStackPos())
                       : 0);
            g_game.setShowOffSocketConfiguration(action->outfit, action->position, itemId, stackpos,
                                                 static_cast<uint8_t>(action->lookDirection),
                                                 action->showSocket || action->enable ? 1 : 0);
        }
        break;
    }
    case A::SET_CREATURE_PODIUM_CONFIGURATION: {
        const uint16_t itemId = action->object && action->object->isItem()
            ? static_cast<uint16_t>(action->object->getId())
            : static_cast<uint16_t>(action->parameter);
        const uint8_t stackpos = action->stackPosition >= 0 ? static_cast<uint8_t>(action->stackPosition) : 0;
        g_game.setCreaturePodiumConfiguration(action->creatureId, action->position, itemId, stackpos,
                                              static_cast<uint8_t>(action->lookDirection),
                                              action->enable ? 1 : 0, action->showSocket || action->chase ? 1 : 0);
        break;
    }
    case A::OPEN_OR_UPDATE_REWARD_WALL_DIALOG:
        g_game.sendOpenRewardWall();
        break;
    case A::CLOSE_REWARD_WALL_DIALOG:
        // Local dialog close is owned by the reward-wall controller; the server
        // also pushes CloseRewardWall. Clear the button-bar highlight.
        publish(makeAction(A::DAILY_REWARD_NOT_COLLECTABLE));
        break;
    case A::REQUEST_DAILY_REWARD_HISTORY:
        g_game.requestOpenRewardHistory();
        break;
    case A::COLLECT_FIXED_ITEMS_DAILY_REWARD:
    case A::COLLECT_PICK_ITEMS_DAILY_REWARD:
        g_game.requestGetRewardDaily(static_cast<uint8_t>(action->parameter > 0 ? action->parameter : 0), {});
        break;
    case A::OPEN_REWARD_WALL_NOT_REACHED_MAIN_MESSAGE:
    case A::OPEN_REWARD_WALL_DAILY_REWARDS_DISABLED_MESSAGE: {
        auto status = mutableClone(*makeAction(A::SET_STATUS_LINE));
        status->text = action->text.isEmpty()
            ? qtTrId(action->type == A::OPEN_REWARD_WALL_NOT_REACHED_MAIN_MESSAGE
                         ? "rewardwall_not_reached"
                         : "rewardwall_daily_rewards_disabled")
            : action->text;
        status->color = QColor(Qt::white);
        publish(status);
        break;
    }
    case A::SHOW_MESSAGE_YOU_SEE_NOTHING: {
        auto message = std::make_shared<TGameActionAddTextMessage>(A::ADD_BENEATH_MIDDLE_WORLDMAP_MESSAGE);
        message->text = action->text.isEmpty() ? qtTrId("you_see_nothing") : action->text;
        message->color = action->color.isValid() ? action->color : QColor(Qt::green);
        publish(message);
        break;
    }
    case A::JOIN_AGGRESSION:
        if (action->creatureId)
            g_game.joinAggression(action->creatureId);
        break;
    case A::REQUEST_TRANSACTION_DETAILS:
        g_game.requestTransactionDetails(static_cast<uint32_t>(action->instance));
        break;
    case A::OPEN_TRADE_CHARACTER_DIALOG:
        // CIP → TCharacterTradeDialogController (CharacterTradeDialog.qml); action 0 = conditions.
        if (gamewindow::TCharacterTradeDialogController* trade =
                gamewindow::TCharacterTradeDialogController::instance()) {
            g_game.requestCharacterTradeConfiguration(0);
            trade->showDialog();
        } else {
            g_game.requestCharacterTradeConfiguration(0);
        }
        break;
    case A::MINIMAP_ONE_FLOOR_UP:
    case A::MINIMAP_ONE_FLOOR_DOWN:
    case A::MINIMAP_ZOOM_IN:
    case A::MINIMAP_ZOOM_OUT:
    case A::MINIMAP_SCROLL_NORTH:
    case A::MINIMAP_SCROLL_EAST:
    case A::MINIMAP_SCROLL_SOUTH:
    case A::MINIMAP_SCROLL_WEST:
    case A::MINIMAP_CENTER:
        if (const auto& handler = minimapActionHandler())
            handler(*action);
        break;
    case A::START_TARGET_SELECTION:
        // CIP: TTargetSelectionController handles this; our listener mirrors it.
        if (const auto& listener = targetSelectionListener())
            listener(true);
        break;
    case A::TARGET_SELECTION_FINISHED:
        if (const auto& listener = targetSelectionListener())
            listener(false);
        break;
    case A::LENSHELP_SHOW:
        setLenshelp(true);
        break;
    case A::LENSHELP_HIDE:
        setLenshelp(false);
        break;
    case A::REQUEST_SELECT_ALL_TEXT:
        // Soft: Soft chat TextEdits are often not focusable — fall back to Soft chat select-all.
        if (QObject* field = focusedTextField()) {
            if (QMetaObject::invokeMethod(field, "selectAll", Qt::DirectConnection))
                break;
        }
        publish(makeAction(A::CHAT_CHANNEL_SELECT_ALL));
        break;
    case A::REQUEST_COPY_SELECTED_TEXT_TO_CLIPBOARD:
    case A::REQUEST_CONFORMATION_FOR_COPYING_TEXT_TO_CLIPBOARD:
        // Handled by TClipboardGameActionHandler when registered.
        break;
    case A::REMOVE_SPRITEIDENTIFIER_FROM_CACHE:
        clearSpriteCaches();
        break;
    case A::RESOLVE_SPRITE:
        // CIP resolves a sprite id into the texture atlas. Without a window-bound
        // SpriteKey here we acknowledge the action; sheets load on next draw.
        qCInfo(lcGameActions) << "RESOLVE_SPRITE" << action->parameter;
        break;
    case A::SPRITE_RESOLVED:
        qCInfo(lcGameActions) << "SPRITE_RESOLVED" << action->parameter;
        break;
    case A::OPEN_SHOW_IMAGE_DIALOG:
        if (gamewindow::TShowImageDialogController* images = gamewindow::TShowImageDialogController::instance()) {
            const QString path = action->text;
            images->showImages(path.isEmpty() ? QStringList{} : QStringList{ path }, path);
        } else {
            qCInfo(lcGameActions) << "OPEN_SHOW_IMAGE_DIALOG" << action->text << action->parameter;
        }
        break;
    case A::SHOW_TUTORIAL_HINT:
    case A::CONFIRM_TUTORIAL_HINT:
    case A::ADD_GUI_TUTORIAL_MARKER:
    case A::REMOVE_ALL_TUTORIAL_MARKERS:
    case A::ADD_HINT_AT_TUTORIAL_MARKER:
    case A::ADD_MOUSE_TRAIL_HINT_AT_TUTORIAL_MARKER:
    case A::FINISHED_TUTORIAL_FOR_CHARACTER:
    case A::TUTORIAL_PROGRESS:
    case A::SESSIONDUMP_START:
    case A::SESSIONDUMP_PAUSE:
    case A::SESSIONDUMP_RESUME:
    case A::SESSIONDUMP_SKIP_TO:
        // Handled by TTutorialShell action handlers when the game window is up.
        break;
    default:
        break;
    }
}

void setMinimapActionHandler(std::function<void(const TGameAction&)> handler)
{
    minimapActionHandler() = std::move(handler);
}

void setActionHandler(const EGameAction type, ActionHandler handler)
{
    if (handler)
        actionHandlers()[type] = std::move(handler);
    else
        actionHandlers().erase(type);
}

int addActionListener(const EGameAction type, ActionListener listener)
{
    static int nextId = 0;
    actionListeners().emplace(++nextId, std::make_pair(type, std::move(listener)));
    return nextId;
}

void removeActionListener(const int id) { actionListeners().erase(id); }

void startTargetSelection(TargetHandler handler)
{
    finishTargetSelection(nullptr);
    targetHandler() = std::move(handler);
    if (targetHandler() && targetSelectionListener())
        targetSelectionListener()(true);
}

bool isSelectingTarget()
{
    return static_cast<bool>(targetHandler());
}

void finishTargetSelection(const TGameActionPtr& target)
{
    if (TargetHandler handler = std::exchange(targetHandler(), {})) {
        if (targetSelectionListener())
            targetSelectionListener()(false);
        handler(target);
    }
}

void setTargetSelectionListener(std::function<void(bool)> listener)
{
    targetSelectionListener() = std::move(listener);
}

}

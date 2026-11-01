#include "gamemessagehandler.h"

#include "chat/chatchannel.h"
#include "chat/chatprotocolmessagehandler.h"
#include "chat/textstorage.h"
#include "config/clientoptiondefaults.h"
#include "input/gameactionhandlers.h"
#include "worldmap/numericaleffectstorage.h"

#include <tibia/world/game.h>
#include <tibia/world/localplayer.h>
#include <client/protocolcodes.h>

namespace tibia::chat {

namespace {

using client::gamecore::argInt;
using client::gamecore::argString;
using Args = std::vector<std::any>;

/// CIP's chat message modes: the protocol's (identity table 0x102309ad4).
enum Mode : int
{
    Say = 1,
    Whisper = 2,
    Yell = 3,
    PrivateFrom = 4,
    Spell = 9,
    NpcFromStartBlock = 10,
    NpcFrom = 11,
    GamemasterBroadcast = 13,
    GamemasterPrivateFrom = 15,
    Login = 17,
    Admin = 18,
    Game = 19,
    GameHighlight = 20,
    Failure = 21,
    Look = 22,
    Status = 30,
    Loot = 31,
    TradeNpc = 32,
    PartyManagement = 34,
    MonsterSay = 36,
    MonsterYell = 37,
    Report = 38,
    HotkeyUse = 39,
    BeyondLast = 44,
    BoostedCreature = 49,
    OfflineTraining = 50,
    Transaction = 51,
    Potion = 52,
    Mode53 = 53,
    Mode54 = 54,
};

/// Hook arguments carry CIP's mode; CIP routes by the protocol's.
int protocolMode(const Args& args, const size_t index)
{
    if (index < args.size())
        if (const auto* mode = std::any_cast<Otc::MessageMode>(&args[index]))
            return Proto::translateMessageModeToServer(*mode);
    return -1;
}

Position argPosition(const Args& args, const size_t index)
{
    if (index < args.size())
        if (const auto* position = std::any_cast<Position>(&args[index]))
            return *position;
    return {};
}

/// FUN_10099b410.
int speechVolume(const int mode)
{
    switch (mode) {
        case Say: return TCreatureSpeechBubble::Say;
        case Whisper: return TCreatureSpeechBubble::Whisper;
        case Yell: return TCreatureSpeechBubble::Yell;
        case Spell: return TCreatureSpeechBubble::Cast;
        default: return TCreatureSpeechBubble::Monster;
    }
}

/// Missing key → CIP default (FUN_1009ad850); gameWindow* filters default to 1.
bool option(const QJsonObject& options, const char* name)
{
    const QLatin1String key(name);
    return options.value(key).toBool(config::defaultClientOptions().value(key).toBool());
}

}

TGameMessageHandler::TGameMessageHandler(TTextStorage& texts, worldmap::TNumericalEffectStorage& effects)
    : m_texts(texts)
    , m_effects(effects)
{
    using client::gamecore::Hook;
    m_hooks.emplace_back("g_game", "onTalk", [this](LuaObject*, const Args& args) {
        onTalk(argString(args, 0), protocolMode(args, 2), argString(args, 3), argPosition(args, 5));
        return std::any();
    });
    m_hooks.emplace_back("g_game", "onTextMessage", [this](LuaObject*, const Args& args) {
        onTextMessage(protocolMode(args, 0), argString(args, 1));
        return std::any();
    });
    m_hooks.emplace_back("g_game", "onNumericalEffect", [this](LuaObject*, const Args& args) {
        onNumericalEffect(argPosition(args, 1), argInt(args, 2), static_cast<int>(argInt(args, 3)));
        return std::any();
    });
    // CIP FUN_100993170 (case 0x66 of FUN_100991ae0): the status line, gated by gameWindowShowMessages.
    input::setActionHandler(input::EGameAction::SET_STATUS_LINE, [this](const input::TGameAction& action) {
        if (m_showMessages)
            m_texts.setStatusMessage(action.text, action.color.isValid() ? action.color : systemWhite());
        return true;
    });
    // CIP FUN_100993370 (case 0xa0): a centre message (text storage slot 0xd0), same gate.
    input::setActionHandler(input::EGameAction::ADD_BENEATH_MIDDLE_WORLDMAP_MESSAGE, [this](const input::TGameAction& action) {
        if (m_showMessages)
            m_texts.addCenterMessage(action.text, action.color.isValid() ? action.color : systemWhite());
        return true;
    });
    input::setActionHandler(input::EGameAction::CLEAR_OLDEST_MESSAGE_FROM_MAPWINDOW, [this](const input::TGameAction&) {
        m_texts.clearOldestMessage();
        return true;
    });
}

TGameMessageHandler::~TGameMessageHandler()
{
    input::setActionHandler(input::EGameAction::SET_STATUS_LINE, nullptr);
    input::setActionHandler(input::EGameAction::ADD_BENEATH_MIDDLE_WORLDMAP_MESSAGE, nullptr);
    input::setActionHandler(input::EGameAction::CLEAR_OLDEST_MESSAGE_FROM_MAPWINDOW, nullptr);
}

void TGameMessageHandler::setOptions(const QJsonObject& options)
{
    m_showTextualEffects = option(options, "gameWindowShowTextualEffects");
    m_showMessages = option(options, "gameWindowShowMessages");
    m_showPrivateMessages = option(options, "gameWindowShowPrivateMessages");
    m_showBoostedCreatureMessages = option(options, "gameWindowShowBoostedCreatureMessages");
    m_showOfflineTrainingMessages = option(options, "gameWindowShowOfflineTrainingMessages");
    m_showStoreMessages = option(options, "gameWindowShowStoreMessages");
    m_showPotionMessages = option(options, "gameWindowShowPotionMessages");
    m_showHotkeyUsageMessages = option(options, "gameWindowShowHotkeyUsageMessages");
    m_showLootMessages = option(options, "gameWindowShowLootMessages");
    m_showOwnSpells = option(options, "gameWindowShowOwnSpells");
    m_showOthersSpells = option(options, "gameWindowShowOthersSpells");
}

QColor TGameMessageHandler::overlayColor(const int mode)
{
    switch (mode) {
        case Say: case Whisper: case Yell: case Spell:
            return QColor(240, 240, 0);
        case PrivateFrom: case NpcFromStartBlock: case NpcFrom:
            return QColor(96, 248, 248);
        case GamemasterBroadcast: case GamemasterPrivateFrom: case Admin: case GameHighlight:
            return QColor(248, 96, 96);
        case Look: case Loot: case TradeNpc: case PartyManagement: case HotkeyUse:
            return QColor(0, 240, 0);
        case MonsterSay: case MonsterYell: case Potion: case Mode53:
            return QColor(255, 102, 0);
        default:
            return QColor(240, 240, 240);
    }
}

void TGameMessageHandler::addSpeech(const QString& name, const int mode, const QString& text, const Position& coordinate)
{
    if (m_showMessages)
        m_texts.addCreatureSpeech(name, speechVolume(mode), text, overlayColor(mode), coordinate);
}

void TGameMessageHandler::onTalk(const QString& name, const int mode, const QString& text, const Position& coordinate)
{
    // CIP FUN_100996120 / handler +0x98: same ignore rules as the console.
    if (m_ignoreList && !TWhiteBlacklist::isExemptMode(mode)
        && (m_ignoreList->isBlacklisted(name) || (mode == Yell && m_ignoreList->filtersYell(name))
            || (mode == PrivateFrom && m_ignoreList->filtersPrivateMessage(name))))
        return;

    switch (mode) {
        case Say:
        case Whisper:
        case Yell:
        case MonsterSay:
        case MonsterYell:
        case Mode53:
            addSpeech(name, mode, text, coordinate);
            break;
        case PrivateFrom:
            if (m_showMessages && m_showPrivateMessages)
                m_texts.addPrivateMessage(name, text, overlayColor(mode));
            break;
        case Spell: {
            const LocalPlayerPtr& player = g_game.getLocalPlayer();
            const bool own = player && name.compare(QString::fromStdString(player->getName()), Qt::CaseInsensitive) == 0;
            if (own ? m_showOwnSpells : m_showOthersSpells)
                addSpeech(name, mode, text, coordinate);
            break;
        }
        case NpcFromStartBlock:
            if (m_showMessages) {
                m_texts.setNpcSpeechHeader(name, overlayColor(mode), coordinate);
                m_texts.addNpcSpeech(name, text);
            }
            break;
        case NpcFrom:
            if (m_showMessages)
                m_texts.addNpcSpeech(name, text);
            break;
        case GamemasterBroadcast:
        case GamemasterPrivateFrom:
            if (m_showMessages)
                m_texts.addGamemasterMessage(name, text, overlayColor(mode));
            break;
        case Game:
            if (m_showMessages)
                m_texts.addCenterMessage(text, overlayColor(mode));
            break;
        case Potion:
            if (m_showPotionMessages)
                addSpeech(name, mode, text, coordinate);
            break;
        default:
            break;
    }
}

void TGameMessageHandler::onTextMessage(const int mode, const QString& text)
{
    switch (mode) {
        case Login:
        case Status:
        case Failure:
        case Mode54:
            if (m_showMessages)
                m_texts.setStatusMessage(text, overlayColor(mode));
            break;
        case Admin:
        case Game:
        case GameHighlight:
        case Report:
        case PartyManagement:
            if (m_showMessages)
                m_texts.addCenterMessage(text, overlayColor(mode));
            break;
        case Look:
        case TradeNpc:
        case BeyondLast:
            if (m_showMessages)
                m_texts.setLookMessage(text, overlayColor(mode), false);
            break;
        case Loot:
            if (m_showLootMessages && m_showMessages)
                m_texts.setLookMessage(formatLootRichText(text), overlayColor(mode), true);
            break;
        case HotkeyUse:
            if (m_showHotkeyUsageMessages && m_showMessages)
                m_texts.setLookMessage(text, overlayColor(mode), false);
            break;
        case BoostedCreature:
            if (m_showBoostedCreatureMessages && m_showMessages)
                m_texts.addCenterMessage(text, overlayColor(mode));
            break;
        case OfflineTraining:
            if (m_showOfflineTrainingMessages && m_showMessages)
                m_texts.addCenterMessage(text, overlayColor(mode));
            break;
        case Transaction:
            if (m_showStoreMessages && m_showMessages)
                m_texts.addCenterMessage(text, overlayColor(mode));
            break;
        default:
            break;
    }
}

void TGameMessageHandler::onNumericalEffect(const Position& coordinate, const qint64 value, const int color)
{
    if (m_showTextualEffects && value != 0)
        m_effects.add(coordinate, value, worldmap::TNumericalEffectStorage::colorFrom8bit(color), TTextStorage::now());
}

}

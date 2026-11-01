#include "informationdialogs.h"

#include "gamewindow/configurecreaturepodiumdialogcontroller.h"
#include "gamewindow/cyclopediadialogcontroller.h"
#include "gamewindow/edittextcontroller.h"
#include "gamewindow/highscoresdialogcontroller.h"
#include "gamewindow/outfitdialogcontroller.h"
#include "gamewindow/preydialogcontroller.h"
#include "gamewindow/questlogcontroller.h"
#include "gamewindow/remainingdialogs.h"
#include "gamewindow/servermodaldialogcontroller.h"
#include "input/gameactionhandlers.h"
#include "qmlcomponents/minimaprendererquickitem.h"

#include <tibia/world/creature.h>
#include <tibia/world/game.h>
#include <client/outfit.h>
#include <client/position.h>
#include <tibia/assets/thingtype.h>
#include <tibia/assets/things.h>

namespace tibia::gamewindow {

namespace {

using Args = std::vector<std::any>;
using A = input::EGameAction;
using Entry = TCustomiseCharacterModel::Entry;

template<class T>
const T* arg(const Args& args, const size_t index)
{
    return index < args.size() ? std::any_cast<T>(&args[index]) : nullptr;
}

bool canBeColorized(const int lookType)
{
    if (!g_things.isValidDatId(lookType, ThingCategoryCreature))
        return false;
    const ThingTypePtr& type = g_things.getThingType(lookType, ThingCategoryCreature);
    return type && type->getLayers() > 1;
}

QString qstr(const std::string& text)
{
    return QString::fromStdString(text);
}

}

TInformationDialogs::TInformationDialogs(QObject* parent)
    : QObject(parent)
    , m_outfitDialog(new TSelectOutfitDialogController(this))
    , m_podiumDialog(new TConfigureCreaturePodiumDialogController(this))
    , m_modalDialog(new TServerModalDialogController(this))
    , m_editText(new TEditTextController(this))
    , m_questLog(new TQuestLogController(this))
    , m_highscores(new THighscoresDialogController(this))
    , m_prey(new TPreyDialogController(this))
    , m_cyclopedia(new TCyclopediaDialogController(this))
    , m_cyclopediaButton(new TCyclopediaButtonController(this))
    , m_bosstiaryButton(new TBosstiaryButtonController(this))
{
    // CIP misc dialogs / overlays kept alive on the information hub (ledger wires).
    new TCrashReportDialogController(this);
    new TEmergencyDialogController(this);
    new TGameSessionDisconnectReactionController(this);
    new TCinematicSessiondumpPlayerController(this);
    new TGenericConfirmWithDontShowAgainDialogController(this);
    new TLinkCopyWarningDialogController(this);
    new TLoginConfirmationCodeEnterTextDialogController(this);
    new TTwoFactorCodeEnterTextDialogController(this);
    new TMultiOfflineTrainingDialogController(this);
    new TOutfitMemorialDialogController(this);
    new TReturnerRewardDialogController(this);
    new TShowImageDialogController(this);
    new TShowSpellBookDialogController(this);
    new TSurveyInvitationDialogController(this);
    new TInfoBannerController(nullptr, this);
    new TLenshelpController(this);
    new TTutorialOverlayController(this);
    new TTutorialOverlayItemController(this);
    new TTutorialOverlayMouseFilterController(this);
    new TTutorialOverlayMouseTrailAnimationController(this);

    hookOutfitDialog();
    hookCreaturePodium();
    hookTextDialogs();
    hookQuestLog();
    hookHighscores();
    hookPrey();
}

TInformationDialogs::~TInformationDialogs()
{
    input::setActionHandler(A::REQUEST_OUTFITS_FROM_SERVER, nullptr);
    input::setActionHandler(A::OPEN_QUESTLOG_DIALOG, nullptr);
    input::setActionHandler(A::OPEN_HIGHSCORES_DIALOG, nullptr);
    for (const A type : { A::OPEN_PREY_DIALOG, A::PREY_REQUEST_LIST_REROLL, A::PREY_SELECT_MONSTER_FROM_LIST, A::PREY_REQUEST_BONUS_REROLL,
                          A::PREY_REQUEST_CHANGE_AUTO_EXPAND_TYPE, A::PREY_REQUEST_FREEZE_AND_SHOW_BESTIARY_MONSTERS,
                          A::PREY_SELECT_MONSTER_FROM_BESTIARY })
        input::setActionHandler(type, nullptr);
}

void TInformationDialogs::setAmbientLightStorage(worldmap::TAmbientLightStorage* ambientLight)
{
    m_cyclopedia->setAmbientLightStorage(ambientLight);
}

void TInformationDialogs::setMinimapTileStorage(minimap::TMinimapTileStorage* tiles) { qmlcomponents::TMinimapRendererQuickItem::setMinimapTileStorage(tiles); }

void TInformationDialogs::setInstanceStorage(mods::InstanceBackend* instance) { m_cyclopedia->setInstanceStorage(instance); }

void TInformationDialogs::setControlScheme(const int controlScheme) { m_cyclopedia->setControlScheme(controlScheme); }

void TInformationDialogs::hookOutfitDialog()
{
    input::setActionHandler(A::REQUEST_OUTFITS_FROM_SERVER, [](const input::TGameAction&) {
        g_game.requestOutfit();
        return true;
    });

    m_hooks.emplace_back("g_game", "onOutfitWindowOptions", [this](LuaObject*, const Args& args) {
        TSelectOutfitDialogController::WindowOptions options{ .mounted = client::gamecore::argBool(args, 1),
                                                              .randomMount = client::gamecore::argBool(args, 2) };
        if (const auto* pairs = arg<std::vector<std::tuple<uint16_t, uint16_t>>>(args, 3))
            for (const auto& [male, female] : *pairs)
                options.genderPairs.append({ male, female });
        const auto offers = [&args](const size_t index) {
            QList<int> list;
            if (const auto* ids = arg<std::vector<uint32_t>>(args, index))
                for (const uint32_t id : *ids)
                    list.append(static_cast<int>(id));
            return list;
        };
        options.outfitOffers = offers(4);
        options.mountOffers = offers(5);
        options.summonOffers = offers(6);
        m_outfitDialog->setWindowOptions(std::move(options));
        return std::any{};
    });

    m_hooks.emplace_back("g_game", "onOpenOutfitWindow", [this](LuaObject*, const Args& args) {
        const auto* creature = arg<CreaturePtr>(args, 0);
        if (!creature || !*creature)
            return std::any{};

        QList<Entry> outfits;
        if (const auto* list = arg<std::vector<std::tuple<uint16_t, std::string, uint8_t, uint8_t>>>(args, 1)) {
            for (const auto& [id, name, addons, mode] : *list)
                outfits.append({ .id = id, .name = qstr(name), .firstAddOn = (addons & 1) != 0, .secondAddOn = (addons & 2) != 0,
                                 .type = mode, .canBeColorized = canBeColorized(id) });
        }
        QList<Entry> mounts;
        if (const auto* list = arg<std::vector<std::tuple<uint16_t, std::string, uint8_t>>>(args, 3)) {
            for (const auto& [id, name, mode] : *list)
                mounts.append({ .id = id, .name = qstr(name), .type = mode, .canBeColorized = canBeColorized(id) });
        }
        QList<Entry> summons;
        if (const auto* list = arg<std::vector<std::tuple<uint16_t, std::string>>>(args, 4)) {
            for (const auto& [id, name] : *list)
                summons.append({ .id = id, .name = qstr(name) });
        }
        m_outfitDialog->open((*creature)->getOutfit(), std::move(outfits), std::move(mounts), std::move(summons));
        return std::any{};
    });

    m_hooks.emplace_back("g_game", "onGameEnd", [this](LuaObject*, const Args&) {
        m_outfitDialog->setWindowOptions({});
        m_questLog->clear();
        m_highscores->clear();
        m_prey->clear();
        return std::any{};
    });
}

void TInformationDialogs::hookCreaturePodium()
{
    using RaceRows = std::vector<TConfigureCreaturePodiumDialogController::RaceRow>;
    m_hooks.emplace_back("g_game", "onConfigureCreaturePodium", [this](LuaObject*, const Args& args) {
        static const RaceRows emptyRaces;
        const Outfit selected = arg<Outfit>(args, 0) ? *arg<Outfit>(args, 0) : Outfit{};
        const bool bossPodium = client::gamecore::argInt(args, 1) != 0;
        const auto* races = arg<RaceRows>(args, 2);
        const Position pos = arg<Position>(args, 3) ? *arg<Position>(args, 3) : Position{};
        m_podiumDialog->open(selected, bossPodium, races ? *races : emptyRaces, pos,
                             static_cast<uint16_t>(client::gamecore::argInt(args, 4)),
                             static_cast<uint8_t>(client::gamecore::argInt(args, 5)),
                             client::gamecore::argInt(args, 6) != 0, client::gamecore::argInt(args, 7) != 0,
                             static_cast<uint8_t>(client::gamecore::argInt(args, 8)));
        return std::any{};
    });
}

void TInformationDialogs::hookTextDialogs()
{
    using client::gamecore::argInt;
    using client::gamecore::argString;

    m_hooks.emplace_back("g_game", "onModalDialog", [this](LuaObject*, const Args& args) {
        using Options = TServerModalDialogController::Options;
        static const Options none;
        const auto* buttons = arg<Options>(args, 3);
        const auto* choices = arg<Options>(args, 6);
        m_modalDialog->open(argInt(args, 0), argString(args, 1), argString(args, 2), buttons ? *buttons : none, argInt(args, 4),
                            argInt(args, 5), choices ? *choices : none);
        return std::any{};
    });

    m_hooks.emplace_back("g_game", "onEditText", [this](LuaObject*, const Args& args) {
        m_editText->showItemText(argInt(args, 0), argInt(args, 1), argInt(args, 2), argString(args, 3), argString(args, 4),
                                 argString(args, 5), client::gamecore::argBool(args, 6));
        return std::any{};
    });

    m_hooks.emplace_back("g_game", "onEditGuildMessage", [this](LuaObject*, const Args& args) {
        m_editText->showEditGuildMessageDialog(argString(args, 0));
        return std::any{};
    });

    m_hooks.emplace_back("g_game", "onEditList", [this](LuaObject*, const Args& args) {
        m_editText->showEditList(argInt(args, 0), argInt(args, 1), argString(args, 2));
        return std::any{};
    });
}

void TInformationDialogs::hookQuestLog()
{
    input::setActionHandler(A::OPEN_QUESTLOG_DIALOG, [this](const input::TGameAction& action) {
        m_questLog->showDialog(action.parameter);
        return true;
    });

    m_hooks.emplace_back("g_game", "onQuestLog", [this](LuaObject*, const Args& args) {
        if (const auto* lines = arg<std::vector<std::tuple<uint16_t, std::string, bool>>>(args, 0))
            m_questLog->setQuestLines(*lines);
        return std::any{};
    });

    m_hooks.emplace_back("g_game", "onQuestLine", [this](LuaObject*, const Args& args) {
        if (const auto* missions = arg<std::vector<std::tuple<std::string, std::string, uint16_t>>>(args, 1))
            m_questLog->setQuestLineParts(client::gamecore::argInt(args, 0), *missions);
        return std::any{};
    });

    // CIP never sends its stored flags on login (0x100f728b0 only runs from the tracker game actions);
    // Crystal keeps none across sessions and only answers a sent list, so remaining stays 0 otherwise.
    m_hooks.emplace_back("g_game", "onGameStart", [this](LuaObject*, const Args&) {
        m_questLog->ensureStorage();
        m_questLog->tracker()->sendStored();
        return std::any{};
    });

    m_hooks.emplace_back("g_game", "onQuestTracker", [this](LuaObject*, const Args& args) {
        using Missions = std::vector<std::tuple<uint16_t, uint16_t, std::string, std::string, std::string>>;
        const auto* missions = arg<Missions>(args, 1);
        if (!missions)
            return std::any{};
        QList<TQuestTrackerStorage::Entry> entries;
        for (const auto& [questLineId, questFlagId, questLineName, part, description] : *missions)
            entries.append({ .questFlagId = questFlagId,
                             .questLineId = questLineId,
                             .questLineName = qstr(questLineName),
                             .questLinePart = qstr(part),
                             .description = qstr(description) });
        m_questLog->ensureStorage();
        m_questLog->tracker()->setTrackedQuestFlags(std::move(entries), client::gamecore::argInt(args, 0));
        return std::any{};
    });

    m_hooks.emplace_back("g_game", "onUpdateQuestTracker", [this](LuaObject*, const Args& args) {
        using client::gamecore::argInt;
        using client::gamecore::argString;
        m_questLog->tracker()->updateQuestFlag({ .questFlagId = static_cast<int>(argInt(args, 1)),
                                                 .questLineId = static_cast<int>(argInt(args, 0)),
                                                 .questLineName = argString(args, 2),
                                                 .questLinePart = argString(args, 3),
                                                 .description = argString(args, 4) });
        return std::any{};
    });
}

void TInformationDialogs::hookHighscores()
{
    using client::gamecore::argBool;
    using client::gamecore::argInt;
    using client::gamecore::argString;

    input::setActionHandler(A::OPEN_HIGHSCORES_DIALOG, [this](const input::TGameAction&) {
        m_highscores->openDialog();
        return true;
    });

    m_hooks.emplace_back("g_game", "onProcessHighscores", [this](LuaObject*, const Args& args) {
        THighscoresDialogController::Answer answer;
        if (const auto* worlds = arg<std::vector<std::string>>(args, 10))
            answer.worlds = *worlds;
        answer.selectedWorld = argString(args, 1).toStdString();
        answer.worldTypeCount = static_cast<uint8_t>(argInt(args, 2));
        answer.battlEye = static_cast<uint8_t>(argInt(args, 3));
        if (const auto* vocations = arg<THighscoresDialogController::Vocations>(args, 4))
            answer.vocations = *vocations;
        if (const auto* categories = arg<THighscoresDialogController::Categories>(args, 5))
            answer.categories = *categories;
        answer.page = static_cast<uint16_t>(argInt(args, 6));
        answer.totalPages = static_cast<uint16_t>(argInt(args, 7));
        if (const auto* entries = arg<THighscoresDialogController::Entries>(args, 8))
            answer.entries = *entries;
        answer.timestamp = static_cast<uint32_t>(argInt(args, 9));
        answer.allVocationsName = argString(args, 11).toStdString();
        answer.selectedVocation = static_cast<uint32_t>(argInt(args, 12));
        answer.selectedCategory = static_cast<uint8_t>(argInt(args, 13));
        answer.selectedIndex = static_cast<uint8_t>(argInt(args, 14));
        answer.showLoyaltyTitle = argBool(args, 15);
        answer.valueType = static_cast<uint8_t>(argInt(args, 16));
        m_highscores->setAnswer(std::move(answer));
        return std::any{};
    });

    m_hooks.emplace_back("g_game", "onHighscoresNoData", [this](LuaObject*, const Args&) {
        m_highscores->setNoData();
        return std::any{};
    });
}

namespace {

prey::TPreyMonster preyMonster(const std::string& name, const Outfit& outfit)
{
    return { .name = QString::fromStdString(name),
             .outfitId = outfit.getId(),
             .head = outfit.getHead(),
             .torso = outfit.getBody(),
             .legs = outfit.getLegs(),
             .detail = outfit.getFeet(),
             .firstAddOn = (outfit.getAddons() & 1) != 0,
             .secondAddOn = (outfit.getAddons() & 2) != 0 };
}

QList<prey::TPreyMonster> preyMonsters(const Args& args, const size_t namesIndex, const size_t outfitsIndex)
{
    QList<prey::TPreyMonster> monsters;
    const auto* names = arg<std::vector<std::string>>(args, namesIndex);
    const auto* outfits = arg<std::vector<Outfit>>(args, outfitsIndex);
    if (!names || !outfits)
        return monsters;
    for (size_t i = 0; i < std::min(names->size(), outfits->size()); ++i)
        monsters.append(preyMonster((*names)[i], (*outfits)[i]));
    return monsters;
}

QList<uint16_t> preyRaces(const Args& args, const size_t index)
{
    const auto* races = arg<std::vector<uint16_t>>(args, index);
    return races ? QList<uint16_t>(races->begin(), races->end()) : QList<uint16_t>{};
}

prey::TPreyBonus preyBonus(const Args& args, const size_t index)
{
    using client::gamecore::argInt;
    return { .type = static_cast<int>(argInt(args, index)), .value = static_cast<int>(argInt(args, index + 1)),
             .grade = static_cast<int>(argInt(args, index + 2)) };
}

}

void TInformationDialogs::hookPrey()
{
    using client::gamecore::argInt;
    using client::gamecore::argString;

    input::setActionHandler(A::OPEN_PREY_DIALOG, [this](const input::TGameAction&) {
        m_prey->showDialog();
        return true;
    });
    // CIP TPreyGameActionHandler: the PreyAction message (0 list reroll,
    // 1 bonus reroll, 2 list choice, 3 bestiary list, 4 bestiary choice,
    // 5 automatic extension).
    const auto preyAction = [](const A type, const uint8_t actionType) {
        input::setActionHandler(type, [actionType](const input::TGameAction& action) {
            g_game.preyAction(static_cast<uint8_t>(action.instance), actionType, static_cast<uint16_t>(action.parameter));
            return true;
        });
    };
    preyAction(A::PREY_REQUEST_LIST_REROLL, 0);
    preyAction(A::PREY_REQUEST_BONUS_REROLL, 1);
    preyAction(A::PREY_SELECT_MONSTER_FROM_LIST, 2);
    preyAction(A::PREY_REQUEST_FREEZE_AND_SHOW_BESTIARY_MONSTERS, 3);
    preyAction(A::PREY_SELECT_MONSTER_FROM_BESTIARY, 4);
    preyAction(A::PREY_REQUEST_CHANGE_AUTO_EXPAND_TYPE, 5);

    // Every slot state ends with the free list reroll (seconds from now) and
    // the automatic extension option (CIP's "wildcards" outside the
    // active state).
    const auto slotData = [this](const Args& args, const size_t rerollIndex, const size_t optionIndex) -> prey::TPreyData* {
        prey::TPreyData* data = m_prey->storage()->slot(static_cast<int>(argInt(args, 0)));
        if (!data)
            return nullptr;
        data->setNextFreeListReroll(argInt(args, rerollIndex));
        data->setAutomaticExtendType(static_cast<int>(argInt(args, optionIndex)));
        return data;
    };
    const auto received = [this](const Args& args) { m_prey->storage()->markReceived(static_cast<int>(argInt(args, 0))); };

    m_hooks.emplace_back("g_game", "onPreyLocked", [slotData, received](LuaObject*, const Args& args) {
        if (prey::TPreyData* data = slotData(args, 2, 3)) {
            const auto* unlock = arg<Otc::PreyUnlockState_t>(args, 1);
            data->setLocked(unlock ? *unlock : Otc::PREY_UNLOCK_NONE);
            received(args);
        }
        return std::any{};
    });
    m_hooks.emplace_back("g_game", "onPreyInactive", [slotData, received](LuaObject*, const Args& args) {
        if (prey::TPreyData* data = slotData(args, 1, 2)) {
            data->setInactive();
            received(args);
        }
        return std::any{};
    });
    m_hooks.emplace_back("g_game", "onPreyActive", [slotData, received](LuaObject*, const Args& args) {
        if (prey::TPreyData* data = slotData(args, 7, 9)) {
            const auto* outfit = arg<Outfit>(args, 2);
            data->setActive(preyBonus(args, 3), preyMonster(argString(args, 1).toStdString(), outfit ? *outfit : Outfit{}),
                            static_cast<int>(argInt(args, 6)));
            received(args);
        }
        return std::any{};
    });
    m_hooks.emplace_back("g_game", "onPreySelection", [slotData, received](LuaObject*, const Args& args) {
        if (prey::TPreyData* data = slotData(args, 3, 4)) {
            data->setSelection(preyMonsters(args, 1, 2));
            received(args);
        }
        return std::any{};
    });
    m_hooks.emplace_back("g_game", "onPreySelectionChangeMonster", [slotData, received](LuaObject*, const Args& args) {
        if (prey::TPreyData* data = slotData(args, 6, 7)) {
            data->setSelectionChangeMonster(preyBonus(args, 3), preyMonsters(args, 1, 2));
            received(args);
        }
        return std::any{};
    });
    m_hooks.emplace_back("g_game", "onPreyListSelection", [slotData, received](LuaObject*, const Args& args) {
        if (prey::TPreyData* data = slotData(args, 2, 3)) {
            data->setListSelection(preyRaces(args, 1));
            received(args);
        }
        return std::any{};
    });
    m_hooks.emplace_back("g_game", "onPreyWildcardSelection", [slotData, received](LuaObject*, const Args& args) {
        if (prey::TPreyData* data = slotData(args, 2, 3)) {
            data->setWildcardSelection(preyBonus(args, 4), preyRaces(args, 1));
            received(args);
        }
        return std::any{};
    });
    m_hooks.emplace_back("g_game", "onPreyTimeLeft", [this](LuaObject*, const Args& args) {
        if (prey::TPreyData* data = m_prey->storage()->slot(static_cast<int>(argInt(args, 0))))
            data->setTimeLeft(static_cast<int>(argInt(args, 1)));
        return std::any{};
    });
    // CIP PreyFreeRerolls (S 0xEE): next free list reroll countdown for one slot.
    m_hooks.emplace_back("g_game", "onPreyFreeRerolls", [this](LuaObject*, const Args& args) {
        if (prey::TPreyData* data = m_prey->storage()->slot(static_cast<int>(argInt(args, 0))))
            data->setNextFreeListReroll(argInt(args, 1));
        return std::any{};
    });
    m_hooks.emplace_back("g_game", "onPreyRerollPrice", [this](LuaObject*, const Args& args) {
        m_prey->storage()->setPrices(static_cast<int>(argInt(args, 0)), static_cast<int>(argInt(args, 1)), static_cast<int>(argInt(args, 2)));
        return std::any{};
    });
    m_hooks.emplace_back("g_game", "onResourcesBalanceChange", [this](LuaObject*, const Args&) {
        m_prey->onBalanceChanged();
        return std::any{};
    });
    // CIP 0x100ee4ec0: ShowMessageDialog types 0x14 and 0x15 are the prey info box.
    m_hooks.emplace_back("g_game", "onServerError", [this](LuaObject*, const Args& args) {
        if ((argInt(args, 0) & ~1) == 0x14)
            m_prey->onRequestShowMessageDialog(argString(args, 1));
        return std::any{};
    });
}

}

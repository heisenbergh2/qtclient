#include "actionbuttoncontroller.h"

#include "gamewindow/actionbarcontroller.h"
#include "gamewindow/contextmenucontroller.h"
#include "input/gameactionhandlers.h"
#include "spells/spellinfostorage.h"

#include <tibia/world/game.h>
#include <tibia/world/localplayer.h>
#include <tibia/assets/thingtype.h>
#include <tibia/assets/things.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace tibia::gamewindow {

namespace {

using Base = cip::tibia::gamewindow::TActionButtonController;
using Setting = config::TActionSetting;
using Info = cip::tibia::qmlcomponents::TObjectAppearanceInstanceInfoQmlType;
using A = input::EGameAction;

/// CIP's multi-action buttons have three slots (FUN_100c90090(button, 3)).
constexpr int MultiActionSlots = 3;

QString objectDisplayName(const int objectId, const std::optional<int>& tier)
{
    QString name;
    if (const ThingTypePtr& type = g_things.getThingType(objectId, ThingCategoryItem); type && !type->getName().empty())
        name = QString::fromStdString(type->getName());
    if (name.isEmpty())
        name = qtTrId("actionbar_tooltip_object_name_dummy");
    if (tier && *tier > 0)
        name = qtTrId("actionbar_tooltip_object_name_with_tier").arg(name).arg(*tier);
    return name;
}

bool isMultiUse(const int objectId)
{
    const ThingTypePtr& type = g_things.getThingType(objectId, ThingCategoryItem);
    return type && type->isMultiUse();
}

/// CIP FUN_100c8ed70.
QString row(const char* labelId, const QString& value)
{
    return QStringLiteral("<tr>\n  <td style=\"text-align:right; padding-right:%3px\">%1:</td>\n  <td >%2</td>\n</tr>\n")
        .arg(qtTrId(labelId), value, QString::number(5));
}

/// CIP FUN_10095ee80.
QString durationText(const qint64 seconds)
{
    if (seconds < 60)
        return qtTrId("time_second_short").arg(seconds);
    if (seconds % 60 == 0)
        return qtTrId("time_minutes_short").arg(seconds / 60);
    return qtTrId("time_minutes_and_seconds_short").arg(seconds / 60).arg(seconds % 60);
}

}

TActionButtonController::TActionButtonController(TActionBarController* bar, const int buttonId, const int multiActionIndex, QObject* parent)
    : Base(parent)
    , m_bar(bar)
{
    m_actionBarID = uint(bar->actionBarId());
    m_actionButtonID = uint(buttonId);
    m_multiActionIndex = multiActionIndex;
    m_isEnabled = true;
}

template <class T>
void TActionButtonController::set(T& member, const T& value, bool& changed)
{
    if (member == value)
        return;
    member = value;
    changed = true;
}

void TActionButtonController::clear()
{
    m_type = EMPTY;
    m_progressType = COOLDOWN;
    m_spellIconUrl.clear();
    m_spellParameters.clear();
    m_text.clear();
    m_objectID = 0;
    m_objectCount = 0;
    m_objectUpgradeTier = 0;
    m_liquidType = Info::ELiquidType::EMPTY;
    m_hookDirection = Info::EHookDirection::NONE;
    m_objectIsEquiped = false;
    m_isEquipeAction = false;
    m_passiveAbilityIconUrl.clear();
    m_spellId = 0;
}

void TActionButtonController::setSetting(const Setting& setting, const QKeySequence& hotkey)
{
    if (setting == m_setting && hotkey == m_hotkey)
        return;
    m_setting = setting;
    m_hotkey = hotkey;
    clear();
    m_hotkeyShort = hotkey.toString(QKeySequence::NativeText);

    switch (setting.kind) {
    case Setting::TEXT:
        if (const spells::TSpellInfo* spell = spells::TSpellInfoStorage::instance()->spellOfText(setting.text); spell && setting.sendAutomatically) {
            m_type = SPELL;
            m_spellId = spell->id;
            m_spellIconUrl = spells::TSpellInfoStorage::iconUrl(*spell);
            m_spellParameters = setting.text.trimmed().mid(spell->words.size()).trimmed();
        } else {
            m_type = TEXT;
        }
        m_text = setting.text;
        break;
    case Setting::OBJECT:
        m_type = OBJECT;
        m_objectID = uint(setting.objectId);
        m_objectUpgradeTier = uint(setting.upgradeTier.value_or(0));
        m_liquidType = Info::ELiquidType(setting.liquidType.value_or(0));
        m_isEquipeAction = setting.useType == config::EUseType::Equip;
        break;
    case Setting::PASSIVE_ABILITY:
        m_type = PASSIVEABILITY;
        m_passiveAbilityIconUrl = QStringLiteral("image://passiveability-icons-32x32/%1").arg(setting.passiveAbility);
        break;
    default:
        break;
    }

    if (setting.kind == Setting::MULTI_ACTION && !isMultiActionChild()) {
        if (m_children.empty()) {
            for (int i = 0; i < MultiActionSlots; ++i) {
                auto* child = new TActionButtonController(m_bar, int(m_actionButtonID), i, this);
                m_children.push_back(child);
                m_multiActions << child;
                const auto relay = [this, child](void (Base::*signal)()) {
                    connect(child, signal, this, [this, child, signal] {
                        if (m_shownSlot == child)
                            Q_EMIT(this->*signal)();
                    });
                };
                relay(&Base::changed);
                relay(&Base::appearanceChanged);
                relay(&Base::cooldownRemainingPercentChanged);
                relay(&Base::cooldownTextChanged);
            }
        }
        for (int i = 0; i < MultiActionSlots; ++i)
            m_children[i]->setSetting(i < int(setting.multiActions.size()) ? setting.multiActions[i] : Setting{}, {});
    } else if (!m_children.empty()) {
        m_shownSlot = nullptr;
        m_multiActions.clear();
        for (TActionButtonController* child : std::exchange(m_children, {}))
            child->deleteLater();
        m_multiActionExpanded = false;
        Q_EMIT multiActionExpandedChanged();
    }

    refreshObject();
    m_showsCooldown = false;
    m_cooldownRemainingPercent = 0;
    m_cooldownText.clear();
    Q_EMIT changed();
    Q_EMIT appearanceChanged();
    Q_EMIT cooldownRemainingPercentChanged();
    Q_EMIT cooldownTextChanged();
}

QString TActionButtonController::tooltip() const
{
    QString text;
    if (!isMultiActionChild())
        text = qtTrId("actionbar_button_identifier").arg(m_actionBarID).arg(m_actionButtonID, 2, 10, QLatin1Char('0')) + QStringLiteral("<br>\n");
    text += QStringLiteral("<table>\n");
    text += shown()->tooltipRows();
    text += QStringLiteral("</table>\n");
    return text;
}

QString TActionButtonController::tooltipRows() const
{
    QString rows;
    switch (m_type) {
    case EMPTY:
        rows += row("actionbar_tooltip_label_action", qtTrId("none"));
        break;
    case SPELL:
        if (const spells::TSpellInfo* spell = spells::TSpellInfoStorage::instance()->spell(m_spellId)) {
            rows += row("actionbar_tooltip_label_action", qtTrId("actionbar_tooltip_spell_action").arg(spell->name));
            QString formula = spell->words;
            if (!m_spellParameters.isEmpty())
                formula += QLatin1Char(' ') + m_spellParameters.toHtmlEscaped();
            rows += row("actionbar_tooltip_label_formula", formula);
            rows += row("cooldown", durationText(spell->cooldownSelf));
            rows += row("mana", spell->mana == 0 ? qtTrId("actionbar_tooltip_mana_variable") : QString::number(spell->mana));
            if (spell->soul != 0)
                rows += row("soul_points", QString::number(spell->soul));
        }
        break;
    case OBJECT: {
        const char* actionId = nullptr;
        switch (m_setting.useType) {
        case config::EUseType::Use:
            actionId = "actionbar_tooltip_use_object";
            break;
        case config::EUseType::UseOnYourself:
            actionId = "actionbar_tooltip_use_object_on_yourself";
            break;
        case config::EUseType::UseOnTarget:
            actionId = "actionbar_tooltip_use_object_on_target";
            break;
        case config::EUseType::SelectUseTarget:
            actionId = "actionbar_tooltip_use_object_with_crosshair";
            break;
        case config::EUseType::Equip:
            actionId = m_objectIsEquiped ? "actionbar_tooltip_unequip_object" : "actionbar_tooltip_equip_object";
            break;
        case config::EUseType::UseAtMouseCursor:
            actionId = "actionbar_tooltip_use_object_at_mousecursor";
            break;
        }
        const QString action = actionId ? qtTrId(actionId).arg(objectDisplayName(m_setting.objectId, m_setting.upgradeTier)) : QString();
        rows += row("actionbar_tooltip_label_action", action);
        rows += row("actionbar_tooltip_label_amount", QString::number(m_objectCount));
        break;
    }
    case TEXT:
        rows += row("actionbar_tooltip_label_action", qtTrId("actionbar_tooltip_text_action").arg(m_setting.text.toHtmlEscaped()));
        rows += row("actionbar_tooltip_label_sendautomatically", qtTrId(m_setting.sendAutomatically ? "yes" : "no"));
        break;
    case PASSIVEABILITY:
        // CIP FUN_100c8f250: the ability's name; qtclient has no passive ability data.
        rows += row("actionbar_tooltip_label_passiveability", QString());
        break;
    }
    if (!isMultiActionChild()) {
        // CIP FUN_100c8f350: each key, "<br>\n" between them.
        QStringList keys;
        for (const QKeySequence& key : m_hotkeys)
            keys.push_back(key.toString(QKeySequence::PortableText));
        rows += row("actionbar_tooltip_label_hotkeys", keys.isEmpty() ? qtTrId("none") : keys.join(QStringLiteral("<br>\n")));
    }
    return rows;
}

void TActionButtonController::refreshObject()
{
    for (TActionButtonController* child : m_children)
        child->refreshObject();
    if (!m_children.empty())
        refreshShownSlot(game::TCooldownStorage::now());
    if (m_type != OBJECT)
        return;
    uint count = 0;
    bool equipped = false;
    if (const LocalPlayerPtr& player = g_game.getLocalPlayer()) {
        const uint8_t tier = uint8_t(m_setting.upgradeTier.value_or(0));
        count = player->getInventoryCount(uint16_t(m_setting.objectId), tier);
        equipped = player->hasEquippedItemId(uint16_t(m_setting.objectId), tier);
    }
    bool changedAppearance = false;
    bool changedState = false;
    set(m_objectCount, count, changedAppearance);
    set(m_objectIsEquiped, equipped, changedState);
    if (changedAppearance)
        Q_EMIT appearanceChanged();
    if (changedAppearance || changedState)
        Q_EMIT changed();
}

game::TCooldown TActionButtonController::currentCooldown() const
{
    const game::TCooldownStorage* storage = game::TCooldownStorage::instance();
    if (!storage)
        return {};
    if (m_type == SPELL)
        return storage->spellCooldown(m_spellId);
    if (m_type == OBJECT) {
        if (const spells::TSpellInfo* rune = spells::TSpellInfoStorage::instance()->runeSpellOfObject(m_setting.objectId))
            return storage->spellCooldown(rune->id);
        if (isMultiUse(m_setting.objectId))
            return storage->multiUseCooldown();
    }
    return {};
}

bool TActionButtonController::isUsable() const
{
    if (m_type == SPELL) {
        const LocalPlayerPtr& player = g_game.getLocalPlayer();
        if (!player)
            return false;
        const std::vector<uint16_t>& learned = player->getSpells();
        return std::find(learned.begin(), learned.end(), uint16_t(m_spellId)) != learned.end();
    }
    if (m_type == OBJECT)
        return m_objectCount > 0;
    return true;
}

qint64 TActionButtonController::remainingCooldown(const qint64 now) const
{
    const game::TCooldownStorage* storage = game::TCooldownStorage::instance();
    if (!storage)
        return 0;
    game::TCooldown cooldown;
    if (m_type == SPELL) {
        cooldown = storage->spellCooldown(m_spellId);
    } else if (m_type == OBJECT) {
        if (const spells::TSpellInfo* rune = spells::TSpellInfoStorage::instance()->runeSpellOfObject(m_setting.objectId)) {
            cooldown = storage->spellCooldown(rune->id);
        } else {
            switch (m_setting.useType) {
            case config::EUseType::UseOnYourself:
            case config::EUseType::UseOnTarget:
            case config::EUseType::SelectUseTarget:
            case config::EUseType::UseAtMouseCursor:
                cooldown = storage->multiUseCooldown();
                break;
            default:
                break;
            }
        }
    }
    return std::max<qint64>(0, cooldown.end - now);
}

TActionButtonController* TActionButtonController::currentMultiAction(const qint64 now) const
{
    const int ping = g_game.getPing();
    const qint64 window = std::min<qint64>(std::max(ping, 0) / 2, 400);
    qint64 best = std::numeric_limits<qint64>::max();
    TActionButtonController* candidate = nullptr;
    for (TActionButtonController* child : m_children) {
        if (child->m_setting.isEmpty())
            continue;
        if (!child->isUsable()) {
            if (!candidate)
                candidate = child;
            continue;
        }
        const qint64 remaining = child->remainingCooldown(now);
        if (remaining == 0)
            return child;
        if (window + remaining < best) {
            candidate = child;
            best = remaining;
        }
    }
    return candidate;
}

void TActionButtonController::refreshShownSlot(const qint64 now)
{
    TActionButtonController* slot = currentMultiAction(now);
    if (slot == m_shownSlot)
        return;
    m_shownSlot = slot;
    Q_EMIT changed();
    Q_EMIT appearanceChanged();
    Q_EMIT cooldownRemainingPercentChanged();
    Q_EMIT cooldownTextChanged();
}

bool TActionButtonController::refreshCooldown(const qint64 now)
{
    bool running = false;
    for (TActionButtonController* child : m_children)
        running = child->refreshCooldown(now) || running;
    if (!m_children.empty())
        refreshShownSlot(now);

    const game::TCooldown cooldown = currentCooldown();
    const qint64 remaining = cooldown.end - now;
    uint percent = 0;
    QString text;
    if (remaining > 0 && cooldown.duration > 0) {
        const double fraction = std::min(1.0, double(remaining) / double(cooldown.duration));
        percent = uint(std::floor(fraction * 100.0 / 5.0) * 5.0);
        text = cooldownText(remaining);
        running = true;
    }
    if (percent != m_cooldownRemainingPercent) {
        m_cooldownRemainingPercent = percent;
        Q_EMIT cooldownRemainingPercentChanged();
    }
    if (text != m_cooldownText) {
        m_cooldownText = text;
        Q_EMIT cooldownTextChanged();
    }
    return running;
}

QString TActionButtonController::cooldownText(const qint64 remainingMs)
{
    if (remainingMs < 11000)
        return QString::number(double(remainingMs) / 1000.0, 'f', 1);
    if (remainingMs < 60000)
        return QString::number(remainingMs / 1000);
    if (remainingMs < 660000)
        return QStringLiteral("%1:%2").arg(remainingMs / 60000).arg((remainingMs / 1000) % 60, 2, 10, QLatin1Char('0'));
    if (remainingMs < 3660000)
        return QStringLiteral("%1m").arg(remainingMs / 60000);
    if (remainingMs < 36000000)
        return QStringLiteral("%1h%2").arg(remainingMs / 3600000).arg((remainingMs / 60000) % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1h").arg(remainingMs / 3600000);
}

void TActionButtonController::onTriggered()
{
    auto action = input::mutableClone(*input::makeAction(A::ACTION_BUTTON_TRIGGERED));
    action->actionBarId = int(m_actionBarID);
    action->actionButtonId = int(m_actionButtonID);
    action->multiActionIndex = m_multiActionIndex;
    input::publish(action);
    if (m_isNewlyUnlocked)
        setIsNewlyUnlocked(false);
}

void TActionButtonController::contextMenuRequested()
{
    TContextMenuController* menu = TContextMenuController::instance();
    if (!menu)
        return;
    const int buttonId = int(m_actionButtonID);
    const int index = m_multiActionIndex;
    TActionBarController* bar = m_bar;
    const auto entry = [&](const bool edit, const char* kindId, auto&& handler) {
        const QString kind = qtTrId(kindId);
        TContextMenuEntryItem* item = menu->addCustomEntry(edit ? qtTrId("actionbar_contextmenu_edit").arg(kind) : qtTrId("actionbar_contextmenu_assign").arg(kind));
        if (item)
            connect(item, &TContextMenuEntryItem::triggered, bar, std::forward<decltype(handler)>(handler));
    };

    menu->beginCustomMenu();
    entry(m_type == SPELL, "actionbar_contextmenu_spell", [bar, buttonId, index] { bar->openAssignDialog(TActionBarController::ASSIGN_SPELL, buttonId, index); });
    entry(m_type == OBJECT, "actionbar_contextmenu_object", [bar, buttonId, index] { bar->openAssignDialog(TActionBarController::ASSIGN_OBJECT, buttonId, index); });
    entry(m_type == TEXT, "actionbar_contextmenu_text", [bar, buttonId, index] { bar->openAssignDialog(TActionBarController::ASSIGN_TEXT, buttonId, index); });
    if (!isMultiActionChild()) {
        entry(m_type == PASSIVEABILITY, "actionbar_contextmenu_passiveability", [bar, buttonId, index] { bar->openAssignDialog(TActionBarController::ASSIGN_PASSIVE_ABILITY, buttonId, index); });
        if (m_setting.kind != Setting::MULTI_ACTION) {
            entry(false, "actionbar_contextmenu_multiaction", [bar, buttonId] {
                Setting multi;
                multi.kind = Setting::MULTI_ACTION;
                multi.multiActions.resize(MultiActionSlots);
                bar->assign(buttonId, -1, multi);
                if (TActionButtonController* button = bar->button(buttonId))
                    button->setMultiActionExpanded(true);
            });
        } else if (m_multiActionExpanded) {
            TContextMenuEntryItem* item = menu->addCustomEntry(qtTrId("actionbar_contextmenu_close").arg(qtTrId("actionbar_contextmenu_multiaction")));
            if (item)
                connect(item, &TContextMenuEntryItem::triggered, this, [this] { setMultiActionExpanded(false); });
        } else {
            TContextMenuEntryItem* item = menu->addCustomEntry(qtTrId("actionbar_contextmenu_edit").arg(qtTrId("actionbar_contextmenu_multiaction")));
            if (item)
                connect(item, &TContextMenuEntryItem::triggered, this, [this] { setMultiActionExpanded(true); });
        }
        entry(!m_hotkey.isEmpty(), "actionbar_contextmenu_hotkey", [bar, buttonId] { bar->editHotkey(buttonId); });
    }
    if (!m_setting.isEmpty()) {
        menu->addCustomSeparator();
        if (TContextMenuEntryItem* item = menu->addCustomEntry(qtTrId("actionbar_contextmenu_clear_action")))
            connect(item, &TContextMenuEntryItem::triggered, bar, [bar, buttonId, index] { bar->assign(buttonId, index, Setting{}); });
    }
    menu->showCustomMenu();
}

void TActionButtonController::onTargetSelected()
{
    input::finishTargetSelection(nullptr);
}

void TActionButtonController::onDragChanged(const bool Dragged)
{
    if (Dragged)
        m_bar->closeOpenMultiActionButtons();
}

void TActionButtonController::setMultiActionExpanded(const bool v)
{
    if (v == m_multiActionExpanded)
        return;
    if (v)
        m_bar->closeOpenMultiActionButtons();
    Base::setMultiActionExpanded(v);
}

}

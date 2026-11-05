#include "gameconfig.h"

#include "framework/core/configmanager.h"
#include "mapviewport.h"

GameConfig g_gameConfig;

void GameConfig::init()
{
    // data/setup.otml values the client reads. The file stays on disk so
    // gamecore can still discover the work directory; it is not parsed.
    m_spriteSize = 32;
    m_drawTyping = false;
    m_typingIcon = "/images/game/console/typing";

    // Astralis 15.30 uses the standard 18x14 wire map (8/9 left/right,
    // 6/7 top/bottom). A larger receive area consumes subsequent packet
    // fields as tiles and desynchronizes the initial 0x64 map description.
    // Keep the legacy visible area; setup.otml is not parsed here.
    m_visibleSqm = 0;
    m_mapViewPort = Size(8, 6);
    m_extendedViewUI = false;
    m_mapMaxZ = 15;
    m_mapSeaFloor = 7;
    m_mapUndergroundFloorRange = 8;
    m_mapAwareUndergroundFloorRange = 2;

    m_tileMaxElevation = 24;
    m_tileMaxThings = 10;
    m_tileTransparentFloorViewRange = 2;

    m_drawInformationByWidget = false;
    m_forceNewWalkingFormula = false;
    m_shieldBlinkTicks = 500;
    m_volatileSquareDuration = 1000;
    m_adjustCreatureInformationBasedCropSize = false;
    m_useCropSizeForUIDraw = true;
    m_creatureDiagonalWalkSpeed = 3;
    m_playerDiagonalWalkSpeed = 3;

    m_invisibleTicksPerFrame = 75;
    m_itemTicksPerFrame = 75;
    m_effectTicksPerFrame = 75;
    m_missileTicksPerFrame = 75;
    m_animatedTextDuration = 1000;
    m_staticDurationPerCharacter = 60;
    m_minStatictextDuration = 3000;

    m_widgetTextFontName = "verdana-11px-antialised";
    m_staticTextFontName = "verdana-11px-rounded";
    m_animatedTextFontName = "verdana-11px-rounded";
    m_creatureNameFontName = "verdana-11px-rounded";
    m_itemCountFontName = "verdana-10px-rounded";

    const auto& publicFont = g_configs.getPublicConfig().font;
    if (!publicFont.widget.empty()) m_widgetTextFontName = publicFont.widget;
    if (!publicFont.staticText.empty()) m_staticTextFontName = publicFont.staticText;
    if (!publicFont.animatedText.empty()) m_animatedTextFontName = publicFont.animatedText;
    if (!publicFont.creatureText.empty()) m_creatureNameFontName = publicFont.creatureText;
    if (!publicFont.itemCount.empty()) m_itemCountFontName = publicFont.itemCount;
}

void GameConfig::terminate() {}

void GameConfig::loadFonts() {}

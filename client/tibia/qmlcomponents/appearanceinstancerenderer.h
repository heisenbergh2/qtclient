#pragma once

#include <cip_gen.h>

#include "renderer/spritetexturecache.h"

#include <QElapsedTimer>
#include <QPointer>

class ThingType;

namespace tibia::qmlcomponents {

/// An object placed in an AppearanceInstanceRenderer (QML ObjectAppearanceInstance;
/// CIP: TObjectAppearanceInstanceInfoQmlType). shift is the type's displacement,
/// elevation its elevation.
class TObjectAppearanceInstanceInfoQmlType : public cip::tibia::qmlcomponents::TObjectAppearanceInstanceInfoQmlType
{
    Q_OBJECT
public:
    explicit TObjectAppearanceInstanceInfoQmlType(QObject* parent = nullptr);

    void setTypeid(int v) override;
    void setPosition(const QPoint& v) override;
    void setCumulativeCount(int v) override;
    void setLiquidType(ELiquidType v) override;
    void setHookDirection(EHookDirection v) override;

    ThingType* type() const;
};

/// A creature outfit placed in an AppearanceInstanceRenderer (QML OutfitAppearanceInstance;
/// CIP: TOutfitAppearanceInstanceQmlType).
class TOutfitAppearanceInstanceQmlType : public cip::tibia::qmlcomponents::TOutfitAppearanceInstanceQmlType
{
    Q_OBJECT
public:
    explicit TOutfitAppearanceInstanceQmlType(QObject* parent = nullptr);

    void setTypeid(int v) override;
    void setPosition(const QPoint& v) override;

    ThingType* type() const;
    /// The palette indices of the colours, as the sprite cache takes them.
    uint32_t colors() const;
};

/// Draws a list of appearance instances placed in world pixels (QML
/// AppearanceInstanceRenderer; CIP: TAppearanceInstanceRenderer, layout v54 0x1010b3610).
/// An instance's position is the top-left of its field; sprites end at the field's
/// bottom-right (CIP UI / SingleObject — no ThingType displacement here; callers such
/// as OutfitPreviewPanel bake shift into position). cameraPosition is the world pixel
/// at the item's top-left; with center / shrinkToFit the opaque protobuf bounding
/// boxes (else the sprite union) are centered / scaled down (never past scaleFactor).
class TAppearanceInstanceRenderer : public cip::tibia::qmlcomponents::TAppearanceInstanceRenderer
{
    Q_OBJECT
public:
    explicit TAppearanceInstanceRenderer(QQuickItem* parent = nullptr);
    ~TAppearanceInstanceRenderer() override;

    QQmlListProperty<cip::tibia::qmlcomponents::TAppearanceInstanceInfoQmlType> appearanceInstances() const override;
    void setCameraPosition(const QPoint& v) override;
    void setCenter(bool v) override;
    void setShrinkToFit(bool v) override;
    void setAnimated(bool v) override;
    void setScaleFactor(float v) override;
    void setSmoothTextureFiltering(bool v) override;
    void refreshAppearanceInstances() override { polish(); }
    void refreshAppearanceInstances(bool) override { polish(); }
    /// RaceAppearanceInstanceRenderer.qml: the race's outfit (or object) from the static race
    /// data. The previous instance this renderer made is released.
    cip::tibia::qmlcomponents::TAppearanceInstanceInfoQmlType* createAppearanceInstanceByRaceID(int RaceID) override;
    QString getRaceName(int RaceID) override;

protected:
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData& value) override;
    void updatePolish() override;
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;

private:
    struct Sprite
    {
        SpriteKey key;
        /// Bottom-right corner in world pixels.
        QPoint anchor;
        QSize size;
    };

    static void append(QQmlListProperty<cip::tibia::qmlcomponents::TAppearanceInstanceInfoQmlType>* list, cip::tibia::qmlcomponents::TAppearanceInstanceInfoQmlType* instance);
    static qsizetype count(QQmlListProperty<cip::tibia::qmlcomponents::TAppearanceInstanceInfoQmlType>* list);
    static cip::tibia::qmlcomponents::TAppearanceInstanceInfoQmlType* at(QQmlListProperty<cip::tibia::qmlcomponents::TAppearanceInstanceInfoQmlType>* list, qsizetype index);
    static void clear(QQmlListProperty<cip::tibia::qmlcomponents::TAppearanceInstanceInfoQmlType>* list);

    void watchFrames();
    void addSprites(ThingType* type, const QPoint& fieldTopLeft, int xPattern, int yPattern, int zPattern, int phase, bool colorize, uint32_t colors);
    void addOutfit(const TOutfitAppearanceInstanceQmlType& instance);
    void addObject(const TObjectAppearanceInstanceInfoQmlType& instance);
    int outfitPhase(ThingType* type, bool moving, int millisecondsPerField) const;

    QList<Sprite> m_sprites;
    /// Item pixel = (world pixel - m_origin) * m_scale.
    QPointF m_origin;
    float m_scale = 1.f;
    QMetaObject::Connection m_frameConnection;
    QElapsedTimer m_sinceLayout;
    QPointer<QObject> m_raceInstance;
};

}

#include "worldmapquickitem.h"
#include "tibia/runtime/host.h"

#include "renderer/childrenscenegraphnode.h"
#include "renderer/highlightedspritenode.h"
#include "renderer/mapscale.h"
#include "mods/worldview.h"
#include "renderer/scenegraphnodeprovider.h"
#include "renderer/spritetextureprovider.h"

#include <framework/core/graphicalapplication.h>
#include "sprites/hdscale.h"

#include <algorithm>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QQuickWindow>
#include <QRunnable>
#include <QSGGeometryNode>
#include <QSGImageNode>
#include <QSGMaterial>
#include <QSGRendererInterface>
#include <QSGTransformNode>

#include <array>
#include <cmath>
#include <optional>
#include <type_traits>
#include <vector>

Q_LOGGING_CATEGORY(lcWorldMap, "tibia.worldmap", QtWarningMsg)

namespace tibia::qmlcomponents {

namespace {

class ReleaseTextures final : public QRunnable
{
public:
    explicit ReleaseTextures(std::unique_ptr<renderer::TSpriteTextureProvider> textures)
        : m_textures(std::move(textures))
    {}
    void run() override { m_textures.reset(); }

private:
    std::unique_ptr<renderer::TSpriteTextureProvider> m_textures;
};

}

TWorldMapQuickItem::TWorldMapQuickItem(QQuickItem* parent)
    : cip::tibia::qmlcomponents::TWorldMapQuickItem(parent)
{
    m_renderScaleFactor = 1.f;
    setFlag(ItemHasContents);
}

TWorldMapQuickItem::~TWorldMapQuickItem() = default;

void TWorldMapQuickItem::setRenderProvider(TWorldMapRenderProvider* provider)
{
    disconnect(m_frameConnection);
    disconnect(m_highlightConnection);
    m_provider = provider;
    // The client advances the provider once per frame (TNextFrameUpdater). The
    // item is hidden when the light Blend draws it.
    if (provider) {
        m_frameConnection = connect(provider, &TWorldMapRenderProvider::frameAdvanced, this, [this] {
            m_highlightOnly = false;
            Q_EMIT framePictureChanged();
            update();
        });
        m_highlightConnection = connect(provider, &TWorldMapRenderProvider::highlightAdvanced, this, [this] {
            m_highlightOnly = true;
            Q_EMIT framePictureChanged();
            update();
        });
    }
    update();
}

void TWorldMapQuickItem::itemChange(const ItemChange change, const ItemChangeData& value)
{
    if (change == ItemSceneChange) {
        disconnect(m_invalidatedConnection);
        if (QQuickWindow* window = value.window) {
            m_invalidatedConnection = connect(window, &QQuickWindow::sceneGraphInvalidated, this,
                                              [this] {
                                                  m_textures.reset();
                                                  clearNodePools();
                                              },
                                              Qt::DirectConnection);
        }
    }
    cip::tibia::qmlcomponents::TWorldMapQuickItem::itemChange(change, value);
}

void TWorldMapQuickItem::releaseResources()
{
    clearNodePools();
    if (m_textures && window())
        window()->scheduleRenderJob(new ReleaseTextures(std::move(m_textures)), QQuickWindow::NoStage);
}

void TWorldMapQuickItem::recycleNode(QSGNode* node)
{
    if (!node)
        return;
    if (node->firstChild())
        harvestNodes(node);
    if (node->type() == QSGNode::OpacityNodeType) {
        auto* opacity = static_cast<renderer::TChildrenScenegraphNode*>(node);
        opacity->setOpacity(1);
        m_opacityPool.push_back(opacity);
        return;
    }
    if (node->type() == QSGNode::GeometryNodeType) {
        auto* geo = static_cast<QSGGeometryNode*>(node);
        static QSGMaterialType* highlightType = [] {
            static renderer::TSpriteDrawCommandHighlightMaterial sample;
            return sample.type();
        }();
        if (geo->material() && geo->material()->type() == highlightType) {
            m_highlightPool.push_back(static_cast<renderer::THighlightedSpriteScenegraphNode*>(node));
            return;
        }
        m_imagePool.push_back(static_cast<QSGImageNode*>(node));
        return;
    }
    delete node;
}

void TWorldMapQuickItem::harvestNodes(QSGNode* node)
{
    if (!node)
        return;
    while (QSGNode* child = node->firstChild()) {
        node->removeChildNode(child);
        recycleNode(child);
    }
}

void TWorldMapQuickItem::clearNodePools()
{
    for (renderer::TChildrenScenegraphNode* node : m_opacityPool)
        delete node;
    for (QSGImageNode* node : m_imagePool)
        delete node;
    for (renderer::THighlightedSpriteScenegraphNode* node : m_highlightPool)
        delete node;
    m_opacityPool.clear();
    m_imagePool.clear();
    m_highlightPool.clear();
}

QSGNode* TWorldMapQuickItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*)
{
    QElapsedTimer cost;
    cost.start();
    if (!m_provider || !m_provider->frame().valid) {
        qCDebug(lcWorldMap) << "no world" << (m_provider != nullptr);
        tibia::mods::abandonWorldOverlayNodes();
        harvestNodes(oldNode);
        delete oldNode;
        return nullptr;
    }
    const TWorldMapRenderProvider::Frame& frame = m_provider->frame();
    const bool hd = tibia::host::app().isHdMode();
    // Drop the whole tree when HD flips so no image node keeps a stale QSGTexture*.
    if (hd != m_paintedHd) {
        tibia::mods::abandonWorldOverlayNodes();
        harvestNodes(oldNode);
        delete oldNode;
        oldNode = nullptr;
        m_paintedHd = hd;
        m_highlights.clear();
        m_marks.clear();
        if (m_textures)
            m_textures->cache().clear();
    }

    if (m_highlightOnly && oldNode && !m_spritesPending) {
        m_highlightOnly = false;
        const float strength = frame.highlightStrength;
        for (HighlightBinding& highlight : m_highlights) {
            QColor color = highlight.color;
            color.setAlphaF(strength);
            highlight.node->set(highlight.texture, highlight.rect, color, highlight.smooth);
        }
        for (renderer::TChildrenScenegraphNode* mark : m_marks) {
            const qreal t = (strength - 0.375) / (0.6 - 0.375);
            mark->setOpacity(0.55 + std::clamp(t, 0.0, 1.0) * 0.45);
        }
        return oldNode;
    }
    m_highlightOnly = false;
    m_highlights.clear();
    m_marks.clear();

    auto* camera = static_cast<QSGTransformNode*>(oldNode);
    if (!camera)
        camera = new QSGTransformNode;
    if (!m_textures)
        m_textures = std::make_unique<renderer::TSpriteTextureProvider>(window());

    // CIP slot 46: camera translation ÷ render scale, rounded to whole pixels,
    // then translate(-x, -y). Scale lives on the QML MapWindowPane; the provider
    // already stores integer map pixels (field origin + walk offset), so the
    // node only translates.
    QMatrix4x4 matrix;
    matrix.translate(-frame.camera.x(), -frame.camera.y());
    camera->setMatrix(matrix);

    const auto filtering = m_smoothTextureFiltering ? QSGTexture::Linear : QSGTexture::Nearest;
    // Software's QSGSoftwareRenderableNode::update null-derefs if an image node
    // is parented before setTexture; highlight materials need RHI shaders.
    const auto api = window()->rendererInterface() ? window()->rendererInterface()->graphicsApi()
                                                   : QSGRendererInterface::Unknown;
    // Software has no RHI highlight material, so it still bakes. CIP also baked on
    // desktop OpenGL (solid full-alpha recolor via TEffectWrapper…); Qt 6 RHI
    // OpenGL/WebGL runs worldmap-highlight like Metal/D3D CipSoft, so keep the pulsed
    // mix there — including browser WASM where auto → OpenGL.
    const bool software = api == QSGRendererInterface::Software;
    const bool bakeHighlight = software;
    // A parent's children are reused in order from last frame's.
    struct Children
    {
        QSGNode* parent = nullptr;
        QSGNode* next = nullptr;
    };
    // Highlight material type — distinguish GeometryNode image vs highlight without RTTI.
    const auto isHighlightNode = [](QSGNode* node) -> bool {
        if (!node || node->type() != QSGNode::GeometryNodeType)
            return false;
        static QSGMaterialType* highlightType = [] {
            static renderer::TSpriteDrawCommandHighlightMaterial sample;
            return sample.type();
        }();
        auto* geo = static_cast<QSGGeometryNode*>(node);
        return geo->material() && geo->material()->type() == highlightType;
    };
    // Takes the next pooled node if it is of the wanted kind, else puts a new
    // one in its place. `create` must leave the node renderable (textures set)
    // before return — parenting triggers an immediate software update.
    const auto reuse = [this, isHighlightNode]<typename Node>(Children& at, auto create) -> Node* {
        QSGNode* node = at.next;
        if (node)
            at.next = node->nextSibling();
        Node* reused = nullptr;
        if (node) {
            if constexpr (std::is_same_v<Node, QSGImageNode>) {
                if (node->type() == QSGNode::GeometryNodeType && !isHighlightNode(node))
                    reused = static_cast<QSGImageNode*>(node);
            } else if constexpr (std::is_same_v<Node, renderer::TChildrenScenegraphNode>) {
                if (node->type() == QSGNode::OpacityNodeType)
                    reused = static_cast<Node*>(node);
            } else if constexpr (std::is_same_v<Node, renderer::THighlightedSpriteScenegraphNode>) {
                if (isHighlightNode(node))
                    reused = static_cast<Node*>(node);
            }
        }
        if (reused)
            return reused;
        Node* created = create();
        if (node) {
            at.parent->insertChildNodeBefore(created, node);
            at.parent->removeChildNode(node);
            recycleNode(node);
        } else {
            at.parent->appendChildNode(created);
        }
        return created;
    };
    const auto trim = [this](Children& at) {
        while (QSGNode* unused = at.next) {
            at.next = unused->nextSibling();
            at.parent->removeChildNode(unused);
            recycleNode(unused);
        }
    };

    // The camera's children: the sprites, then the product's raycast lighting while it is on.
    QSGNode* sprites = camera->firstChild();
    if (!sprites) {
        sprites = new QSGNode;
        camera->appendChildNode(sprites);
    }

    // Opacity groups (CIP: TChildrenScenegraphNode / world draw commands
    // 0x1010ce2b0): sprites below full opacity share one node per percent,
    // placed where that opacity first appears; fully transparent ones are not drawn.
    Children root{ sprites, sprites->firstChild() };
    std::array<std::optional<Children>, 100> groups;
    const auto parentFor = [&](const float opacity) -> Children* {
        const int percent = static_cast<int>(std::lround(opacity * 100.f));
        if (percent <= 0)
            return nullptr;
        if (percent >= 100)
            return &root;
        std::optional<Children>& group = groups[percent];
        if (!group) {
            auto* node = reuse.operator()<renderer::TChildrenScenegraphNode>(root, [this] {
                if (m_opacityPool.empty())
                    return new renderer::TChildrenScenegraphNode;
                renderer::TChildrenScenegraphNode* pooled = m_opacityPool.back();
                m_opacityPool.pop_back();
                return pooled;
            });
            node->setAlphaF(percent / 100.0);
            group = Children{ node, node->firstChild() };
        }
        return &*group;
    };

    int drawn = 0;
    int highlighted = 0;
    bool waiting = false;
    for (const SpriteRecord& record : frame.sprites) {
        Children* parent = parentFor(record.opacity);
        if (!parent)
            continue;
        QSGTexture* texture = m_textures->cache().texture(record.sprite);
        if (!texture) {
            // A sheet that is still decompressing is not stored. Keep painting
            // until it arrives; a missing sprite is stored and is not waited on.
            if (!m_textures->cache().contains(record.sprite))
                waiting = true;
            continue;
        }
        ++drawn;
        const QSize size = texture->textureSize();
        // HD textures are 4×; draw at logical size. ShaderEffectSource.textureSize is empty
        // in HD so the FBO is the item's native size and each texel lands 1:1.
        const int hdDiv = (!record.sprite.isCreatureMark() && !record.sprite.isHighlightBaseplate() && hd)
            ? std::max(1, tibia::sprites::hdFactor())
            : 1;
        const qreal drawW = size.width() / qreal(hdDiv);
        const qreal drawH = size.height() / qreal(hdDiv);
        const QRectF source(QPointF(), size);
        const QRectF rect(record.anchor.x() - drawW, record.anchor.y() - drawH, drawW, drawH);

        if (record.highlight != SpriteRecord::NoHighlight && !bakeHighlight) {
            ++highlighted;
            const QColor base = renderer::TScenegraphNodeProvider::highlightColor(record.highlight);
            QColor color = base;
            color.setAlphaF(frame.highlightStrength);
            const bool smooth = m_smoothTextureFiltering;
            auto* node = reuse.operator()<renderer::THighlightedSpriteScenegraphNode>(
                *parent, [this, texture, rect, color, smooth] {
                    renderer::THighlightedSpriteScenegraphNode* created = nullptr;
                    if (m_highlightPool.empty())
                        created = new renderer::THighlightedSpriteScenegraphNode;
                    else {
                        created = m_highlightPool.back();
                        m_highlightPool.pop_back();
                    }
                    created->set(texture, rect, color, smooth);
                    return created;
                });
            node->set(texture, rect, color, smooth);
            m_highlights.push_back({ node, texture, rect, base, smooth });
            continue;
        }
        if (record.highlight != SpriteRecord::NoHighlight && bakeHighlight) {
            QColor baked = renderer::TScenegraphNodeProvider::highlightColor(record.highlight);
            baked.setAlpha(255);
            if (QSGTexture* tinted = m_textures->cache().highlighted(record.sprite, baked))
                texture = tinted;
        }
        auto* image = reuse.operator()<QSGImageNode>(*parent, [this, texture, filtering, source, rect] {
            QSGImageNode* created = nullptr;
            if (m_imagePool.empty()) {
                created = window()->createImageNode();
                created->setOwnsTexture(false);
            } else {
                created = m_imagePool.back();
                m_imagePool.pop_back();
            }
            created->setTexture(texture);
            created->setFiltering(filtering);
            created->setSourceRect(source);
            created->setRect(rect);
            return created;
        });
        // Skip redundant dirty flags when the pooled node already matches (stable
        // sprite lists keep sync in the ~0.2 ms regime from the rewrite plan).
        if (image->texture() != texture)
            image->setTexture(texture);
        if (image->filtering() != filtering)
            image->setFiltering(filtering);
        if (image->sourceRect() != source)
            image->setSourceRect(source);
        if (image->rect() != rect)
            image->setRect(rect);
    }
    int groupCount = 0;
    for (std::optional<Children>& group : groups) {
        if (group) {
            trim(*group);
            ++groupCount;
        }
    }
    trim(root);
    m_textures->cache().endFrame();
    // Sheets still loading: the highlight pulse would keep this incomplete tree.
    m_spritesPending = waiting;
    if (waiting)
        update();

    // Pack overlays (raycast, weather, …) — each factory owns its node; startup registration only.
    for (tibia::mods::WorldOverlayFactory* factory : tibia::mods::worldOverlayFactories()) {
        if (factory)
            factory->updateScene(window(), camera, sprites);
    }

    if (lcWorldMap().isDebugEnabled()) {
        static QElapsedTimer logged;
        if (!logged.isValid() || logged.elapsed() > 1000) {
            logged.start();
            qCDebug(lcWorldMap) << "sprites" << frame.sprites.size() << "drawn" << drawn << "highlighted" << highlighted
                                << "opacity groups" << groupCount
                                << "textures" << m_textures->cache().size()
                                << "creatures" << frame.creatures.size() << "camera" << frame.camera
                                << "build us" << m_provider->buildNs() / 1000 << "sync us" << cost.nsecsElapsed() / 1000;
        }
    }
    return camera;
}

}

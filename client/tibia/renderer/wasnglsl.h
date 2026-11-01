#pragma once

#include <rhi/qshader.h>

#include <QByteArray>
#include <QFile>
#include <QLatin1String>
#include <QString>

#if defined(Q_OS_WASM) || defined(__EMSCRIPTEN__)

namespace tibia::renderer {

/// Strip packed #version / precision lines and force one WebGL2 preamble so
/// QRhi cannot pair mismatched GLSL families across stages.
inline QByteArray normalizeGlslEs300(QByteArray source)
{
    source = source.trimmed();
    while (source.startsWith("#version") || source.startsWith("precision ")) {
        const int nl = source.indexOf('\n');
        if (nl < 0)
            break;
        source = source.mid(nl + 1).trimmed();
    }
    return QByteArrayLiteral("#version 300 es\nprecision highp float;\nprecision highp int;\n") + source;
}

/// Keep only baked GLSL ES 300 (drop SPIR-V / other GLSL). WebGL QRhi may
/// otherwise translate SPIR-V per stage into mismatched #version lines.
inline QShader isolateGlslEs300(const QShader& full)
{
    QShaderKey keep;
    bool found = false;
    // Prefer the ES variant when both desktop 300 and 300 es are present.
    for (const QShaderKey& key : full.availableShaders()) {
        if (key.source() != QShader::GlslShader || key.sourceVersion().version() != 300)
            continue;
        if (key.sourceVersion().flags() & QShaderVersion::GlslEs) {
            keep = key;
            found = true;
            break;
        }
        if (!found) {
            keep = key;
            found = true;
        }
    }
    if (!found) {
        for (const QShaderKey& key : full.availableShaders()) {
            if (key.source() == QShader::GlslShader) {
                keep = key;
                found = true;
                break;
            }
        }
    }
    if (!found)
        return full;

    QShaderCode code = full.shader(keep);
    code.setShader(normalizeGlslEs300(code.shader()));

    const QShaderKey outKey{ QShader::GlslShader, QShaderVersion(300, QShaderVersion::GlslEs),
                             keep.sourceVariant() };
    QShader slim;
    slim.setStage(full.stage());
    slim.setDescription(full.description());
    slim.setShader(outKey, code);
    slim.setResourceBindingMap(outKey, full.nativeResourceBindingMap(keep));
    slim.setSeparateToCombinedImageSamplerMappingList(
        outKey, full.separateToCombinedImageSamplerMappingList(keep));
    return slim;
}

inline QShader loadStockShader(const char* name)
{
    QFile file(QStringLiteral(":/tibia/shaders/%1.qsb").arg(QLatin1String(name)));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return isolateGlslEs300(QShader::fromSerialized(file.readAll()));
}

/// Pack-owned shaders (`:/<module>/shaders/<name>.qsb`).
inline QShader loadPackShader(const char* qrcModule, const char* name)
{
    QFile file(QStringLiteral(":/%1/shaders/%2.qsb")
                   .arg(QLatin1String(qrcModule), QLatin1String(name)));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return isolateGlslEs300(QShader::fromSerialized(file.readAll()));
}

} // namespace tibia::renderer

#else

namespace tibia::renderer {

inline QShader loadStockShader(const char* name)
{
    QFile file(QStringLiteral(":/tibia/shaders/%1.qsb").arg(QLatin1String(name)));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QShader::fromSerialized(file.readAll());
}

/// Pack-owned shaders (`:/<module>/shaders/<name>.qsb`).
inline QShader loadPackShader(const char* qrcModule, const char* name)
{
    QFile file(QStringLiteral(":/%1/shaders/%2.qsb")
                   .arg(QLatin1String(qrcModule), QLatin1String(name)));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QShader::fromSerialized(file.readAll());
}

} // namespace tibia::renderer

#endif

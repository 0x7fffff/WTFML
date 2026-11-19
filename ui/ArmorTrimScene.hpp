#pragma once
#include "SkinModel.hpp"
#include <QCoreApplication>
#include <QHash>
#include <QPainter>

namespace ArmorTrimScene {
struct Result { SkinModel::Scene scene; QString error; };

// Images arrive at runtime; no copyrighted bitmap assets live in the source tree.
inline Result build(const QHash<QString,QImage> &images, const QString &armor, int part,
                    const QString &pattern, const QString &material, bool wholeSet) {
    Result result;
    if (armor == QStringLiteral("turtle") && wholeSet) { wholeSet=false; part=0; }
    const bool trimmed=!pattern.isEmpty();
    if (armor.isEmpty() || (trimmed && material.isEmpty()) || part < 0 || part > 3
        || (armor == QStringLiteral("turtle") && (wholeSet || part != 0))) {
        result.error = QCoreApplication::translate("ArmorTrimScene", "当前盔甲与部位组合不支持预览。");
        return result;
    }
    auto boxes = SkinModel::playerCuboids(false);
    // Empty support canvas: a mannequin would hide the inside of hollow armor.
    QImage mannequin(64,64,QImage::Format_ARGB32); mannequin.fill(Qt::transparent);
    result.scene.skin = mannequin;
    result.scene.yaw = 25; result.scene.pitch = -8;
    for (int slot = 0; slot < 4; ++slot) {
        if (!wholeSet && slot != part) continue;
        const QString layer = slot == 2 ? QStringLiteral("2") : QStringLiteral("1");
        const QString baseKey = QStringLiteral("models/armor/%1_layer_%2").arg(armor,layer);
        QImage base = images.value(baseKey);
        const QString patternKey = QStringLiteral("trims/models/armor/%1%2").arg(pattern,
                                              slot == 2 ? QStringLiteral("_leggings") : QString());
        QString paletteKey = QStringLiteral("trims/color_palettes/") + material;
        // Same-material trims use the darker vanilla palette to stay visible.
        if (armor == material && (material == QStringLiteral("iron") || material == QStringLiteral("gold")
            || material == QStringLiteral("diamond") || material == QStringLiteral("netherite")))
            paletteKey += QStringLiteral("_darker");
        const QImage keys = images.value(QStringLiteral("trims/color_palettes/trim_palette"));
        const QImage palette = images.value(paletteKey);
        const QImage patternImage = images.value(patternKey);
        if (base.size() != QSize(64,32) || (trimmed && (keys.size() != QSize(8,1)
            || palette.size() != QSize(8,1) || patternImage.size() != QSize(64,32)))) {
            result.scene = {};
            result.error = QCoreApplication::translate("ArmorTrimScene", "当前资源缺少所选盔甲、图案或调色板。请换用包含此图案的客户端（涡流/镶铆需要 1.21；树脂质需要 1.21.4）。");
            return result;
        }
        if (armor == QStringLiteral("leather")) {
            base = base.convertToFormat(QImage::Format_ARGB32);
            for (int y=0; y<base.height(); ++y) {
                auto *line = reinterpret_cast<QRgb *>(base.scanLine(y));
                for (int x=0; x<base.width(); ++x) {
                    const auto c = line[x];
                    line[x] = qRgba(qRed(c)*160/255, qGreen(c)*101/255, qBlue(c)*64/255, qAlpha(c));
                }
            }
            const auto overlay = images.value(baseKey + QStringLiteral("_overlay"));
            if (overlay.isNull()) {
                result.scene = {};
                result.error = QCoreApplication::translate("ArmorTrimScene", "皮革覆盖纹理缺失。");
                return result;
            }
            QPainter composite(&base); composite.drawImage(0,0,overlay);
        }
        // Palette substitution without a dependency on core tools (ui/ never includes src/).
        if (trimmed) {
        QImage trim = patternImage.convertToFormat(QImage::Format_ARGB32);
        QHash<QRgb,QRgb> paletteMap;
        for(int x=0;x<8;++x) paletteMap.insert(keys.pixel(x,0)&0xffffff,palette.pixel(x,0));
        for(int y=0;y<trim.height();++y) {
            auto *line=reinterpret_cast<QRgb *>(trim.scanLine(y));
            for(int x=0;x<trim.width();++x) {
                const auto c=line[x]; const auto it=paletteMap.constFind(c&0xffffff);
                if(qAlpha(c) && it!=paletteMap.cend())
                    line[x]=qRgba(qRed(*it),qGreen(*it),qBlue(*it),qAlpha(c)*qAlpha(*it)/255);
            }
        }
        // Composite before drawing so the inner face carries the trim too; separate
        // outer shells would put the opaque base between the eye and the inside trim.
        { QPainter composite(&base); composite.drawImage(0,0,trim); }
        }
        const QList<int> indices = slot==0 ? QList<int>{0} : slot==1 ? QList<int>{1,2,3}
                                              : slot==2 ? QList<int>{1,4,5} : QList<int>{4,5};
        for (int index : indices) {
            auto box = boxes[index]; box.hasOverlay=false; box.doubleSided=true;
            box.inflation=slot==2 ? .5f : 1.0f;
            if(index==3) { box.base=QPoint(40,16); box.mirror=true; }
            if(index==5) { box.base=QPoint(0,16); box.mirror=true; }
            result.scene.additionalParts.append({box,base});
        }
    }
    return result;
}

struct Equipment { QString armor, pattern, material; };
inline Result buildEquipment(const QHash<QString,QImage> &images, const QList<Equipment> &equipment,
                             const QImage &skin, bool slim, const QImage &cape = QImage()) {
    Result result;
    result.scene.skin=skin.isNull()?SkinModel::defaultSteveSkin():skin;
    // 披风是选修项：调用方不给图就保持空（SkinModel::render 不画那一条）。
    result.scene.cape=cape;
    result.scene.slim=slim; result.scene.yaw=25; result.scene.pitch=-8;
    QStringList errors;
    for(int slot=0;slot<qMin(4,static_cast<int>(equipment.size()));++slot) {
        const auto &selection=equipment[slot];
        if(selection.armor.isEmpty()) continue;
        const auto part=build(images,selection.armor,slot,selection.pattern,selection.material,false);
        if(!part.error.isEmpty()) { errors.append(part.error); continue; }
        result.scene.additionalParts.append(part.scene.additionalParts);
    }
    errors.removeDuplicates(); result.error=errors.join(QLatin1Char('\n'));
    return result;
}
}

#include "floodfill.h"

#include <QStack>
#include <QHash>
#include <QPair>
#include <QVector>
#include <QtConcurrent>
#include <QFuture>
#include <QFutureSynchronizer>
#include <QElapsedTimer>

#include <cmath>
#include <array>

struct Span
{
    qint32 x1;
    qint32 x2;
    qint32 y;
    qint32 dy;
};

using TileId = QPoint;
using SeedPointList = QVector<QPoint>;
using TilePropagationInfo = QHash<TileId, SeedPointList>;
static constexpr QSize tileSize {64, 64};

struct TileData
{
    quint8 referencePixel;
    quint8 fillMaskPixel;
};

using SeedSpanList = QVector<Span>;
using TilePropagationInfoScanLine = QHash<TileId, SeedSpanList>;
static constexpr QSize tileSizeScanLine {64, 64};

inline uint qHash(const QPoint &key)
{
    return qHash((static_cast<quint64>(key.x()) << 32) | key.y());
}

quint8 getPixel(const QImage &image, const QPoint &point)
{
    return *(image.scanLine(point.y()) + point.x());
}

void setPixel(QImage &image, const QPoint &point, quint8 value)
{
    *(image.scanLine(point.y()) + point.x()) = value;
}

quint8 getDifference(const QImage &image, const QPoint &point, quint8 seedValue)
{
    return qAbs(getPixel(image, point) - seedValue);
}

QImage floodFill(const QImage &referenceImage, const QPoint &seedPoint, quint8 threshold)
{
    Q_ASSERT(referenceImage.format() == QImage::Format_Grayscale8);

    QElapsedTimer timer;
    timer.start();

    QImage fillMaskImage(referenceImage.size(), referenceImage.format());
    fillMaskImage.fill(0);

    if (!referenceImage.rect().contains(seedPoint)) {
        return fillMaskImage;
    }

    QStack<QPoint> nodes;
    const quint8 seedValue = getPixel(referenceImage, seedPoint);

    nodes.push(seedPoint);

    while(!nodes.isEmpty()) {
        const QPoint p = nodes.pop();

        if (getPixel(fillMaskImage, p) > 0) {
            continue;
        }

        const quint8 value = getPixel(referenceImage, p);
        const quint8 difference = qAbs(value - seedValue);

        if (difference >= threshold) {
            continue;
        }

        const quint8 selectionValue = 255 - (difference * 255 / threshold);

        setPixel(fillMaskImage, p, selectionValue);

        if (p.x() > 0) {
            nodes.push(QPoint(p.x() - 1, p.y()));
        }
        if (p.x() < referenceImage.width() - 1) {
            nodes.push(QPoint(p.x() + 1, p.y()));
        }
        if (p.y() > 0) {
            nodes.push(QPoint(p.x(), p.y() - 1));
        }
        if (p.y() < referenceImage.height() - 1) {
            nodes.push(QPoint(p.x(), p.y() + 1));
        }
    }

    qDebug() << "floodFill" << (timer.nsecsElapsed() / 1000000.0) << "ms";

    return fillMaskImage;
}

QImage floodFillScanLine(const QImage &referenceImage, const QPoint &seedPoint, quint8 threshold)
{
    Q_ASSERT(referenceImage.format() == QImage::Format_Grayscale8);

    QElapsedTimer timer;
    timer.start();

    QImage fillMaskImage(referenceImage.size(), referenceImage.format());
    fillMaskImage.fill(0);

    if (!referenceImage.rect().contains(seedPoint)) {
        return fillMaskImage;
    }

    QStack<Span> spans;
    const quint8 seedValue = getPixel(referenceImage, seedPoint);

    spans.push({seedPoint.x(), seedPoint.x(), seedPoint.y(), 1});

    while(!spans.isEmpty()) {
        Span span = spans.pop();

        if (span.y < 0 || span.y >= referenceImage.height()) {
            continue;
        }

        qint32 x1 = span.x1;
        qint32 x2 = span.x1;
        
        if (getPixel(fillMaskImage, {span.x1, span.y}) == 0 &&
            qAbs(getPixel(referenceImage, {span.x1, span.y}) - seedValue) < threshold) {
            while (true) {
                if (x1 - 1 < 0) {
                    break;
                }
                const QPoint p(x1 - 1, span.y);
                if (getPixel(fillMaskImage, p) > 0) {
                    break;
                }
                const quint8 value = getPixel(referenceImage, p);
                const quint8 difference = qAbs(value - seedValue);
                if (difference >= threshold) {
                    break;
                }
                const quint8 selectionValue = 255 - (difference * 255 / threshold);
                setPixel(fillMaskImage, p, selectionValue);
                --x1;
            }
        }

        while (x2 <= span.x2) {
            while (true) {
                if (x2 >= referenceImage.width()) {
                    break;
                }
                const QPoint p(x2, span.y);
                if (getPixel(fillMaskImage, p) > 0) {
                    break;
                }
                const quint8 value = getPixel(referenceImage, p);
                const quint8 difference = qAbs(value - seedValue);
                if (difference >= threshold) {
                    break;
                }
                const quint8 selectionValue = 255 - (difference * 255 / threshold);
                setPixel(fillMaskImage, p, selectionValue);
                ++x2;
            }
            if (x2 > x1) {
                spans.push({x1, x2 - 1, span.y - span.dy, -span.dy});
                spans.push({x1, x2 - 1, span.y + span.dy, span.dy});
            }
            ++x2;
            while (x2 < span.x2 &&
                   x2 < referenceImage.width() &&
                   getPixel(fillMaskImage, {x2, span.y}) > 0 &&
                   qAbs(getPixel(referenceImage, {x2, span.y}) - seedValue) >= threshold) {
                ++x2;
            }
            x1 = x2;
        }
    }

    qDebug() << "floodFillScanLine" << (timer.nsecsElapsed() / 1000000.0) << "ms";

    return fillMaskImage;
}

TilePropagationInfo floodFillTile(const QImage &referenceImage,
                                  QImage &fillMaskImage,
                                  const SeedPointList &seedPoints,
                                  quint8 originalSeedValue,
                                  const TileId &currentTileId,
                                  const QRect &globalRect,
                                  const QRect &tileRect,
                                  const QSize &tileGridSize,
                                  quint8 threshold)
{
    TilePropagationInfo tilePropagationInfo;

    tilePropagationInfo[{currentTileId.x() - 1, currentTileId.y()}].reserve(tileSize.width());
    tilePropagationInfo[{currentTileId.x() + 1, currentTileId.y()}].reserve(tileSize.width());
    tilePropagationInfo[{currentTileId.x(), currentTileId.y() - 1}].reserve(tileSize.height());
    tilePropagationInfo[{currentTileId.x(), currentTileId.y() + 1}].reserve(tileSize.height());

    TileData tileData[tileSize.width() * tileSize.height()];

    for (quint32 y = tileRect.top(); y <= tileRect.bottom(); ++y) {
        const quint8 *referencePixel = referenceImage.scanLine(y) + tileRect.left();
        TileData *tilePixel = &tileData[(y - tileRect.top()) * tileSize.width()];
        for (quint32 x = 0; x < tileSize.width(); ++x, ++referencePixel, ++tilePixel) {
            tilePixel->referencePixel = *referencePixel;
        }
    }
    for (quint32 y = tileRect.top(); y <= tileRect.bottom(); ++y) {
        const quint8 *fillMaskPixel = fillMaskImage.scanLine(y) + tileRect.left();
        TileData *tilePixel = &tileData[(y - tileRect.top()) * tileSize.width()];
        for (quint32 x = 0; x < tileSize.width(); ++x, ++fillMaskPixel, ++tilePixel) {
            tilePixel->fillMaskPixel = *fillMaskPixel;
        }
    }

    QStack<QPoint> nodes;
    for (const QPoint &seedPoint : seedPoints) {
        nodes.push(seedPoint);
    }

    while(!nodes.isEmpty()) {
        const QPoint p = nodes.pop();
        const QPoint tileP = p - tileRect.topLeft();
        TileData &tilePixel = tileData[tileP.y() * tileSize.width() + tileP.x()];

        if (tilePixel.fillMaskPixel > 0) {
            continue;
        }

        const quint8 value = tilePixel.referencePixel;
        const quint8 difference = qAbs(value - originalSeedValue);

        if (difference >= threshold) {
            continue;
        }

        const quint8 selectionValue = 255 - (difference * 255 / threshold);

        tilePixel.fillMaskPixel = selectionValue;

        if (p.y() > globalRect.top()) {
            if (p.y() > tileRect.top()) {
                nodes.push({p.x(), p.y() - 1});
            } else {
                tilePropagationInfo[{currentTileId.x(), currentTileId.y() - 1}].append({p.x(), p.y() - 1});
            }
        }
        if (p.y() < globalRect.bottom()) {
            if (p.y() < tileRect.bottom()) {
                nodes.push({p.x(), p.y() + 1});
            } else {
                tilePropagationInfo[{currentTileId.x(), currentTileId.y() + 1}].append({p.x(), p.y() + 1});
            }
        }
        if (p.x() > globalRect.left()) {
            if (p.x() > tileRect.left()) {
                nodes.push({p.x() - 1, p.y()});
            } else {
                tilePropagationInfo[{currentTileId.x() - 1, currentTileId.y()}].append({p.x() - 1, p.y()});
            }
        }
        if (p.x() < globalRect.right()) {
            if (p.x() < tileRect.right()) {
                nodes.push({p.x() + 1, p.y()});
            } else {
                tilePropagationInfo[{currentTileId.x() + 1, currentTileId.y()}].append({p.x() + 1, p.y()});
            }
        }
    }

    for (quint32 y = tileRect.top(); y <= tileRect.bottom(); ++y) {
        quint8 *fillMaskPixel = fillMaskImage.scanLine(y) + tileRect.left();
        const TileData *tilePixel = &tileData[(y - tileRect.top()) * tileSize.width()];
        for (quint32 x = 0; x < tileSize.width(); ++x, ++fillMaskPixel, ++tilePixel) {
            *fillMaskPixel = tilePixel->fillMaskPixel;
        }
    }

    return tilePropagationInfo;
}

QImage floodFillMT(const QImage &referenceImage, const QPoint &seedPoint, quint8 threshold)
{
    Q_ASSERT(referenceImage.format() == QImage::Format_Grayscale8);

    QElapsedTimer globalTimer;
    QElapsedTimer timer;
    globalTimer.start();

    QImage fillMaskImage(referenceImage.size(), referenceImage.format());
    fillMaskImage.fill(0);

    if (!referenceImage.rect().contains(seedPoint)) {
        return fillMaskImage;
    }

    const quint8 originalSeedValue = getPixel(referenceImage, seedPoint);
    const QRect globalRect = referenceImage.rect();
    const QSize tileGridSize(
        std::ceil(static_cast<qreal>(globalRect.width()) / tileSize.width()),
        std::ceil(static_cast<qreal>(globalRect.height()) / tileSize.height())
    );
    const TileId seedPointTileId(
        seedPoint.x() / tileSize.width(),
        seedPoint.y() / tileSize.height()
    );
    TilePropagationInfo tilePropagationInfo;

    tilePropagationInfo.insert(seedPointTileId, {seedPoint});

    qint64 processingTime = 0;
    qint64 hashManipulationTime = 0;

    while (!tilePropagationInfo.isEmpty()) {
        QFutureSynchronizer<TilePropagationInfo> futureSynchronizer;

        timer.start();

        QHashIterator<TileId, SeedPointList> tilePropagationInfoIt(tilePropagationInfo);
        while (tilePropagationInfoIt.hasNext()) {
            tilePropagationInfoIt.next();

            futureSynchronizer.addFuture(
                QtConcurrent::run(
                    [&referenceImage, &fillMaskImage, &originalSeedValue,
                     &globalRect, &tileGridSize, &threshold, tilePropagationInfoIt]
                    () -> TilePropagationInfo
                    {
                        return
                            floodFillTile(
                                referenceImage, fillMaskImage, tilePropagationInfoIt.value(),
                                originalSeedValue, tilePropagationInfoIt.key(), globalRect,
                                QRect(
                                    tilePropagationInfoIt.key().x() * tileSize.width(),
                                    tilePropagationInfoIt.key().y() * tileSize.height(),
                                    tileSize.width(), tileSize.height()
                                ).intersected(globalRect),
                                tileGridSize, threshold
                            );
                    }
                )
            );
        }
        futureSynchronizer.waitForFinished();

        processingTime += timer.nsecsElapsed();
        timer.start();

        tilePropagationInfo.clear();

        for (QFuture<TilePropagationInfo> future : futureSynchronizer.futures()) {
            QHashIterator<TileId, SeedPointList> futureTilePropagationInfoIt(future.result());
            while (futureTilePropagationInfoIt.hasNext()) {
                futureTilePropagationInfoIt.next();

                const TileId &tileId = futureTilePropagationInfoIt.key();
                if (tileId.x() < 0 || tileId.x() >= tileGridSize.width() ||
                    tileId.y() < 0 || tileId.y() >= tileGridSize.height()) {
                    continue;
                }

                const SeedPointList &seedPointList = futureTilePropagationInfoIt.value();
                if (seedPointList.isEmpty()) {
                    continue;
                }

                tilePropagationInfo[tileId].append(seedPointList);
            }
        }

        hashManipulationTime += timer.nsecsElapsed();
    }

    qDebug() << "processingTime" << (processingTime / 1000000.0) << "ms";
    qDebug() << "hash manipulation time" << (hashManipulationTime / 1000000.0) << "ms";
    qDebug() << "floodFillMT" << (globalTimer.nsecsElapsed() / 1000000.0) << "ms";

    return fillMaskImage;
}

TilePropagationInfoScanLine floodFillTileScanLine(const QImage &referenceImage,
                                                  QImage &fillMaskImage,
                                                  const SeedSpanList &seedSpans,
                                                  quint8 originalSeedValue,
                                                  const TileId &currentTileId,
                                                  const QRect &globalRect,
                                                  const QRect &tileRect,
                                                  const QSize & tileGridSize,
                                                  quint8 threshold)
{
    TilePropagationInfoScanLine tilePropagationInfo;
    TileData tileData[tileSizeScanLine.width() * tileSizeScanLine.height()];

    for (quint32 y = tileRect.top(); y <= tileRect.bottom(); ++y) {
        const quint8 *referencePixel = referenceImage.scanLine(y) + tileRect.left();
        TileData *tilePixel = &tileData[(y - tileRect.top()) * tileSizeScanLine.width()];
        for (quint32 x = 0; x < tileSizeScanLine.width(); ++x, ++referencePixel, ++tilePixel) {
            tilePixel->referencePixel = *referencePixel;
        }
    }
    for (quint32 y = tileRect.top(); y <= tileRect.bottom(); ++y) {
        const quint8 *fillMaskPixel = fillMaskImage.scanLine(y) + tileRect.left();
        TileData *tilePixel = &tileData[(y - tileRect.top()) * tileSizeScanLine.width()];
        for (quint32 x = 0; x < tileSizeScanLine.width(); ++x, ++fillMaskPixel, ++tilePixel) {
            tilePixel->fillMaskPixel = *fillMaskPixel;
        }
    }

    QStack<Span> spans;

    for (const Span &seedSpan : seedSpans) {
        spans.push(seedSpan);
    }

    while(!spans.isEmpty()) {
        Span span = spans.pop();

        if (span.y < globalRect.top() || span.y > globalRect.bottom()) {
            continue;
        }

        qint32 x1 = span.x1;
        qint32 x2 = span.x1;
        
        if (tileData[(span.y - tileRect.top()) * tileSizeScanLine.width() + (span.x1 - tileRect.left())].fillMaskPixel == 0 &&
            qAbs(tileData[(span.y - tileRect.top()) * tileSizeScanLine.width() + (span.x1 - tileRect.left())].referencePixel - originalSeedValue) < threshold) {
            while (true) {
                const QPoint p(x1 - 1, span.y);
                const QPoint tileP = p - tileRect.topLeft();
                if (p.x() < globalRect.left()) {
                    break;
                }
                if (p.x() < tileRect.left()) {
                    tilePropagationInfo[{currentTileId.x() - 1, currentTileId.y()}].append({p.x(), p.x(), p.y(), span.dy});
                    break;
                }
                if (tileData[tileP.y() * tileSizeScanLine.width() + tileP.x()].fillMaskPixel > 0) {
                    break;
                }
                const quint8 value = tileData[tileP.y() * tileSizeScanLine.width() + tileP.x()].referencePixel;
                const quint8 difference = qAbs(value - originalSeedValue);
                if (difference >= threshold) {
                    break;
                }
                const quint8 selectionValue = 255 - (difference * 255 / threshold);
                tileData[tileP.y() * tileSizeScanLine.width() + tileP.x()].fillMaskPixel = selectionValue;
                --x1;
            }
        }

        while (x2 <= span.x2) {
            while (true) {
                const QPoint p(x2, span.y);
                const QPoint tileP = p - tileRect.topLeft();
                if (p.x() > globalRect.right()) {
                    break;
                }
                if (p.x() > tileRect.right()) {
                    tilePropagationInfo[{currentTileId.x() + 1, currentTileId.y()}].append({p.x(), p.x(), p.y(), span.dy});
                    break;
                }
                if (tileData[tileP.y() * tileSizeScanLine.width() + tileP.x()].fillMaskPixel > 0) {
                    break;
                }
                const quint8 value = tileData[tileP.y() * tileSizeScanLine.width() + tileP.x()].referencePixel;
                const quint8 difference = qAbs(value - originalSeedValue);
                if (difference >= threshold) {
                    break;
                }
                const quint8 selectionValue = 255 - (difference * 255 / threshold);
                tileData[tileP.y() * tileSizeScanLine.width() + tileP.x()].fillMaskPixel = selectionValue;
                ++x2;
            }
            if (x2 > x1) {
                const qint32 spanY1 = span.y - span.dy;
                const qint32 spanY2 = span.y + span.dy;
                if (spanY1 < tileRect.top()) {
                    tilePropagationInfo[{currentTileId.x(), currentTileId.y() - 1}].append({x1, x2 - 1, spanY1, -span.dy});
                } else if (spanY1 > tileRect.bottom()) {
                    tilePropagationInfo[{currentTileId.x(), currentTileId.y() + 1}].append({x1, x2 - 1, spanY1, -span.dy});
                } else {
                    spans.push({x1, x2 - 1, spanY1, -span.dy});
                }
                if (spanY2 < tileRect.top()) {
                    tilePropagationInfo[{currentTileId.x(), currentTileId.y() - 1}].append({x1, x2 - 1, spanY2, span.dy});
                } else if (spanY2 > tileRect.bottom()) {
                    tilePropagationInfo[{currentTileId.x(), currentTileId.y() + 1}].append({x1, x2 - 1, spanY2, span.dy});
                } else {
                    spans.push({x1, x2 - 1, spanY2, span.dy});
                }
            }
            ++x2;
            while (x2 < span.x2 &&
                   x2 <= globalRect.right() &&
                   x2 <= tileRect.right() &&
                   tileData[(span.y - tileRect.top()) * tileSizeScanLine.width() + (x2 - tileRect.left())].fillMaskPixel > 0 &&
                   qAbs(tileData[(span.y - tileRect.top()) * tileSizeScanLine.width() + (x2 - tileRect.left())].referencePixel - originalSeedValue) >= threshold) {
                ++x2;
            }
            x1 = x2;
        }
    }

    for (quint32 y = tileRect.top(); y <= tileRect.bottom(); ++y) {
        quint8 *fillMaskPixel = fillMaskImage.scanLine(y) + tileRect.left();
        const TileData *tilePixel = &tileData[(y - tileRect.top()) * tileSizeScanLine.width()];
        for (quint32 x = 0; x < tileSizeScanLine.width(); ++x, ++fillMaskPixel, ++tilePixel) {
            *fillMaskPixel = tilePixel->fillMaskPixel;
        }
    }

    return tilePropagationInfo;
}

QImage floodFillScanLineMT(const QImage &referenceImage, const QPoint &seedPoint, quint8 threshold)
{
    Q_ASSERT(referenceImage.format() == QImage::Format_Grayscale8);

    QElapsedTimer globalTimer;
    QElapsedTimer timer;
    globalTimer.start();

    QImage fillMaskImage(referenceImage.size(), referenceImage.format());
    fillMaskImage.fill(0);

    if (!referenceImage.rect().contains(seedPoint)) {
        return fillMaskImage;
    }

    const quint8 originalSeedValue = getPixel(referenceImage, seedPoint);
    const QRect globalRect = referenceImage.rect();
    const QSize tileGridSize(
        std::ceil(static_cast<qreal>(globalRect.width()) / tileSizeScanLine.width()),
        std::ceil(static_cast<qreal>(globalRect.height()) / tileSizeScanLine.height())
    );
    const TileId seedPointTileId(
        seedPoint.x() / tileSizeScanLine.width(),
        seedPoint.y() / tileSizeScanLine.height()
    );
    TilePropagationInfoScanLine tilePropagationInfo;

    tilePropagationInfo.insert(seedPointTileId, {{seedPoint.x(), seedPoint.x(), seedPoint.y(), 1}});

    qint64 processingTime = 0;
    qint64 hashManipulationTime = 0;

    while (!tilePropagationInfo.isEmpty()) {
        QFutureSynchronizer<TilePropagationInfoScanLine> futureSynchronizer;

        timer.start();

        QHashIterator<TileId, SeedSpanList> tilePropagationInfoIt(tilePropagationInfo);
        while (tilePropagationInfoIt.hasNext()) {
            tilePropagationInfoIt.next();

            futureSynchronizer.addFuture(
                QtConcurrent::run(
                    [&referenceImage, &fillMaskImage, &originalSeedValue,
                     &globalRect, &tileGridSize, &threshold, tilePropagationInfoIt]
                    () -> TilePropagationInfoScanLine
                    {
                        return
                            floodFillTileScanLine(
                                referenceImage, fillMaskImage, tilePropagationInfoIt.value(),
                                originalSeedValue, tilePropagationInfoIt.key(), globalRect,
                                QRect(
                                    tilePropagationInfoIt.key().x() * tileSizeScanLine.width(),
                                    tilePropagationInfoIt.key().y() * tileSizeScanLine.height(),
                                    tileSizeScanLine.width(), tileSizeScanLine.height()
                                ).intersected(globalRect),
                                tileGridSize, threshold
                            );
                    }
                )
            );
        }
        futureSynchronizer.waitForFinished();

        processingTime += timer.nsecsElapsed();
        timer.start();

        tilePropagationInfo.clear();

        for (QFuture<TilePropagationInfoScanLine> future : futureSynchronizer.futures()) {
            QHashIterator<TileId, SeedSpanList> futureTilePropagationInfoIt(future.result());
            while (futureTilePropagationInfoIt.hasNext()) {
                futureTilePropagationInfoIt.next();

                const TileId &tileId = futureTilePropagationInfoIt.key();
                if (tileId.x() < 0 || tileId.x() >= tileGridSize.width() ||
                    tileId.y() < 0 || tileId.y() >= tileGridSize.height()) {
                    continue;
                }

                const SeedSpanList &seedSpanList = futureTilePropagationInfoIt.value();
                if (seedSpanList.isEmpty()) {
                    continue;
                }

                tilePropagationInfo[tileId].append(seedSpanList);
            }
        }

        hashManipulationTime += timer.nsecsElapsed();
    }

    qDebug() << "processingTime" << (processingTime / 1000000.0) << "ms";
    qDebug() << "hash manipulation time" << (hashManipulationTime / 1000000.0) << "ms";
    qDebug() << "floodFillScanLineMT" << (globalTimer.nsecsElapsed() / 1000000.0) << "ms";

    return fillMaskImage;
}

#ifndef FLOODFILL_H
#define FLOODFILL_H

#include <QImage>
#include <QPoint>

QImage floodFill(const QImage &referenceImage, const QPoint &seedPoint, quint8 threshold);
QImage floodFillScanLine(const QImage &referenceImage, const QPoint &seedPoint, quint8 threshold);
QImage floodFillMT(const QImage &referenceImage, const QPoint &seedPoint, quint8 threshold);
QImage floodFillScanLineMT(const QImage &referenceImage, const QPoint &seedPoint, quint8 threshold);

#endif

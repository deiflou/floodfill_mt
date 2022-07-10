#include "window.h"

#include <QPainter>
#include <QMouseEvent>
#include <QDebug>
#include <QElapsedTimer>

#include "floodfill.h"

// for TEST_IMAGE choose:
// * ":/test01.png" (small size image)
// * ":/test02.png" (medium/big size image)
#define TEST_IMAGE ":/test02.png"

// For FLOODFILL_ALGORITHM choose:
// * floodFill (naive floodfill)
// * floodFillScanLine (scanline floodfill)
// * floodFillMT (multithreaded naive floodfill)
// * floodFillScanLineMT (multithreaded scanline floodfill)
#define FLOODFILL_ALGORITHM floodFillScanLineMT

window::window()
{
    loadReferenceImage();

    resize(m_referenceImage.size());
}

window::~window()
{}

void window::paintEvent(QPaintEvent*)
{
    QPainter p(this);

    p.fillRect(rect(), QColor(255, 0, 0));

    p.drawImage(0, 0, m_referenceImage);

    QImage ff(m_floodFillImage.size(), QImage::Format_ARGB32);
    ff.fill(qRgb(192, 192, 192));
    ff.setAlphaChannel(m_floodFillImage);
    p.drawImage(0, 0, ff);
}

void window::mousePressEvent(QMouseEvent *e)
{
    if (!m_referenceImage.rect().contains(e->pos())) {
        return;
    }

    createFloodFillSelection(e->pos());

    update();
}

void window::loadReferenceImage()
{
    m_referenceImage = QImage(TEST_IMAGE).convertToFormat(QImage::Format_Grayscale8);
}

void window::createFloodFillSelection(const QPoint &p)
{
    m_floodFillImage = FLOODFILL_ALGORITHM(m_referenceImage, p, 128);
}

#ifndef WINDOW_H
#define WINDOW_H

#include <QWidget>

class window : public QWidget
{
    Q_OBJECT

public:
    window();
    ~window();

    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;

private:
    QImage m_referenceImage;
    QImage m_floodFillImage;

    int vizMode {0};

    void loadReferenceImage();
    void createFloodFillSelection(const QPoint &p);
};

#endif
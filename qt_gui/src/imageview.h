#pragma once

#include <QColor>
#include <QGraphicsView>
#include <QImage>

class QGraphicsPixmapItem;
class QGraphicsTextItem;

class ImageView : public QGraphicsView {
    Q_OBJECT

public:
    explicit ImageView(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    void clearImage();
    void fitToImage();
    void setZoom(double value);
    double zoom() const;
    void setBackgroundColor(const QColor& color);
    void resetBackground();
    QColor backgroundColor() const;
    void setOverlayWidget(QWidget* overlay);

signals:
    void zoomChanged(double value);
    void imageChanged(bool hasImage);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void updatePlaceholder();
    QBrush checkerboardBrush() const;
    void updateOverlayGeometry();

    QGraphicsScene* scene_ = nullptr;
    QGraphicsPixmapItem* pixmapItem_ = nullptr;
    QGraphicsTextItem* placeholder_ = nullptr;
    double zoom_ = 1.0;
    bool fitMode_ = true;
    QWidget* overlay_ = nullptr;
    QColor backgroundColor_ = QColor(224, 228, 235);
};

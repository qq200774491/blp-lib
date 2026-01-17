#include "imageview.h"

#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QPainter>
#include <QWheelEvent>

ImageView::ImageView(QWidget* parent)
    : QGraphicsView(parent),
      scene_(new QGraphicsScene(this)) {
    setScene(scene_);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setRenderHint(QPainter::SmoothPixmapTransform, true);
    setBackgroundBrush(checkerboardBrush());
    updatePlaceholder();
}

void ImageView::setImage(const QImage& image) {
    scene_->clear();
    pixmapItem_ = scene_->addPixmap(QPixmap::fromImage(image));
    placeholder_ = nullptr;
    scene_->setSceneRect(pixmapItem_->boundingRect());
    fitMode_ = false;
    setZoom(1.0);
    centerOn(pixmapItem_);
    emit imageChanged(true);
}

void ImageView::clearImage() {
    scene_->clear();
    pixmapItem_ = nullptr;
    zoom_ = 1.0;
    fitMode_ = true;
    updatePlaceholder();
    emit zoomChanged(zoom_);
    emit imageChanged(false);
}

void ImageView::fitToImage() {
    if (!pixmapItem_) {
        return;
    }
    fitInView(pixmapItem_, Qt::KeepAspectRatio);
    zoom_ = transform().m11();
    fitMode_ = true;
    emit zoomChanged(zoom_);
}

void ImageView::setZoom(double value) {
    if (!pixmapItem_) {
        return;
    }
    zoom_ = qBound(0.05, value, 20.0);
    resetTransform();
    scale(zoom_, zoom_);
    fitMode_ = false;
    emit zoomChanged(zoom_);
}

double ImageView::zoom() const {
    return zoom_;
}

void ImageView::setBackgroundColor(const QColor& color) {
    if (!color.isValid()) {
        return;
    }
    backgroundColor_ = color;
    setBackgroundBrush(checkerboardBrush());
    viewport()->update();
}

void ImageView::resetBackground() {
    backgroundColor_ = QColor(224, 228, 235);
    setBackgroundBrush(checkerboardBrush());
    viewport()->update();
}

QColor ImageView::backgroundColor() const {
    return backgroundColor_;
}

void ImageView::setOverlayWidget(QWidget* overlay) {
    overlay_ = overlay;
    if (!overlay_) {
        return;
    }
    overlay_->setParent(viewport());
    overlay_->raise();
    updateOverlayGeometry();
}

void ImageView::wheelEvent(QWheelEvent* event) {
    if (!pixmapItem_) {
        event->ignore();
        return;
    }

    const int delta = event->angleDelta().y();
    if (delta == 0) {
        event->ignore();
        return;
    }

    const double factor = delta > 0 ? 1.15 : (1.0 / 1.15);
    setZoom(zoom_ * factor);
    event->accept();
}

void ImageView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    if (pixmapItem_ && fitMode_) {
        fitToImage();
    } else if (!pixmapItem_) {
        updatePlaceholder();
    }
    updateOverlayGeometry();
}

void ImageView::updatePlaceholder() {
    if (!scene_) {
        return;
    }

    scene_->clear();
    scene_->setSceneRect(QRectF(QPointF(0, 0), viewport()->size()));
    placeholder_ = scene_->addText("拖放图片到这里预览");
    placeholder_->setDefaultTextColor(QColor(90, 96, 110));

    const QRectF bounds = sceneRect();
    const QRectF textRect = placeholder_->boundingRect();
    placeholder_->setPos(bounds.center().x() - textRect.width() / 2.0,
                         bounds.center().y() - textRect.height() / 2.0);
}

QBrush ImageView::checkerboardBrush() const {
    const int tileSize = 16;
    QPixmap pixmap(tileSize * 2, tileSize * 2);
    const QColor lightColor = backgroundColor_.isValid()
                                  ? backgroundColor_
                                  : QColor(224, 228, 235);
    const QColor darkColor = lightColor.darker(115);
    pixmap.fill(lightColor);

    QPainter painter(&pixmap);
    painter.fillRect(0, 0, tileSize, tileSize, darkColor);
    painter.fillRect(tileSize, tileSize, tileSize, tileSize, darkColor);
    painter.end();

    return QBrush(pixmap);
}

void ImageView::updateOverlayGeometry() {
    if (!overlay_) {
        return;
    }

    const int margin = 8;
    overlay_->adjustSize();
    const int height = overlay_->sizeHint().height();
    const int width = qMax(0, viewport()->width() - margin * 2);
    overlay_->setGeometry(margin, margin, width, height);
    overlay_->raise();
}

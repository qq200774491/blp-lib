#pragma once

#include <QListWidget>
#include <QMainWindow>
#include <QSet>

#include "blp_api.h"
#include "image_io.h"

class QAction;
class QLabel;
class QLineEdit;
class QListWidgetItem;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSlider;
class QButtonGroup;
class QCheckBox;
class QGroupBox;
class QToolButton;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;

class ImageView;
class FileListWidget : public QListWidget {
    Q_OBJECT

public:
    explicit FileListWidget(QWidget* parent = nullptr);

signals:
    void filesDropped(const QStringList& paths);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onAddFiles();
    void onAddFolder();
    void onScanInputDir();
    void onRemoveSelected();
    void onClearList();
    void onBrowseInputDir();
    void onBrowseOutputDir();
    void onConvertAll();
    void onSelectionChanged(QListWidgetItem* current, QListWidgetItem* previous);
    void onMipSelectionChanged(QListWidgetItem* current, QListWidgetItem* previous);
    void onFormatChanged();
    void onZoomSliderChanged(int value);
    void onFitClicked();
    void onResetZoomClicked();
    void onAssociateBlp();
    void onThumbnailToggled(bool enabled);
    void onPickPreviewBackground();
    void onResetPreviewBackground();
    void onAlignPow2();
    void onRestorePow2();
    void onSavePow2();

private:
    void setupUi();
    void applyStyle();
    void addFiles(const QStringList& paths);
    void addFolderFiles(const QString& folder, bool recursive);
    void updatePreview(const QString& path);
    void updateInfoBar(const QString& path);
    void updateBlpStatus();
    void updateAssociationAction();
    void updateThumbnailAction();
    void updatePow2Overlay();
    void logMessage(const QString& message);
    QString buildOutputPath(const QString& inputPath, const QString& format, bool overwrite) const;
    QString normalizedFormat() const;
    void setInfoText(const ImageMeta& meta, int mipIndex);
    void clearPreviewState();
    void setPreviewImage(const QImage& image, bool adjusted);

    FileListWidget* fileList_ = nullptr;
    ImageView* imageView_ = nullptr;
    QGroupBox* mipGroup_ = nullptr;
    QListWidget* mipList_ = nullptr;
    QLabel* infoTitleLabel_ = nullptr;

    QLineEdit* inputDirEdit_ = nullptr;
    QLineEdit* outputDirEdit_ = nullptr;
    QButtonGroup* formatGroup_ = nullptr;
    QLabel* qualityLabel_ = nullptr;
    QLabel* qualityValueLabel_ = nullptr;
    QSlider* qualitySlider_ = nullptr;
    QCheckBox* overwriteCheck_ = nullptr;
    QCheckBox* recursiveCheck_ = nullptr;
    QAction* associateAction_ = nullptr;
    QAction* thumbnailAction_ = nullptr;

    QLabel* infoLabel_ = nullptr;
    QLabel* zoomLabel_ = nullptr;
    QLabel* blpLabel_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QPlainTextEdit* logEdit_ = nullptr;
    QSlider* zoomSlider_ = nullptr;
    QWidget* previewOverlay_ = nullptr;
    QWidget* pow2Panel_ = nullptr;
    QToolButton* pow2AlignButton_ = nullptr;
    QToolButton* pow2RestoreButton_ = nullptr;
    QToolButton* pow2SaveButton_ = nullptr;
    QToolButton* backgroundButton_ = nullptr;

    QSet<QString> fileSet_;
    QByteArray currentBlpBytes_;
    ImageMeta currentMeta_;
    QImage previewOriginalImage_;
    QImage previewAdjustedImage_;
    QString currentPreviewPath_;
    bool previewAdjusted_ = false;
    bool currentIsBlp_ = false;
    int currentMipIndex_ = 0;
    BlpApi blpApi_;
};

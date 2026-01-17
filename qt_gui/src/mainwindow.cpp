#include "mainwindow.h"

#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCoreApplication>
#include <QColorDialog>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QEvent>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QMenu>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSlider>
#include <QSplitter>
#include <QSizePolicy>
#include <QStatusBar>
#include <QTemporaryFile>
#include <QToolButton>
#include <QToolBar>
#include <QVBoxLayout>
#include <QUrl>
#include <QtEndian>
#include <QVector>

#ifdef Q_OS_WIN
#include <ShlObj.h>
#include <Windows.h>
#endif

#include "blp_api.h"
#include "image_io.h"
#include "imageview.h"

namespace {

QStringList extractLocalPaths(const QMimeData* mimeData) {
    QStringList paths;
    if (!mimeData || !mimeData->hasUrls()) {
        return paths;
    }

    const QList<QUrl> urls = mimeData->urls();
    for (const QUrl& url : urls) {
        const QString localPath = url.toLocalFile();
        if (!localPath.isEmpty()) {
            paths << localPath;
        }
    }
    return paths;
}

bool isPowerOfTwo(int value) {
    return value > 0 && ((value & (value - 1)) == 0);
}

int nearestPowerOfTwo(int value) {
    if (value <= 0) {
        return 1;
    }
    if (isPowerOfTwo(value)) {
        return value;
    }

    int upper = 1;
    while (upper < value && upper < (1 << 30)) {
        upper <<= 1;
    }
    int lower = upper >> 1;
    if (value - lower < upper - value) {
        return lower;
    }
    if (value - lower > upper - value) {
        return upper;
    }
    return upper;
}

int autoMipCount(int width, int height) {
    int maxDim = qMax(width, height);
    int count = 1;
    while (maxDim > 1 && count < 16) {
        maxDim /= 2;
        ++count;
    }
    return count;
}

const char* kBlpProgId = "BLPViewer.File";
const char* kThumbnailClsid = "{27A35239-0B87-4085-8944-463B440D162F}";
const char* kThumbnailHandler = "{E357FCCD-A995-4576-B01F-234630154E96}";

#ifdef Q_OS_WIN
bool isBlpAssociated() {
    QSettings classes("HKEY_CURRENT_USER\\Software\\Classes", QSettings::NativeFormat);
    classes.beginGroup(".blp");
    const QString progId = classes.value(".").toString();
    classes.endGroup();
    return progId.compare(QLatin1String(kBlpProgId), Qt::CaseInsensitive) == 0;
}

bool registerBlpAssociation(const QString& appPath, QString* outError) {
    QSettings classes("HKEY_CURRENT_USER\\Software\\Classes", QSettings::NativeFormat);
    classes.beginGroup(".blp");
    classes.setValue(".", QLatin1String(kBlpProgId));
    classes.endGroup();
    classes.setValue(QString("%1/.").arg(QLatin1String(kBlpProgId)), "BLP Image");
    classes.setValue(QString("%1/DefaultIcon/.").arg(QLatin1String(kBlpProgId)),
                     QDir::toNativeSeparators(appPath) + ",0");
    classes.setValue(QString("%1/shell/open/command/.").arg(QLatin1String(kBlpProgId)),
                     QString("\"%1\" \"%2\"").arg(QDir::toNativeSeparators(appPath), "%1"));
    classes.sync();
    if (classes.status() != QSettings::NoError) {
        if (outError) {
            *outError = "写入注册表失败";
        }
        return false;
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

bool isThumbnailRegistered() {
    QSettings classes("HKEY_CURRENT_USER\\Software\\Classes", QSettings::NativeFormat);
    const QString key = QString(".blp/ShellEx/%1/.").arg(QLatin1String(kThumbnailHandler));
    const QString clsid = classes.value(key).toString();
    return clsid.compare(QLatin1String(kThumbnailClsid), Qt::CaseInsensitive) == 0;
}

bool callDllEntry(const QString& dllPath, const char* entry, QString* outError) {
    const std::wstring widePath = dllPath.toStdWString();
    HMODULE module = LoadLibraryW(widePath.c_str());
    if (!module) {
        if (outError) {
            *outError = "加载缩略图 DLL 失败";
        }
        return false;
    }

    auto* func = reinterpret_cast<HRESULT (STDAPICALLTYPE*)(void)>(
        GetProcAddress(module, entry));
    if (!func) {
        if (outError) {
            *outError = "找不到注册入口";
        }
        FreeLibrary(module);
        return false;
    }

    const HRESULT hr = func();
    FreeLibrary(module);
    if (FAILED(hr)) {
        if (outError) {
            *outError = "注册调用失败";
        }
        return false;
    }
    return true;
}
#else
bool isBlpAssociated() {
    return false;
}

bool registerBlpAssociation(const QString&, QString* outError) {
    if (outError) {
        *outError = "仅支持 Windows";
    }
    return false;
}

bool isThumbnailRegistered() {
    return false;
}

bool callDllEntry(const QString&, const char*, QString* outError) {
    if (outError) {
        *outError = "仅支持 Windows";
    }
    return false;
}
#endif

struct BlpMipEntry {
    int index = 0;
    quint32 offset = 0;
    quint32 size = 0;
};

QVector<BlpMipEntry> readBlpMipEntries(const QByteArray& bytes) {
    QVector<BlpMipEntry> entries;
    if (bytes.size() < 148) {
        return entries;
    }

    const char* data = bytes.constData();
    if (memcmp(data, "BLP1", 4) != 0 && memcmp(data, "BLP2", 4) != 0) {
        return entries;
    }

    const int offsetsOffset = 20;
    const int sizesOffset = offsetsOffset + 16 * 4;

    for (int i = 0; i < 16; ++i) {
        const quint32 offset = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar*>(data + offsetsOffset + i * 4));
        const quint32 size = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar*>(data + sizesOffset + i * 4));
        if (offset != 0 && size != 0) {
            entries.push_back({i, offset, size});
        }
    }

    return entries;
}

RgbaImage toRgbaImage(const QImage& image) {
    QImage converted = image.convertToFormat(QImage::Format_RGBA8888);
    RgbaImage rgba;
    rgba.width = converted.width();
    rgba.height = converted.height();
    const int byteCount = converted.bytesPerLine() * converted.height();
    rgba.pixels = QByteArray(reinterpret_cast<const char*>(converted.constBits()), byteCount);
    return rgba;
}

const char* kInfoNormalStyle =
    "font-size: 15px;"
    "font-weight: 600;"
    "color: #1e2127;"
    "padding: 8px 10px;"
    "background: #eef2f8;"
    "border-radius: 6px;";

const char* kInfoWarnStyle =
    "font-size: 15px;"
    "font-weight: 600;"
    "color: #c0392b;"
    "padding: 8px 10px;"
    "background: #fff0f0;"
    "border-radius: 6px;";

} // namespace

FileListWidget::FileListWidget(QWidget* parent)
    : QListWidget(parent) {
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setAcceptDrops(true);
    setDragEnabled(false);
    setDropIndicatorShown(true);
}

void FileListWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void FileListWidget::dragMoveEvent(QDragMoveEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void FileListWidget::dropEvent(QDropEvent* event) {
    if (!event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }

    const QStringList paths = extractLocalPaths(event->mimeData());

    if (!paths.isEmpty()) {
        emit filesDropped(paths);
        event->acceptProposedAction();
        return;
    }

    event->ignore();
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setupUi();
    applyStyle();
    qApp->installEventFilter(this);
    updateBlpStatus();
    updateAssociationAction();
    updateThumbnailAction();
#ifdef Q_OS_WIN
    if (!isBlpAssociated()) {
        QString error;
        const QString appPath = QCoreApplication::applicationFilePath();
        if (registerBlpAssociation(appPath, &error)) {
            logMessage("已自动关联 .blp 打开方式");
        } else {
            logMessage(QString("自动关联 .blp 失败：%1").arg(error));
        }
        updateAssociationAction();
    }
#endif
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    if (!event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }

    const QStringList paths = extractLocalPaths(event->mimeData());

    addFiles(paths);
    event->acceptProposedAction();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    const auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget || (widget != this && !isAncestorOf(widget))) {
        return QMainWindow::eventFilter(watched, event);
    }

    if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
        auto* dragEvent = static_cast<QDragEnterEvent*>(event);
        if (dragEvent->mimeData()->hasUrls()) {
            dragEvent->acceptProposedAction();
            return true;
        }
    }

    if (event->type() == QEvent::Drop) {
        auto* dropEvent = static_cast<QDropEvent*>(event);
        const QStringList paths = extractLocalPaths(dropEvent->mimeData());
        if (!paths.isEmpty()) {
            addFiles(paths);
            dropEvent->acceptProposedAction();
            return true;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::setupUi() {
    setWindowTitle("BLP 查看器");
    setMinimumSize(1200, 760);
    setAcceptDrops(true);

    QWidget* central = new QWidget(this);
    QVBoxLayout* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(10);

    QSplitter* splitter = new QSplitter(Qt::Horizontal, central);

    QWidget* leftPanel = new QWidget(splitter);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(12);
    leftPanel->setMinimumWidth(300);
    leftPanel->setMaximumWidth(440);

    QGroupBox* fileGroup = new QGroupBox("待处理文件", leftPanel);
    QVBoxLayout* fileLayout = new QVBoxLayout(fileGroup);
    fileLayout->setSpacing(8);
    QLabel* fileHint = new QLabel("拖拽图片到任意位置，或使用按钮添加。列表中的文件会参与批量转换。", fileGroup);
    fileHint->setWordWrap(true);
    fileHint->setStyleSheet("color: #5b6472;");
    QWidget* fileListFrame = new QWidget(fileGroup);
    fileListFrame->setObjectName("fileListFrame");
    QVBoxLayout* fileListFrameLayout = new QVBoxLayout(fileListFrame);
    fileListFrameLayout->setContentsMargins(0, 0, 0, 0);
    fileListFrameLayout->setSpacing(0);
    fileList_ = new FileListWidget(fileGroup);
    fileList_->setObjectName("fileList");
    fileList_->setMinimumHeight(240);
    fileList_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    fileListFrameLayout->addWidget(fileList_);

    QWidget* fileButtonsRow = new QWidget(fileGroup);
    QHBoxLayout* fileButtons = new QHBoxLayout(fileButtonsRow);
    fileButtons->setContentsMargins(0, 0, 0, 0);
    fileButtons->setSpacing(8);
    fileButtonsRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    fileButtonsRow->setMinimumHeight(36);
    QPushButton* addFilesButton = new QPushButton("添加文件", fileButtonsRow);
    QPushButton* addFolderButton = new QPushButton("添加文件夹", fileButtonsRow);
    QPushButton* removeButton = new QPushButton("移除", fileButtonsRow);
    QPushButton* clearButton = new QPushButton("清空", fileButtonsRow);
    fileButtons->addWidget(addFilesButton);
    fileButtons->addWidget(addFolderButton);
    fileButtons->addWidget(removeButton);
    fileButtons->addWidget(clearButton);
    fileLayout->addWidget(fileHint);
    fileLayout->addWidget(fileListFrame, 1);
    fileLayout->addWidget(fileButtonsRow);
    leftLayout->addWidget(fileGroup, 3);

    QGroupBox* pathGroup = new QGroupBox("批量路径", leftPanel);
    QVBoxLayout* pathLayout = new QVBoxLayout(pathGroup);
    pathLayout->setSpacing(6);

    QLabel* inputLabel = new QLabel("输入目录（可选）：", pathGroup);
    inputDirEdit_ = new QLineEdit(pathGroup);
    inputDirEdit_->setPlaceholderText("选择目录后可点击“扫描”加入列表");
    inputDirEdit_->setClearButtonEnabled(true);
    inputDirEdit_->setMinimumWidth(0);
    inputDirEdit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QPushButton* inputBrowse = new QPushButton("选择", pathGroup);
    QPushButton* scanButton = new QPushButton("扫描", pathGroup);
    inputBrowse->setFixedWidth(72);
    scanButton->setFixedWidth(72);

    QWidget* inputRow = new QWidget(pathGroup);
    QHBoxLayout* inputRowLayout = new QHBoxLayout(inputRow);
    inputRowLayout->setContentsMargins(0, 0, 0, 0);
    inputRowLayout->setSpacing(6);
    inputRowLayout->addWidget(inputDirEdit_, 1);
    inputRowLayout->addWidget(inputBrowse);
    inputRowLayout->addWidget(scanButton);

    QLabel* outputLabel = new QLabel("输出目录（必填）：", pathGroup);
    outputDirEdit_ = new QLineEdit(pathGroup);
    outputDirEdit_->setPlaceholderText("选择输出目录");
    outputDirEdit_->setClearButtonEnabled(true);
    outputDirEdit_->setMinimumWidth(0);
    outputDirEdit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QPushButton* outputBrowse = new QPushButton("选择", pathGroup);
    outputBrowse->setFixedWidth(72);

    QWidget* outputRow = new QWidget(pathGroup);
    QHBoxLayout* outputRowLayout = new QHBoxLayout(outputRow);
    outputRowLayout->setContentsMargins(0, 0, 0, 0);
    outputRowLayout->setSpacing(6);
    outputRowLayout->addWidget(outputDirEdit_, 1);
    outputRowLayout->addWidget(outputBrowse);

    recursiveCheck_ = new QCheckBox("包含子目录", pathGroup);

    pathLayout->addWidget(inputLabel);
    pathLayout->addWidget(inputRow);
    pathLayout->addWidget(outputLabel);
    pathLayout->addWidget(outputRow);
    pathLayout->addWidget(recursiveCheck_);
    leftLayout->addWidget(pathGroup);

    QGroupBox* convertGroup = new QGroupBox("转换设置", leftPanel);
    QGridLayout* convertLayout = new QGridLayout(convertGroup);
    convertLayout->setColumnStretch(1, 1);
    convertLayout->setColumnStretch(2, 0);
    convertLayout->setHorizontalSpacing(10);

    QLabel* formatLabel = new QLabel("输出格式：", convertGroup);
    formatGroup_ = new QButtonGroup(convertGroup);
    formatGroup_->setExclusive(true);
    QWidget* formatOptions = new QWidget(convertGroup);
    QGridLayout* formatLayout = new QGridLayout(formatOptions);
    formatLayout->setContentsMargins(0, 0, 0, 0);
    formatLayout->setHorizontalSpacing(12);
    formatLayout->setVerticalSpacing(6);

    auto addFormatOption = [&](const QString& label, const QString& value, int row, int col, bool checked) {
        auto* button = new QRadioButton(label, formatOptions);
        button->setProperty("format", value);
        button->setChecked(checked);
        formatGroup_->addButton(button);
        formatLayout->addWidget(button, row, col);
    };
    addFormatOption("BLP", "BLP", 0, 0, true);
    addFormatOption("PNG", "PNG", 0, 1, false);
    addFormatOption("JPG", "JPG", 0, 2, false);
    addFormatOption("BMP", "BMP", 1, 0, false);
    addFormatOption("TGA", "TGA", 1, 1, false);
    formatLayout->setColumnStretch(2, 1);

    qualityLabel_ = new QLabel("质量：", convertGroup);
    qualitySlider_ = new QSlider(Qt::Horizontal, convertGroup);
    qualitySlider_->setRange(0, 100);
    qualitySlider_->setValue(100);
    qualitySlider_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    qualityValueLabel_ = new QLabel("100", convertGroup);
    qualityValueLabel_->setMinimumWidth(32);

    overwriteCheck_ = new QCheckBox("覆盖已存在文件", convertGroup);
    overwriteCheck_->setChecked(false);

    convertLayout->addWidget(formatLabel, 0, 0);
    convertLayout->addWidget(formatOptions, 0, 1, 1, 2);
    convertLayout->addWidget(qualityLabel_, 1, 0);
    convertLayout->addWidget(qualitySlider_, 1, 1);
    convertLayout->addWidget(qualityValueLabel_, 1, 2);
    convertLayout->addWidget(overwriteCheck_, 2, 0, 1, 3);

    leftLayout->addWidget(convertGroup);

    QPushButton* convertButton = new QPushButton("开始转换", leftPanel);
    convertButton->setMinimumHeight(36);
    leftLayout->addWidget(convertButton);

    progressBar_ = new QProgressBar(leftPanel);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    leftLayout->addWidget(progressBar_);

    QGroupBox* logGroup = new QGroupBox("日志", leftPanel);
    QVBoxLayout* logLayout = new QVBoxLayout(logGroup);
    logEdit_ = new QPlainTextEdit(logGroup);
    logEdit_->setReadOnly(true);
    logEdit_->setMinimumHeight(80);
    logLayout->addWidget(logEdit_);
    logGroup->setMaximumHeight(160);
    leftLayout->addWidget(logGroup, 0);

    QWidget* rightPanel = new QWidget(splitter);
    QVBoxLayout* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(10);

    infoTitleLabel_ = new QLabel("未加载图像", rightPanel);
    infoTitleLabel_->setStyleSheet(kInfoNormalStyle);
    infoTitleLabel_->setWordWrap(true);
    rightLayout->addWidget(infoTitleLabel_);

    imageView_ = new ImageView(rightPanel);
    previewOverlay_ = new QWidget(imageView_->viewport());
    previewOverlay_->setObjectName("previewOverlay");
    QHBoxLayout* overlayLayout = new QHBoxLayout(previewOverlay_);
    overlayLayout->setContentsMargins(0, 0, 0, 0);
    overlayLayout->setSpacing(8);

    pow2Panel_ = new QWidget(previewOverlay_);
    pow2Panel_->setObjectName("pow2Panel");
    QHBoxLayout* pow2Layout = new QHBoxLayout(pow2Panel_);
    pow2Layout->setContentsMargins(8, 4, 8, 4);
    pow2Layout->setSpacing(6);
    QLabel* pow2Label = new QLabel("非 2 次幂", pow2Panel_);
    pow2AlignButton_ = new QToolButton(pow2Panel_);
    pow2AlignButton_->setText("对齐 2 次幂");
    pow2RestoreButton_ = new QToolButton(pow2Panel_);
    pow2RestoreButton_->setText("恢复");
    pow2SaveButton_ = new QToolButton(pow2Panel_);
    pow2SaveButton_->setText("保存");
    pow2Layout->addWidget(pow2Label);
    pow2Layout->addWidget(pow2AlignButton_);
    pow2Layout->addWidget(pow2RestoreButton_);
    pow2Layout->addWidget(pow2SaveButton_);
    pow2Panel_->setVisible(false);

    QWidget* backgroundPanel = new QWidget(previewOverlay_);
    backgroundPanel->setObjectName("bgPanel");
    QHBoxLayout* backgroundLayout = new QHBoxLayout(backgroundPanel);
    backgroundLayout->setContentsMargins(8, 4, 8, 4);
    backgroundLayout->setSpacing(6);
    backgroundButton_ = new QToolButton(backgroundPanel);
    backgroundButton_->setText("背景");
    backgroundButton_->setPopupMode(QToolButton::InstantPopup);
    QMenu* backgroundMenu = new QMenu(backgroundButton_);
    QAction* pickBackgroundAction = backgroundMenu->addAction("选择颜色");
    QAction* resetBackgroundAction = backgroundMenu->addAction("恢复默认");
    backgroundButton_->setMenu(backgroundMenu);
    backgroundLayout->addWidget(backgroundButton_);

    overlayLayout->addWidget(pow2Panel_);
    overlayLayout->addStretch(1);
    overlayLayout->addWidget(backgroundPanel);
    imageView_->setOverlayWidget(previewOverlay_);
    rightLayout->addWidget(imageView_, 1);

    mipGroup_ = new QGroupBox("BLP 层级", rightPanel);
    QVBoxLayout* mipLayout = new QVBoxLayout(mipGroup_);
    mipList_ = new QListWidget(mipGroup_);
    mipList_->setSelectionMode(QAbstractItemView::SingleSelection);
    mipList_->setMinimumHeight(80);
    mipList_->setMaximumHeight(130);
    mipLayout->addWidget(mipList_);
    rightLayout->addWidget(mipGroup_);

    QHBoxLayout* zoomLayout = new QHBoxLayout();
    QPushButton* fitButton = new QPushButton("适应窗口", rightPanel);
    QPushButton* resetZoomButton = new QPushButton("原始大小", rightPanel);
    zoomSlider_ = new QSlider(Qt::Horizontal, rightPanel);
    zoomSlider_->setRange(10, 400);
    zoomSlider_->setValue(100);
    zoomLayout->addWidget(fitButton);
    zoomLayout->addWidget(resetZoomButton);
    zoomLayout->addWidget(zoomSlider_, 1);
    rightLayout->addLayout(zoomLayout);

    splitter->addWidget(leftPanel);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({340, 860});

    rootLayout->addWidget(splitter, 1);

    setCentralWidget(central);

    QToolBar* toolBar = new QToolBar("工具", this);
    toolBar->setMovable(false);
    toolBar->setFloatable(false);
    addToolBar(Qt::TopToolBarArea, toolBar);
    associateAction_ = toolBar->addAction("关联 BLP 打开方式");
    thumbnailAction_ = toolBar->addAction("资源管理器缩略图");
    thumbnailAction_->setCheckable(true);

    infoLabel_ = new QLabel("未加载图像", this);
    zoomLabel_ = new QLabel("缩放：100%", this);
    blpLabel_ = new QLabel("BLP：未加载", this);

    statusBar()->addWidget(infoLabel_, 1);
    statusBar()->addPermanentWidget(zoomLabel_);
    statusBar()->addPermanentWidget(blpLabel_);

    connect(associateAction_, &QAction::triggered, this, &MainWindow::onAssociateBlp);
    connect(thumbnailAction_, &QAction::toggled, this, &MainWindow::onThumbnailToggled);
    connect(addFilesButton, &QPushButton::clicked, this, &MainWindow::onAddFiles);
    connect(addFolderButton, &QPushButton::clicked, this, &MainWindow::onAddFolder);
    connect(scanButton, &QPushButton::clicked, this, &MainWindow::onScanInputDir);
    connect(removeButton, &QPushButton::clicked, this, &MainWindow::onRemoveSelected);
    connect(clearButton, &QPushButton::clicked, this, &MainWindow::onClearList);
    connect(inputBrowse, &QPushButton::clicked, this, &MainWindow::onBrowseInputDir);
    connect(outputBrowse, &QPushButton::clicked, this, &MainWindow::onBrowseOutputDir);
    connect(convertButton, &QPushButton::clicked, this, &MainWindow::onConvertAll);
    connect(fileList_, &QListWidget::currentItemChanged, this, &MainWindow::onSelectionChanged);
    connect(fileList_, &FileListWidget::filesDropped, this, &MainWindow::addFiles);
    connect(mipList_, &QListWidget::currentItemChanged, this, &MainWindow::onMipSelectionChanged);
    connect(formatGroup_, QOverload<QAbstractButton*>::of(&QButtonGroup::buttonClicked), this,
            [this](QAbstractButton*) { onFormatChanged(); });
    connect(qualitySlider_, &QSlider::valueChanged, this, [this](int value) {
        qualityValueLabel_->setText(QString::number(value));
    });
    connect(zoomSlider_, &QSlider::valueChanged, this, &MainWindow::onZoomSliderChanged);
    connect(fitButton, &QPushButton::clicked, this, &MainWindow::onFitClicked);
    connect(resetZoomButton, &QPushButton::clicked, this, &MainWindow::onResetZoomClicked);
    connect(pow2AlignButton_, &QToolButton::clicked, this, &MainWindow::onAlignPow2);
    connect(pow2RestoreButton_, &QToolButton::clicked, this, &MainWindow::onRestorePow2);
    connect(pow2SaveButton_, &QToolButton::clicked, this, &MainWindow::onSavePow2);
    connect(pickBackgroundAction, &QAction::triggered, this, &MainWindow::onPickPreviewBackground);
    connect(resetBackgroundAction, &QAction::triggered, this, &MainWindow::onResetPreviewBackground);
    connect(imageView_, &ImageView::zoomChanged, this, [this](double zoom) {
        const int percent = qBound(10, static_cast<int>(zoom * 100.0 + 0.5), 400);
        zoomSlider_->blockSignals(true);
        zoomSlider_->setValue(percent);
        zoomSlider_->blockSignals(false);
        zoomLabel_->setText(QString("缩放：%1%").arg(percent));
    });
    connect(imageView_, &ImageView::imageChanged, this, [this](bool hasImage) {
        if (!hasImage) {
            zoomSlider_->blockSignals(true);
            zoomSlider_->setValue(100);
            zoomSlider_->blockSignals(false);
            zoomLabel_->setText("缩放：100%");
        }
    });

    onFormatChanged();
    clearPreviewState();
}

void MainWindow::applyStyle() {
    QPalette palette = qApp->palette();
    palette.setColor(QPalette::Window, QColor(245, 246, 248));
    palette.setColor(QPalette::Base, QColor(255, 255, 255));
    palette.setColor(QPalette::AlternateBase, QColor(236, 238, 242));
    palette.setColor(QPalette::Button, QColor(45, 108, 223));
    palette.setColor(QPalette::ButtonText, QColor(255, 255, 255));
    palette.setColor(QPalette::Text, QColor(30, 33, 39));
    palette.setColor(QPalette::WindowText, QColor(30, 33, 39));
    palette.setColor(QPalette::Highlight, QColor(45, 108, 223));
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    qApp->setPalette(palette);

    const QString style =
        "QMainWindow { background: #f5f6f8; }"
        "QLineEdit, QSpinBox, QPlainTextEdit {"
        "  border: 1px solid #d1d5dc;"
        "  border-radius: 6px;"
        "  padding: 4px 6px;"
        "  background: #ffffff;"
        "}"
        "QComboBox {"
        "  border: 1px solid #d1d5dc;"
        "  border-radius: 6px;"
        "  padding: 4px 6px 4px 8px;"
        "  background: #ffffff;"
        "  min-width: 140px;"
        "}"
        "QComboBox::item {"
        "  padding: 4px 8px;"
        "}"
        "QComboBox::drop-down {"
        "  border: 0px;"
        "  width: 20px;"
        "  subcontrol-origin: padding;"
        "  subcontrol-position: right center;"
        "}"
        "QPushButton {"
        "  background: #2d6cdf;"
        "  color: white;"
        "  border-radius: 6px;"
        "  padding: 6px 10px;"
        "}"
        "QPushButton:hover { background: #245ec2; }"
        "QPushButton:disabled { background: #9fb6e2; }"
        "QGroupBox {"
        "  border: 1px solid #d7dbe3;"
        "  border-radius: 8px;"
        "  margin-top: 8px;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin;"
        "  left: 10px;"
        "  padding: 0 4px;"
        "  color: #3b3f45;"
        "}"
        "QWidget#fileListFrame {"
        "  border: 1px solid #d1d5dc;"
        "  border-radius: 6px;"
        "  background: #ffffff;"
        "}"
        "QListWidget#fileList {"
        "  border: 0px;"
        "  background: transparent;"
        "}"
        "QListWidget {"
        "  border: 1px solid #d1d5dc;"
        "  border-radius: 6px;"
        "  background: #ffffff;"
        "}"
        "QRadioButton {"
        "  color: #1e2127;"
        "}"
        "QToolBar {"
        "  background: #f5f6f8;"
        "  border: 0px;"
        "}"
        "QWidget#previewOverlay { background: transparent; }"
        "QWidget#pow2Panel, QWidget#bgPanel {"
        "  background: rgba(255, 255, 255, 0.92);"
        "  border: 1px solid #d7dbe3;"
        "  border-radius: 8px;"
        "}"
        "QWidget#previewOverlay QToolButton {"
        "  background: transparent;"
        "  border: none;"
        "  padding: 4px 6px;"
        "  color: #1e2127;"
        "}"
        "QWidget#previewOverlay QToolButton:hover {"
        "  background: #eef2f8;"
        "  border-radius: 6px;"
        "}"
        "QWidget#previewOverlay QToolButton:disabled {"
        "  color: #9aa1ad;"
        "}"
        "QWidget#previewOverlay QLabel { color: #5b6472; }"
        "QStatusBar { background: #eef0f4; }"
        "QProgressBar {"
        "  border: 1px solid #d1d5dc;"
        "  border-radius: 5px;"
        "  text-align: center;"
        "}"
        "QProgressBar::chunk {"
        "  background-color: #2d6cdf;"
        "  border-radius: 4px;"
        "}";

    qApp->setStyleSheet(style);
}

void MainWindow::onAddFiles() {
    const QString filter = "图片 (*.png *.jpg *.jpeg *.bmp *.tga *.blp)";
    const QStringList files = QFileDialog::getOpenFileNames(this, "添加图片", QString(), filter);
    addFiles(files);
}

void MainWindow::onAddFolder() {
    const QString startDir = inputDirEdit_->text().trimmed();
    const QString folder = QFileDialog::getExistingDirectory(this, "选择文件夹", startDir);
    if (folder.isEmpty()) {
        return;
    }

    inputDirEdit_->setText(folder);
    addFolderFiles(folder, recursiveCheck_->isChecked());
}

void MainWindow::onScanInputDir() {
    const QString folder = inputDirEdit_->text().trimmed();
    if (folder.isEmpty()) {
        logMessage("请先设置输入目录");
        return;
    }

    QFileInfo info(folder);
    if (!info.exists() || !info.isDir()) {
        logMessage("输入目录不存在");
        return;
    }

    addFolderFiles(info.absoluteFilePath(), recursiveCheck_->isChecked());
}

void MainWindow::onRemoveSelected() {
    const QList<QListWidgetItem*> items = fileList_->selectedItems();
    for (QListWidgetItem* item : items) {
        const QString path = item->text();
        fileSet_.remove(path);
        delete fileList_->takeItem(fileList_->row(item));
    }
}

void MainWindow::onClearList() {
    fileSet_.clear();
    fileList_->clear();
    imageView_->clearImage();
    clearPreviewState();
}

void MainWindow::onBrowseInputDir() {
    const QString startDir = inputDirEdit_->text().trimmed();
    const QString folder = QFileDialog::getExistingDirectory(this, "选择输入目录", startDir);
    if (!folder.isEmpty()) {
        inputDirEdit_->setText(folder);
    }
}

void MainWindow::onBrowseOutputDir() {
    const QString startDir = outputDirEdit_->text().trimmed();
    const QString folder = QFileDialog::getExistingDirectory(this, "选择输出目录", startDir);
    if (!folder.isEmpty()) {
        outputDirEdit_->setText(folder);
    }
}

void MainWindow::onConvertAll() {
    if (fileList_->count() == 0) {
        const QString inputDir = inputDirEdit_->text().trimmed();
        if (!inputDir.isEmpty()) {
            addFolderFiles(inputDir, recursiveCheck_->isChecked());
        }
    }

    if (fileList_->count() == 0) {
        logMessage("没有可转换的文件");
        return;
    }

    const QString outputDir = outputDirEdit_->text().trimmed();
    if (outputDir.isEmpty()) {
        logMessage("请先设置输出目录");
        return;
    }

    const QString format = normalizedFormat();
    const int quality = qualitySlider_->value();
    const bool formatIsBlp = (format == "blp");
    const bool overwrite = overwriteCheck_->isChecked();

    progressBar_->setRange(0, fileList_->count());
    progressBar_->setValue(0);

    int successCount = 0;
    for (int i = 0; i < fileList_->count(); ++i) {
        QListWidgetItem* item = fileList_->item(i);
        const QString inputPath = item->text();

        RgbaImage image;
        ImageMeta meta;
        QString error;
        if (!loadImageFile(inputPath, &image, &meta, &error, &blpApi_)) {
            logMessage(QString("读取失败：%1（%2）").arg(inputPath, error));
            progressBar_->setValue(i + 1);
            qApp->processEvents();
            continue;
        }

        const QString outputPath = buildOutputPath(inputPath, format, overwrite);
        const int mipCount = formatIsBlp ? autoMipCount(image.width, image.height) : 1;
        if (!writeImageFile(outputPath, format, image, quality, mipCount, &error, &blpApi_)) {
            logMessage(QString("写入失败：%1（%2）").arg(outputPath, error));
            progressBar_->setValue(i + 1);
            qApp->processEvents();
            continue;
        }

        ++successCount;
        progressBar_->setValue(i + 1);
        qApp->processEvents();
    }

    logMessage(QString("已转换 %1 / %2 个文件").arg(successCount).arg(fileList_->count()));
    updateBlpStatus();
}

void MainWindow::onSelectionChanged(QListWidgetItem* current, QListWidgetItem* previous) {
    Q_UNUSED(previous)
    if (!current) {
        imageView_->clearImage();
        clearPreviewState();
        return;
    }
    updatePreview(current->text());
}

void MainWindow::onMipSelectionChanged(QListWidgetItem* current, QListWidgetItem* previous) {
    Q_UNUSED(previous)
    if (!current || !currentIsBlp_ || currentBlpBytes_.isEmpty()) {
        return;
    }

    bool ok = false;
    const int mipIndex = current->data(Qt::UserRole).toInt(&ok);
    if (!ok || mipIndex < 0) {
        return;
    }
    if (mipIndex == currentMipIndex_) {
        return;
    }

    QString error;
    if (!blpApi_.ensureLoaded(&error)) {
        logMessage(QString("BLP 未加载：%1").arg(error));
        updateBlpStatus();
        return;
    }

    QTemporaryFile temp(QDir::tempPath() + "/blp_mip_XXXXXX.png");
    temp.setAutoRemove(true);
    if (!temp.open()) {
        logMessage("创建临时文件失败");
        return;
    }
    const QString tempPath = temp.fileName();
    temp.close();

    if (!blpApi_.decodeMipToPngFromBuffer(currentBlpBytes_, mipIndex, tempPath, &error)) {
        logMessage(QString("层级解码失败：%1").arg(error));
        return;
    }

    RgbaImage image;
    if (!loadImageFile(tempPath, &image, nullptr, &error, &blpApi_)) {
        logMessage(QString("层级预览失败：%1").arg(error));
        return;
    }

    const QImage previewImage = rgbaToQImage(image);
    previewOriginalImage_ = previewImage;
    previewAdjusted_ = false;
    previewAdjustedImage_ = QImage();
    currentMipIndex_ = mipIndex;
    setPreviewImage(previewImage, false);
}

void MainWindow::onFormatChanged() {
    const QString fmt = normalizedFormat();
    const bool isBlp = (fmt == "blp");
    const bool isJpg = (fmt == "jpg");

    qualitySlider_->setEnabled(isBlp || isJpg);
    qualityLabel_->setText(isBlp ? "BLP 质量：" : (isJpg ? "JPG 质量：" : "质量："));
    qualityValueLabel_->setEnabled(isBlp || isJpg);
}

void MainWindow::onZoomSliderChanged(int value) {
    imageView_->setZoom(value / 100.0);
}

void MainWindow::onFitClicked() {
    imageView_->fitToImage();
}

void MainWindow::onResetZoomClicked() {
    imageView_->setZoom(1.0);
}

void MainWindow::onPickPreviewBackground() {
    const QColor current = imageView_->backgroundColor();
    const QColor chosen = QColorDialog::getColor(current, this, "选择预览背景");
    if (!chosen.isValid()) {
        return;
    }
    imageView_->setBackgroundColor(chosen);
}

void MainWindow::onResetPreviewBackground() {
    imageView_->resetBackground();
}

void MainWindow::onAlignPow2() {
    if (previewOriginalImage_.isNull()) {
        return;
    }

    const int targetWidth = nearestPowerOfTwo(previewOriginalImage_.width());
    const int targetHeight = nearestPowerOfTwo(previewOriginalImage_.height());
    if (targetWidth == previewOriginalImage_.width() &&
        targetHeight == previewOriginalImage_.height()) {
        updatePow2Overlay();
        return;
    }

    const QImage adjusted = previewOriginalImage_.scaled(targetWidth,
                                                         targetHeight,
                                                         Qt::IgnoreAspectRatio,
                                                         Qt::SmoothTransformation);
    setPreviewImage(adjusted, true);
}

void MainWindow::onRestorePow2() {
    if (previewOriginalImage_.isNull()) {
        return;
    }
    setPreviewImage(previewOriginalImage_, false);
}

void MainWindow::onSavePow2() {
    if (!previewAdjusted_ || previewAdjustedImage_.isNull()) {
        return;
    }

    QStringList filters;
    filters << "BLP (*.blp)"
            << "PNG (*.png)"
            << "JPG (*.jpg *.jpeg)"
            << "BMP (*.bmp)"
            << "TGA (*.tga)";

    QString defaultExt = normalizeFormat(QFileInfo(currentPreviewPath_).suffix());
    if (defaultExt.isEmpty()) {
        defaultExt = "png";
    }

    QString defaultName = "aligned_pow2";
    QString defaultDir = QDir::homePath();
    if (!currentPreviewPath_.isEmpty()) {
        const QFileInfo info(currentPreviewPath_);
        defaultDir = info.absolutePath();
        defaultName = info.completeBaseName() + "_pow2";
        if (!info.suffix().isEmpty()) {
            defaultExt = normalizeFormat(info.suffix());
        }
    }

    const QString defaultPath = QDir(defaultDir).filePath(defaultName + "." + defaultExt);
    const QString filterString = filters.join(";;");
    QString outputPath = QFileDialog::getSaveFileName(this,
                                                      "保存对齐图像",
                                                      defaultPath,
                                                      filterString);
    if (outputPath.isEmpty()) {
        return;
    }

    QFileInfo outInfo(outputPath);
    QString format = normalizeFormat(outInfo.suffix());
    if (format.isEmpty()) {
        format = defaultExt;
        outputPath += "." + format;
        outInfo = QFileInfo(outputPath);
    }

    if (!supportedExtensions().contains(format)) {
        logMessage(QString("保存失败：不支持的格式 %1").arg(outInfo.suffix()));
        return;
    }

    RgbaImage image = toRgbaImage(previewAdjustedImage_);
    const int quality = qualitySlider_->value();
    const int mipCount = (format == "blp") ? autoMipCount(image.width, image.height) : 1;
    QString error;
    if (!writeImageFile(outputPath, format, image, quality, mipCount, &error, &blpApi_)) {
        logMessage(QString("保存失败：%1（%2）").arg(outputPath, error));
        return;
    }

    logMessage(QString("已保存对齐图像：%1").arg(outputPath));
}

void MainWindow::onAssociateBlp() {
    QString error;
    const QString appPath = QCoreApplication::applicationFilePath();
    if (registerBlpAssociation(appPath, &error)) {
        logMessage("已关联 .blp 打开方式");
    } else {
        logMessage(QString("关联 .blp 失败：%1").arg(error));
    }
    updateAssociationAction();
}

void MainWindow::onThumbnailToggled(bool enabled) {
    QString error;
    const QString dllPath =
        QDir(QCoreApplication::applicationDirPath()).filePath("blp_thumbnail.dll");
    const char* entry = enabled ? "DllRegisterServer" : "DllUnregisterServer";
    if (callDllEntry(dllPath, entry, &error)) {
        logMessage(enabled ? "已启用资源管理器缩略图" : "已关闭资源管理器缩略图");
    } else {
        logMessage(QString("%1：%2")
                       .arg(enabled ? "启用缩略图失败" : "关闭缩略图失败")
                       .arg(error));
    }
    updateThumbnailAction();
}

void MainWindow::addFiles(const QStringList& paths) {
    QStringList pending;

    for (const QString& path : paths) {
        QFileInfo info(path);
        if (!info.exists()) {
            continue;
        }
        if (info.isDir()) {
            addFolderFiles(info.absoluteFilePath(), recursiveCheck_->isChecked());
            continue;
        }
        if (!isSupportedFile(info.absoluteFilePath())) {
            continue;
        }
        const QString fullPath = info.absoluteFilePath();
        if (fileSet_.contains(fullPath)) {
            continue;
        }
        pending << fullPath;
        fileSet_.insert(fullPath);
    }

    for (const QString& path : pending) {
        fileList_->addItem(path);
    }

    if (fileList_->count() > 0 && !fileList_->currentItem()) {
        fileList_->setCurrentRow(0);
    }
}

void MainWindow::addFolderFiles(const QString& folder, bool recursive) {
    if (folder.trimmed().isEmpty()) {
        return;
    }

    QStringList filters;
    for (const QString& ext : supportedExtensions()) {
        filters << QString("*.%1").arg(ext);
    }

    QDirIterator it(folder,
                    filters,
                    QDir::Files,
                    recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);

    QStringList paths;
    while (it.hasNext()) {
        paths << it.next();
    }

    addFiles(paths);
}

void MainWindow::updatePreview(const QString& path) {
    clearPreviewState();
    currentPreviewPath_ = path;

    const QFileInfo info(path);
    const QString ext = normalizeFormat(info.suffix());

    if (ext == "blp") {
        currentIsBlp_ = true;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            imageView_->clearImage();
            infoTitleLabel_->setText("打开 BLP 失败");
            infoLabel_->setText("打开 BLP 失败");
            logMessage(QString("打开失败：%1").arg(path));
            return;
        }

        const QByteArray bytes = file.readAll();
        if (bytes.isEmpty()) {
            imageView_->clearImage();
            infoTitleLabel_->setText("BLP 文件为空");
            infoLabel_->setText("BLP 文件为空");
            logMessage(QString("文件为空：%1").arg(path));
            return;
        }

        currentBlpBytes_ = bytes;

        QString error;
        if (!blpApi_.ensureLoaded(&error)) {
            imageView_->clearImage();
            infoTitleLabel_->setText("BLP 库未加载");
            infoLabel_->setText("BLP 库未加载");
            logMessage(QString("BLP 未加载：%1").arg(error));
            updateBlpStatus();
            return;
        }

        BlpImage blpImage = {};
        const BlpResult result = blpApi_.loadFromBuffer(bytes, &blpImage);
        if (result != BLP_SUCCESS) {
            imageView_->clearImage();
            infoTitleLabel_->setText("BLP 解码失败");
            infoLabel_->setText("BLP 解码失败");
            logMessage(QString("BLP 解码失败：%1").arg(path));
            updateBlpStatus();
            return;
        }

        RgbaImage image;
        image.width = static_cast<int>(blpImage.width);
        image.height = static_cast<int>(blpImage.height);
        image.pixels = QByteArray(reinterpret_cast<const char*>(blpImage.data),
                                  static_cast<int>(blpImage.data_len));
        blpApi_.freeImage(&blpImage);

        const QImage previewImage = rgbaToQImage(image);

        currentMeta_.width = image.width;
        currentMeta_.height = image.height;
        currentMeta_.format = "blp";
        currentMeta_.fileSize = info.size();
        currentMipIndex_ = 0;
        previewOriginalImage_ = previewImage;
        previewAdjusted_ = false;
        previewAdjustedImage_ = QImage();
        setPreviewImage(previewImage, false);

        if (mipGroup_) {
            mipGroup_->setVisible(true);
        }
        const QVector<BlpMipEntry> entries = readBlpMipEntries(bytes);
        mipList_->blockSignals(true);
        mipList_->clear();

        if (entries.isEmpty()) {
            auto* item = new QListWidgetItem(
                QString("第1层（%1 x %2）").arg(currentMeta_.width).arg(currentMeta_.height));
            item->setData(Qt::UserRole, 0);
            mipList_->addItem(item);
        } else {
            for (int i = 0; i < 16; ++i) {
                const int mipWidth = qMax(1, currentMeta_.width >> i);
                const int mipHeight = qMax(1, currentMeta_.height >> i);
                const BlpMipEntry* entry = nullptr;
                for (const BlpMipEntry& candidate : entries) {
                    if (candidate.index == i) {
                        entry = &candidate;
                        break;
                    }
                }

                const bool hasData = (entry && entry->offset != 0 && entry->size != 0) || i == 0;
                const QString sizeText = (entry && entry->size > 0)
                                             ? formatFileSize(entry->size)
                                             : QString("未知大小");
                const QString text = hasData
                                         ? QString("第%1层（%2 x %3，%4）")
                                               .arg(i + 1)
                                               .arg(mipWidth)
                                               .arg(mipHeight)
                                               .arg(sizeText)
                                         : QString("第%1层（无数据）").arg(i + 1);

                auto* item = new QListWidgetItem(text);
                item->setData(Qt::UserRole, i);
                if (!hasData) {
                    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
                    item->setForeground(QColor(140, 146, 156));
                }
                mipList_->addItem(item);
            }
        }

        mipList_->setEnabled(true);
        mipList_->setCurrentRow(0);
        mipList_->blockSignals(false);

        updateBlpStatus();
        return;
    }

    RgbaImage image;
    ImageMeta meta;
    QString error;
    if (!loadImageFile(path, &image, &meta, &error, &blpApi_)) {
        imageView_->clearImage();
        infoTitleLabel_->setText("加载图像失败");
        infoLabel_->setText("加载图像失败");
        logMessage(QString("预览失败：%1（%2）").arg(path, error));
        updateBlpStatus();
        return;
    }

    const QImage previewImage = rgbaToQImage(image);
    currentMeta_ = meta;
    previewOriginalImage_ = previewImage;
    previewAdjusted_ = false;
    previewAdjustedImage_ = QImage();
    setPreviewImage(previewImage, false);

    if (mipGroup_) {
        mipGroup_->setVisible(false);
    }
    mipList_->blockSignals(true);
    mipList_->clear();
    mipList_->setEnabled(false);
    mipList_->blockSignals(false);

    updateBlpStatus();
}

void MainWindow::updateInfoBar(const QString& path) {
    QFileInfo info(path);
    if (!info.exists()) {
        infoLabel_->setText("未加载图像");
        return;
    }

    infoLabel_->setText(QString("%1 (%2)").arg(info.fileName(), formatFileSize(info.size())));
}

void MainWindow::updateBlpStatus() {
    if (blpApi_.isLoaded()) {
        const QString version = blpApi_.version();
        blpLabel_->setText(version.isEmpty() ? "BLP：已加载" : QString("BLP：%1").arg(version));
    } else {
        blpLabel_->setText("BLP：未加载");
    }
}

void MainWindow::updateAssociationAction() {
    if (!associateAction_) {
        return;
    }

    if (isBlpAssociated()) {
        associateAction_->setText("BLP 已关联");
        associateAction_->setEnabled(false);
        return;
    }

#ifdef Q_OS_WIN
    associateAction_->setText("关联 BLP 打开方式");
    associateAction_->setEnabled(true);
#else
    associateAction_->setText("关联 BLP（仅 Windows）");
    associateAction_->setEnabled(false);
#endif
}

void MainWindow::updateThumbnailAction() {
    if (!thumbnailAction_) {
        return;
    }

#ifdef Q_OS_WIN
    const QString dllPath =
        QDir(QCoreApplication::applicationDirPath()).filePath("blp_thumbnail.dll");
    if (!QFileInfo::exists(dllPath)) {
        thumbnailAction_->setText("资源管理器缩略图（缺少 DLL）");
        thumbnailAction_->setEnabled(false);
        thumbnailAction_->setChecked(false);
        return;
    }
    const bool registered = isThumbnailRegistered();
    thumbnailAction_->blockSignals(true);
    thumbnailAction_->setChecked(registered);
    thumbnailAction_->blockSignals(false);
    thumbnailAction_->setText(registered ? "资源管理器缩略图已启用" : "资源管理器缩略图");
    thumbnailAction_->setEnabled(true);
#else
    thumbnailAction_->setText("资源管理器缩略图（仅 Windows）");
    thumbnailAction_->setEnabled(false);
    thumbnailAction_->setChecked(false);
#endif
}

void MainWindow::updatePow2Overlay() {
    if (!pow2Panel_) {
        return;
    }

    if (previewOriginalImage_.isNull()) {
        pow2Panel_->setVisible(false);
        return;
    }

    const bool originalIsPot = isPowerOfTwo(previewOriginalImage_.width()) &&
                               isPowerOfTwo(previewOriginalImage_.height());
    const bool showPanel = !originalIsPot || previewAdjusted_;
    pow2Panel_->setVisible(showPanel);
    if (pow2AlignButton_) {
        pow2AlignButton_->setEnabled(!originalIsPot && !previewAdjusted_);
    }
    if (pow2RestoreButton_) {
        pow2RestoreButton_->setEnabled(previewAdjusted_);
    }
    if (pow2SaveButton_) {
        pow2SaveButton_->setEnabled(previewAdjusted_);
    }
}

void MainWindow::setPreviewImage(const QImage& image, bool adjusted) {
    imageView_->setImage(image);
    previewAdjusted_ = adjusted;
    if (adjusted) {
        previewAdjustedImage_ = image;
    } else {
        previewAdjustedImage_ = QImage();
    }

    ImageMeta displayMeta = currentMeta_;
    displayMeta.width = image.width();
    displayMeta.height = image.height();
    const int mipIndex = currentIsBlp_ ? currentMipIndex_ : -1;
    setInfoText(displayMeta, mipIndex);
    updatePow2Overlay();
}

void MainWindow::setInfoText(const ImageMeta& meta, int mipIndex) {
    QString info = QString("%1 x %2 像素 | %3 | %4")
                       .arg(meta.width)
                       .arg(meta.height)
                       .arg(formatFileSize(meta.fileSize))
                       .arg(meta.format.toUpper());

    if (mipIndex >= 0) {
        info += QString(" | 层级 %1").arg(mipIndex + 1);
    }

    const bool isPot = isPowerOfTwo(meta.width) && isPowerOfTwo(meta.height);
    if (!isPot) {
        info += " | 非 2 次幂";
    }

    infoLabel_->setText(info);
    infoTitleLabel_->setText(info);
    infoTitleLabel_->setStyleSheet(isPot ? kInfoNormalStyle : kInfoWarnStyle);
    infoLabel_->setStyleSheet(isPot ? "" : "color: #c0392b;");
}

void MainWindow::clearPreviewState() {
    currentBlpBytes_.clear();
    currentMeta_ = ImageMeta();
    currentIsBlp_ = false;
    currentMipIndex_ = 0;
    previewOriginalImage_ = QImage();
    previewAdjustedImage_ = QImage();
    currentPreviewPath_.clear();
    previewAdjusted_ = false;

    if (mipGroup_) {
        mipGroup_->setVisible(false);
    }
    if (mipList_) {
        mipList_->blockSignals(true);
        mipList_->clear();
        mipList_->setEnabled(false);
        mipList_->blockSignals(false);
    }

    if (infoLabel_) {
        infoLabel_->setText("未加载图像");
        infoLabel_->setStyleSheet("");
    }
    if (infoTitleLabel_) {
        infoTitleLabel_->setText("未加载图像");
        infoTitleLabel_->setStyleSheet(kInfoNormalStyle);
    }
    updatePow2Overlay();
}

void MainWindow::logMessage(const QString& message) {
    const QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    logEdit_->appendPlainText(QString("[%1] %2").arg(timestamp, message));
}

QString MainWindow::buildOutputPath(const QString& inputPath, const QString& format, bool overwrite) const {
    const QString outputDir = outputDirEdit_->text().trimmed();
    const QFileInfo inputInfo(inputPath);
    const QString baseName = inputInfo.completeBaseName();
    const QString ext = normalizeFormat(format);

    QString candidate = QDir(outputDir).filePath(QString("%1.%2").arg(baseName, ext));
    if (overwrite || !QFileInfo::exists(candidate)) {
        return candidate;
    }

    int index = 1;
    while (QFileInfo::exists(candidate)) {
        candidate = QDir(outputDir).filePath(QString("%1_%2.%3").arg(baseName).arg(index).arg(ext));
        ++index;
    }

    return candidate;
}

QString MainWindow::normalizedFormat() const {
    if (!formatGroup_) {
        return "blp";
    }
    const QAbstractButton* checked = formatGroup_->checkedButton();
    if (!checked) {
        return "blp";
    }
    return normalizeFormat(checked->property("format").toString());
}

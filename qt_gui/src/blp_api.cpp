#include "blp_api.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>

BlpApi::BlpApi() {
    lib_.setLoadHints(QLibrary::ResolveAllSymbolsHint);
#ifdef BLP_STATIC_LINK
    loadFromBuffer_ = &blp_load_from_buffer;
    freeImage_ = &blp_free_image;
    getVersion_ = &blp_get_version;
    encodeBytesToBlp_ = &blp_encode_bytes_to_blp;
    decodeMipToPngFromBuffer_ = &blp_decode_mip_to_png_from_buffer;
    loaded_ = true;
    loadedPath_ = QStringLiteral("static");
#endif
}

bool BlpApi::ensureLoaded(QString* outError) {
#ifdef BLP_STATIC_LINK
    if (!loaded_) {
        loadFromBuffer_ = &blp_load_from_buffer;
        freeImage_ = &blp_free_image;
        getVersion_ = &blp_get_version;
        encodeBytesToBlp_ = &blp_encode_bytes_to_blp;
        decodeMipToPngFromBuffer_ = &blp_decode_mip_to_png_from_buffer;
        loaded_ = true;
        loadedPath_ = QStringLiteral("static");
    }
    if (!loadFromBuffer_ || !freeImage_ || !getVersion_ || !encodeBytesToBlp_ ||
        !decodeMipToPngFromBuffer_) {
        if (outError) {
            *outError = "静态链接的 BLP 符号缺失";
        }
        return false;
    }
    return true;
#endif
    if (loaded_) {
        return true;
    }

    const QString envPath = qEnvironmentVariable("BLP_LIB_PATH");
    if (!envPath.isEmpty()) {
        if (tryLoadLibrary(envPath, outError)) {
            return true;
        }
    }

    const QStringList candidates = candidateLibraryPaths();
    for (const QString& path : candidates) {
        if (tryLoadLibrary(path, outError)) {
            return true;
        }
    }

    QStringList baseNames;
#ifdef Q_OS_WIN
    baseNames << "blp_lib" << "blp-windows";
#elif defined(Q_OS_MAC)
    baseNames << "blp_lib" << "blp-macos";
#else
    baseNames << "blp_lib" << "blp-linux";
#endif

    for (const QString& baseName : baseNames) {
        lib_.setFileName(baseName);
        if (!lib_.load()) {
            continue;
        }
        if (resolveSymbols(outError)) {
            loaded_ = true;
            loadedPath_ = lib_.fileName();
            return true;
        }
        lib_.unload();
    }

    if (outError) {
        *outError = "无法加载 BLP 库";
    }
    return false;
}

bool BlpApi::isLoaded() const {
    return loaded_;
}

QString BlpApi::libraryPath() const {
    return loadedPath_;
}

QString BlpApi::version() const {
    if (getVersion_) {
        return QString::fromUtf8(getVersion_());
    }
    return QString();
}

BlpResult BlpApi::loadFromBuffer(const QByteArray& data, BlpImage* outImage) {
    if (!loadFromBuffer_ || !outImage) {
        return BLP_INVALID_INPUT;
    }
    return loadFromBuffer_(reinterpret_cast<const uint8_t*>(data.constData()),
                           static_cast<uint32_t>(data.size()),
                           outImage);
}

void BlpApi::freeImage(BlpImage* image) {
    if (freeImage_) {
        freeImage_(image);
    }
}

bool BlpApi::encodePngBytesToBlp(const QByteArray& pngBytes,
                                 const QString& outputPath,
                                 int quality,
                                 int mipCount,
                                 QString* outError) {
    if (!encodeBytesToBlp_) {
        if (outError) {
            *outError = "BLP 编码入口缺失";
        }
        return false;
    }

    const int clampedQuality = qBound(0, quality, 100);
    const uint32_t clampedMipCount = static_cast<uint32_t>(qMax(1, mipCount));
    const QByteArray pathUtf8 = QDir::toNativeSeparators(outputPath).toUtf8();

    const BlpResult result = encodeBytesToBlp_(
        reinterpret_cast<const uint8_t*>(pngBytes.constData()),
        static_cast<uint32_t>(pngBytes.size()),
        pathUtf8.constData(),
        static_cast<uint8_t>(clampedQuality),
        clampedMipCount);

    if (result != BLP_SUCCESS) {
        if (outError) {
            *outError = "BLP 编码失败";
        }
        return false;
    }

    return true;
}

bool BlpApi::decodeMipToPngFromBuffer(const QByteArray& blpBytes,
                                      int mipIndex,
                                      const QString& outputPath,
                                      QString* outError) {
    if (!decodeMipToPngFromBuffer_) {
        if (outError) {
            *outError = "BLP 解码入口缺失";
        }
        return false;
    }

    const uint32_t clampedMip = static_cast<uint32_t>(qMax(0, mipIndex));
    const QByteArray pathUtf8 = QDir::toNativeSeparators(outputPath).toUtf8();

    const BlpResult result = decodeMipToPngFromBuffer_(
        reinterpret_cast<const uint8_t*>(blpBytes.constData()),
        static_cast<uint32_t>(blpBytes.size()),
        clampedMip,
        pathUtf8.constData());

    if (result != BLP_SUCCESS) {
        if (outError) {
            *outError = "BLP 层级解码失败";
        }
        return false;
    }

    return true;
}

bool BlpApi::resolveSymbols(QString* outError) {
    loadFromBuffer_ = reinterpret_cast<LoadFromBufferFn>(lib_.resolve("blp_load_from_buffer"));
    freeImage_ = reinterpret_cast<FreeImageFn>(lib_.resolve("blp_free_image"));
    getVersion_ = reinterpret_cast<GetVersionFn>(lib_.resolve("blp_get_version"));
    encodeBytesToBlp_ = reinterpret_cast<EncodeBytesToBlpFn>(lib_.resolve("blp_encode_bytes_to_blp"));
    decodeMipToPngFromBuffer_ = reinterpret_cast<DecodeMipToPngFromBufferFn>(
        lib_.resolve("blp_decode_mip_to_png_from_buffer"));

    if (!loadFromBuffer_ || !freeImage_ || !getVersion_ || !encodeBytesToBlp_ ||
        !decodeMipToPngFromBuffer_) {
        if (outError) {
            *outError = "解析 BLP 符号失败";
        }
        return false;
    }

    return true;
}

bool BlpApi::tryLoadLibrary(const QString& path, QString* outError) {
    if (path.trimmed().isEmpty()) {
        return false;
    }

    QFileInfo info(path);
    const QString resolved = info.exists() ? info.absoluteFilePath() : path;

    lib_.setFileName(resolved);
    if (!lib_.load()) {
        return false;
    }

    if (!resolveSymbols(outError)) {
        lib_.unload();
        return false;
    }

    loaded_ = true;
    loadedPath_ = lib_.fileName();
    return true;
}

QStringList BlpApi::candidateLibraryPaths() const {
    QStringList names;
#ifdef Q_OS_WIN
    names << "blp_lib.dll" << "blp-windows.dll";
#elif defined(Q_OS_MAC)
    names << "libblp_lib.dylib" << "libblp-macos.dylib";
#else
    names << "libblp_lib.so" << "libblp-linux.so";
#endif

    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList dirs;
    dirs << appDir;
    dirs << QDir::currentPath();
    dirs << QDir(appDir).filePath("../target/release");
    dirs << QDir(appDir).filePath("../target/debug");

    QStringList candidates;
    for (const QString& dir : dirs) {
        for (const QString& name : names) {
            candidates << QDir(dir).filePath(name);
        }
    }

    return candidates;
}

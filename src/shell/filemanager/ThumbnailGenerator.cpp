// Thumbnail Generator Implementation

#include "ThumbnailGenerator.hpp"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QImageReader>
#include <QPainter>
#include <QFont>
#include <QIcon>
#include <QMimeDatabase>
#include <QMimeType>
#include <QStandardPaths>
#include <QDateTime>
#include <QPainterPath>

namespace havel {

ThumbnailGenerator::ThumbnailGenerator()
    : m_cache(50)  // Cache 50 thumbnails
    , m_cacheMaxSize(50)
    , m_initialized(false)
{
    // Image formats
    m_imageFormats = {"png", "jpg", "jpeg", "gif", "bmp", "webp", "svg", "ico", "tiff"};
    
    // Video formats
    m_videoFormats = {"mp4", "webm", "avi", "mkv", "mov", "flv", "wmv"};
    
    // Document formats
    m_documentFormats = {"pdf", "odt", "ods", "odp", "doc", "docx", "xls", "xlsx"};
    
    // Audio formats
    m_audioFormats = {"mp3", "flac", "ogg", "wav", "aac", "wma", "m4a"};
    
    // Archive formats
    m_archiveFormats = {"zip", "tar", "gz", "rar", "7z", "bz2", "xz"};
    
    // Code formats
    m_codeFormats = {"cpp", "c", "h", "hpp", "py", "js", "ts", "java", "cs", "rb", "go", "rs", "sh"};
}

ThumbnailGenerator::~ThumbnailGenerator() {
    shutdown();
}

ThumbnailGenerator& ThumbnailGenerator::instance() {
    static ThumbnailGenerator instance;
    return instance;
}

void ThumbnailGenerator::initialize() {
    if (m_initialized) return;
    
    m_cache.setMaxCost(m_cacheMaxSize);
    m_initialized = true;
}

void ThumbnailGenerator::shutdown() {
    clearCache();
    m_initialized = false;
}

QImage ThumbnailGenerator::generateThumbnail(const QString& filePath, ThumbnailSize size) {
    QFileInfo fi(filePath);
    if (!fi.exists()) {
        return QImage();
    }
    
    // Check cache first
    QString cacheKey = filePath + "_" + QString::number(static_cast<int>(size));
    
    {
        QMutexLocker locker(&m_cacheMutex);
        ThumbnailEntry* entry = m_cache.object(cacheKey);
        if (entry && entry->isValid()) {
            // Verify file hasn't changed
            if (entry->lastModified == fi.lastModified() && 
                entry->fileSize == fi.size()) {
                return entry->image;
            }
        }
    }
    
    // Generate thumbnail
    QImage thumbnail;
    int pixelSize = static_cast<int>(size);
    
    QString mimeType = detectMimeType(filePath);
    QString category = getFileCategory(filePath);
    
    if (category == "image") {
        thumbnail = generateImageThumbnail(filePath, pixelSize);
    } else if (category == "video") {
        thumbnail = generateVideoThumbnail(filePath, pixelSize);
    } else if (category == "document") {
        thumbnail = generateDocumentThumbnail(filePath, pixelSize);
    } else if (category == "audio") {
        thumbnail = generateAudioThumbnail(filePath, pixelSize);
    } else if (category == "archive") {
        thumbnail = generateArchiveThumbnail(filePath, pixelSize);
    } else if (category == "code") {
        thumbnail = generateCodeThumbnail(filePath, pixelSize);
    } else {
        thumbnail = generateDefaultThumbnail(filePath, pixelSize);
    }
    
    // Cache the result
    if (!thumbnail.isNull()) {
        ThumbnailEntry* entry = new ThumbnailEntry();
        entry->image = thumbnail;
        entry->mimeType = mimeType;
        entry->fileSize = fi.size();
        entry->lastModified = fi.lastModified();
        
        QMutexLocker locker(&m_cacheMutex);
        m_cache.insert(cacheKey, entry);
    }
    
    return thumbnail;
}

void ThumbnailGenerator::generateThumbnailAsync(const QString& filePath, ThumbnailSize size,
                                                 std::function<void(const QImage&)> callback) {
    // Simple async implementation - would use QThread in production
    QImage thumbnail = generateThumbnail(filePath, size);
    callback(thumbnail);
}

QImage ThumbnailGenerator::generateImageThumbnail(const QString& filePath, int size) {
    QImageReader reader(filePath);
    
    // Set scaled size for efficiency
    reader.setScaledSize(QSize(size, size));
    
    if (reader.canRead()) {
        QImage image = reader.read();
        if (!image.isNull()) {
            return scaleToFit(image, size);
        }
    }
    
    // Fallback: try loading as QImage directly
    QImage image(filePath);
    if (!image.isNull()) {
        return scaleToFit(image, size);
    }
    
    return generateDefaultThumbnail(filePath, size);
}

QImage ThumbnailGenerator::generateVideoThumbnail(const QString& filePath, int size) {
    // For video, we'd use FFmpeg or GStreamer to extract a frame
    // For now, show a default video icon with file extension
    
    QImage thumbnail(size, size, QImage::Format_ARGB32);
    thumbnail.fill(Qt::transparent);
    
    QPainter painter(&thumbnail);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Draw background
    painter.setBrush(QColor(40, 40, 60));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(2, 2, size - 4, size - 4, 8, 8);
    
    // Draw play icon
    painter.setBrush(Qt::white);
    QPolygon playPolygon;
    playPolygon << QPoint(size/2 - 4, size/3);
    playPolygon << QPoint(size/2 - 4, 2*size/3);
    playPolygon << QPoint(2*size/3, size/2);
    painter.drawPolygon(playPolygon);
    
    // Draw extension
    QString ext = getExtension(filePath).toUpper();
    QFont font = painter.font();
    font.setPointSize(size / 5);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(0, size - 4, size, 20, Qt::AlignCenter, ext);
    
    return thumbnail;
}

QImage ThumbnailGenerator::generateDocumentThumbnail(const QString& filePath, int size) {
    QImage thumbnail(size, size, QImage::Format_ARGB32);
    thumbnail.fill(Qt::transparent);
    
    QPainter painter(&thumbnail);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Draw document shape
    painter.setBrush(QColor(240, 240, 255));
    painter.setPen(QColor(100, 100, 150));
    
    // Page shape with folded corner
    QPainterPath path;
    path.moveTo(4, 4);
    path.lineTo(size - 12, 4);
    path.lineTo(size - 4, 12);
    path.lineTo(size - 4, size - 4);
    path.lineTo(4, size - 4);
    path.closeSubpath();
    painter.drawPath(path);
    
    // Folded corner
    painter.setBrush(QColor(200, 200, 230));
    QPainterPath fold;
    fold.moveTo(size - 12, 4);
    fold.lineTo(size - 4, 4);
    fold.lineTo(size - 4, 12);
    fold.closeSubpath();
    painter.drawPath(fold);
    
    // Draw extension
    QString ext = getExtension(filePath).toUpper();
    QFont font = painter.font();
    font.setPointSize(size / 4);
    painter.setFont(font);
    painter.setPen(QColor(50, 50, 100));
    painter.drawText(0, size/2, size, size/2, Qt::AlignCenter, ext);
    
    return thumbnail;
}

QImage ThumbnailGenerator::generateAudioThumbnail(const QString& filePath, int size) {
    QImage thumbnail(size, size, QImage::Format_ARGB32);
    thumbnail.fill(Qt::transparent);
    
    QPainter painter(&thumbnail);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Draw CD/disc shape
    painter.setBrush(QColor(60, 60, 80));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(4, 4, size - 8, size - 8);
    
    // Draw inner circle
    painter.setBrush(QColor(100, 100, 130));
    painter.drawEllipse(size/2 - 6, size/2 - 6, 12, 12);
    
    // Draw center hole
    painter.setBrush(QColor(40, 40, 60));
    painter.drawEllipse(size/2 - 2, size/2 - 2, 4, 4);
    
    // Draw music note
    painter.setPen(QColor(200, 200, 255));
    QFont font = painter.font();
    font.setPointSize(size / 3);
    painter.setFont(font);
    painter.drawText(0, 0, size, size - 4, Qt::AlignCenter, "♪");
    
    return thumbnail;
}

QImage ThumbnailGenerator::generateArchiveThumbnail(const QString& filePath, int size) {
    QImage thumbnail(size, size, QImage::Format_ARGB32);
    thumbnail.fill(Qt::transparent);
    
    QPainter painter(&thumbnail);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Draw box shape
    painter.setBrush(QColor(200, 150, 100));
    painter.setPen(QColor(150, 100, 50));
    painter.drawRect(4, 4, size - 8, size - 8);
    
    // Draw zipper pattern
    painter.setPen(QColor(100, 80, 40));
    for (int i = 8; i < size - 8; i += 6) {
        painter.drawLine(4, i, size - 4, i);
    }
    
    // Draw extension
    QString ext = getExtension(filePath).toUpper();
    QFont font = painter.font();
    font.setPointSize(size / 5);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(0, size - 4, size, 20, Qt::AlignCenter, ext);
    
    return thumbnail;
}

QImage ThumbnailGenerator::generateCodeThumbnail(const QString& filePath, int size) {
    QImage thumbnail(size, size, QImage::Format_ARGB32);
    thumbnail.fill(QColor(30, 30, 40));
    
    QPainter painter(&thumbnail);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Draw code lines
    painter.setPen(QColor(100, 200, 100));
    QFont font("Monospace", size / 6);
    painter.setFont(font);
    
    int y = size / 4;
    for (int i = 0; i < 4; i++) {
        int lineWidth = size / 2 + (i % 3) * 10;
        painter.drawLine(8, y + i * 12, 8 + lineWidth, y + i * 12);
    }
    
    // Draw extension
    QString ext = getExtension(filePath);
    QFont extFont = painter.font();
    extFont.setPointSize(size / 6);
    extFont.setBold(true);
    painter.setFont(extFont);
    painter.setPen(QColor(150, 150, 200));
    painter.drawText(0, size - 4, size, 16, Qt::AlignCenter, "." + ext);
    
    return thumbnail;
}

QImage ThumbnailGenerator::generateDefaultThumbnail(const QString& filePath, int size) {
    QImage thumbnail(size, size, QImage::Format_ARGB32);
    thumbnail.fill(Qt::transparent);
    
    QPainter painter(&thumbnail);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Draw generic file icon
    painter.setBrush(QColor(180, 180, 200));
    painter.setPen(QColor(120, 120, 150));
    
    // File shape
    QPainterPath path;
    path.moveTo(6, 4);
    path.lineTo(size - 8, 4);
    path.lineTo(size - 4, 8);
    path.lineTo(size - 4, size - 4);
    path.lineTo(6, size - 4);
    path.closeSubpath();
    painter.drawPath(path);
    
    // Draw extension
    QString ext = getExtension(filePath).toUpper();
    if (!ext.isEmpty()) {
        QFont font = painter.font();
        font.setPointSize(size / 5);
        painter.setFont(font);
        painter.setPen(QColor(80, 80, 120));
        painter.drawText(0, size - 4, size, 20, Qt::AlignCenter, ext);
    }
    
    return thumbnail;
}

QImage ThumbnailGenerator::scaleToFit(const QImage& image, int maxSize) {
    if (image.isNull()) return image;
    
    return image.scaled(maxSize, maxSize, Qt::KeepAspectRatio, 
                        Qt::SmoothTransformation);
}

QString ThumbnailGenerator::detectMimeType(const QString& filePath) const {
    QMimeDatabase db;
    QMimeType mime = db.mimeTypeForFile(filePath);
    return mime.name();
}

QString ThumbnailGenerator::getFileCategory(const QString& filePath) const {
    QString ext = getExtension(filePath).toLower();
    
    if (m_imageFormats.contains(ext)) return "image";
    if (m_videoFormats.contains(ext)) return "video";
    if (m_documentFormats.contains(ext)) return "document";
    if (m_audioFormats.contains(ext)) return "audio";
    if (m_archiveFormats.contains(ext)) return "archive";
    if (m_codeFormats.contains(ext)) return "code";
    
    return "other";
}

QStringList ThumbnailGenerator::getSupportedImageFormats() const {
    return m_imageFormats;
}

QStringList ThumbnailGenerator::getSupportedVideoFormats() const {
    return m_videoFormats;
}

QStringList ThumbnailGenerator::getSupportedDocumentFormats() const {
    return m_documentFormats;
}

bool ThumbnailGenerator::isSupported(const QString& filePath) const {
    QString ext = getExtension(filePath).toLower();
    
    return m_imageFormats.contains(ext) ||
           m_videoFormats.contains(ext) ||
           m_documentFormats.contains(ext) ||
           m_audioFormats.contains(ext) ||
           m_archiveFormats.contains(ext) ||
           m_codeFormats.contains(ext);
}

void ThumbnailGenerator::clearCache() {
    QMutexLocker locker(&m_cacheMutex);
    m_cache.clear();
}

void ThumbnailGenerator::removeFromCache(const QString& filePath) {
    QMutexLocker locker(&m_cacheMutex);
    
    // Remove all size variants
    for (int size : {32, 64, 128, 256}) {
        QString cacheKey = filePath + "_" + QString::number(size);
        m_cache.remove(cacheKey);
    }
}

size_t ThumbnailGenerator::getCacheSize() const {
    return m_cache.count();
}

void ThumbnailGenerator::setCacheMaxSize(int maxSize) {
    m_cacheMaxSize = maxSize;
    m_cache.setMaxCost(maxSize);
}

QString ThumbnailGenerator::getExtension(const QString& filePath) const {
    QFileInfo fi(filePath);
    return fi.suffix();
}

} // namespace havel

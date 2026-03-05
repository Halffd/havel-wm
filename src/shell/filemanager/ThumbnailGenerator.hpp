// Thumbnail Generator - Generate previews for files

#pragma once

#include <QString>
#include <QImage>
#include <QSize>
#include <QCache>
#include <QMutex>
#include <QDateTime>
#include <functional>

namespace havel {

/**
 * Thumbnail size presets
 */
enum class ThumbnailSize {
    Small = 32,
    Medium = 64,
    Large = 128,
    ExtraLarge = 256
};

/**
 * Thumbnail cache entry
 */
struct ThumbnailEntry {
    QImage image;
    QString mimeType;
    qint64 fileSize;
    QDateTime lastModified;
    bool isValid() const { return !image.isNull(); }
};

/**
 * Thumbnail Generator
 * 
 * Generates and caches thumbnails for various file types:
 * - Images (PNG, JPG, GIF, BMP, WEBP, SVG)
 * - Videos (MP4, WEBM, AVI, MKV)
 * - Documents (PDF, ODT)
 * - Audio (MP3, FLAC, OGG)
 * - Archives (ZIP, TAR, RAR)
 * - Code files (with syntax highlighting preview)
 */
class ThumbnailGenerator {
public:
    static ThumbnailGenerator& instance();
    
    // Initialize
    void initialize();
    void shutdown();
    
    // Generate thumbnail
    QImage generateThumbnail(const QString& filePath, ThumbnailSize size = ThumbnailSize::Medium);
    
    // Async generation
    void generateThumbnailAsync(const QString& filePath, ThumbnailSize size,
                                std::function<void(const QImage&)> callback);
    
    // Cache management
    void clearCache();
    void removeFromCache(const QString& filePath);
    size_t getCacheSize() const;
    void setCacheMaxSize(int maxSize);
    
    // Supported formats
    QStringList getSupportedImageFormats() const;
    QStringList getSupportedVideoFormats() const;
    QStringList getSupportedDocumentFormats() const;
    bool isSupported(const QString& filePath) const;
    
    // File type detection
    QString detectMimeType(const QString& filePath) const;
    QString getFileCategory(const QString& filePath) const;

private:
    ThumbnailGenerator();
    ~ThumbnailGenerator();
    ThumbnailGenerator(const ThumbnailGenerator&) = delete;
    ThumbnailGenerator& operator=(const ThumbnailGenerator&) = delete;
    
    // Generation methods
    QImage generateImageThumbnail(const QString& filePath, int size);
    QImage generateVideoThumbnail(const QString& filePath, int size);
    QImage generateDocumentThumbnail(const QString& filePath, int size);
    QImage generateAudioThumbnail(const QString& filePath, int size);
    QImage generateArchiveThumbnail(const QString& filePath, int size);
    QImage generateCodeThumbnail(const QString& filePath, int size);
    QImage generateDefaultThumbnail(const QString& filePath, int size);
    
    // Helper methods
    QImage scaleToFit(const QImage& image, int maxSize);
    QImage addFileIconOverlay(const QImage& base, const QString& mimeType);
    QString getExtension(const QString& filePath) const;
    
    // Cache
    QCache<QString, ThumbnailEntry> m_cache;
    QMutex m_cacheMutex;
    int m_cacheMaxSize;
    
    // Format support
    QStringList m_imageFormats;
    QStringList m_videoFormats;
    QStringList m_documentFormats;
    QStringList m_audioFormats;
    QStringList m_archiveFormats;
    QStringList m_codeFormats;
    
    bool m_initialized;
};

} // namespace havel

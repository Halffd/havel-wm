// NASA Wallpaper Fetcher - APOD and NASA Image API

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <cstdint>

namespace havel {

/**
 * NASA image metadata
 */
struct NASAImage {
    std::string title;
    std::string description;
    std::string url;
    std::string hdUrl;
    std::string mediaType;  // "image" or "video"
    std::string date;
    std::string copyright;
    int width = 0;
    int height = 0;
    bool isValid() const { return !url.empty() && mediaType == "image"; }
};

/**
 * NASA API configuration
 */
struct NASAAPIConfig {
    std::string apiKey;  // DEMO_KEY for testing, get free key from api.nasa.gov
    std::string endpoint = "apod";  // "apod" or "images"
    int count = 10;  // Number of images to fetch
    bool startDate = false;
    std::string startDateValue;
    std::string endDateValue;
    bool downloadHD = true;
    std::string cacheDir = "~/.cache/havel-wm/nasa-wallpapers";
    int cacheExpiryDays = 7;
};

/**
 * Download progress callback
 */
using DownloadProgressCallback = std::function<void(int percent, size_t downloaded, size_t total)>;

/**
 * NASA Wallpaper Manager
 * 
 * Features:
 * - Fetch APOD (Astronomy Picture of the Day)
 * - Fetch NASA Image Library
 * - Automatic download and caching
 * - Progress reporting
 * - Metadata storage
 * - Slideshow integration
 */
class NASAWallpaperManager {
public:
    NASAWallpaperManager();
    ~NASAWallpaperManager();

    // Initialize
    bool initialize(const std::string& cacheDir = "");
    void shutdown();
    bool isInitialized() const { return m_initialized; }
    
    // Configuration
    void setConfig(const NASAAPIConfig& config);
    const NASAAPIConfig& getConfig() const { return m_config; }
    void setApiKey(const std::string& key) { m_config.apiKey = key; }
    
    // Fetch images
    bool fetchAPOD(int count = 1);
    bool fetchNASAImages(int count = 10);
    bool fetchRandomImages(int count = 10);
    
    // Get fetched images
    const std::vector<NASAImage>& getImages() const { return m_images; }
    const NASAImage* getCurrentImage() const;
    NASAImage* getCurrentImage();
    
    // Download images
    bool downloadCurrentImage();
    bool downloadAllImages();
    bool downloadImage(const NASAImage& image, const std::string& outputPath);
    
    // Set as wallpaper
    bool setAsWallpaper(bool useHD = true);
    
    // Cache management
    void clearCache();
    size_t getCacheSize() const;
    std::string getCacheDir() const { return m_config.cacheDir; }
    void cleanupOldCache();
    
    // Slideshow integration
    bool startSlideshow(int intervalSeconds = 300);  // Default 5 minutes
    void stopSlideshow();
    bool isSlideshowRunning() const { return m_slideshowRunning; }
    void nextImage();
    void previousImage();
    
    // Progress
    int getDownloadProgress() const { return m_downloadProgress; }
    bool isDownloading() const { return m_downloading; }
    const std::string& getDownloadStatus() const { return m_downloadStatus; }
    
    // Callbacks
    using ImageCallback = std::function<void(const NASAImage&)>;
    using ErrorCallback = std::function<void(const std::string&)>;
    
    void setOnImageFetched(ImageCallback cb) { m_onImageFetched = cb; }
    void setOnDownloadComplete(ImageCallback cb) { m_onDownloadComplete = cb; }
    void setOnError(ErrorCallback cb) { m_onError = cb; }
    void setOnProgress(DownloadProgressCallback cb) { m_onProgress = cb; }

private:
    // HTTP download
    bool downloadFile(const std::string& url, const std::string& outputPath);
    std::string fetchURL(const std::string& url);
    
    // JSON parsing
    bool parseAPODResponse(const std::string& json);
    bool parseNASAImagesResponse(const std::string& json);
    
    // File utilities
    bool createDirectory(const std::string& path);
    bool fileExists(const std::string& path);
    std::string generateFilename(const NASAImage& image);
    std::string getCachePath() const;
    
    // Slideshow
    void slideshowTimer();
    
    NASAAPIConfig m_config;
    std::vector<NASAImage> m_images;
    int m_currentImageIndex = 0;
    
    // Download state
    bool m_downloading = false;
    int m_downloadProgress = 0;
    std::string m_downloadStatus;
    
    // Slideshow state
    bool m_slideshowRunning = false;
    int m_slideshowInterval = 300;
    uint64_t m_lastSlideshowChange = 0;
    
    // Callbacks
    ImageCallback m_onImageFetched;
    ImageCallback m_onDownloadComplete;
    ErrorCallback m_onError;
    DownloadProgressCallback m_onProgress;
    
    bool m_initialized = false;
};

/**
 * Global NASA wallpaper manager access
 */
NASAWallpaperManager& getNASAWallpaperManager();

} // namespace havel

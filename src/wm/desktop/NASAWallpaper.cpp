// NASA Wallpaper Fetcher Implementation

#include "NASAWallpaper.hpp"
#include "DesktopManager.hpp"
#include <Logger.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <random>
#include <cstdlib>
#include <sys/stat.h>
#include <dirent.h>
#include <curl/curl.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

namespace havel {

// Global NASA wallpaper manager instance
static NASAWallpaperManager* g_nasaWallpaperManager = nullptr;

NASAWallpaperManager& getNASAWallpaperManager() {
    if (!g_nasaWallpaperManager) {
        g_nasaWallpaperManager = new NASAWallpaperManager();
    }
    return *g_nasaWallpaperManager;
}

// ============================================================================
// Size callback for curl
// ============================================================================

static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    std::ofstream* file = static_cast<std::ofstream*>(userp);
    if (!file) return 0;
    
    file->write(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

static size_t headerCallback(char* buffer, size_t size, size_t nitems, void* userp) {
    // Could parse headers for content-length
    (void)buffer;
    (void)nitems;
    (void)userp;
    return size * nitems;
}

// ============================================================================
// NASAWallpaperManager Implementation
// ============================================================================

NASAWallpaperManager::NASAWallpaperManager() {
    // Set default config
    m_config.apiKey = "DEMO_KEY";  // Free NASA API key
    m_config.endpoint = "apod";
    m_config.count = 10;
    m_config.downloadHD = true;
    m_config.cacheDir = std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.cache/havel-wm/nasa-wallpapers";
    m_config.cacheExpiryDays = 7;
}

NASAWallpaperManager::~NASAWallpaperManager() {
    shutdown();
}

bool NASAWallpaperManager::initialize(const std::string& cacheDir) {
    if (m_initialized) {
        return true;
    }
    
    // Initialize curl
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    // Set cache directory
    if (!cacheDir.empty()) {
        m_config.cacheDir = cacheDir;
    }
    
    // Create cache directory
    if (!createDirectory(m_config.cacheDir)) {
        LOG_ERROR("[NASAWallpaper] Failed to create cache directory: %s", m_config.cacheDir.c_str());
        return false;
    }
    
    LOG_INFO("[NASAWallpaper] Initialized (cache: %s)", m_config.cacheDir.c_str());
    m_initialized = true;
    return true;
}

void NASAWallpaperManager::shutdown() {
    stopSlideshow();
    m_images.clear();
    curl_global_cleanup();
    m_initialized = false;
    LOG_INFO("[NASAWallpaper] Shutdown complete");
}

void NASAWallpaperManager::setConfig(const NASAAPIConfig& config) {
    m_config = config;
    LOG_INFO("[NASAWallpaper] Configuration updated");
}

bool NASAWallpaperManager::fetchAPOD(int count) {
    if (!m_initialized) {
        LOG_ERROR("[NASAWallpaper] Not initialized");
        return false;
    }
    
    m_images.clear();
    m_currentImageIndex = 0;
    
    LOG_INFO("[NASAWallpaper] Fetching %d APOD images", count);
    
    // Build API URL
    std::ostringstream url;
    url << "https://api.nasa.gov/planetary/apod?api_key=" << m_config.apiKey;
    url << "&count=" << count;
    
    // Fetch from API
    std::string response = fetchURL(url.str());
    if (response.empty()) {
        if (m_onError) {
            m_onError("Failed to fetch APOD data");
        }
        return false;
    }
    
    // Parse response
    if (!parseAPODResponse(response)) {
        if (m_onError) {
            m_onError("Failed to parse APOD response");
        }
        return false;
    }
    
    LOG_INFO("[NASAWallpaper] Fetched %zu APOD images", m_images.size());
    
    if (m_onImageFetched && !m_images.empty()) {
        m_onImageFetched(m_images[0]);
    }
    
    return true;
}

bool NASAWallpaperManager::fetchNASAImages(int count) {
    if (!m_initialized) {
        LOG_ERROR("[NASAWallpaper] Not initialized");
        return false;
    }
    
    m_images.clear();
    m_currentImageIndex = 0;
    
    LOG_INFO("[NASAWallpaper] Fetching %d NASA images", count);
    
    // Build API URL for NASA Image Library
    std::ostringstream url;
    url << "https://images-api.nasa.gov/search?media_type=image";
    url << "&page_size=" << count;
    url << "&api_key=" << m_config.apiKey;
    
    // Fetch from API
    std::string response = fetchURL(url.str());
    if (response.empty()) {
        if (m_onError) {
            m_onError("Failed to fetch NASA images");
        }
        return false;
    }
    
    // Parse response
    if (!parseNASAImagesResponse(response)) {
        if (m_onError) {
            m_onError("Failed to parse NASA images response");
        }
        return false;
    }
    
    LOG_INFO("[NASAWallpaper] Fetched %zu NASA images", m_images.size());
    
    if (m_onImageFetched && !m_images.empty()) {
        m_onImageFetched(m_images[0]);
    }
    
    return true;
}

bool NASAWallpaperManager::fetchRandomImages(int count) {
    // Fetch APOD with random start date
    if (!m_initialized) return false;
    
    // Generate random date range
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dayDis(1, 28);
    std::uniform_int_distribution<> monthDis(1, 12);
    std::uniform_int_distribution<> yearDis(2015, 2024);
    
    char startDate[32];
    char endDate[32];
    
    int year = yearDis(gen);
    int month = monthDis(gen);
    int day = dayDis(gen);
    
    snprintf(startDate, sizeof(startDate), "%04d-%02d-%02d", year, month, day);
    snprintf(endDate, sizeof(endDate), "%04d-%02d-%02d", year, month, day + 10);
    
    // Build API URL with date range
    std::ostringstream url;
    url << "https://api.nasa.gov/planetary/apod?api_key=" << m_config.apiKey;
    url << "&start_date=" << startDate;
    url << "&end_date=" << endDate;
    
    std::string response = fetchURL(url.str());
    if (response.empty()) {
        return false;
    }
    
    return parseAPODResponse(response);
}

const NASAImage* NASAWallpaperManager::getCurrentImage() const {
    if (m_images.empty() || m_currentImageIndex >= static_cast<int>(m_images.size())) {
        return nullptr;
    }
    return &m_images[m_currentImageIndex];
}

NASAImage* NASAWallpaperManager::getCurrentImage() {
    return const_cast<NASAImage*>(const_cast<const NASAWallpaperManager*>(this)->getCurrentImage());
}

bool NASAWallpaperManager::downloadCurrentImage() {
    const NASAImage* image = getCurrentImage();
    if (!image || !image->isValid()) {
        LOG_ERROR("[NASAWallpaper] No valid image to download");
        return false;
    }
    
    return downloadImage(*image, "");
}

bool NASAWallpaperManager::downloadAllImages() {
    if (m_images.empty()) {
        LOG_ERROR("[NASAWallpaper] No images to download");
        return false;
    }
    
    int successCount = 0;
    for (const auto& image : m_images) {
        if (image.isValid() && downloadImage(image, "")) {
            successCount++;
        }
    }
    
    LOG_INFO("[NASAWallpaper] Downloaded %d/%zu images", successCount, m_images.size());
    return successCount > 0;
}

bool NASAWallpaperManager::downloadImage(const NASAImage& image, const std::string& outputPath) {
    if (!image.isValid()) {
        return false;
    }
    
    m_downloading = true;
    m_downloadProgress = 0;
    m_downloadStatus = "Starting download...";
    
    std::string url = m_config.downloadHD && !image.hdUrl.empty() ? image.hdUrl : image.url;
    
    std::string finalPath = outputPath;
    if (finalPath.empty()) {
        finalPath = getCachePath() + "/" + generateFilename(image);
    }
    
    // Check if already downloaded
    if (fileExists(finalPath)) {
        LOG_INFO("[NASAWallpaper] Image already cached: %s", finalPath.c_str());
        m_downloading = false;
        m_downloadProgress = 100;
        
        if (m_onDownloadComplete) {
            m_onDownloadComplete(image);
        }
        return true;
    }
    
    LOG_INFO("[NASAWallpaper] Downloading: %s -> %s", url.c_str(), finalPath.c_str());
    
    bool success = downloadFile(url, finalPath);
    
    m_downloading = false;
    m_downloadProgress = success ? 100 : 0;
    m_downloadStatus = success ? "Download complete" : "Download failed";
    
    if (success && m_onDownloadComplete) {
        m_onDownloadComplete(image);
    } else if (!success && m_onError) {
        m_onError("Download failed: " + url);
    }
    
    return success;
}

bool NASAWallpaperManager::setAsWallpaper(bool useHD) {
    const NASAImage* image = getCurrentImage();
    if (!image || !image->isValid()) {
        LOG_ERROR("[NASAWallpaper] No valid image to set as wallpaper");
        return false;
    }
    
    // Download if not already cached
    std::string cachePath = getCachePath() + "/" + generateFilename(*image);
    if (!fileExists(cachePath)) {
        if (!downloadImage(*image, cachePath)) {
            return false;
        }
    }
    
    // Set as wallpaper via DesktopManager
    auto& desktop = getDesktopManager();
    desktop.setWallpaper(cachePath);
    
    LOG_INFO("[NASAWallpaper] Set as wallpaper: %s", cachePath.c_str());
    return true;
}

void NASAWallpaperManager::clearCache() {
    std::string cachePath = getCachePath();
    
    // Remove all files in cache directory
    DIR* dir = opendir(cachePath.c_str());
    if (!dir) return;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        std::string filePath = cachePath + "/" + entry->d_name;
        remove(filePath.c_str());
    }
    
    closedir(dir);
    LOG_INFO("[NASAWallpaper] Cache cleared");
}

size_t NASAWallpaperManager::getCacheSize() const {
    size_t totalSize = 0;
    std::string cachePath = getCachePath();
    
    DIR* dir = opendir(cachePath.c_str());
    if (!dir) return 0;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type == DT_REG) {
            std::string filePath = cachePath + "/" + entry->d_name;
            struct stat st;
            if (stat(filePath.c_str(), &st) == 0) {
                totalSize += st.st_size;
            }
        }
    }
    
    closedir(dir);
    return totalSize;
}

void NASAWallpaperManager::cleanupOldCache() {
    // Remove files older than cacheExpiryDays
    std::string cachePath = getCachePath();
    time_t now = time(nullptr);
    time_t expiryTime = now - (m_config.cacheExpiryDays * 24 * 60 * 60);
    
    DIR* dir = opendir(cachePath.c_str());
    if (!dir) return;
    
    struct dirent* entry;
    int removedCount = 0;
    
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type == DT_REG) {
            std::string filePath = cachePath + "/" + entry->d_name;
            struct stat st;
            if (stat(filePath.c_str(), &st) == 0 && st.st_mtime < expiryTime) {
                remove(filePath.c_str());
                removedCount++;
            }
        }
    }
    
    closedir(dir);
    LOG_INFO("[NASAWallpaper] Cleaned up %d old cache files", removedCount);
}

bool NASAWallpaperManager::startSlideshow(int intervalSeconds) {
    if (m_slideshowRunning) {
        stopSlideshow();
    }

    m_slideshowRunning = true;
    m_slideshowInterval = intervalSeconds;
    m_lastSlideshowChange = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    LOG_INFO("[NASAWallpaper] Slideshow started (interval: %ds)", intervalSeconds);
    return true;
}

void NASAWallpaperManager::stopSlideshow() {
    m_slideshowRunning = false;
    LOG_INFO("[NASAWallpaper] Slideshow stopped");
}

void NASAWallpaperManager::nextImage() {
    if (m_images.empty()) return;
    
    m_currentImageIndex = (m_currentImageIndex + 1) % m_images.size();
    
    const NASAImage* image = getCurrentImage();
    if (image && image->isValid()) {
        setAsWallpaper();
    }
}

void NASAWallpaperManager::previousImage() {
    if (m_images.empty()) return;
    
    m_currentImageIndex = (m_currentImageIndex - 1 + m_images.size()) % m_images.size();
    
    const NASAImage* image = getCurrentImage();
    if (image && image->isValid()) {
        setAsWallpaper();
    }
}

void NASAWallpaperManager::slideshowTimer() {
    if (!m_slideshowRunning) return;

    uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    if (now - m_lastSlideshowChange >= static_cast<uint64_t>(m_slideshowInterval)) {
        nextImage();
        m_lastSlideshowChange = now;
    }
}

bool NASAWallpaperManager::downloadFile(const std::string& url, const std::string& outputPath) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR("[NASAWallpaper] Failed to initialize curl");
        return false;
    }
    
    std::ofstream file(outputPath, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("[NASAWallpaper] Failed to open file: %s", outputPath.c_str());
        curl_easy_cleanup(curl);
        return false;
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Havel-WM/1.0");
    
    // Set progress callback
    if (m_onProgress) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        // Would set XFERINFOFUNCTION for progress
    }
    
    CURLcode res = curl_easy_perform(curl);
    
    file.close();
    
    if (res != CURLE_OK) {
        LOG_ERROR("[NASAWallpaper] Download failed: %s", curl_easy_strerror(res));
        remove(outputPath.c_str());
        curl_easy_cleanup(curl);
        return false;
    }
    
    long responseCode;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
    
    curl_easy_cleanup(curl);
    
    if (responseCode != 200) {
        LOG_ERROR("[NASAWallpaper] HTTP error: %ld", responseCode);
        remove(outputPath.c_str());
        return false;
    }
    
    return true;
}

std::string NASAWallpaperManager::fetchURL(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR("[NASAWallpaper] Failed to initialize curl");
        return "";
    }
    
    std::string response;
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, [](void* contents, size_t size, size_t nmemb, void* userp) -> size_t {
        size_t realsize = size * nmemb;
        std::string* str = static_cast<std::string*>(userp);
        str->append(static_cast<char*>(contents), realsize);
        return realsize;
    });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Havel-WM/1.0");
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        LOG_ERROR("[NASAWallpaper] Fetch failed: %s", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        return "";
    }
    
    long responseCode;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
    
    curl_easy_cleanup(curl);
    
    if (responseCode != 200) {
        LOG_ERROR("[NASAWallpaper] HTTP error: %ld", responseCode);
        return "";
    }
    
    return response;
}

bool NASAWallpaperManager::parseAPODResponse(const std::string& json) {
    // Simple JSON parsing for APOD response
    // Extracts url, hdurl, title, date, explanation

    size_t urlPos = json.find("\"url\"");
    if (urlPos == std::string::npos) {
        return false;
    }

    NASAImage image;

    // Extract URL
    size_t start = json.find("\"", urlPos + 6);
    size_t end = json.find("\"", start + 1);
    if (start != std::string::npos && end != std::string::npos) {
        image.url = json.substr(start + 1, end - start - 1);
    }

    // Extract HD URL
    size_t hdUrlPos = json.find("\"hdurl\"");
    if (hdUrlPos != std::string::npos) {
        start = json.find("\"", hdUrlPos + 8);
        end = json.find("\"", start + 1);
        if (start != std::string::npos && end != std::string::npos) {
            image.hdUrl = json.substr(start + 1, end - start - 1);
        }
    }

    // Extract title
    size_t titlePos = json.find("\"title\"");
    if (titlePos != std::string::npos) {
        start = json.find("\"", titlePos + 8);
        end = json.find("\"", start + 1);
        if (start != std::string::npos && end != std::string::npos) {
            image.title = json.substr(start + 1, end - start - 1);
        }
    }

    // Extract date
    size_t datePos = json.find("\"date\"");
    if (datePos != std::string::npos) {
        start = json.find("\"", datePos + 7);
        end = json.find("\"", start + 1);
        if (start != std::string::npos && end != std::string::npos) {
            image.date = json.substr(start + 1, end - start - 1);
        }
    }

    // Extract explanation/description
    size_t descPos = json.find("\"explanation\"");
    if (descPos != std::string::npos) {
        start = json.find("\"", descPos + 14);
        end = json.find("\"", start + 1);
        if (start != std::string::npos && end != std::string::npos) {
            image.description = json.substr(start + 1, end - start - 1);
        }
    }

    // Extract media_type
    size_t mediaPos = json.find("\"media_type\"");
    if (mediaPos != std::string::npos) {
        start = json.find("\"", mediaPos + 13);
        end = json.find("\"", start + 1);
        if (start != std::string::npos && end != std::string::npos) {
            image.mediaType = json.substr(start + 1, end - start - 1);
        }
    } else {
        image.mediaType = "image";  // Default
    }

    if (image.isValid()) {
        m_images.push_back(image);
        LOG_INFO("[NASAWallpaper] Parsed APOD: %s (%s)", image.title.c_str(), image.date.c_str());
        return true;
    }

    return false;
}

bool NASAWallpaperManager::parseNASAImagesResponse(const std::string& json) {
    // Parse NASA Image Library response
    // Looks for collection.items array

    size_t collectionPos = json.find("\"collection\"");
    if (collectionPos == std::string::npos) {
        return false;
    }

    // Find items array
    size_t itemsPos = json.find("\"items\"", collectionPos);
    if (itemsPos == std::string::npos) {
        return false;
    }

    // Parse each item in the array
    size_t pos = itemsPos;
    int itemsFound = 0;
    
    while ((pos = json.find("\"href\"", pos)) != std::string::npos) {
        // Found an image href
        size_t start = json.find("\"", pos + 7);
        size_t end = json.find("\"", start + 1);
        
        if (start != std::string::npos && end != std::string::npos) {
            std::string href = json.substr(start + 1, end - start - 1);
            
            // Create NASAImage entry
            NASAImage image;
            image.url = href;
            image.mediaType = "image";
            
            // Try to find title near this href
            size_t titlePos = json.rfind("\"title\"", pos);
            if (titlePos != std::string::npos && titlePos > itemsPos) {
                start = json.find("\"", titlePos + 8);
                end = json.find("\"", start + 1);
                if (start != std::string::npos && end != std::string::npos) {
                    image.title = json.substr(start + 1, end - start - 1);
                }
            }
            
            if (image.title.empty()) {
                image.title = "NASA Image " + std::to_string(itemsFound + 1);
            }
            
            if (image.isValid()) {
                m_images.push_back(image);
                itemsFound++;
            }
        }
        
        pos = end + 1;
    }

    LOG_INFO("[NASAWallpaper] Parsed NASA images response: %d items found", itemsFound);
    return itemsFound > 0;
}

bool NASAWallpaperManager::createDirectory(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    
    // Create parent directories
    size_t pos = path.rfind('/');
    if (pos != std::string::npos && pos > 0) {
        if (!createDirectory(path.substr(0, pos))) {
            return false;
        }
    }
    
    return mkdir(path.c_str(), 0755) == 0;
}

bool NASAWallpaperManager::fileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

std::string NASAWallpaperManager::generateFilename(const NASAImage& image) {
    // Generate filename from date and title
    std::string filename = image.date;
    if (filename.empty()) {
        filename = "nasa_image";
    }
    
    // Replace invalid characters
    for (char& c : filename) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }
    
    filename += ".jpg";
    return filename;
}

std::string NASAWallpaperManager::getCachePath() const {
    return m_config.cacheDir;
}

} // namespace havel

// NASA Wallpaper Fetcher Implementation

#include "NASAWallpaper.hpp"
#include "DesktopManager.hpp"
#include <nlohmann/json.hpp>
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

using json = nlohmann::json;

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

bool NASAWallpaperManager::parseAPODResponse(const std::string& jsonStr) {
    // Parse APOD response using nlohmann/json
    json j;
    try {
        j = json::parse(jsonStr);
    } catch (const json::parse_error& e) {
        LOG_ERROR("[NASAWallpaper] JSON parse error: %s", e.what());
        return false;
    }

    NASAImage image;

    // Extract fields using nlohmann/json
    if (j.contains("url") && j["url"].is_string()) {
        image.url = j["url"].get<std::string>();
    }

    if (j.contains("hdurl") && j["hdurl"].is_string()) {
        image.hdUrl = j["hdurl"].get<std::string>();
    }

    if (j.contains("title") && j["title"].is_string()) {
        image.title = j["title"].get<std::string>();
    }

    if (j.contains("date") && j["date"].is_string()) {
        image.date = j["date"].get<std::string>();
    }

    if (j.contains("explanation") && j["explanation"].is_string()) {
        image.description = j["explanation"].get<std::string>();
    }

    if (j.contains("media_type") && j["media_type"].is_string()) {
        image.mediaType = j["media_type"].get<std::string>();
    } else {
        image.mediaType = "image";
    }

    if (image.isValid()) {
        m_images.push_back(image);
        LOG_INFO("[NASAWallpaper] Parsed APOD: %s (%s)", image.title.c_str(), image.date.c_str());
        return true;
    }

    return false;
}

bool NASAWallpaperManager::parseNASAImagesResponse(const std::string& jsonStr) {
    // Parse NASA Image Library response using nlohmann/json
    json j;
    try {
        j = json::parse(jsonStr);
    } catch (const json::parse_error& e) {
        LOG_ERROR("[NASAWallpaper] JSON parse error: %s", e.what());
        return false;
    }

    // Look for collection.items array
    if (!j.contains("collection") || !j["collection"].contains("items")) {
        LOG_ERROR("[NASAWallpaper] No collection.items in response");
        return false;
    }

    int itemsFound = 0;
    
    // Parse each item in the array
    for (const auto& item : j["collection"]["items"]) {
        NASAImage image;
        
        // Extract href (image URL)
        if (item.contains("href") && item["href"].is_string()) {
            image.url = item["href"].get<std::string>();
        }
        
        // Extract title from metadata
        if (item.contains("data") && item["data"].is_array() && !item["data"].empty()) {
            if (item["data"][0].contains("title")) {
                image.title = item["data"][0]["title"].get<std::string>();
            }
        }
        
        image.mediaType = "image";
        
        if (image.title.empty()) {
            image.title = "NASA Image " + std::to_string(itemsFound + 1);
        }

        if (image.isValid()) {
            m_images.push_back(image);
            itemsFound++;
        }
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

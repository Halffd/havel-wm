// NASA Wallpaper Tests

#include "NASAWallpaper.hpp"
#include <cstdio>
#include <cassert>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

namespace havel {
namespace tests {

/**
 * Test suite for NASA Wallpaper Manager
 */
class NASAWallpaperTests {
public:
    NASAWallpaperTests() : m_passed(0), m_failed(0) {}
    
    // Run all tests
    void runAll() {
        printf("\n=== NASA Wallpaper Tests ===\n\n");
        
        testInitialization();
        testFetchAPOD();
        testFetchNASAImages();
        testImageValidation();
        testCacheDirectory();
        testDownload();
        testSlideshow();
        
        printf("\n=== Test Results ===\n");
        printf("Passed: %d\n", m_passed);
        printf("Failed: %d\n", m_failed);
        printf("Total:  %d\n", m_passed + m_failed);
        printf("===================\n\n");
    }
    
private:
    int m_passed;
    int m_failed;
    
    void pass(const char* testName) {
        printf("✓ PASS: %s\n", testName);
        m_passed++;
    }
    
    void fail(const char* testName, const char* reason) {
        printf("✗ FAIL: %s - %s\n", testName, reason);
        m_failed++;
    }
    
    void testInitialization() {
        printf("\n--- Test: Initialization ---\n");
        
        auto& manager = getNASAWallpaperManager();
        
        // Test basic initialization
        bool initialized = manager.initialize("/tmp/havel-nasa-test");
        if (initialized) {
            pass("Initialization");
        } else {
            fail("Initialization", "Failed to initialize");
            return;
        }
        
        // Test isInitialized
        if (manager.isInitialized()) {
            pass("isInitialized");
        } else {
            fail("isInitialized", "Returned false after successful init");
        }
        
        // Test config
        const auto& config = manager.getConfig();
        if (!config.cacheDir.empty()) {
            pass("Config cache directory");
        } else {
            fail("Config cache directory", "Empty path");
        }
        
        manager.shutdown();
    }
    
    void testFetchAPOD() {
        printf("\n--- Test: Fetch APOD ---\n");
        
        auto& manager = getNASAWallpaperManager();
        if (!manager.initialize("/tmp/havel-nasa-test")) {
            fail("Fetch APOD", "Initialization failed");
            return;
        }
        
        // Fetch APOD
        bool fetched = manager.fetchAPOD(1);
        if (fetched) {
            pass("Fetch APOD");
        } else {
            fail("Fetch APOD", "API request failed");
            manager.shutdown();
            return;
        }
        
        // Check images were fetched
        const auto& images = manager.getImages();
        if (!images.empty()) {
            pass("Images fetched");
        } else {
            fail("Images fetched", "No images returned");
            manager.shutdown();
            return;
        }
        
        // Check image has required fields
        const auto& image = images[0];
        if (!image.url.empty()) {
            pass("Image URL present");
        } else {
            fail("Image URL present", "URL is empty");
        }
        
        if (!image.title.empty()) {
            pass("Image title present");
        } else {
            fail("Image title present", "Title is empty");
        }
        
        if (image.mediaType == "image") {
            pass("Media type is image");
        } else {
            fail("Media type is image", "Type mismatch");
        }
        
        manager.shutdown();
    }
    
    void testFetchNASAImages() {
        printf("\n--- Test: Fetch NASA Images ---\n");
        
        auto& manager = getNASAWallpaperManager();
        if (!manager.initialize("/tmp/havel-nasa-test")) {
            fail("Fetch NASA Images", "Initialization failed");
            return;
        }
        
        // Fetch NASA images
        bool fetched = manager.fetchNASAImages(5);
        if (fetched) {
            pass("Fetch NASA Images");
        } else {
            fail("Fetch NASA Images", "API request failed");
            manager.shutdown();
            return;
        }
        
        // Check images were fetched
        const auto& images = manager.getImages();
        if (!images.empty()) {
            pass("NASA Images fetched");
        } else {
            fail("NASA Images fetched", "No images returned");
        }
        
        manager.shutdown();
    }
    
    void testImageValidation() {
        printf("\n--- Test: Image Validation ---\n");
        
        auto& manager = getNASAWallpaperManager();
        if (!manager.initialize("/tmp/havel-nasa-test")) {
            fail("Image Validation", "Initialization failed");
            return;
        }
        
        // Fetch an image
        manager.fetchAPOD(1);
        const auto& images = manager.getImages();
        
        if (images.empty()) {
            fail("Image Validation", "No images to validate");
            manager.shutdown();
            return;
        }
        
        const auto& image = images[0];
        
        // Test isValid
        if (image.isValid()) {
            pass("Image isValid");
        } else {
            fail("Image isValid", "Image marked as invalid");
        }
        
        // Test getCurrentImage
        const NASAImage* currentImage = manager.getCurrentImage();
        if (currentImage != nullptr) {
            pass("getCurrentImage");
        } else {
            fail("getCurrentImage", "Returned null");
        }
        
        manager.shutdown();
    }
    
    void testCacheDirectory() {
        printf("\n--- Test: Cache Directory ---\n");
        
        auto& manager = getNASAWallpaperManager();
        std::string testCacheDir = "/tmp/havel-nasa-cache-test";
        
        if (!manager.initialize(testCacheDir)) {
            fail("Cache Directory", "Initialization failed");
            return;
        }
        
        // Check cache directory exists
        struct stat st;
        if (stat(testCacheDir.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            pass("Cache directory created");
        } else {
            fail("Cache directory created", "Directory not found");
        }
        
        // Test getCacheDir
        if (manager.getCacheDir() == testCacheDir) {
            pass("getCacheDir");
        } else {
            fail("getCacheDir", "Path mismatch");
        }
        
        // Test clear cache (should not fail on empty cache)
        manager.clearCache();
        pass("clearCache (empty)");
        
        // Test cache size
        size_t size = manager.getCacheSize();
        if (size == 0) {
            pass("getCacheSize (empty)");
        } else {
            fail("getCacheSize (empty)", "Non-zero size");
        }
        
        manager.shutdown();
        
        // Cleanup
        rmdir(testCacheDir.c_str());
    }
    
    void testDownload() {
        printf("\n--- Test: Download ---\n");
        
        auto& manager = getNASAWallpaperManager();
        if (!manager.initialize("/tmp/havel-nasa-download-test")) {
            fail("Download", "Initialization failed");
            return;
        }
        
        // Fetch an image
        bool fetched = manager.fetchAPOD(1);
        if (!fetched) {
            fail("Download", "Failed to fetch image");
            manager.shutdown();
            return;
        }
        
        const auto& images = manager.getImages();
        if (images.empty()) {
            fail("Download", "No images fetched");
            manager.shutdown();
            return;
        }
        
        // Test download
        bool downloaded = manager.downloadCurrentImage();
        if (downloaded) {
            pass("Download current image");
        } else {
            fail("Download current image", "Download failed");
        }
        
        // Test download progress
        int progress = manager.getDownloadProgress();
        if (progress == 100 || progress == 0) {
            pass("Download progress tracking");
        } else {
            fail("Download progress tracking", "Invalid progress");
        }
        
        // Test isDownloading
        bool isDownloading = manager.isDownloading();
        if (!isDownloading) {
            pass("isDownloading (after complete)");
        } else {
            fail("isDownloading (after complete)", "Still downloading");
        }
        
        manager.shutdown();
    }
    
    void testSlideshow() {
        printf("\n--- Test: Slideshow ---\n");
        
        auto& manager = getNASAWallpaperManager();
        if (!manager.initialize("/tmp/havel-nasa-slideshow-test")) {
            fail("Slideshow", "Initialization failed");
            return;
        }
        
        // Test start slideshow
        bool started = manager.startSlideshow(60);
        if (started) {
            pass("Start slideshow");
        } else {
            fail("Start slideshow", "Failed to start");
        }
        
        // Test isSlideshowRunning
        if (manager.isSlideshowRunning()) {
            pass("isSlideshowRunning");
        } else {
            fail("isSlideshowRunning", "Returned false");
        }
        
        // Test stop slideshow
        manager.stopSlideshow();
        if (!manager.isSlideshowRunning()) {
            pass("Stop slideshow");
        } else {
            fail("Stop slideshow", "Still running");
        }
        
        // Test next/previous image
        manager.fetchAPOD(3);
        
        const NASAImage* initialImage = manager.getCurrentImage();
        manager.nextImage();
        const NASAImage* nextImage = manager.getCurrentImage();
        
        if (initialImage != nextImage) {
            pass("nextImage");
        } else {
            fail("nextImage", "Same image");
        }
        
        manager.previousImage();
        const NASAImage* prevImage = manager.getCurrentImage();
        
        if (prevImage == initialImage) {
            pass("previousImage");
        } else {
            fail("previousImage", "Wrong image");
        }
        
        manager.shutdown();
    }
};

/**
 * Run NASA wallpaper tests
 */
void runNASAWallpaperTests() {
    NASAWallpaperTests tests;
    tests.runAll();
}

} // namespace tests
} // namespace havel

// Main for standalone test execution
#ifdef STANDALONE_TEST
int main() {
    havel::tests::runNASAWallpaperTests();
    return 0;
}
#endif

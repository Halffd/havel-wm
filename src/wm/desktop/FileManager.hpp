// File Manager Integration - Desktop file operations

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace havel {

/**
 * File info structure
 */
struct FileInfo {
    std::string name;
    std::string path;
    std::string mimeType;
    uint64_t size = 0;
    uint64_t modified = 0;
    bool isDirectory = false;
    bool isHidden = false;
    std::string icon;
};

/**
 * File Manager for desktop operations
 */
class FileManager {
public:
    FileManager();
    ~FileManager();

    // Directory operations
    std::vector<FileInfo> listDirectory(const std::string& path);
    bool createDirectory(const std::string& path);
    bool deleteDirectory(const std::string& path);
    
    // File operations
    bool copyFile(const std::string& src, const std::string& dst);
    bool moveFile(const std::string& src, const std::string& dst);
    bool deleteFile(const std::string& path);
    bool renameFile(const std::string& oldPath, const std::string& newPath);
    
    // File info
    FileInfo getFileInfo(const std::string& path);
    bool fileExists(const std::string& path);
    bool isDirectory(const std::string& path);
    uint64_t getFileSize(const std::string& path);
    
    // Desktop integration
    std::string getDesktopPath();
    std::string getHomePath();
    std::string getDownloadsPath();
    std::string getDocumentsPath();
    std::string getPicturesPath();
    
    // MIME type detection
    std::string getMimeType(const std::string& path);
    std::string getIconForMimeType(const std::string& mimeType);
    
    // Callbacks
    using FileCallback = std::function<void(const FileInfo&)>;
    using ErrorCallback = std::function<void(const std::string&)>;
    
    void setOnFileCreated(FileCallback cb) { m_onFileCreated = cb; }
    void setOnFileDeleted(FileCallback cb) { m_onFileDeleted = cb; }
    void setOnError(ErrorCallback cb) { m_onError = cb; }

private:
    std::string getBasename(const std::string& path);
    std::string getExtension(const std::string& path);
    
    FileCallback m_onFileCreated;
    FileCallback m_onFileDeleted;
    ErrorCallback m_onError;
};

/**
 * Global file manager access
 */
FileManager& getFileManager();

} // namespace havel

// File Manager Implementation

#include "FileManager.hpp"
#include <Logger.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>
#include <ctime>
#include <cstdint>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

namespace havel {

static FileManager* g_fileManager = nullptr;

FileManager& getFileManager() {
    if (!g_fileManager) {
        g_fileManager = new FileManager();
    }
    return *g_fileManager;
}

FileManager::FileManager() = default;

FileManager::~FileManager() = default;

std::vector<FileInfo> FileManager::listDirectory(const std::string& path) {
    std::vector<FileInfo> files;
    
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        if (m_onError) {
            m_onError("Cannot open directory: " + path);
        }
        return files;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        FileInfo info;
        info.name = entry->d_name;
        info.path = path + "/" + entry->d_name;
        info.isHidden = (entry->d_name[0] == '.');
        
        // Get file stats
        struct stat st;
        if (stat(info.path.c_str(), &st) == 0) {
            info.size = st.st_size;
            info.modified = st.st_mtime;
            info.isDirectory = S_ISDIR(st.st_mode);
            info.mimeType = info.isDirectory ? "inode/directory" : getMimeType(info.path);
            info.icon = getIconForMimeType(info.mimeType);
        }
        
        files.push_back(info);
    }
    
    closedir(dir);
    
    // Sort: directories first, then alphabetically
    std::sort(files.begin(), files.end(),
        [](const FileInfo& a, const FileInfo& b) {
            if (a.isDirectory != b.isDirectory) {
                return a.isDirectory;
            }
            return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
        });
    
    return files;
}

bool FileManager::createDirectory(const std::string& path) {
    // Create parent directories if needed
    size_t pos = path.rfind('/');
    if (pos != std::string::npos && pos > 0) {
        std::string parent = path.substr(0, pos);
        if (!fileExists(parent)) {
            createDirectory(parent);
        }
    }
    
    int result = mkdir(path.c_str(), 0755);
    if (result == 0) {
        LOG_INFO("[FileManager] Created directory: %s", path.c_str());
        
        FileInfo info;
        info.path = path;
        info.name = getBasename(path);
        info.isDirectory = true;
        info.mimeType = "inode/directory";
        
        if (m_onFileCreated) {
            m_onFileCreated(info);
        }
        
        return true;
    }
    
    if (m_onError) {
        m_onError("Failed to create directory: " + path);
    }
    return false;
}

bool FileManager::deleteDirectory(const std::string& path) {
    // Recursively delete contents
    std::vector<FileInfo> files = listDirectory(path);
    for (const auto& file : files) {
        if (file.isDirectory) {
            deleteDirectory(file.path);
        } else {
            deleteFile(file.path);
        }
    }
    
    int result = rmdir(path.c_str());
    if (result == 0) {
        LOG_INFO("[FileManager] Deleted directory: %s", path.c_str());
        return true;
    }
    
    if (m_onError) {
        m_onError("Failed to delete directory: " + path);
    }
    return false;
}

bool FileManager::copyFile(const std::string& src, const std::string& dst) {
    FILE* srcFile = fopen(src.c_str(), "rb");
    if (!srcFile) {
        if (m_onError) {
            m_onError("Cannot open source file: " + src);
        }
        return false;
    }
    
    FILE* dstFile = fopen(dst.c_str(), "wb");
    if (!dstFile) {
        fclose(srcFile);
        if (m_onError) {
            m_onError("Cannot create destination file: " + dst);
        }
        return false;
    }
    
    char buffer[4096];
    size_t bytesRead;
    while ((bytesRead = fread(buffer, 1, sizeof(buffer), srcFile)) > 0) {
        fwrite(buffer, 1, bytesRead, dstFile);
    }
    
    fclose(srcFile);
    fclose(dstFile);
    
    LOG_INFO("[FileManager] Copied: %s -> %s", src.c_str(), dst.c_str());
    return true;
}

bool FileManager::moveFile(const std::string& src, const std::string& dst) {
    if (rename(src.c_str(), dst.c_str()) == 0) {
        LOG_INFO("[FileManager] Moved: %s -> %s", src.c_str(), dst.c_str());
        return true;
    }
    
    // Cross-device move: copy then delete
    if (copyFile(src, dst)) {
        return deleteFile(src);
    }
    
    return false;
}

bool FileManager::deleteFile(const std::string& path) {
    int result = unlink(path.c_str());
    if (result == 0) {
        LOG_INFO("[FileManager] Deleted file: %s", path.c_str());
        
        FileInfo info;
        info.path = path;
        info.name = getBasename(path);
        
        if (m_onFileDeleted) {
            m_onFileDeleted(info);
        }
        
        return true;
    }
    
    if (m_onError) {
        m_onError("Failed to delete file: " + path);
    }
    return false;
}

bool FileManager::renameFile(const std::string& oldPath, const std::string& newPath) {
    if (rename(oldPath.c_str(), newPath.c_str()) == 0) {
        LOG_INFO("[FileManager] Renamed: %s -> %s", oldPath.c_str(), newPath.c_str());
        return true;
    }
    
    if (m_onError) {
        m_onError("Failed to rename file: " + oldPath);
    }
    return false;
}

FileInfo FileManager::getFileInfo(const std::string& path) {
    FileInfo info;
    info.path = path;
    info.name = getBasename(path);
    
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        info.size = st.st_size;
        info.modified = st.st_mtime;
        info.isDirectory = S_ISDIR(st.st_mode);
        info.isHidden = (info.name[0] == '.');
        info.mimeType = info.isDirectory ? "inode/directory" : getMimeType(path);
        info.icon = getIconForMimeType(info.mimeType);
    }
    
    return info;
}

bool FileManager::fileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

bool FileManager::isDirectory(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

uint64_t FileManager::getFileSize(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return st.st_size;
    }
    return 0;
}

std::string FileManager::getDesktopPath() {
    const char* home = getenv("HOME");
    if (home) {
        std::string desktop = std::string(home) + "/Desktop";
        if (fileExists(desktop)) {
            return desktop;
        }
    }
    return home ? std::string(home) : "/tmp";
}

std::string FileManager::getHomePath() {
    const char* home = getenv("HOME");
    return home ? std::string(home) : "/tmp";
}

std::string FileManager::getDownloadsPath() {
    const char* home = getenv("HOME");
    if (home) {
        std::string downloads = std::string(home) + "/Downloads";
        if (fileExists(downloads)) {
            return downloads;
        }
    }
    return home ? std::string(home) : "/tmp";
}

std::string FileManager::getDocumentsPath() {
    const char* home = getenv("HOME");
    if (home) {
        std::string docs = std::string(home) + "/Documents";
        if (fileExists(docs)) {
            return docs;
        }
    }
    return home ? std::string(home) : "/tmp";
}

std::string FileManager::getPicturesPath() {
    const char* home = getenv("HOME");
    if (home) {
        std::string pics = std::string(home) + "/Pictures";
        if (fileExists(pics)) {
            return pics;
        }
    }
    return home ? std::string(home) : "/tmp";
}

std::string FileManager::getMimeType(const std::string& path) {
    std::string ext = getExtension(path);
    
    if (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "gif" || ext == "bmp" || ext == "svg") {
        return "image/" + ext;
    }
    if (ext == "mp4" || ext == "avi" || ext == "mkv" || ext == "webm") {
        return "video/" + ext;
    }
    if (ext == "mp3" || ext == "wav" || ext == "flac" || ext == "ogg") {
        return "audio/" + ext;
    }
    if (ext == "txt" || ext == "md" || ext == "log") {
        return "text/plain";
    }
    if (ext == "pdf") {
        return "application/pdf";
    }
    if (ext == "doc" || ext == "docx") {
        return "application/msword";
    }
    if (ext == "xls" || ext == "xlsx") {
        return "application/vnd.ms-excel";
    }
    if (ext == "zip" || ext == "tar" || ext == "gz" || ext == "rar") {
        return "application/archive";
    }
    if (ext == "desktop") {
        return "application/x-desktop";
    }
    
    return "application/octet-stream";
}

std::string FileManager::getIconForMimeType(const std::string& mimeType) {
    if (mimeType == "inode/directory") {
        return "folder";
    }
    if (mimeType.find("image/") == 0) {
        return "image-x-generic";
    }
    if (mimeType.find("video/") == 0) {
        return "video-x-generic";
    }
    if (mimeType.find("audio/") == 0) {
        return "audio-x-generic";
    }
    if (mimeType == "text/plain") {
        return "text-x-generic";
    }
    if (mimeType == "application/pdf") {
        return "application-pdf";
    }
    if (mimeType.find("application/") == 0) {
        return "application-x-executable";
    }
    
    return "unknown";
}

std::string FileManager::getBasename(const std::string& path) {
    size_t pos = path.rfind('/');
    if (pos == std::string::npos) {
        return path;
    }
    return path.substr(pos + 1);
}

std::string FileManager::getExtension(const std::string& path) {
    size_t pos = path.rfind('.');
    if (pos == std::string::npos || pos == 0) {
        return "";
    }
    std::string ext = path.substr(pos + 1);
    // Convert to lowercase
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

} // namespace havel

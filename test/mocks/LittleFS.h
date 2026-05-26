#ifndef MOCK_LITTLEFS_H
#define MOCK_LITTLEFS_H

#include "Arduino.h"
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <memory>
#include <cstdarg>

class File {
public:
    std::string _path;
    bool _isDir;
    std::shared_ptr<std::fstream> _stream;
    std::vector<std::string> _dirFiles;
    size_t _dirIndex;

    File() : _path(""), _isDir(false), _stream(nullptr), _dirIndex(0) {}
    
    File(const std::string& path, bool isDir, std::shared_ptr<std::fstream> stream = nullptr) 
        : _path(path), _isDir(isDir), _stream(stream), _dirIndex(0) {}

    bool isDirectory() const { return _isDir; }
    
    File openNextFile() {
        if (!_isDir || _dirIndex >= _dirFiles.size()) {
            return File();
        }
        std::string filepath = _dirFiles[_dirIndex++];
        return File(filepath, false, std::make_shared<std::fstream>(filepath, std::ios::in));
    }

    String name() const {
        size_t lastSlash = _path.find_last_of('/');
        if (lastSlash == std::string::npos) {
            lastSlash = _path.find_last_of('\\');
        }
        if (lastSlash == std::string::npos) {
            return String(_path);
        }
        return String(_path.substr(lastSlash + 1));
    }

    void close() {
        if (_stream && _stream->is_open()) {
            _stream->close();
        }
    }

    operator bool() const {
        return _isDir || (_stream && _stream->is_open());
    }

    size_t size() {
        if (_isDir) return 0;
        try {
            return std::filesystem::file_size(_path);
        } catch (...) {
            return 0;
        }
    }

    int printf(const char* format, ...) {
        if (_stream && _stream->is_open()) {
            char loc_buf[1024];
            va_list arg;
            va_start(arg, format);
            int len = vsnprintf(loc_buf, sizeof(loc_buf), format, arg);
            va_end(arg);
            _stream->write(loc_buf, len);
            return len;
        }
        return 0;
    }

    void println(const String& s) {
        if (_stream && _stream->is_open()) {
            *_stream << s.c_str() << "\n";
        }
    }

    void println(const char* s) {
        if (_stream && _stream->is_open()) {
            *_stream << s << "\n";
        }
    }

    // Support std::istream/std::ostream interfaces for ArduinoJson
    int read() {
        if (_stream && _stream->is_open()) {
            return _stream->get();
        }
        return -1;
    }

    size_t read(uint8_t* buf, size_t size) {
        if (_stream && _stream->is_open()) {
            _stream->read(reinterpret_cast<char*>(buf), size);
            return _stream->gcount();
        }
        return 0;
    }

    size_t write(uint8_t val) {
        if (_stream && _stream->is_open()) {
            _stream->put(val);
            return 1;
        }
        return 0;
    }

    size_t write(const uint8_t* buf, size_t size) {
        if (_stream && _stream->is_open()) {
            _stream->write(reinterpret_cast<const char*>(buf), size);
            return size;
        }
        return 0;
    }

    int available() {
        if (_stream && _stream->is_open()) {
            std::streampos curr = _stream->tellg();
            _stream->seekg(0, std::ios::end);
            std::streampos end = _stream->tellg();
            _stream->seekg(curr);
            return (int)(end - curr);
        }
        return 0;
    }
};

class LittleFSImpl {
private:
    std::string getHostPath(const String& path) const {
        std::string p = path.c_str();
        // Replace virtual LittleFS path with a test directory
        if (p.rfind("/queue", 0) == 0) {
            return "./test_queue" + p.substr(6);
        }
        if (p == "/debug.log") {
            return "./test_queue/debug.log";
        }
        if (p == "/debug.bak.log") {
            return "./test_queue/debug.bak.log";
        }
        return "./test_queue" + p;
    }

public:
    bool begin(bool formatOnFail = false) {
        std::filesystem::create_directories("./test_queue");
        return true;
    }

    bool exists(const String& path) {
        return std::filesystem::exists(getHostPath(path));
    }

    bool mkdir(const String& path) {
        return std::filesystem::create_directories(getHostPath(path));
    }

    File open(const String& path, const char* mode) {
        std::string hostPath = getHostPath(path);
        std::string m = mode;

        if (m == "r") {
            if (std::filesystem::is_directory(hostPath)) {
                File dirFile(hostPath, true);
                for (const auto& entry : std::filesystem::directory_iterator(hostPath)) {
                    dirFile._dirFiles.push_back(entry.path().string());
                }
                return dirFile;
            }
            auto stream = std::make_shared<std::fstream>(hostPath, std::ios::in | std::ios::binary);
            if (!stream->is_open()) return File();
            return File(hostPath, false, stream);
        } 
        else if (m == "w") {
            auto stream = std::make_shared<std::fstream>(hostPath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!stream->is_open()) return File();
            return File(hostPath, false, stream);
        }
        else if (m == "a") {
            auto stream = std::make_shared<std::fstream>(hostPath, std::ios::out | std::ios::binary | std::ios::app);
            if (!stream->is_open()) return File();
            return File(hostPath, false, stream);
        }

        return File();
    }

    bool remove(const String& path) {
        try {
            return std::filesystem::remove(getHostPath(path));
        } catch (...) {
            return false;
        }
    }

    bool rename(const String& from, const String& to) {
        try {
            std::filesystem::rename(getHostPath(from), getHostPath(to));
            return true;
        } catch (...) {
            return false;
        }
    }
};

extern LittleFSImpl LittleFS;

#endif // MOCK_LITTLEFS_H

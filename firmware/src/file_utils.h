#pragma once

#include <Arduino.h>
#include <FS.h>
#include <ArduinoJson.h>

namespace FileUtils
{
    bool exists(fs::FS &fs, const char *path);

    bool ensureDirectory(fs::FS &fs, const char *dirPath);
    bool ensureParentDirectory(fs::FS &fs, const char *filePath);

    bool createFileIfNotExists(fs::FS &fs, const char *path);

    bool writeText(fs::FS &fs, const char *path, const String &content);
    bool appendText(fs::FS &fs, const char *path, const String &content);
    bool readText(fs::FS &fs, const char *path, String &outContent);

    bool writeBytes(fs::FS &fs, const char *path, const uint8_t *data, size_t length);

    bool readBytes(
        fs::FS &fs,
        const char *path,
        uint8_t *buffer,
        size_t maxLength,
        size_t &outBytesRead);

    bool removeFile(fs::FS &fs, const char *path);

    size_t fileSize(fs::FS &fs, const char *path);

    bool listFilesJson(fs::FS &fs, const char *dirPath, String &outJson, bool recursive = false);

    bool removeDirectoryRecursive(fs::FS &fs, const char *dirPath);

    bool removePath(fs::FS &fs, const char *path);
}
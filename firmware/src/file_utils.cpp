#include "file_utils.h"

namespace FileUtils
{

    bool exists(fs::FS &fs, const char *path)
    {
        if (path == nullptr)
        {
            return false;
        }

        return fs.exists(path);
    }

    bool ensureDirectory(fs::FS &fs, const char *dirPath)
    {
        if (dirPath == nullptr || strlen(dirPath) == 0)
        {
            return false;
        }

        if (fs.exists(dirPath))
        {
            return true;
        }

        return fs.mkdir(dirPath);
    }

    bool ensureParentDirectory(fs::FS &fs, const char *filePath)
    {
        if (filePath == nullptr)
        {
            return false;
        }

        String path = String(filePath);
        int lastSlash = path.lastIndexOf('/');

        if (lastSlash <= 0)
        {
            return true;
        }

        String dir = path.substring(0, lastSlash);

        if (dir.length() == 0)
        {
            return true;
        }

        if (fs.exists(dir))
        {
            return true;
        }

        String current = "";

        for (int i = 1; i < dir.length(); i++)
        {
            if (dir[i] == '/')
            {
                current = dir.substring(0, i);

                if (current.length() > 0 && !fs.exists(current))
                {
                    if (!fs.mkdir(current))
                    {
                        return false;
                    }
                }
            }
        }

        if (!fs.exists(dir))
        {
            return fs.mkdir(dir);
        }

        return true;
    }

    bool createFileIfNotExists(fs::FS &fs, const char *path)
    {
        if (path == nullptr)
        {
            return false;
        }

        if (fs.exists(path))
        {
            return true;
        }

        if (!ensureParentDirectory(fs, path))
        {
            return false;
        }

        File file = fs.open(path, FILE_WRITE);

        if (!file)
        {
            return false;
        }

        file.close();
        return true;
    }

    bool writeText(fs::FS &fs, const char *path, const String &content)
    {
        if (path == nullptr)
        {
            return false;
        }

        if (!ensureParentDirectory(fs, path))
        {
            return false;
        }

        File file = fs.open(path, FILE_WRITE);

        if (!file)
        {
            return false;
        }

        size_t written = file.print(content);
        file.flush();
        file.close();

        return written == content.length();
    }

    bool appendText(fs::FS &fs, const char *path, const String &content)
    {
        if (path == nullptr)
        {
            return false;
        }

        if (!createFileIfNotExists(fs, path))
        {
            return false;
        }

        File file = fs.open(path, FILE_APPEND);

        if (!file)
        {
            return false;
        }

        size_t written = file.print(content);
        file.flush();
        file.close();

        return written == content.length();
    }

    bool readText(fs::FS &fs, const char *path, String &outContent)
    {
        outContent = "";

        if (path == nullptr)
        {
            return false;
        }

        File file = fs.open(path, FILE_READ);

        if (!file)
        {
            return false;
        }

        while (file.available())
        {
            int c = file.read();

            if (c < 0)
            {
                file.close();
                return false;
            }

            outContent += char(c);
        }

        file.close();
        return true;
    }

    bool writeBytes(fs::FS &fs, const char *path, const uint8_t *data, size_t length)
    {
        if (path == nullptr || data == nullptr)
        {
            return false;
        }

        if (!ensureParentDirectory(fs, path))
        {
            return false;
        }

        File file = fs.open(path, FILE_WRITE);

        if (!file)
        {
            return false;
        }

        size_t written = file.write(data, length);
        file.flush();
        file.close();

        return written == length;
    }

    bool readBytes(
        fs::FS &fs,
        const char *path,
        uint8_t *buffer,
        size_t maxLength,
        size_t &outBytesRead)
    {
        outBytesRead = 0;

        if (path == nullptr || buffer == nullptr || maxLength == 0)
        {
            return false;
        }

        File file = fs.open(path, FILE_READ);

        if (!file)
        {
            return false;
        }

        while (file.available() && outBytesRead < maxLength)
        {
            int c = file.read();

            if (c < 0)
            {
                file.close();
                return false;
            }

            buffer[outBytesRead] = uint8_t(c);
            outBytesRead++;
        }

        file.close();
        return true;
    }

    bool removeFile(fs::FS &fs, const char *path)
    {
        if (path == nullptr)
        {
            return false;
        }

        if (!fs.exists(path))
        {
            return true;
        }

        return fs.remove(path);
    }

    size_t fileSize(fs::FS &fs, const char *path)
    {
        if (path == nullptr)
        {
            return 0;
        }

        File file = fs.open(path, FILE_READ);

        if (!file)
        {
            return 0;
        }

        size_t size = file.size();
        file.close();

        return size;
    }

    static String joinPath(const String &base, const char *name)
    {
        if (name == nullptr)
        {
            return base;
        }

        String child = String(name);

        // Su alcune versioni ESP32, entry.name() può già restituire path assoluto.
        if (child.startsWith("/"))
        {
            return child;
        }

        if (base.endsWith("/"))
        {
            return base + child;
        }

        return base + "/" + child;
    }

    bool listFilesJson(fs::FS &fs, const char *dirPath, String &outJson, bool recursive)
    {
        outJson = "[]";

        if (dirPath == nullptr || strlen(dirPath) == 0)
        {
            return false;
        }

        File root = fs.open(dirPath, FILE_READ);

        if (!root)
        {
            return false;
        }

        if (!root.isDirectory())
        {
            root.close();
            return false;
        }

        JsonDocument doc;
        JsonArray files = doc.to<JsonArray>();

        File file = root.openNextFile();

        while (file)
        {
            JsonObject item = files.add<JsonObject>();

            String filePath = joinPath(String(dirPath), file.name());

            item["path"] = filePath;
            item["name"] = file.name();
            item["type"] = file.isDirectory() ? "dir" : "file";
            item["size"] = file.isDirectory() ? 0 : file.size();

            if (recursive && file.isDirectory())
            {
                String childrenJson;

                if (listFilesJson(fs, filePath.c_str(), childrenJson, true))
                {
                    JsonDocument childrenDoc;
                    DeserializationError error = deserializeJson(childrenDoc, childrenJson);

                    if (!error && childrenDoc.is<JsonArray>())
                    {
                        item["children"] = childrenDoc.as<JsonArray>();
                    }
                }
            }

            file.close();
            file = root.openNextFile();
        }

        root.close();

        outJson = "";
        serializeJson(doc, outJson);

        return true;
    }

    bool removeDirectoryRecursive(fs::FS &fs, const char *dirPath)
    {
        if (dirPath == nullptr || strlen(dirPath) == 0)
        {
            return false;
        }

        File root = fs.open(dirPath, FILE_READ);

        if (!root)
        {
            return false;
        }

        if (!root.isDirectory())
        {
            root.close();
            return false;
        }

        File file = root.openNextFile();

        while (file)
        {
            String childPath = joinPath(String(dirPath), file.name());

            bool ok = false;

            if (file.isDirectory())
            {
                file.close();
                ok = removeDirectoryRecursive(fs, childPath.c_str());
            }
            else
            {
                file.close();
                ok = fs.remove(childPath.c_str());
            }

            if (!ok)
            {
                root.close();
                return false;
            }

            file = root.openNextFile();
        }

        root.close();

        return fs.rmdir(dirPath);
    }

    bool removePath(fs::FS &fs, const char *path)
    {
        if (path == nullptr || strlen(path) == 0)
        {
            return false;
        }

        if (!fs.exists(path))
        {
            return true;
        }

        File file = fs.open(path, FILE_READ);

        if (!file)
        {
            return false;
        }

        bool isDir = file.isDirectory();
        file.close();

        if (isDir)
        {
            return removeDirectoryRecursive(fs, path);
        }

        return fs.remove(path);
    }
}
#pragma once

#include <errno.h>
#include <mutex>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define EXECUTE "execute"

namespace util {

class ResourceManager {

  private:
    std::mutex mutex;
    std::string folderName;
    const int BUFFERSIZE = 4096;

  public:
    ResourceManager(const char *folder) { folderName = folder; }

    ~ResourceManager() {}

    // get size of the resource; -1 if not exist
    long getSize(const std::string &resourceName) {
        std::string filename = folderName + "/" + resourceName;
        struct stat stat_buf;
        int rc = stat(filename.c_str(), &stat_buf);
        return rc == 0 ? stat_buf.st_size : -1;
    }

    // get mime type
    const char *getContentType(const std::string &resourceName) {
        const char *name = resourceName.c_str();
        int dot = resourceName.find_last_of('.');
        if (dot > 0) {
            const char *extention = resourceName.c_str() + dot + 1;
            if (strcmp(extention, "html") == 0) {
                return "text/html";
            }
            if (strcmp(extention, "ico") == 0) {
                return "image/x-icon";
            }
            if (strcmp(extention, "css") == 0) {
                return "text/css";
            }
            if (strcmp(extention, "svg") == 0) {
                return "image/svg+xml";
            }
            if (strcmp(extention, "server") == 0) {
                return EXECUTE;
            }
        }
        return "text/plain";
    }

    // write resource to a socket
    bool writeToSocket(const std::string &resourceName, const int socket) {
        std::string filename = folderName + "/" + resourceName;
        FILE *file = fopen(filename.c_str(), "rb");
        if (!file) {
            fprintf(stderr, "Error: could not open file %s: %d - %s\n", filename.c_str(), errno, strerror(errno));
            return false;
        }
        // read the resource by chunks
        ssize_t bytes;
        char buffer[BUFFERSIZE];
        while ((bytes = fread(buffer, sizeof(char), BUFFERSIZE, file)) > 0) {
            // write them to stream
            auto bytesout = write(socket, buffer, bytes);
            if (bytesout <= 0) {
                fclose(file);
                fprintf(stderr, "Error: could not write to socket: %d - %s\n", errno, strerror(errno));
                return false;
            }
        }

        // Done and close
        fclose(file);
        return true;
    }

    // get resource as string
    void asString(const std::string &resourceName, std::string &outString) {
        std::string filename = folderName + "/" + resourceName;
        FILE *file = fopen(filename.c_str(), "rb");
        if (!file) {
            fprintf(stderr, "Error: could not open file %s: %d - %s\n", filename.c_str(), errno, strerror(errno));
        } else {
            // read the resource by chunks
            long bytes;
            char buffer[BUFFERSIZE];
            const char *p = buffer;
            while ((bytes = fread((void *)buffer, sizeof(char), BUFFERSIZE, file)) > 0) {
                // append them to str
                outString.append(p, bytes);
            }
            // Done and close.
            fclose(file);
        }
    }

    // no assignments allowed
    ResourceManager &operator=(const ResourceManager &) = delete;
    ResourceManager &operator=(ResourceManager &&) = delete;
};

} // namespace util
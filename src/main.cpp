
#include "HTTPMultiThreadServer.hpp"
#include "ResourceManager.hpp"
#include "V8Thread.hpp"
#include <signal.h>
#include <stdlib.h>
#include <thread>
#include <unistd.h>

typedef struct {
    util::ResourceManager *resourceManager;
    util::HTTPMultiThreadServer *httpServer;
    util::V8Thread *v8Thread;
} Context;

void response404(const int socket, const std::string &uri) {
    char content[500];
    sprintf(content, "resource not found: %s", uri.c_str());
    char header[1024];
    sprintf(header, "HTTP/1.1 404 Resource Not Found\r\nContent-type: text/plain\r\nContent-Length: %ld\r\n\r\n%s", strlen(content), content);
    write(socket, header, strlen(header));
    close(socket);
}

void connection_handler(util::HTTPRequest *request, void *_context) {
    Context *context = (Context *)_context;
    const auto socket = request->socket;
    // content type
    const char *contentType = context->resourceManager->getContentType(request->uri);

    if (strcmp(contentType, EXECUTE) == 0) {
        // execute on server
        const char *ex = strrchr(request->uri, '.');
        if (ex != nullptr) {
            std::string fileJS;
            fileJS.append(request->uri, ex - request->uri);
            fileJS += ".js";
            const long size = context->resourceManager->getSize(fileJS.c_str());
            if (size <= 0) {
                response404(request->socket, request->uri);
                return;
            }
            auto task = new util::V8Task(socket, fileJS, request->method, request->uri);
            context->v8Thread->enqueueTask(task);
        }
    } else {
        const long size = context->resourceManager->getSize(request->uri);
        if (size <= 0) {
            response404(request->socket, request->uri);
            return;
        }

        // serve file
        char header[1024];
        sprintf(header, "HTTP/1.1 200 OK\r\nContent-type: %s\r\nContent-Length: %ld\r\n\r\n", contentType, size);
        write(socket, header, strlen(header));
        context->resourceManager->writeToSocket(request->uri, socket);
        close(socket);
    }
}

int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN);
    const int port = 8888;
    Context context;

    util::ResourceManager resourceManager("./cache");
    util::HTTPMultiThreadServer server(port, 2, 1000);

    if (server.isInitialized()) {
        util::V8Thread v8executionThread(argv[0], &resourceManager);

        context.resourceManager = &resourceManager;
        context.httpServer = &server;
        context.v8Thread = &v8executionThread;

        if (server.startListening(connection_handler, (void *)&context)) {
            fprintf(stderr, "%s", server.getError());
        }
    } else {
        fprintf(stderr, "%s", server.getError());
    }
    return 0;
}
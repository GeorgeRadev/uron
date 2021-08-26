#pragma once

#include "ArrayBlockingQueue.hpp"
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace util {

class HTTPRequest {
  public:
    int socket;
    char *method;
    char *uri;

    HTTPRequest() {
        socket = -1;
        method = nullptr;
        uri = nullptr;
    }

    ~HTTPRequest() {
        socket = -1;
        delete[] method;
        delete[] uri;
    }

    HTTPRequest(int _socket, char *_method, char *_uri) {
        socket = _socket;
        {
            int len = strlen(_method);
            method = new char[len];
            strcpy(method, _method);
        }
        {
            int len = strlen(_uri);
            uri = new char[len];
            strcpy(uri, _uri);
        }
    }

    HTTPRequest &operator=(const HTTPRequest &_request) {
        socket = _request.socket;
        uri = _request.uri;
        {
            delete[] method;
            int len = strlen(_request.method);
            method = new char[len];
            strcpy(method, _request.method);
        }
        {
            delete[] uri;
            int len = strlen(_request.uri);
            uri = new char[len];
            strcpy(uri, _request.uri);
        }
        return *this;
    }
};

typedef void (*handler_type)(HTTPRequest *, void *context);

class HTTPMultiThreadServer {

#define METHOD_LIMIT 100
#define URI_LIMIT 4096

  private:
    int port;
    int server_socket;
    int handlers_count;
    std::thread *handlers;
    bool initialized;
    const char *error;
    util::ArrayBlockingQueue<HTTPRequest> requestQueue;
    handler_type requestHandler;
    void *requestHandlerContext;

  public:
    bool isInitialized() { return initialized; }
    const char *getError() { return error; }

    // init the server
    HTTPMultiThreadServer(unsigned int port, int thread_count, int requestQueueSize) : requestQueue(requestQueueSize) {
        initialized = false;
        this->port = port;
        error = nullptr;
        if (thread_count <= 0) {
            error = "thread_count must be positive number";
        } else {
            // Create socket
            server_socket = socket(AF_INET, SOCK_STREAM, 0);

            if (server_socket == -1) {
                error = "Could not create socket";
            } else { // Prepare the sockaddr_in structure
                struct sockaddr_in server;
                server.sin_family = AF_INET;
                server.sin_addr.s_addr = INADDR_ANY;
                server.sin_port = htons(8888);

                // Bind
                if (bind(server_socket, (struct sockaddr *)&server, sizeof(server)) < 0) {
                    error = "bind failed.";
                } else {
                    handlers_count = thread_count;
                    handlers = new std::thread[handlers_count];
                    if (handlers == nullptr) {
                        error = " no memory for threads";
                    } else {
                        for (int i = 0; i < handlers_count; i++) {
                            handlers[i] = std::thread(&threadServingHandler, this);
                        }
                        initialized = true;
                    }
                }
            }
        }
    }

    ~HTTPMultiThreadServer() {
        if (handlers != nullptr) {
            delete handlers;
        }
    }

    // start listening for connections and call the request handler
    const char *startListening(handler_type handler, void *context) {
        if (!initialized) {
            return error = "not initialized properly";
        }
        if (!handler) {
            return error = "handler cannot be null";
        }
        requestHandler = handler;
        requestHandlerContext = context;
        // Listen
        listen(server_socket, 1000);

        int c = sizeof(struct sockaddr_in);
        struct sockaddr_in client;
        int client_socket;
        // wait for connections

        fprintf(stderr, "Server started at port: %d\n", port);
        while ((client_socket = accept(server_socket, (struct sockaddr *)&client, (socklen_t *)&c))) {
            char method[METHOD_LIMIT + 1];
            char uri[URI_LIMIT + 1];

            read(client_socket, method, ' ', METHOD_LIMIT);
            read(client_socket, uri, ' ', URI_LIMIT);
            read(client_socket, nullptr, '\n', URI_LIMIT);
            // clear / from the beginning of the uri
            char *uri_cleaned = uri;
            while (*uri_cleaned == '/') {
                uri_cleaned++;
            }
            if (*uri_cleaned == '\0') {
                // set default uri
                uri_cleaned = uri;
                strcpy(uri_cleaned, "index.html");
            }

            if (validateMethod(method, METHOD_LIMIT) && validateUri(uri_cleaned, URI_LIMIT - (uri_cleaned - uri))) {
                HTTPRequest *request = new HTTPRequest(client_socket, method, uri_cleaned);
                requestQueue.enqueue(request);
            } else {
                const char *result = "invalid resource request";
                char response[512];
                sprintf(response, "HTTP/1.1 418 I'm a teapot\r\nContent-type: text/html\r\nContent-Length: %ld\r\n\r\n%s", strlen(result), result);
                write(client_socket, response, strlen(response));
                close(client_socket);
            }
        }

        if (client_socket < 0) {
            return error = "accept failed";
        } else {
            return nullptr;
        }
    }

    HTTPRequest *getRequest() { return requestQueue.dequeue_for(std::chrono::milliseconds(100)); }

    // no assignments allowed
    HTTPMultiThreadServer &operator=(const HTTPMultiThreadServer &) = delete;
    HTTPMultiThreadServer &operator=(HTTPMultiThreadServer &&) = delete;

  private:
    static void threadServingHandler(HTTPMultiThreadServer *server) {
        while (true) {
            HTTPRequest *request = server->getRequest();
            if (request) {
                try {
                    server->requestHandler(request, server->requestHandlerContext);
                } catch (const std::exception &e) {
                    fprintf(stderr, "Error: %s", e.what());
                } catch (...) {
                    // nothing to do here
                }
                delete request;
            }
        }
    }

    void read(int socket, char *buffer, char stopChar, int readlimit) {
        int i = 0;
        char c;
        while ((recv(socket, &c, 1, 0)) > 0) {
            if (c == stopChar) {
                break;
            }
            if (buffer != nullptr) {
                buffer[i] = c;
            }
            i++;
            if (i >= readlimit) {
                break;
            }
        }
        if (buffer != nullptr) {
            buffer[i] = 0;
        }
    }

    bool validateMethod(char *method, int limit) {
        int i = 0;
        while (i < limit) {
            char c = method[i];
            if (c == 0) {
                break;
            } else if (c < 'A' && 'Z' < c) {
                return false;
            }
            i++;
        }
        return i > 0;
    }

#define VALID_URI_CHAR(C) (('a' <= (C) && (C) <= 'z') || ('A' <= (C) && (C) <= 'Z') || ('0' <= (C) && (C) <= '9') || (C) == '_' || (C) == '-')

    // valid requests are small leters separated by / and the last is with extention
    // example : /aaa/bbb/ccccc.xxx?whatEver^comes=next
    bool validateUri(char *uri, int limit) {
        int i = 0;
        char prev;
        char c = 0;
        while (i < limit) {
            prev = c;
            c = uri[i];
            if (c == 0) {
                return i > 0;
            } else if (c == '?') {
                return prev != 0;
            } else if (VALID_URI_CHAR(c)) {
                // ok - continue checking
            } else if (c == '/') {
                if (prev == '/') {
                    return false;
                }
            } else if (c == '.') {
                // catch extention
                if (VALID_URI_CHAR(prev)) {
                    i++; // skip dot
                    while (i < limit) {
                        c = uri[i];
                        if (c == 0) {
                            return true;
                        } else if (VALID_URI_CHAR(c)) {
                            // ok - continue checking
                        } else if (c == '?') {
                            return true;
                        } else {
                            return false;
                        }
                        i++;
                    }
                    break;
                } else {
                    return false;
                }
            } else {
                return false;
            }
            i++;
        }
        return false;
    }
};

} // namespace util
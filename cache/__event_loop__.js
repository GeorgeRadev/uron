class HttpRequest {
    constructor(socket, method, uri) {
        this.context = Object.freeze({ socket, method, uri });
    }

    getMethod() {
        return this.context.method;
    }

    getURI() {
        return this.context.uri;
    }
}

const CONTENT_LENGTH = "content-length";
const CONTENT_TYPE = "content-type";

class HttpResponse {

    constructor(socket) {
        this.context = Object.freeze({ socket });
        this.header = {};
        this.statusCode = 200;
        this.contentType = 'application/json';
    }

    setContentType(contentType) {
        this.contentType = contentType;
    }

    getContentType() {
        return this.contentType;
    }
    setStatus(statusCode) {
        this.statusCode = statusCode;
    }

    setHeader(key, value) {
        key = key.toLowerCase();
        const oldValue = this.header[key];
        this.header[key] = (oldValue) ? (oldValue + "; " + value) : value;
    }

    getHeader(key) {
        key = key.toLowerCase();
        return this.header[key];
    }

    sendHeader() {
        for (const key in this.header) {
            core.coreSocketWrite(this.context.socket, key + ": " + this.header[key] + "\r\n");
        }
        // separator
        core.coreSocketWrite(this.context.socket, "\r\n");
    }

    send(buffer) {
        // http protocol
        const http = "HTTP/1.1 " + this.statusCode + " " + ((this.statusCode == 200) ? "OK" : "ERROR") + "\r\n";
        core.coreSocketWrite(this.context.socket, http);
        //http header
        if (this.contentType) {
            this.header[CONTENT_TYPE] = this.contentType;
        }
        if (!this.header[CONTENT_LENGTH]) {
            this.header[CONTENT_LENGTH] = core.coreGetBytesLength(buffer);
        }
        this.sendHeader();
        //send content
        core.coreSocketWrite(this.context.socket, buffer);
        core.coreSocketClose(this.context.socket);
    }
}

function log() {
    for (const e in arguments) {
        core.logSTDOUT(JSON.stringify(arguments[e]));
    }
}

function error() {
    for (const e of arguments) {
        core.logSTDOUT(JSON.stringify(arguments[e]));
    }
}

function include(modulename) {
    log({ log: "include: " + modulename });
    const result = core.coreInclude(modulename);
    log({ log: "include result: " + (typeof result), result: handler });
    if (typeof result === 'string') {
        throw new Error(result);
    }
    return result;
}

async function callHandler(request, urijs) {
    const socket = request.socket;
    try {
        const handler = include(urijs);

        if (typeof handler === 'function') {
            const httpRequest = new HttpRequest(socket, request.method, request.uri);
            const httpResponse = new HttpResponse(socket);
            const result = handler(httpRequest, httpResponse);
            log({ log: "handler result: " + (typeof result) + " isPromise: " + (result instanceof Promise) });
            await result;
        } else {
            error({ log: "serveRequest: handler in [" + urijs + "] type [" + (typeof handler) + "] is not a function.", result: handler });
            const result = "Not Implemented";
            const len = result.length;
            core.coreSocketWrite(socket, "HTTP/1.1 501 ERROR\r\n");
            core.coreSocketWrite(socket, "content-type: text/plain\r\n");
            core.coreSocketWrite(socket, "content-length: " + len + "\r\n\r\n");
            core.coreSocketWrite(socket, result);
        }
    } catch (error) {
        error({ log: "request exception: ", name: error.message, error: error.toString(), stack: error.stack.toString() });
        const result = error.message;
        const len = result.length;
        if (socket > 0) {
            core.coreSocketWrite(socket, "HTTP/1.1 500 ERROR\r\n");
            core.coreSocketWrite(socket, "content-type: text/plain\r\n");
            core.coreSocketWrite(socket, "content-length: " + len + "\r\n\r\n");
            core.coreSocketWrite(socket, result);
        }
    } finally {
        if (socket > 0) {
            core.coreSocketClose(socket);
        }
    }
}

function dummy() { }

function onUnhandledRejection(...args) {
    error({ log: "onUnhandledRejection", args: args })
}

// main loop for serving requests
while (true) {
    try {
        const request = core.coreGetRequest();
        if (request) {
            log({ log: "loop request", request });
            const uri = request.uri;
            const urijs = uri.split(".")[0] + ".js";
            const result = callHandler(request, urijs)
                .then(dummy)
                .catch(function (error) {
                    error({ log: "loop call exception: ", name: error.message, error: error.toString(), stack: error.stack.toString() });
                })
                .finally(dummy);
            Promise.resolve(result);
        }
    } catch (error) {
        error({ log: "loop exception", name: error.message, error: error.toString(), stack: error.stack.toString() });
    }
}
// this module is the entry point from the native code 
//serveRequest is called function to initialize the serving
// handler is the default function named after the module with parameters (HttpRequest, HttpResponse)

function include(modulename) {
    const result = core.coreInclude(modulename);
    if (typeof result === 'string') {
        throw new Error(result);
    }
    return result;
}

function log() {
    for (const e in arguments) {
        const value = arguments[e];
        if (value) {
            core.logSTDOUT(JSON.stringify(value));
        }
    }
}

function logError() {
    for (const e in arguments) {
        const value = arguments[e];
        if (value) {
            core.logSTDERR(JSON.stringify(value));
        }
    }
}

const { HttpRequest, HttpResponse } = include('http.js');

// special one for unhandled global promises
function onUnhandledRejection(error) {
    logError({ log: "unhandledRejection", error: error });
    const result = "ERROR: " + error.toString();
    const len = result.length;
    core.coreSocketWrite(currentRequest.request.socket, "HTTP/1.1 500 ERROR\r\n");
    core.coreSocketWrite(currentRequest.request.socket, "content-type: text/plain\r\n");
    core.coreSocketWrite(currentRequest.request.socket, "content-length: " + len + "\r\n\r\n");
    core.coreSocketWrite(currentRequest.request.socket, result);
}

const currentRequest = {};

function emptyFuntion() {
    logError({ log: "empty function" });
}

function execute(request, urijs) {
    const socket = request.socket;
    const handler = include(urijs);
    var error = "";

    if (typeof handler === 'function') {
        const httpRequest = new HttpRequest(socket, request.method, request.uri);
        const httpResponse = new HttpResponse(socket);
        handler(httpRequest, httpResponse);
        //core.coreSocketClose(socket);
        return;
    } else if (typeof handler === 'object') {
        const httpRequest = new HttpRequest(socket, request.method, request.uri);
        const httpResponse = new HttpResponse(socket);
        if (handler.default) {
            const handlerDefault = handler.default;
            if ((typeof handlerDefault) === 'function') {
                handlerDefault(httpRequest, httpResponse);
                //core.coreSocketClose(socket);
                return;
            } else {
                error = "handlerFunction type: " + (typeof handlerDefault);
            }
        } else {
            error = "no handlerFunction found";
        }
    }

    logError({ log: "serveRequest: handler in [" + urijs + "] type [" + (typeof handler) + "] is not a function.", result: handler });
    const result = "No Handler Implemented: " + error;
    const len = result.length;
    core.coreSocketWrite(socket, "HTTP/1.1 501 ERROR\r\n");
    core.coreSocketWrite(socket, "content-type: text/plain\r\n");
    core.coreSocketWrite(socket, "content-length: " + len + "\r\n\r\n");
    core.coreSocketWrite(socket, result);
    core.coreSocketClose(socket);
}

// main loop for serving requests
while (true) {
    let request = undefined;
    let socket = 0;
    try {
        request = core.coreGetRequest();
        if (request) {
            socket = request.socket;
            currentRequest.request = request;
            log({ log: "loop request", request });
            const uri = request.uri;
            const urijs = uri.split(".")[0] + ".js";
            execute(request, urijs);
        }
    } catch (exception) {
        logError({ log: "loop exception", name: exception.message, error: exception.toString(), stack: exception.stack.toString() });
        if (socket > 0) {
            const result = exception.message;
            const len = result.length;
            core.coreSocketWrite(socket, "HTTP/1.1 500 ERROR\r\n");
            core.coreSocketWrite(socket, "content-type: text/plain\r\n");
            core.coreSocketWrite(socket, "content-length: " + len + "\r\n\r\n");
            core.coreSocketWrite(socket, result);
            core.coreSocketClose(socket);
        }
    }
}
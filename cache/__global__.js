// this module is the entry point from the native code 
// with defining GLOBAL functions
// the result of this non module script MUST to be a function that will be called each time a request is made.
// call signature is :
// function(request)
// where request is object like {socket, method, uri}

const { log, logError } = include('log.js');
const { HttpRequest, HttpResponse } = include('http.js');

function emptyFuntion() {
    logError({ log: "empty function" });
}

async function execute(request, urijs) {
    const handler = include(urijs);
    logError({ log: "serveRequest: handler in [" + urijs + "] type [" + (typeof handler) + "] is not a function.", result: handler });
    var error = "";

    if (typeof handler === 'function') {
        const httpRequest = new HttpRequest(request.method, request.uri);
        const httpResponse = new HttpResponse();
        handler(httpRequest, httpResponse);
        return;
    } else if (typeof handler === 'object') {
        const httpRequest = new HttpRequest(request.method, request.uri);
        const httpResponse = new HttpResponse();
        if (handler.default) {
            const handlerDefault = handler.default;
            if ((typeof handlerDefault) === 'function') {
                handlerDefault(httpRequest, httpResponse);
                return;
            } else {
                error = "handlerFunction type: " + (typeof handlerDefault);
            }
        } else {
            error = "no handlerFunction found";
        }
    }

    const result = "No Handler Implemented: " + error;
    const len = result.length;
    core.socketWrite("HTTP/1.1 501 ERROR\r\n");
    core.socketWrite("content-type: text/plain\r\n");
    core.socketWrite("content-length: " + len + "\r\n\r\n");
    core.socketWrite(result);
    core.socketClose();
}

// main function for serving requests
function serveRequest(request) {
    const uri = request.uri;
    const urijs = uri.split(".")[0] + ".js";
    execute(request, urijs).then(
        () => {log({ log: "serveRequest then", request });core.socketClose()}
    ).catch(function () {
        () => {log({ log: "serveRequest catch", request });core.socketClose()}
    });
}

// the result is the handling function
serveRequest;
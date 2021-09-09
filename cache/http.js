// this module is the entry point from the native code 
//serveRequest is called function to initialize the serving
// handler is the default function named after the module with parameters (HttpRequest, HttpResponse)
export default function serveRequest(socket, method, uri, handler) {
    if (typeof handler === 'function') {
        const request = new HttpRequest(socket, method, uri);
        const response = new HttpResponse(socket);
        handler(request, response);
    } else {
        const result = "serveRequest: handler is not a function";
        const len = result.length();
        coreSocketWrite(socket, "HTTP/1.1 200 OK\r\n");
        coreSocketWrite(socket, "content-type: text/html\r\n");
        coreSocketWrite(socket, "content-length: " + len + "\r\n\r\n");
        coreSocketWrite(socket, result);
        coreSocketClose(socket);
    }
}

export class HttpRequest {
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

export class HttpResponse {

    constructor(socket) {
        this.context = Object.freeze({ socket });
        header = {};
        statusCode = 200;
        contentType = 'application/json';
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
        const oldValue = header[key];
        header[key] = (oldValue) ? (oldValue + "; " + value) : value;
    }

    getHeader(key) {
        key = key.toLowerCase();
        return header[key];
    }

    sendHeader() {
        for (key in header) {
            coreSocketWrite(context.socket, key + ": " + header[key] + "\r\n");
        }
        // separator
        coreSocketWrite(context.socket, "\r\n");
    }

    send(buffer) {
        // http protocol
        const http = "HTTP/1.1 " + this.statusCode + " " + ((this.statusCode == 200) ? "OK" : "ERROR") + "\r\n";
        coreSocketWrite(context.socket, http);
        //http header
        if (!header[CONTENT_LENGTH]) {
            header[CONTENT_LENGTH] = coreGetBytesLength(buffer);
        }
        this.sendHeader();
        //send content
        coreSocketWrite(context.socket, buffer);
        coreSocketClose(context.socket);
    }
}

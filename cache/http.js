// HTTP Request and Responce classes for reading and writing to requests
export const CONTENT_LENGTH = "content-length";
export const CONTENT_TYPE = "content-type";

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

export class HttpResponse {

    constructor(socket) {
        this.context = Object.freeze({ socket });
        this.header = {};
        this.statusCode = 200;
        this.contentType = 'application/json';
    }

    setContentType(contentType) {
        this.contentType = contentType;
        return this;
    }

    getContentType() {
        return this.contentType;
    }
    setStatus(statusCode) {
        this.statusCode = statusCode;
        return this;
    }

    setHeader(key, value) {
        key = key.toLowerCase();
        const oldValue = this.header[key];
        this.header[key] = (oldValue) ? (oldValue + "; " + value) : value;
        return this;
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
        return this;
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
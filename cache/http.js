// HTTP Request and Responce classes for reading and writing to requests
export const CONTENT_LENGTH = "content-length";
export const CONTENT_TYPE = "content-type";

export class HttpRequest {
    constructor(method, uri) {
        this.method = method;
        this.uri = uri;
    }

    getMethod() {
        return this.method;
    }

    getURI() {
        return this.uri;
    }

    getQuery() {
        if (!this.query) {
            const query = this.uri.substring(this.uri.split('?')[0].length + 1);
            const vars = query.split('&');
            const queryObject = {};
            for (var i = 0; i < vars.length; i++) {
                const currentVar = vars[i];
                const key = currentVar.split('=')[0];
                const value = currentVar.substring(key.length + 1);
                queryObject[key] = value;
            }
            this.query = queryObject;
        }
        return this.query;
    }

    getHeader() {
        if (!this.header) {
            const headerObject = {};
            const headerLines = core.socketHeader().trim().split('\n');
            for (var i = 0; i < headerLines.length; i++) {
                const currentHeaderLine = headerLines[i];
                const key = currentHeaderLine.split(':')[0];
                const value = currentHeaderLine.substring(key.length + 1).trim();
                headerObject[key] = value;
            }
            this.header = headerObject;
        }
        return this.header;
    }
}

export class HttpResponse {

    constructor() {
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
            core.socketWrite(key + ": " + this.header[key] + "\r\n");
        }
        // separator
        core.socketWrite("\r\n");
        return this;
    }

    send(buffer) {
        // http protocol
        const http = "HTTP/1.1 " + this.statusCode + " " + ((this.statusCode == 200) ? "OK" : "ERROR") + "\r\n";
        core.socketWrite(http);
        //http header
        if (this.contentType) {
            this.header[CONTENT_TYPE] = this.contentType;
        }
        if (!this.header[CONTENT_LENGTH]) {
            this.header[CONTENT_LENGTH] = core.getBytesLength(buffer);
        }
        this.sendHeader();
        //send content
        core.socketWrite(buffer);
    }
}
#pragma once

#define V8_COMPRESS_POINTERS
#define V8_31BIT_SMIS_ON_64BIT_ARCH
#include <libplatform/libplatform.h>
#include <v8.h>

#define SOCKET_VAR_NAME "_this_is_the_socket_variable_in_the_execution_context"

static std::size_t extra_space(const char *str) noexcept {
    std::size_t result = 0;
    for (int i = 0; str[i]; ++i) {
        const char c = str[i];
        switch (c) {
        case '"':
        case '\\':
        case '\b':
        case '\f':
        case '\n':
        case '\r':
        case '\t':
            // from c (1 byte) to \x (2 bytes)
            result += 1;
            break;
        default:
            if (c >= 0x00 and c <= 0x1f) {
                // from c (1 byte) to \uxxxx (6 bytes)
                result += 5;
            }
            break;
        }
    }
    return result;
}

static void escape_string(std::string &outstr, const char *str) noexcept {
    const auto space = extra_space(str);
    if (space == 0) {
        // no escaping required
        outstr += str;
        return;
    }

    // create a result string of necessary size
    outstr.reserve(outstr.length() + strlen(str) + space);
    char escape[3] = "\\";
    char escapeU[10];

    for (int i = 0; str[i]; ++i) {
        const char c = str[i];
        switch (c) {
        // quotation mark (0x22)
        case '"':
            escape[1] = '"';
            outstr += escape;
            break;
        // reverse solidus (0x5c)
        case '\\':
            escape[1] = '\\';
            outstr += escape;
            break;
        // backspace (0x08)
        case '\b':
            escape[1] = 'b';
            outstr += escape;
            break;
        // formfeed (0x0c)
        case '\f':
            escape[1] = 'f';
            outstr += escape;
            break;
        // newline (0x0a)
        case '\n':
            escape[1] = 'n';
            outstr += escape;
            break;
        // carriage return (0x0d)
        case '\r':
            escape[1] = 'r';
            outstr += escape;
            break;
        // horizontal tab (0x09)
        case '\t':
            escape[1] = 't';
            outstr += escape;
            break;
        default:
            if (c >= 0x00 and c <= 0x1f) {
                // print character c as \uxxxx
                sprintf(escapeU, "\\u%04x", int(c));
                outstr += escapeU;

            } else {
                // all other characters are added as-is
                outstr.push_back(c);
            }
            break;
        }
    }
}

static void serveError(int socket, const char *error) {
    const int len = strlen(error);
    const char *str;
    char buff[10];
    sprintf(buff, "%d", len);

    str = "HTTP/1.1 500 ERROR\r\n";
    write(socket, str, strlen(str));
    str = "content-type: text/plain\r\n";
    write(socket, str, strlen(str));
    str = "content-length: ";
    write(socket, str, strlen(str));
    str = buff;
    write(socket, str, strlen(str));
    str = "\r\n\r\n";
    write(socket, str, strlen(str));
    write(socket, error, len);
    close(socket);
}

static void serveError(int socket, std::string &errorText) {
    const char *error = errorText.c_str();
    serveError(socket, error);
}

static void logSTDOUT(const v8::FunctionCallbackInfo<v8::Value> &args) {
    if (args.Length() < 1) {
        return;
    }
    v8::Isolate *isolate = args.GetIsolate();
    v8::HandleScope scope(isolate);

    const int l = args.Length();
    for (int i = 0; i < l; ++i) {
        v8::Local<v8::Value> arg = args[i];
        v8::String::Utf8Value value(isolate, arg);
        fputs(*value, stderr);
        fputs("\r\n", stderr);
    }
}

static void logSTDERR(const v8::FunctionCallbackInfo<v8::Value> &args) {
    if (args.Length() < 1) {
        return;
    }
    v8::Isolate *isolate = args.GetIsolate();
    v8::HandleScope scope(isolate);

    const int l = args.Length();
    for (int i = 0; i < l; ++i) {
        v8::Local<v8::Value> arg = args[i];
        v8::String::Utf8Value value(isolate, arg);
        fputs(*value, stderr);
        fputs("\r\n", stderr);
    }
}

static void getBytesLength(const v8::FunctionCallbackInfo<v8::Value> &args) {
    const auto isolate = args.GetIsolate();
    v8::HandleScope scope(isolate);

    uint32_t len = 0;
    if (args.Length() == 1) {
        v8::String::Utf8Value value(isolate, args[0]);
        if (*value == NULL) {
            args.GetIsolate()->ThrowError("Cannot convert to char*");
            return;
        }
        v8::String::Utf8Value str(isolate, args[0]);
        len = strnlen(*str, 2 * str.length());
        args.GetReturnValue().Set(len);
    } else {
        args.GetReturnValue().Set(0);
    }
}

static int getSocket(v8::Isolate *isolate) {
    int socket = -1;
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    auto global = context->Global();
    auto socketMayValue = global->Get(context, v8::String::NewFromUtf8(isolate, SOCKET_VAR_NAME, v8::NewStringType::kNormal).ToLocalChecked());
    v8::Local<v8::Value> socketValue;
    if (socketMayValue.ToLocal(&socketValue)) {
        if (socketValue->IsNumber()) {
            socket = socketValue->Int32Value(isolate->GetCurrentContext()).ToChecked();
        }
    }
}

static void socketWrite(const v8::FunctionCallbackInfo<v8::Value> &args) {
    const auto isolate = args.GetIsolate();
    v8::HandleScope scope(isolate);
    const int socket = getSocket(isolate);
    if (socket > 3) {
        const int l = args.Length();
        for (int i = 0; i < l; ++i) {
            v8::Local<v8::Value> arg = args[i];
            v8::String::Utf8Value value(isolate, arg);
            const char *strValue = *value;
            if (strValue == NULL) {
                isolate->ThrowException(v8::String::NewFromUtf8(isolate, "Cannot convert parameter to char*").ToLocalChecked());
                return;
            }
            write(socket, strValue, strlen(strValue));
        }
    } else {
        fputs("no socket in current context !!!", stderr);
    }
}

static void socketClose(const v8::FunctionCallbackInfo<v8::Value> &args) {
    const auto isolate = args.GetIsolate();
    v8::HandleScope scope(isolate);
    const int socket = getSocket(isolate);
    if (socket > 3) {
        close(socket);
        args.GetReturnValue().Set(errno);
    } else {
        fputs("no socket in current context !!!", stderr);
        args.GetReturnValue().Set(-1);
    }
}
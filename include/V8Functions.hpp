#pragma once

#define V8_COMPRESS_POINTERS
#define V8_31BIT_SMIS_ON_64BIT_ARCH
#include <libplatform/libplatform.h>
#include <v8.h>

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

static const char *ToCString(const v8::String::Utf8Value &value) { return *value ? *value : "<string conversion failed>"; }

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

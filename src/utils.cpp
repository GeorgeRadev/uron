#include "utils.hpp"

namespace utils {

std::map<std::string, builtin *> builtins;
std::map<std::string, register_plugin> modules;
std::map<void *, std::string> isolateIDs;

ssize_t process_memory_usage() {
    char buf[1024];
    const char *s = NULL;
    ssize_t n = 0;
    unsigned long val = 0;
    int fd = 0;
    int i = 0;
    do {
        fd = open("/proc/thread-self/stat", O_RDONLY);
    } while (fd == -1 && errno == EINTR);
    if (fd == -1) {
        return (ssize_t)errno;
    }
    do {
        n = read(fd, buf, sizeof(buf) - 1);
    } while (n == -1 && errno == EINTR);
    close(fd);
    if (n == -1) {
        return (ssize_t)errno;
    }
    buf[n] = '\0';
    s = strchr(buf, ' ');
    if (s == NULL) {
        goto err;
    }
    s += 1;
    if (*s != '(') {
        goto err;
    }
    s = strchr(s, ')');
    if (s == NULL) {
        goto err;
    }
    for (i = 1; i <= 22; i++) {
        s = strchr(s + 1, ' ');
        if (s == NULL) {
            goto err;
        }
    }
    errno = 0;
    val = strtoul(s, NULL, 10);
    if (errno != 0) {
        goto err;
    }
    return val * (unsigned long)getpagesize();
err:
    return 0;
}

void builtins_add(const char *name, const char *source, unsigned int size) {
    struct builtin *b = new builtin();
    b->size = size;
    b->source = source;
    builtins[name] = b;
}

void SET_VALUE(Isolate *isolate, Local<ObjectTemplate> recv, const char *name, Local<Value> value) {
    // set local variable
    recv->Set(String::NewFromUtf8(isolate, name, NewStringType::kNormal).ToLocalChecked(), value);
}

void SET_METHOD(Isolate *isolate, Local<ObjectTemplate> recv, const char *name, FunctionCallback callback) {
    // define method callback
    recv->Set(String::NewFromUtf8(isolate, name, NewStringType::kNormal).ToLocalChecked(), FunctionTemplate::New(isolate, callback));
}

void SET_MODULE(Isolate *isolate, Local<ObjectTemplate> recv, const char *name, Local<ObjectTemplate> module) {
    // define module source
    recv->Set(String::NewFromUtf8(isolate, name, NewStringType::kNormal).ToLocalChecked(), module);
}

void PrintStackTrace(Isolate *isolate, const TryCatch &try_catch) {
    HandleScope handleScope(isolate);
    Local<Value> exception = try_catch.Exception();
    Local<Message> message = try_catch.Message();
    Local<StackTrace> stack = message->GetStackTrace();
    String::Utf8Value ex(isolate, exception);
    Local<Value> scriptName = message->GetScriptResourceName();
    String::Utf8Value scriptname(isolate, scriptName);
    Local<Context> context = isolate->GetCurrentContext();
    int linenum = message->GetLineNumber(context).FromJust();
    fprintf(stderr, "%s in %s on line %i\n", *ex, *scriptname, linenum);
    if (stack.IsEmpty())
        return;
    for (int i = 0; i < stack->GetFrameCount(); i++) {
        Local<StackFrame> stack_frame = stack->GetFrame(isolate, i);
        Local<String> functionName = stack_frame->GetFunctionName();
        Local<String> scriptName = stack_frame->GetScriptName();
        String::Utf8Value fn_name_s(isolate, functionName);
        String::Utf8Value script_name(isolate, scriptName);
        const int line_number = stack_frame->GetLineNumber();
        const int column = stack_frame->GetColumn();
        if (stack_frame->IsEval()) {
            if (stack_frame->GetScriptId() == Message::kNoScriptIdInfo) {
                fprintf(stderr, "    at [eval]:%i:%i\n", line_number, column);
            } else {
                fprintf(stderr, "    at [eval] (%s:%i:%i)\n", *script_name, line_number, column);
            }
            break;
        }
        if (fn_name_s.length() == 0) {
            fprintf(stderr, "    at %s:%i:%i\n", *script_name, line_number, column);
        } else {
            fprintf(stderr, "    at %s (%s:%i:%i)\n", *fn_name_s, *script_name, line_number, column);
        }
    }
    fflush(stderr);
}

v8::MaybeLocal<v8::Module> OnModuleInstantiate(Local<Context> context, Local<String> specifier, Local<Module> referrer) {
    HandleScope handle_scope(context->GetIsolate());
    return MaybeLocal<Module>();
}

void PromiseRejectCallback(PromiseRejectMessage data) {
    if (data.GetEvent() == v8::kPromiseRejectAfterResolved || data.GetEvent() == v8::kPromiseResolveAfterResolved) {
        // Ignore reject/resolve after resolved.
        return;
    }
    Local<Promise> promise = data.GetPromise();
    Isolate *isolate = promise->GetIsolate();
    if (data.GetEvent() == v8::kPromiseHandlerAddedAfterReject) {
        return;
    }
    Local<Value> exception = data.GetValue();
    v8::Local<Message> message;
    // Assume that all objects are stack-traces.
    if (exception->IsObject()) {
        message = v8::Exception::CreateMessage(isolate, exception);
    }
    if (!exception->IsNativeError() && (message.IsEmpty() || message->GetStackTrace().IsEmpty())) {
        // If there is no real Error object, manually create a stack trace.
        exception = v8::Exception::Error(v8::String::NewFromUtf8Literal(isolate, "Unhandled Promise."));
        message = Exception::CreateMessage(isolate, exception);
    }
    Local<Context> context = isolate->GetCurrentContext();
    TryCatch try_catch(isolate);
    Local<Object> globalInstance = context->Global();
    Local<Value> func = globalInstance->Get(context, String::NewFromUtf8Literal(isolate, "onUnhandledRejection", NewStringType::kNormal)).ToLocalChecked();
    if (func.IsEmpty()) {
        return;
    }
    Local<Function> onUnhandledRejection = Local<Function>::Cast(func);
    if (try_catch.HasCaught()) {
        fprintf(stderr, "PromiseRejectCallback: Cast: ");
        PrintStackTrace(isolate, try_catch);
        return;
    }
    Local<Value> argv[1] = {exception};
    MaybeLocal<Value> result = onUnhandledRejection->Call(context, globalInstance, 1, argv);
    if (result.IsEmpty() && try_catch.HasCaught()) {
        fprintf(stderr, "PromiseRejectCallback: Call: ");
        PrintStackTrace(isolate, try_catch);
        return;
    }
}

void FreeMemory(void *buf, size_t length, void *data) { free(buf); }

void generateUUID(std::string &str) {
    uuid_t binuuid;
    char uuid[40];
    uuid_generate_random(binuuid);
    uuid_unparse_upper(binuuid, uuid);
    str += uuid;
}

int CreateIsolate(int argc, char **argv, const char *main_src, unsigned int main_len, const char *js, unsigned int js_len, struct iovec *buf, int fd) {
    Isolate::CreateParams create_params;
    int statusCode = 0;
    create_params.array_buffer_allocator = ArrayBuffer::Allocator::NewDefaultAllocator();
    Isolate *isolate = Isolate::New(create_params);
    void *isolateAddress = (void *)isolate;
    bool stop = false;
    {
        Isolate::Scope isolate_scope(isolate);
        HandleScope handle_scope(isolate);
        // TODO: make this a config option
        isolate->SetCaptureStackTraceForUncaughtExceptions(true, 1000, StackTrace::kDetailed);
        Local<ObjectTemplate> global = ObjectTemplate::New(isolate);
        {
            std::string str;
            generateUUID(str);
            isolateIDs[isolateAddress] = str;
        }
        Local<ObjectTemplate> uron = ObjectTemplate::New(isolate);
        Init(isolate, uron);
        global->Set(String::NewFromUtf8Literal(isolate, "uron", NewStringType::kNormal), uron);
        Local<Context> context = Context::New(isolate, NULL, global);
        Context::Scope context_scope(context);
        // TODO: make this a config option
        context->AllowCodeGenerationFromStrings(false);
        isolate->SetPromiseRejectCallback(PromiseRejectCallback);
        Local<Array> arguments = Array::New(isolate);
        for (int i = 0; i < argc; i++) {
            arguments->Set(context, i, String::NewFromUtf8(isolate, argv[i], NewStringType::kNormal, strlen(argv[i])).ToLocalChecked()).Check();
        }
        Local<Object> globalInstance = context->Global();
        globalInstance->Set(context, String::NewFromUtf8Literal(isolate, "global", NewStringType::kNormal), globalInstance).Check();
        Local<Value> obj = globalInstance->Get(context, String::NewFromUtf8Literal(isolate, "uron", NewStringType::kNormal)).ToLocalChecked();
        Local<Object> uronInstance = Local<Object>::Cast(obj);
        if (buf != NULL) {
            std::unique_ptr<BackingStore> backing = ArrayBuffer::NewBackingStore(
                buf->iov_base, buf->iov_len, [](void *, size_t, void *) {}, nullptr);
            Local<SharedArrayBuffer> ab = SharedArrayBuffer::New(isolate, std::move(backing));
            uronInstance->Set(context, String::NewFromUtf8Literal(isolate, "buffer", NewStringType::kNormal), ab).Check();
        }
        if (fd != 0) {
            uronInstance->Set(context, String::NewFromUtf8Literal(isolate, "fd", NewStringType::kNormal), Integer::New(isolate, fd)).Check();
        }
        uronInstance->Set(context, String::NewFromUtf8Literal(isolate, "args", NewStringType::kNormal), arguments).Check();
        if (js_len > 0) {
            uronInstance->Set(context, String::NewFromUtf8Literal(isolate, "workerSource", NewStringType::kNormal), String::NewFromUtf8(isolate, js, NewStringType::kNormal, js_len).ToLocalChecked()).Check();
        }
        TryCatch try_catch(isolate);
        ScriptOrigin baseorigin(String::NewFromUtf8Literal(isolate, "generated.js",
                                                           NewStringType::kNormal), // resource name
                                Integer::New(isolate, 0),                           // line offset
                                Integer::New(isolate, 0),                           // column offset
                                False(isolate),                                     // is shared cross-origin
                                Local<Integer>(),                                   // script id
                                Local<Value>(),                                     // source map url
                                False(isolate),                                     // is opaque
                                False(isolate),                                     // is wasm
                                True(isolate)                                       // is module
        );
        Local<Module> module;
        Local<String> base;
        base = String::NewFromUtf8(isolate, main_src, NewStringType::kNormal, main_len).ToLocalChecked();
        ScriptCompiler::Source basescript(base, baseorigin);
        if (!ScriptCompiler::CompileModule(isolate, &basescript).ToLocal(&module)) {
            PrintStackTrace(isolate, try_catch);
            statusCode = 1;
            goto leave;
        }
        Maybe<bool> ok = module->InstantiateModule(context, OnModuleInstantiate);
        if (!ok.ToChecked()) {
            PrintStackTrace(isolate, try_catch);
            statusCode = 1;
            goto leave;
        }
        MaybeLocal<Value> maybe_result = module->Evaluate(context);
        Local<Value> result;
        if (!maybe_result.ToLocal(&result)) {
            PrintStackTrace(isolate, try_catch);
            // ReportException(isolate, &try_catch);
            statusCode = 2;
            goto leave;
        }
        Local<Value> func = globalInstance->Get(context, String::NewFromUtf8Literal(isolate, "onExit", NewStringType::kNormal)).ToLocalChecked();
        if (func->IsFunction()) {
            Local<Function> onExit = Local<Function>::Cast(func);
            Local<Value> argv[1] = {Integer::New(isolate, 0)};
            MaybeLocal<Value> result = onExit->Call(context, globalInstance, 0, argv);
            if (!result.IsEmpty()) {
                statusCode = result.ToLocalChecked()->Uint32Value(context).ToChecked();
            }
            if (try_catch.HasCaught() && !try_catch.HasTerminated()) {
                PrintStackTrace(isolate, try_catch);
                statusCode = 2;
                goto leave;
            }
            statusCode = result.ToLocalChecked()->Uint32Value(context).ToChecked();
        }
    }

leave:
    isolateIDs.erase(isolateAddress);

    isolate->ContextDisposedNotification();
    isolate->LowMemoryNotification();
    isolate->ClearKeptObjects();
    while (!stop) {
        stop = isolate->IdleNotificationDeadline(1);
    }
    isolate->Dispose();
    delete create_params.array_buffer_allocator;
    isolate = nullptr;
    return statusCode;
}

int CreateIsolate(int argc, char **argv, char const *main_src, std::size_t main_len) {
    // create Isolate only with main source
    return CreateIsolate(argc, argv, main_src, main_len, NULL, 0, NULL, 0);
}

void Print(const FunctionCallbackInfo<Value> &args) {
    Isolate *isolate = args.GetIsolate();
    if (args[0].IsEmpty())
        return;
    String::Utf8Value str(args.GetIsolate(), args[0]);
    int endline = 1;
    if (args.Length() > 1) {
        endline = static_cast<int>(args[1]->BooleanValue(isolate));
    }
    const char *cstr = *str;
    if (endline == 1) {
        fprintf(stdout, "%s\n", cstr);
    } else {
        fprintf(stdout, "%s", cstr);
    }
}

void Error(const FunctionCallbackInfo<Value> &args) {
    Isolate *isolate = args.GetIsolate();
    if (args[0].IsEmpty())
        return;
    String::Utf8Value str(args.GetIsolate(), args[0]);
    int endline = 1;
    if (args.Length() > 1) {
        endline = static_cast<int>(args[1]->BooleanValue(isolate));
    }
    const char *cstr = *str;
    if (endline == 1) {
        fprintf(stderr, "%s\n", cstr);
    } else {
        fprintf(stderr, "%s", cstr);
    }
}

void Load(const FunctionCallbackInfo<Value> &args) {
    Isolate *isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    Local<ObjectTemplate> exports = ObjectTemplate::New(isolate);
    if (args[0]->IsString()) {
        String::Utf8Value name(isolate, args[0]);
        auto iter = modules.find(*name);
        if (iter == modules.end()) {
            return;
        } else {
            register_plugin _init = (*iter->second);
            auto _register = reinterpret_cast<InitializerCallback>(_init());
            _register(isolate, exports);
        }
    } else {
        Local<BigInt> address64 = Local<BigInt>::Cast(args[0]);
        void *ptr = reinterpret_cast<void *>(address64->Uint64Value());
        register_plugin _init = reinterpret_cast<register_plugin>(ptr);
        auto _register = reinterpret_cast<InitializerCallback>(_init());
        _register(isolate, exports);
    }
    args.GetReturnValue().Set(exports->NewInstance(context).ToLocalChecked());
}

void Builtin(const FunctionCallbackInfo<Value> &args) {
    Isolate *isolate = args.GetIsolate();
    String::Utf8Value name(isolate, args[0]);
    builtin *b = builtins[*name];
    if (b == nullptr) {
        args.GetReturnValue().Set(Null(isolate));
        return;
    }
    if (args.Length() == 1) {
        args.GetReturnValue().Set(String::NewFromUtf8(isolate, b->source, NewStringType::kNormal, b->size).ToLocalChecked());
        return;
    }

    std::unique_ptr<BackingStore> backing = ArrayBuffer::NewBackingStore((void *)b->source, b->size, [](void *, size_t, void *) {}, nullptr);
    Local<SharedArrayBuffer> ab = SharedArrayBuffer::New(isolate, std::move(backing));

    // Local<ArrayBuffer> ab = ArrayBuffer::New(isolate, (void*)b->source, b->size, v8::ArrayBufferCreationMode::kExternalized);
    args.GetReturnValue().Set(ab);
}

void Sleep(const FunctionCallbackInfo<Value> &args) { sleep(Local<Integer>::Cast(args[0])->Value()); }

void MemoryUsage(const FunctionCallbackInfo<Value> &args) {
    Isolate *isolate = args.GetIsolate();
    ssize_t rss = process_memory_usage();
    HeapStatistics v8_heap_stats;
    isolate->GetHeapStatistics(&v8_heap_stats);
    Local<BigUint64Array> array;
    Local<ArrayBuffer> ab;
    if (args.Length() > 0) {
        array = args[0].As<BigUint64Array>();
        ab = array->Buffer();
    } else {
        ab = ArrayBuffer::New(isolate, 16 * 8);
        array = BigUint64Array::New(ab, 0, 16);
    }
    std::shared_ptr<BackingStore> backing = ab->GetBackingStore();
    uint64_t *fields = static_cast<uint64_t *>(backing->Data());
    fields[0] = rss;
    fields[1] = v8_heap_stats.total_heap_size();
    fields[2] = v8_heap_stats.used_heap_size();
    fields[3] = v8_heap_stats.external_memory();
    fields[4] = v8_heap_stats.does_zap_garbage();
    fields[5] = v8_heap_stats.heap_size_limit();
    fields[6] = v8_heap_stats.malloced_memory();
    fields[7] = v8_heap_stats.number_of_detached_contexts();
    fields[8] = v8_heap_stats.number_of_native_contexts();
    fields[9] = v8_heap_stats.peak_malloced_memory();
    fields[10] = v8_heap_stats.total_available_size();
    fields[11] = v8_heap_stats.total_heap_size_executable();
    fields[12] = v8_heap_stats.total_physical_size();
    fields[13] = isolate->AdjustAmountOfExternalAllocatedMemory(0);
    args.GetReturnValue().Set(array);
}

void Exit(const FunctionCallbackInfo<Value> &args) { exit(Local<Integer>::Cast(args[0])->Value()); }

void PID(const FunctionCallbackInfo<Value> &args) { args.GetReturnValue().Set(Integer::New(args.GetIsolate(), getpid())); }

void Chdir(const FunctionCallbackInfo<Value> &args) {
    Isolate *isolate = args.GetIsolate();
    String::Utf8Value path(isolate, args[0]);
    args.GetReturnValue().Set(Integer::New(isolate, chdir(*path)));
}

void ExecutionContextIDGetter(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<Value> &info) {
    Isolate *isolate = info.GetIsolate();
    std::string &executionID = isolateIDs[(void *)isolate];
    Local<String> id = String::NewFromUtf8(isolate, executionID.c_str()).ToLocalChecked();
    info.GetReturnValue().Set(id);
}

void ExecutionContextIDSetter(v8::Local<v8::String> property, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void> &info) {
    // constants cannot be set
}

void Init(Isolate *isolate, Local<ObjectTemplate> target) {
    Local<ObjectTemplate> version = ObjectTemplate::New(isolate);
    SET_VALUE(isolate, version, "v8", String::NewFromUtf8(isolate, v8::V8::GetVersion()).ToLocalChecked());
    Local<ObjectTemplate> kernel = ObjectTemplate::New(isolate);
    utsname kernel_rec;
    int rc = uname(&kernel_rec);
    if (rc == 0) {
        kernel->Set(String::NewFromUtf8Literal(isolate, "os", NewStringType::kNormal), String::NewFromUtf8(isolate, kernel_rec.sysname).ToLocalChecked());
        kernel->Set(String::NewFromUtf8Literal(isolate, "release", NewStringType::kNormal), String::NewFromUtf8(isolate, kernel_rec.release).ToLocalChecked());
        kernel->Set(String::NewFromUtf8Literal(isolate, "version", NewStringType::kNormal), String::NewFromUtf8(isolate, kernel_rec.version).ToLocalChecked());
    }
    version->Set(String::NewFromUtf8Literal(isolate, "kernel", NewStringType::kNormal), kernel);
    SET_METHOD(isolate, target, "print", Print);
    SET_METHOD(isolate, target, "error", Error);
    SET_METHOD(isolate, target, "load", Load);
    SET_METHOD(isolate, target, "exit", Exit);
    SET_METHOD(isolate, target, "pid", PID);
    SET_METHOD(isolate, target, "chdir", Chdir);
    SET_METHOD(isolate, target, "sleep", Sleep);
    SET_METHOD(isolate, target, "builtin", Builtin);
    SET_METHOD(isolate, target, "memoryUsage", MemoryUsage);
    SET_MODULE(isolate, target, "version", version);

    target->SetAccessor(String::NewFromUtf8(isolate, "executionContextID").ToLocalChecked(), ExecutionContextIDGetter, ExecutionContextIDSetter);
}
} // namespace uron
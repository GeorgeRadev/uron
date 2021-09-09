
#pragma once

#define V8_COMPRESS_POINTERS
#define V8_31BIT_SMIS_ON_64BIT_ARCH
#include <libplatform/libplatform.h>
#include <v8.h>

#include <map>
#include <stdlib.h>
#include <string.h>
#include <thread>

#include "ArrayBlockingQueue.hpp"
#include "ResourceManager.hpp"
#include "V8Functions.hpp"

// https://gist.github.com/surusek/4c05e4dcac6b82d18a1a28e6742fc23e

namespace util {

class V8Task {
  public:
    bool request;
    std::string module;
    std::string method;
    std::string uri;
    int socket;

    V8Task(int _socket, const std::string &_module, const std::string &_method, const std::string &_uri) {
        module = _module;
        method = _method;
        uri = _uri;
        socket = _socket;
        request = true;
    }

    ~V8Task() {}

    V8Task &operator=(const V8Task &task) = delete;
};

class V8Thread {
  private:
    static std::map<void *, V8Thread *> isolateToThread;
    static V8Thread *getByIsolate(void *isolate) { return V8Thread::isolateToThread[isolate]; }

    util::ResourceManager *resourceManager;
    v8::Global<v8::Value> lastResult;

    const char *arg;
    bool exit;
    std::thread eventLoopThread;
    util::ArrayBlockingQueue<V8Task> eventLoopQueue;

    void eventLoopThreadHandler() {
        // Creating platform
        std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform(4);
        // Initializing V8 VM
        v8::V8::InitializePlatform(platform.get());
        v8::V8::Initialize();
        // Creating isolate from the params (VM instance)
        v8::Isolate::CreateParams create_params;
        create_params.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
        v8::Isolate *isolate = v8::Isolate::New(create_params);
        // add to map
        isolateToThread[(void *)isolate] = this;
        {
            v8::Isolate::Scope isolate_scope(isolate);
            // any Local should has this one first
            v8::HandleScope handle_scope(isolate);

            isolate->SetCaptureStackTraceForUncaughtExceptions(true, 1000, v8::StackTrace::kDetailed);
            // Binding dynamic import() callback
            isolate->SetHostImportModuleDynamicallyCallback(callDynamic);
            isolate->SetPromiseRejectCallback(PromiseRejectCallback);

            v8::Local<v8::ObjectTemplate> global_ = v8::ObjectTemplate::New(isolate);
            {
                // Binding functions
                v8::Local<v8::ObjectTemplate> core = v8::ObjectTemplate::New(isolate);

                core->Set(isolate, "logSTDOUT", v8::FunctionTemplate::New(isolate, logSTDOUT));
                core->Set(isolate, "logSTDERR", v8::FunctionTemplate::New(isolate, logSTDERR));
                core->Set(isolate, "coreInclude", v8::FunctionTemplate::New(isolate, include));

                core->Set(isolate, "coreSetResult", v8::FunctionTemplate::New(isolate, setResult));
                core->Set(isolate, "coreSocketWrite", v8::FunctionTemplate::New(isolate, socketWrite));
                core->Set(isolate, "coreSocketClose", v8::FunctionTemplate::New(isolate, socketClose));
                core->Set(isolate, "coreGetBytesLength", v8::FunctionTemplate::New(isolate, getBytesLength));
                core->Set(isolate, "coreGetRequest", v8::FunctionTemplate::New(isolate, getRequest));

                global_->Set(v8::String::NewFromUtf8Literal(isolate, "core", v8::NewStringType::kNormal), core);
            }
            // Creating context
            v8::Local<v8::Context> context_ = v8::Context::New(isolate, NULL, global_);
            v8::Context::Scope context_scope(context_);
            context_->AllowCodeGenerationFromStrings(false);
            v8::Local<v8::Object> globalInstance = context_->Global();
            globalInstance->Set(context_, v8::String::NewFromUtf8Literal(isolate, "global", v8::NewStringType::kNormal), globalInstance).Check();
            v8::Local<v8::Value> obj = globalInstance->Get(context_, v8::String::NewFromUtf8Literal(isolate, "core", v8::NewStringType::kNormal)).ToLocalChecked();
            v8::Local<v8::Object> coreInstance = v8::Local<v8::Object>::Cast(obj);
            coreInstance->Set(context_, v8::String::NewFromUtf8Literal(isolate, "global", v8::NewStringType::kNormal), globalInstance).Check();
            
            {
                // Enter this processor's context so all the remaining operations be executed in it
                v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate, context_);
                v8::Context::Scope context_scope(context);
                v8::Local<v8::Object> global = context->Global();
                v8::TryCatch try_catch(isolate);

                // call request handling loop
                std::string name("__event_loop__.js");
                std::string source;
                resourceManager->asString(name, source);
                v8::Local<v8::String> nameLocal = v8::String::NewFromUtf8(isolate, name.c_str()).ToLocalChecked();
                v8::Local<v8::String> sourceLocal = v8::String::NewFromUtf8(isolate, source.c_str()).ToLocalChecked();

                v8::ScriptOrigin origin(nameLocal,                    // source name
                                        v8::Integer::New(isolate, 0), // line offset
                                        v8::Integer::New(isolate, 0), // column offset
                                        v8::False(isolate),           // cross origin
                                        v8::Local<v8::Integer>(),     // script id
                                        v8::Local<v8::Value>(),       // source map url
                                        v8::False(isolate),           // is opaque
                                        v8::False(isolate),           // is WASM
                                        v8::False(isolate)            // is ES6 module
                );

                auto compileResult = v8::Script::Compile(context, sourceLocal, &origin);
                v8::Local<v8::Script> script;
                if (compileResult.ToLocal(&script)) {
                    v8::Local<v8::Value> result;
                    auto runResult = script->Run(context);
                }
                ReportException(isolate, try_catch);
            }
        }
        // clean up
        isolateToThread.erase((void *)isolate);

        // Proper VM deconstructing
        isolate->Dispose();
        v8::V8::Dispose();
        v8::V8::ShutdownPlatform();
    }

  public:
    V8Thread(const char *_argv0, util::ResourceManager *_resourceManager) : exit(false), resourceManager(_resourceManager), eventLoopQueue(256), eventLoopThread(&V8Thread::eventLoopThreadHandler, this) {
        arg = _argv0;
        eventLoopThread.detach();
    }

    ~V8Thread() {
        exit = true;
        // eventLoopThread.join();
    }

    void enqueueTask(V8Task *task) { eventLoopQueue.enqueue(task); }

  private:
    static void include(const v8::FunctionCallbackInfo<v8::Value> &args) {
        if (args.Length() < 1) {
            return;
        }
        v8::Isolate *isolate = args.GetIsolate();

        // Enter this processor's context so all the remaining operations be executed in it
        v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate, isolate->GetCurrentContext());
        v8::Context::Scope context_scope(context);
        v8::Local<v8::Object> global = context->Global();
        v8::TryCatch try_catch(isolate);

        v8::Local<v8::String> name;
        v8::Local<v8::String> source;
        {
            v8::Local<v8::Value> arg = args[0];
            v8::String::Utf8Value resource(isolate, arg);
            std::string resourceName(*resource);
            std::string resourceSource;
            fprintf(stderr, "include: %s\n", resourceName.c_str());

            V8Thread *thread = getByIsolate(isolate);
            thread->resourceManager->asString(resourceName, resourceSource);
            name = v8::String::NewFromUtf8(isolate, resourceName.c_str()).ToLocalChecked();
            source = v8::String::NewFromUtf8(isolate, resourceSource.c_str(), v8::NewStringType::kNormal, static_cast<int>(resourceSource.length())).ToLocalChecked();
        }

        v8::ScriptOrigin origin(name,                         // source name
                                v8::Integer::New(isolate, 0), // line offset
                                v8::Integer::New(isolate, 0), // column offset
                                v8::False(isolate),           // cross origin
                                v8::Local<v8::Integer>(),     // script id
                                v8::Local<v8::Value>(),       // source map url
                                v8::False(isolate),           // is opaque
                                v8::False(isolate),           // is WASM
                                v8::False(isolate)            // is ES6 module
        );

        // compile function close
        v8::ScriptCompiler::Source basescript(source, origin);
        auto maybeFunctionScript = v8::ScriptCompiler::CompileFunctionInContext(context, &basescript, 0, nullptr, 0, nullptr, v8::ScriptCompiler::kEagerCompile);

        if (try_catch.HasCaught()) {
            // ReportException(isolate, try_catch);
            v8::Local<v8::String> errorText = getExceptionText(isolate, try_catch);
            // isolate->ThrowException(exception);
            args.GetReturnValue().Set(errorText);
            return;
        }

        // run the function closure
        auto function = maybeFunctionScript.ToLocalChecked();
        v8::MaybeLocal<v8::Value> callResult = function->Call(context, global, 0, nullptr);
        v8::Local<v8::Value> result;
        if (callResult.ToLocal(&result)) {
            args.GetReturnValue().Set(result);
        } else {
            if (try_catch.HasCaught()) {
                // ReportException(isolate, try_catch);
                v8::Local<v8::String> errorText = getExceptionText(isolate, try_catch);
                // isolate->ThrowException(exception);
                args.GetReturnValue().Set(errorText);
            }
        }
    }

    static void getRequest(const v8::FunctionCallbackInfo<v8::Value> &args) {
        auto isolate = args.GetIsolate();
        V8Thread *thread = getByIsolate(isolate);
        auto context = isolate->GetCurrentContext();
        v8::HandleScope scope(isolate);
        if (thread) {
            V8Task *task = thread->eventLoopQueue.dequeue_for(std::chrono::milliseconds(1000));
            if (task) {
                try {
                    v8::Local<v8::Object> requestObject = v8::Object::New(isolate);
                    auto t = requestObject->Set(context, v8::String::NewFromUtf8(isolate, "method", v8::NewStringType::kNormal).ToLocalChecked(), v8::String::NewFromUtf8(isolate, task->method.c_str(), v8::NewStringType::kNormal).ToLocalChecked());
                    t = requestObject->Set(context, v8::String::NewFromUtf8(isolate, "uri", v8::NewStringType::kNormal).ToLocalChecked(), v8::String::NewFromUtf8(isolate, task->uri.c_str(), v8::NewStringType::kNormal).ToLocalChecked());
                    t = requestObject->Set(context, v8::String::NewFromUtf8(isolate, "socket", v8::NewStringType::kNormal).ToLocalChecked(), v8::Int32::New(isolate, task->socket));

                    args.GetReturnValue().Set(requestObject);
                    return;

                } catch (const std::exception &e) {
                    fprintf(stderr, "V8Thread Error: %s\n", e.what());
                } catch (...) {
                    // nothing to do here
                    fprintf(stderr, "V8Thread Exception\n");
                }
                delete task;
            }
        }
        // return undefined on no requests
        args.GetReturnValue().Set(v8::Undefined(isolate));
    }

    static void setResult(const v8::FunctionCallbackInfo<v8::Value> &args) {
        auto isolate = args.GetIsolate();
        auto thread = getByIsolate(isolate);
        v8::HandleScope scope(isolate);
        if (args.Length() > 0) {
            thread->lastResult.Reset(isolate, args[0]);
        } else {
            thread->lastResult.Reset(isolate, v8::Undefined(isolate));
        }
    }

    static void socketWrite(const v8::FunctionCallbackInfo<v8::Value> &args) {
        auto isolate = args.GetIsolate();
        v8::HandleScope scope(isolate);

        if (args.Length() < 2 || !args[0]->IsNumber()) {
            isolate->ThrowException(v8::String::NewFromUtf8(isolate, "Expecting 2 parameters: socketNumber and arrayBuffer").ToLocalChecked());
            return;
        }
        v8::Local<v8::Number> socketRef = args[0].As<v8::Number>();
        const int socket = socketRef->IntegerValue(isolate->GetCurrentContext()).ToChecked();

        // as string value
        v8::String::Utf8Value value(args.GetIsolate(), args[1]);
        const char *strValue = *value;
        if (*value == NULL) {
            args.GetIsolate()->ThrowError("Error getting value");
            return;
        }
        if (socket > 0) {
            write(socket, strValue, strlen(strValue));
        }
    }

    static void socketClose(const v8::FunctionCallbackInfo<v8::Value> &args) {
        auto isolate = args.GetIsolate();
        v8::HandleScope scope(isolate);

        if (args.Length() < 1 || !args[0]->IsNumber()) {
            isolate->ThrowException(v8::String::NewFromUtf8(isolate, "Expecting 1 parameters: socketNumber ").ToLocalChecked());
            return;
        }
        int socket = args[0]->Int32Value(isolate->GetCurrentContext()).ToChecked();
        if (socket > 0) {
            close(socket);
            args.GetReturnValue().Set(errno);
        } else {
            args.GetReturnValue().Set(-1);
        }
    }

    static void getBytesLength(const v8::FunctionCallbackInfo<v8::Value> &args) {
        auto isolate = args.GetIsolate();
        v8::HandleScope scope(isolate);

        uint32_t len = 0;
        if (args.Length() == 1) {
            v8::String::Utf8Value file(args.GetIsolate(), args[0]);
            if (*file == NULL) {
                args.GetIsolate()->ThrowError("Error loading file");
                return;
            }
            v8::String::Utf8Value str(isolate, args[0]);
            len = strnlen(*str, 2 * str.length());
            args.GetReturnValue().Set(len);
        } else {
            args.GetReturnValue().Set(-1);
        }
    }

    static v8::Local<v8::String> getExceptionText(v8::Isolate *isolate, v8::TryCatch &try_catch) {
        if (!try_catch.HasCaught()) {
            return v8::String::NewFromUtf8(isolate, "").ToLocalChecked();
        }
        std::string errorMessage;
        v8::String::Utf8Value exception(isolate, try_catch.Exception());
        const char *exception_string = *exception;
        v8::Local<v8::Message> message = try_catch.Message();
        if (message.IsEmpty()) {
            // V8 didn't provide any extra information about this error; just
            // print the exception.
            errorMessage += exception_string;
        } else {
            // generate:
            // (filename):(line number): (message)
            // (source line)
            v8::String::Utf8Value filename(isolate, message->GetScriptOrigin().ResourceName());
            v8::Local<v8::Context> context(isolate->GetCurrentContext());
            const char *filename_string = *filename;
            int linenum = message->GetLineNumber(context).FromJust();
            errorMessage += filename_string;
            errorMessage += ":";
            {
                char buf[100];
                sprintf(buf, "%d", linenum);
                errorMessage += buf;
            }
            errorMessage += ": ";
            errorMessage += exception_string;
            errorMessage += "\n";

            v8::String::Utf8Value sourceline(isolate, message->GetSourceLine(context).ToLocalChecked());
            const char *sourceline_string = *sourceline;
            errorMessage += sourceline_string;
            errorMessage += "\n";

            // Print wavy underline (GetUnderline is deprecated).
            int start = message->GetStartColumn(context).FromJust();
            for (int i = 0; i < start; i++) {
                errorMessage += " ";
            }
            int end = message->GetEndColumn(context).FromJust();
            for (int i = start; i < end; i++) {
                errorMessage += "^";
            }
            errorMessage += "\n";
        }
        fputs(errorMessage.c_str(), stderr);
        return v8::String::NewFromUtf8(isolate, errorMessage.c_str()).ToLocalChecked();
    }

    static void ReportException(v8::Isolate *isolate, v8::TryCatch &try_catch) {
        if (!try_catch.HasCaught()) {
            return;
        }
        v8::String::Utf8Value exception(isolate, try_catch.Exception());
        const char *exception_string = *exception;
        v8::Local<v8::Message> message = try_catch.Message();
        if (message.IsEmpty()) {
            // V8 didn't provide any extra information about this error; just
            // print the exception.
            fprintf(stderr, "%s\n", exception_string);
        } else {
            // Print (filename):(line number): (message).
            v8::String::Utf8Value filename(isolate, message->GetScriptOrigin().ResourceName());
            v8::Local<v8::Context> context(isolate->GetCurrentContext());
            const char *filename_string = *filename;
            int linenum = message->GetLineNumber(context).FromJust();
            fprintf(stderr, "%s:%i: %s\n", filename_string, linenum, exception_string);
            // Print line of source code.
            v8::String::Utf8Value sourceline(isolate, message->GetSourceLine(context).ToLocalChecked());
            const char *sourceline_string = *sourceline;
            fprintf(stderr, "%s\n", sourceline_string);
            // Print wavy underline (GetUnderline is deprecated).
            int start = message->GetStartColumn(context).FromJust();
            for (int i = 0; i < start; i++) {
                fprintf(stderr, " ");
            }
            int end = message->GetEndColumn(context).FromJust();
            for (int i = start; i < end; i++) {
                fprintf(stderr, "^");
            }
            fprintf(stderr, "\n");
            v8::Local<v8::Value> stack_trace_string;
            if (try_catch.StackTrace(context).ToLocal(&stack_trace_string) && stack_trace_string->IsString() && stack_trace_string.As<v8::String>()->Length() > 0) {
                v8::String::Utf8Value stack_trace(isolate, stack_trace_string);
                const char *stack_trace_string = *stack_trace;
                fprintf(stderr, "%s\n", stack_trace_string);
            }
        }
    }

    static v8::MaybeLocal<v8::Module> loadModule(v8::Local<v8::Context> context, const char *name, const char *code) {
        // Convert char[] to VM's string type
        auto isolate = context->GetIsolate();
        v8::Local<v8::String> vcode = v8::String::NewFromUtf8(isolate, code).ToLocalChecked();
        // Create script origin to determine if it is module or not.
        // Only first and last argument matters; other ones are default values.
        // First argument gives script name (useful in error messages), last
        // informs that it is a module.
        v8::ScriptOrigin origin(v8::String::NewFromUtf8(isolate, name).ToLocalChecked(), // source name
                                v8::Integer::New(isolate, 0),                            // line offset
                                v8::Integer::New(isolate, 0),                            // column offset
                                v8::False(isolate),                                      // cross origin
                                v8::Local<v8::Integer>(),                                // script id
                                v8::Local<v8::Value>(),                                  // source map url
                                v8::False(isolate),                                      // is opaque
                                v8::False(isolate),                                      // is WASM
                                v8::True(isolate)                                        // is ES6 module
        );

        // Compiling module from source (code + origin)
        v8::ScriptCompiler::Source source(vcode, origin);
        v8::MaybeLocal<v8::Module> mod;
        mod = v8::ScriptCompiler::CompileModule(isolate, &source);
        // Returning non-checked module
        return mod;
    }

    static bool checkModule(v8::Local<v8::Context> context, v8::MaybeLocal<v8::Module> maybeModule) {
        auto isolate = context->GetIsolate();
        v8::Local<v8::Module> mod;
        if (!maybeModule.ToLocal(&mod)) {
            printf("Error loading module!\n");
            return false;
        }
        v8::Maybe<bool> result = mod->InstantiateModule(context, callResolve);
        // return !result.IsNothing();
        return result.FromMaybe(false);
    }

    static v8::Local<v8::Value> runModule(v8::Local<v8::Context> context, v8::Local<v8::Module> module, bool nsObject) {
        auto isolate = context->GetIsolate();
        v8::Local<v8::Value> retValue;
        if (module->Evaluate(context).ToLocal(&retValue)) {
            if (nsObject) {
                retValue = module->GetModuleNamespace();
            }
        } else {
            fprintf(stderr, "Error evaluating module!\n");
            retValue = module->GetModuleNamespace();
        }
        return retValue;
    }

    static v8::MaybeLocal<v8::Module> callResolve(v8::Local<v8::Context> context, v8::Local<v8::String> specifier, v8::Local<v8::Module> referrer) {
        auto isolate = context->GetIsolate();
        v8::String::Utf8Value name(isolate, specifier);
        V8Thread *thread = getByIsolate(isolate);
        std::string resource(*name);
        std::string src;
        thread->resourceManager->asString(resource, src);
        auto module = loadModule(context, *name, src.c_str());
        return module;
    }

    static v8::MaybeLocal<v8::Promise> callDynamic(v8::Local<v8::Context> context, v8::Local<v8::ScriptOrModule> referrer, v8::Local<v8::String> specifier) {
        auto isolate = context->GetIsolate();
        v8::TryCatch try_catch(isolate);
        v8::Local<v8::Value> retValue;

        v8::Local<v8::Promise::Resolver> resolver = v8::Promise::Resolver::New(context).ToLocalChecked();
        v8::MaybeLocal<v8::Promise> promise(resolver->GetPromise());
        v8::String::Utf8Value name(isolate, specifier);
        V8Thread *thread = getByIsolate(isolate);
        std::string resource(*name);
        std::string src;
        thread->resourceManager->asString(resource, src);
        v8::MaybeLocal<v8::Module> molule = loadModule(context, *name, src.c_str());
        if (checkModule(context, molule)) {
            v8::Local<v8::Module> localModule;
            if (molule.ToLocal(&localModule)) {
                retValue = runModule(context, localModule, true);
            }
        }
        if (try_catch.HasCaught()) {
            ReportException(isolate, try_catch);
            auto res = resolver->Reject(context, try_catch.Exception());
        } else {
            v8::String::Utf8Value resource(isolate, retValue);
            fprintf(stderr, "callDynamic value: %s\n", *resource);
            auto res = resolver->Resolve(context, retValue);
        }
        return promise;
    }

    static void PromiseRejectCallback(v8::PromiseRejectMessage data) {
        if (data.GetEvent() == v8::kPromiseRejectAfterResolved || data.GetEvent() == v8::kPromiseResolveAfterResolved) {
            // Ignore reject/resolve after resolved.
            return;
        }
        v8::Local<v8::Promise> promise = data.GetPromise();
        v8::Isolate *isolate = promise->GetIsolate();
        if (data.GetEvent() == v8::kPromiseHandlerAddedAfterReject) {
            return;
        }
        v8::Local<v8::Value> exception = data.GetValue();
        v8::Local<v8::Message> message;
        // Assume that all objects are stack-traces.
        if (exception->IsObject()) {
            message = v8::Exception::CreateMessage(isolate, exception);
        }
        if (!exception->IsNativeError() && (message.IsEmpty() || message->GetStackTrace().IsEmpty())) {
            // If there is no real Error object, manually create a stack trace.
            exception = v8::Exception::Error(v8::String::NewFromUtf8Literal(isolate, "Unhandled Promise."));
            message = v8::Exception::CreateMessage(isolate, exception);
        }
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::TryCatch try_catch(isolate);
        v8::Local<v8::Object> globalInstance = context->Global();
        v8::Local<v8::Value> func = globalInstance->Get(context, v8::String::NewFromUtf8Literal(isolate, "onUnhandledRejection", v8::NewStringType::kNormal)).ToLocalChecked();
        if (func.IsEmpty()) {
            return;
        }
        v8::Local<v8::Function> onUnhandledRejection = v8::Local<v8::Function>::Cast(func);
        if (try_catch.HasCaught()) {
            fprintf(stderr, "PromiseRejectCallback: Cast\n");
            ReportException(isolate, try_catch);
            return;
        }
        v8::Local<v8::Value> argv[1] = {exception};
        v8::MaybeLocal<v8::Value> result = onUnhandledRejection->Call(context, globalInstance, 1, argv);
        if (result.IsEmpty() && try_catch.HasCaught()) {
            fprintf(stderr, "PromiseRejectCallback: Call\n");
            ReportException(isolate, try_catch);
        }
    }
};

std::map<void *, util::V8Thread *> util::V8Thread::isolateToThread = {};

} // namespace util
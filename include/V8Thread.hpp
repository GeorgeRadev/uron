
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

#define PUMP_LIMIT 5
#define GLOBAL_JS "__global__.js"

#define DEBUG_MODE

#ifdef DEBUG_MODE
#define DEBUG(args...) fprintf(stderr, args);
#else
#define DEBUG(x)
#endif

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

    const char *arg;
    bool exit;
    std::thread eventLoopThread;
    util::ArrayBlockingQueue<V8Task> eventLoopQueue;

    void eventLoopThreadHandler() {
        exit = true;
        // Creating platform
        std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform(2, v8::platform::IdleTaskSupport::kEnabled);
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
            // Binding dynamic import() callbacks
            isolate->SetHostImportModuleDynamicallyCallback(callDynamic);
            isolate->SetPromiseRejectCallback(PromiseRejectCallback);
            isolate->SetMicrotasksPolicy(v8::MicrotasksPolicy::kExplicit);

            v8::Local<v8::ObjectTemplate> global_ = v8::ObjectTemplate::New(isolate);
            global_->Set(isolate, "include", v8::FunctionTemplate::New(isolate, include));
            {
                // Binding functions
                v8::Local<v8::ObjectTemplate> core = v8::ObjectTemplate::New(isolate);

                core->Set(isolate, "logSTDOUT", v8::FunctionTemplate::New(isolate, logSTDOUT));
                core->Set(isolate, "logSTDERR", v8::FunctionTemplate::New(isolate, logSTDERR));

                core->Set(isolate, "socketWrite", v8::FunctionTemplate::New(isolate, socketWrite));
                core->Set(isolate, "socketClose", v8::FunctionTemplate::New(isolate, socketClose));
                core->Set(isolate, "socketHeader", v8::FunctionTemplate::New(isolate, socketHeader));
                core->Set(isolate, "getBytesLength", v8::FunctionTemplate::New(isolate, getBytesLength));

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

            v8::Local<v8::Function> requestFunction_;

            { // compile and load global js
                v8::TryCatch try_catch(isolate);
                std::string name(GLOBAL_JS);
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

                auto compileResult = v8::Script::Compile(context_, sourceLocal, &origin);
                v8::Local<v8::Script> script;
                if (compileResult.ToLocal(&script)) {
                    v8::Local<v8::Value> result;
                    auto runResult = script->Run(context_);
                    if (runResult.ToLocal(&result)) {
                        if (result->IsFunction()) {
                            requestFunction_ = result.As<v8::Function>();
                            exit = false;
                        } else {
                            fputs("global result is not a function", stderr);
                        }
                    } else {
                        fputs("global result is not defined", stderr);
                    }
                } else {
                    // there should be exception
                }

                ReportException(isolate, try_catch);
            }

            while (!exit) {
                // pump message loop and resolve promises
                for (int count = 0; v8::platform::PumpMessageLoop(platform.get(), isolate) && count < PUMP_LIMIT; count++) {
                    continue;
                }
                isolate->PerformMicrotaskCheckpoint();

                V8Task *task = eventLoopQueue.dequeue_for(std::chrono::milliseconds(1000));
                if (task) {
                    try {
                        // Enter this processor's context so all the remaining operations be executed in it
                        v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate, context_);
                        v8::Context::Scope context_scope(context);
                        v8::Local<v8::Object> global = context->Global();
                        v8::TryCatch try_catch(isolate);
                        v8::Local<v8::Object> requestObject = v8::Object::New(isolate);

                        auto t = global->Set(context, v8::String::NewFromUtf8(isolate, SOCKET_VAR_NAME, v8::NewStringType::kNormal).ToLocalChecked(), v8::Int32::New(isolate, task->socket));
                        t = requestObject->Set(context, v8::String::NewFromUtf8(isolate, "socket", v8::NewStringType::kNormal).ToLocalChecked(), v8::Int32::New(isolate, task->socket));
                        t = requestObject->Set(context, v8::String::NewFromUtf8(isolate, "method", v8::NewStringType::kNormal).ToLocalChecked(), v8::String::NewFromUtf8(isolate, task->method.c_str(), v8::NewStringType::kNormal).ToLocalChecked());
                        t = requestObject->Set(context, v8::String::NewFromUtf8(isolate, "uri", v8::NewStringType::kNormal).ToLocalChecked(), v8::String::NewFromUtf8(isolate, task->uri.c_str(), v8::NewStringType::kNormal).ToLocalChecked());

                        const int argc = 1;
                        v8::Local<v8::Value> argv[argc] = {requestObject};
                        v8::Local<v8::Function> requestFunction = v8::Local<v8::Function>::New(isolate, requestFunction_);
                        v8::MaybeLocal<v8::Value> callResult = requestFunction->Call(context, context->Global(), argc, argv);

                        // log classical try cach error
                        // async ones are served by PromiseRejectCallback
                        if (try_catch.HasCaught()) {
                            v8::String::Utf8Value exception(isolate, try_catch.Exception());
                            v8::Local<v8::Message> message = try_catch.Message();
                            std::string exeptionText = getExceptionString(isolate, exception, message);
                            fputs(exeptionText.c_str(), stderr);
                            serveError(task->socket, exeptionText);
                        }

                    } catch (...) {
                        // nothing to do here
                        fprintf(stderr, "V8Thread Exception\n");
                    }
                    delete task;
                }
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
        auto isolate = args.GetIsolate();
        v8::HandleScope scope(isolate);

        // Enter this processor's context so all the remaining operations be executed in it
        v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate, isolate->GetCurrentContext());
        v8::Context::Scope context_scope(context);
        v8::Local<v8::Object> global = context->Global();

        v8::Local<v8::Promise::Resolver> resolver = v8::Promise::Resolver::New(context).ToLocalChecked();
        args.GetReturnValue().Set(resolver->GetPromise());

        v8::TryCatch try_catch(isolate);

        v8::Local<v8::String> name;
        v8::Local<v8::String> source;
        {
            v8::Local<v8::Value> arg = args[0];
            v8::String::Utf8Value resource(isolate, arg);
            std::string resourceName(*resource);
            std::string resourceSource;
            DEBUG("include: %s\n", resourceName.c_str());

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
                                v8::True(isolate)             // is ES6 module
        );

        v8::ScriptCompiler::Source basescript(source, origin);
        v8::MaybeLocal<v8::Module> maybeModule = v8::ScriptCompiler::CompileModule(isolate, &basescript);

        if (!try_catch.HasCaught()) {
            v8::Local<v8::Module> module;
            if (maybeModule.ToLocal(&module)) {
                v8::Maybe<bool> isModule = module->InstantiateModule(context, callResolve);
                if (isModule.FromMaybe(false)) {
                    v8::Local<v8::Value> result;
                    if (module->Evaluate(context).ToLocal(&result)) {
                        result = module->GetModuleNamespace();
                        args.GetReturnValue().Set(result);
                        return;
                    } else {
                        fprintf(stderr, "Error evaluating module!\n");
                    }
                } else {
                    fprintf(stderr, "Error evaluating module!\n");
                }
            } else {
                fprintf(stderr, "Error evaluating module!\n");
            }
        }

        if (try_catch.HasCaught()) {
            // Reject current scope
            // interestingly try_catch cannot be retrown
            v8::Local<v8::String> errorText = getExceptionText(isolate, try_catch);
            auto res = resolver->Reject(context, errorText.As<v8::Value>());
            return;
        }
    }

    static std::string getExceptionString(v8::Isolate *isolate, v8::String::Utf8Value &exception, v8::Local<v8::Message> &message) {
        std::string exceptionString;
        const char *exception_string = *exception;
        if (message.IsEmpty()) {
            // V8 didn't provide any extra information about this error; just
            // print the exception.
            exceptionString += exception_string;
        } else {
            // generate:
            // (filename):(line number): (message)
            // (source line)
            v8::String::Utf8Value filename(isolate, message->GetScriptOrigin().ResourceName());
            v8::Local<v8::Context> context(isolate->GetCurrentContext());
            const char *filename_string = *filename;
            int linenum = message->GetLineNumber(context).FromJust();
            exceptionString += filename_string;
            exceptionString += ":";
            {
                char buf[100];
                sprintf(buf, "%d", linenum);
                exceptionString += buf;
            }
            exceptionString += ": ";
            exceptionString += exception_string;
            exceptionString += "\n";

            v8::String::Utf8Value sourceline(isolate, message->GetSourceLine(context).ToLocalChecked());
            const char *sourceline_string = *sourceline;
            exceptionString += sourceline_string;
            exceptionString += "\n";

            // Print wavy underline (GetUnderline is deprecated).
            int start = message->GetStartColumn(context).FromJust();
            for (int i = 0; i < start; i++) {
                exceptionString += " ";
            }
            int end = message->GetEndColumn(context).FromJust();
            for (int i = start; i < end; i++) {
                exceptionString += "^";
            }
            exceptionString += "\n";
        }
        return exceptionString;
    }

    static v8::Local<v8::String> getExceptionText(v8::Isolate *isolate, v8::TryCatch &try_catch) {
        if (!try_catch.HasCaught()) {
            return v8::String::NewFromUtf8(isolate, "").ToLocalChecked();
        }
        v8::String::Utf8Value exception(isolate, try_catch.Exception());
        v8::Local<v8::Message> message = try_catch.Message();
        std::string exceptionString = getExceptionString(isolate, exception, message);
        return v8::String::NewFromUtf8(isolate, exceptionString.c_str()).ToLocalChecked();
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
            v8::Local<v8::Message> message = try_catch.Message();
            std::string exceptionString = getExceptionString(isolate, exception, message);
            fputs(exceptionString.c_str(), stderr);
            v8::Local<v8::Context> context(isolate->GetCurrentContext());
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
        DEBUG("import: %s\n", name);

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
            fprintf(stderr, "Error loading module!\n");
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
        { // check for  unhandled rejection
            const auto event = data.GetEvent();
            if (event != v8::kPromiseRejectWithNoHandler) {
                // event == v8::kPromiseRejectAfterResolved
                // event == v8::kPromiseResolveAfterResolved
                // Ignore reject/resolve after resolved.

                // event == v8::kPromiseResolveAfterResolved
                // already rejected
                return;
            }
        }

        v8::Local<v8::Promise> promise = data.GetPromise();
        v8::Isolate *isolate = promise->GetIsolate();
        v8::Local<v8::Value> exception = data.GetValue();
        v8::Local<v8::Message> message;
        // Assume that all objects are stack-traces.

        const int socket = getSocket(isolate);
        if (exception->IsObject()) {
            v8::String::Utf8Value exceptionStr(isolate, exception);
            message = v8::Exception::CreateMessage(isolate, exception);
            std::string error = getExceptionString(isolate, exceptionStr, message);
            fputs(error.c_str(), stderr);
            // response as error
            if (socket > 0) {
                serveError(socket, error);
            }
        } else {
            v8::String::Utf8Value exceptionStr(isolate, exception);
            fputs(*exceptionStr, stderr);
            // response as error
            if (socket > 0) {
                serveError(socket, *exceptionStr);
            }
        }
    }
};

std::map<void *, util::V8Thread *> util::V8Thread::isolateToThread = {};

} // namespace util
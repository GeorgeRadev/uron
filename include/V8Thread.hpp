
#pragma once

#define V8_COMPRESS_POINTERS
#include <libplatform/libplatform.h>
#include <v8.h>

#include <map>
#include <stdlib.h>
#include <string.h>
#include <thread>

#include "ArrayBlockingQueue.hpp"
#include "ResourceManager.hpp"

// https://gist.github.com/surusek/4c05e4dcac6b82d18a1a28e6742fc23e

namespace util {

class V8Task {
  public:
    std::string module;
    std::string method;
    std::string uri;
    int socket;

    V8Task(int _socket, const std::string &_module, const std::string &_method, const std::string &_uri) {
        module = _module;
        method = _method;
        uri = _uri;
        socket = _socket;
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
        // Where is icudtxx.dat? Does nothing if ICU database is in library itself
        // v8::V8::InitializeICUDefaultLocation(arg);
        // Where is snapshot_blob.bin? Does nothing if external data is disabled
        // v8::V8::InitializeExternalStartupData(arg);
        // Creating platform
        std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();
        // Initializing V8 VM
        v8::V8::InitializePlatform(platform.get());
        v8::V8::Initialize();
        // Creating isolate from the params (VM instance)
        v8::Isolate::CreateParams mCreateParams;
        mCreateParams.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
        v8::Isolate *isolate = v8::Isolate::New(mCreateParams);
        // add to map
        isolateToThread[(void *)isolate] = this;

        // any Local should has this one first
        v8::HandleScope globalScope(isolate);

        // Binding print() funtion to the VM; check line #
        v8::Local<v8::ObjectTemplate> global_ = v8::ObjectTemplate::New(isolate);
        global_->Set(isolate, "log", v8::FunctionTemplate::New(isolate, LogCallback));
        global_->Set(isolate, "print", v8::FunctionTemplate::New(isolate, print));
        global_->Set(isolate, "include", v8::FunctionTemplate::New(isolate, include));
        global_->Set(isolate, "require", v8::FunctionTemplate::New(isolate, include));
        global_->Set(isolate, "setResult", v8::FunctionTemplate::New(isolate, setResult));

        // Creating context
        v8::Local<v8::Context> context_ = v8::Context::New(isolate, nullptr, global_);

        // Binding dynamic import() callback
        isolate->SetHostImportModuleDynamicallyCallback(callDynamic);

        while (!exit) {
            // start pulling tasks
            V8Task *task = eventLoopQueue.dequeue_for(std::chrono::milliseconds(50));
            if (task) {
                try {
                    // Enter this processor's context so all the remaining operations
                    v8::Local<v8::Context> context = v8::Local<v8::Context>::New(isolate, context_);
                    v8::Context::Scope context_scope(context);
                    v8::Local<v8::Object> global = context->Global();
                    v8::TryCatch try_catch(isolate);

                    // execute
                    runRequest(isolate, context, try_catch, *task);

                    ReportException(isolate, try_catch);

                } catch (const std::exception &e) {
                    fprintf(stderr, "V8Thread Error: %s", e.what());
                } catch (...) {
                    // nothing to do here
                }
                delete task;
                {
                    char content[] = "OK";
                    char header[1024];
                    sprintf(header, "HTTP/1.1 200 OK\r\nContent-type: text/plain\r\nContent-Length: %ld\r\n\r\n", strlen(content));
                    write(task->socket, header, strlen(header));
                    write(task->socket, content, strlen(content));
                    close(task->socket);
                }
            }
        }

        // clean up
        isolateToThread.erase((void *)isolate);

        // Proper VM deconstructing
        v8::V8::Dispose();
        v8::V8::ShutdownPlatform();
    }

  public:
    V8Thread(const char *_argv0, util::ResourceManager *_resourceManager) : exit(false), resourceManager(_resourceManager), eventLoopQueue(256), eventLoopThread(&V8Thread::eventLoopThreadHandler, this) { arg = _argv0; }

    ~V8Thread() {
        exit = true;
        eventLoopThread.join();
    }

    void enqueueTask(V8Task *task) { eventLoopQueue.enqueue(task); }

  private:
    void static runRequest(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::TryCatch &try_catch, V8Task &task) {
        // Initializing handle scope
        v8::HandleScope scope(isolate);
        V8Thread *thread = getByIsolate(isolate);

        // Reading a module from resources
        std::string moduleSource = "import defaultFunction from '";
        moduleSource += task.module;
        moduleSource += "'; setResult(defaultFunction);";

        v8::Local<v8::Value> result;
        // load init module
        v8::MaybeLocal<v8::Module> module = loadModule(context, task.module.c_str(), moduleSource.c_str(), &try_catch);
        if (checkModule(context, module)) {
            v8::Local<v8::Module> localModule;
            if (module.ToLocal(&localModule)) {
                result = runModule(context, localModule, true);
                // we should have a function here from lastResult
                result = thread->lastResult.Get(isolate);
                if (result->IsFunction()) {
                    v8::Local<v8::Function> function = result.As<v8::Function>();
                    const int argc = 2;
                    v8::Local<v8::String> methodStr = v8::String::NewFromUtf8(isolate, task.method.c_str()).ToLocalChecked();
                    v8::Local<v8::String> uriStr = v8::String::NewFromUtf8(isolate, task.uri.c_str()).ToLocalChecked();
                    v8::Local<v8::Value> argv[argc] = {methodStr, uriStr};
                    if (!function->Call(context, context->Global(), argc, argv).ToLocal(&result)) {
                        ReportException(isolate, try_catch);
                    }
                } else {
                    Log("Error: no default method in module");
                }
            } else {
                ReportException(isolate, try_catch);
            }
        } else {
            Log("Error: cannot instantiate in module");
            ReportException(isolate, try_catch);
        }
    }

    static void LogCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        if (args.Length() < 1) {
            return;
        }
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope scope(isolate);
        v8::Local<v8::Value> arg = args[0];
        v8::String::Utf8Value value(isolate, arg);
        Log(*value);
    }

    static void Log(const char *value) { printf("log: %s\n", value); }

    static void print(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::String::Utf8Value val(isolate, args[0]);
        if (*val == nullptr) {
            isolate->ThrowException(v8::String::NewFromUtf8(isolate, "First argument of function is empty").ToLocalChecked());
        }
        printf("print: %s\n", *val);
    }

    static void include(const v8::FunctionCallbackInfo<v8::Value> &args) {
        if (args.Length() < 1) {
            return;
        }
        v8::Isolate *isolate = args.GetIsolate();
        V8Thread *thread = getByIsolate(isolate);

        v8::Local<v8::Value> arg = args[0];
        v8::Local<v8::String> name;
        v8::Local<v8::String> source;
        {
            v8::String::Utf8Value resource(isolate, arg);
            std::string resourceName(*resource);
            std::string resourceSource;
            thread->resourceManager->asString(resourceName, resourceSource);
            name = v8::String::NewFromUtf8(isolate, resourceName.c_str()).ToLocalChecked();
            source = v8::String::NewFromUtf8(isolate, resourceSource.c_str(), v8::NewStringType::kNormal, static_cast<int>(resourceSource.length())).ToLocalChecked();
        }

        v8::Local<v8::Script> script;
        v8::Local<v8::Value> result;
        v8::Local<v8::Context> context(isolate->GetCurrentContext());

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

        if (v8::Script::Compile(context, source, &origin).ToLocal(&script)) {
            if (script->Run(context).ToLocal(&result)) {
                args.GetReturnValue().Set(result);
            }
        }
    }

    static void setResult(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        V8Thread *thread = getByIsolate(isolate);
        if (args.Length() < 1) {
            thread->lastResult.Reset(isolate, v8::Undefined(isolate));
        } else {
            thread->lastResult.Reset(isolate, args[0]);
        }
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

    static v8::MaybeLocal<v8::Module> loadModule(v8::Local<v8::Context> context, const char *name, const char *code, v8::TryCatch *try_catch) {
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
        if (try_catch != nullptr) {
            ReportException(isolate, *try_catch);
        }
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
        auto module = loadModule(context, *name, src.c_str(), nullptr);
        return module;
    }

    static v8::MaybeLocal<v8::Promise> callDynamic(v8::Local<v8::Context> context, v8::Local<v8::ScriptOrModule> referrer, v8::Local<v8::String> specifier) {
        auto isolate = context->GetIsolate();
        v8::Local<v8::Promise::Resolver> resolver = v8::Promise::Resolver::New(context).ToLocalChecked();
        v8::MaybeLocal<v8::Promise> promise(resolver->GetPromise());
        v8::String::Utf8Value name(isolate, specifier);
        V8Thread *thread = getByIsolate(isolate);
        std::string resource(*name);
        std::string src;
        thread->resourceManager->asString(resource, src);
        v8::MaybeLocal<v8::Module> molule = loadModule(context, *name, src.c_str(), nullptr);
        if (checkModule(context, molule)) {
            v8::Local<v8::Module> localModule;
            if (molule.ToLocal(&localModule)) {
                v8::Local<v8::Value> retValue = runModule(context, localModule, true);
                v8::Maybe<bool> result = resolver->Resolve(context, retValue);
            }
        }
        return promise;
    };
};

std::map<void *, util::V8Thread *> util::V8Thread::isolateToThread = {};

} // namespace util
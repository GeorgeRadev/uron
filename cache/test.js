const { moduleFunc, moduleAsyncFunc } = include("testModule.js");

const incFunc = include("testInclude.js");

async function verify() {
    incFunc();
    moduleFunc();
    await moduleAsyncFunc();
}

await verify()

async function requestHandler(request, response) {
    log({ method: request.getMethod(), uri: request.getURI() });
    response.setStatus(200)
    response.setContentType("text/html")
    response.send("OK");
}

return requestHandler;
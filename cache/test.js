import { moduleFunc, moduleAsyncFunc , modulePromiseFunc} from "testModule.js";

const { incFunc } = include("testInclude.js");

async function verify() {
    incFunc();
    moduleFunc();
    await moduleAsyncFunc();
    logError({ log: "after moduleAsyncFunc" });
    await modulePromiseFunc();
    logError({ log: "after modulePromiseFunc" });
}

export default async function requestHandler(request, response) {
    logError({ log: "From test: ", method: request.getMethod(), uri: request.getURI() });
    await verify();
    response.setStatus(200);
    response.setContentType("text/html");
    response.send("OK");
    logError({ log: "From test: end!" });
}
import { moduleFunc, moduleAsyncFunc } from "testModule.js";

const { incFunc } = include("testInclude.js");

async function verify() {
    incFunc();
    moduleFunc();
    await moduleAsyncFunc();
    logError({ log: "after moduleAsyncFunc" });
}

export default async function requestHandler(request, response) {
    await verify();
    logError({ log: "From test: ", method: request.getMethod(), uri: request.getURI() });
    response.setStatus(200);
    response.setContentType("text/html");
    response.send("OK");
    logError({ log: "From test: end!" });
}
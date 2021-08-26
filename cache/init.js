import { moduleFunc, moduleAsyncFunc } from "module.js"

const includeFunc = include("include.js")

async function verify() {
    includeFunc();
    moduleFunc();
    await moduleAsyncFunc();
}

await verify()

function requestHandler(method, uri) {
    print("request method: " + method + " uri: " + uri);
}

export default requestHandler;
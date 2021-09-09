export default async function requestHandler(method, uri) {
    log("request async method: " + method + " uri: " + uri);
    log("eval = " + eval("23"));
}
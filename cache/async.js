export default async function requestHandler(method, uri) {
    print("request async method: " + method + " uri: " + uri);
    print("eval = " + eval("23"));
}
export default async function requestHandler(request, response) {
    response.setStatus(200);
    response.setContentType("text/html");
    var result = "request async<br> method: <b>" + request.getMethod() + "</b><br> uri: <b>" + request.getURI() + "</b><br>";
    result += "query: <b>" + JSON.stringify(request.getQuery()) + "</b><br>";
    result += "header: <b>" + JSON.stringify(request.getHeader()) + "</b><br>";
    response.send(result);
}
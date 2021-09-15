export default async function requestHandler(request, response) {
    response.setStatus(200);
    response.setContentType("text/html");
    const result = "request async<br> method: <b>" + request.getMethod() + "</b><br> uri: <b>" + request.getURI() + "</b>";
    response.send(result);
}
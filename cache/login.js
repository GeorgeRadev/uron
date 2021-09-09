function requestHandler(request, response) {
    var sql = SELECT user WHERE user = 'test';
    const resultObj = { method: request.getMethod(), uri: request.getURI(), sql: sql };
    
    const result = JSON.stringify(resultObj);
    log(result);
    response.setStatus(200);
    response.setContentType('application/json');
    response.send(result);
};

return requestHandler;
export default async function requestHandler(request, response) {
    const username = "username"
    const sql = SELECT user WHERE user = :username AND role = 'test';
    
    response.setStatus(200);
    response.setContentType('text/plain');
    response.send(JSON.stringify(sql));
}
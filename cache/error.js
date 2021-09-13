export default async function requestHandler(request, response) {
    throw Error("This is intentional error from " + request.getURI());
};
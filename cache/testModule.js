// used for multiple exporte functionality
function moduleFunc() {
    log("from inside module");
    return 42;
}

async function moduleAsyncFunc() {
    log("from inside async module");
    return 73;
}

return  { moduleFunc: moduleFunc, moduleAsyncFunc: moduleAsyncFunc };
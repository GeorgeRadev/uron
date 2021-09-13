// used for multiple exporte functionality
export function moduleFunc() {
    log("from inside module");
    return 42;
}

export async function moduleAsyncFunc() {
    log("from inside async module 1");
    return new Promise(function (resolve, reject) {
        log("from inside async module 2");
        resolve(73);
    });

    //log("from inside async module");
    //return 73;
}

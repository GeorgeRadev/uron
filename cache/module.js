export function moduleFunc() {
    print("from inside module");
    return 42;
}

export async function moduleAsyncFunc() {
    print("from inside async module");
    return 73;
}
export function log() {
    for (const e in arguments) {
        const value = arguments[e];
        if (value) {
            core.logSTDOUT(JSON.stringify(value));
        }
    }
}

export function logError() {
    for (const e in arguments) {
        const value = arguments[e];
        if (value) {
            core.logSTDERR(JSON.stringify(value));
        }
    }
}

// Fly is often started from a terminal that is closed later, or with its output piped
// to a reader that exits. Writes then fail with EIO or EPIPE. Node reports the first
// failure through the write callback but raises the next one as an uncaught exception,
// which Electron shows as a blocking error dialog. Lost console output is harmless.
function ignoreClosedOutput(streams = [process.stdout, process.stderr]) {
    for (const stream of streams) stream.on('error', () => {});
}

module.exports = {ignoreClosedOutput};

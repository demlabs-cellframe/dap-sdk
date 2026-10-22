"""Exercise production HTTP line framing with exact-sized inputs under sanitizers."""
import pathlib
import subprocess
import tempfile

sdk = pathlib.Path(__file__).resolve().parents[4]
code = r'''
#include <assert.h>
#include <stdlib.h>
#include "dap_http_line.h"
static int check(const char *s, size_t n, size_t limit, size_t *used) {
    char *p = malloc(n ? n : 1);
    memcpy(p, s, n);
    int rc = dap_http_line_read(p, n, limit, used);
    free(p);
    return rc;
}
int main(void) {
    const char *lines[] = {"GET / HTTP/1.1\r\n", "Host: example.org\r\n", "\r\n"};
    size_t used;
    for (size_t j = 0; j < sizeof(lines)/sizeof(*lines); ++j) {
        size_t n = strlen(lines[j]);
        for (size_t i = 0; i < n; ++i) {
            used = 999;
            assert(check(lines[j], i, n, &used) == 0 && used == 0);
        }
        assert(check(lines[j], n, n, &used) == 1 && used == n);
        assert(check(lines[j], n, n - 1, &used) == -1 && used == 0);
    }
    const char *bad[] = {"\n", "x\n", "x\ry", "x\r\r\n"};
    for (size_t i = 0; i < sizeof(bad)/sizeof(*bad); ++i)
        assert(check(bad[i], strlen(bad[i]), 4096, &used) == -1);
    assert(check("a\0b\r\n", 5, 4096, &used) == -1);
    assert(check("abcd", 4, 4, &used) == -1);
    /* Coalesced body/next line may contain arbitrary bytes, outside this line. */
    assert(check("x\r\n\0\n", 5, 3, &used) == 1 && used == 3);
    assert(dap_http_line_read(NULL, 0, 4096, &used) == 0);
    assert(dap_http_line_read(NULL, 1, 4096, &used) == -1);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    source = pathlib.Path(tmp) / "test.c"
    exe = pathlib.Path(tmp) / "test"
    source.write_text(code)
    subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-I", str(sdk / "net/server/http_server/http_client/include"),
                    str(source), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: production HTTP line framing, split boundaries and invalid input")

#ifdef _WIN32

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_file(const char *path, const char *text)
{
    if (!path || !*path) return;

    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(text, 1, strlen(text), f);
    fclose(f);
}

static void run_cmd_echo(const char *marker_path)
{
    if (!marker_path || !*marker_path) return;

    char cmdline[2048];
    snprintf(cmdline, sizeof(cmdline),
             "cmd.exe /d /c echo MYTERM_SMOKE_OK > \"%s\"", marker_path);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);

    if (CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

int main(void)
{
    const char *direct_marker = getenv("MYTERM_SMOKE_MARKER");
    const char *cmd_marker = getenv("MYTERM_SMOKE_CMD_MARKER");

    if (direct_marker && *direct_marker) {
        write_file(direct_marker, "MYTERM_SMOKE_READY\r\n");
    }

    if (cmd_marker && *cmd_marker) {
        run_cmd_echo(cmd_marker);
    }

    printf("MYTERM_SMOKE_HELPER_READY\n");
    fflush(stdout);

    for (;;) {
        Sleep(1000);
    }

    return 0;
}

#endif

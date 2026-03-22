/*
 * pty_windows.c — Windows ConPTY pseudo-terminal backend.
 *
 * Uses the Windows Pseudo Console API (ConPTY), available since
 * Windows 10 1809 and fully supported on Windows 11.
 *
 * ConPTY creates a pseudo-terminal that bridges a console application
 * (like cmd.exe or powershell.exe) with our terminal emulator.
 */

#ifdef _WIN32

#include "myterm.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <shellapi.h>

/* ConPTY API — available in Windows SDK 10.0.17763.0+ */
#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

/* Some MinGW/Windows SDK combinations do not expose ConPTY declarations. */
#ifndef HPCON
DECLARE_HANDLE(HPCON);
#endif

#ifndef WINAPI_PARTITION_DESKTOP
#define WINAPI_PARTITION_DESKTOP 0x00000001
#endif

#ifndef _PSEUDOCONSOLE_INHERIT_CURSOR
#define _PSEUDOCONSOLE_INHERIT_CURSOR 0x00000001
#endif

#ifndef HRESULT_FROM_WIN32
#define HRESULT_FROM_WIN32(x) ((HRESULT)((x) <= 0 ? ((x)) : (((x) & 0x0000FFFF) | (7 << 16) | 0x80000000)))
#endif

#ifndef S_OK
#define S_OK ((HRESULT)0L)
#endif

#ifndef SUCCEEDED
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#endif

#ifndef FAILED
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#endif

#ifndef __MINGW_NAME_AW
#define __MINGW_NAME_AW(func) func##W
#endif

#ifndef CreatePseudoConsole
HRESULT WINAPI CreatePseudoConsole(COORD size, HANDLE hInput, HANDLE hOutput,
                                   DWORD dwFlags, HPCON *phPC);
#endif

#ifndef ResizePseudoConsole
HRESULT WINAPI ResizePseudoConsole(HPCON hPC, COORD size);
#endif

#ifndef ClosePseudoConsole
void WINAPI ClosePseudoConsole(HPCON hPC);
#endif

struct MtPty {
    HPCON               hpc;            /* Pseudo console handle */
    HANDLE              pipe_in_read;   /* PTY reads from child via this */
    HANDLE              pipe_in_write;  /* Child writes to this */
    HANDLE              pipe_out_read;  /* Child reads from this */
    HANDLE              pipe_out_write; /* We write user input to this */
    HANDLE              child_process;
    HANDLE              child_thread;
    STARTUPINFOEXW      si;
    PROCESS_INFORMATION pi;
    bool                alive;
};

static void close_handle_safe(HANDLE *h)
{
    if (h && *h && *h != INVALID_HANDLE_VALUE) {
        CloseHandle(*h);
        *h = NULL;
    }
}

static void close_pseudoconsole_safe(HPCON *hpc)
{
    if (hpc && *hpc) {
        ClosePseudoConsole(*hpc);
        *hpc = NULL;
    }
}

static void free_attribute_list_safe(STARTUPINFOEXW *si)
{
    if (si && si->lpAttributeList) {
        DeleteProcThreadAttributeList(si->lpAttributeList);
        free(si->lpAttributeList);
        si->lpAttributeList = NULL;
    }
}

static void cleanup_failed_pty(MtPty *pty)
{
    if (!pty) return;
    free_attribute_list_safe(&pty->si);
    close_pseudoconsole_safe(&pty->hpc);
    close_handle_safe(&pty->pipe_in_read);
    close_handle_safe(&pty->pipe_in_write);
    close_handle_safe(&pty->pipe_out_read);
    close_handle_safe(&pty->pipe_out_write);
    close_handle_safe(&pty->child_process);
    close_handle_safe(&pty->child_thread);
    free(pty);
}

/* Helper: create a pipe pair */
static bool create_pipe_pair(HANDLE *read_end, HANDLE *write_end)
{
    SECURITY_ATTRIBUTES sa;
    ZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;
    return CreatePipe(read_end, write_end, &sa, 0) != 0;
}

static bool utf8_to_utf16(const char *src, wchar_t *dst, size_t dst_len)
{
    if (!src || !*src || !dst || dst_len == 0) return false;

    if (MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, (int)dst_len) > 0) {
        return true;
    }

    return MultiByteToWideChar(CP_ACP, 0, src, -1, dst, (int)dst_len) > 0;
}

static bool spawn_command_line(MtPty *pty, const wchar_t *command_line)
{
    wchar_t cmd[1024];
    wchar_t exe_path[MAX_PATH];
    wchar_t *argv0 = NULL;
    wchar_t **argv = NULL;
    int argc = 0;
    bool success = false;

    if (!pty || !command_line || !*command_line) return false;

    ZeroMemory(cmd, sizeof(cmd));
    wcsncpy(cmd, command_line, (sizeof(cmd) / sizeof(cmd[0])) - 1);

    argv = CommandLineToArgvW(cmd, &argc);
    if (!argv || argc <= 0 || !argv[0] || !*argv[0]) {
        if (argv) LocalFree(argv);
        return false;
    }
    argv0 = argv[0];

    ZeroMemory(exe_path, sizeof(exe_path));
    DWORD search_result = SearchPathW(NULL, argv0, NULL,
                                      (DWORD)(sizeof(exe_path) / sizeof(exe_path[0])),
                                      exe_path, NULL);
    if (search_result == 0 || search_result >= (DWORD)(sizeof(exe_path) / sizeof(exe_path[0]))) {
        wcsncpy(exe_path, argv0, (sizeof(exe_path) / sizeof(exe_path[0])) - 1);
    }

    ZeroMemory(&pty->pi, sizeof(pty->pi));
    success = CreateProcessW(
        exe_path,
        cmd,
        NULL, NULL,
        FALSE,
        EXTENDED_STARTUPINFO_PRESENT,
        NULL, NULL,
        &pty->si.StartupInfo,
        &pty->pi) != 0;

    LocalFree(argv);
    return success;
}

static bool spawn_shell(MtPty *pty)
{
    wchar_t cmd[1024];
    const char *configured_shell = getenv("MYTERM_SHELL");

    if (configured_shell && *configured_shell) {
        ZeroMemory(cmd, sizeof(cmd));
        if (utf8_to_utf16(configured_shell, cmd, sizeof(cmd) / sizeof(cmd[0]))) {
            if (spawn_command_line(pty, cmd)) {
                fprintf(stderr, "myterm: spawned configured shell: %s\n", configured_shell);
                return true;
            }
            fprintf(stderr, "myterm: configured shell failed (%lu): %s\n",
                    (unsigned long)GetLastError(), configured_shell);
        } else {
            fprintf(stderr, "myterm: failed to decode configured shell: %s\n", configured_shell);
        }
    }

    const wchar_t *shells[] = {
        L"pwsh.exe -NoLogo -NoProfile",
        L"powershell.exe -NoLogo -NoProfile",
        L"cmd.exe",
        NULL
    };

    for (int i = 0; shells[i] != NULL; i++) {
        if (spawn_command_line(pty, shells[i])) {
            fwprintf(stderr, L"myterm: spawned fallback shell: %ls\n", shells[i]);
            return true;
        }
    }

    return false;
}

MtPty *mt_pty_new(int cols, int rows)
{
    MtPty *pty = calloc(1, sizeof(MtPty));
    if (!pty) return NULL;

    /* Create two pipe pairs for ConPTY I/O */
    if (!create_pipe_pair(&pty->pipe_in_read, &pty->pipe_in_write) ||
        !create_pipe_pair(&pty->pipe_out_read, &pty->pipe_out_write)) {
        fprintf(stderr, "myterm: failed to create pipes\n");
        cleanup_failed_pty(pty);
        return NULL;
    }

    /* Create the pseudo console */
    COORD size;
    size.X = (SHORT)cols;
    size.Y = (SHORT)rows;
    HRESULT hr = CreatePseudoConsole(
        size,
        pty->pipe_out_read,   /* ConPTY reads input from here */
        pty->pipe_in_write,   /* ConPTY writes output here */
        0,                    /* flags */
        &pty->hpc
    );

    if (FAILED(hr)) {
        fprintf(stderr, "myterm: CreatePseudoConsole failed: 0x%08lx\n", (unsigned long)hr);
        cleanup_failed_pty(pty);
        return NULL;
    }

    SetHandleInformation(pty->pipe_in_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(pty->pipe_out_write, HANDLE_FLAG_INHERIT, 0);

    /* The pseudo console owns these ends now; keep only the app-facing ends. */
    close_handle_safe(&pty->pipe_out_read);
    close_handle_safe(&pty->pipe_in_write);

    /* Prepare STARTUPINFOEX with the pseudo console attribute */
    ZeroMemory(&pty->si, sizeof(pty->si));
    pty->si.StartupInfo.cb = sizeof(STARTUPINFOEXW);

    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
    pty->si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(attr_size);
    if (!pty->si.lpAttributeList) {
        cleanup_failed_pty(pty);
        return NULL;
    }

    if (!InitializeProcThreadAttributeList(pty->si.lpAttributeList, 1, 0, &attr_size)) {
        fprintf(stderr, "myterm: InitializeProcThreadAttributeList failed\n");
        goto cleanup;
    }

    if (!UpdateProcThreadAttribute(
            pty->si.lpAttributeList,
            0,
            PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
            pty->hpc,
            sizeof(pty->hpc),
            NULL, NULL)) {
        fprintf(stderr, "myterm: UpdateProcThreadAttribute failed\n");
        goto cleanup;
    }

    if (!spawn_shell(pty)) {
        fprintf(stderr, "myterm: failed to spawn shell process\n");
        goto cleanup;
    }

    pty->child_process = pty->pi.hProcess;
    pty->child_thread  = pty->pi.hThread;
    pty->alive = true;

    return pty;

cleanup:
    cleanup_failed_pty(pty);
    return NULL;
}

void mt_pty_destroy(MtPty *pty)
{
    if (!pty) return;

    pty->alive = false;

    /* Close the pseudo console first — this signals the child */
    close_handle_safe(&pty->pipe_out_write);
    close_pseudoconsole_safe(&pty->hpc);

    /* Wait briefly for child to exit */
    if (pty->child_process) {
        DWORD wait_result = WaitForSingleObject(pty->child_process, 1000);
        if (wait_result == WAIT_TIMEOUT) {
            TerminateProcess(pty->child_process, 0);
            WaitForSingleObject(pty->child_process, 1000);
        }
    }

    close_handle_safe(&pty->child_thread);
    close_handle_safe(&pty->child_process);
    close_handle_safe(&pty->pipe_in_read);
    close_handle_safe(&pty->pipe_in_write);
    close_handle_safe(&pty->pipe_out_read);
    close_handle_safe(&pty->pipe_out_write);
    free_attribute_list_safe(&pty->si);

    free(pty);
}

int mt_pty_read(MtPty *pty, char *buf, size_t len)
{
    if (!pty || !pty->alive) return -1;

    /* Non-blocking: peek first to check if data is available */
    DWORD available = 0;
    if (!PeekNamedPipe(pty->pipe_in_read, NULL, 0, NULL, &available, NULL)) {
        pty->alive = false;
        return -1;
    }

    if (available == 0) return 0;

    /* Read available data (up to buf size) */
    DWORD to_read = (DWORD)((len < (size_t)available) ? len : (size_t)available);

    DWORD bytes_read = 0;
    if (!ReadFile(pty->pipe_in_read, buf, to_read, &bytes_read, NULL)) {
        pty->alive = false;
        return -1;
    }

    return (int)bytes_read;
}

int mt_pty_write(MtPty *pty, const char *buf, size_t len)
{
    if (!pty || !pty->alive || len == 0) return 0;

    DWORD written = 0;
    if (!WriteFile(pty->pipe_out_write, buf, (DWORD)len, &written, NULL)) {
        pty->alive = false;
        return -1;
    }
    return (int)written;
}

void mt_pty_resize(MtPty *pty, int cols, int rows)
{
    if (!pty || !pty->alive) return;
    COORD size;
    size.X = (SHORT)cols;
    size.Y = (SHORT)rows;
    ResizePseudoConsole(pty->hpc, size);
}

bool mt_pty_is_alive(MtPty *pty)
{
    if (!pty || !pty->alive) return false;

    /* Check if child process has exited */
    DWORD exit_code = 0;
    if (pty->child_process && GetExitCodeProcess(pty->child_process, &exit_code)) {
        if (exit_code != STILL_ACTIVE) {
            pty->alive = false;
            return false;
        }
    }
    return true;
}

#endif /* _WIN32 */

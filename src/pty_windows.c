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

/* ConPTY API — available in Windows SDK 10.0.17763.0+ */
#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
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

/* Helper: create a pipe pair */
static bool create_pipe_pair(HANDLE *read_end, HANDLE *write_end)
{
    SECURITY_ATTRIBUTES sa = {
        .nLength = sizeof(SECURITY_ATTRIBUTES),
        .bInheritHandle = FALSE,
        .lpSecurityDescriptor = NULL,
    };
    return CreatePipe(read_end, write_end, &sa, 0) != 0;
}

MtPty *mt_pty_new(int cols, int rows)
{
    MtPty *pty = calloc(1, sizeof(MtPty));
    if (!pty) return NULL;

    /* Create two pipe pairs for ConPTY I/O */
    if (!create_pipe_pair(&pty->pipe_in_read, &pty->pipe_in_write) ||
        !create_pipe_pair(&pty->pipe_out_read, &pty->pipe_out_write)) {
        fprintf(stderr, "myterm: failed to create pipes\n");
        free(pty);
        return NULL;
    }

    /* Create the pseudo console */
    COORD size = { .X = (SHORT)cols, .Y = (SHORT)rows };
    HRESULT hr = CreatePseudoConsole(
        size,
        pty->pipe_out_read,   /* ConPTY reads input from here */
        pty->pipe_in_write,   /* ConPTY writes output here */
        0,                    /* flags */
        &pty->hpc
    );

    if (FAILED(hr)) {
        fprintf(stderr, "myterm: CreatePseudoConsole failed: 0x%lx\n", hr);
        CloseHandle(pty->pipe_in_read);
        CloseHandle(pty->pipe_in_write);
        CloseHandle(pty->pipe_out_read);
        CloseHandle(pty->pipe_out_write);
        free(pty);
        return NULL;
    }

    /* Prepare STARTUPINFOEX with the pseudo console attribute */
    ZeroMemory(&pty->si, sizeof(pty->si));
    pty->si.StartupInfo.cb = sizeof(STARTUPINFOEXW);

    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
    pty->si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(attr_size);
    if (!pty->si.lpAttributeList) {
        ClosePseudoConsole(pty->hpc);
        CloseHandle(pty->pipe_in_read);
        CloseHandle(pty->pipe_in_write);
        CloseHandle(pty->pipe_out_read);
        CloseHandle(pty->pipe_out_write);
        free(pty);
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
            sizeof(HPCON),
            NULL, NULL)) {
        fprintf(stderr, "myterm: UpdateProcThreadAttribute failed\n");
        goto cleanup;
    }

    /* Spawn the shell process.
     * Try PowerShell 7 first, then Windows PowerShell, then cmd.exe */
    const wchar_t *shells[] = {
        L"pwsh.exe",
        L"powershell.exe",
        L"cmd.exe",
        NULL
    };

    bool spawned = false;
    for (int i = 0; shells[i] != NULL; i++) {
        /* Use a mutable copy since CreateProcessW may modify it */
        wchar_t cmd[MAX_PATH];
        wcscpy_s(cmd, MAX_PATH, shells[i]);

        ZeroMemory(&pty->pi, sizeof(pty->pi));
        if (CreateProcessW(
                NULL,
                cmd,
                NULL, NULL,
                FALSE,
                EXTENDED_STARTUPINFO_PRESENT,
                NULL, NULL,
                &pty->si.StartupInfo,
                &pty->pi)) {
            spawned = true;
            break;
        }
    }

    if (!spawned) {
        fprintf(stderr, "myterm: failed to spawn shell process\n");
        goto cleanup;
    }

    pty->child_process = pty->pi.hProcess;
    pty->child_thread  = pty->pi.hThread;
    pty->alive = true;

    /* Set the read pipe to non-blocking via overlapped I/O mode.
     * We use PeekNamedPipe for non-blocking reads instead. */

    return pty;

cleanup:
    if (pty->si.lpAttributeList) {
        DeleteProcThreadAttributeList(pty->si.lpAttributeList);
        free(pty->si.lpAttributeList);
    }
    ClosePseudoConsole(pty->hpc);
    CloseHandle(pty->pipe_in_read);
    CloseHandle(pty->pipe_in_write);
    CloseHandle(pty->pipe_out_read);
    CloseHandle(pty->pipe_out_write);
    free(pty);
    return NULL;
}

void mt_pty_destroy(MtPty *pty)
{
    if (!pty) return;

    /* Close the pseudo console first — this signals the child */
    ClosePseudoConsole(pty->hpc);

    /* Wait briefly for child to exit */
    if (pty->child_process) {
        WaitForSingleObject(pty->child_process, 1000);
        CloseHandle(pty->child_process);
    }
    if (pty->child_thread) {
        CloseHandle(pty->child_thread);
    }

    CloseHandle(pty->pipe_in_read);
    CloseHandle(pty->pipe_in_write);
    CloseHandle(pty->pipe_out_read);
    CloseHandle(pty->pipe_out_write);

    if (pty->si.lpAttributeList) {
        DeleteProcThreadAttributeList(pty->si.lpAttributeList);
        free(pty->si.lpAttributeList);
    }

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
    DWORD to_read = (DWORD)len;
    if (available < to_read) to_read = available;

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
        return -1;
    }
    return (int)written;
}

void mt_pty_resize(MtPty *pty, int cols, int rows)
{
    if (!pty || !pty->alive) return;
    COORD size = { .X = (SHORT)cols, .Y = (SHORT)rows };
    ResizePseudoConsole(pty->hpc, size);
}

bool mt_pty_is_alive(MtPty *pty)
{
    if (!pty) return false;
    if (!pty->alive) return false;

    /* Check if child process has exited */
    DWORD exit_code = 0;
    if (GetExitCodeProcess(pty->child_process, &exit_code)) {
        if (exit_code != STILL_ACTIVE) {
            pty->alive = false;
            return false;
        }
    }
    return true;
}

#endif /* _WIN32 */

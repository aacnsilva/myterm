/*
 * pty_unix.c — Unix PTY backend (for development/testing on Linux/macOS).
 *
 * Uses forkpty() to create a pseudo-terminal and spawn a shell.
 * This allows developing and testing myterm on non-Windows platforms.
 */

#ifndef _WIN32

#include "myterm.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif

struct MtPty {
    int   master_fd;
    pid_t child_pid;
    bool  alive;
};

MtPty *mt_pty_new(int cols, int rows)
{
    MtPty *pty = calloc(1, sizeof(MtPty));
    if (!pty) return NULL;

    struct winsize ws = {
        .ws_col = (unsigned short)cols,
        .ws_row = (unsigned short)rows,
    };

    pid_t pid = forkpty(&pty->master_fd, NULL, NULL, &ws);
    if (pid < 0) {
        perror("forkpty");
        free(pty);
        return NULL;
    }

    if (pid == 0) {
        /* Child: exec the user's shell */
        const char *shell = getenv("SHELL");
        if (!shell) shell = "/bin/sh";

        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);

        execlp(shell, shell, "-l", (char *)NULL);
        perror("execlp");
        _exit(1);
    }

    /* Parent */
    pty->child_pid = pid;
    pty->alive = true;

    /* Set master fd to non-blocking */
    int flags = fcntl(pty->master_fd, F_GETFL, 0);
    fcntl(pty->master_fd, F_SETFL, flags | O_NONBLOCK);

    return pty;
}

void mt_pty_destroy(MtPty *pty)
{
    if (!pty) return;

    if (pty->master_fd >= 0) {
        close(pty->master_fd);
    }

    if (pty->child_pid > 0) {
        kill(pty->child_pid, SIGHUP);
        waitpid(pty->child_pid, NULL, WNOHANG);
    }

    free(pty);
}

int mt_pty_read(MtPty *pty, char *buf, size_t len)
{
    if (!pty || !pty->alive) return -1;

    ssize_t n = read(pty->master_fd, buf, len);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0; /* nothing available */
        }
        pty->alive = false;
        return -1;
    }
    if (n == 0) {
        pty->alive = false;
        return -1;
    }
    return (int)n;
}

int mt_pty_write(MtPty *pty, const char *buf, size_t len)
{
    if (!pty || !pty->alive || len == 0) return 0;

    ssize_t n = write(pty->master_fd, buf, len);
    if (n < 0) return -1;
    return (int)n;
}

void mt_pty_resize(MtPty *pty, int cols, int rows)
{
    if (!pty || !pty->alive) return;

    struct winsize ws = {
        .ws_col = (unsigned short)cols,
        .ws_row = (unsigned short)rows,
    };
    ioctl(pty->master_fd, TIOCSWINSZ, &ws);
}

bool mt_pty_is_alive(MtPty *pty)
{
    if (!pty || !pty->alive) return false;

    int status;
    pid_t result = waitpid(pty->child_pid, &status, WNOHANG);
    if (result > 0) {
        pty->alive = false;
        return false;
    }
    return true;
}

#endif /* !_WIN32 */

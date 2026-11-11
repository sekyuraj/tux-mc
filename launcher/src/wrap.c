/* tux-mc-wrap: Prism's wrapper command for fast starts (Linux).
 *
 *   tux-mc-wrap <capture file> <command...>
 *
 * Prism starts the game as  <wrapper> java ... org.prismlauncher.EntryPoint
 * and then writes the launch script (classpath, main class, game arguments)
 * to the game's stdin, ending with a "launch" line. This reads that script,
 * saves the command, the INST_* variables and the script (with the access
 * token taken out) to the capture file, then runs the command with the same
 * script on its stdin. Next time the launcher can start the game the same way
 * by itself, without Prism and its sign-in round trips (see mc-fast.c).
 *
 * Whatever goes wrong with the capture, the game still starts.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static char *buf;
static size_t len, cap;

static void add(const char *p, size_t n)
{
    if (len + n + 1 > cap) {
        cap = (len + n + 1) * 2;
        char *nb = realloc(buf, cap);
        if (!nb)
            _exit(127);
        buf = nb;
    }
    memcpy(buf + len, p, n);
    len += n;
    buf[len] = 0;
}

/* the script is done at a line "launch" or "abort" */
static int script_done(void)
{
    for (char *l = buf; l && *l;) {
        char *e = strchr(l, '\n');
        if (!e)
            return 0;
        size_t n = (size_t)(e - l);
        if ((n == 6 && !memcmp(l, "launch", 6)) || (n == 5 && !memcmp(l, "abort", 5)))
            return 1;
        l = e + 1;
    }
    return 0;
}

static int write_all(int fd, const char *p, size_t n)
{
    while (n) {
        ssize_t w = write(fd, p, n);
        if (w < 0 && errno == EINTR)
            continue;
        if (w <= 0)
            return -1;
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

/* the access token: the value after "param --accessToken" (NULL if none) */
static char *find_token(void)
{
    const char *k = "\nparam --accessToken\nparam ";
    char *at = strstr(buf, k);
    if (!at)
        return NULL;
    at += strlen(k);
    char *e = strchr(at, '\n');
    return e && e - at >= 16 ? strndup(at, (size_t)(e - at)) : NULL;
}

/* capture file: "TUXMC-FAST 1", cwd, env lines, arg lines, then "script" and the
 * script. Every copy of the access token (it's also inside sessionId) becomes
 * @TUXMC_TOKEN@; without a token we keep nothing. */
static void save(const char *path, int argc, char **argv)
{
    char *tok = find_token();
    if (!tok)
        return;
    char tmp[4096];
    if (snprintf(tmp, sizeof tmp, "%s.part", path) >= (int)sizeof tmp)
        goto out;
    char *slash = strrchr(tmp, '/');
    if (slash) {
        *slash = 0;
        mkdir(tmp, 0700);
        *slash = '/';
    }
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0)
        goto out;
    FILE *f = fdopen(fd, "w");
    if (!f) {
        close(fd);
        goto out;
    }
    char cwd[4096];
    fprintf(f, "TUXMC-FAST 1\ncwd %s\n", getcwd(cwd, sizeof cwd) ? cwd : ".");
    for (char **e = environ; *e; e++)
        if (!strncmp(*e, "INST_", 5) && !strchr(*e, '\n'))
            fprintf(f, "env %s\n", *e);
    for (int i = 0; i < argc; i++)
        fprintf(f, "arg %s\n", argv[i]);
    fputs("script\n", f);
    size_t tl = strlen(tok);
    for (const char *s = buf, *hit; *s; s = hit + tl) {
        hit = strstr(s, tok);
        if (!hit) {
            fputs(s, f);
            break;
        }
        fwrite(s, 1, (size_t)(hit - s), f);
        fputs("@TUXMC_TOKEN@", f);
    }
    int bad = ferror(f) | fclose(f);
    if (bad || rename(tmp, path) != 0)
        unlink(tmp);
out:
    free(tok);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: tux-mc-wrap <capture file> <command...>\n");
        return 127;
    }
    char rd[65536];
    while (!script_done()) {
        ssize_t n = read(0, rd, sizeof rd);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        add(rd, (size_t)n);
    }
    if (len && script_done() && !strstr(buf, "\nabort\n"))
        save(argv[1], argc - 2, argv + 2);

    /* the game gets the script on a new pipe; a grandchild writes it (pipes
     * hold 64 KiB, a script with a long classpath can be bigger) */
    int p[2];
    if (len && pipe2(p, O_CLOEXEC) == 0) {
        pid_t w = fork();
        if (w == 0) {
            if (fork() == 0) {
                close(p[0]);
                write_all(p[1], buf, len);
                _exit(0);
            }
            _exit(0);
        }
        if (w > 0)
            waitpid(w, NULL, 0);
        close(p[1]);
        dup2(p[0], 0);                     /* dup2 drops O_CLOEXEC on fd 0 */
        close(p[0]);
    }
    execvp(argv[2], argv + 2);
    fprintf(stderr, "tux-mc-wrap: %s: %s\n", argv[2], strerror(errno));
    return 127;
}

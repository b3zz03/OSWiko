/* OSWiko - osh, la shell di debug sulla console seriale */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/klog.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_ARGS 16

static int cmd_help(int argc, char **argv);

static int cmd_echo(int argc, char **argv)
{
	for (int i = 1; i < argc; i++)
		printf("%s%s", argv[i], i + 1 < argc ? " " : "");
	printf("\n");
	return 0;
}

static int cmd_ls(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : ".";
	DIR *d = opendir(dir);
	struct dirent *de;

	if (!d) {
		perror(dir);
		return 1;
	}
	while ((de = readdir(d))) {
		if (de->d_name[0] == '.')
			continue;
		char path[512];
		struct stat st;
		snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
		if (lstat(path, &st) == 0 && S_ISDIR(st.st_mode))
			printf("\033[1;34m%s/\033[0m\n", de->d_name);
		else if (lstat(path, &st) == 0)
			printf("%-28s %8lld\n", de->d_name, (long long)st.st_size);
	}
	closedir(d);
	return 0;
}

static int cmd_cat(int argc, char **argv)
{
	char buf[4096];
	for (int i = 1; i < argc; i++) {
		int fd = open(argv[i], O_RDONLY);
		ssize_t n;
		if (fd < 0) {
			perror(argv[i]);
			continue;
		}
		while ((n = read(fd, buf, sizeof(buf))) > 0)
			fwrite(buf, 1, n, stdout);
		close(fd);
	}
	return 0;
}

static int cmd_cd(int argc, char **argv)
{
	if (chdir(argc > 1 ? argv[1] : "/") < 0)
		perror("cd");
	return 0;
}

static int cmd_pwd(int argc, char **argv)
{
	char buf[512];
	(void)argc; (void)argv;
	if (getcwd(buf, sizeof(buf)))
		printf("%s\n", buf);
	return 0;
}

static int cmd_uname(int argc, char **argv)
{
	struct utsname u;
	(void)argc; (void)argv;
	uname(&u);
	printf("OSWiko su %s %s %s (%s)\n", u.sysname, u.release, u.machine, u.nodename);
	return 0;
}

static int cmd_mem(int argc, char **argv)
{
	struct sysinfo si;
	(void)argc; (void)argv;
	sysinfo(&si);
	printf("RAM: %lu MB totali, %lu MB liberi\n",
	       si.totalram * si.mem_unit >> 20, si.freeram * si.mem_unit >> 20);
	printf("Acceso da %ld s, %d processi\n", si.uptime, si.procs);
	return 0;
}

static int cmd_ps(int argc, char **argv)
{
	DIR *d = opendir("/proc");
	struct dirent *de;
	(void)argc; (void)argv;

	printf("  PID  NOME\n");
	while (d && (de = readdir(d))) {
		char path[300], name[64] = "?";
		if (de->d_name[0] < '0' || de->d_name[0] > '9')
			continue;
		snprintf(path, sizeof(path), "/proc/%s/comm", de->d_name);
		FILE *f = fopen(path, "r");
		if (f) {
			if (fgets(name, sizeof(name), f))
				name[strcspn(name, "\n")] = 0;
			fclose(f);
		}
		printf("%5s  %s\n", de->d_name, name);
	}
	if (d)
		closedir(d);
	return 0;
}

static int cmd_dmesg(int argc, char **argv)
{
	(void)argc; (void)argv;
	int n = klogctl(10, NULL, 0); /* SYSLOG_ACTION_SIZE_BUFFER */
	if (n <= 0)
		n = 1 << 16;
	char *buf = malloc(n);
	if (!buf)
		return 1;
	n = klogctl(3, buf, n); /* SYSLOG_ACTION_READ_ALL */
	if (n > 0)
		fwrite(buf, 1, n, stdout);
	free(buf);
	return 0;
}

static int cmd_kill(int argc, char **argv)
{
	if (argc < 2) {
		printf("uso: kill PID [SEGNALE]\n");
		return 1;
	}
	if (kill(atoi(argv[1]), argc > 2 ? atoi(argv[2]) : SIGTERM) < 0)
		perror("kill");
	return 0;
}

static int cmd_clear(int argc, char **argv)
{
	(void)argc; (void)argv;
	printf("\033[2J\033[H");
	return 0;
}

static int cmd_reboot(int argc, char **argv)
{
	(void)argc;
	sync();
	if (!strcmp(argv[0], "poweroff"))
		reboot(RB_POWER_OFF);
	else
		reboot(RB_AUTOBOOT);
	perror("reboot");
	return 1;
}

static const struct {
	const char *name;
	int (*fn)(int, char **);
	const char *help;
} builtins[] = {
	{ "help", cmd_help, "questo elenco" },
	{ "echo", cmd_echo, "stampa il testo" },
	{ "ls", cmd_ls, "elenca i file" },
	{ "cat", cmd_cat, "mostra un file" },
	{ "cd", cmd_cd, "cambia cartella" },
	{ "pwd", cmd_pwd, "cartella corrente" },
	{ "uname", cmd_uname, "versione del sistema" },
	{ "mem", cmd_mem, "memoria e uptime" },
	{ "ps", cmd_ps, "processi in esecuzione" },
	{ "dmesg", cmd_dmesg, "messaggi del kernel" },
	{ "kill", cmd_kill, "termina un processo" },
	{ "clear", cmd_clear, "pulisce lo schermo" },
	{ "reboot", cmd_reboot, "riavvia" },
	{ "poweroff", cmd_reboot, "spegne" },
};
#define N_BUILTINS (sizeof(builtins) / sizeof(builtins[0]))

static int cmd_help(int argc, char **argv)
{
	(void)argc; (void)argv;
	printf("Comandi di osh:\n");
	for (size_t i = 0; i < N_BUILTINS; i++)
		printf("  %-10s %s\n", builtins[i].name, builtins[i].help);
	printf("Altri programmi vengono cercati in /bin.\n");
	return 0;
}

static int run_external(char **argv)
{
	char path[256];
	pid_t pid;
	int status;

	if (strchr(argv[0], '/'))
		snprintf(path, sizeof(path), "%s", argv[0]);
	else
		snprintf(path, sizeof(path), "/bin/%s", argv[0]);
	pid = fork();
	if (pid == 0) {
		signal(SIGINT, SIG_DFL);
		execv(path, argv);
		fprintf(stderr, "osh: %s: comando non trovato\n", argv[0]);
		_exit(127);
	}
	waitpid(pid, &status, 0);
	return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

int main(void)
{
	char line[512];

	signal(SIGINT, SIG_IGN);
	setvbuf(stdout, NULL, _IOLBF, 0);
	printf("osh - la shell di OSWiko. Digita 'help'.\n");
	for (;;) {
		char cwd[256] = "?";
		if (!getcwd(cwd, sizeof(cwd)))
			strcpy(cwd, "?");
		printf("\033[1;32moswiko\033[0m:\033[1;34m%s\033[0m# ", cwd);
		fflush(stdout);
		if (!fgets(line, sizeof(line), stdin)) {
			if (errno == EINTR) {
				clearerr(stdin);
				continue;
			}
			return 0;
		}
		char *argv[MAX_ARGS + 1];
		int argc = 0;
		for (char *tok = strtok(line, " \t\n"); tok && argc < MAX_ARGS;
		     tok = strtok(NULL, " \t\n"))
			argv[argc++] = tok;
		argv[argc] = NULL;
		if (argc == 0)
			continue;
		if (!strcmp(argv[0], "exit"))
			return 0;
		size_t i;
		for (i = 0; i < N_BUILTINS; i++)
			if (!strcmp(argv[0], builtins[i].name)) {
				builtins[i].fn(argc, argv);
				break;
			}
		if (i == N_BUILTINS)
			run_external(argv);
	}
}

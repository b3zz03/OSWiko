/* OSWiko - init (PID 1)
 *
 * Primo programma eseguito dal kernel. Monta i filesystem virtuali,
 * avvia l'interfaccia grafica e la shell di debug sulla console seriale,
 * e li riavvia se terminano.
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define UI_PATH    "/bin/ui"
#define SHELL_PATH "/bin/osh"
#define MSGD_PATH  "/bin/msgd"

static void log_msg(const char *msg)
{
	fprintf(stderr, "\033[1;35m[init]\033[0m %s\n", msg);
}

static void mount_fs(const char *src, const char *dst, const char *type,
		     unsigned long flags)
{
	mkdir(dst, 0755);
	if (mount(src, dst, type, flags, NULL) < 0 && errno != EBUSY)
		fprintf(stderr, "[init] mount %s su %s: %s\n", type, dst, strerror(errno));
}

/* Nome del dispositivo console reale (es. ttyAMA0), per la shell. */
static void console_tty(char *buf, size_t n)
{
	FILE *f = fopen("/sys/class/tty/console/active", "r");
	char name[64] = "";

	if (f) {
		if (fscanf(f, "%63s", name) != 1)
			name[0] = 0;
		fclose(f);
	}
	/* l'ultimo della lista è la console principale */
	snprintf(buf, n, "/dev/%s", name[0] ? name : "console");
}

/* Monta la memoria permanente su /data. Il dispositivo è indicato sulla riga
 * di comando del kernel (oswiko.data=/dev/vda nell'emulatore; sul telefono
 * sarà la partizione userdata). Se manca, usa la RAM e lo segnala in
 * /run/data-volatile: le app possono avvisare che i dati non restano. */
static void mount_data(void)
{
	char cmdline[1024] = "", dev[128] = "";
	FILE *f = fopen("/proc/cmdline", "r");
	if (f) {
		if (!fgets(cmdline, sizeof(cmdline), f))
			cmdline[0] = 0;
		fclose(f);
	}
	char *p = strstr(cmdline, "oswiko.data=");
	if (p)
		sscanf(p + 12, "%127s", dev);

	mkdir("/data", 0755);
	if (dev[0]) {
		for (int i = 0; i < 30 && access(dev, F_OK) != 0; i++)
			usleep(100000); /* il disco può comparire con un attimo di ritardo */
		/* commit=2: i dati arrivano sul disco entro 2 secondi */
		if (mount(dev, "/data", "ext4", MS_NOSUID | MS_NODEV | MS_NOATIME,
			  "commit=2") == 0) {
			fprintf(stderr, "[init] memoria permanente: %s su /data\n", dev);
			return;
		}
		fprintf(stderr, "[init] impossibile montare %s: %s\n", dev, strerror(errno));
	}
	mount_fs("tmpfs", "/data", "tmpfs", MS_NOSUID | MS_NODEV);
	int fd = open("/run/data-volatile", O_WRONLY | O_CREAT, 0644);
	if (fd >= 0)
		close(fd);
	log_msg("nessun disco: i dati restano solo fino allo spegnimento");
}

static pid_t spawn(const char *path, int with_tty)
{
	pid_t pid = fork();

	if (pid != 0)
		return pid;

	/* figlio */
	sigset_t all;
	sigemptyset(&all);
	sigprocmask(SIG_SETMASK, &all, NULL);
	setsid();
	if (with_tty) {
		char tty[80];
		console_tty(tty, sizeof(tty));
		int fd = open(tty, O_RDWR);
		if (fd >= 0) {
			ioctl(fd, TIOCSCTTY, 0);
			dup2(fd, 0);
			dup2(fd, 1);
			dup2(fd, 2);
			if (fd > 2)
				close(fd);
		}
	}
	char *argv[] = { (char *)path, NULL };
	char *envp[] = { "PATH=/bin", "HOME=/", "TERM=vt100",
			 "TZ=CET-1CEST,M3.5.0,M10.5.0/3", NULL };
	execve(path, argv, envp);
	fprintf(stderr, "[init] impossibile avviare %s: %s\n", path, strerror(errno));
	_exit(127);
}

static void shutdown_handler(int sig)
{
	log_msg(sig == SIGUSR2 ? "spegnimento" : "riavvio");
	kill(-1, SIGTERM);
	sleep(1);
	sync();
	reboot(sig == SIGUSR2 ? RB_POWER_OFF : RB_AUTOBOOT);
}

int main(void)
{
	if (getpid() != 1)
		fprintf(stderr, "[init] attenzione: non sono il PID 1\n");

	mount_fs("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC);
	mount_fs("sysfs", "/sys", "sysfs", MS_NOSUID | MS_NODEV | MS_NOEXEC);
	mount_fs("devtmpfs", "/dev", "devtmpfs", MS_NOSUID);
	mount_fs("tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV);
	mount_fs("tmpfs", "/run", "tmpfs", MS_NOSUID | MS_NODEV);
	mount_data();
	if (sethostname("oswiko", 6) < 0)
		perror("sethostname");
	setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);

	fprintf(stderr,
		"\n\033[1;36m"
		"   ___  ______        ___ _\n"
		"  / _ \\/ ___\\ \\      / (_) | _____\n"
		" | | | \\___ \\\\ \\ /\\ / /| | |/ / _ \\\n"
		" | |_| |___) |\\ V  V / | |   < (_) |\n"
		"  \\___/|____/  \\_/\\_/  |_|_|\\_\\___/\033[0m\n\n");

	/* kill -USR1 1 = riavvia, kill -USR2 1 = spegni */
	signal(SIGUSR1, shutdown_handler);
	signal(SIGUSR2, shutdown_handler);

	log_msg("avvio interfaccia grafica");
	pid_t ui = spawn(UI_PATH, 0);
	log_msg("avvio shell (digita 'help')");
	pid_t sh = spawn(SHELL_PATH, 1);
	pid_t msgd = spawn(MSGD_PATH, 0);

	/* Raccoglie i processi terminati e riavvia quelli di sistema. */
	for (;;) {
		int status;
		pid_t pid = wait(&status);
		if (pid < 0) {
			if (errno == ECHILD)
				sleep(1);
			continue;
		}
		if (pid == ui) {
			log_msg("interfaccia terminata, la riavvio");
			sleep(1);
			ui = spawn(UI_PATH, 0);
		} else if (pid == sh) {
			sleep(1);
			sh = spawn(SHELL_PATH, 1);
		} else if (pid == msgd) {
			sleep(5);
			msgd = spawn(MSGD_PATH, 0);
		}
	}
}

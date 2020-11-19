#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>

#include "shared.h"

#define STTLENGTH 256
#define LOCKFILE  "/tmp/dsblocks.pid"

typedef struct {
	void (*funcu)(char* str, int sigval);
	void (*funcc)(int button);
	const int interval;
	const int signal;
	char	  cmdoutcur[CMDLENGTH];
	char	  cmdoutprv[CMDLENGTH];
} Block;

#include "blocks.h"

static void buttonhandler(int signal, siginfo_t* si, void* ucontext);
static void setroot();
static void setupsignals();
static void sighandler(int signal, siginfo_t* si, void* ucontext);
static void statusloop();
static void termhandler(int signum);
static int	updatestatus();
static void writepid();

Display* dpy;
pid_t	 pid;

static int		statuscontinue = 1;
static char		statusstr[STTLENGTH];
static size_t	delimlength;
static sigset_t blocksigmask;

void buttonhandler(int signal, siginfo_t* si, void* ucontext) {
	signal = si->si_value.sival_int >> 8;
	for (Block* block = blocks; block->funcu; block++)
		if (block->signal == signal)
			switch (fork()) {
				case -1:
					perror("buttonhandler - fork");
					break;
				case 0:
					close(ConnectionNumber(dpy));
					block->funcc(si->si_value.sival_int & 0xff);
					exit(0);
			}
}

void setroot() {
	if (updatestatus()) {
		XStoreName(dpy, DefaultRootWindow(dpy), statusstr);
		XSync(dpy, False);
	}
}

void setupsignals() {
	struct sigaction sa;

	/* to handle HUP, INT and TERM */
	sa.sa_flags = SA_RESTART;
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = termhandler;
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	/* to ignore unused realtime signals */
	// sa.sa_flags = SA_RESTART;
	// sigemptyset(&sa.sa_mask);
	sa.sa_handler = SIG_IGN;
	for (int i = SIGRTMIN + 1; i <= SIGRTMAX; i++)
		sigaction(i, &sa, NULL);

	/* to prevent forked children from becoming zombies */
	sa.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART;
	// sigemptyset(&sa.sa_mask);
	sa.sa_handler = SIG_DFL;
	sigaction(SIGCHLD, &sa, NULL);

	/* to handle signals generated by dwm on click events */
	sa.sa_flags = SA_RESTART | SA_SIGINFO;
	// sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = buttonhandler;
	sigaction(SIGRTMIN, &sa, NULL);

	/* to handle update signals for individual blocks */
	sa.sa_flags |= SA_NODEFER;
	sa.sa_mask		= blocksigmask;
	sa.sa_sigaction = sighandler;
	for (Block* block = blocks; block->funcu; block++)
		if (block->signal > 0)
			sigaction(SIGRTMIN + block->signal, &sa, NULL);
}

void sighandler(int signal, siginfo_t* si, void* ucontext) {
	signal -= SIGRTMIN;
	for (Block* block = blocks; block->funcu; block++)
		if (block->signal == signal)
			block->funcu(block->cmdoutcur, si->si_value.sival_int);
	setroot();
}

void statusloop() {
	int i;

	/* first run */
	sigprocmask(SIG_BLOCK, &blocksigmask, NULL);
	for (Block* block = blocks; block->funcu; block++)
		if (block->interval >= 0)
			block->funcu(block->cmdoutcur, NILL);
	setroot();
	sigprocmask(SIG_UNBLOCK, &blocksigmask, NULL);
	sleep(SLEEPINTERVAL);
	i = SLEEPINTERVAL;
	/* main loop */
	while (statuscontinue) {
		sigprocmask(SIG_BLOCK, &blocksigmask, NULL);
		for (Block* block = blocks; block->funcu; block++)
			if (block->interval > 0 && i % block->interval == 0)
				block->funcu(block->cmdoutcur, NILL);
		setroot();
		sigprocmask(SIG_UNBLOCK, &blocksigmask, NULL);
		sleep(SLEEPINTERVAL);
		i += SLEEPINTERVAL;
	}
}

void termhandler(int signum) {
	statuscontinue = 0;
}

/* returns whether block outputs have changed and updates statusstr if they have */
int updatestatus() {
	char*		s = statusstr;
	char *		c, *p; /* for cmdoutcur and cmdoutprv */
	const char* d;	   /* for delimiter */
	Block*		block = blocks;

	/* checking half of the function */
	/* find the first non-empty block */
	for (;; block++) {
		/* all blocks are empty */
		if (!block->funcu)
			return 0;
		/* contents of the block changed */
		if (*block->cmdoutcur != *block->cmdoutprv)
			goto update0;
		/* skip delimiter handler for the first non-empty block */
		if (*block->cmdoutcur != '\0' && *block->cmdoutcur != '\n')
			goto skipdelimc;
	}
	/* main loop */
	for (; block->funcu; block++) {
		/* contents of the block changed */
		if (*block->cmdoutcur != *block->cmdoutprv)
			goto update1;
		/* delimiter handler */
		if (*block->cmdoutcur != '\0' && *block->cmdoutcur != '\n')
			s += delimlength;
		/* skip over empty blocks */
		else
			continue;
	skipdelimc:
		/* checking for the first byte has been done */
		c = block->cmdoutcur + 1, p = block->cmdoutprv + 1;
		for (; *c != '\0' && *c != '\n'; c++, p++)
			/* contents of the block changed */
			if (*c != *p) {
				s += c - block->cmdoutcur;
				goto update2;
			}
		s += c - block->cmdoutcur;
		/* byte containing info about signal number for the block */
		if (block->funcc && block->signal)
			s++;
	}
	return 0;

	/* updating half of the function */
	/* find the first non-empty block */
	for (;; block++) {
		/* all blocks are empty */
		if (!block->funcu)
			return 1;
	update0:
		/* don't add delimiter before the first non-empty block */
		if (*block->cmdoutcur != '\0' && *block->cmdoutcur != '\n')
			goto skipdelimu;
		*block->cmdoutprv = *block->cmdoutcur;
	}
	/* main loop */
	for (; block->funcu; block++) {
	update1:
		/* delimiter handler */
		if (*block->cmdoutcur != '\0' && *block->cmdoutcur != '\n') {
			d = delim;
			while (*d != '\0')
				*(s++) = *(d++);
			*(s++) = '\n'; /* to mark the end of delimiter */
						   /* skip over empty blocks */
		} else {
			*block->cmdoutprv = *block->cmdoutcur;
			continue;
		}
	skipdelimu:
		c = block->cmdoutcur, p = block->cmdoutprv;
	update2:
		do {
			*(s++) = *c;
			*p	   = *c;
			c++, p++;
		} while (*c != '\0' && *c != '\n');
		if (block->funcc && block->signal)
			*(s++) = block->signal;
	}
	*s = '\0';
	return 1;
}

void writepid() {
	int			 fd;
	struct flock fl;

	fd = open(LOCKFILE, O_RDWR | O_CREAT, 0644);
	if (fd == -1) {
		perror("writepid - open");
		exit(1);
	}
	fl.l_type	= F_WRLCK;
	fl.l_start	= 0;
	fl.l_whence = SEEK_SET;
	fl.l_len	= 0;
	if (fcntl(fd, F_SETLK, &fl) == -1) {
		if (errno == EACCES || errno == EAGAIN) {
			fputs("Error: another instance of dsblocks is already running.\n", stderr);
			exit(2);
		}
		perror("writepid - fcntl");
		exit(1);
	}
	if (ftruncate(fd, 0) == -1) {
		perror("writepid - ftruncate");
		exit(1);
	}
	if (dprintf(fd, "%ld", (long) pid) < 0) {
		perror("writepid - dprintf");
		exit(1);
	}
}

int main(int argc, char* argv[]) {
	pid = getpid();
	writepid();
	if (argc > 2)
		if (strcmp(argv[1], "-d") == 0)
			delim = argv[2];
	delimlength = strlen(delim) + 1;
	if (!(dpy = XOpenDisplay(NULL))) {
		fputs("Error: could not open display.\n", stderr);
		return 1;
	}
	sigemptyset(&blocksigmask);
	sigaddset(&blocksigmask, SIGHUP);
	sigaddset(&blocksigmask, SIGINT);
	sigaddset(&blocksigmask, SIGTERM);
	for (Block* block = blocks; block->funcu; block++)
		if (block->signal > 0)
			sigaddset(&blocksigmask, SIGRTMIN + block->signal);
	setupsignals();
	statusloop();
	unlink(LOCKFILE);
	XStoreName(dpy, DefaultRootWindow(dpy), "");
	XCloseDisplay(dpy);
	return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "dhcpc.h"

int main (int argc, char *argv[]) {
	/* Preparar el pipe que viene en STDOUT */
	int pipe;
	int null_fd;
	
	pipe = dup (STDOUT_FILENO);
	null_fd = open ("/dev/null", O_RDWR);
	
	close (STDOUT_FILENO);
	close (STDIN_FILENO);
	
	dup2 (null_fd, STDOUT_FILENO);
	dup2 (null_fd, STDIN_FILENO);
	
	close (null_fd);
	
	/* Parsear aquí los argumentos */
	if (argc < 2) {
		fprintf (stderr, "Missing interface argument");
		return EXIT_FAILURE;
	}
	
	udhcpc_main (pipe, argv[1]);
	
	close (STDOUT_FILENO);
	
	return 0;
}

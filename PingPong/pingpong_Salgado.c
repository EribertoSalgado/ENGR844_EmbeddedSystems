// pingpong_Salgado.c - Sends a message to the M4 over rpmsg_char and prints the reply.
// build: arm-linux-gnueabihf-gcc pingpong_Salgado.c -o pingpong_Salgado
// usage:
//      ./pingpong_Salgado [service_name] [dst_addr] [message...]
// example:
//      ./pingpong_Salgado m4-pingpong 0x400 "hello world"

#include <fcntl.h>
#include <linux/rpmsg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>

// Creates an RPMsg endpoint on the A7 side. Linux uses this handle to talk to M4.
static int create_ept(const char *ctrl, const char *name, unsigned dst) {
	int cfd = open(ctrl, O_RDWR);
	if (cfd < 0) return perror("open ctrl"), -1;

	struct rpmsg_endpoint_info e = {0};
	snprintf(e.name, sizeof(e.name), "%s", name);
	e.src = RPMSG_ADDR_ANY;   // let Linux pick the source address
	e.dst = dst;              // destination is the M4 endpoint address

#ifndef RPMSG_ADDR_EPT
# ifdef RPMSG_CREATE_EPT_IOCTL
#  define RPMSG_CREATE_EPT RPMSG_CREATE_EPT_IOCTL
# else
# error "RPMSG_CREATE_EPT not defined by kernel headers"
# endif
#endif

	int rc = ioctl(cfd, RPMSG_CREATE_EPT, &e);
	close(cfd);

	return (rc < 0) ? (perror("RPMSG_CREATE_EPT"), -1) : 0;
}

// Searches through /sys/class/rpmsg to find the device that matches the service name.
// Once found, the function fills 'out' with something like "/dev/rpmsg0".
static int find_dev_by_service(const char *svc, char *out, size_t n) {
	DIR *d = opendir("/sys/class/rpmsg");
	if (!d) return -1;

	struct dirent *de;
	while ((de = readdir(d))) {
		if (strncmp(de->d_name, "rpmsg", 5)) continue;

		// Build the path to the "name" file for this rpmsg device
		char path[256];
		snprintf(path, sizeof(path), "/sys/class/rpmsg/%s/name", de->d_name);

		FILE *f = fopen(path, "r");
		if (!f) continue;

		char name[128] = {0};
		fgets(name, sizeof(name), f);
		fclose(f);

		name[strcspn(name, "\r\n")] = 0;  // strip newline

		// Compare the service name we are looking for
		if (!strcmp(name, svc)) {
			snprintf(out, n, "/dev/%s", de->d_name);
			closedir(d);
			return 0;
		}
	}

	closedir(d);
	return -1;
}

int main(int argc, char **argv) {
	// Service name defaults to "m4-pingpong" if not provided on the command line
	const char *svc = (argc > 1) ? argv[1] : "m4-pingpong";

	// M4 endpoint address (usually 0x400 unless changed)
	unsigned dst = (argc > 2) ? (unsigned)strtoul(argv[2], NULL, 0) : 0x400;

	// Default message includes my name as required
	const char *msg = (argc > 3 ? argv[3] : "A7: Hello from Eriberto!");

	// Create the endpoint on A7
	if (create_ept("/dev/rpmsg_ctrl0", svc, dst) < 0) return 1;

	// Give Linux a moment to create the device entry
	usleep(200 * 1000);

	// Look for the device that matches the service name
	char dev[64];
	if (find_dev_by_service(svc, dev, sizeof(dev)) < 0) {
		fprintf(stderr, "Cannot find rpmsg device for %s\n", svc);
		return 1;
	}

	printf("Using endpoint: %s\n", dev);

	// Open the rpmsg data device
	int fd = open(dev, O_RDWR);
	if (fd < 0) return perror("open data"), 1;

	// Send our message to the M4
	write(fd, msg, strlen(msg));
	printf("Sent: '%s'\n", msg);

	// Wait for the M4 to respond
	char buf[1024];
	ssize_t r = read(fd, buf, sizeof(buf) - 1);
	if (r < 0) return perror("read"), 1;

	buf[r] = 0;
	printf("Reply: '%s'\n", buf);

	close(fd);
	return 0;
}


#define _GNU_SOURCE
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>

int start_connect_socket(unsigned short port)
{
	int socket_fd;
	int opt;
	int rc;
	struct sockaddr_in serveraddr;

	/*
	 * create a socket - endpoint of the communication
	 * AF_INET - IPv4 Internet protocol
	 * SOCK_STREAM - connection based byte stream
	 */
	socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (socket_fd == -1) {
		perror("socket failed");
		exit(EXIT_FAILURE);
	}

	/* this is so that on restart the server did not get "Address already in use" */
	opt = 1;
	rc = setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt,
			sizeof(opt));
	if (rc == -1) {
		perror("setsockopt failed");
		exit(EXIT_FAILURE);
	}

	/* assign address to the socket */
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons(port);
	rc = bind(socket_fd, (struct sockaddr *)&serveraddr,
		  sizeof(serveraddr));
	if (rc == -1) {
		perror("bind failed");
		exit(EXIT_FAILURE);
	}

	/* mark the socket as listening for connection requests. 1 is
	 * max length of queue of connection requests */
	rc = listen(socket_fd, 1);
	if (rc == -1) {
		perror("listen failed");
		exit(EXIT_FAILURE);
	}
	return socket_fd;
}

int handle_connect(int socket_fd)
{
	struct sockaddr_in clientaddr;
	socklen_t size;
	int rc;

	size = sizeof(clientaddr);
	rc = accept(socket_fd, (struct sockaddr *)&clientaddr, &size);
	if (rc == -1) {
		perror("accept failed");
		exit(EXIT_FAILURE);
	}
	printf("connected %s to %d\n", inet_ntoa(clientaddr.sin_addr), rc);
	return rc;
}

int process_client_msg(int fd, fd_set *fds)
{
	char buf[1024];
	ssize_t readrc;

	readrc = read(fd, buf, sizeof(buf) - 1);
	if (readrc == -1) {
		perror("read failed");
		exit(EXIT_FAILURE);
	}
	buf[readrc] = 0;

	if (!strcmp(buf, "quit\r\n")) {
		close(fd);
		FD_CLR(fd, fds);
		printf("closed connection from %d\n", fd);
	} else {
		printf("received from client %d: %s", fd, buf);
	}
	return 0;
}

int main(void)
{
	int socket_fd;
	int rc;
	fd_set fds;
	fd_set selectfds;
	int i;

	socket_fd = start_connect_socket(5555);

	FD_ZERO(&fds);
	/* initially fd set contains only connect socket */
	FD_SET(socket_fd, &fds);
	while (1) {
		/* reinitialize selectfds with active sockets */
		selectfds = fds;
		/* wait until input arrives on sockets in selectfds set */
		rc = select(FD_SETSIZE, &selectfds, NULL, NULL, NULL);
		if (rc == -1) {
			perror("select failed");
			return 1;
		}

		/* check all sockets for which input is pending for processing */
		for (i = 0; i < FD_SETSIZE; i++) {
			if (!FD_ISSET (i, &selectfds))
				continue;

			if (i == socket_fd) {
				/* connect request */
				rc = handle_connect(socket_fd);
				FD_SET(rc, &fds);
				continue;
			}

			process_client_msg(i, &fds);
		}
	}
	/* no way to get here */
	close(socket_fd);
	return 0;
}


#define _GNU_SOURCE
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

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
/*	opt = 1;
	rc = setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt,
			sizeof(opt));
	if (rc == -1) {
		perror("setsockopt failed");
		exit(EXIT_FAILURE);
	}
*/
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

#define CLIENT_USERNAME 1
#define CLIENT_KEY_ID 2
#define CLIENT_CONFIRMATION 3
#define CLIENT_OK 4  // OK <x> <y>\a\b
#define CLIENT_RECHARGING 5
#define CLIENT_FULL_POWER 6
#define CLIENT_MESSAGE 7
#define CLIENT_UNKNOWN 8

// Check whether msg suits to format  "<text>\a\b" (text length + 2 <= max)
// Can be used to check if message suits CLIENT_USERNAME (max = 20) or CLIENT_MESSAGE (max = 100)
// return true if yes
// 		  false otherwise
_Bool decode_client_text(char* msg, int max) {
	char* res = strstr(msg, "\a\b");
	if (res == NULL) {
		return false;
	}
	if ((res - msg + 2 > max) || (res == msg)) {
		return false;
	}
	
	return true;
}

// Check whether msg suits to format "decimal number\a\b". Decimal num has to be less than max.
// Can be used to check if message suits CLIENT_CONFIRMATION (max=65535) or CLIENT_KEY_ID (max=999).
// return true if yes
// 		  false otherwise 
_Bool decode_client_keyid_confirm(char* msg, int max, int* key_id) {
	char* p;
	int num;
	
	num = strtol(msg, &p, 10);
	if (num == 0 && errno == EINVAL) {
		return false;
	}
	if (num < 0 || num > max) {
		return false;
	}
	if (p[0] != '\a' || p[1] != '\b') {
		return false;
	}

	*key_id = num;
	return true;
}



// Check whether msg suits to format "OK <x> <y>\a\b"
// return true if yes
// 		  false otherwise
_Bool decode_client_ok(char* msg, int *x, int *y) {
	char* p;

	*x = strtol(msg + 3, &p, 10);
	if (&x == 0 && errno == EINVAL) {
		// No decimal number
		return false;
	}	
	*y = strtol(p, &p, 10);
	if (&y == 0 && errno == EINVAL) {
		return false;
	}
	if (p[0] != '\a' || p[1] != '\b') {
		return false;
	}
	
	return true;
}



/*
int decode_client_msg(char* msg) {
	if (strcmp(msg, "RECHARGING\a\b") == 0) {
		return CLIENT_RECHARGING;
	}
	if (strcmp(msg, "FULL POWER\a\b") == 0) {
		return CLIENT_FULL_POWER;
	}
	if (msg[0] == 'O' && msg[1] == 'K' && msg[2] == ' ') {
		if (decode_client_ok(msg)) {
			return CLIENT_OK;
		}
	}

	return CLIENT_UNKNOWN;
}
*/

#define SERVER_CONFIRMATION
#define SERVER_MOVE "102 MOVE\a\b"
#define SERVER_TURN_LEFT "103 TURN LEFT\a\b"
#define SERVER_TURN_RIGHT "104 TURN RIGHT\a\b"
#define SERVER_PICK_UP "105 GET MESSAGE\a\b"
#define SERVER_LOGOUT "106 LOGOUT\a\b"
#define SERVER_KEY_REQUEST "107 KEY REQUEST\a\b"
#define SERVER_OK "200 OK\a\b"
#define SERVER_LOGIN_FAILED "300 LOGIN FAILED\a\b"
#define SERVER_SYNTAX_ERROR "301 SYNTAX ERROR\a\b"
#define SERVER_LOGIC_ERROR "302 LOGIC ERROR\a\b"
#define SERVER_KEY_OUT_OF_RANGE_ERROR "303 KEY OUT OF RANGE\a\b"


#define EXPECT_USERNAME 1
#define EXPECT_KEY_ID 2
#define EXPECT_CONFIRMATION 3
#define EXPECT_CLIENT_OK 4
#define EXPECT_CLIENT_MSG 5
struct {
	int state;
	char name[20];
	int keyid;
} client_states[FD_SETSIZE];

struct {
	int server_key;
	int client_key;
} authentification_keys[5] = {
	{23019, 32037},
	{32037, 29295},
	{18789, 13603},
	{16443, 29533},
	{18189, 21952}
};

int get_hash(char* name) {
	int sum = 0;
	int i;

	for (i = 0; i < strlen(name); i++) {
		sum += name[i];
	}
	sum *= 1000;
	sum %= 65536;

	return sum;
}

int process_client_msg(int fd, fd_set *fds)
{
	char buf[1024];
	ssize_t rc;
	
	rc = read(fd, buf, sizeof(buf) - 1);
	if (rc == -1) {
		perror("read failed");
		exit(EXIT_FAILURE);
	}
	buf[rc] = 0;
	printf("%s, Expect %d\n", buf, client_states[fd].state);
       
	if (client_states[fd].state == EXPECT_USERNAME) {
		// Check that client send CLIENT_USERNAME message
		if (!decode_client_text(buf, 20)){
			// Not CLIENT_USERNAME
			close(fd);
			FD_CLR(fd, fds);
			return 0;
		}
		strncpy(client_states[fd].name, buf, strlen(buf) - 2);
		rc = write(fd, SERVER_KEY_REQUEST, strlen(SERVER_KEY_REQUEST));
		if (rc != strlen(SERVER_KEY_REQUEST)) {
			close(fd);
			FD_CLR(fd, fds);
			return 0;
		}
		client_states[fd].state = EXPECT_KEY_ID;

		return 0;
	}

	if (client_states[fd].state == EXPECT_KEY_ID) {
		int hash;
		int key_id;

		if (!decode_client_keyid_confirm(buf, 999, &key_id)) {
			// Not CLIENT_KEY_ID
			close(fd);
			FD_CLR(fd, fds);
			return 0;
		}
		// Compose reply to the client
		hash = get_hash(client_states[fd].name);
		hash += authentification_keys[key_id].server_key;
		hash %= 65536;
		sprintf(buf, "%d\a\b", hash);
		rc = write(fd, buf, strlen(buf));
		if (rc != strlen(buf)) {
			close(fd);
			FD_CLR(fd, fds);
			return 0;
		}
		client_states[fd].state = EXPECT_CONFIRMATION;
		client_states[fd].keyid = key_id;
		return 0;
	}

	if (client_states[fd].state == EXPECT_CONFIRMATION) {
		int code;
		char* msg;

		printf("Decode\n");
		if (!decode_client_keyid_confirm(buf,65535, &code)) {
			// Not CLIENT_CONFIRMATION
			close(fd);
			FD_CLR(fd, fds);
			return 0;
		}
       		// Check confirmation code: restore hash value
		code += 65536;
		code -= authentification_keys[client_states[fd].keyid].client_key;
		code %= 65536;
		
		if (code != get_hash(client_states[fd].name)) {
			// confirmation code is wrong
			msg = SERVER_LOGIN_FAILED;
			rc = write(fd, msg, strlen(msg));
			if (rc != strlen(msg)) {
				close(fd);
				FD_CLR(fd, fds);
				return 0;
			}
			return 0;
		}
		msg = SERVER_OK;
		rc = write(fd, msg, strlen(msg));
		if (rc != strlen(msg)) {
			close(fd);
			FD_CLR(fd, fds);
			return 0;
		}
		// Send first of moves to detect current location
		rc = write(fd, SERVER_MOVE, strlen(SERVER_MOVE));
		if (rc != strlen(SERVER_MOVE)) {
			close(fd);
			FD_CLR(fd, fds);
			return 0;
		}
		client_states[fd].state = EXPECT_CLIENT_OK;

		return 0;		
	}
	if (client_states[fd].state == EXPECT_CLIENT_OK) {
		int x;
		int y;

      		if (!decode_client_ok(buf, &x, &y)) {
			// Not CLIENT_OK
			close(fd);
			FD_CLR(fd, fds);
			return 0;
		}
		if (x == 0 && y == 0) {
			rc = write(fd, SERVER_PICK_UP, strlen(SERVER_PICK_UP));
			if (rc != strlen(SERVER_PICK_UP)) {
				close(fd);
				FD_CLR(fd, fds);
				return 0;
			}
			client_states[fd].state = EXPECT_CLIENT_MSG;
			return 0;
		}
		printf("%d %d\n", x, y);
		return 0;
	}
	if (client_states[fd].state == EXPECT_CLIENT_MSG) {
		if (!decode_client_text(buf, 100)) {
			// Not CLIENT_TEXT
			close(fd);
			FD_CLR(fd, fds);
			return 0;
		}
		rc = write(fd, SERVER_LOGOUT, strlen(SERVER_LOGOUT));
		if (rc != strlen(SERVER_LOGOUT)){
			close(fd);
			FD_CLR(fd, fds);
			return 0;
		}
		// Done with the the client
		close(fd);
		FD_CLR(fd, fds);
		return 0;
	}

	// Unknown state
	printf("Should not be here\n");
	close(fd);
	FD_CLR(fd, fds);
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
				client_states[rc].state = EXPECT_USERNAME;
				continue;
			}

			process_client_msg(i, &fds);
		}
	}
	/* no way to get here */
	close(socket_fd);
	return 0;
}


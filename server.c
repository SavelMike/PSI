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
#include <limits.h>

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

	/* this is so that on restart the server did not get bind failure with "Address already in use" */
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


int has_terminator(char* msg, int length) {
	int i;
	int len = -1;

	for (i = 0; i < length - 1; i++) {
		if (msg[i] == '\a' && msg[i + 1] == '\b') {
			len = i;
			break;
		}
	}

	return len;
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
// Can be used to check if message suits CLIENT_USERNAME (max = USERNAME_MAXLEN) or
// CLIENT_MESSAGE (max = CLIENTMSG_MAXLEN)
// return true if yes
// false otherwise
_Bool decode_client_text(char* msg, int length, int max, int* textlen) {
	int len;

	len = has_terminator(msg, length);
	if (len == -1) {
		return false;
	}

	if ((len + 2 > max) || (len == 0)) {
		return false;
	}
	*textlen = len;
	
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
	if (*x == 0 && errno == EINVAL) {
		// No decimal number
		return false;
	}	
	*y = strtol(p, &p, 10);
	if (*y == 0 && errno == EINVAL) {
		return false;
	}
	if (p[0] != '\a' || p[1] != '\b') {
		return false;
	}
	
	return true;
}

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

#define DIRECTION_RIGHT 1
#define DIRECTION_LEFT 2
#define DIRECTION_UP 3
#define DIRECTION_DOWN 4
#define DIRECTION_UNKNOWN 5
#define X_UNKNOWN INT_MAX
#define Y_UNKNOWN INT_MAX

char* bypass_turn_left[8] = {
	SERVER_TURN_LEFT,
	SERVER_MOVE,
	SERVER_TURN_RIGHT,
	SERVER_MOVE,
	SERVER_MOVE,
	SERVER_TURN_RIGHT,
	SERVER_MOVE,
	SERVER_TURN_LEFT
};
char* bypass_turn_right[8] = {
	SERVER_TURN_RIGHT,
	SERVER_MOVE,
	SERVER_TURN_LEFT,
	SERVER_MOVE,
	SERVER_MOVE,
	SERVER_TURN_LEFT,
	SERVER_MOVE,
	SERVER_TURN_RIGHT
};

#define USERNAME_MAXLEN 20
#define CLIENTMSG_MAXLEN 100
struct {
	int state;
	char name[USERNAME_MAXLEN];
	int namelen;
	int keyid;
	int x;
	int y;
	int direction;
	char client_msg[CLIENTMSG_MAXLEN];
	int cur_size;
	int did_turn; // did turn during orientation detection
	int was_move; // flag is set to 1 during SERVER_MOVE
	int in_bypass;
	int bypass_cmd;
	char** bypass_cmds;
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

int get_hash(char* name, int namelen) {
	int sum = 0;
	int i;

	for (i = 0; i < namelen; i++) {
		sum += name[i];
	}
	sum *= 1000;
	sum %= 65536;

	return sum;
}

int next_step(int fd) {
	ssize_t rc;
	char* msg;
	if (client_states[fd].x == 0 && client_states[fd].y > 0) {
		// Have to go down
		switch (client_states[fd].direction) {
		case DIRECTION_DOWN:
			msg = SERVER_MOVE;
			break;
		case DIRECTION_UP:	
			msg = SERVER_TURN_LEFT;
			client_states[fd].direction = DIRECTION_LEFT;
			break;
		case DIRECTION_LEFT:
			msg = SERVER_TURN_LEFT;
			client_states[fd].direction = DIRECTION_DOWN;
			break;
		case DIRECTION_RIGHT:
			msg = SERVER_TURN_RIGHT;
			client_states[fd].direction = DIRECTION_DOWN;
			break;
		}	
	} else if (client_states[fd].x == 0 && client_states[fd].y < 0) {
		// Have to go up
		switch(client_states[fd].direction) {
		case DIRECTION_UP:
			msg = SERVER_MOVE;
			break;
		case DIRECTION_DOWN:
			msg = SERVER_TURN_RIGHT;
			client_states[fd].direction = DIRECTION_LEFT;
			break;
		case DIRECTION_LEFT:
			msg = SERVER_TURN_RIGHT;
			client_states[fd].direction = DIRECTION_UP;
			break;
		case DIRECTION_RIGHT:
			msg = SERVER_TURN_LEFT;
			client_states[fd].direction = DIRECTION_UP;
			break;
		}
	} else if (client_states[fd].y == 0 && client_states[fd].x > 0) {
		// Have to go left
		switch (client_states[fd].direction) {
		case DIRECTION_LEFT:
			msg = SERVER_MOVE;
			break;
		case DIRECTION_UP:
			msg = SERVER_TURN_LEFT;
			client_states[fd].direction = DIRECTION_LEFT;
			break;
		case DIRECTION_RIGHT:
			msg = SERVER_TURN_RIGHT;
			client_states[fd].direction = DIRECTION_DOWN;
			break;
		case DIRECTION_DOWN:
			msg = SERVER_TURN_RIGHT;
			client_states[fd].direction = DIRECTION_LEFT;
			break;
		}
	} else if (client_states[fd].y == 0 && client_states[fd].x < 0) {
		// Have to go right
		switch (client_states[fd].direction) {
		case DIRECTION_RIGHT:
			msg = SERVER_MOVE;
			break;
		case DIRECTION_UP:
			msg = SERVER_TURN_RIGHT;
			client_states[fd].direction = DIRECTION_RIGHT;
			break;
		case DIRECTION_LEFT:
			msg = SERVER_TURN_LEFT;
			client_states[fd].direction = DIRECTION_DOWN;
			break;
	    	case DIRECTION_DOWN:
			msg = SERVER_TURN_LEFT;
			client_states[fd].direction = DIRECTION_RIGHT;
			break;
		}
	} else if (client_states[fd].x > 0 && client_states[fd].y > 0) {
		// Have to go to left or down
		switch (client_states[fd].direction) {
		case DIRECTION_LEFT:
		case DIRECTION_DOWN:
			msg = SERVER_MOVE;
			break;
		case DIRECTION_RIGHT:
			msg = SERVER_TURN_RIGHT;
			client_states[fd].direction = DIRECTION_DOWN;
			break;
		case DIRECTION_UP:
			msg = SERVER_TURN_LEFT;
			client_states[fd].direction = DIRECTION_LEFT;
			break;
		}
	} else if (client_states[fd].x > 0 && client_states[fd].y < 0) {
		// Have to go left or up
		switch (client_states[fd].direction) {
		case DIRECTION_LEFT:
		case DIRECTION_UP:
			msg = SERVER_MOVE;
			break;
		case DIRECTION_RIGHT:
			msg = SERVER_TURN_LEFT;
			client_states[fd].direction = DIRECTION_UP;
			break;
		case DIRECTION_DOWN:
			msg = SERVER_TURN_RIGHT;
			client_states[fd].direction = DIRECTION_LEFT;
			break;
		}
	} else if (client_states[fd].x < 0 && client_states[fd].y > 0) {
		// Have to go right or down
		switch (client_states[fd].direction) {
		case DIRECTION_RIGHT:
		case DIRECTION_DOWN:
			msg = SERVER_MOVE;
			break;
		case DIRECTION_LEFT:
			msg = SERVER_TURN_LEFT;
			client_states[fd].direction = DIRECTION_DOWN;
			break;
		case DIRECTION_UP:
			msg = SERVER_TURN_RIGHT;
			client_states[fd].direction = DIRECTION_RIGHT;
			break;
		}
	} else if (client_states[fd].x < 0 && client_states[fd].y < 0) {
		// Have to go up or right
		switch (client_states[fd].direction) {
		case DIRECTION_UP:
		case DIRECTION_RIGHT:
			msg = SERVER_MOVE;
			break;
		case DIRECTION_LEFT:
			msg = SERVER_TURN_RIGHT;
			client_states[fd].direction = DIRECTION_UP;
			break;
		case DIRECTION_DOWN:
			msg = SERVER_TURN_LEFT;
			client_states[fd].direction = DIRECTION_RIGHT;
			break;
		}
	} else {
		printf("Should not be here\n");
		exit(1);
	}

	rc = write(fd, msg, strlen(msg));
	if (rc != strlen(msg)) {
		return 1;
	}

	client_states[fd].was_move = !strcmp(SERVER_MOVE, msg);
	
	return 0;
}

#define MSG_COMPLETE 1
#define MSG_INCOMPLETE 2
#define MSG_WRONG 3

int is_complete(char* buf, ssize_t size, int fd, int* tail_size) {
	int i;
	int old_len;

	old_len = client_states[fd].cur_size;
	for (i = 0; i < size; i++) {
		if (old_len + i == CLIENTMSG_MAXLEN) {
			return MSG_WRONG;
		}
		client_states[fd].client_msg[old_len + i] = buf[i];
		client_states[fd].cur_size++;
		if (buf[i] == '\b') {
			// Check previous char
			if (i + old_len != 0 && client_states[fd].client_msg[old_len + i - 1] == '\a') {
				// \a\b at the end
				*tail_size = size - (i + 1);
				
				return MSG_COMPLETE;
			}
		}
	}
	*tail_size = 0;
	return MSG_INCOMPLETE;
}

void print_client_msg(char* tail, int tail_size) {
	int i;

	for (i = 0; i < tail_size; i++) {
		if (isprint(tail[i])) {
			printf("%c", tail[i]);
		} else {
			printf("%d", tail[i]);
		}
	}
	printf(" :length = %d\n", tail_size);
}

int start_bypass_turn_right(int fd) {
	printf("I am here1\n");
	client_states[fd].bypass_cmds = bypass_turn_right;
	client_states[fd].bypass_cmd = 0;
	client_states[fd].in_bypass = 1;
}

int start_bypass_turn_left(int fd) {
	printf("I am here2\n");
	client_states[fd].bypass_cmds = bypass_turn_left;
	client_states[fd].bypass_cmd = 0;
	client_states[fd].in_bypass = 1;
}

int process_client_msg(int fd, fd_set *fds)
{
	char buf[1024];
	char* pbuf;
	int bytes;
	char* cmd;
	int cmd_len;
	ssize_t rc;
	int tail_size;
	char* tail;

	pbuf = buf;
	bytes = read(fd, buf, sizeof(buf));
	if (bytes == -1) {
		perror("read failed");
		exit(EXIT_FAILURE);
	}
	while (1) {
		// Check wether message is complete
		switch (is_complete(pbuf, bytes, fd, &tail_size)) {
		case MSG_WRONG:
			close(fd);
			FD_CLR(fd, fds);
			return 0;
		case MSG_INCOMPLETE:
			return 0;
		case MSG_COMPLETE:
			tail = pbuf + bytes - tail_size;
			pbuf = tail;
			bytes = tail_size;
			break;
		}
		cmd = client_states[fd].client_msg;
		cmd_len = client_states[fd].cur_size;

		print_client_msg(cmd, cmd_len);

		client_states[fd].cur_size = 0;
		if (client_states[fd].state == EXPECT_USERNAME) {
			int textlen;

			// Check that client send CLIENT_USERNAME message
			if (!decode_client_text(cmd, cmd_len, USERNAME_MAXLEN, &textlen)){
				// Not CLIENT_USERNAME
				printf("Not client username\n");
				rc = write(fd, SERVER_SYNTAX_ERROR, strlen(SERVER_SYNTAX_ERROR));
				if (rc != strlen(SERVER_SYNTAX_ERROR)) {
					close(fd);
					FD_CLR(fd, fds);
					return 0;
				}
				close(fd);
				FD_CLR(fd, fds);
				return 0;
			}
			client_states[fd].namelen = textlen;
			memcpy(client_states[fd].name, cmd, textlen);
			rc = write(fd, SERVER_KEY_REQUEST, strlen(SERVER_KEY_REQUEST));
			if (rc != strlen(SERVER_KEY_REQUEST)) {
				close(fd);
				FD_CLR(fd, fds);
				return 0;
			}
			client_states[fd].state = EXPECT_KEY_ID;
			print_client_msg(client_states[fd].name, textlen);
			continue;
		}

		if (client_states[fd].state == EXPECT_KEY_ID) {
			int hash;
			int key_id;
			char tmp[128];

			if (!decode_client_keyid_confirm(cmd, 999, &key_id)) {
				// Not CLIENT_KEY_ID
				close(fd);
				FD_CLR(fd, fds);
				return 0;
			}
			if (0 > key_id || key_id > 4) {
				// Key_id out of range
				rc = write(fd, SERVER_KEY_OUT_OF_RANGE_ERROR, strlen(SERVER_KEY_OUT_OF_RANGE_ERROR));
				if (rc != strlen(SERVER_KEY_OUT_OF_RANGE_ERROR)) {
					close(fd);
					FD_CLR(fd, fds);
					return 0;
				}
				close(fd);
				FD_CLR(fd, fds);
				return 0;
			}
		
			// Compose reply to the client
			hash = get_hash(client_states[fd].name, client_states[fd].namelen);
			hash += authentification_keys[key_id].server_key;
			hash %= 65536;
			sprintf(tmp, "%d\a\b", hash);
			rc = write(fd, tmp, strlen(tmp));
			if (rc != strlen(tmp)) {
				close(fd);
				FD_CLR(fd, fds);
				return 0;
			}
			client_states[fd].state = EXPECT_CONFIRMATION;
			client_states[fd].keyid = key_id;

			continue;
		}

		if (client_states[fd].state == EXPECT_CONFIRMATION) {
			int code;
			char* tmp;

			if (!decode_client_keyid_confirm(cmd, 65535, &code)) {
				// Not CLIENT_CONFIRMATION
				close(fd);
				FD_CLR(fd, fds);
				return 0;
			}
			// Check confirmation code: restore hash value
			code += 65536;
			code -= authentification_keys[client_states[fd].keyid].client_key;
			code %= 65536;
		
			if (code != get_hash(client_states[fd].name, client_states[fd].namelen)) {
				// confirmation code is wrong
				tmp = SERVER_LOGIN_FAILED;
				rc = write(fd, tmp, strlen(tmp));
				if (rc != strlen(tmp)) {
					close(fd);
					FD_CLR(fd, fds);
					return 0;
				}
				// Close connection
				close(fd);
				FD_CLR(fd, fds);
				return 0;
			}
			tmp = SERVER_OK;
			rc = write(fd, tmp, strlen(tmp));
			if (rc != strlen(tmp)) {
				close(fd);
				FD_CLR(fd, fds);
				return 0;
			}
			// Initialize unknown position and orientation
			client_states[fd].x = X_UNKNOWN;
			client_states[fd].y = Y_UNKNOWN;
			client_states[fd].direction = DIRECTION_UNKNOWN;
		
			// Send first of moves to detect current location
			rc = write(fd, SERVER_MOVE, strlen(SERVER_MOVE));
			if (rc != strlen(SERVER_MOVE)) {
				close(fd);
				FD_CLR(fd, fds);
				return 0;
			}
			client_states[fd].was_move = 1;
			client_states[fd].state = EXPECT_CLIENT_OK;
      
			continue;		
		}
		if (client_states[fd].state == EXPECT_CLIENT_OK) {
			// Client response to MOVE and ROTATE is recieved
			int x;
			int y;

			if (!decode_client_ok(cmd, &x, &y)) {
				// Not CLIENT_OK
				close(fd);
				FD_CLR(fd, fds);
				return 0;
			}
			if (x == 0 && y == 0) {
				// Target is reached
				rc = write(fd, SERVER_PICK_UP, strlen(SERVER_PICK_UP));
				if (rc != strlen(SERVER_PICK_UP)) {
					close(fd);
					FD_CLR(fd, fds);
					return 0;
				}
				client_states[fd].state = EXPECT_CLIENT_MSG;

				continue;
			}
			if (client_states[fd].x == X_UNKNOWN) {
				// Position and orientation were unknown, now pos is known
				client_states[fd].x = x;
				client_states[fd].y = y;
				rc = write(fd, SERVER_MOVE, strlen(SERVER_MOVE));
				if (rc != strlen(SERVER_MOVE)){
					close(fd);
					FD_CLR(fd, fds);
					return 0;
				}
				client_states[fd].was_move = 1;
				// State remains EXPECT_CLIENT_OK
				
				continue;
			}
			if (client_states[fd].direction == DIRECTION_UNKNOWN) {
				// Position is known, orientation is not
				if (client_states[fd].x == x && client_states[fd].y == y) {
                                        // Move did not change position
					if (client_states[fd].did_turn == 0) {
						rc = write(fd, SERVER_TURN_RIGHT, strlen(SERVER_TURN_RIGHT));
						if (rc != strlen(SERVER_TURN_RIGHT)) {
							close(fd);
							FD_CLR(fd, fds);
							return 0;
						}
						client_states[fd].did_turn = 1;
						client_states[fd].was_move = 0;
					} else {
						rc = write(fd, SERVER_MOVE, strlen(SERVER_MOVE));
						if (rc != strlen(SERVER_MOVE)) {
							close(fd);
							FD_CLR(fd, fds);
							return 0;
						}
						client_states[fd].was_move = 1;
					}
					
					// State remains EXPE
//					printf("x = %d, y = %d\n", x, y);
					continue;
				}
				if (client_states[fd].x == x) {
					// Robot orientation is vertical
					if (client_states[fd].y < y) {
						// Direction is up
						client_states[fd].direction = DIRECTION_UP;
					} else {
						// Direction is down
						client_states[fd].direction = DIRECTION_DOWN;
					}
				}
				if (client_states[fd].y == y) {
					// Robot orientation is horizontal
					if (client_states[fd].x < x) {
						// Direction is right
						client_states[fd].direction = DIRECTION_RIGHT;
					} else {
						// Direction is left
						client_states[fd].direction = DIRECTION_LEFT;
					}
				}
			}
			printf("x = %d, y = %d\n", x, y);
			if (client_states[fd].x == x && client_states[fd].y == y &&
			    client_states[fd].was_move && !client_states[fd].in_bypass) {
				// Stuck on obstacle
				
				switch (client_states[fd].direction) {
				case DIRECTION_RIGHT:
					if (y > 0) {
						start_bypass_turn_right(fd);
					} else {
						start_bypass_turn_left(fd);
					}
					break;
				case DIRECTION_LEFT:
					if (y > 0) {
						start_bypass_turn_left(fd);
					} else {
						start_bypass_turn_right(fd);
					}
					break;
				case DIRECTION_UP:
					if (x > 0) {
						start_bypass_turn_left(fd);
					} else {
						start_bypass_turn_right(fd);
					}
					break;
				case DIRECTION_DOWN:
					if (x > 0) {
						start_bypass_turn_right(fd);
					} else {
						start_bypass_turn_left(fd);
					}
					break;
				}
			}
			
			// Update coordinates
			client_states[fd].x = x;
			client_states[fd].y = y;
			if (client_states[fd].in_bypass) {
				// Continue process of bypassing
				char* c = client_states[fd].bypass_cmds[client_states[fd].bypass_cmd];
				printf("Bypass_cmd: %s\n", c);
				rc = write(fd, c, strlen(c));
				if (rc != strlen(c)) {
					close(fd);
					FD_CLR(fd, fds);
					return 0;
				}
				client_states[fd].bypass_cmd++;
				if (client_states[fd].bypass_cmd == 8) {
					client_states[fd].in_bypass = 0;
					client_states[fd].was_move = 0;
				}
				continue;
			}
			if (next_step(fd) != 0) {
				close(fd);
				FD_CLR(fd, fds);
				return 0;
			}
		
			printf("Client ok %d %d\n", x, y);
			
			continue;
		}
		if (client_states[fd].state == EXPECT_CLIENT_MSG) {
			int textlen;

			if (!decode_client_text(cmd, cmd_len, CLIENTMSG_MAXLEN, &textlen)) {
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

	}

	// Unknown state
	printf("Should not be here\n");
	close(fd);
	FD_CLR(fd, fds);
	return 0;
}


int main(void)
{
	struct timeval timeout;
	int socket_fd;
	int rc;
	fd_set fds;
	fd_set selectfds;
	int i;
	

	socket_fd = start_connect_socket(5555);
	printf("%d\n", socket_fd);
	
	FD_ZERO(&fds);
	/* initially fd set contains only connect socket */
	FD_SET(socket_fd, &fds);
	while (1) {
		/* reinitialize selectfds with active sockets */
		selectfds = fds;
		/* wait until input arrives on sockets in selectfds set */
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		rc = select(FD_SETSIZE, &selectfds, NULL, NULL, &timeout);
		if (rc == -1) {
			perror("select failed");
			return 1;
		}
		if (rc == 0) {
			// Select timed out, close all connections except socket_fd
			for (i = 0; i < FD_SETSIZE; i++) {
				if (!FD_ISSET(i, &fds) || i == socket_fd) {
					continue;
				}
				close(i);
				FD_CLR(i, &fds);
			}
			continue;
		}

		/* check all sockets for which input is pending for processing */
		for (i = 0; i < FD_SETSIZE; i++) {
			if (!FD_ISSET (i, &selectfds))
				continue;
			
			if (i == socket_fd) {
				int fd;

                                /* connect request */
				fd = handle_connect(socket_fd);
				FD_SET(fd, &fds);
				memset(&client_states[fd], 0, sizeof(client_states[fd]));
				client_states[fd].state = EXPECT_USERNAME;
				continue;
			}

			process_client_msg(i, &fds);
		}
	}
	/* no way to get here */
	close(socket_fd);
	return 0;
}


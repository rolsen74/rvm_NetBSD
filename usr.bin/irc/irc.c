/* Small, plain-text IRC client for testing the RVM network stack. */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>

#include <netdb.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LINE_SIZE 1024

static int
send_line(int fd, const char *line)
{
	char wire[LINE_SIZE + 2];
	size_t len, done;
	ssize_t n;

	len = strlen(line);
	if (len > 510) {
		fprintf(stderr, "IRC line is too long\n");
		return -1;
	}
	memcpy(wire, line, len);
	wire[len++] = '\r';
	wire[len++] = '\n';
	for (done = 0; done < len; done += (size_t)n) {
		n = send(fd, wire + done, len - done, 0);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0) {
			perror("send");
			return -1;
		}
	}
	return 0;
}

static int
connect_server(const char *host, const char *port)
{
	struct addrinfo hints, *addresses, *ai;
	int fd, error;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	error = getaddrinfo(host, port, &hints, &addresses);
	if (error != 0) {
		fprintf(stderr, "%s: %s\n", host, gai_strerror(error));
		return -1;
	}
	fd = -1;
	for (ai = addresses; ai != NULL; ai = ai->ai_next) {
		fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (fd < 0)
			continue;
		if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
			break;
		close(fd);
		fd = -1;
	}
	freeaddrinfo(addresses);
	if (fd < 0)
		perror(host);
	return fd;
}

static int
server_line(int fd, const char *line, const char *channel, const char *key,
    int *joined)
{
	const char *ping;
	char reply[LINE_SIZE];

	puts(line);
	fflush(stdout);
	ping = line;
	if (*ping == ':') {
		ping = strchr(ping, ' ');
		if (ping == NULL)
			return 0;
		ping++;
	}
	if (strncmp(ping, "PING ", 5) == 0) {
		if (snprintf(reply, sizeof(reply), "PONG %s", ping + 5) >=
		    (int)sizeof(reply))
			return -1;
		return send_line(fd, reply);
	}
	/* Join only after registration succeeds. */
	if (channel != NULL && !*joined && strncmp(ping, "001 ", 4) == 0) {
		*joined = 1;
		if (snprintf(reply, sizeof(reply), "JOIN %s%s%s", channel,
		    key != NULL ? " " : "", key != NULL ? key : "") >=
		    (int)sizeof(reply))
			return -1;
		return send_line(fd, reply);
	}
	return 0;
}

static int
input_line(int fd, char *line, char *target, size_t target_size)
{
	char wire[LINE_SIZE];
	char *space;

	if (strcmp(line, "/quit") == 0)
		return send_line(fd, "QUIT :Leaving") == 0 ? 1 : -1;
	if (strncmp(line, "/join ", 6) == 0) {
		size_t len;

		if (line[6] != '#' || line[7] == '\0') {
			fprintf(stderr, "Expected /join #channel\n");
			return 0;
		}
		space = strpbrk(line + 6, " \t");
		len = space != NULL ? (size_t)(space - (line + 6)) : strlen(line + 6);
		if (len < 2 || len >= target_size)
			return -1;
		memcpy(target, line + 6, len);
		target[len] = '\0';
		if (snprintf(wire, sizeof(wire), "JOIN %s", line + 6) >=
		    (int)sizeof(wire))
			return -1;
		return send_line(fd, wire);
	}
	if (strncmp(line, "/nick ", 6) == 0) {
		if (snprintf(wire, sizeof(wire), "NICK %s", line + 6) >=
		    (int)sizeof(wire))
			return -1;
		return send_line(fd, wire);
	}
	if (strncmp(line, "/raw ", 5) == 0)
		return send_line(fd, line + 5);
	if (strncmp(line, "/msg ", 5) == 0) {
		space = strchr(line + 5, ' ');
		if (space == NULL || space[1] == '\0')
			return 0;
		*space++ = '\0';
		if (snprintf(wire, sizeof(wire), "PRIVMSG %s :%s", line + 5,
		    space) >= (int)sizeof(wire))
			return -1;
		return send_line(fd, wire);
	}
	if (line[0] == '/') {
		fprintf(stderr, "Commands: /join /msg /nick /raw /quit\n");
		return 0;
	}
	if (target[0] == '\0') {
		fprintf(stderr, "Join a channel or use /msg target text\n");
		return 0;
	}
	if (snprintf(wire, sizeof(wire), "PRIVMSG %s :%s", target, line) >=
	    (int)sizeof(wire))
		return -1;
	return send_line(fd, wire);
}

int
main(int argc, char **argv)
{
	char incoming[LINE_SIZE], input[LINE_SIZE], target[128], command[LINE_SIZE];
	const char *port, *nick, *channel, *key;
	size_t used, input_used;
	ssize_t count;
	fd_set reads;
	int fd, joined, rc, i, stdin_open;

	if (argc < 2 || argc > 6) {
		fprintf(stderr, "usage: irc server [port [nick [channel [key]]]]\n");
		return 2;
	}
	port = argc > 2 ? argv[2] : "6667";
	nick = argc > 3 ? argv[3] : "rvm";
	channel = argc > 4 ? argv[4] : NULL;
	key = argc > 5 ? argv[5] : NULL;
	if (strchr(nick, ' ') != NULL || strchr(nick, '\r') != NULL ||
	    strchr(nick, '\n') != NULL || nick[0] == '\0' ||
	    (channel != NULL && (channel[0] != '#' || channel[1] == '\0' ||
	    strlen(channel) >= sizeof(target) || strchr(channel, ' ') != NULL ||
	    strchr(channel, '\r') != NULL || strchr(channel, '\n') != NULL)) ||
	    (key != NULL && (key[0] == '\0' || strchr(key, ' ') != NULL ||
	    strchr(key, '\r') != NULL || strchr(key, '\n') != NULL))) {
		fprintf(stderr, "Invalid nickname or channel\n");
		return 2;
	}
	fd = connect_server(argv[1], port);
	if (fd < 0)
		return 1;
	(void)signal(SIGPIPE, SIG_IGN);
	if (snprintf(command, sizeof(command), "NICK %s", nick) >=
	    (int)sizeof(command) || send_line(fd, command) < 0 ||
	    snprintf(command, sizeof(command), "USER %s 0 * :RVM test client",
	    nick) >= (int)sizeof(command) || send_line(fd, command) < 0) {
		close(fd);
		return 1;
	}
	used = input_used = 0;
	joined = 0;
	stdin_open = 1;
	target[0] = '\0';
	if (channel != NULL)
		(void)snprintf(target, sizeof(target), "%s", channel);
	for (;;) {
		FD_ZERO(&reads);
		FD_SET(fd, &reads);
		if (stdin_open)
			FD_SET(STDIN_FILENO, &reads);
		if (select(fd + 1, &reads, NULL, NULL, NULL) < 0) {
			if (errno == EINTR)
				continue;
			perror("select");
			break;
		}
		if (FD_ISSET(fd, &reads)) {
			count = recv(fd, incoming, sizeof(incoming), 0);
			if (count <= 0) {
				if (count < 0) perror("recv");
				else puts("Disconnected");
				break;
			}
			for (i = 0; i < count; i++) {
				if (incoming[i] == '\n') {
					if (used != 0 && input[used - 1] == '\r') used--;
					input[used] = '\0';
					if (server_line(fd, input, channel, key, &joined) < 0)
						goto done;
					used = 0;
				} else if (used < sizeof(input) - 1) {
					input[used++] = incoming[i];
				} else {
					fprintf(stderr, "Server line too long\n");
					goto done;
				}
			}
		}
		if (stdin_open && FD_ISSET(STDIN_FILENO, &reads)) {
			count = read(STDIN_FILENO, incoming, sizeof(incoming));
			if (count <= 0) {
				stdin_open = 0;
				continue;
			}
			for (i = 0; i < count; i++) {
				if (incoming[i] == '\n') {
					if (input_used != 0 &&
					    command[input_used - 1] == '\r') input_used--;
					command[input_used] = '\0';
					rc = input_line(fd, command, target, sizeof(target));
					if (rc != 0) goto done;
					input_used = 0;
				} else if (input_used < sizeof(command) - 1) {
					command[input_used++] = incoming[i];
				} else {
					fprintf(stderr, "Input line too long\n");
					goto done;
				}
			}
		}
	}
done:
	close(fd);
	return 0;
}

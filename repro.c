#include <arpa/inet.h>
#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>


#define pr_perror(fmt, ...) ({ fprintf(stderr, "%s:%d: " fmt " : %m\n", __func__, __LINE__, ##__VA_ARGS__); 1; })


void print_usage(FILE *stream, char *name)
{
	fprintf(stream, "Usage: [options] %s <dst addr> <port>\n", name);
	fprintf(stream, "\n");
	fprintf(stream, "options:\n");
	fprintf(stream, "-h, --help Show this help message and exit.\n");
}


int main(int argc, char **argv)
{
	struct addrinfo *gai_res, gai_hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
		.ai_protocol = 0,
		.ai_flags = 0,
	};
	struct sockaddr_in addr;
	socklen_t addrlen;
	struct {
		struct in_addr src;
		in_port_t sport;
		struct in_addr dst;
		in_port_t dport;
		unsigned int seq;
		unsigned int ack_seq;
	} ci;
	char *node, *service;
	char addr_buf[INET_ADDRSTRLEN];
	int optval;
	socklen_t optlen;
	int sfd;
	int retval;

	/* parse args */
	while (true) {
		static struct option long_options[] = {
			{"help", no_argument, NULL, 'h'},
			{NULL, 0, NULL, 0}
		};
		int c;

		c = getopt_long(argc, argv, "h", long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			print_usage(stdout, argv[0]);
			return 0;

		case '?':
			fprintf(stderr, "getopt_long error\n");
			print_usage(stderr, argv[0]);
			return 1;
		}
	}

	node = argv[optind++];
	service = argv[optind++];

	if (optind != argc) {
		fprintf(stderr, "wrong number of arguments\n");
		print_usage(stderr, argv[0]);
		return 1;
	}

	/* lookup target */
	retval = getaddrinfo(node, service, &gai_hints, &gai_res);
	if (retval != 0) {
		fprintf(stderr, "getaddrinfo failed: %s\n",
			gai_strerror(retval));
		return 1;
	}
	memcpy(&addr, gai_res->ai_addr, gai_res->ai_addrlen);
	freeaddrinfo(gai_res);

	/* create and connect socket */
	sfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sfd == -1) {
		return pr_perror("socket");
	}
	if (connect(sfd, (struct sockaddr *)&addr, sizeof(addr))) {
		return pr_perror("connect");
	}

	/* get connection information */
	addrlen = sizeof(addr);
	if (getsockname(sfd, (struct sockaddr *)&addr, &addrlen)) {
		return pr_perror("getsockname");
	}
	ci.src = addr.sin_addr;
	ci.sport = addr.sin_port;

	if (inet_ntop(addr.sin_family, &addr.sin_addr, addr_buf,
		      sizeof(addr_buf)) == NULL) {
		return pr_perror("inet_ntop");
	}
	printf("local: %s %u\n", addr_buf, ntohs(addr.sin_port));

	addrlen = sizeof(addr);
	if (getpeername(sfd, (struct sockaddr *)&addr, &addrlen)) {
		return pr_perror("getpeername");
	}
	ci.dst = addr.sin_addr;
	ci.dport = addr.sin_port;

	if (inet_ntop(addr.sin_family, &addr.sin_addr, addr_buf,
			 sizeof(addr_buf)) == NULL) {
		return pr_perror("inet_ntop");
	}
	printf("remote: %s %u\n", addr_buf, ntohs(addr.sin_port));

	optval = 1;
	if (setsockopt(sfd, SOL_TCP, TCP_REPAIR, &optval, sizeof(optval)) ==
	    -1) {
		return pr_perror("setsockopt TCP_REPAIR");
	}

	optval = TCP_SEND_QUEUE;
	if (setsockopt(sfd, SOL_TCP, TCP_REPAIR_QUEUE, &optval,
		       sizeof(optval)) == -1) {
		return pr_perror("setsockopt TCP_REPAIR_QUEUE/TCP_SEND_QUEUE");
	}

	optlen = sizeof(optval);
	if (getsockopt(sfd, SOL_TCP, TCP_QUEUE_SEQ, &optval, &optlen)) {
		return pr_perror("getsockopt TCP_SEND_QUEUE/TCP_QUEUE_SEQ");
	}
	ci.seq = optval;

	optval = TCP_RECV_QUEUE;
	if (setsockopt(sfd, SOL_TCP, TCP_REPAIR_QUEUE, &optval,
		       sizeof(optval)) == -1) {
		return pr_perror("setsockopt TCP_REPAIR_QUEUE/TCP_RECV_QUEUE");
	}

	optlen = sizeof(optval);
	if (getsockopt(sfd, SOL_TCP, TCP_QUEUE_SEQ, &optval, &optlen)) {
		return pr_perror("getsockopt TCP_RECV_QUEUE/TCP_QUEUE_SEQ");
	}
	ci.ack_seq = optval;

	printf("seq %u ack_seq %u\n", ci.seq, ci.ack_seq);

	close(sfd);

	/* create another socket and restore parameters from first connection */
	sfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sfd == -1) {
		return pr_perror("socket");
	}

	optval = 1;
	if (setsockopt(sfd, SOL_TCP, TCP_REPAIR, &optval, sizeof(optval)) ==
	    -1) {
		return pr_perror("setsockopt TCP_REPAIR");
	}

	optval = TCP_SEND_QUEUE;
	if (setsockopt(sfd, SOL_TCP, TCP_REPAIR_QUEUE, &optval,
		       sizeof(optval)) == -1) {
		return pr_perror("setsockopt TCP_REPAIR_QUEUE/TCP_SEND_QUEUE");
	}

	optval = ci.seq;
	if (setsockopt(sfd, SOL_TCP, TCP_QUEUE_SEQ, &optval, sizeof(optval))
	    == -1) {
		return pr_perror("setsockopt TCP_SEND_QUEUE/TCP_QUEUE_SEQ");
	}

	optval = TCP_RECV_QUEUE;
	if (setsockopt(sfd, SOL_TCP, TCP_REPAIR_QUEUE, &optval,
		       sizeof(optval)) == -1) {
		return pr_perror("setsockopt TCP_REPAIR_QUEUE/TCP_RECV_QUEUE");
	}

	optval = ci.ack_seq;
	if (setsockopt(sfd, SOL_TCP, TCP_QUEUE_SEQ, &optval, sizeof(optval))
	    == -1) {
		return pr_perror("setsockopt TCP_RECV_QUEUE/TCP_QUEUE_SEQ");
	}

	addr.sin_family = AF_INET;
	addr.sin_port = ci.sport;
	addr.sin_addr = ci.src;
	if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr))) {
		return pr_perror("bind");
	}

	optval = 0;
	if (setsockopt(sfd, SOL_TCP, TCP_REPAIR, &optval, sizeof(optval)) ==
	    -1) {
		return pr_perror("setsockopt TCP_REPAIR");
	}

	/* send duplicate syn */
	addr.sin_family = AF_INET;
	addr.sin_port = ci.dport;
	addr.sin_addr = ci.dst;
	if (connect(sfd, (struct sockaddr *)&addr, sizeof(addr))) {
		return pr_perror("connect");
	}

	pause();

	return 0;
}

#include "serial.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>


#define BUF_SIZE		1024
#define LOG_BUF_SIZE	256

#define FLAG_OK_WAITING	0x01


int socket_fd;
FILE *dbgf;


void log_(const char *fmt, ...)
{
	va_list args;
	
	va_start(args, fmt);
	vfprintf(dbgf, fmt, args);
	va_end(args);
	
	fflush(dbgf);
}

void print_buf(const char *pref, char *buf, int len)
{
	char buf2[BUF_SIZE];
	
	memcpy(buf2, buf, len);
	buf2[len] = 0;
	int i;
	for (i=0; i<len; i++) {
		if (buf2[i] == '\n')
			buf2[i] = '^';
		else if (buf2[i] == '\r')
			buf2[i] = '~';
	}
	log_("%s", pref);
	log_("'%s'\n", buf2);
}

int buf_read(char *buf, char **beg, char **end, int (*read_func)(char *, int))
{
	int n, i;
	char *p;
	
beg_label:
	for (p = *beg; p < *end; p++) {
		if (*p == '\n') {
			n = p - *beg + 1;
			return n;
		}
	}
	
	if (*end - *beg >= BUF_SIZE) {
		*beg = buf;
		*end = buf;
		//buf len error
		log_("len error!!!\n");
	} else if (*beg != buf) {
		n = *end - *beg;
		for (i = 0; i < n; i++) {
			buf[i] = (*beg)[i];
		}
		*beg = buf;
		*end = buf + n;
	}
	
	n = read_func(*end, BUF_SIZE - (*end - *beg));
	if (n > 0) {
		*end += n;
		goto beg_label;
		return 0;
	}
	
	return -1;
}

int server_read(char *buf, int len)
{
	return recv(socket_fd, buf, len, 0);
}

int server_start(const char *serial_dev, speed_t serial_speed, struct in_addr ip, int port, int flags)
{
	fd_set fds;
	int listen_fd, serial_fd;
	int maxfd, n, ret;
	int ok;
	int yes = 1;
	int wb_cnt = 0;
	char ser_buf[BUF_SIZE], *ser_beg, *ser_end;
	char net_buf[BUF_SIZE], *net_beg, *net_end;
	struct sockaddr_in addr;
	
	listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	
	if (listen_fd < 0) {
		log_("socket error\n");
		return -1;
	}
	
	log_("start\n");

	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
	
	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr = ip;
	addr.sin_port = htons(port);
	if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		log_("bind error\n");
		goto sock_end;
	}
	
	if (listen(listen_fd, 1) != 0) {
		log_("listen error\n");
		goto sock_end;
	}
	
	while (1) {
		socket_fd = accept(listen_fd, NULL, NULL);
		
		if (socket_fd < 0)
			continue;
		
		ret = fcntl(socket_fd, F_GETFL, 0);
		if (fcntl(socket_fd, F_SETFL, ret|O_NONBLOCK) < 0) {
			log_("fcntl error\n");
			goto conn_end;
		}
		
		serial_fd = serial_connect(serial_dev, serial_speed);
		if (serial_fd < 0) {
			log_("serial connection failed\n");
			goto conn_end;
		}
		
		log_("connected\n");
		
		maxfd = (serial_fd > socket_fd) ? serial_fd : socket_fd;
		
		ser_beg = ser_end = ser_buf;
		net_beg = net_end = net_buf;
		
		FD_ZERO(&fds);
		
		ok = 0;
		
		while (1) {
			FD_SET(socket_fd, &fds);
			FD_SET(serial_fd, &fds);
			
			ret = select(maxfd + 1, &fds, NULL, NULL, NULL);
			
			if (ret > 0) {
				if (FD_ISSET(serial_fd, &fds)) {
					while ((n = buf_read(ser_buf, &ser_beg, &ser_end, serial_read)) > 0) {
						print_buf(">>>", ser_beg, n);
						send(socket_fd, ser_beg, n, 0);
						if (strncmp(ser_beg, "ok", 2) == 0 || strncmp(ser_beg, "start", 5) == 0)
							ok = 1;
						ser_beg += n;
					}
					if (n == 0) {
						goto conn_end;
					} else if (n == -1 && errno != EAGAIN) {
						log_("serial reading error %d\n", errno);
						goto conn_end;
					}
				}
				if (FD_ISSET(socket_fd, &fds)) {
					if (!ok && (flags & FLAG_OK_WAITING)) {
						usleep(10000);
						continue;
					}
					
					n = buf_read(net_buf, &net_beg, &net_end, server_read);
					if (n > 0) {
						print_buf("<<<", net_beg, n);
						serial_send(net_beg, n);
						net_beg += n;
						ok = 0;
						wb_cnt = 0;
					} else if (n == 0) {
						goto conn_end;
					} else if (n == -1) {
						if (errno == EWOULDBLOCK) {
							wb_cnt++;	
							if (wb_cnt > 16)   // it's workaround for closing the connection after an other side disconnected
								goto conn_end; // for some reason recv keeps on EWOULDBLOCK answer after disconnecting
						} else {
							log_("server reading error %d\n", errno);
							goto conn_end;
						}
					}
				}
			} else {
				goto conn_end;
			}
		}
conn_end:
		log_("close connection\n");
		serial_disconnect();
		close(socket_fd);
	}
	
sock_end:
	close(listen_fd);
	
	return 0;
}

void print_usage()
{
	printf("Usage:\n");
	printf("\t$ 3dps --dev /dev/ttyACM0 --speed 115200 --port 666 [--ip 192.168.0.1] [--dbgfile] [--okwait]\n");
}

int main(int argc, char * argv[])
{
	int i, port = 0, flags = 0;
	speed_t speed = 0;
	const char *dev = NULL;
	struct in_addr ip;
	
	dbgf = stdout;
	
	ip.s_addr = htonl(INADDR_ANY);
	
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--dev") == 0) {
			if (i+1 >= argc)
				goto arg_error;
			dev = argv[i+1];
			i++;
		} else if (strcmp(argv[i], "--speed") == 0) {
			if (i+1 >= argc)
				goto arg_error;
			speed = serial_translate_speed(atoi(argv[i+1]));
			if (speed == 0) {
				log_("speed value error\n");
				return -1;
			}
			i++;
		} else if (strcmp(argv[i], "--port") == 0) {
			if (i+1 >= argc)
				goto arg_error;
			port = atoi(argv[i+1]);
			i++;
		} else if (strcmp(argv[i], "--ip") == 0) {
			if (i+1 >= argc)
				goto arg_error;
			if (inet_pton(AF_INET, argv[i+1], &ip) != 1) {
				log_("invalid ip address\n");
				return -1;
			}
			i++;
		} else if (strcmp(argv[i], "--dbgfile") == 0) {
			if (i+1 >= argc)
				goto arg_error;
			
			dbgf = fopen(argv[i+1], "w");
			if (!dbgf)
				dbgf = stdout;
			i++;
		} else if (strcmp(argv[i], "--okwait") == 0) {
			flags |= FLAG_OK_WAITING;
		} else
			goto arg_error;
	}
	
	if (!(dev && speed && port))
		goto arg_error;
	
	server_start(dev, speed, ip, port, flags);
	
	fclose(dbgf);
	
	return 0;
	
arg_error:
	print_usage();
	return -1;
	
}

#include "serial.h"

#include <stdio.h>
#include <fcntl.h>


struct speed_kv {
	int key;
	speed_t val;
};


int fd = -1;


int serial_connect(const char *dev, speed_t speed)
{
	struct termios port_settings;
	
	fd = open(dev, O_RDWR | O_NOCTTY | O_NDELAY);
	
	if (fd < 0)
		return -1;
	
	fcntl(fd, F_SETFL, O_NONBLOCK);
	
	cfsetispeed(&port_settings, speed);
	cfsetospeed(&port_settings, speed);
	
	port_settings.c_cflag &= ~PARENB;
	port_settings.c_cflag &= ~CSTOPB;
	port_settings.c_cflag &= ~CSIZE;
	port_settings.c_cflag |= CS8;
	
	cfmakeraw(&port_settings);
	tcsetattr(fd, TCSANOW, &port_settings);
	
	return fd;
}

int serial_disconnect()
{
	if (fd >= 0)
		close(fd);
	fd = -1;
}

int serial_read(char *buf, int len)
{
	if (fd < 0)
		return -1;
	return read(fd, buf, len);
}

int serial_send(const char *data, int len)
{
	if (fd < 0)
		return -1;
	return write(fd, data, len);
}

speed_t serial_translate_speed(int speed)
{
	struct speed_kv skv_arr[] = {{57600, B57600},
								{115200, B115200},
								{230400, B230400},
								{460800, B460800},
								{500000, B500000},
								{576000, B576000},
								{921600, B921600},
								{0, 0}};
	struct speed_kv *skv;
	
	for (skv = skv_arr; skv->key > 0; skv++)
		if (skv->key == speed)
			return skv->val;
	
	return 0;
}


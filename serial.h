#ifndef _PRINT3D_SERIAL_H_
#define _PRINT3D_SERIAL_H_


#include <termios.h>


int serial_connect(const char *dev, speed_t speed);
int serial_disconnect();
int serial_send(const char *data, int len);
int serial_read(char *buf, int len);
speed_t serial_translate_speed(int speed);


#endif

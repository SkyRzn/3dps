
CC = gcc

all: 3dps

3dps: main.o serial.o
	$(CC) main.o serial.o -o 3dps

main.o:
	$(CC) -Os -c main.c

serial.o:
	$(CC) -Os -c serial.c


uninstall:
clean:
	rm -f *.o 3dps

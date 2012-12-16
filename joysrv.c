/*
 *
 * Gameport joystick proxy server
 *
 */

#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <linux/input.h>
#include <linux/joystick.h>

char *axis_names[ABS_MAX + 1] = {
"X", "Y", "Z", "Rx", "Ry", "Rz", "Throttle", "Rudder", 
"Wheel", "Gas", "Brake", "?", "?", "?", "?", "?",
"Hat0X", "Hat0Y", "Hat1X", "Hat1Y", "Hat2X", "Hat2Y", "Hat3X", "Hat3Y",
"?", "?", "?", "?", "?", "?", "?", 
};

char *button_names[KEY_MAX - BTN_MISC + 1] = {
"Btn0", "Btn1", "Btn2", "Btn3", "Btn4", "Btn5", "Btn6", "Btn7", "Btn8", "Btn9", "?", "?", "?", "?", "?", "?",
"LeftBtn", "RightBtn", "MiddleBtn", "SideBtn", "ExtraBtn", "ForwardBtn", "BackBtn", "TaskBtn", "?", "?", "?", "?", "?", "?", "?", "?",
"Trigger", "ThumbBtn", "ThumbBtn2", "TopBtn", "TopBtn2", "PinkieBtn", "BaseBtn", "BaseBtn2", "BaseBtn3", "BaseBtn4", "BaseBtn5", "BaseBtn6", "BtnDead",
"BtnA", "BtnB", "BtnC", "BtnX", "BtnY", "BtnZ", "BtnTL", "BtnTR", "BtnTL2", "BtnTR2", "BtnSelect", "BtnStart", "BtnMode", "BtnThumbL", "BtnThumbR", "?",
"?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", 
"WheelBtn", "Gear up",
};

#define NAME_LENGTH 128

void sigpipe_handler(int signum)
{
	/* do nothing */
}

int main (int argc, char **argv)
{
	int fd, i;
	unsigned char axes = 2;
	unsigned char buttons = 0;
	int version = 0x000800;
	char name[NAME_LENGTH] = "Pedals";
	uint16_t btnmap[KEY_MAX - BTN_MISC + 1];
	uint8_t axmap[ABS_MAX + 1];
	char *devname = "/dev/input/js0";

	int sockfd = 0, newsockfd = 0;
	uint16_t port = 10987;
	socklen_t clilen;
	struct sockaddr_in serv_addr, cli_addr;
	int n;

	signal(SIGPIPE, sigpipe_handler);

	if ((fd = open(devname, O_RDONLY)) < 0) {
		perror("Can't open device");
		exit(1);
	}

	ioctl(fd, JSIOCGVERSION, &version);
	ioctl(fd, JSIOCGAXES, &axes);
	ioctl(fd, JSIOCGBUTTONS, &buttons);
	ioctl(fd, JSIOCGNAME(NAME_LENGTH), name);
	ioctl(fd, JSIOCGAXMAP, axmap);
	ioctl(fd, JSIOCGBTNMAP, btnmap);


	printf("Driver version is %d.%d.%d.\n",
		version >> 16, (version >> 8) & 0xff, version & 0xff);

	printf("Joystick (%s) has %d axes (", name, axes);
	for (i = 0; i < axes; i++)
		printf("%s%s", i > 0 ? ", " : "", axis_names[axmap[i]]);
	puts(")");

	printf("and %d buttons (", buttons);
	for (i = 0; i < buttons; i++)
		printf("%s%s", i > 0 ? ", " : "", button_names[btnmap[i] - BTN_MISC]);
	puts(").");

	printf("Working ... (interrupt to exit)\n");

	/*
	 * Wait for connection.
	 */

	while(1)
	{
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
		if (sockfd < 0) {
			perror("ERROR opening socket");
			goto fail;
		}

		bzero((char *) &serv_addr, sizeof(serv_addr));
		serv_addr.sin_family = AF_INET;
		serv_addr.sin_addr.s_addr = INADDR_ANY;
		serv_addr.sin_port = htons(port);

		if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
			perror("ERROR on binding");
			goto fail;
		}

		printf("Waiting for connection ...\n");

		listen(sockfd, 1);
		clilen = sizeof(cli_addr);
		newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
		if (newsockfd < 0) {
			perror("ERROR on accept");
			goto fail;
		}

		printf("Got remote connection!\n");

		/* Force no delay */
		int flag = 1;
		int result = setsockopt(newsockfd,		/* socket affected */
								IPPROTO_TCP,	/* set option at TCP level */
								TCP_NODELAY,	/* name of option */
								(char *) &flag,	/* the cast is historical cruft */
								sizeof(int));	/* length of option value */

		/*
		 * Using select() on joystick fd.
		 */

		struct js_event js;
		struct timeval tv;
		fd_set set;
		int16_t data[2] = {0};

		tv.tv_sec = 1;
		tv.tv_usec = 0;

		while (1) {

			FD_ZERO(&set);
			FD_SET(fd, &set);

			if (select(fd+1, &set, NULL, NULL, &tv)) {

				if (read(fd, &js, sizeof(struct js_event)) != sizeof(struct js_event)) {
					perror("\nError reading");
					goto fail;
				}

				//printf("Event: type %d, time %d, number %d, value %d\n",
				//	js.type, js.time, js.number, js.value);

				if (js.number == 0) {
					if (js.value <= 0) {
						data[0] = -js.value;
					} else {
						data[0] = 0;
					}
				}
				else if (js.number == 1) {
					if (js.value <= 0) {
						data[1] = -js.value;
					} else {
						data[1] = 0;
					}
				}

				n = write(newsockfd, data, sizeof(data));
				if (n < 0) {
					perror("\nERROR writing to socket");
					goto fail;
				}
			}
		}

fail:
		if (newsockfd)
			close(newsockfd);
		if (sockfd)
			close(sockfd);

		sleep(5);
	}

	return 0;
}

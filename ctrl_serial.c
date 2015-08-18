/* ctrl_serial is a controller for the plugin Control of lcd4linux.
 *
 * See https://lcd4linux.bulix.org/wiki/plugin_control_ctrl_serial
 *
 * Copyright (C) 2015 Marcus Menzel <codingmax@gmx-topmail.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 * http://www.gnu.org/licenses/gpl-2.0.html
 */
    
#include <fcntl.h>   
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <unistd.h>    
#include <math.h>    
#include <errno.h>
#include <string.h>

#include "util.h"

#define PROG "ctrl_serial"
#define RELEASE PROG " 0.0.1"

typedef struct Settings {        
	char *serPortPath;
	int port;
	int delay;
	int *loopsOut;
	int loopsIn;
	int testmode;
	
} Settings;


static Settings *settings = NULL;


static void free_settings() {
	
	if (settings == NULL)
		return;

	if (settings->loopsOut == NULL)
		free(settings->loopsOut);

	if (settings->port >= 0) {
		close(settings->port);
	}
	
	free(settings);
	settings = NULL;
}

static void my_exit(int retVal) {
	
	free_settings();
	info("Exit.");
	exit(retVal);
}


static void print_info() {
	printf("\n%s\n", RELEASE);
    printf("\n");
    printf("This program is a controller for the plugin 'Control' of lcd4linux.\n");
    printf("\n");
    printf("It reads and writes data from/to a serial port to get the states of\n");
    printf("4 push buttons and set the state of 2 LED groups.\n");
    printf("\n");
    printf("Please visit the wiki for further information.\n");
    printf("\n");
    printf("usage:\n");
    printf("\n");
    printf("%s [options]",PROG);
    printf("\n");
    printf("options:\n");
    printf("  -h              help (this info)\n");
    printf("  -p <path>       path of serial port (e.g. '/dev/tyyS0')\n");
    printf("                  NOT optional\n");
    printf("  -t              testmode\n");
    printf("  -d <delay>      interval between polling 2 loops in milliseconds, default: 10\n");
    printf("  -b <number>     number of polling loops a button state has to be\n");
    printf("                  consant to be regarded. Default: 4\n");
    printf("  -[2-8] <number> number of polling loops a LED in blink mode 2 -8 keeps in\n");
	printf("                  constant state\n");
    printf("\n");
}


static int init_settings() {
   
    settings = malloc(sizeof(Settings));
    if (settings == NULL) {
		noMem();
		return 0;
	}

	settings->loopsOut = malloc(7*sizeof(int));
    if (settings->loopsOut == NULL) {
		noMem();
		return 0;
	}
	int i;
	for (i = 0; i < 7; i++) 
		if (!get_opt_int_between('2'+i, 1, 0, 1000, round(5 * pow(20.,1.*(6-i)/6)), &settings->loopsOut[i]))
			return 0;

    settings->serPortPath = NULL;
	settings->port = -1;
	settings->delay = 10;
	settings->loopsIn = 4;
	settings->testmode = 0;
	    
	if (!get_opt_str('p', 1, &settings->serPortPath))
		return 0;
	
	if (	!get_opt_int_between('d', 1, 1, 1000, settings->delay, &settings->delay)
		||  !get_opt_int_between('b', 1, 1, 1000, settings->loopsIn, &settings->loopsIn)){
	
		return 0;
	}
		
    if (get_opt_str('t', 0, NULL))
		settings->testmode = 1;        

	return 1;
}


static int open_serial_port() {
    settings->port = open(settings->serPortPath, O_RDWR | O_NOCTTY | O_NONBLOCK | O_SYNC );
    if (settings->port == -1) {
		int err = errno;
        error("Can't open serial port '%s': %s.", 
				settings->serPortPath, strerror(err));
        return 0;
    }
    return 1;
}


static int get_serial_data(int *serData) {
	int data = 0;
    if (ioctl(settings->port, TIOCMGET, &data)==-1) {
        error("Can't read from serial port '%s' (TIOCMGET).",settings->serPortPath);
        return 0;
    }
    *serData = data;
    return 1;
}


static int set_serial_data(int data) {
    if (ioctl(settings->port, TIOCMSET, &data)==-1) {
        error("Can't write to serial port '%s' (TIOCMSET).",settings->serPortPath);
        return 0;
    }
    return 1;
}


static int set_txd(int high) {
	
	int tio = high ? TIOCSBRK : TIOCCBRK;
    if (ioctl(settings->port, tio, NULL ) ==-1) {
        error("Can't write to serial port '%s' (%s).",settings->serPortPath,high ? "TIOCSBRK" : "TIOCCBRK");
        return 0;
    }	
    return 1;
}

static int serIn_to_stdout(int data) {

	static int pins[] = { TIOCM_RNG, TIOCM_CTS, TIOCM_DSR,  TIOCM_CD };
    static unsigned char old = 0;
    static unsigned char  oldSent = 0;
    static int cnt = 0;
	static struct pollfd pfd = {1,POLLOUT,0};

	unsigned char val = 0;
	int i = 0;
	
	for (i = 0; i < 4; i++) {
		if (data  & pins[i]) 
			val |= (1<<i);
	}

	/* invert */
	val ^= (data & TIOCM_DTR ? 0x0F : 0);
	
	if (val == old) {
		
		int d = cnt + 1 - settings->loopsIn;
		if (d <= 0) 
			cnt++;
		if (d == 0 && oldSent != val) {
			
			if (!settings->testmode) {
				if ( (poll(&pfd, 1, 0)<=0 )
					 || ((pfd.revents & POLLOUT) == 0 )
					 || (write(1, &val, 1) != 1)) {
						 
					error("Can't write unsigned char '0x%02X' to stdout",val);
					return 0;
				}
			} else {
				printf("Buttons: %s\n",get_multi_base_str(val));
			}
								
			oldSent = val;
		}
	} else {
		cnt = 0;
		old = val;
	}
	return 1;
}


static int stdin_to_serOut(int data) {

	static int firstRun = 1;
	static int serOutStat[] = {0,0};
	static int serOutStatOld[] = {0,0};
	static int serOutMode[] = {0,0};
	static int cntOut[] = {0,0};
	static struct pollfd pfd = {0,POLLIN,0};

	int bufSize = 100;
	unsigned char buf [bufSize];
	
	int i,j;

	if (firstRun && settings->testmode) {
		
		printf("\nTest mode - %s\n\n",RELEASE);

		for (i = 0; i < 7; i++)
			printf("loopsout[%d]: %d\n",i+2,settings->loopsOut[i]);

		printf("\n");

		for (i = 0; i < 2; i++)
			printf("mode LED %d: %d\n",i,serOutMode[0]);
		
		printf("\nPlease press a button connected to the serial port\n");
		printf("or enter 1-2 digits followed by the Return key.\n\n");
	}
	
	int nbIn  = 0;
	
	if (poll(&pfd, 1, 0)>0 && (pfd.revents & POLLIN))
		nbIn = read(0, buf, bufSize);	
		
	if (nbIn < 0)
		return 0;
	
	if (nbIn>0 && settings->testmode) {
		
		int ignore = 0;
		
		if (nbIn >= 2 && nbIn <= 3 && buf[nbIn-1] == '\n') {
			unsigned char tmp = 0;									
			for (i = 0; i< nbIn-1; i++) {
				if (buf[i] >= '0' && buf[i] <= '9')
					tmp = 10 * tmp + (buf[i] - '0');
				else 
					ignore = 1;
			}
			if (!ignore) {
				buf[0] = tmp;
				nbIn = 1;
			}
		} else {
			ignore = 1;
		}	
		if (ignore)	{
			info("ignored:");
			for (i = 0; i< nbIn; i++) 
				if (buf[i]>= 0x20 && buf[i]< 0x7F)
					info("%d: 0x%02x '%c')", i, buf[i], buf[i]);
				else
					info("%d: 0x%02x)", i, buf[i]);
			nbIn = 0;
		}
	}
	
	if (nbIn > 10)
		error("Found at least %d bytes in stdin. => May read slower than writer writes.",nbIn);
	
	int newMode[] = {0,0}; 

	for (i = 0; i < nbIn; i++) {
		
		newMode[0] = buf[i] % 10;
		newMode[1] = (buf[i]/10) % 10;
	
		for (j = 0; j < 2; j++) {
			if (newMode[j] != 9) {
				if (serOutMode[j] != newMode[j] && settings->testmode) 
					info("LED %d set to mode %d.",j,newMode[j]);
				serOutMode[j] = newMode[j];
			}
		}
	} 

	/* sync led f same mode */
	if (serOutMode[0] == serOutMode[1]) {
		serOutStat[0] = serOutStat[1];
		
		if (cntOut[0] > cntOut[1]) 
			cntOut[0] = cntOut[1];
		else
			cntOut[1] = cntOut[0];
	}
	
	for (i = 0; i < 2; i++) {

		if (serOutMode[i] == 0) {
			cntOut[i] = 0;
			serOutStat[i] = 0;
		}
		
		if (serOutMode[i] == 1) {
			cntOut[i] = 0;
			serOutStat[i] = 1;
		}

		if (serOutMode[i] >= 2 && serOutMode[i] <= 8) {
			if (cntOut[i] < settings->loopsOut[serOutMode[i]-2])
				cntOut[i]++;
			else {
				cntOut[i] = 0;
				serOutStat[i] = 1-serOutStat[i];
			}
		}
	}	
	
	int res = 1;
	
	for (i = 0; i<2; i++) {
		if (firstRun || serOutStat[i] != serOutStatOld[i]) {
			serOutStatOld[i] = serOutStat[i];

			if (i==0) {
				if (serOutStat[0]) {
					data |= TIOCM_DTR; 
					data &= ~TIOCM_RTS;
				} else {
					data &= ~TIOCM_DTR;
					data |= TIOCM_RTS;
				}
				if (!set_serial_data(data))
					res = 0;
			}
			
			if (i==1 && !set_txd(serOutStat[1]))
				res = 0;
		}
	}

	firstRun = 0;
	return res;
}


int main(int argc, char *argv[]){
	
	if (!init_util(PROG, argc, argv, ":b:d:hp:t2:3:4:5:6:7:8:"))
		my_exit(EXIT_FAILURE);
    
    if (get_opt_str('h', 0, NULL)) {
		print_info();
		my_exit(EXIT_SUCCESS);
	}

	if (	!init_settings()
		||  !open_serial_port()) {
		
		my_exit(EXIT_FAILURE);
	}
	
	int data;
	
    while (1) {

		if (	!get_serial_data(&data)
			||	!serIn_to_stdout(data)
			||	!stdin_to_serOut(data)) {
					
			my_exit(EXIT_FAILURE);
		}

		if (stopped_by_signal())
			my_exit(EXIT_SUCCESS);

		usleep(settings->delay*1000);
	}
	
	return EXIT_FAILURE;
}

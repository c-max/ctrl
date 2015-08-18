/* ctrl_fifo is a controller for the plugin Control of lcd4linux.
 * 
 * See https://lcd4linux.bulix.org/wiki/plugin_control_ctrl_fifo
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
#include <string.h>
#include <stdlib.h>
#include <unistd.h>    
#include <errno.h>
#include <sys/stat.h>

#include "util.h"

#define PROG "ctrl_fifo"
#define RELEASE PROG " 0.0.1"

typedef struct Settings {        
	char *fifoPath;
	int testmode;
} Settings;

static Settings *settings = NULL;


static void free_settings() {
	
	if (settings == NULL)
		return;
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
    printf("This program is a controller for the plugin Control of lcd4linux.\n");
    printf("It reads data from a fifo and writes to stdout.\n");
    printf("Please visit the wiki for further information.\n");
    printf("\n");
    printf("usage: %s [options]",PROG);
    printf("\n");
    printf("options:\n");
    printf("  -h              help (this info)\n");
    printf("  -p <path>       path of the fifo (e.g. '/tmp/l4l_fifo')\n");
    printf("                  NOT optional\n");
    printf("  -t              testmode\n");
    printf("\n");
}

static int init_settings() {
   
    settings = malloc(sizeof(Settings));
    if (settings == NULL) {
		noMem();
		return 0;
	}

    settings->fifoPath = NULL;
	settings->testmode = 0;
	    
	if (!get_opt_str('p', 1, &settings->fifoPath))
		return 0;
		
    if (get_opt_str('t', 0, NULL))
		settings->testmode = 1;        

	return 1;
}


static int handle_fifo() {
		
	int fd = -1;
	int mkfifoCalled = 0;
	
	while (fd < 0 && !mkfifoCalled) {
		if (settings->testmode)
			printf("\nTry to open fifo '%s'...\n",settings->fifoPath);
		fd = open(settings->fifoPath, O_RDONLY);
		if (fd < 0) {
			int err = errno;
			if (settings->testmode)
				printf("Failed to open fifo: %s.\n",strerror(err));
			if (err == ENOENT && !mkfifoCalled) {
				if (settings->testmode)
					printf("Try to create fifo...\n");
				mkfifoCalled = 1;
				if (mkfifo(settings->fifoPath, 0600) != 0) {
					err = errno;
					error("Couldn't create fifo '%s': %s.",settings->fifoPath, strerror(err));
					return 0;
				} else {
					if (settings->testmode)
						printf("Fifo created.\n");
				}
			} else {
				error("Couldn't open fifo '%s': %s.",settings->fifoPath, strerror(err));
				return 0;
			}			
		}
	}

	if (settings->testmode)
		printf("Fifo opened.\n");

    int bufSize = 100;
    unsigned char buf[bufSize];
    
    int i;
    
    while(1) {
		int nb = read(fd, buf, bufSize);
		
		if (settings->testmode) {
			for (i = 0; i < nb; i++) 
				printf("Byte %3d: %s\n",i,get_multi_base_str(buf[i]));
		} else {
			int pos = 0;
			while (pos < nb) {
				int r = write(1, buf + pos, nb - pos);
				if (r < 0)
					break;
				pos += r;
			}
		}
    
		if (nb <= 0)
			break;
	}    
    
    close(fd);
	if (settings->testmode)
		printf("Fifo closed.\n");
    return 1;
}


static void signalHandler(int sig) {
	my_exit(EXIT_SUCCESS);
}


int main(int argc, char *argv[]){
	
	if (!init_util_sig(PROG, argc, argv, ":hp:t", signalHandler))
		my_exit(EXIT_FAILURE);
    
    if (get_opt_str('h', 0, NULL)) {
		print_info();
		my_exit(EXIT_SUCCESS);
	}

	if (!init_settings()) 
		my_exit(EXIT_FAILURE);
	
	if (settings->testmode)
		printf("\nTest mode - %s\n",RELEASE);
		printf("Please write some bytes to '%s'.\n",settings->fifoPath);

    while (1) {

		if (!handle_fifo()) 
			my_exit(EXIT_FAILURE);
	}
	
	return EXIT_FAILURE;
}

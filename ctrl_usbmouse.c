/* ctrl_usbmouse is a controller for the plugin Control of lcd4linux.
 *
 * See https://lcd4linux.bulix.org/wiki/plugin_control_ctrl_usbmouse
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libusb-1.0/libusb.h>
#include <unistd.h>
#include <errno.h>

#include "util.h"

#define PROG "ctrl_usbmouse"
#define RELEASE PROG " 0.0.1"


typedef struct Settings {
	
	char * id;
	int testMode;
	int buttonIdx;
	int wheelIdx;
	int wheelZero;
	int stop;

	struct libusb_context *ctx;
	libusb_device *device;
	libusb_device_handle *handle;
	int endpoint;
	int byteNb;

} Settings;


static Settings * settings = NULL;


int my_exit(int retVal) {
	
	if (settings == NULL)
		exit(retVal);

	if (settings->handle != NULL) {

		if (libusb_release_interface(settings->handle,0) != 0)
			error("Can't release mouse interface.");

		if (libusb_kernel_driver_active(settings->handle,0) == 0 
			&& libusb_attach_kernel_driver(settings->handle,0) != 0)
			error("Can't attach kernel driver.");

		libusb_close(settings->handle);
	}

	if (settings->ctx != NULL) 
		libusb_exit(settings->ctx);	

	info("Exit done.");
	exit(retVal);
}


static int check_number(char *section, int number) {
		
	if (number <= 0) {
		error("Number of %s (%d) <= 0.", section, number);
		return 1;
	}
	if (number != 1) {
		error("Number of %s (%d) != 1.", section, number);
	}
	return 0;
}


//returns ok 1, else 0 
static int check_device(struct libusb_config_descriptor **configAddr) {

	struct libusb_device_descriptor desc;
	const struct libusb_interface *inter = NULL;
	const struct libusb_interface_descriptor *interdesc = NULL;
	const struct libusb_endpoint_descriptor *epdesc = NULL;
	
	char devId[10];

	if (libusb_get_device_descriptor(settings->device, &desc) != 0) 
		return 0;
	
	snprintf(devId,10,"%04x:%04x",desc.idVendor,desc.idProduct);


	if (strcasecmp(devId,settings->id) != 0) 
		return 0;


	if (check_number("configurations",desc.bNumConfigurations))
		return 0;
	
	
	if (libusb_get_config_descriptor(settings->device, 0, configAddr) != 0){
		error("Can't open config descriptor 0.");
		*configAddr = NULL;
		return 0;
	}

	if (check_number("interfaces",(*configAddr)->bNumInterfaces))
		return 0;
		
	inter = &(*configAddr)->interface[0];

	if (check_number("interface descriptors",inter->num_altsetting))
		return 0;
	
	interdesc = &inter->altsetting[0];

	/*check if it's a mouse */
	if (interdesc->bInterfaceClass != 3 
		|| interdesc->bInterfaceSubClass != 1
		|| interdesc->bInterfaceProtocol !=2) {

		error("Device doesn't seem to be a mouse.");
		return 0;
	}

	if (check_number("endpoints",interdesc->bNumEndpoints))
		return 0;

	epdesc = &interdesc->endpoint[0];
	
	int endpoint = (int)epdesc->bEndpointAddress;

	if (endpoint < 0x80){
		error("Endpoint address (%d) < 0x80 (==> not an input).",endpoint);
		return 0;
	}

	int byteNb = (int)epdesc->wMaxPacketSize;

	if (byteNb < 1 || byteNb > 20){
		error("Byte number (%d) not in [1..20].",byteNb);
		return 0;
	}

	settings->endpoint = endpoint;
	settings->byteNb = byteNb;
	
	return 1;
}


static int init_device() {
	
	if (libusb_init(&settings->ctx) < 0) {
		settings->ctx = NULL;
		error("Cant't init USB context.");
		return 0;
	}
		
	libusb_set_debug(settings->ctx, LIBUSB_LOG_LEVEL_WARNING); 

	libusb_device ** devLst = NULL;
	ssize_t devNb = libusb_get_device_list(settings->ctx, &devLst);

	int i;
	for (i = 0; i< devNb; i++) {

		settings->device = devLst[i];
		struct libusb_config_descriptor *config = NULL;
		int ret = check_device(&config);
		libusb_free_config_descriptor(config);
		if (ret == 1) 
			break;
	}

	if (settings->endpoint < 0x80) {
		error("No mouse with id %s found.",settings->id);
		libusb_free_device_list(devLst,1);
		return 0;
	}

	libusb_set_debug(settings->ctx,  LIBUSB_LOG_LEVEL_NONE); 

	if (libusb_open(settings->device, &settings->handle) != 0) {
		int err = errno;
		error("Can't open mouse device: %s.",strerror(err));
		libusb_free_device_list(devLst,1);
		return 0;
	}

	libusb_set_debug(settings->ctx, LIBUSB_LOG_LEVEL_WARNING); 

	libusb_free_device_list(devLst,1);
		
	return 1;
}


void handle_input() {	
	
	unsigned char buf[settings->byteNb];
	unsigned char valueOld = 0;
	
	int errorMsgLeft = 5;
	
	while (!stopped_by_signal()) {
		
		memset(buf, 0, settings->byteNb);

		int transferred = 0;	
		
        if (libusb_interrupt_transfer(settings->handle, settings->endpoint, buf, 
								settings->byteNb, &transferred , 100) != 0) {

			continue;
		}
		
		if (transferred != settings->byteNb) {
			if (errorMsgLeft > 0) {
				errorMsgLeft--;
				error("Received %d bytes while expecting %d ==> ignored.",transferred, settings->byteNb);	
			}
			continue;
		}

		unsigned char value = 0;
		
		if (settings->buttonIdx >= 0) 
			value = buf[settings->buttonIdx];
			
		if (settings->wheelIdx >= 0) {

			value &= 0x3f; /* clear bits 6 & 7 */

			if (buf[settings->wheelIdx] > 0) {
				value |= (buf[settings->wheelIdx] > 127) ? (1 << 6) : (1 << 7);
				if (!settings->wheelZero)
					valueOld = 0xFF; 
			}
		}
			
		if (settings->testMode) {	
			
			int i;
			for (i = 0; i < settings->byteNb; i++)  
				printf("%4d ",(char)buf[i]);
			
			if (value != valueOld) {
				char * str = get_multi_base_str(value);
				printf("- send: %s",str);
				free(str);
			}

			printf("\n");
			fflush(stdout);
		
		} else {
			if (value != valueOld) {
				write(1,&value,1);
			}
		}
		valueOld = value;	
	}
	
}


static void print_info() {
	printf("\n%s\n", RELEASE);
    printf("\n");
    printf("This program is a controller for the plugin 'Control' of lcd4linux.\n");
    printf("\n");
    printf("It detaches an USB mouse from the kernel and listen to its actions.\n"); 
    printf("Changes of button states or wheel movement will lead to a byte\n");
    printf("written to stdout. (If not in testmode.)\n");
    printf("(bits 0-5: button states (bits 0-7 if '-w -1'), bit 6: wheel down,\n");
    printf("bit 7: wheel up)\n");
    printf("\n");
    printf("Please visit the wiki for further information.\n");
    printf("\n");
    printf("usage:\n");
    printf("\n");
    printf("%s [options]",PROG);
    printf("\n");
    printf("options:\n");
    printf("  -h              help (this info)\n");
    printf("  -i <device id>  id of the USB mouse in the format of lsusb\n");
    printf("                  NOT optional\n");
    printf("  -t              testmode all raw bytes read from the mouse and\n");
    printf("                  the resulting byte in bin hex and dec.\n");
    printf("  -b <index>      index of the byte which will be interpreted\n");
    printf("                  as button state - default: 0\n");
    printf("  -w <index>      index of the byte which will be interpreted\n");
    printf("                  as wheel action - default: 3\n");
    printf("  -z              Wheel bits have to be changed for new output byte\n");
    printf("                  Set this option if -w is set to a rawbyte that\n"); 
    printf("                  indicates horizontal wheel movement.\n");
    printf("\n");
}


int is_id_format(char *idStr) {
	
	int ok = 1;
	int len = strlen(idStr);
	if (idStr == NULL || len!= 9 || idStr[4] != ':')
		ok = 0;
	int i;
	for (i = 0; ok && i < len; i++){
		
		if (i==4) 
			continue;
			
		char c = idStr[i];
		
		if (  !(c>='0' && c<='9') && !(c>='a' && c<='f')
			&&!(c>='A' && c<='F')) {
				
			ok = 0;	
		}
	}
	return ok;
}


static int init_settings() {
	
	if (get_args(NULL) != 0) {
		error("Non-option arguments given but not allowed.");
		my_exit(EXIT_FAILURE);
	}
	
	settings = malloc(sizeof(Settings));
	if (settings == NULL) {
		noMem();
		return 0;
	}

	settings->ctx = NULL;
	settings->device = NULL;
	settings->handle = NULL;
	settings->endpoint = -1;
	settings->byteNb = -1;
	settings->buttonIdx = 0;
	settings->wheelIdx = 3;
	settings->wheelZero = 0;
	settings->testMode = 0;
	settings->stop = 0;

	if (!get_opt_str('i', 0, &settings->id) || !is_id_format(settings->id)) {
		error("No valide device id (option '-i') given.");
		return 0;
	}
		
	if (get_opt_str('t', 0, NULL))
		settings->testMode = 1;


	if (	!get_opt_int_default('b', 1, 0, &settings->buttonIdx)
		||	!get_opt_int_default('w', 1, 3, &settings->wheelIdx)){
	
		return 0;
	}

	if (settings->buttonIdx == settings->wheelIdx) {
		error("Options '-b' and '-w' must be set to different values.");
		my_exit(EXIT_FAILURE);
	}
	
	if (get_opt_str('z', 0, NULL))
		settings->wheelZero = 1;
		
	if (!init_device())
		return 0;


	if (settings->buttonIdx < -1 || settings->buttonIdx >= settings->byteNb) {
		error("Value for '-b' option is not in [-1..%d] - found %d.", settings->byteNb, settings->buttonIdx);
		return 0;
	}
	
	if (settings->wheelIdx < -1 || settings->wheelIdx >= settings->byteNb) {
		error("Value for '-w' option is not in [-1..%d] - found %d.", settings->byteNb,settings->wheelIdx);
		return 0;
	}
	
	if (libusb_kernel_driver_active(settings->handle,0) == 1 
		&& libusb_detach_kernel_driver(settings->handle,0) != 0){
			
		error("Can't detached kernel driver.");
		return 0;
	}
	
	if (libusb_claim_interface(settings->handle,0) != 0){
		error("Can't claim interface.");
		return 0;
	}

	return 1;
}


int main(int argc, char *argv[]) {
	
	if (!init_util(PROG, argc, argv, ":b:hi:tw:z"))
		my_exit(EXIT_FAILURE);
		
	if (get_opt_str('h', 0, NULL)) {
		print_info();
		my_exit(EXIT_SUCCESS);
	}

	if (!init_settings()) 
		my_exit(EXIT_FAILURE);

	if (settings->testMode) {

		printf("\n\nMouse found.\n");
		printf("endpoint: 0x%02x\n",settings->endpoint);
		printf("byteNb: %d\n",settings->byteNb);
	}
	
	handle_input();

	my_exit(0);
	return 0;
}
	

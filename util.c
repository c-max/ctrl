/* Collection of functions used from controllers
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
#include <unistd.h>
#include <signal.h>
#include <stdarg.h>
#include <syslog.h>

#include "util.h"

typedef struct Option Option;
typedef struct Argument Argument;

typedef struct Option {

	char key;
	char *value;
	 Option * next;
	 
} Option;

typedef struct Argument {

	char *value;
	Argument * next;
	 
} Argument;


typedef struct Settings {
	Option * firstOption;
	Argument * firstArgument;
	int argNb;
	int stop;
	int testmode;
	
} Settings;

static Settings * settings = NULL;
static char * prog = "util";
static void (*externalSignalHandler)(int);


/* writes to stderr, fallback: syslog */
void msg(const char *format, ...) {
	static int log = 0;
	
	int nb = 0;
		
	if (!log) {

		fprintf(stderr, "%s: ", prog);
		
		va_list args;
		va_start(args,format);
		nb = vfprintf(stderr, format, args);
		va_end(args);
		
		if (nb < 0) {
			log = 1;
			openlog(prog, LOG_PID, LOG_USER);
		} else
			fprintf(stderr, "\n");
	}

	if (log) {

		char * str = NULL;
		va_list args;
		va_start(args,format);
		int len = vasprintf(&str, format,args);
		va_end(args);
		if (len >= 0) {
			syslog(LOG_ERR, "%s", str);		
			free(str);
		}	
	}
}


static int add_opt(char key, char *value) {

	Option ** addr;
	for (addr = &settings->firstOption; *addr != NULL; addr = &(*addr)->next) 
		if ((*addr)->key == key) {
			error("Option '-%c' was set multiple times.",key); 
			return -1;
		}

	Option * opt = malloc(sizeof(Option));
	if (opt == NULL) {
		noMem();
		return -1;
	}
	opt->key = key;
	opt->value = value;
	opt->next = NULL;
	*addr = opt;

	return 0;
}


static int add_arg(char *value) {

	Argument ** addr;
	Argument * arg = malloc(sizeof(Argument));
	if (arg == NULL) {
		noMem();
		return -1;
	}
	arg->value = value;
	arg->next = NULL;
	for (addr = &settings->firstArgument; *addr != NULL; addr = &(*addr)->next); /*sic*/
	*addr = arg;
	settings->argNb++;
	return 0;
}


static void free_settings() {

	Option * opt;
	Option * nextOpt;
	for (opt = settings->firstOption; opt != NULL; opt = nextOpt) {
		nextOpt = opt->next;
		free(opt);
	}
	settings->firstOption = NULL;
	
	Argument * arg;
	Argument * nextArg;
	for (arg = settings->firstArgument; arg != NULL; arg = nextArg) {
		nextArg = arg->next;
		free(arg);
	}
	settings->firstArgument = NULL;
	settings->argNb = 0;
	settings->stop = 1;
}

static void signalHandler(int sig) {
	char * sigStr = strsignal(sig); /* no free */
	info("Signal %d (%s) caught.",sig,sigStr);
	settings->stop = 1;
	if (externalSignalHandler) 
		externalSignalHandler(sig);
}

static int init_settings(char * progname, int argc, char *argv[], char *optString) {

	prog = progname;

	info("Init.");

	if (settings != NULL) {
		error("init_util already done.");
		return 1; /*don't free_settings*/
	}

	if (optString == NULL || *optString != ':') {
		error("option string has to start with ':'.");
		return 0;
	}

	settings = malloc(sizeof(Settings));
	if (settings == NULL) {
		noMem();
		return 0;
	}

	settings->firstOption = NULL;
	settings->firstArgument = NULL;
	settings->argNb = 0;
	settings->stop = 0;
	settings->testmode = 0;
	
	int opt;
	int err = 0;
	
	while (!err && (opt = getopt (argc, argv, optString)) != EOF) {
		switch (opt) {	
			case ':':
				error("Option '-%c' requires a value.",optopt);
				err = 1;				
				break;
            case '?':
				error("Unknown option: '-%c'",optopt);
				err = 1;				
				break;
            default:
				if (add_opt(opt,optarg) == -1)
					err = 1;
		}
	}

	int i;
	for (i = 0; !err && i < argc-optind; i++) {
		char * value = argv[optind + i];
		if (add_arg(value) == -1)
			err = 1;
	}

	if (err)
		free_util();
	else {
		signal(SIGHUP, signalHandler);
		signal(SIGTERM, signalHandler);
		signal(SIGPIPE, signalHandler);
		
		settings->testmode = get_opt_str('t', 0, NULL) == 1 ? 1 : 0;
		
		signal(SIGINT, settings->testmode ? signalHandler : SIG_IGN);
		signal(SIGQUIT, settings->testmode ? signalHandler : SIG_IGN);
	}
	
	if (err) {
		info("Try '%s -h' for more information.",prog);
		free_settings();
	}
	return err ? 0 : 1;
}


int get_opt_str(char key, int withErrorMsg, char **strAddr) {
	Option * opt;
	for (opt = settings->firstOption; opt != NULL; opt = opt->next) 
		if (opt->key == key) {
			if (strAddr!= NULL)
				*strAddr = opt->value;
			return 1;
		}
	if (withErrorMsg)
		error("Option '-%c' not found.",key);
		
	return 0;	
}


int get_opt_int(char key, int withErrorMsg, int *intAddr) {
	
	char *str;
	
	if (!get_opt_str(key, 0, &str) || !str_to_int(str, intAddr)) {
		if (withErrorMsg)
			error("Value of option '-%c' was not found or not a valide integer.", key);
		return 0;
	}
	return 1;
}


int get_opt_int_default(char key, int withErrorMsg, int dflt, int *intAddr) {
	
	char *str;
	
	if (!get_opt_str(key, 0, &str)) {
		*intAddr = dflt;
		return 1;
	}
	
	if (!str_to_int(str, intAddr)) {
		if (withErrorMsg)
			error("Value of option '-%c' is not a valide integer.", key);
		return 0;
	}
	return 1;
}


int get_opt_int_between(char key, int withErrorMsg, int from, int to, int dflt, int *intAddr) {

	int val;
	if (!get_opt_int(key, 0, &val)) 
		val = dflt;
	
	if (val < from || val > to) {
		if (withErrorMsg)
			error("Value of option '-%c' is not a valide integer from [%d..%d].", key, from ,to);
		return 0;
	} 
	
	*intAddr = val;
	return 1;	
}


int get_args(char ***args) {
	
	if (settings->argNb <= 0) {	
		if (args != NULL)
			*args = NULL;
		return 0;
	}
	
	if (args != NULL) {
		char **strLst = malloc(settings->argNb * sizeof(char*));
		if (strLst == NULL) {
			noMem();
			return -1;
		}
		Argument *arg;
		int i;
		for (arg = settings->firstArgument, i = 0; arg != NULL; arg = arg->next, i ++) 
			strLst[i] = arg->value;

		*args = strLst;
	}
	return settings->argNb;
}


char * make_str(const char *format, ...)
{
	char * str = NULL;
	
	va_list args;
	va_start(args,format);
	int len = vasprintf(&str, format,args);
	va_end(args);
   
	if (len < 0) {
		error("make_str() could't create string.");
		return NULL;
	}	
	return str;
}


int str_to_int(char *str, int * intPtr) {
	char * endptr;
	long lval = strtol (str, &endptr, 10);
	int val = (int)lval;
	if (*endptr != '\0' || val != lval)
		return 0;
	*intPtr = val;
	return 1;
}


char * get_bin_str(unsigned char val) {
	
	char *str = malloc(9*sizeof(char));
	if (str == NULL) {
		noMem();
		return NULL;
	}
	int i;
	for (i = 0; i < 8; i++)
		str[i] = (val & (1<<(7-i))) ? '1' : '0';
	str[8] = '\0';
	return str;
}


char * get_multi_base_str(unsigned char val) {
	
	char * str = NULL;
	char * binStr = get_bin_str(val);
	
	if (val >= 0x20 && val < 0x7f)
		str = make_str("bin:%s oct:0%03o hex:%02x  dec:%3d  char:'%c'",binStr,val,val,val,val);
	else
		str = make_str("bin:%s oct:0%03o hex:%02x  dec:%3d  char:'\\x%02x'",binStr,val,val,val,val);
	free(binStr);
	return str;
}


int init_util(char * progname, int argc, char *argv[], char *optString) {
	return init_settings(progname, argc, argv, optString);
}


int init_util_sig(char * progname, int argc, char *argv[], char *optString, void (*signalHandler)(int)) {
	int r = init_settings(progname, argc, argv, optString);
	if (r != 0)
		externalSignalHandler = signalHandler;
	return r;
}


void free_util() {
	free_settings();
}


int stopped_by_signal() {
	return settings->stop;
}

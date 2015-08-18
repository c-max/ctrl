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
 
#ifndef _UTIL_H_
#define _UTIL_H_

#define info(args...)  msg(args)
#define error(args...) msg("ERROR: " args) 
#define noMem() msg("ERROR: Couldn't allocate new memory. (%s:%d)",__FILE__, __LINE__)

void msg(const char *format, ...) __attribute__ ((format(__printf__, 1, 2)));

int init_util(char *progname, int argc, char *argv[], char *optString);
int init_util_sig(char * progname, int argc, char *argv[], char *optString, void (*signalHandler)(int));
void free_util();
int get_opt_str(char key, int withErrorMsg, char **strAddr);
int get_opt_int(char key, int withErrorMsg, int *intAddr);
int get_opt_int_default(char key, int withErrorMsg, int dflt, int *intAddr);
int get_opt_int_between(char key, int withErrorMsg, int from, int to, int dflt, int *intAddr);

int get_args(char ***args);

char * make_str(const char *format, ...) __attribute__ ((format(__printf__, 1, 2)));
int str_to_int(char *str, int * intAddr);
char * get_bin_str(unsigned char byte);
char * get_multi_base_str(unsigned char byte);

int stopped_by_signal();

#endif

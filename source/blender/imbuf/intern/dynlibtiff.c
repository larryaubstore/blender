/**
 * Dynamically loaded libtiff support.
 *
 * This file is automatically generated by the gen_dynlibtiff.py script.
 * 
 * ***** BEGIN GPL/BL DUAL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version. The Blender
 * Foundation also sells licenses for use in proprietary software under
 * the Blender License.  See http://www.blender.org/BL/ for information
 * about this.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * Contributor(s): Jonathan Merritt.
 *
 * ***** END GPL/BL DUAL LICENSE BLOCK *****
 */
 
/**
 * To use the dynamic libtiff support, you must initialize the library using:
 *	libtiff_init()
 * This attempts to load libtiff dynamically at runtime.  G.have_libtiff will
 * be set to indicate whether or not libtiff is available.  If libtiff is
 * not available, Blender can proceed with no ill effects, provided that
 * it does not attempt to use any of the libtiff_ functions.  When you're
 * finished, close the library with:
 *	libtiff_exit()
 * These functions are both declared in IMB_imbuf.h
 *
 * The functions provided by dyn_libtiff.h are the same as those in the
 * normal static / shared libtiff, except that they are prefixed by the 
 * string "libtiff_" to indicate that they belong to a dynamically-loaded 
 * version.
 */
#include "dynlibtiff.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "BLI_blenlib.h"

#include "imbuf.h"
#include "imbuf_patch.h"
#include "IMB_imbuf.h"

#include "BKE_global.h"
#include "PIL_dynlib.h"

/*********************
 * LOCAL DEFINITIONS *
 *********************/
PILdynlib *libtiff = NULL;
void  libtiff_loadlibtiff(void);
void* libtiff_findsymbol(char*);
int   libtiff_load_symbols(void);


/**************************
 * LIBRARY INITIALIZATION *
 **************************/

void libtiff_loadlibtiff(void)
{
	char *filename;
	libtiff = NULL;

#ifndef __APPLE__       /* no standard location of libtiff in MacOS X */

	/* Try to find libtiff in a couple of standard places */
	libtiff = PIL_dynlib_open("libtiff.so");
	if (libtiff != NULL)  return;
	libtiff = PIL_dynlib_open("libtiff.dll");
	if (libtiff != NULL)  return;
	libtiff = PIL_dynlib_open("/usr/lib/libtiff.so");
	if (libtiff != NULL)  return;
	/* OSX has version specific library */
	libtiff = PIL_dynlib_open("/usr/lib/libtiff.so.3");
	if (libtiff != NULL)  return;
	libtiff = PIL_dynlib_open("/usr/local/lib/libtiff.so");
	if (libtiff != NULL)  return;
	/* For solaris */
	libtiff = PIL_dynlib_open("/usr/openwin/lib/libtiff.so");
	if (libtiff != NULL)  return;

#endif

	filename = getenv("BF_TIFF_LIB");
	if (filename) libtiff = PIL_dynlib_open(filename);
}

void *libtiff_findsymbol(char *name)
{
	void *symbol = NULL;
	assert(libtiff != NULL);
	symbol = PIL_dynlib_find_symbol(libtiff, name);
	if (symbol == NULL) {
		printf("libtiff_findsymbol: error %s\n",
			PIL_dynlib_get_error_as_string(libtiff));
		libtiff = NULL;
		G.have_libtiff = (0);
		return NULL;
	}
	return symbol;
}

void libtiff_init(void)
{
	if (libtiff != NULL) {
		printf("libtiff_init: Attempted to load libtiff twice!\n");
		return;
	}
	libtiff_loadlibtiff();
	G.have_libtiff = ((libtiff != NULL) && (libtiff_load_symbols()));
}

void libtiff_exit(void)
{
	if (libtiff != NULL) {
		PIL_dynlib_close(libtiff);
		libtiff = NULL;
	}
}


int libtiff_load_symbols(void)
{
	/* Attempt to load TIFFClientOpen */
	libtiff_TIFFClientOpen = libtiff_findsymbol("TIFFClientOpen");
	if (libtiff_TIFFClientOpen == NULL) {
		return (0);
	}
	/* Attempt to load TIFFClose */
	libtiff_TIFFClose = libtiff_findsymbol("TIFFClose");
	if (libtiff_TIFFClose == NULL) {
		return (0);
	}
	/* Attempt to load TIFFGetField */
	libtiff_TIFFGetField = libtiff_findsymbol("TIFFGetField");
	if (libtiff_TIFFGetField == NULL) {
		return (0);
	}
	/* Attempt to load TIFFOpen */
	libtiff_TIFFOpen = libtiff_findsymbol("TIFFOpen");
	if (libtiff_TIFFOpen == NULL) {
		return (0);
	}
	/* Attempt to load TIFFReadRGBAImage */
	libtiff_TIFFReadRGBAImage = libtiff_findsymbol("TIFFReadRGBAImage");
	if (libtiff_TIFFReadRGBAImage == NULL) {
		return (0);
	}
	/* Attempt to load TIFFSetField */
	libtiff_TIFFSetField = libtiff_findsymbol("TIFFSetField");
	if (libtiff_TIFFSetField == NULL) {
		return (0);
	}
	/* Attempt to load TIFFWriteEncodedStrip */
	libtiff_TIFFWriteEncodedStrip = libtiff_findsymbol("TIFFWriteEncodedStrip");
	if (libtiff_TIFFWriteEncodedStrip == NULL) {
		return (0);
	}
	/* Attempt to load _TIFFfree */
	libtiff__TIFFfree = libtiff_findsymbol("_TIFFfree");
	if (libtiff__TIFFfree == NULL) {
		return (0);
	}
	/* Attempt to load _TIFFmalloc */
	libtiff__TIFFmalloc = libtiff_findsymbol("_TIFFmalloc");
	if (libtiff__TIFFmalloc == NULL) {
		return (0);
	}
	return (1);
}

	
/*******************
 * SYMBOL POINTERS *
 *******************/

TIFF* (*libtiff_TIFFClientOpen)(const char*, const char*, thandle_t, TIFFReadWriteProc, TIFFReadWriteProc, TIFFSeekProc, TIFFCloseProc, TIFFSizeProc, TIFFMapFileProc, TIFFUnmapFileProc) = NULL;
void (*libtiff_TIFFClose)(TIFF*) = NULL;
int (*libtiff_TIFFGetField)(TIFF*, ttag_t, ...) = NULL;
TIFF* (*libtiff_TIFFOpen)(const char*, const char*) = NULL;
int (*libtiff_TIFFReadRGBAImage)(TIFF*, uint32, uint32, uint32*, int) = NULL;
int (*libtiff_TIFFSetField)(TIFF*, ttag_t, ...) = NULL;
tsize_t (*libtiff_TIFFWriteEncodedStrip)(TIFF*, tstrip_t, tdata_t, tsize_t) = NULL;
void (*libtiff__TIFFfree)(tdata_t) = NULL;
tdata_t (*libtiff__TIFFmalloc)(tsize_t) = NULL;

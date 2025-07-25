/*******************************************************************************
 * Copyright IBM Corp. and others 1991
 *
 * This program and the accompanying materials are made available under
 * the terms of the Eclipse Public License 2.0 which accompanies this
 * distribution and is available at https://www.eclipse.org/legal/epl-2.0/
 * or the Apache License, Version 2.0 which accompanies this distribution and
 * is available at https://www.apache.org/licenses/LICENSE-2.0.
 *
 * This Source Code may also be made available under the following
 * Secondary Licenses when the conditions for such availability set
 * forth in the Eclipse Public License, v. 2.0 are satisfied: GNU
 * General Public License, version 2 with the GNU Classpath
 * Exception [1] and GNU General Public License, version 2 with the
 * OpenJDK Assembly Exception [2].
 *
 * [1] https://www.gnu.org/software/classpath/license.html
 * [2] https://openjdk.org/legal/assembly-exception.html
 *
 * SPDX-License-Identifier: EPL-2.0 OR Apache-2.0 OR GPL-2.0-only WITH Classpath-exception-2.0 OR GPL-2.0-only WITH OpenJDK-assembly-exception-1.0
 *******************************************************************************/

/**
 * @file
 * @ingroup Port
 * @brief shared library
 */

#if defined(LINUX) && !defined(OMRZTPF)

/* defining _GNU_SOURCE allows the use of dladdr() in dlfcn.h */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#elif defined(OSX)
#define _XOPEN_SOURCE
#endif /* defined(LINUX)  && !defined(OMRZTPF) */

/*
 *	Standard unix shared libraries
 *	(AIX: 4.2 and higher only)
 */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <dlfcn.h>
#include "ut_omrport.h"

/* Length of string buffers. */
#define MAX_STRING_LENGTH 1024
#define MAX_ERR_BUF_LENGTH 512

/* Start copy from omrfiletext.c */
/* __STDC_ISO_10646__ indicates that the platform wchar_t encoding is Unicode */
/* but older versions of libc fail to set the flag, even though they are Unicode */
#if defined(__STDC_ISO_10646__) || defined(LINUX) || defined(OSX)
#define J9VM_USE_MBTOWC
#else /* defined(__STDC_ISO_10646__) || defined(LINUX) || defined(OSX) */
#include "omriconvhelpers.h"
#endif /* defined(__STDC_ISO_10646__) || defined(LINUX) || defined(OSX) */


#if defined(J9VM_USE_MBTOWC) || defined(J9VM_USE_ICONV)
#include <nl_types.h>
#include <langinfo.h>

/* Some older platforms (Netwinder) don't declare CODESET */
#ifndef CODESET
#define CODESET _NL_CTYPE_CODESET_NAME
#endif
#endif

/* End copy */

#include "omrport.h"
#include "portnls.h"

#if defined(OSX)
#define PLATFORM_DLL_EXTENSION ".dylib"
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#else /* defined(OSX) */
#define PLATFORM_DLL_EXTENSION ".so"
#endif /* defined(OSX) */

#if (defined(J9VM_USE_MBTOWC)) /* priv. proto (autogen) */

static void convertWithMBTOWC(struct OMRPortLibrary *portLibrary, const char *error, char *errBuf, uintptr_t bufLen);
#endif /* J9VM_USE_MBTOWC (autogen) */



#if (defined(J9VM_USE_ICONV)) /* priv. proto (autogen) */

static void convertWithIConv(struct OMRPortLibrary *portLibrary, char *error, char *errBuf, uintptr_t bufLen);
#endif /* J9VM_USE_ICONV (autogen) */



static void getDLError(struct OMRPortLibrary *portLibrary, char *errBuf, uintptr_t bufLen);


/**
 * Close a shared library.
 *
 * @param[in] portLibrary The port library.
 * @param[in] descriptor Shared library handle to close.
 *
 * @return 0 on success, any other value on failure.
 */
uintptr_t
omrsl_close_shared_library(struct OMRPortLibrary *portLibrary, uintptr_t descriptor)
{
	uintptr_t result = 1;

	Trc_PRT_sl_close_shared_library_Entry(descriptor);

	if (0 != descriptor) {
		result = (uintptr_t)dlclose((void *)descriptor);
		if (0 != result) {
			char errBuf[MAX_ERR_BUF_LENGTH];
			getDLError(portLibrary, errBuf, sizeof(errBuf));
			portLibrary->tty_printf(portLibrary, "dlclose() failed: return code: %d message: \"%s\" \n", result, errBuf);
		}
	}

	Trc_PRT_sl_close_shared_library_Exit(result);

	return result;
}

uintptr_t
omrsl_open_shared_library(struct OMRPortLibrary *portLibrary, char *name, uintptr_t *descriptor, uintptr_t flags)
{
	void *handle = NULL;
	char *openName = name;
	char mangledName[MAX_STRING_LENGTH + 1];
	char errBuf[MAX_ERR_BUF_LENGTH];
	int lazyOrNow = OMR_ARE_ALL_BITS_SET(flags, OMRPORT_SLOPEN_LAZY) ? RTLD_LAZY : RTLD_NOW;
	BOOLEAN decorate = OMR_ARE_ALL_BITS_SET(flags, OMRPORT_SLOPEN_DECORATE);
	BOOLEAN openExec = OMR_ARE_ALL_BITS_SET(flags, OMRPORT_SLOPEN_OPEN_EXECUTABLE);
	BOOLEAN openNoLoad = OMR_ARE_ALL_BITS_SET(flags, OMRPORT_SLOPEN_NO_LOAD);
	uintptr_t pathLength = 0;

	Trc_PRT_sl_open_shared_library_Entry(name, flags);

	/* No need to name mangle if a handle to the executable is requested for. */
	if (!openExec && decorate) {
		char *p = strrchr(name, '/');
		if (NULL != p) {
			/* the names specifies a path */
			pathLength = portLibrary->str_printf(portLibrary, mangledName, (MAX_STRING_LENGTH + 1), "%.*slib%s" PLATFORM_DLL_EXTENSION, (uintptr_t)p + 1 - (uintptr_t)name, name, p + 1);
		} else {
			pathLength = portLibrary->str_printf(portLibrary, mangledName, (MAX_STRING_LENGTH + 1), "lib%s" PLATFORM_DLL_EXTENSION, name);
		}
		if (pathLength >= MAX_STRING_LENGTH) {
			Trc_PRT_sl_open_shared_library_Exit2(OMRPORT_SL_UNSUPPORTED);
			return OMRPORT_SL_UNSUPPORTED;
		}
		openName = mangledName;
	}

	Trc_PRT_sl_open_shared_library_Event1(openName);

	if (openNoLoad) {
		/* One of RTLD_LAZY and RTLD_NOW must be included in the flag. */
		handle = dlopen(openExec ? NULL : openName, RTLD_NOLOAD | lazyOrNow);
		Trc_PRT_sl_open_shared_library_noload(handle);
		goto exitOnSuccess;
	}

	/* dlopen(2) called with NULL filename opens a handle to current executable. */
	handle = dlopen(openExec ? NULL : openName, lazyOrNow);

#if defined(OMR_HAVE_DLADDR)
	if ((NULL == handle) && !openExec) {
		/* last ditch, try dir port lib DLL is in */
		char portLibDir[MAX_STRING_LENGTH];
		Dl_info libraryInfo;
		int rc = dladdr((void *)omrsl_open_shared_library, &libraryInfo);

		if (0 != rc) {
			/* find the directory of the port library shared object */
			char *dirSep = strrchr(libraryInfo.dli_fname, '/');
			/* retry only if the path will be different than the attempt above */
			if (NULL != dirSep) {
				/* +1 so length includes the '/' */
				pathLength = dirSep - libraryInfo.dli_fname + 1;
				/* proceed only if buffer size can fit the new concatenated path (+1 for NUL) */
				if (MAX_STRING_LENGTH < (pathLength + strlen(openName) + 1)) {
					const char *error = portLibrary->nls_lookup_message(portLibrary,
								  J9NLS_ERROR | J9NLS_DO_NOT_APPEND_NEWLINE,
								  J9NLS_PORT_SL_BUFFER_EXCEEDED_ERROR,
								  "Insufficient buffer memory while attempting to load a shared library");
					strncpy(errBuf, error, MAX_ERR_BUF_LENGTH);
					errBuf[MAX_ERR_BUF_LENGTH - 1] = '\0'; /* If NLS message size exceeds buffer length */
					Trc_PRT_sl_open_shared_library_Exit2(OMRPORT_SL_INVALID);
					return portLibrary->error_set_last_error_with_message(portLibrary, OMRPORT_SL_INVALID, errBuf);
				}
				memcpy(portLibDir, libraryInfo.dli_fname, pathLength);
				strcpy(portLibDir + pathLength, openName);
				handle = dlopen(portLibDir, lazyOrNow);
				if (NULL == handle) {
					/* rerun the dlopen to get the right error code again */
					handle = dlopen(openName, lazyOrNow);
				}
			}
		}
	}
#endif /* defined(OMR_HAS_DLADDR) */

	if (NULL == handle) {
		getDLError(portLibrary, errBuf, sizeof(errBuf));
		Trc_PRT_sl_open_shared_library_Event2(errBuf);
		if (EsIsFile == portLibrary->file_attr(portLibrary, openName)) {
			Trc_PRT_sl_open_shared_library_Exit2(OMRPORT_SL_INVALID);
			return portLibrary->error_set_last_error_with_message(portLibrary, OMRPORT_SL_INVALID, errBuf);
		} else {
			Trc_PRT_sl_open_shared_library_Exit2(OMRPORT_SL_NOT_FOUND);
			return portLibrary->error_set_last_error_with_message(portLibrary, OMRPORT_SL_NOT_FOUND, errBuf);
		}
	}

exitOnSuccess:
	*descriptor = (uintptr_t)handle;
	Trc_PRT_sl_open_shared_library_Exit1(*descriptor);
	return 0;
}
/**
 * Search for a function named 'name' taking argCount in the shared library 'descriptor'.
 *
 * @param[in] portLibrary The port library.
 * @param[in] descriptor Shared library to search.
 * @param[in] name Function to look up.
 * @param[out] func Pointer to the function.
 * @param[in] argSignature Argument signature.
 *
 * @return 0 on success, any other value on failure.
 *
 * argSignature is a C (ie: NUL-terminated) string with the following possible values for each character:
 *
 *		V	- void
 *		Z	- boolean
 *		B	- byte
 *		C	- char (16 bits)
 *		I	- integer (32 bits)
 *		J	- long (64 bits)
 *		F	- float (32 bits)
 *		D	- double (64 bits)
 *		L	- object / pointer (32 or 64, depending on platform)
 *		P	- pointer-width platform data. (in J9 terms an intptr_t)
 *
 * Lower case signature characters imply unsigned value.
 * Upper case signature characters imply signed values.
 * If it doesn't make sense to be signed/unsigned (eg: V, L, F, D Z) the character is upper case.
 *
 * argList[0] is the return type from the function.
 * The argument list is as it appears in english: list is left (1) to right (argCount)
 *
 * @note contents of func are undefined on failure.
 */
uintptr_t
omrsl_lookup_name(struct OMRPortLibrary *portLibrary, uintptr_t descriptor, char *name, uintptr_t *func, const char *argSignature)
{
	void *address;

	Trc_PRT_sl_lookup_name_Entry(descriptor, name, argSignature);
	address = dlsym((void *)descriptor, name);
	if (NULL == address) {
		Trc_PRT_sl_lookup_name_Exit2(name, argSignature, descriptor, 1);
		return 1;
	}
	*func = (uintptr_t) address;

	Trc_PRT_sl_lookup_name_Exit1(*func);
	return 0;
}

static void
getDLError(struct OMRPortLibrary *portLibrary, char *errBuf, uintptr_t bufLen)
{
	const char *error = NULL;

	if (0 == bufLen) {
		return;
	}

	error = dlerror();
	if ((NULL == error) || ('\0' == error[0])) {
		/* just in case another thread consumed our error message */
		error = portLibrary->nls_lookup_message(portLibrary,
				J9NLS_ERROR | J9NLS_DO_NOT_APPEND_NEWLINE,
				J9NLS_PORT_SL_UNKOWN_ERROR,
				"Unknown error");
		strncpy(errBuf, error, bufLen);
		errBuf[bufLen - 1] = '\0';
		return;
	}

#if defined(J9VM_USE_MBTOWC)
	convertWithMBTOWC(portLibrary, error, errBuf, bufLen);
#elif defined(J9VM_USE_ICONV)
	convertWithIConv(portLibrary, error, errBuf, bufLen);
#else
	strncpy(errBuf, error, bufLen);
	errBuf[bufLen - 1] = '\0';
#endif
}

#if (defined(J9VM_USE_ICONV)) /* priv. proto (autogen) */

static void
convertWithIConv(struct OMRPortLibrary *portLibrary, char *error, char *errBuf, uintptr_t bufLen)
{
	iconv_t converter;
	size_t inbytesleft, outbytesleft;
	char *inbuf, *outbuf;

	converter = iconv_get(portLibrary, J9SL_ICONV_DESCRIPTOR, "UTF-8", nl_langinfo(CODESET));

	if (J9VM_INVALID_ICONV_DESCRIPTOR == converter) {
		/* no converter available for this code set. Just dump the platform chars */
		strncpy(errBuf, error, bufLen);
		errBuf[bufLen - 1] = '\0';
		return;
	}

	inbuf = (char *)error; /* for some reason this argument isn't const */
	outbuf = errBuf;
	inbytesleft = strlen(error);
	outbytesleft = bufLen - 1;

	while ((outbytesleft > 0) && (inbytesleft > 0)) {
		if ((size_t)-1 == iconv(converter, &inbuf, &inbytesleft, &outbuf, &outbytesleft)) {
			if (E2BIG == errno) {
				break;
			}

			/* if we couldn't translate this character, copy one byte verbatim */
			*outbuf = *inbuf;
			outbuf++;
			inbuf++;
			inbytesleft--;
			outbytesleft--;
		}
	}

	*outbuf = '\0';
	iconv_free(portLibrary, J9SL_ICONV_DESCRIPTOR, converter);
}
#endif /* J9VM_USE_ICONV (autogen) */


#if (defined(J9VM_USE_MBTOWC)) /* priv. proto (autogen) */

static void
convertWithMBTOWC(struct OMRPortLibrary *portLibrary, const char *error, char *errBuf, uintptr_t bufLen)
{
	char *out = errBuf;
	char *end = &errBuf[bufLen - 1];
	const char *walk = error;
	wchar_t ch = '\0';
	int ret = 0;

	/* reset the shift state */
	ret = mbtowc(NULL, NULL, 0);

	while ('\0' != *walk) {
		ret = mbtowc(&ch, walk, MB_CUR_MAX);
		if (ret < 0) {
			ch = *walk++;
		} else if (0 == ret) {
			break;
		} else {
			walk += ret;
		}

		if ('\r' == ch) {
			continue;
		}
		if ('\n' == ch) {
			ch = ' ';
		}
		if (ch < 0x80) {
			if ((out + 1) > end) {
				break;
			}
			*out++ = (char)ch;
		} else if (ch < 0x800) {
			if ((out + 2) > end) {
				break;
			}
			*out++ = (char)(0xc0 | ((ch >> 6) & 0x1f));
			*out++ = (char)(0x80 | (ch & 0x3f));
		} else {
			if ((out + 3) > end) {
				break;
			}
			*out++ = (char)(0xe0 | ((ch >> 12) & 0x0f));
			*out++ = (char)(0x80 | ((ch >> 6) & 0x3f));
			*out++ = (char)(0x80 | (ch & 0x3f));
		}
	}

	*out = '\0';
}
#endif /* J9VM_USE_MBTOWC (autogen) */

/**
 * PortLibrary shutdown.
 *
 * This function is called during shutdown of the portLibrary.  Any resources that were created by @ref omrsl_startup
 * should be destroyed here.
 *
 * @param[in] portLibrary The port library.
 *
 * @note Most implementations will be empty.
 */
void
omrsl_shutdown(struct OMRPortLibrary *portLibrary)
{
}

/**
 * PortLibrary startup.
 *
 * This function is called during startup of the portLibrary.  Any resources that are required for
 * the shared library operations may be created here.  All resources created here should be destroyed
 * in @ref omrsl_shutdown.
 *
 * @param[in] portLibrary The port library.
 *
 * @return 0 on success, negative error code on failure.  Error code values returned are
 * \arg OMRPORT_ERROR_STARTUP_SL
 *
 * @note Most implementations will simply return success.
 */
int32_t
omrsl_startup(struct OMRPortLibrary *portLibrary)
{
	return 0;
}

/**
 * Native Libraries
 *
 * This function is called go get all the shared libraries loaded by a process.
 *
 * @param[in] portLibrary Pointer to the OMR port library.
 * @param[in] callback Function to be called for each library.
 * @param[in] userData User-defined data passed to the callback.
 *
 * @return 0 if successful, or the first non-zero return value from the callback.
 */
uintptr_t
omrsl_get_libraries(struct OMRPortLibrary *portLibrary, OMRLibraryInfoCallback callback, void *userData)
{
#if defined(LINUX)
	/*
	 * Length of buffer
	 * PATH_MAX is the maximum file path length.
	 * 100 extra bytes account for memory addresses, permissions and offsets.
	 * 1 extra byte accounts for null terminator.
	 */
	char buffer[PATH_MAX + 101];
	uintptr_t result = 0;
	void *addrLow = NULL;
	void *addrHigh = NULL;
	unsigned long offset = 0;
	int devMajor = 0;
	int devMinor = 0;
	unsigned long inode = 0;
	char permissions[5] = {0};
	int count = 0;
	int pathOffset = 0;
	int32_t portableError = 0;
	intptr_t fd = -1;
	if (NULL == callback) {
		portLibrary->error_set_last_error_with_message(
				portLibrary,
				OMRPORT_ERROR_OPFAILED,
				"Callback function is NULL.");
		return (uintptr_t)(intptr_t)OMRPORT_ERROR_OPFAILED;
	}
	fd = portLibrary->file_open(portLibrary, "/proc/self/maps", EsOpenRead, 0);
	if (-1 == fd) {
		portableError = portLibrary->error_last_error_number(portLibrary);
		Trc_PRT_failed_to_open_proc_maps(portableError);
		portLibrary->error_set_last_error_with_message(
				portLibrary,
				portableError,
				"Failed to open /proc/self/maps");
		return (uintptr_t)(intptr_t)portableError;
	}
	while (NULL != portLibrary->file_read_text(portLibrary, fd, buffer, sizeof(buffer))) {
		if (NULL == strchr(buffer, '\n')) {
			portableError = portLibrary->error_last_error_number(portLibrary);
			portLibrary->error_set_last_error_with_message(
					portLibrary,
					portableError,
					"An unexpected long path name. Line is truncated.");
			continue;
		}
		count = sscanf(
				buffer,
				"%p-%p %4s %lx %x:%x %lu %n",
				&addrLow, &addrHigh, permissions,
				&offset, &devMajor, &devMinor, &inode, &pathOffset);
		if ((7 == count)  && ('/' == buffer[pathOffset])) {
			result = callback(&buffer[pathOffset], addrLow, addrHigh, userData);
			if (0 != result) {
				break;
			}
		}
	}
	portLibrary->file_close(portLibrary, fd);
	return result;
#elif defined(OSX) /* defined(LINUX) */
	uint32_t imageCount = _dyld_image_count();
	uint32_t i = 0;
	if (NULL == callback) {
		portLibrary->error_set_last_error_with_message(
				portLibrary,
				OMRPORT_ERROR_OPFAILED,
				"Callback function is NULL.");
		return (uintptr_t)(intptr_t)OMRPORT_ERROR_OPFAILED;
	}
	for (i = 0; i < imageCount; ++i) {
		intptr_t slide = 0;
		const struct mach_header_64 *header = NULL;
		const struct load_command *lc = NULL;
		uint32_t cmd = 0;
		const char *imageName = _dyld_get_image_name(i);
		if ((NULL == imageName) || ('/' != imageName[0])) {
			continue;
		}
		header = (const struct mach_header_64 *)_dyld_get_image_header(i);
		if ((NULL == header) || (MH_MAGIC_64 != header->magic)) {
			continue;
		}
		lc = (const struct load_command *)(header + 1);
		slide = _dyld_get_image_vmaddr_slide(i);
		for (cmd = 0; cmd < header->ncmds; ++cmd) {
			if (0 == lc->cmdsize) {
				break;
			}
			if (LC_SEGMENT_64 == lc->cmd) {
				const struct segment_command_64 *seg = (const struct segment_command_64 *)lc;
				uint64_t addrLow = seg->vmaddr + slide;
				uint64_t addrHigh = addrLow + seg->vmsize;
				uintptr_t result = callback(imageName, (void *)addrLow, (void *)addrHigh, userData);
				if (0 != result) {
					return result;
				}
			}
			lc = (const struct load_command *)((const uint8_t *)lc + lc->cmdsize);
		}
	}
	return 0;
#else /* defined(OSX) */
	/* Platform not supported. */
	return OMRPORT_ERROR_NOT_SUPPORTED_ON_THIS_PLATFORM;
#endif /* defined(LINUX) */
}

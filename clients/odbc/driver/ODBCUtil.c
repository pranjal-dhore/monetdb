/*
 * The contents of this file are subject to the MonetDB Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.monetdb.org/Legal/MonetDBLicense
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
 * License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is the MonetDB Database System.
 *
 * The Initial Developer of the Original Code is CWI.
 * Portions created by CWI are Copyright (C) 1997-July 2008 CWI.
 * Copyright August 2008-2013 MonetDB B.V.
 * All Rights Reserved.
 */

/*
 * This code was created by Peter Harvey (mostly during Christmas 98/99).
 * This code is LGPL. Please ensure that this message remains in future
 * distributions and uses of this code (thats about all I get out of it).
 * - Peter Harvey pharvey@codebydesign.com
 *
 * This file has been modified for the MonetDB project.  See the file
 * Copyright in this directory for more information.
 */

/**********************************************
 * ODBCUtil.c
 *
 * Description:
 * This file contains utility functions for
 * the ODBC driver implementation.
 *
 * Author: Martin van Dinther, Sjoerd Mullender
 * Date  : 30 aug 2002
 *
 **********************************************/

#include "ODBCUtil.h"
#include <float.h>


#ifdef WIN32
/* Windows seems to need this */
BOOL WINAPI
DllMain(HINSTANCE hinstDLL, DWORD reason, LPVOID reserved)
{
#ifdef ODBCDEBUG
	ODBCLOG("DllMain %ld (%s)\n", (long) reason, PACKAGE_STRING);
#endif
	(void) hinstDLL;
	(void) reason;
	(void) reserved;

	return TRUE;
}
#endif

/*
 * Utility function to duplicate an ODBC string (with a length
 * specified, may not be null terminated) to a normal C string (null
 * terminated).
 *
 * Precondition: inStr != NULL
 * Postcondition: returns a newly allocated null terminated string.
 */
char *
dupODBCstring(const SQLCHAR *inStr, size_t length)
{
	char *tmp = (char *) malloc((length + 1) * sizeof(char));

	assert(tmp);
	strncpy(tmp, (const char *) inStr, length);
	tmp[length] = '\0';	/* make it null terminated */
	return tmp;
}

/* Conversion to and from SQLWCHAR */
static int utf8chkmsk[] = {
	0x0000007f,
	0x00000780,
	0x0000f800,
	0x001f0000,
	0x03e00000,
	0x7c000000
};

#define LEAD_OFFSET		(0xD800 - (0x10000 >> 10))
#define SURROGATE_OFFSET	(0x10000 - (0xD800 << 10) - 0xDC00)

/* Convert a SQLWCHAR (UTF-16 encoded string) to UTF-8.  On success,
   clears the location pointed to by errmsg and returns NULL or a
   newly allocated buffer.  On error, assigns a string with an error
   message to the location pointed to by errmsg and returns NULL.
   The first two arguments describe the input string in the normal
   ODBC fashion.
*/
SQLCHAR *
ODBCwchar2utf8(const SQLWCHAR *s, SQLLEN length, char **errmsg)
{
	const SQLWCHAR *s1, *e;
	unsigned long c;
	SQLCHAR *buf, *p;
	int l, n;

	if (errmsg)
		*errmsg = NULL;
	if (s == NULL)
		return NULL;
	if (length == SQL_NTS)
		for (s1 = s, length = 0; *s1; s1++)
			length++;
	else if (length == SQL_NULL_DATA)
		return NULL;
	else if (length < 0) {
		if (errmsg)
			*errmsg = "Invalid length parameter";
		return NULL;
	}
	e = s + length;
	/* count necessary length */
	l = 1;			/* space for NULL byte */
	for (s1 = s; s1 < e; s1++) {
		c = *s1;
		if (0xD800 <= c && c <= 0xDBFF) {
			/* high surrogate, must be followed by low surrogate */
			s1++;
			if (s1 >= e || *s1 < 0xDC00 || *s1 > 0xDFFF) {
				if (errmsg)
					*errmsg = "High surrogate not followed by low surrogate";
				return NULL;
			}
#if 1
			if (errmsg)
				*errmsg = "Code points larger than U+FFFF are not supported";
			return NULL;
#else
			c = (c << 10) + *s1 + SURROGATE_OFFSET;
#endif
		} else if (0xDC00 <= c && c <= 0xDFFF) {
			/* low surrogate--illegal */
			if (errmsg)
				*errmsg = "Low surrogate not preceded by high surrogate";
			return NULL;
		}
		for (n = 5; n > 0; n--)
			if (c & utf8chkmsk[n])
				break;
		l += n + 1;
	}
	/* convert */
	buf = (SQLCHAR *) malloc(l);
	if (buf == NULL) {
		if (errmsg)
			*errmsg = "Memory allocation error";
		return NULL;
	}
	for (s1 = s, p = buf; s1 < e; s1++) {
		c = *s1;
		if (0xD800 <= c && c <= 0xDBFF) {
			/* high surrogate followed by low surrogate */
			s1++;
			c = (c << 10) + *s1 + SURROGATE_OFFSET;
		}
		for (n = 5; n > 0; n--)
			if (c & utf8chkmsk[n])
				break;
		if (n == 0)
			*p++ = (SQLCHAR) c;
		else {
			*p++ = (SQLCHAR) (((c >> (n * 6)) | (0x1F80 >> n)) & 0xFF);
			while (--n >= 0)
				*p++ = (SQLCHAR) (((c >> (n * 6)) & 0x3F) | 0x80);
		}
	}
	*p = 0;
	return buf;
}

/* Convert a UTF-8 encoded string to UTF-16 (SQLWCHAR).  On success
   returns NULL, on error returns a string with an error message.  The
   first two arguments describe the input, the last three arguments
   describe the output, both in the normal ODBC fashion. */
char *
ODBCutf82wchar(const SQLCHAR *s,
	       SQLINTEGER length,
	       SQLWCHAR *buf,
	       SQLLEN buflen,
	       SQLSMALLINT *buflenout)
{
	SQLWCHAR *p;
	const SQLCHAR *e;
	int m, n;
	unsigned int c;
	SQLSMALLINT len = 0;

	if (s == NULL || length == SQL_NULL_DATA) {
		if (buf && buflen > 0)
			*buf = 0;
		if (buflenout)
			*buflenout = 0;
		return NULL;
	}
	if (length == SQL_NTS)
		length = (SQLINTEGER) strlen((const char *) s);
	else if (length < 0)
		return "Invalid length parameter";

	if (buf == NULL)
		buflen = 0;

	for (p = buf, e = s + length; s < e; ) {
		c = *s++;
		if ((c & 0x80) != 0) {
			for (n = 0, m = 0x40; c & m; n++, m >>= 1)
				;
			/* n now is number of 10xxxxxx bytes that
			 * should follow */
			if (n == 0 || n >= 6)
				return "Illegal UTF-8 sequence";
			if (s + n > e)
				return "Truncated UTF-8 sequence";
			c &= ~(0xFFC0) >> n;
			while (--n >= 0) {
				c <<= 6;
				c |= *s++ & 0x3F;
			}
		}
		if (c > 0x10FFFF) {
			/* cannot encode as UTF-16 */
			return "Codepoint too large to be representable in UTF-16";
		}
		if ((c & 0x1FFF800) == 0xD800) {
			/* UTF-8 encoded high or low surrogate */
			return "Illegal code point";
		}
		if (c <= 0xFFFF) {
			if (--buflen > 0 && p != NULL)
				*p++ = c;
			len++;
		} else {
#if 1
			return "Code points larger than U+FFFF are not supported";
#else
			if ((buflen -= 2) > 0 && p != NULL) {
				*p++ = LEAD_OFFSET + (c >> 10);
				*p++ = 0xDC00 + (c & 0x3FF);
			}
			len += 2;
#endif
		}
	}
	if (p != NULL)
		*p = 0;
	if (buflenout)
		*buflenout = len;
	return NULL;
}

/*
 * Translate an ODBC-compatible query to one that the SQL server
 * understands.
 *
 * Precondition: query != NULL
 * Postcondition: returns a newly allocated null terminated strings.
 */
/*
  Escape sequences:
  {d 'yyyy-mm-dd'}
  {t 'hh:mm:ss'}
  {ts 'yyyy-mm-dd hh:mm:ss[.f...]'}
  {fn scalar-function}
  {escape 'escape-character'}
  {oj outer-join}
  where outer-join is:
	table-reference {LEFT|RIGHT|FULL} OUTER JOIN
	{table-reference | outer-join} ON search condition
  {[?=]call procedure-name[([parameter][,[parameter]]...)]}
 */

char *
ODBCTranslateSQL(const SQLCHAR *query, size_t length, SQLUINTEGER noscan)
{
	char *nquery;
	char *p, *q;
	char buf[512];
	unsigned yr, mt, dy, hr, mn, sc;
	unsigned long fr = 0;
	int n, pr;

	nquery = dupODBCstring(query, length);
	if (noscan == SQL_NOSCAN_ON)
		return nquery;
	/* scan from the back in preparation for dealing with nested escapes */
	q = nquery + length;
	while (q > nquery) {
		if (*--q != '{')
			continue;
		p = q;
		while (*++p == ' ')
			;
		if (sscanf(p, "ts '%u-%u-%u %u:%u:%u%n",
			   &yr, &mt, &dy, &hr, &mn, &sc, &n) >= 6) {
			p += n;
			pr = 0;
			if (*p == '.') {
				char *e;

				p++;
				fr = strtoul(p, &e, 10);
				if (e > p) {
					pr = (int) (e - p);
					p = e;
				} else {
					continue;
				}
			}
			if (*p != '\'')
				continue;
			while (*++p && *p == ' ')
				;
			if (*p != '}')
				continue;
			p++;
			if (pr > 0) {
				snprintf(buf, sizeof(buf),
					 "TIMESTAMP '%04u-%02u-%02u %02u:%02u:%02u.%0*lu'",
					 yr, mt, dy, hr, mn, sc, pr, fr);
			} else {
				snprintf(buf, sizeof(buf),
					 "TIMESTAMP '%04u-%02u-%02u %02u:%02u:%02u'",
					 yr, mt, dy, hr, mn, sc);
			}
			n = (int) (q - nquery);
			pr = (int) (p - q);
			q = malloc(length - pr + strlen(buf) + 1);
			sprintf(q, "%.*s%s%s", n, nquery, buf, p);
			free(nquery);
			nquery = q;
			q += n;
		} else if (sscanf(p, "t '%u:%u:%u%n", &hr, &mn, &sc, &n) >= 3) {
			p += n;
			pr = 0;
			if (*p == '.') {
				char *e;

				p++;
				fr = strtoul(p, &e, 10);
				if (e > p) {
					pr = (int) (e - p);
					p = e;
				} else
					continue;
			}
			if (*p != '\'')
				continue;
			while (*++p && *p == ' ')
				;
			if (*p != '}')
				continue;
			p++;
			if (pr > 0) {
				snprintf(buf, sizeof(buf),
					 "TIME '%02u:%02u:%02u.%0*lu'",
					 hr, mn, sc, pr, fr);
			} else {
				snprintf(buf, sizeof(buf),
					 "TIME '%02u:%02u:%02u'", hr, mn, sc);
			}
			n = (int) (q - nquery);
			pr = (int) (p - q);
			q = malloc(length - pr + strlen(buf) + 1);
			sprintf(q, "%.*s%s%s", n, nquery, buf, p);
			free(nquery);
			nquery = q;
			q += n;
		} else if (sscanf(p, "d '%u-%u-%u'%n", &yr, &mt, &dy, &n) >= 3) {
			p += n;
			while (*p == ' ')
				p++;
			if (*p != '}')
				continue;
			p++;
			pr = 0;
			snprintf(buf, sizeof(buf),
				 "DATE '%04u-%02u-%02u'", yr, mt, dy);
			n = (int) (q - nquery);
			pr = (int) (p - q);
			q = malloc(length - pr + strlen(buf) + 1);
			sprintf(q, "%.*s%s%s", n, nquery, buf, p);
			free(nquery);
			nquery = q;
			q += n;
		}
	}
	return nquery;
}

char *
ODBCParseOA(const char *tab, const char *col, const char *arg, size_t len)
{
	size_t i;
	char *res;
	const char *s;

	/* count length, counting ' and \ double */
	for (i = 0, s = arg; s < arg + len; i++, s++) {
		if (*s == '\'' || *s == '\\')
			i++;
	}
	i += strlen(tab) + strlen(col) + 10; /* ""."" = '' */
	res = malloc(i + 1);
	snprintf(res, i, "\"%s\".\"%s\" = '", tab, col);
	for (i = strlen(res), s = arg; s < arg + len; s++) {
		if (*s == '\'' || *s == '\\')
			res[i++] = *s;
		res[i++] = *s;
	}
	res[i++] = '\'';
	res[i] = 0;
	return res;
}

char *
ODBCParsePV(const char *tab, const char *col, const char *arg, size_t len)
{
	size_t i;
	char *res;
	const char *s;

	/* count length, counting ' and \ double */
	for (i = 0, s = arg; s < arg + len; i++, s++) {
		if (*s == '\'' || *s == '\\')
			i++;
	}
	i += strlen(tab) + strlen(col) + 25; /* ""."" like '' escape '\\' */
	res = malloc(i + 1);
	snprintf(res, i, "\"%s\".\"%s\" like '", tab, col);
	for (i = strlen(res), s = arg; s < arg + len; s++) {
		if (*s == '\'' || *s == '\\')
			res[i++] = *s;
		res[i++] = *s;
	}
	for (s = "' escape '\\\\'"; *s; s++)
		res[i++] = *s;
	res[i] = 0;
	return res;
}

char *
ODBCParseID(const char *tab, const char *col, const char *arg, size_t len)
{
	size_t i;
	char *res;
	const char *s;
	int fold = 1;

	while (len > 0 && (arg[--len] == ' ' || arg[len] == '\t'))
		;
	len++;
	if (len >= 2 && *arg == '"' && arg[len - 1] == '"') {
		arg++;
		len -= 2;
		fold = 0;
	}

	for (i = 0, s = arg; s < arg + len; i++, s++) {
		if (*s == '\'' || *s == '\\')
			i++;
	}
	i += strlen(tab) + strlen(col) + 10; /* ""."" = '' */
	if (fold)
		i += 14;	/* 2 times upper() */
	res = malloc(i + 1);
	if (fold)
		snprintf(res, i, "upper(\"%s\".\"%s\") = upper('", tab, col);
	else
		snprintf(res, i, "\"%s\".\"%s\" = '", tab, col);
	for (i = strlen(res); len != 0; len--, arg++) {
		if (*arg == '\'' || *arg == '\\')
			res[i++] = *arg;
		res[i++] = *arg;
	}
	res[i++] = '\'';
	if (fold)
		res[i++] = ')';
	res[i] = 0;
	return res;
}

struct sql_types ODBC_sql_types[] = {
	{SQL_CHAR, SQL_CHAR, 0, 0, UNAFFECTED, 1, UNAFFECTED, 0, SQL_FALSE},
	{SQL_VARCHAR, SQL_VARCHAR, 0, 0, UNAFFECTED, 1, UNAFFECTED, 0, SQL_FALSE},
	{SQL_LONGVARCHAR, SQL_LONGVARCHAR, 0, UNAFFECTED, UNAFFECTED, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_WCHAR, SQL_WCHAR, 0, UNAFFECTED, UNAFFECTED, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_WVARCHAR, SQL_WVARCHAR, 0, UNAFFECTED, UNAFFECTED, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_WLONGVARCHAR, SQL_WLONGVARCHAR, 0, UNAFFECTED, UNAFFECTED, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_DECIMAL, SQL_DECIMAL, 0, 17, UNAFFECTED, UNAFFECTED, 0, 10, SQL_TRUE},
	{SQL_NUMERIC, SQL_NUMERIC, 0, 17, UNAFFECTED, UNAFFECTED, 0, 10, SQL_TRUE},
	{SQL_BIT, SQL_BIT, 0, UNAFFECTED, UNAFFECTED, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_TINYINT, SQL_TINYINT, 0, UNAFFECTED, UNAFFECTED, UNAFFECTED, UNAFFECTED, 10, SQL_TRUE},
	{SQL_SMALLINT, SQL_SMALLINT, 0, UNAFFECTED, UNAFFECTED, UNAFFECTED, UNAFFECTED, 10, SQL_TRUE},
	{SQL_INTEGER, SQL_INTEGER, 0, UNAFFECTED, UNAFFECTED, UNAFFECTED, UNAFFECTED, 10, SQL_TRUE},
	{SQL_BIGINT, SQL_BIGINT, 0, UNAFFECTED, UNAFFECTED, UNAFFECTED, UNAFFECTED, 10, SQL_TRUE},
	{SQL_REAL, SQL_REAL, 0, FLT_MANT_DIG, UNAFFECTED, UNAFFECTED, UNAFFECTED, 2, SQL_FALSE},
	{SQL_FLOAT, SQL_FLOAT, 0, DBL_MANT_DIG, UNAFFECTED, UNAFFECTED, UNAFFECTED, 2, SQL_FALSE},
	{SQL_DOUBLE, SQL_DOUBLE, 0, DBL_MANT_DIG, UNAFFECTED, UNAFFECTED, UNAFFECTED, 2, SQL_FALSE},
	{SQL_BINARY, SQL_BINARY, 0, 0, UNAFFECTED, 1, UNAFFECTED, 0, SQL_FALSE},
	{SQL_VARBINARY, SQL_VARBINARY, 0, 0, UNAFFECTED, 1, UNAFFECTED, 0, SQL_FALSE},
	{SQL_LONGVARBINARY, SQL_LONGVARBINARY, 0, UNAFFECTED, UNAFFECTED, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_GUID, SQL_GUID, 0, UNAFFECTED, UNAFFECTED, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_TYPE_DATE, SQL_DATETIME, SQL_CODE_DATE, 0, UNAFFECTED, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_TYPE_TIME, SQL_DATETIME, SQL_CODE_TIME, 0, UNAFFECTED, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_TYPE_TIMESTAMP, SQL_DATETIME, SQL_CODE_TIMESTAMP, 6, UNAFFECTED, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_INTERVAL_MONTH, SQL_INTERVAL, SQL_CODE_MONTH, 0, 2, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_INTERVAL_YEAR, SQL_INTERVAL, SQL_CODE_YEAR, 0, 2, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_INTERVAL_YEAR_TO_MONTH, SQL_INTERVAL, SQL_CODE_YEAR_TO_MONTH, 0, 2, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_INTERVAL_DAY, SQL_INTERVAL, SQL_CODE_DAY, 0, 2, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_INTERVAL_HOUR, SQL_INTERVAL, SQL_CODE_HOUR, 0, 2, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_INTERVAL_MINUTE, SQL_INTERVAL, SQL_CODE_MINUTE, 0, 2, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_INTERVAL_SECOND, SQL_INTERVAL, SQL_CODE_SECOND, 6, 2, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_INTERVAL_DAY_TO_HOUR, SQL_INTERVAL, SQL_CODE_DAY_TO_HOUR, 0, 2, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_INTERVAL_DAY_TO_MINUTE, SQL_INTERVAL, SQL_CODE_DAY_TO_MINUTE, 0, 2, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_INTERVAL_DAY_TO_SECOND, SQL_INTERVAL, SQL_CODE_DAY_TO_SECOND, 6, 2, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_INTERVAL_HOUR_TO_MINUTE, SQL_INTERVAL, SQL_CODE_HOUR_TO_MINUTE, 0, 2, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_INTERVAL_HOUR_TO_SECOND, SQL_INTERVAL, SQL_CODE_HOUR_TO_SECOND, 6, 2, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_INTERVAL_MINUTE_TO_SECOND, SQL_INTERVAL, SQL_CODE_MINUTE_TO_SECOND, 6, 2, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{0, 0, 0, 0, 0, 0, 0, 0, 0},	/* sentinel */
};

struct sql_types ODBC_c_types[] = {
	{SQL_C_CHAR, SQL_C_CHAR, 0, 0, UNAFFECTED, 1, UNAFFECTED, 0, SQL_FALSE},
	{SQL_C_WCHAR, SQL_C_WCHAR, 0, UNAFFECTED, UNAFFECTED, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_C_BIT, SQL_C_BIT, 0, UNAFFECTED, UNAFFECTED, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_C_NUMERIC, SQL_C_NUMERIC, 0, 17, UNAFFECTED, UNAFFECTED, 0, 10, SQL_TRUE},
	{SQL_C_STINYINT, SQL_C_STINYINT, 0, UNAFFECTED, UNAFFECTED, UNAFFECTED, UNAFFECTED, 10, SQL_TRUE},
	{SQL_C_UTINYINT, SQL_C_UTINYINT, 0, UNAFFECTED, UNAFFECTED, UNAFFECTED, UNAFFECTED, 10, SQL_TRUE},
	{SQL_C_TINYINT, SQL_C_TINYINT, 0, UNAFFECTED, UNAFFECTED, UNAFFECTED, UNAFFECTED, 10, SQL_TRUE},
	{SQL_C_SBIGINT, SQL_C_SBIGINT, 0, UNAFFECTED, UNAFFECTED, UNAFFECTED, UNAFFECTED, 10, SQL_TRUE},
	{SQL_C_UBIGINT, SQL_C_UBIGINT, 0, UNAFFECTED, UNAFFECTED, UNAFFECTED, UNAFFECTED, 10, SQL_TRUE},
	{SQL_C_SSHORT, SQL_C_SSHORT, 0, UNAFFECTED, UNAFFECTED, UNAFFECTED, UNAFFECTED, 10, SQL_TRUE},
	{SQL_C_USHORT, SQL_C_USHORT, 0, UNAFFECTED, UNAFFECTED, UNAFFECTED, UNAFFECTED, 10, SQL_TRUE},
	{SQL_C_SHORT, SQL_C_SHORT, 0, UNAFFECTED, UNAFFECTED, UNAFFECTED, UNAFFECTED, 10, SQL_TRUE},
	{SQL_C_SLONG, SQL_C_SLONG, 0, UNAFFECTED, UNAFFECTED, UNAFFECTED, UNAFFECTED, 10, SQL_TRUE},
	{SQL_C_ULONG, SQL_C_ULONG, 0, UNAFFECTED, UNAFFECTED, UNAFFECTED, UNAFFECTED, 10, SQL_TRUE},
	{SQL_C_LONG, SQL_C_LONG, 0, UNAFFECTED, UNAFFECTED, UNAFFECTED, UNAFFECTED, 10, SQL_TRUE},
	{SQL_C_FLOAT, SQL_C_FLOAT, 0, FLT_MANT_DIG, UNAFFECTED, UNAFFECTED, UNAFFECTED, 2, SQL_FALSE},
	{SQL_C_DOUBLE, SQL_C_DOUBLE, 0, DBL_MANT_DIG, UNAFFECTED, UNAFFECTED, UNAFFECTED, 2, SQL_FALSE},
	{SQL_C_BINARY, SQL_C_BINARY, 0, UNAFFECTED, UNAFFECTED, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_C_TYPE_DATE, SQL_DATETIME, SQL_CODE_DATE, 0, UNAFFECTED, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_C_TYPE_TIME, SQL_DATETIME, SQL_CODE_TIME, 0, UNAFFECTED, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_C_TYPE_TIMESTAMP, SQL_DATETIME, SQL_CODE_TIMESTAMP, 6, UNAFFECTED, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_C_INTERVAL_MONTH, SQL_INTERVAL, SQL_CODE_MONTH, UNAFFECTED, 2, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_C_INTERVAL_YEAR, SQL_INTERVAL, SQL_CODE_YEAR, UNAFFECTED, 2, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_C_INTERVAL_YEAR_TO_MONTH, SQL_INTERVAL, SQL_CODE_YEAR_TO_MONTH, UNAFFECTED, 2, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_C_INTERVAL_DAY, SQL_INTERVAL, SQL_CODE_DAY, UNAFFECTED, 2, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_C_INTERVAL_HOUR, SQL_INTERVAL, SQL_CODE_HOUR, UNAFFECTED, 2, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_C_INTERVAL_MINUTE, SQL_INTERVAL, SQL_CODE_MINUTE, UNAFFECTED, 2, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_C_INTERVAL_SECOND, SQL_INTERVAL, SQL_CODE_SECOND, UNAFFECTED, 6, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_C_INTERVAL_DAY_TO_HOUR, SQL_INTERVAL, SQL_CODE_DAY_TO_HOUR, UNAFFECTED, 2, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_C_INTERVAL_DAY_TO_MINUTE, SQL_INTERVAL, SQL_CODE_DAY_TO_MINUTE, UNAFFECTED, 2, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_C_INTERVAL_DAY_TO_SECOND, SQL_INTERVAL, SQL_CODE_DAY_TO_SECOND, UNAFFECTED, 6, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_C_INTERVAL_HOUR_TO_MINUTE, SQL_INTERVAL, SQL_CODE_HOUR_TO_MINUTE, UNAFFECTED, 2, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_C_INTERVAL_HOUR_TO_SECOND, SQL_INTERVAL, SQL_CODE_HOUR_TO_SECOND, UNAFFECTED, 6, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_C_INTERVAL_MINUTE_TO_SECOND, SQL_INTERVAL, SQL_CODE_MINUTE_TO_SECOND, UNAFFECTED, 6, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_C_GUID, SQL_C_GUID, 0, UNAFFECTED, UNAFFECTED, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{SQL_C_DEFAULT, SQL_C_DEFAULT, 0, UNAFFECTED, UNAFFECTED, UNAFFECTED, UNAFFECTED, 0, SQL_FALSE},
	{0, 0, 0, 0, 0, 0, 0, 0, 0},	/* sentinel */
};

#ifdef ODBCDEBUG

const char *ODBCdebug;

char *
translateCType(SQLSMALLINT ValueType)
{
	switch (ValueType) {
	case SQL_C_CHAR:
		return "SQL_C_CHAR";
	case SQL_C_WCHAR:
		return "SQL_C_WCHAR";
	case SQL_C_BINARY:
		return "SQL_C_BINARY";
	case SQL_C_BIT:
		return "SQL_C_BIT";
	case SQL_C_STINYINT:
		return "SQL_C_STINYINT";
	case SQL_C_UTINYINT:
		return "SQL_C_UTINYINT";
	case SQL_C_TINYINT:
		return "SQL_C_TINYINT";
	case SQL_C_SSHORT:
		return "SQL_C_SSHORT";
	case SQL_C_USHORT:
		return "SQL_C_USHORT";
	case SQL_C_SHORT:
		return "SQL_C_SHORT";
	case SQL_C_SLONG:
		return "SQL_C_SLONG";
	case SQL_C_ULONG:
		return "SQL_C_ULONG";
	case SQL_C_LONG:
		return "SQL_C_LONG";
	case SQL_C_SBIGINT:
		return "SQL_C_SBIGINT";
	case SQL_C_UBIGINT:
		return "SQL_C_UBIGINT";
	case SQL_C_NUMERIC:
		return "SQL_C_NUMERIC";
	case SQL_C_FLOAT:
		return "SQL_C_FLOAT";
	case SQL_C_DOUBLE:
		return "SQL_C_DOUBLE";
	case SQL_C_TYPE_DATE:
		return "SQL_C_TYPE_DATE";
	case SQL_C_TYPE_TIME:
		return "SQL_C_TYPE_TIME";
	case SQL_C_TYPE_TIMESTAMP:
		return "SQL_C_TYPE_TIMESTAMP";
	case SQL_C_INTERVAL_YEAR:
		return "SQL_C_INTERVAL_YEAR";
	case SQL_C_INTERVAL_MONTH:
		return "SQL_C_INTERVAL_MONTH";
	case SQL_C_INTERVAL_YEAR_TO_MONTH:
		return "SQL_C_INTERVAL_YEAR_TO_MONTH";
	case SQL_C_INTERVAL_DAY:
		return "SQL_C_INTERVAL_DAY";
	case SQL_C_INTERVAL_HOUR:
		return "SQL_C_INTERVAL_HOUR";
	case SQL_C_INTERVAL_MINUTE:
		return "SQL_C_INTERVAL_MINUTE";
	case SQL_C_INTERVAL_SECOND:
		return "SQL_C_INTERVAL_SECOND";
	case SQL_C_INTERVAL_DAY_TO_HOUR:
		return "SQL_C_INTERVAL_DAY_TO_HOUR";
	case SQL_C_INTERVAL_DAY_TO_MINUTE:
		return "SQL_C_INTERVAL_DAY_TO_MINUTE";
	case SQL_C_INTERVAL_DAY_TO_SECOND:
		return "SQL_C_INTERVAL_DAY_TO_SECOND";
	case SQL_C_INTERVAL_HOUR_TO_MINUTE:
		return "SQL_C_INTERVAL_HOUR_TO_MINUTE";
	case SQL_C_INTERVAL_HOUR_TO_SECOND:
		return "SQL_C_INTERVAL_HOUR_TO_SECOND";
	case SQL_C_INTERVAL_MINUTE_TO_SECOND:
		return "SQL_C_INTERVAL_MINUTE_TO_SECOND";
	case SQL_C_GUID:
		return "SQL_C_GUID";
	case SQL_C_DEFAULT:
		return "SQL_C_DEFAULT";
	case SQL_ARD_TYPE:
		return "SQL_ARD_TYPE";
	case SQL_DATETIME:
		return "SQL_DATETIME";
	case SQL_INTERVAL:
		return "SQL_INTERVAL";
	default:
		return "unknown";
	}
}

char *
translateSQLType(SQLSMALLINT ParameterType)
{
	switch (ParameterType) {
	case SQL_CHAR:
		return "SQL_CHAR";
	case SQL_VARCHAR:
		return "SQL_VARCHAR";
	case SQL_LONGVARCHAR:
		return "SQL_LONGVARCHAR";
	case SQL_BINARY:
		return "SQL_BINARY";
	case SQL_VARBINARY:
		return "SQL_VARBINARY";
	case SQL_LONGVARBINARY:
		return "SQL_LONGVARBINARY";
	case SQL_TYPE_DATE:
		return "SQL_TYPE_DATE";
	case SQL_INTERVAL_MONTH:
		return "SQL_INTERVAL_MONTH";
	case SQL_INTERVAL_YEAR:
		return "SQL_INTERVAL_YEAR";
	case SQL_INTERVAL_YEAR_TO_MONTH:
		return "SQL_INTERVAL_YEAR_TO_MONTH";
	case SQL_INTERVAL_DAY:
		return "SQL_INTERVAL_DAY";
	case SQL_INTERVAL_HOUR:
		return "SQL_INTERVAL_HOUR";
	case SQL_INTERVAL_MINUTE:
		return "SQL_INTERVAL_MINUTE";
	case SQL_INTERVAL_DAY_TO_HOUR:
		return "SQL_INTERVAL_DAY_TO_HOUR";
	case SQL_INTERVAL_DAY_TO_MINUTE:
		return "SQL_INTERVAL_DAY_TO_MINUTE";
	case SQL_INTERVAL_HOUR_TO_MINUTE:
		return "SQL_INTERVAL_HOUR_TO_MINUTE";
	case SQL_TYPE_TIME:
		return "SQL_TYPE_TIME";
	case SQL_TYPE_TIMESTAMP:
		return "SQL_TYPE_TIMESTAMP";
	case SQL_INTERVAL_SECOND:
		return "SQL_INTERVAL_SECOND";
	case SQL_INTERVAL_DAY_TO_SECOND:
		return "SQL_INTERVAL_DAY_TO_SECOND";
	case SQL_INTERVAL_HOUR_TO_SECOND:
		return "SQL_INTERVAL_HOUR_TO_SECOND";
	case SQL_INTERVAL_MINUTE_TO_SECOND:
		return "SQL_INTERVAL_MINUTE_TO_SECOND";
	case SQL_DECIMAL:
		return "SQL_DECIMAL";
	case SQL_NUMERIC:
		return "SQL_NUMERIC";
	case SQL_FLOAT:
		return "SQL_FLOAT";
	case SQL_REAL:
		return "SQL_REAL";
	case SQL_DOUBLE:
		return "SQL_DOUBLE";
	case SQL_WCHAR:
		return "SQL_WCHAR";
	case SQL_WVARCHAR:
		return "SQL_WVARCHAR";
	case SQL_WLONGVARCHAR:
		return "SQL_WLONGVARCHAR";
	case SQL_BIT:
		return "SQL_BIT";
	case SQL_TINYINT:
		return "SQL_TINYINT";
	case SQL_SMALLINT:
		return "SQL_SMALLINT";
	case SQL_INTEGER:
		return "SQL_INTEGER";
	case SQL_BIGINT:
		return "SQL_BIGINT";
	case SQL_GUID:
		return "SQL_GUID";
	case SQL_DATETIME:
		return "SQL_DATETIME";
	case SQL_INTERVAL:
		return "SQL_INTERVAL";
	default:
		return "unknown";
	}
}

char *
translateFieldIdentifier(SQLSMALLINT FieldIdentifier)
{
	switch (FieldIdentifier) {
	case SQL_COLUMN_LENGTH:
		return "SQL_COLUMN_LENGTH";
	case SQL_COLUMN_PRECISION:
		return "SQL_COLUMN_PRECISION";
	case SQL_COLUMN_SCALE:
		return "SQL_COLUMN_SCALE";
	case SQL_DESC_AUTO_UNIQUE_VALUE:
		return "SQL_DESC_AUTO_UNIQUE_VALUE";
	case SQL_DESC_BASE_COLUMN_NAME:
		return "SQL_DESC_BASE_COLUMN_NAME";
	case SQL_DESC_BASE_TABLE_NAME:
		return "SQL_DESC_BASE_TABLE_NAME";
	case SQL_DESC_CASE_SENSITIVE:
		return "SQL_DESC_CASE_SENSITIVE";
	case SQL_DESC_CATALOG_NAME:
		return "SQL_DESC_CATALOG_NAME";
	case SQL_DESC_CONCISE_TYPE:
		return "SQL_DESC_CONCISE_TYPE";
	case SQL_DESC_COUNT:
		return "SQL_DESC_COUNT";
	case SQL_DESC_DATA_PTR:
		return "SQL_DESC_DATA_PTR";
	case SQL_DESC_DATETIME_INTERVAL_CODE:
		return "SQL_DESC_DATETIME_INTERVAL_CODE";
	case SQL_DESC_DATETIME_INTERVAL_PRECISION:
		return "SQL_DESC_DATETIME_INTERVAL_PRECISION";
	case SQL_DESC_DISPLAY_SIZE:
		return "SQL_DESC_DISPLAY_SIZE";
	case SQL_DESC_FIXED_PREC_SCALE:
		return "SQL_DESC_FIXED_PREC_SCALE";
	case SQL_DESC_INDICATOR_PTR:
		return "SQL_DESC_INDICATOR_PTR";
	case SQL_DESC_LABEL:
		return "SQL_DESC_LABEL";
	case SQL_DESC_LENGTH:
		return "SQL_DESC_LENGTH";
	case SQL_DESC_LITERAL_PREFIX:
		return "SQL_DESC_LITERAL_PREFIX";
	case SQL_DESC_LITERAL_SUFFIX:
		return "SQL_DESC_LITERAL_SUFFIX";
	case SQL_DESC_LOCAL_TYPE_NAME:
		return "SQL_DESC_LOCAL_TYPE_NAME";
	case SQL_DESC_NAME:
		return "SQL_DESC_NAME";
	case SQL_DESC_NULLABLE:
		return "SQL_DESC_NULLABLE";
	case SQL_DESC_NUM_PREC_RADIX:
		return "SQL_DESC_NUM_PREC_RADIX";
	case SQL_DESC_OCTET_LENGTH:
		return "SQL_DESC_OCTET_LENGTH";
	case SQL_DESC_OCTET_LENGTH_PTR:
		return "SQL_DESC_OCTET_LENGTH_PTR";
	case SQL_DESC_PARAMETER_TYPE:
		return "SQL_DESC_PARAMETER_TYPE";
	case SQL_DESC_PRECISION:
		return "SQL_DESC_PRECISION";
	case SQL_DESC_ROWVER:
		return "SQL_DESC_ROWVER";
	case SQL_DESC_SCALE:
		return "SQL_DESC_SCALE";
	case SQL_DESC_SCHEMA_NAME:
		return "SQL_DESC_SCHEMA_NAME";
	case SQL_DESC_SEARCHABLE:
		return "SQL_DESC_SEARCHABLE";
	case SQL_DESC_TABLE_NAME:
		return "SQL_DESC_TABLE_NAME";
	case SQL_DESC_TYPE:
		return "SQL_DESC_TYPE";
	case SQL_DESC_TYPE_NAME:
		return "SQL_DESC_TYPE_NAME";
	case SQL_DESC_UNNAMED:
		return "SQL_DESC_UNNAMED";
	case SQL_DESC_UNSIGNED:
		return "SQL_DESC_UNSIGNED";
	case SQL_DESC_UPDATABLE:
		return "SQL_DESC_UPDATABLE";
	default:
		return "unknown";
	}
}

char *
translateFetchOrientation(SQLUSMALLINT FetchOrientation)
{
	switch (FetchOrientation) {
	case SQL_FETCH_NEXT:
		return "SQL_FETCH_NEXT";
	case SQL_FETCH_FIRST:
		return "SQL_FETCH_FIRST";
	case SQL_FETCH_LAST:
		return "SQL_FETCH_LAST";
	case SQL_FETCH_PRIOR:
		return "SQL_FETCH_PRIOR";
	case SQL_FETCH_RELATIVE:
		return "SQL_FETCH_RELATIVE";
	case SQL_FETCH_ABSOLUTE:
		return "SQL_FETCH_ABSOLUTE";
	case SQL_FETCH_BOOKMARK:
		return "SQL_FETCH_BOOKMARK";
	default:
		return "unknown";
	}
}

char *
translateConnectAttribute(SQLINTEGER Attribute)
{
	switch (Attribute) {
	case SQL_ATTR_ACCESS_MODE:
		return "SQL_ATTR_ACCESS_MODE";
	case SQL_ATTR_ASYNC_ENABLE:
		return "SQL_ATTR_ASYNC_ENABLE";
	case SQL_ATTR_AUTOCOMMIT:
		return "SQL_ATTR_AUTOCOMMIT";
	case SQL_ATTR_AUTO_IPD:
		return "SQL_ATTR_AUTO_IPD";
	case SQL_ATTR_CONNECTION_DEAD:
		return "SQL_ATTR_CONNECTION_DEAD";
	case SQL_ATTR_CONNECTION_TIMEOUT:
		return "SQL_ATTR_CONNECTION_TIMEOUT";
	case SQL_ATTR_CURRENT_CATALOG:
		return "SQL_ATTR_CURRENT_CATALOG";
	case SQL_ATTR_DISCONNECT_BEHAVIOR:
		return "SQL_ATTR_DISCONNECT_BEHAVIOR";
	case SQL_ATTR_ENLIST_IN_DTC:
		return "SQL_ATTR_ENLIST_IN_DTC";
	case SQL_ATTR_ENLIST_IN_XA:
		return "SQL_ATTR_ENLIST_IN_XA";
	case SQL_ATTR_LOGIN_TIMEOUT:
		return "SQL_ATTR_LOGIN_TIMEOUT";
	case SQL_ATTR_METADATA_ID:
		return "SQL_ATTR_METADATA_ID";
	case SQL_ATTR_ODBC_CURSORS:
		return "SQL_ATTR_ODBC_CURSORS";
	case SQL_ATTR_PACKET_SIZE:
		return "SQL_ATTR_PACKET_SIZE";
	case SQL_ATTR_QUIET_MODE:
		return "SQL_ATTR_QUIET_MODE";
	case SQL_ATTR_TRACE:
		return "SQL_ATTR_TRACE";
	case SQL_ATTR_TRACEFILE:
		return "SQL_ATTR_TRACEFILE";
	case SQL_ATTR_TRANSLATE_LIB:
		return "SQL_ATTR_TRANSLATE_LIB";
	case SQL_ATTR_TRANSLATE_OPTION:
		return "SQL_ATTR_TRANSLATE_OPTION";
	case SQL_ATTR_TXN_ISOLATION:
		return "SQL_ATTR_TXN_ISOLATION";
	default:
		return "unknown";
	}
}

char *
translateConnectOption(SQLUSMALLINT Option)
{
	switch (Option) {
	case SQL_ACCESS_MODE:
		return "SQL_ACCESS_MODE";
	case SQL_AUTOCOMMIT:
		return "SQL_AUTOCOMMIT";
	case SQL_LOGIN_TIMEOUT:
		return "SQL_LOGIN_TIMEOUT";
	case SQL_ODBC_CURSORS:
		return "SQL_ODBC_CURSORS";
	case SQL_OPT_TRACE:
		return "SQL_OPT_TRACE";
	case SQL_PACKET_SIZE:
		return "SQL_PACKET_SIZE";
	case SQL_TRANSLATE_OPTION:
		return "SQL_TRANSLATE_OPTION";
	case SQL_TXN_ISOLATION:
		return "SQL_TXN_ISOLATION";
	case SQL_QUIET_MODE:
		return "SQL_QUIET_MODE";
	case SQL_CURRENT_QUALIFIER:
		return "SQL_CURRENT_QUALIFIER";
	case SQL_OPT_TRACEFILE:
		return "SQL_OPT_TRACEFILE";
	case SQL_TRANSLATE_DLL:
		return "SQL_TRANSLATE_DLL";
	default:
		return translateConnectAttribute((SQLSMALLINT) Option);
	}
}

char *
translateEnvAttribute(SQLINTEGER Attribute)
{
	switch (Attribute) {
	case SQL_ATTR_ODBC_VERSION:
		return "SQL_ATTR_ODBC_VERSION";
	case SQL_ATTR_OUTPUT_NTS:
		return "SQL_ATTR_OUTPUT_NTS";
	case SQL_ATTR_CONNECTION_POOLING:
		return "SQL_ATTR_CONNECTION_POOLING";
	case SQL_ATTR_CP_MATCH:
		return "SQL_ATTR_CP_MATCH";
	default:
		return "unknown";
	}
}

char *
translateStmtAttribute(SQLINTEGER Attribute)
{
	switch (Attribute) {
	case SQL_ATTR_APP_PARAM_DESC:
		return "SQL_ATTR_APP_PARAM_DESC";
	case SQL_ATTR_APP_ROW_DESC:
		return "SQL_ATTR_APP_ROW_DESC";
	case SQL_ATTR_ASYNC_ENABLE:
		return "SQL_ATTR_ASYNC_ENABLE";
	case SQL_ATTR_CONCURRENCY:
		return "SQL_ATTR_CONCURRENCY";
	case SQL_ATTR_CURSOR_SCROLLABLE:
		return "SQL_ATTR_CURSOR_SCROLLABLE";
	case SQL_ATTR_CURSOR_SENSITIVITY:
		return "SQL_ATTR_CURSOR_SENSITIVITY";
	case SQL_ATTR_CURSOR_TYPE:
		return "SQL_ATTR_CURSOR_TYPE";
	case SQL_ATTR_IMP_PARAM_DESC:
		return "SQL_ATTR_IMP_PARAM_DESC";
	case SQL_ATTR_IMP_ROW_DESC:
		return "SQL_ATTR_IMP_ROW_DESC";
	case SQL_ATTR_MAX_LENGTH:
		return "SQL_ATTR_MAX_LENGTH";
	case SQL_ATTR_MAX_ROWS:
		return "SQL_ATTR_MAX_ROWS";
	case SQL_ATTR_NOSCAN:
		return "SQL_ATTR_NOSCAN";
	case SQL_ATTR_PARAM_BIND_OFFSET_PTR:
		return "SQL_ATTR_PARAM_BIND_OFFSET_PTR";
	case SQL_ATTR_PARAM_BIND_TYPE:
		return "SQL_ATTR_PARAM_BIND_TYPE";
	case SQL_ATTR_PARAM_OPERATION_PTR:
		return "SQL_ATTR_PARAM_OPERATION_PTR";
	case SQL_ATTR_PARAM_STATUS_PTR:
		return "SQL_ATTR_PARAM_STATUS_PTR";
	case SQL_ATTR_PARAMS_PROCESSED_PTR:
		return "SQL_ATTR_PARAMS_PROCESSED_PTR";
	case SQL_ATTR_PARAMSET_SIZE:
		return "SQL_ATTR_PARAMSET_SIZE";
	case SQL_ATTR_RETRIEVE_DATA:
		return "SQL_ATTR_RETRIEVE_DATA";
	case SQL_ATTR_ROW_ARRAY_SIZE:
		return "SQL_ATTR_ROW_ARRAY_SIZE";
	case SQL_ROWSET_SIZE:
		return "SQL_ROWSET_SIZE";
	case SQL_ATTR_ROW_BIND_OFFSET_PTR:
		return "SQL_ATTR_ROW_BIND_OFFSET_PTR";
	case SQL_ATTR_ROW_BIND_TYPE:
		return "SQL_ATTR_ROW_BIND_TYPE";
	case SQL_ATTR_ROW_NUMBER:
		return "SQL_ATTR_ROW_NUMBER";
	case SQL_ATTR_ROW_OPERATION_PTR:
		return "SQL_ATTR_ROW_OPERATION_PTR";
	case SQL_ATTR_ROW_STATUS_PTR:
		return "SQL_ATTR_ROW_STATUS_PTR";
	case SQL_ATTR_ROWS_FETCHED_PTR:
		return "SQL_ATTR_ROWS_FETCHED_PTR";
	case SQL_ATTR_METADATA_ID:
		return "SQL_ATTR_METADATA_ID";
	case SQL_ATTR_ENABLE_AUTO_IPD:
		return "SQL_ATTR_ENABLE_AUTO_IPD";
	case SQL_ATTR_FETCH_BOOKMARK_PTR:
		return "SQL_ATTR_FETCH_BOOKMARK_PTR";
	case SQL_ATTR_KEYSET_SIZE:
		return "SQL_ATTR_KEYSET_SIZE";
	case SQL_ATTR_QUERY_TIMEOUT:
		return "SQL_ATTR_QUERY_TIMEOUT";
	case SQL_ATTR_SIMULATE_CURSOR:
		return "SQL_ATTR_SIMULATE_CURSOR";
	case SQL_ATTR_USE_BOOKMARKS:
		return "SQL_ATTR_USE_BOOKMARKS";
	default:
		return "unknown";
	}
}

char *
translateStmtOption(SQLUSMALLINT Option)
{
	switch (Option) {
	case SQL_QUERY_TIMEOUT:
		return "SQL_QUERY_TIMEOUT";
	case SQL_MAX_ROWS:
		return "SQL_MAX_ROWS";
	case SQL_NOSCAN:
		return "SQL_NOSCAN";
	case SQL_MAX_LENGTH:
		return "SQL_MAX_LENGTH";
	case SQL_ASYNC_ENABLE:
		return "SQL_ASYNC_ENABLE";
	case SQL_BIND_TYPE:
		return "SQL_BIND_TYPE";
	case SQL_CURSOR_TYPE:
		return "SQL_CURSOR_TYPE";
	case SQL_CONCURRENCY:
		return "SQL_CONCURRENCY";
	case SQL_KEYSET_SIZE:
		return "SQL_KEYSET_SIZE";
	case SQL_ROWSET_SIZE:
		return "SQL_ROWSET_SIZE";
	case SQL_SIMULATE_CURSOR:
		return "SQL_SIMULATE_CURSOR";
	case SQL_RETRIEVE_DATA:
		return "SQL_RETRIEVE_DATA";
	case SQL_USE_BOOKMARKS:
		return "SQL_USE_BOOKMARKS";
	case SQL_ROW_NUMBER:
		return "SQL_ROW_NUMBER";
	default:
		return "unknown";
	}
}

char *
translateCompletionType(SQLSMALLINT CompletionType)
{
	switch (CompletionType) {
	case SQL_COMMIT:
		return "SQL_COMMIT";
	case SQL_ROLLBACK:
		return "SQL_ROLLBACK";
	default:
		return "unknown";
	}
}

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901
#include <stdarg.h>

void
ODBCLOG(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (ODBCdebug == NULL) {
		if ((ODBCdebug = getenv("ODBCDEBUG")) == NULL)
			ODBCdebug = strdup("");
		else
			ODBCdebug = strdup(ODBCdebug);
	}
	if (ODBCdebug != NULL && *ODBCdebug != 0) {
		FILE *f;

		f = fopen(ODBCdebug, "a");
		if (f) {
			vfprintf(f, fmt, ap);
			fclose(f);
		} else
			vfprintf(stderr, fmt, ap);
	}
	va_end(ap);
}
#endif
#endif

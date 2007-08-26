 /*
  

 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2005-2006 NFG Net Facilities Group BV support@nfg.nl

 This program is free software; you can redistribute it and/or 
 modify it under the terms of the GNU General Public License 
 as published by the Free Software Foundation; either 
 version 2 of the License, or (at your option) any later 
 version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*
 * memblock.c
 *
 * implementations of functions declared in memblock.h
 */

#include "dbmail.h"

#define MAX_ERROR_SIZE 128

enum __M_ERRORS { M_NOERROR, M_NOMEM, M_BADMEM, M_BADDATA, M_BADWHENCE,
	    M_LASTERR };

const char *__m_error_desc[M_LASTERR] = {
	"no error", "not enough memory", "bad memory structure specified",
	"bad data block specified", "bad whence indicator specified"
};

int __m_errno;
char __m_error_str[MAX_ERROR_SIZE];

/* internal use only */
int __m_blkadd(MEM * m);


/*
 * mopen()
 *
 * opens a mem-structure
 */
MEM *mopen()
{
	MEM *mp = g_new0(MEM, 1);

	if (!mp) {
		__m_errno = M_NOMEM;
		return NULL;
	}

	mp->firstblk = g_new0(memblock_t, 1);

	memset(mp->firstblk->data, 0, _MEMBLOCK_SIZE);

	mp->firstblk->nextblk = NULL;
	mp->firstblk->prevblk = NULL;

	mp->lastblk = mp->firstblk;
	mp->currblk = mp->firstblk;

	mp->nblocks = 1;

	__m_errno = M_NOERROR;
	return mp;
}


/*
 * mclose()
 *
 * closes a mem structure
 *
 */
void mclose(MEM ** m)
{
	memblock_t *tmp, *next;

	__m_errno = M_NOERROR;

	if (!m || !(*m))
		return;

	tmp = (*m)->firstblk;
	while (tmp) {
		next = tmp->nextblk;	/* save address */
		g_free(tmp);
		tmp = next;
	}

	g_free(*m);
	*m = NULL;

	return;
}


/*
 * mwrite()
 *
 * writes size bytes of data to the memory associated with m
 */
int mwrite(const void *data, int size, MEM * m)
{
	long left;

	if (!m) {
		__m_errno = M_BADMEM;
		return 0;
	}

	if (!data) {
		__m_errno = M_BADDATA;
		return 0;
	}

	if (size <= 0)
		return 0;


	left = _MEMBLOCK_SIZE - m->mpos;

	if (size <= left) {
		/* entire fit */
		memmove(&m->currblk->data[m->mpos], data, size);
		m->mpos += size;

		if (size == left) {
			/* update */
			if (m->currblk == m->lastblk) {
				if (!__m_blkadd(m)) {
					m->mpos--;
					m->eom = m->mpos;
					return size - 1;
				}
			}

			m->currblk = m->currblk->nextblk;
			m->mpos = 0;
		}

		if (m->currblk == m->lastblk && m->mpos > m->eom)
			m->eom = m->mpos;

		return size;
	}

	/* copy everything that can be placed */
	memmove(&m->currblk->data[m->mpos], data, left);
	m->mpos += left;

	if (m->currblk == m->lastblk) {
		/* need a new block */
		if (!__m_blkadd(m))
			return left;

		m->eom = 0;
	}

	m->currblk = m->currblk->nextblk;	/* advance current block */
	m->mpos = 0;

	return left + mwrite(&((char *) data)[left], size - left, m);
}


/*
 * mread()
 *
 * reads up to size bytes from m into data
 *
 * returns the number of bytes actually read
 */
int mread(void *data, int size, MEM * m)
{
	long left;

	if (!m) {
		__m_errno = M_BADMEM;
		return 0;
	}

	if (!data) {
		__m_errno = M_BADDATA;
		return 0;
	}

	if (size <= 0)
		return 0;

	if (m->lastblk == m->currblk)
		left = m->eom - m->mpos;
	else
		left = _MEMBLOCK_SIZE - m->mpos;

	if (left <= 0)
		return 0;

	if (size < left) {
		/* entire fit */
		memmove(data, &m->currblk->data[m->mpos], size);
		m->mpos += size;

		return size;
	}

	/* copy everything that can be placed */
	memmove(data, &m->currblk->data[m->mpos], left);
	m->mpos += left;

	if (m->currblk == m->lastblk) {
		/* no more data */
		return left;
	}

	m->currblk = m->currblk->nextblk;	/* advance current block */
	m->mpos = 0;

	return left + mread(&((char *) data)[left], size - left, m);
}


/*
 * mseek()
 *
 * moves the current pos in m offset bytes according to whence:
 * SEEK_SET seek from the beginning
 * SEEK_CUR seek from the current pos
 * SEEK_END seek from the end
 *
 * returns 0 on succes, -1 on error
 */
int mseek(MEM * m, long offset, int whence)
{
	long left;

	if (!m) {
		__m_errno = M_BADMEM;
		return -1;
	}

	switch (whence) {
	case SEEK_SET:
		m->currblk = m->firstblk;
		m->mpos = 0;

		if (offset <= 0)
			return 0;

		return mseek(m, offset, SEEK_CUR);

	case SEEK_CUR:
		if (offset == 0)
			return 0;

		if (offset > 0) {
			left = _MEMBLOCK_SIZE - m->mpos;
			if (offset >= left) {
				if (m->currblk == m->lastblk) {
					m->mpos = m->eom;
					return 0;
				}

				m->currblk = m->currblk->nextblk;
				m->mpos = 0;
				return mseek(m, offset - left, SEEK_CUR);
			} else {
				m->mpos += offset;

				if (m->currblk == m->lastblk
				    && m->mpos > m->eom)
					m->mpos = m->eom;

				return 0;
			}
		} else {
			/* offset < 0, walk backwards */
			left = -m->mpos;

			if (offset <= left) {
				if (m->currblk == m->firstblk) {
					m->mpos = 0;
					return 0;
				}

				m->currblk = m->currblk->prevblk;
				m->mpos = _MEMBLOCK_SIZE;
				return mseek(m, offset - left, SEEK_CUR);
			} else {
				m->mpos += offset;	/* remember: offset<0 */
				return 0;
			}
		}

	case SEEK_END:
		m->currblk = m->lastblk;
		m->mpos = m->eom;

		if (offset >= 0)
			return 0;

		return mseek(m, offset, SEEK_CUR);

	default:
		__m_errno = M_BADWHENCE;
		return -1;
	}

	return 0;
}


/*
 * mrewind()
 *
 * equivalent to mseek(m, 0, SEEK_SET)
 */
void mrewind(MEM * m)
{
	mseek(m, 0, SEEK_SET);
	__m_errno = M_NOERROR;
}


/* 
 * __m_blkadd()
 * adds a block to m
 * returns 0 on failure, 1 on succes
 */
int __m_blkadd(MEM * m)
{
	memblock_t *newblk;

	if (!m) {
		__m_errno = M_BADMEM;
		return 0;
	}

	newblk = g_new0(memblock_t, 1);

	memset(newblk->data, 0, _MEMBLOCK_SIZE);

	newblk->prevblk = m->lastblk;
	newblk->nextblk = NULL;

	m->nblocks++;
	m->lastblk->nextblk = newblk;
	m->lastblk = newblk;
	return 1;
}

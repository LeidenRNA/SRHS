/*
 * Copyright (c) 2007,2008,2009,2010 Mij <mij@bitchx.it>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * SimCList library. See http://mij.oltrelinux.com/devel/simclist
 */

/* SimCList implementation, version 1.5 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <limits.h>
#include <stdint.h>
#include "util.h"

/* work around lack of inttypes.h support in broken Microsoft Visual Studio compilers */
#if !defined(WIN32) || !defined(_MSC_VER)
#   include <inttypes.h>   /* (u)int*_t */
#else
#   include <basetsd.h>
typedef UINT8   uint8_t;
typedef UINT16  uint16_t;
typedef ULONG32 uint32_t;
typedef UINT64  uint64_t;
typedef INT8    int8_t;
typedef INT16   int16_t;
typedef LONG32  int32_t;
typedef INT64   int64_t;
#endif

#include "simclist.h"

static int list_drop_elem(list_t *restrict l, struct list_entry_s *tmp, struct list_entry_s *prev);

static inline struct list_entry_s *list_findpos(const list_t *restrict l, int posstart);

/* list initialization */
int list_init(list_t *restrict l) {
    if (l == NULL) return -1;

    l->numels = 0;

    /* iteration attributes */
    l->iter_active = 0;
    l->iter_pos = 0;
    l->iter_curentry = NULL;

    l->head = NULL;
    l->tail = NULL;

    l->comparator = NULL;
    l->seeker = NULL;

    return 0;
}

int list_destroy(list_t *restrict l) {
    if (l->iter_active) return -1;

    while (l->head)
    {
        struct list_entry_s *next=l->head->next;
        free (l->head);
        l->head=next;
    }

    l->numels = 0;

    l->tail = NULL;

    return 0;
}

/* setting list properties */
int list_attributes_comparator(list_t *restrict l, element_comparator comparator_fun) {
    if (l == NULL) return -1;

    l->comparator = comparator_fun;

    return 0;
}

int list_attributes_seeker(list_t *restrict l, element_seeker seeker_fun) {
    if (l == NULL) return -1;

    l->seeker = seeker_fun;

    return 0;
}

int list_append(list_t *restrict l, const void *data) {
    struct list_entry_s *lent=NULL;

    if (l->iter_active) return -1;

    lent=(struct list_entry_s *) malloc(sizeof (struct list_entry_s));
    if (lent == NULL)
    {
        return -1;
    }

    lent->data = (void*)data;
    lent->next = NULL;

    /* actually append nt_element */

    if (!l->tail)
    {
        l->head=lent;
        l->tail=lent;
    }
    else
    {
        l->tail->next=lent;
        l->tail=lent;
    }

    l->numels++;

    return 1;
}

void *list_get_at(const list_t *restrict l, unsigned int pos) {
    struct list_entry_s *tmp;

    tmp = list_findpos(l, pos);

    return (tmp != NULL ? tmp->data : NULL);
}

static inline struct list_entry_s *list_findpos(const list_t *restrict l, int posstart) {
    if (l==NULL) return NULL;
    
    REGISTER
    struct list_entry_s *ptr;
    
    REGISTER
    int i;
    
//    if (posstart < -1 || posstart > (int)l->numels) return NULL;
    if (posstart < 0 || posstart > (int)l->numels) return NULL;

    ptr=l->head;
    for (i=0; i<posstart; i++)
    {
        ptr=ptr->next;
    }

    return ptr;
}

static inline struct list_entry_s *list_findpos2(const list_t *restrict l, int posstart, struct list_entry_s **prev) {
    if (l==NULL) return NULL;
    
    REGISTER
    struct list_entry_s *ptr;
    
    REGISTER
    int i;
    
//    if (posstart < -1 || posstart > (int)l->numels) return NULL;
    if (posstart < 0 || posstart > (int)l->numels) return NULL;

    *prev=NULL;
    ptr=l->head;
    for (i=0; i<posstart; i++)
    {
        *prev=ptr;
        ptr=ptr->next;
    }

    return ptr;
}

void *list_extract_at(list_t *restrict l, unsigned int pos) {
    if (l->iter_active || pos >= l->numels) return NULL;
    
    struct list_entry_s *tmp, *prev;
    void *data;
    
    tmp = list_findpos2(l, pos, &prev);
    data = tmp->data;

    tmp->data = NULL;   /* save data from list_drop_elem() free() */
    list_drop_elem(l, tmp, prev);
    l->numels--;

    if (!l->numels)
    {
        l->head=NULL;
        l->tail=NULL;
    }

    return data;
}

int list_insert_at(list_t *restrict l, const void *data, unsigned int pos) {
    if (l->iter_active || pos > l->numels) return -1;

    struct list_entry_s *lent=NULL, *prec=NULL;
    
    lent=(struct list_entry_s *) malloc(sizeof (struct list_entry_s));
    if (lent == NULL)
    {
        return -1;
    }

    lent->data = (void*)data;

    /* actually append nt_element */

    prec=list_findpos(l, (int)pos-1);

    if (prec==NULL)
    {
        if (l->head)
        {
            lent->next=l->head;
        }
        else
        {
            lent->next=NULL;
            l->tail=lent;
        }
        l->head=lent;
    }
    else
    {
        if (prec==l->tail)
        {
            l->tail=lent;
        }
        lent->next=prec->next;
        prec->next=lent;
    }

    l->numels++;

    return 1;
}

int list_delete(list_t *restrict l, const void *data) {
	int pos, r;

	pos = list_locate(l, data);
	if (pos < 0)
		return -1;

	r = list_delete_at(l, pos);
	if (r < 0)
		return -1;

	return 0;
}

int list_delete_at(list_t *restrict l, unsigned int pos) {
    struct list_entry_s *delendo, *prev;

    if (l->iter_active || pos >= l->numels) return -1;

    delendo = list_findpos2(l, pos, &prev);

    list_drop_elem(l, delendo, prev);

    l->numels--;

    if (!l->numels)
    {
        l->head = NULL;
        l->tail = NULL;
    }

    return  0;
}

unsigned int list_size(const list_t *restrict l) {
    return l->numels;
}

int list_locate(const list_t *restrict l, const void *data) {
    REGISTER
    struct list_entry_s *el;
    int pos = 0;

    if (l->comparator != NULL) {
        /* use comparator */
        for (el = l->head; el != NULL; el = el->next, pos++) {
            if (l->comparator(data, el->data) == 0) break;
        }
    } else {
        /* compare references */
        for (el = l->head; el != NULL; el = el->next, pos++) {
            if (el->data == data) break;
        }
    }
    if (el == NULL) return -1;

    return pos;
}

void *list_seek(list_t *restrict l, const void *indicator) {
    REGISTER
    const struct list_entry_s *iter;

    if (l->seeker == NULL) return NULL;

    for (iter = l->head; iter != NULL; iter = iter->next) {
        if (l->seeker(iter->data, indicator) != 0) return iter->data;
    }

    return NULL;
}

int list_contains(const list_t *restrict l, const void *data) {
    return (list_locate(l, data) >= 0);
}

int list_iterator_start(list_t *restrict l) {
    if (l->iter_active) return 0;
    l->iter_pos = 0;
    l->iter_active = 1;
    l->iter_curentry = l->head;
    return 1;
}

void *list_iterator_next(list_t *restrict l) {
    void *toret;

    if (!l->iter_active || !l->iter_curentry) return NULL;

    toret = l->iter_curentry->data;
    l->iter_curentry = l->iter_curentry->next;
    l->iter_pos++;

    return toret;
}

int list_iterator_hasnext(const list_t *restrict l) {
    if (! l->iter_active) return 0;
    return (l->iter_pos < l->numels);
}

int list_iterator_stop(list_t *restrict l) {
    if (! l->iter_active) return 0;
    l->iter_pos = 0;
    l->iter_active = 0;
    return 1;
}

static int list_drop_elem(list_t *restrict l, struct list_entry_s *tmp, struct list_entry_s *prev) {
    if (tmp == NULL) return -1;

    if (l->head==tmp)
    {
        l->head=tmp->next;
    }
    else
    {
        prev->next=tmp->next;
    }

    if (l->tail==tmp)
    {
        l->tail=prev;
    }

    free (tmp);

    return 0;
}

/*
 * Copyright (c) 2007,2008 Mij <mij@bitchx.it>
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


#ifndef SIMCLIST_H
#define SIMCLIST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>
#include <errno.h>
#include <sys/types.h>

/* Be friend of both C90 and C99 compilers */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
    /* "inline" and "restrict" are keywords */
#else
#   define inline           /* inline */
#   define restrict         /* restrict */
#endif

/**
 * a comparator of elements.
 *
 * A comparator of elements is a function that:
 *      -# receives two references to elements a and b
 *      -# returns {<0, 0, >0} if (a > b), (a == b), (a < b) respectively
 *
 * It is responsability of the function to handle possible NULL values.
 */
typedef int (*element_comparator)(const void *a, const void *b);

/**
 * a seeker of elements.
 *
 * An element seeker is a function that:
 *      -# receives a reference to an element el
 *      -# receives a reference to some indicator data
 *      -# returns non-0 if the element matches the indicator, 0 otherwise
 *
 * It is responsability of the function to handle possible NULL values in any
 * argument.
 */
typedef int (*element_seeker)(const void *el, const void *indicator);

/**
 * an element lenght meter.
 *
 * An element meter is a function that:
 *      -# receives the reference to an element el
 *      -# returns its size in bytes
 *
 * It is responsability of the function to handle possible NULL values.
 */
typedef size_t (*element_meter)(const void *el);

/* [private-use] list entry -- olds actual user datum */
struct list_entry_s {
    void *data;

    /* singly-linked list service references */
    struct list_entry_s *next;
};

/** list object */
typedef struct {
    struct list_entry_s *head, *tail;

    unsigned int numels;

    /* service variables for list iteration */
    int iter_active;
    unsigned int iter_pos;
    struct list_entry_s *iter_curentry;

    /* list attributes */

    /* user-set routine for comparing list elements */
    element_comparator comparator;
    /* user-set routing for seeking elements */
    element_seeker seeker;
} list_t;

/**
 * initialize a list object for use.
 *
 * @param l     must point to a user-provided memory location
 * @return      0 for success. -1 for failure
 */
int list_init(list_t *restrict l);

/**
 * completely remove the list from memory.
 *
 * This function is the inverse of list_init(). It is meant to be called when
 * the list is no longer going to be used. Elements and possible memory taken
 * for internal use are freed.
 *
 * @param l     list to destroy
 *
 * @return      0 if the deleted successfully; -1 otherwise
 */
int list_destroy(list_t *restrict l);

/**
 * set the comparator function for list elements.
 *
 * Comparator functions are used for searching and sorting. If NULL is passed
 * as reference to the function, the comparator is disabled.
 *
 * @param l     list to operate
 * @param comparator_fun    pointer to the actual comparator function
 * @return      0 if the attribute was successfully set; -1 otherwise
 *
 * @see element_comparator()
 */
int list_attributes_comparator(list_t *restrict l, element_comparator comparator_fun);

/**
 * set a seeker function for list elements.
 *
 * Seeker functions are used for finding elements. If NULL is passed as reference
 * to the function, the seeker is disabled.
 *
 * @param l     list to operate
 * @param seeker_fun    pointer to the actual seeker function
 * @return      0 if the attribute was successfully set; -1 otherwise
 *
 * @see element_seeker()
 */
int list_attributes_seeker(list_t *restrict l, element_seeker seeker_fun);

/**
 * append data at the end of the list.
 *
 * This function is useful for adding elements with a FIFO/queue policy.
 *
 * @param l     list to operate
 * @param data  pointer to user data to append
 *
 * @return      1 for success. < 0 for failure
 */
int list_append(list_t *restrict l, const void *data);

/**
 * retrieve an element at a given position.
 *
 * @param l     list to operate
 * @param pos   [0,size-1] position index of the element wanted
 * @return      reference to user datum, or NULL on errors
 */
void *list_get_at(const list_t *restrict l, unsigned int pos);

/**
 * retrieve and remove from list an element at a given position.
 *
 * @param l     list to operate
 * @param pos   [0,size-1] position index of the element wanted
 * @return      reference to user datum, or NULL on errors
 */
void *list_extract_at(list_t *restrict l, unsigned int pos);

/**
 * insert an element at a given position.
 *
 * @param l     list to operate
 * @param data  reference to data to be inserted
 * @param pos   [0,size-1] position index to insert the element at
 * @return      positive value on success. Negative on failure
 */
int list_insert_at(list_t *restrict l, const void *data, unsigned int pos);

/**
 * expunge the first found given element from the list.
 *
 * Inspects the given list looking for the given element; if the element
 * is found, it is removed. Only the first occurence is removed.
 * If a comparator function was not set, elements are compared by reference.
 * Otherwise, the comparator is used to match the element.
 *
 * @param l     list to operate
 * @param data  reference of the element to search for
 * @return      0 on success. Negative value on failure
 *
 * @see list_attributes_comparator()
 * @see list_delete_at()
 */
int list_delete(list_t *restrict l, const void *data);

/**
 * expunge an element at a given position from the list.
 *
 * @param l     list to operate
 * @param pos   [0,size-1] position index of the element to be deleted
 * @return      0 on success. Negative value on failure
 */
int list_delete_at(list_t *restrict l, unsigned int pos);

/**
 * clear all the elements off of the list.
 *
 * The element datums will not be freed.
 *
 * @see list_delete_range()
 * @see list_size()
 *
 * @param l     list to operate
 * @return      the number of elements in the list before cleaning
 */
int list_clear(list_t *restrict l);

/**
 * inspect the number of elements in the list.
 *
 * @param l     list to operate
 * @return      number of elements currently held by the list
 */
unsigned int list_size(const list_t *restrict l);

/**
 * find the position of an element in a list.
 *
 * @warning Requires a comparator function to be set for the list.
 *
 * Inspects the given list looking for the given element; if the element
 * is found, its position into the list is returned.
 * Elements are inspected comparing references if a comparator has not been
 * set. Otherwise, the comparator is used to find the element.
 *
 * @param l     list to operate
 * @param data  reference of the element to search for
 * @return      position of element in the list, or <0 if not found
 * 
 * @see list_attributes_comparator()
 * @see list_get_at()
 */
int list_locate(const list_t *restrict l, const void *data);

/**
 * returns an element given an indicator.
 *
 * @warning Requires a seeker function to be set for the list.
 *
 * Inspect the given list looking with the seeker if an element matches
 * an indicator. If such element is found, the reference to the element
 * is returned.
 *
 * @param l     list to operate
 * @param indicator indicator data to pass to the seeker along with elements
 * @return      reference to the element accepted by the seeker, or NULL if none found
 */
void *list_seek(list_t *restrict l, const void *indicator);

/**
 * inspect whether some data is member of the list.
 *
 * @warning Requires a comparator function to be set for the list.
 *
 * By default, a per-reference comparison is accomplished. That is,
 * the data is in list if any element of the list points to the same
 * location of data.
 * A "semantic" comparison is accomplished, otherwise, if a comparator
 * function has been set previously, with list_attributes_comparator();
 * in which case, the given data reference is believed to be in list iff
 * comparator_fun(elementdata, userdata) == 0 for any element in the list.
 * 
 * @param l     list to operate
 * @param data  reference to the data to search
 * @return      0 iff the list does not contain data as an element
 *
 * @see list_attributes_comparator()
 */
int list_contains(const list_t *restrict l, const void *data);

/**
 * start an iteration session.
 *
 * This function prepares the list to be iterated.
 *
 * @param l     list to operate
 * @return 		0 if the list cannot be currently iterated. >0 otherwise
 * 
 * @see list_iterator_stop()
 */
int list_iterator_start(list_t *restrict l);

/**
 * return the next element in the iteration session.
 *
 * @param l     list to operate
 * @return		element datum, or NULL on errors
 */
void *list_iterator_next(list_t *restrict l);

/**
 * inspect whether more elements are available in the iteration session.
 *
 * @param l     list to operate
 * @return      0 iff no more elements are available.
 */
int list_iterator_hasnext(const list_t *restrict l);

/**
 * end an iteration session.
 *
 * @param l     list to operate
 * @return      0 iff the iteration session cannot be stopped
 */
int list_iterator_stop(list_t *restrict l);

#ifdef __cplusplus
}
#endif

#endif


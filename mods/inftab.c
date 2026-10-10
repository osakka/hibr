/* The tables both directions of RFC 1951 read.
 *
 * In inflate.c until 0.99.128, where they were the decoder's -- and a
 * decoder is all anything needed. The encoder beside it reads exactly the
 * same boundaries in the other direction (which length code covers a match
 * of this length, rather than which length this code means), so they are
 * their own file now: one definition, and a module that only compresses
 * does not carry six kilobytes of decoder it never calls to reach half a
 * kilobyte of data.
 *
 * The permutation is the order the code-length code's own lengths are
 * written in, which puts the likeliest first so the tail can be cut.
 */
#include "inflate.h"

const short inf_ord[19] = { 16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
			    11, 4,  12, 3, 13, 2, 14, 1, 15 };

const short inf_lbase[29] = { 3,  4,  5,  6,  7,  8,  9,  10,  11,  13,
			      15, 17, 19, 23, 27, 31, 35, 43,  51,  59,
			      67, 83, 99, 115, 131, 163, 195, 227, 258 };

const short inf_lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
			     2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };

const short inf_dbase[30] = { 1,    2,    3,    4,    5,    7,     9,
			      13,   17,   25,   33,   49,   65,    97,
			      129,  193,  257,  385,  513,  769,   1025,
			      1537, 2049, 3073, 4097, 6145, 8193,  12289,
			      16385, 24577 };

const short inf_dext[30] = { 0, 0, 0,  0,  1,  1,  2,  2,  3,  3,  4,  4,
			     5, 5, 6,  6,  7,  7,  8,  8,  9,  9,  10, 10,
			     11, 11, 12, 12, 13, 13 };

#ifndef RNA_SEQUENCE_H
#define RNA_SEQUENCE_H

#include <stdbool.h>
#include "crc32.h"
#include "util.h"

typedef DWORD nt_seq_hash;

#define MAX_SEQ_LEN 	3000

nt_seq_hash get_seq_hash (const char *sequence);
bool is_seq_valid (const char
                   *sequence); // DEBUG version of sequence checker, used during search
bool is_valid_sequence (const char *restrict sequence,
                        char *restrict *err);	// frontend equivalent of is_seq_valid
bool read_seq_from_fn (const char *fn, char **buff, nt_file_size *fsize);

#endif //RNA_SEQUENCE_H

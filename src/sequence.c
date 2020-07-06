#include "sequence.h"
#include "util.h"
#include "m_model.h"
#include "interface.h"

nt_seq_hash get_seq_hash (const char *sequence) {
	if (!sequence || strlen (sequence) == 0) {
		COMMIT_DEBUG (REPORT_WARNINGS, SEQ,
		              "seq is NULL or has zero length, now generating NULL string crc", false);
		char seq_buf[1];
		seq_buf[0] = '\0';
		return crc32buf (seq_buf, 0);
	}
	
	else {
		COMMIT_DEBUG (REPORT_INFO, SEQ, "generating crc for seq", false);
		return crc32buf ((char *)sequence, strlen (sequence));
	}
}

bool is_seq_valid (const char *sequence) {
	if (!sequence || (strlen (sequence) < (MIN_STACK_LEN * 2 + MIN_IDIST))) {
		COMMIT_DEBUG1 (REPORT_ERRORS, SEQ,
		               "seq is NULL or shorter than the minimum length (%d)",
		               MIN_STACK_LEN * 2 + MIN_IDIST, false);
		return false;
	}
	
	for (nt_abs_seq_len l = 0; l < strlen (sequence); l++) {
		if (sequence[l] != 'a' && sequence[l] != 'c' && sequence[l] != 'g' &&
		    sequence[l] != 'u') {
			COMMIT_DEBUG2 (REPORT_ERRORS, SEQ,
			               "illegal nt symbol '%c' found at seq postion (%lu)", sequence[l], l + 1, false);
			return false;
		}
	}
	
	return true;
}

/*
 * is_valid_sequence performs the same checks as is_seq_valid, except that:
 * . it returns a formatted error message (similar to is_valid_* functions in interface.c)
 * . does not commit to DEBUG
 */
bool is_valid_sequence (const char *restrict sequence, char *restrict *err) {
	char err_msg[MAX_ERR_STRING_LEN + 1];
	REGISTER
	bool err_found = false;
	
	if (NULL == sequence || ((MIN_STACK_LEN * 2 + MIN_IDIST) > strlen (sequence)) ||
	    (MAX_SEQ_LEN < strlen (sequence))) {
		sprintf (err_msg,
		         "sequence string must be at least %d and at most %d characters in length",
		         (MIN_STACK_LEN * 2 + MIN_IDIST), MAX_SEQ_LEN);
		err_found = true;
	}
	
	else {
		for (REGISTER int i = 0; i < strlen (sequence); i++) {
			if (sequence[i] == 'a' || sequence[i] == 'c' || sequence[i] == 'g' ||
			    sequence[i] == 'u') {
				continue;
			}
			
			sprintf (err_msg, "invalid character for sequence at position %d", i + 1);
			err_found = true;
		}
	}
	
	if (err_found) {
		*err = MALLOC_DEBUG (MAX_ERR_STRING_LEN + 1,
		                     "err message string in is_valid_sequence");
		                     
		if (!*err) {
			COMMIT_DEBUG (REPORT_ERRORS, INTERFACE,
			              "cannot allocate memory for err message string in is_valid_sequence", false);
			*err = NULL;
			return false;
		}
		
		sprintf (*err, "%s", err_msg);
		return false;
	}
	
	else {
		return true;
	}
}

bool read_seq_from_fn (const char *fn, char **buff, nt_file_size *fsize) {
	if (!fn || strlen (fn) == 0) {
		DEBUG_NOW (REPORT_ERRORS, UTILS, "invalid sequence filename");
		return false;
	}
	
	FILE *f = fopen (fn, "rb");
	
	if (f == NULL) {
		DEBUG_NOW1 (REPORT_ERRORS, UTILS, "could not open sequence file '%s'", fn);
		return false;
	}
	
	#ifdef _WIN32
	WIN32_FILE_ATTRIBUTE_DATA fa;
	
	if (!GetFileAttributesEx (fn, GetFileExInfoStandard, (void *)&fa)) {
		DEBUG_NOW1 (REPORT_ERRORS, UTILS, "could not read to end of sequence file '%s'",
		            fn);
		fclose (f);
		return false;
	}
	
	else {
		*fsize = ((nt_file_size)fa.nFileSizeHigh << 32) + ((nt_file_size)
		                                        fa.nFileSizeLow);
	}
	
	#else
	
	if (fseek (f, 0, SEEK_END) || ((*fsize = ftell (f)) < 0)) {
		DEBUG_NOW1 (REPORT_ERRORS, UTILS, "could not read to end of sequence file '%s'",
		            fn);
		fclose (f);
		return false;
	}
	
	*fsize = ftell (f);
	
	if (*fsize < 0) {
		DEBUG_NOW1 (REPORT_ERRORS, UTILS, "could not get size of sequence file '%s'",
		            fn);
		fclose (f);
		return false;
	}
	
	fseek (f, 0, SEEK_SET);
	#endif
	
	if (*fsize > MAX_FILE_SIZE_BYTES) {
		DEBUG_NOW2 (REPORT_ERRORS, UTILS,
		            "sequence file '%s' exceeds file size limit (%llu)", fn, MAX_FILE_SIZE_BYTES);
		fclose (f);
		return false;
	}
	
	*buff = malloc ((size_t) * fsize);
	
	if (!*buff) {
		DEBUG_NOW1 (REPORT_ERRORS, UTILS,
		            "could not allocate memory for buffer when reading '%s'", fn);
		fclose (f);
		return false;
	}
	
	nt_file_size bytes_read = fread (*buff, 1, (size_t) * fsize, f);
	
	if (bytes_read != *fsize) {
		DEBUG_NOW1 (REPORT_ERRORS, UTILS,
		            "could not read all data into buffer when reading '%s'", fn);
		free (*buff);
		fclose (f);
		return false;
	}
	
	fclose (f);
	return true;
}

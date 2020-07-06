#include "interface.h"
#include "util.h"
#include "m_build.h"

/*
 * static, inline replacements for memset/memcpy - silences google sanitizers
 */
static inline void g_memset (void *p, const char v, const int len) {
	REGISTER
	char *pc = (char *)p;
	
	for (REGISTER int i = 0; i < len; i++) {
		pc[i] = v;
	}
}

static inline void g_memcpy (void *p, const void *r, const int len) {
	REGISTER
	char *pc = (char *)p, *rc = (char *)r;
	
	for (REGISTER int i = 0; i < len; i++) {
		pc[i] = rc[i];
	}
}

static inline short S_GET_INDEX (const char *restrict s, const char c) {
	const char *restrict ptr = strchr (s, c);
	return ptr == NULL ? (short) -1 : (short) (ptr - s);
}

static inline ushort get_pos_var_value (const char p) {
	if (p >= '0' && p <= '9') {
		return (ushort) (p - '0');
	}
	
	else {
		return (ushort) (p - 'a' + 10);
	}
}

static inline char write_pos_var_value (const ushort pos_var_value) {
	if (pos_var_value > MAX_POS_VAR) {
		COMMIT_DEBUG2 (REPORT_ERRORS, INTERFACE,
		               "tried to convert a pos_var_value (%d) that is greater than MAX_POS_VAR (%d) in write_pos_var_value",
		               pos_var_value, MAX_POS_VAR, false);
		return S_WHITE_SPACE[0];    // have to return soem char, so when in error, return white space
	}
	
	else
		if (!pos_var_value) {
			COMMIT_DEBUG (REPORT_ERRORS, INTERFACE,
			              "tried to convert a 0 pos_var_value in write_pos_var_value", false);
			return S_WHITE_SPACE[0];
		}
		
		else {
			if (pos_var_value > 9) {
				return (char) ('a' + (pos_var_value - 10));
			}
			
			else {
				return (char) ('0' + pos_var_value);
			}
		}
}

static inline bool is_valid_pos_var (char p) {
	if ((p >= '1' && p <= SAFE_MIN ('9', MAX_POS_VAR_CHAR)) ||
	    (p >= 'a' && p <= MAX_POS_VAR_CHAR)) {
		return true;
	}
	
	return false;
}

static inline ushort get_CSSD_symbol_count (const char *restrict CS,
                                        const char symbol) {
	REGISTER
	ushort cnt = 0;
	
	for (REGISTER ushort c = 0; c < strlen (CS); c++) {
		if (CS[c] == symbol) {
			cnt++;
		}
	}
	
	return cnt;
}

static inline short get_CSSD_paired_symbol_balance (const char *restrict CS,
                                        const char open_symbol, const char close_symbol) {
	REGISTER
	short balance = 0;
	
	for (REGISTER
	     ushort c = 0; c < strlen (CS); c++) {
		if (CS[c] == open_symbol) {
			balance++;
		}
		
		else
			if (CS[c] == close_symbol) {
				balance--;
			}
	}
	
	return balance;
}

static inline short check_CSSD_paired_symbol_order (const char *restrict CS,
                                        const char open_symbol, const char close_symbol) {
	REGISTER
	short balance = 0;
	
	for (REGISTER ushort c = 0; c < strlen (CS); c++) {
		if (CS[c] == open_symbol) {
			balance++;
		}
		
		else
			if (CS[c] == close_symbol) {
				balance--;
			}
			
		if (balance < 0) {
			return (short) (-c - 1);
		}
	}
	
	return 0;
}

static inline short check_CSSD_paired_symbol_contig (const char *restrict CS,
                                        const char open_symbol, const char close_symbol) {
	REGISTER
	short open_symbol_posn = -1, close_symbol_posn = -1;
	
	for (REGISTER ushort c = 0; c < strlen (CS); c++) {
		if (CS[c] == open_symbol) {
			if (open_symbol_posn < 0) {
				open_symbol_posn = c;
			}
			
			else
				if (CS[c - 1] != open_symbol) {
					return (short) (-c - 1);
				}
		}
		
		else
			if (CS[c] == close_symbol) {
				if (close_symbol_posn < 0) {
					close_symbol_posn = c;
				}
				
				else
					if (CS[c - 1] != close_symbol) {
						return (short) (-c - 1);
					}
			}
	}
	
	return 0;
}

static inline short get_corresponding_close_position (const char *restrict CS,
                                        const char symbol, const ushort open_position) {
	bool is_open_term = S_GET_INDEX (S_OPEN_TERM, CS[open_position]) >= 0,
	     is_open_multi = S_GET_INDEX (S_OPEN_MULTI, CS[open_position]) >= 0;
	ushort sym_balance =
	                    1, // in balance take into account the open symbol at open_position
	                    CS_len = (ushort) strlen (CS);
	                    
	if (is_open_term || is_open_multi) {
		// find matching close symbol, scanning CS from right after the "offending" symbol
		ushort k = (ushort) (open_position + 1);
		
		while (k < CS_len) {
			if (is_open_term) {
				if (S_GET_INDEX (S_OPEN_TERM, CS[k]) >= 0) {
					sym_balance++;
				}
				
				else
					if (S_GET_INDEX (S_CLOSE_TERM, CS[k]) >= 0) {
						sym_balance--;
					}
					
				if (!sym_balance) {
					return k;
				}
			}
			
			if (is_open_multi) {
				if (S_GET_INDEX (S_OPEN_MULTI, CS[k]) >= 0) {
					sym_balance++;
				}
				
				else
					if (S_GET_INDEX (S_CLOSE_MULTI, CS[k]) >= 0) {
						sym_balance--;
					}
					
				if (!sym_balance) {
					return k;
				}
			}
			
			k++;
		}
	}
	
	return -1;
}

static inline bool do_symbols_match (char model_symbol, char string_symbol) {
	if (string_symbol == SS_NEUTRAL_UNKNOWN_SYMBOL) {
		if (S_GET_INDEX (S_UNSTRUCTURED_RESIDUE, model_symbol) >= 0 ||
		    S_GET_INDEX (S_HAIRPIN_RESIDUE, model_symbol) >= 0 ||
		    S_GET_INDEX (S_MULTI_RESIDUE, model_symbol) >= 0 ||
		    S_GET_INDEX (S_INTERIOR_RESIDUE, model_symbol) >= 0) {
			return true;
		}
		
		return false;
	}
	
	// treat TERM/MULTI as equal for the purpose of model/string checking
	else
		if (string_symbol == SS_NEUTRAL_OPEN_TERM) {
			if (S_GET_INDEX (S_OPEN_TERM, model_symbol) >= 0 ||
			    (S_GET_INDEX (S_OPEN_MULTI, model_symbol) >= 0)) {
				return true;
			}
			
			return false;
		}
		
		else
			if (string_symbol == SS_NEUTRAL_CLOSE_TERM) {
				if (S_GET_INDEX (S_CLOSE_TERM, model_symbol) >= 0 ||
				    (S_GET_INDEX (S_CLOSE_MULTI, model_symbol) >= 0)) {
					return true;
				}
				
				return false;
			}
			
			else
				if (string_symbol == SS_NEUTRAL_OPEN_PK) {
					if (S_GET_INDEX (S_OPEN_PK, model_symbol) >= 0) {
						return true;
					}
					
					return false;
				}
				
				else
					if (string_symbol == SS_NEUTRAL_CLOSE_PK) {
						if (S_GET_INDEX (S_CLOSE_PK, model_symbol) >= 0) {
							return true;
						}
						
						return false;
					}
					
					else
						if (string_symbol == SS_NEUTRAL_BT_PAIR) {
							if (S_GET_INDEX (S_BASE_TRIPLE_PAIR, model_symbol) >= 0) {
								return true;
							}
							
							return false;
						}
						
						else
							if (string_symbol == SS_NEUTRAL_BT_SINGLE) {
								if (S_GET_INDEX (S_BASE_TRIPLE_SINGLE, model_symbol) >= 0) {
									return true;
								}
								
								return false;
							}
							
							else {
								return false;
							}
}

bool is_valid_definition (const char *restrict definition,
                          char *restrict *err) {
	char err_msg[MAX_ERR_STRING_LEN + 1];
	REGISTER
	bool err_found = false;
	
	if (NULL == definition || (S_DEFINITION_MIN_LEN > strlen (definition)) ||
	    (S_DEFINITION_MAX_LEN < strlen (definition))) {
		sprintf (err_msg,
		         "definition string must be at least %d and at most %d characters in length",
		         S_DEFINITION_MIN_LEN, S_DEFINITION_MAX_LEN);
		err_found = true;
	}
	
	else {
		for (REGISTER int i = 0; i < strlen (definition); i++) {
			if ((definition[i] >= 'a' && definition[i] <= 'z') || (definition[i] >= 'A' &&
			                                        definition[i] <= 'Z') || (definition[i] >= '0' && definition[i] <= '9')) {
				continue;
			}
			
			REGISTER int j = 0;
			
			for (; j < strlen (S_DEFINITION_SPECIAL_CHARS); j++) {
				if (definition[i] == S_DEFINITION_SPECIAL_CHARS[j]) {
					break;
				}
			}
			
			if (j < strlen (S_DEFINITION_SPECIAL_CHARS)) {
				continue;
			}
			
			sprintf (err_msg, "valid characters for definition are \"a-zA-Z0-9%s\"",
			         S_DEFINITION_SPECIAL_CHARS);
			err_found = true;
		}
	}
	
	if (err_found) {
		*err = MALLOC_DEBUG (MAX_ERR_STRING_LEN + 1,
		                     "err message string in is_valid_definition");
		                     
		if (!*err) {
			COMMIT_DEBUG (REPORT_ERRORS, INTERFACE,
			              "cannot allocate memory for err message string in is_valid_definition", false);
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

bool is_valid_accession (const char *restrict accession, char *restrict *err) {
	char err_msg[MAX_ERR_STRING_LEN + 1];
	REGISTER
	bool err_found = false;
	
	if (NULL == accession || (S_ACCESSION_MIN_LEN > strlen (accession)) ||
	    (S_ACCESSION_MAX_LEN < strlen (accession))) {
		sprintf (err_msg,
		         "accession string must be at least %d and at most %d characters in length",
		         S_ACCESSION_MIN_LEN, S_ACCESSION_MAX_LEN);
		err_found = true;
	}
	
	else {
		for (REGISTER int i = 0; i < strlen (accession); i++) {
			if ((accession[i] >= 'a' && accession[i] <= 'z') || (accession[i] >= 'A' &&
			                                        accession[i] <= 'Z') || (accession[i] >= '0' && accession[i] <= '9')) {
				continue;
			}
			
			REGISTER int j = 0;
			
			for (; j < strlen (S_ACCESSION_SPECIAL_CHARS); j++) {
				if (accession[i] == S_ACCESSION_SPECIAL_CHARS[j]) {
					break;
				}
			}
			
			if (j < strlen (S_ACCESSION_SPECIAL_CHARS)) {
				continue;
			}
			
			sprintf (err_msg, "valid characters for accession are \"a-zA-Z0-9%s\"",
			         S_ACCESSION_SPECIAL_CHARS);
			err_found = true;
		}
	}
	
	if (err_found) {
		*err = MALLOC_DEBUG (MAX_ERR_STRING_LEN + 1,
		                     "err message string in is_valid_accession");
		                     
		if (!*err) {
			COMMIT_DEBUG (REPORT_ERRORS, INTERFACE,
			              "cannot allocate memory for err message string in is_valid_accession", false);
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

bool is_valid_group (const char *restrict group, char *restrict *err) {
	char err_msg[MAX_ERR_STRING_LEN + 1];
	REGISTER
	bool err_found = false;
	
	if (NULL == group || (S_GROUP_MIN_LEN > strlen (group)) ||
	    (S_GROUP_MAX_LEN < strlen (group))) {
		sprintf (err_msg,
		         "group string must be at least %d and at most %d characters in length",
		         S_GROUP_MIN_LEN, S_GROUP_MAX_LEN);
		err_found = true;
	}
	
	else {
		for (REGISTER int i = 0; i < strlen (group); i++) {
			if ((group[i] >= 'a' && group[i] <= 'z') || (group[i] >= 'A' &&
			                                        group[i] <= 'Z') || (group[i] >= '0' && group[i] <= '9')) {
				continue;
			}
			
			REGISTER int j = 0;
			
			for (; j < strlen (S_GROUP_SPECIAL_CHARS); j++) {
				if (group[i] == S_GROUP_SPECIAL_CHARS[j]) {
					break;
				}
			}
			
			if (j < strlen (S_GROUP_SPECIAL_CHARS)) {
				continue;
			}
			
			sprintf (err_msg, "valid characters for group are \"a-zA-Z0-9%s\"",
			         S_GROUP_SPECIAL_CHARS);
			err_found = true;
		}
	}
	
	if (err_found) {
		*err = MALLOC_DEBUG (MAX_ERR_STRING_LEN + 1,
		                     "err message string in is_valid_group");
		                     
		if (!*err) {
			COMMIT_DEBUG (REPORT_ERRORS, INTERFACE,
			              "cannot allocate memory for err message string in is_valid_group", false);
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

bool is_valid_CSSD (const char *restrict CS, const char *restrict pos_var,
                    char *restrict *err) {
	char err_msg[MAX_ERR_STRING_LEN + 1];
	REGISTER
	bool err_found = false;
	ushort CS_len = 0, pos_var_len = 0;
	
	if (!CS) {
		sprintf (err_msg, "no CS provided");
		err_found = true;
	}
	
	else {
		CS_len = (ushort) strlen (CS);
	}
	
	if (!err_found && !CS_len) {
		sprintf (err_msg, "CS is empty");
		err_found = true;
	}
	
	if (!err_found && pos_var) {
		pos_var_len = (ushort) strlen (pos_var);
		
		if (pos_var_len > CS_len) {
			for (ushort i = CS_len; i < pos_var_len; i++) {
				if (S_GET_INDEX (S_WHITE_SPACE, pos_var[i]) < 0) {
					sprintf (err_msg, "pos_var has extra symbols after end of CS");
					err_found = true;
					break;
				}
			}
		}
		
		if (!err_found) {
			for (ushort i = 0; i < pos_var_len; i++) {
				if (S_GET_INDEX (S_WHITE_SPACE, pos_var[i]) < 0 &&
				    !is_valid_pos_var (pos_var[i])) {
					sprintf (err_msg, "invalid pos_var symbol \"%c\" at position %d", pos_var[i],
					         i + 1);
					err_found = true;
					break;
				}
			}
		}
	}
	
	if (!err_found) {
		REGISTER
		short p;
		// assumes at least one (symbol) is allowed/defined for each CSSD symbol type
		ushort  NUM_MULTI_ALLOWED = (ushort) strlen (S_OPEN_MULTI),
		        NUM_TERM_ALLOWED = (ushort) strlen (S_OPEN_TERM),
		        NUM_PK_ALLOWED = (ushort) strlen (S_OPEN_PK),
		        NUM_BASE_TRIPLE_PAIR_ALLOWED = (ushort) strlen (S_BASE_TRIPLE_PAIR);
		        
		for (REGISTER ushort a = 0; a < NUM_BASE_TRIPLE_PAIR_ALLOWED; a++) {
			if (S_GET_INDEX (CS, S_BASE_TRIPLE_PAIR[a]) >= 0 ||
			    (S_GET_INDEX (CS, S_BASE_TRIPLE_SINGLE[a]) >= 0)) {
				if ((p = get_CSSD_symbol_count (CS, S_BASE_TRIPLE_PAIR[a])) != 2) {
					sprintf (err_msg, "BASE TRIPLE requires 2 paired symbols \"%c%c\" but found %d",
					         S_BASE_TRIPLE_PAIR[a], S_BASE_TRIPLE_PAIR[a], p);
					err_found = true;
					break;
				}
				
				if ((p = get_CSSD_symbol_count (CS, S_BASE_TRIPLE_SINGLE[a])) != 1) {
					sprintf (err_msg,
					         "BASE TRIPLE requires a single 'unpaired' symbol \"%c\" but found %d",
					         S_BASE_TRIPLE_SINGLE[a], p);
					err_found = true;
					break;
				}
			}
		}
		
		if (!err_found) {
			for (REGISTER ushort a = 0; a < NUM_MULTI_ALLOWED; a++) {
				if ((S_GET_INDEX (CS, S_OPEN_MULTI[a]) >= 0 ||
				     S_GET_INDEX (CS, S_CLOSE_MULTI[a]) >= 0) &&
				    ((p = get_CSSD_paired_symbol_balance (CS, S_OPEN_MULTI[a],
				                                            S_CLOSE_MULTI[a])) != 0)) {
					if (p > 0) {
						sprintf (err_msg,
						         "imbalanced number of MULTI symbols [%d more \"%c\" than \"%c\"]", p,
						         S_OPEN_MULTI[a], S_CLOSE_MULTI[a]);
					}
					
					else {
						sprintf (err_msg,
						         "imbalanced number of MULTI symbols [%d less \"%c\" than \"%c\"]", abs (p),
						         S_OPEN_MULTI[a], S_CLOSE_MULTI[a]);
					}
					
					err_found = true;
					break;
				}
			}
		}
		
		if (!err_found) {
			for (REGISTER ushort a = 0; a < NUM_TERM_ALLOWED; a++) {
				if ((S_GET_INDEX (CS, S_OPEN_TERM[a]) >= 0 ||
				     S_GET_INDEX (CS, S_CLOSE_TERM[a]) >= 0) &&
				    ((p = get_CSSD_paired_symbol_balance (CS, S_OPEN_TERM[a],
				                                            S_CLOSE_TERM[a])) != 0)) {
					if (p > 0) {
						sprintf (err_msg,
						         "imbalanced number of TERM symbols [%d more \"%c\" than \"%c\"]", p,
						         S_OPEN_TERM[a], S_CLOSE_TERM[a]);
					}
					
					else {
						sprintf (err_msg,
						         "imbalanced number of TERM symbols [%d less \"%c\" than \"%c\"]", abs (p),
						         S_OPEN_TERM[a], S_CLOSE_TERM[a]);
					}
					
					err_found = true;
					break;
				}
			}
		}
		
		if (!err_found) {
			for (REGISTER ushort a = 0; a < NUM_PK_ALLOWED; a++) {
				if (S_GET_INDEX (CS, S_OPEN_PK[a]) >= 0 ||
				    S_GET_INDEX (CS, S_CLOSE_PK[a]) >= 0) {
					if ((p = get_CSSD_paired_symbol_balance (CS, S_OPEN_PK[a],
					                                        S_CLOSE_PK[a])) != 0) {
						if (p > 0) {
							sprintf (err_msg,
							         "imbalanced number of PK symbols [%d more \"%c\" than \"%c\"]", p, S_OPEN_PK[a],
							         S_CLOSE_PK[a]);
						}
						
						else {
							sprintf (err_msg,
							         "imbalanced number of PK symbols [%d less \"%c\" than \"%c\"]", abs (p),
							         S_OPEN_PK[a], S_CLOSE_PK[a]);
						}
						
						err_found = true;
						break;
					}
					
					else
						if ((p = check_CSSD_paired_symbol_order (CS, S_OPEN_PK[a],
						                                        S_CLOSE_PK[a])) < 0) {
							sprintf (err_msg, "inverted order of PK symbols \"%c%c\" at position %d",
							         S_OPEN_PK[a], S_CLOSE_PK[a], -p);
							err_found = true;
							break;
						}
						
						else
							if ((p = check_CSSD_paired_symbol_contig (CS, S_OPEN_PK[a],
							                                        S_CLOSE_PK[a])) < 0) {
								sprintf (err_msg,
								         "multiple instances of PK symbol pairs \"%c%c\" at position %d", S_OPEN_PK[a],
								         S_CLOSE_PK[a], -p);
								err_found = true;
								break;
							}
				}
			}
		}
		
		if (!err_found) {
			ushort  open_multi_cnt[NUM_MULTI_ALLOWED], close_multi_cnt[NUM_MULTI_ALLOWED],
			        open_term_cnt[NUM_TERM_ALLOWED], close_term_cnt[NUM_TERM_ALLOWED];
			REGISTER
			bool doing_multi = false, doing_term = false;
			char last_term_symbol = S_WHITE_SPACE[0], last_multi_symbol = S_WHITE_SPACE[0];
			
			for (REGISTER ushort a = 0; a < NUM_MULTI_ALLOWED; a++) {
				open_multi_cnt[a] = 0;
				close_multi_cnt[a] = 0;
			}
			
			for (REGISTER ushort a = 0; a < NUM_TERM_ALLOWED; a++) {
				open_term_cnt[a] = 0;
				close_term_cnt[a] = 0;
			}
			
			for (REGISTER ushort c = 0; c < strlen (CS); c++) {
				if ((p = S_GET_INDEX (S_UNSTRUCTURED_RESIDUE, CS[c])) >= 0) {
					for (REGISTER ushort a = 0; a < NUM_MULTI_ALLOWED; a++) {
						if (open_multi_cnt[a]) {
							sprintf (err_msg,
							         "UNSTRUCTURED RESIDUE %c at position %d is within a MULTI \"%c%c\" substructure",
							         S_UNSTRUCTURED_RESIDUE[p], c + 1, S_OPEN_MULTI[a], S_CLOSE_MULTI[a]);
							err_found = true;
							break;
						}
					}
					
					if (!err_found) {
						for (REGISTER ushort a = 0; a < NUM_TERM_ALLOWED; a++) {
							if (open_term_cnt[a]) {
								sprintf (err_msg,
								         "UNSTRUCTURED RESIDUE %c at position %d is within a TERM \"%c%c\" substructure",
								         S_UNSTRUCTURED_RESIDUE[p], c + 1, S_OPEN_TERM[a], S_CLOSE_TERM[a]);
								err_found = true;
								break;
							}
						}
					}
					
					if (!err_found) {
						doing_multi = false;
						last_multi_symbol = S_WHITE_SPACE[0];
						doing_term = false;
						last_term_symbol = S_WHITE_SPACE[0];
					}
					
					else {
						break;
					}
				}
				
				else
					if ((p = S_GET_INDEX (S_OPEN_MULTI, CS[c])) >= 0) {
						if (!doing_multi) {
							for (REGISTER ushort a = 0; a < NUM_MULTI_ALLOWED; a++) {
								if (open_multi_cnt[a]) {
									sprintf (err_msg,
									         "MULTI substructure \"%c%c\" at position %d is nested within a MULTI substructure \"%c%c\"",
									         S_OPEN_MULTI[p], S_CLOSE_MULTI[p], c + 1, S_OPEN_MULTI[a], S_CLOSE_MULTI[a]);
									err_found = true;
									break;
								}
							}
							
							if (!err_found) {
								for (REGISTER ushort a = 0; a < NUM_TERM_ALLOWED; a++) {
									if (open_term_cnt[a]) {
										sprintf (err_msg,
										         "MULTI substructure \"%c%c\" at position %d is nested within a TERM substructure \"%c%c\"",
										         S_OPEN_MULTI[p], S_CLOSE_MULTI[p], c + 1, S_OPEN_TERM[a], S_CLOSE_TERM[a]);
										err_found = true;
										break;
									}
								}
							}
						}
						
						if (!err_found) {
							doing_multi = true;
							last_multi_symbol = CS[c];
							doing_term = false;
							last_term_symbol = S_WHITE_SPACE[0];
							open_multi_cnt[p]++;
						}
						
						else {
							break;
						}
					}
					
					else
						if ((p = S_GET_INDEX (S_CLOSE_MULTI, CS[c])) >= 0) {
							if (!doing_multi && !open_multi_cnt[p]) {
								sprintf (err_msg,
								         "MULTI substructure \"%c\" at position %d has no corresponding opening symbol \"%c\"",
								         S_CLOSE_MULTI[p], c + 1, S_OPEN_MULTI[p]);
								err_found = true;
							}
							
							if (!err_found) {
								doing_term = false;
								last_term_symbol = S_WHITE_SPACE[0];
								close_multi_cnt[p]++;
								
								if (close_multi_cnt[p] == open_multi_cnt[p]) {
									open_multi_cnt[p] = 0;
									close_multi_cnt[p] = 0;
									doing_multi = false;
									last_multi_symbol = S_WHITE_SPACE[0];
								}
								
								else {
									doing_multi = true;
									last_multi_symbol = CS[c];
								}
							}
							
							else {
								break;
							}
						}
						
						else
							if ((p = S_GET_INDEX (S_OPEN_TERM, CS[c])) >= 0) {
								if (!doing_term) {
									for (REGISTER ushort a = 0; a < NUM_MULTI_ALLOWED; a++) {
										if (open_multi_cnt[a] && close_multi_cnt[a]) {
											sprintf (err_msg,
											         "TERM substructure \"%c%c\" at position %d is not at MULTIJUNCTION of MULTI substructure \"%c%c\"",
											         S_OPEN_TERM[p], S_CLOSE_TERM[p], c + 1, S_OPEN_MULTI[a], S_CLOSE_MULTI[a]);
											err_found = true;
											break;
										}
									}
									
									if (!err_found) {
										for (REGISTER ushort a = 0; a < NUM_TERM_ALLOWED; a++) {
											if (open_term_cnt[a]) {
												sprintf (err_msg,
												         "TERM substructure \"%c%c\" at position %d is juxtaposed against TERM substructure \"%c%c\"",
												         S_OPEN_TERM[p], S_CLOSE_TERM[p], c + 1, S_OPEN_TERM[a], S_CLOSE_TERM[a]);
												err_found = true;
												break;
											}
										}
									}
								}
								
								if (!err_found) {
									doing_multi = false;
									last_multi_symbol = S_WHITE_SPACE[0];
									doing_term = true;
									last_term_symbol = CS[c];
									open_term_cnt[p]++;
								}
								
								else {
									break;
								}
							}
							
							else
								if ((p = S_GET_INDEX (S_CLOSE_TERM, CS[c])) >= 0) {
									if (!doing_term && !open_term_cnt[p]) {
										sprintf (err_msg,
										         "TERM substructure \"%c\" at position %d has no corresponding opening symbol \"%c\"",
										         S_CLOSE_TERM[p], c + 1, S_OPEN_TERM[p]);
										err_found = true;
									}
									
									if (!err_found) {
										doing_multi = false;
										last_multi_symbol = S_WHITE_SPACE[0];
										close_term_cnt[p]++;
										
										if (close_term_cnt[p] == open_term_cnt[p]) {
											open_term_cnt[p] = 0;
											close_term_cnt[p] = 0;
											doing_term = false;
											last_term_symbol = S_WHITE_SPACE[0];
										}
										
										else {
											doing_term = true;
											last_term_symbol = CS[c];
										}
									}
									
									else {
										break;
									}
								}
								
								else
									if ((p = S_GET_INDEX (S_HAIRPIN_RESIDUE, CS[c])) >= 0) {
										bool in_a_loop = false;
										ushort a = 0;
										
										while (a < NUM_TERM_ALLOWED) {
											if (open_term_cnt[a] && !close_term_cnt[a]) {
												in_a_loop = true;
												break;
											}
											
											a++;
										}
										
										if (!in_a_loop) {
											sprintf (err_msg,
											         "HAIRPIN RESIDUE %c at position %d is not within loop of TERM \"%c%c\" substructure",
											         S_HAIRPIN_RESIDUE[p], c + 1, S_OPEN_TERM[a], S_CLOSE_TERM[a]);
											err_found = true;
											break;
										}
										
										else {
											doing_multi = false;
											last_multi_symbol = S_WHITE_SPACE[0];
											doing_term = false;
											last_term_symbol = S_WHITE_SPACE[0];
										}
									}
									
									else
										if ((p = S_GET_INDEX (S_MULTI_RESIDUE, CS[c])) >= 0) {
											REGISTER
											bool in_junction = false;
											REGISTER
											ushort a = 0;
											
											while (a < NUM_MULTI_ALLOWED) {
												if (open_multi_cnt[a] && !close_multi_cnt[a]) {
													in_junction = true;
													break;
												}
												
												a++;
											}
											
											if (!in_junction) {
												sprintf (err_msg,
												         "MULTI RESIDUE \"%c\" at position %d is not at MULTIJUNCTION of a MULTI substructure",
												         S_MULTI_RESIDUE[p], c + 1);
												err_found = true;
											}
											
											else {
												for (a = 0; a < NUM_TERM_ALLOWED; a++) {
													if (open_term_cnt[a]) {
														sprintf (err_msg,
														         "MULTI RESIDUE \"%c\" at position %d is juxtaposed against TERM substructure \"%c%c\"",
														         S_MULTI_RESIDUE[p], c + 1, S_OPEN_TERM[a], S_CLOSE_TERM[a]);
														err_found = true;
														break;
													}
												}
											}
											
											if (!err_found) {
												doing_multi = false;
												last_multi_symbol = S_WHITE_SPACE[0];
												doing_term = false;
												last_term_symbol = S_WHITE_SPACE[0];
											}
											
											else {
												break;
											}
										}
										
										else
											if ((p = S_GET_INDEX (S_INTERIOR_RESIDUE, CS[c])) >= 0) {
												if (! (c > 0 &&
												       S_GET_INDEX (S_BASE_TRIPLE_PAIR, CS[c - 1]) >=
												       0)) {	// skip check is interior residue is preceeded by triple base pair symbol
													if ((!doing_term && !doing_multi) ||
													    (c >= strlen (CS) - 1) ||
													    ((S_GET_INDEX (S_INTERIOR_RESIDUE, CS[c + 1]) < 0) &&
													     (((S_GET_INDEX (S_OPEN_TERM, last_term_symbol) >= 0) &&
													       (S_GET_INDEX (S_OPEN_TERM, CS[c + 1]) < 0 &&
													        S_GET_INDEX (S_BASE_TRIPLE_PAIR, CS[c + 1]) < 0)) ||
													      ((S_GET_INDEX (S_CLOSE_TERM, last_term_symbol) >= 0) &&
													       (S_GET_INDEX (S_CLOSE_TERM, CS[c + 1]) < 0 &&
													        S_GET_INDEX (S_BASE_TRIPLE_PAIR, CS[c + 1]) < 0)) ||
													      ((S_GET_INDEX (S_OPEN_MULTI, last_multi_symbol) >= 0) &&
													       (S_GET_INDEX (S_OPEN_MULTI, CS[c + 1]) < 0 &&
													        S_GET_INDEX (S_BASE_TRIPLE_PAIR, CS[c + 1]) < 0)) ||
													      ((S_GET_INDEX (S_CLOSE_MULTI, last_multi_symbol) >= 0) &&
													       (S_GET_INDEX (S_CLOSE_MULTI, CS[c + 1]) < 0 &&
													        S_GET_INDEX (S_BASE_TRIPLE_PAIR, CS[c + 1]) < 0)))
													    )) {
														sprintf (err_msg,
														         "INTERIOR RESIDUE \"%c\" at position %d is not within a MULTI or TERM substructure",
														         S_INTERIOR_RESIDUE[p], c + 1);
														err_found = true;
														break;
													}
												}
											}
											
											else
												if (S_GET_INDEX (S_BASE_TRIPLE_PAIR, CS[c]) >= 0 ||
												    S_GET_INDEX (S_BASE_TRIPLE_SINGLE, CS[c]) >= 0 ||
												    S_GET_INDEX (S_OPEN_PK, CS[c]) >= 0 || S_GET_INDEX (S_CLOSE_PK, CS[c]) >= 0) {
													// we assume that BASE TRIPLEs and PKs extend a currently opening/closing multi/term, so no clearing of doing_* and last_*_symbol
													continue;
												}
												
												else {
													sprintf (err_msg, "unrecognized symbol %c at position %d", CS[c], c + 1);
													err_found = true;
													break;
												}
			}
		}
	}
	
	if (!err_found && pos_var_len) {
		short last_pos_var_idx = -1;
		char last_pos_var_symbols[S_MAX_LEN + 1];
		
		for (ushort i = (ushort) (last_pos_var_idx + 1); i < pos_var_len; i++) {
			if (S_GET_INDEX (S_WHITE_SPACE, pos_var[i]) >= 0) {
				continue;
			}
			
			if (!is_valid_pos_var (pos_var[i])) {
				sprintf (err_msg, "invalid pos_var symbol \"%c\" at position %d", pos_var[i],
				         i + 1);
				err_found = true;
				break;
			}
			
			else
				if (S_GET_INDEX (S_BASE_TRIPLE_SINGLE, CS[i]) >= 0 ||
				    (S_GET_INDEX (S_BASE_TRIPLE_PAIR, CS[i]) >= 0)) {
					sprintf (err_msg, "pos_var symbol for BASE_TRIPLE \"%c\" found at position %d",
					         CS[i], i + 1);
					err_found = true;
					break;
				}
				
				else
					if (S_GET_INDEX (S_CLOSE_MULTI, CS[i]) >= 0) {
						sprintf (err_msg, "pos_var symbol for CLOSE MULTI \"%c\" found at position %d",
						         CS[i], i + 1);
						err_found = true;
						break;
					}
					
					else
						if (S_GET_INDEX (S_CLOSE_TERM, CS[i]) >= 0) {
							sprintf (err_msg, "pos_var symbol for CLOSE TERM \"%c\" found at position %d",
							         CS[i], i + 1);
							err_found = true;
							break;
						}
						
						else
							if (S_GET_INDEX (S_CLOSE_PK, CS[i]) >= 0) {
								sprintf (err_msg, "pos_var symbol for CLOSE PK \"%c\" found at position %d",
								         CS[i], i + 1);
								err_found = true;
								break;
							}
							
			if (!err_found) {
				short p;
				
				if (last_pos_var_idx < 0) {
					last_pos_var_idx = i;
				}
				
				else {
					// check symbols spanning last_pos_var_idx and i for multiplicity of pos_vars
					ushort j = (ushort) (last_pos_var_idx + 1);
					
					while (j <= i) {
						if (S_GET_INDEX (last_pos_var_symbols, CS[j]) < 0) {
							break;
						}
						
						j++;
					}
					
					if (j > i) {
						/*
						 * in case of OPEN_TERM and OPEN_MULTI, duplicity is not an issue if the corresponding
						 * CLOSE symbols are discontinuous from where the first duplicate instance was found:
						 *
						 * e.g. <<<___>>>    is illegal
						 *      1 1
						 *
						 *      but
						 *
						 *      <<<___>->>   is valid, since the OPEN_TERM symbols constitute two "nested" helices
						 *      1 1
						 *
						 * TODO: currently, this check ONLY WORKS for OPEN symbols wrt CLOSE, but not vice versa,
						 *       such that  <-<<___>>>  would be deemed illegal
						 *                         1 1
						 */
						/*
						 * taking TERM as an example:
						 *
						 * <<< ... >>->
						 * a b     b' a'
						 *
						 * first "roll-back" from position b to a, then identify position a', and finally check if
						 * there are discontinuities between b' and a' (note that b may not necessarily be aligned
						 * with the first bp in a helix)
						 */
						bool skip = false;
						short a_dash = get_corresponding_close_position (CS, CS[last_pos_var_idx],
						                                        (ushort) last_pos_var_idx),
						               b_dash = get_corresponding_close_position (CS, CS[i], i);
						               
						if (a_dash > b_dash) {
							bool is_open_multi = S_GET_INDEX (S_OPEN_MULTI, CS[i]) >= 0,
							     is_open_term = S_GET_INDEX (S_OPEN_TERM, CS[i]) >= 0;
							     
							for (ushort k = (ushort) (b_dash + 1); k < a_dash; k++) {
								if ((is_open_multi && S_GET_INDEX (S_CLOSE_MULTI, CS[k]) < 0) ||
								    (is_open_term && S_GET_INDEX (S_CLOSE_TERM, CS[k]) < 0)) {
									skip = true;
									break;
								}
							}
						}
						
						if (!skip) {
							sprintf (err_msg,
							         "second (redundant) pos_var symbol \"%c\" found at position %d", pos_var[i],
							         i + 1);
							err_found = true;
							break;
						}
					}
					
					last_pos_var_idx = i;
				}
				
				if (S_GET_INDEX (S_UNSTRUCTURED_RESIDUE, CS[i]) >= 0) {
					g_memcpy (last_pos_var_symbols, S_UNSTRUCTURED_RESIDUE,
					          strlen (S_UNSTRUCTURED_RESIDUE));
					last_pos_var_symbols[strlen (S_UNSTRUCTURED_RESIDUE)] = '\0';
				}
				
				else
					if (S_GET_INDEX (S_OPEN_MULTI, CS[i]) >= 0) {
						g_memcpy (last_pos_var_symbols, S_OPEN_MULTI, strlen (S_OPEN_MULTI));
						last_pos_var_symbols[strlen (S_OPEN_MULTI)] = '\0';
					}
					
					else
						if (S_GET_INDEX (S_OPEN_TERM, CS[i]) >= 0) {
							g_memcpy (last_pos_var_symbols, S_OPEN_TERM, strlen (S_OPEN_TERM));
							last_pos_var_symbols[strlen (S_OPEN_TERM)] = '\0';
						}
						
						else
							if ((p = S_GET_INDEX (S_OPEN_PK, CS[i])) >= 0) {
								g_memcpy (last_pos_var_symbols, &S_OPEN_PK[p],
								          1);    // S_OPEN_K contains char representations of *unique* instances of PK,
								// so only copy the found symbol here and not any other chars in S_OPEN_K
								last_pos_var_symbols[1] = '\0';
							}
							
							else
								if (S_GET_INDEX (S_HAIRPIN_RESIDUE, CS[i]) >= 0) {
									g_memcpy (last_pos_var_symbols, S_HAIRPIN_RESIDUE, strlen (S_HAIRPIN_RESIDUE));
									last_pos_var_symbols[strlen (S_HAIRPIN_RESIDUE)] = '\0';
								}
								
								else
									if (S_GET_INDEX (S_MULTI_RESIDUE, CS[i]) >= 0) {
										g_memcpy (last_pos_var_symbols, S_MULTI_RESIDUE, strlen (S_MULTI_RESIDUE));
										last_pos_var_symbols[strlen (S_MULTI_RESIDUE)] = '\0';
									}
									
									else
										if (S_GET_INDEX (S_INTERIOR_RESIDUE, CS[i]) >= 0) {
											g_memcpy (last_pos_var_symbols, S_INTERIOR_RESIDUE,
											          strlen (S_INTERIOR_RESIDUE));
											last_pos_var_symbols[strlen (S_INTERIOR_RESIDUE)] = '\0';
										}
			}
		}
	}
	
	if (err_found) {
		*err = MALLOC_DEBUG (MAX_ERR_STRING_LEN + 1,
		                     "err message string in is_valid_CSSD");
		                     
		if (!*err) {
			COMMIT_DEBUG (REPORT_ERRORS, INTERFACE,
			              "cannot allocate memory for err message string in is_valid_CSSD", false);
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

bool convert_CSSD_substructure_to_model (const char *restrict CS,
                                        const char *restrict pos_var,
                                        ntp_model restrict model, ntp_element restrict prev_el,
                                        const nt_branch_type prev_el_br_type,
                                        char **constraint_symbol,
                                        ntp_constraint **constraint,
                                        void ***constraint_param1,
                                        void ***constraint_param2,
                                        void ***constraint_param3,
                                        void ***constraint_param4,
                                        ushort *num_constraints) {
	ushort CS_len;
	
	if (CS) {
		CS_len = (ushort) strlen (CS);
	}
	
	else {
		CS_len = 0;
	}
	
	if (!CS_len) {
		return true;    // nothing to do here
	}
	
	ushort pos_var_len;
	
	if (pos_var) {
		pos_var_len = (ushort) strlen (pos_var);
	}
	
	else {
		pos_var_len = 0;
	}
	
	REGISTER
	ushort current_posn = 0;
	REGISTER
	short p;
	ushort this_pos_var = 0;
	
	if (S_GET_INDEX (S_OPEN_MULTI, CS[current_posn]) >= 0) {
		ushort open_multi_cnt = current_posn, tmp;
		const ushort open_posn = current_posn;
		
		do {
			if (!this_pos_var && pos_var_len &&
			    S_GET_INDEX (S_WHITE_SPACE, pos_var[current_posn]) < 0) {
				this_pos_var = get_pos_var_value (pos_var[current_posn]);
			}
			
			current_posn++;
		}
		while (current_posn < CS_len &&
		       (S_GET_INDEX (S_OPEN_MULTI, CS[current_posn]) >= 0));
		       
		open_multi_cnt = current_posn - open_multi_cnt; // number of open multi symbols
		tmp = open_multi_cnt;
		
		while (current_posn < CS_len && (open_multi_cnt > 0)) {
			if (S_GET_INDEX (S_OPEN_MULTI, CS[current_posn]) >= 0) {
				open_multi_cnt++;
			}
			
			else
				if (S_GET_INDEX (S_CLOSE_MULTI, CS[current_posn]) >= 0) {
					open_multi_cnt--;
				}
				
				else
					if (open_multi_cnt < tmp) {
						// we start, optimistically, by assuming that the initial (open_multi_cnt==)tmp can be matched by
						// closing symbols. if not the case, by, for example, having the string of closing symbols 'broken'
						// up by interior residues, then tmp is iteratively reduced until the open/close symbols can match
						// in a contiguous fashion
						tmp = open_multi_cnt;
					}
					
			current_posn++;
		}
		
		if (open_multi_cnt) {
			COMMIT_DEBUG (REPORT_ERRORS, INTERFACE,
			              "failed to close MULTI while building model in convert_CSSD_substructure_to_model",
			              false);
			return false;
		}
		
		open_multi_cnt = tmp;
		const ushort close_posn = current_posn;
		ntp_element new_el;
		
		if (this_pos_var) {
			ntp_element e = initialize_element (paired,
			                                    (nt_element_count) (open_multi_cnt - 1),
			                                    (nt_element_count) (open_multi_cnt - 1 + this_pos_var));
			new_el = add_element_to_model (model, prev_el, e, prev_el_br_type);
		}
		
		else {
			new_el = add_element_to_model (model, prev_el, initialize_element (paired,
			                                        open_multi_cnt, open_multi_cnt), prev_el_br_type);
		}
		
		char delta_CS_fp[close_posn - open_posn - (open_multi_cnt * 2) + 1],
		     delta_CS_tp[CS_len - close_posn + 1],
		     delta_pos_var_fp[close_posn - open_posn - (open_multi_cnt * 2) + 1],
		     delta_pos_var_tp[pos_var_len - close_posn + 1];
		GET_SUBSTRING (CS, open_posn + open_multi_cnt,
		               (short) (close_posn - (open_multi_cnt * 2)), delta_CS_fp);
		GET_SUBSTRING (CS, close_posn, (short) (CS_len - close_posn), delta_CS_tp);
		GET_SUBSTRING (pos_var, open_posn + open_multi_cnt,
		               (short) (close_posn - (open_multi_cnt * 2)), delta_pos_var_fp);
		GET_SUBSTRING (pos_var, close_posn, (short) (pos_var_len - close_posn),
		               delta_pos_var_tp);
		return convert_CSSD_substructure_to_model (delta_CS_fp, delta_pos_var_fp, model,
		                                        new_el, five_prime,
		                                        constraint_symbol, constraint,
		                                        constraint_param1, constraint_param2, constraint_param3, constraint_param4,
		                                        num_constraints) &&
		       convert_CSSD_substructure_to_model (delta_CS_tp, delta_pos_var_tp, model,
		                                               new_el, three_prime,
		                                               constraint_symbol, constraint,
		                                               constraint_param1, constraint_param2, constraint_param3, constraint_param4,
		                                               num_constraints);
	}
	
	else
		if (S_GET_INDEX (S_OPEN_TERM, CS[current_posn]) >= 0) {
			ushort open_term_cnt = current_posn, tmp;
			const ushort open_posn = current_posn;
			
			do {
				if (!this_pos_var && pos_var_len &&
				    S_GET_INDEX (S_WHITE_SPACE, pos_var[current_posn]) < 0) {
					this_pos_var = get_pos_var_value (pos_var[current_posn]);
				}
				
				current_posn++;
			}
			while (current_posn < CS_len &&
			       (S_GET_INDEX (S_OPEN_TERM, CS[current_posn]) >= 0));
			       
			open_term_cnt = current_posn - open_term_cnt; // number of open term symbols
			tmp = open_term_cnt;
			
			while (current_posn < CS_len && (open_term_cnt > 0)) {
				if (S_GET_INDEX (S_OPEN_TERM, CS[current_posn]) >= 0) {
					open_term_cnt++;
				}
				
				else
					if (S_GET_INDEX (S_CLOSE_TERM, CS[current_posn]) >= 0) {
						open_term_cnt--;
					}
					
					else
						if (open_term_cnt < tmp) {
							// we start, optimistically, by assuming that the initial (open_term_cnt==)tmp can be matched by
							// closing symbols. if not the case, by, for example, having the string of closing symbols 'broken'
							// up by interior residues, then tmp is iteratively reduced until the open/close symbols can match
							// in a contiguous fashion
							tmp = open_term_cnt;
						}
						
				current_posn++;
			}
			
			if (open_term_cnt) {
				COMMIT_DEBUG (REPORT_ERRORS, INTERFACE,
				              "failed to close TERM while building model in convert_CSSD_substructure_to_model",
				              false);
				return false;
			}
			
			open_term_cnt = tmp;
			const ushort close_posn = current_posn;
			ntp_element new_el;
			
			if (this_pos_var) {
				ntp_element e = initialize_element (paired,
				                                    (nt_element_count) (open_term_cnt - 1),
				                                    (nt_element_count) (open_term_cnt - 1 + this_pos_var));
				new_el = add_element_to_model (model, prev_el, e, prev_el_br_type);
			}
			
			else {
				new_el = add_element_to_model (model, prev_el, initialize_element (paired,
				                                        open_term_cnt, open_term_cnt), prev_el_br_type);
			}
			
			char delta_CS_fp[close_posn - open_posn - (open_term_cnt * 2) + 1],
			     delta_CS_tp[CS_len - close_posn + 1],
			     delta_pos_var_fp[close_posn - open_posn - (open_term_cnt * 2) + 1],
			     delta_pos_var_tp[pos_var_len - close_posn + 1];
			GET_SUBSTRING (CS, open_posn + open_term_cnt,
			               (short) (close_posn - (open_term_cnt * 2)), delta_CS_fp);
			GET_SUBSTRING (CS, close_posn, CS_len - close_posn, delta_CS_tp);
			GET_SUBSTRING (pos_var, open_posn + open_term_cnt,
			               (short) (close_posn - (open_term_cnt * 2)), delta_pos_var_fp);
			GET_SUBSTRING (pos_var, close_posn, (short) (pos_var_len - close_posn),
			               delta_pos_var_tp);
			return convert_CSSD_substructure_to_model (delta_CS_fp, delta_pos_var_fp, model,
			                                        new_el, five_prime,
			                                        constraint_symbol, constraint,
			                                        constraint_param1, constraint_param2, constraint_param3, constraint_param4,
			                                        num_constraints) &&
			       convert_CSSD_substructure_to_model (delta_CS_tp, delta_pos_var_tp, model,
			                                               new_el, three_prime,
			                                               constraint_symbol, constraint,
			                                               constraint_param1, constraint_param2, constraint_param3, constraint_param4,
			                                               num_constraints);
		}
		
		else
			if ((p = S_GET_INDEX (S_OPEN_PK,
			                      CS[current_posn])) >= 0) {
				// assumes that, after successful validation, open PK occurs before close PK
				do {
					if (!this_pos_var && pos_var_len &&
					    S_GET_INDEX (S_WHITE_SPACE, pos_var[current_posn]) < 0) {
						this_pos_var = get_pos_var_value (pos_var[current_posn]);
					}
					
					current_posn++;
				}
				while (current_posn < CS_len &&
				       (S_GET_INDEX (S_OPEN_PK, CS[current_posn]) == p));
				       
				ntp_constraint pk_constraint;
				
				if (this_pos_var) {
					pk_constraint = initialize_pseudoknot ((nt_element_count) (current_posn - 1),
					                                       (nt_element_count) (current_posn - 1 + this_pos_var));
				}
				
				else {
					pk_constraint = initialize_pseudoknot (current_posn, current_posn);
				}
				
				// store matching *close* symbol
				(*constraint_symbol)[*num_constraints] = S_CLOSE_PK[p];
				(*constraint)[*num_constraints] = pk_constraint;
				(*constraint_param1)[*num_constraints] = (void *)prev_el;
				(*constraint_param2)[*num_constraints] = (void *)prev_el_br_type;
				(*num_constraints)++;
				char delta_CS[CS_len - current_posn + 1],
				     delta_pos_var[pos_var_len - current_posn + 1];
				GET_SUBSTRING (CS, current_posn, CS_len - current_posn, delta_CS);
				GET_SUBSTRING (pos_var, current_posn, pos_var_len - current_posn,
				               delta_pos_var);
				return convert_CSSD_substructure_to_model (delta_CS, delta_pos_var, model,
				                                        pk_constraint->pseudoknot->fp_element, unbranched,
				                                        constraint_symbol, constraint,
				                                        constraint_param1, constraint_param2, constraint_param3, constraint_param4,
				                                        num_constraints);
			}
			
			else
				if ((p = S_GET_INDEX (S_CLOSE_PK, CS[current_posn])) >= 0) {
					do {
						current_posn++;
					}
					while (current_posn < CS_len &&
					       (S_GET_INDEX (S_CLOSE_PK, CS[current_posn]) == p));
					       
					ushort i;
					
					for (i = 0; i < *num_constraints; i++) {
						if ((*constraint_symbol)[i] == S_CLOSE_PK[p]) {
							break;
						}
					}
					
					if (i == *num_constraints) {
						COMMIT_DEBUG (REPORT_ERRORS, INTERFACE,
						              "failed to match OPEN and CLOSE PK symbols in convert_CSSD_substructure_to_model",
						              false);
						return false;
					}
					
					if (add_pseudoknot_to_model (model, (*constraint_param1)[i],
					                             (nt_branch_type) (*constraint_param2)[i], prev_el, prev_el_br_type,
					                             (*constraint)[i])) {
						(*constraint_symbol)[i] = '\0';
					}
					
					char delta_CS[CS_len - current_posn + 1],
					     delta_pos_var[pos_var_len - current_posn + 1];
					GET_SUBSTRING (CS, current_posn, CS_len - current_posn, delta_CS);
					GET_SUBSTRING (pos_var, current_posn, pos_var_len - current_posn,
					               delta_pos_var);
					return convert_CSSD_substructure_to_model (delta_CS, delta_pos_var, model,
					                                        (*constraint)[i]->pseudoknot->tp_element, unbranched,
					                                        constraint_symbol, constraint,
					                                        constraint_param1, constraint_param2, constraint_param3, constraint_param4,
					                                        num_constraints);
				}
				
				else
					if ((p = S_GET_INDEX (S_BASE_TRIPLE_PAIR, CS[current_posn])) >= 0 ||
					    (p = S_GET_INDEX (S_BASE_TRIPLE_SINGLE, CS[current_posn])) >= 0)
						// assumes successful base triple validation => no pos_vars, and single positions for pair and single
						// TODO: GET_SUBSTRING below assumes base triple (its pair) is in the middle of a helix
					{
						/*
						 * for base triples, the first (fp) contact is always stored in constraint_param1/constraint_param2;
						 * constraint_param3/constraint_param4 to hold either second (tp) contact, or the triple contact, whichever is parsed first
						 */
						// check if this is the first parsing step for this base triple
						ushort i;
						
						for (i = 0; i < *num_constraints; i++) {
							if ((*constraint_symbol)[i] ==
							    S_BASE_TRIPLE_PAIR[p]) { // use S_BASE_TRIPLE_PAIR and not S_BASE_TRIPLE_SINGLE as indicator
								break;
							}
						}
						
						ntp_element this_element = NULL;
						
						if (i == *num_constraints) {
							// first encounter with base triple structure
							(*constraint_symbol)[*num_constraints] =
							                    S_BASE_TRIPLE_PAIR[p]; // use S_BASE_TRIPLE_PAIR and not S_BASE_TRIPLE_SINGLE as indicator
							ntp_constraint bt_constraint =
							                    initialize_base_triple();        // TODO: validate allocation
							(*constraint)[*num_constraints] = bt_constraint;
							
							if ((S_GET_INDEX (S_BASE_TRIPLE_PAIR, CS[current_posn])) >= 0) {
								// always store first of base pair (i.e. fp) in _param1/2
								(*constraint_param1)[*num_constraints] = (void *) prev_el;
								(*constraint_param2)[*num_constraints] = (void *) prev_el_br_type;
								this_element = bt_constraint->base_triple->fp_element;
							}
							
							else {
								// otherwise store single in _param3/4
								(*constraint_param3)[*num_constraints] = (void *) prev_el;
								(*constraint_param4)[*num_constraints] = (void *) prev_el_br_type;
								this_element = bt_constraint->base_triple->single_element;
							}
							
							(*num_constraints)++;
						}
						
						else {
							// already have partial, or, all data for this base triple
							nt_branch_type *first_branch_type = (*constraint_param2)[i],
							                *second_branch_type = (*constraint_param4)[i];
							                
							if ((first_branch_type == (void *)no_branch_type) ||
							    (second_branch_type == (void *)no_branch_type)) {
								// we have partial data
								if ((S_GET_INDEX (S_BASE_TRIPLE_PAIR, CS[current_posn])) >= 0) {
									if (first_branch_type == (void *)no_branch_type) {
										// now have first of pair (fp)
										(*constraint_param1)[i] = (void *) prev_el;
										(*constraint_param2)[i] = (void *) prev_el_br_type;
										this_element = ((*constraint)[i])->base_triple->fp_element;
									}
									
									else {
										// now have second of pair (tp)
										(*constraint_param3)[i] = (void *) prev_el;
										(*constraint_param4)[i] = (void *) prev_el_br_type;
										this_element = ((*constraint)[i])->base_triple->tp_element;
									}
								}
								
								else {
									// single of triple should always be in _param3/4                   // TODO: validation checks apart from is_valid_CSSD ?
									(*constraint_param3)[i] = (void *) prev_el;
									(*constraint_param4)[i] = (void *) prev_el_br_type;
									this_element = ((*constraint)[i])->base_triple->single_element;
								}
							}
							
							else {
								// we have all data, add base triple to model
								bool ret_val;
								
								/*
								 * we get to this point in only one of two ways:
								 *
								 * a) current element is the second of S_BASE_TRIPLE_PAIR; or
								 * b) current element is the single S_BASE_TRIPLE_SINGLE
								 *
								 */
								if ((S_GET_INDEX (S_BASE_TRIPLE_PAIR, CS[current_posn])) >= 0) {
									// => param1 holds first of base pair, param3 holds single element
									ret_val = add_base_triple_to_model (model,
									                                    (*constraint_param1)[i], (nt_branch_type) (*constraint_param2)[i],
									                                    prev_el, prev_el_br_type,
									                                    (*constraint_param3)[i], (nt_branch_type) (*constraint_param4)[i],
									                                    (*constraint)[i]);
									this_element = ((*constraint)[i])->base_triple->tp_element;
								}
								
								else {
									// => param1 holds first of base pair, param3 holds second of base pair
									ret_val = add_base_triple_to_model (model,
									                                    (*constraint_param1)[i], (nt_branch_type) (*constraint_param2)[i],
									                                    (*constraint_param3)[i], (nt_branch_type) (*constraint_param4)[i],
									                                    prev_el, prev_el_br_type,
									                                    (*constraint)[i]);
									this_element = ((*constraint)[i])->base_triple->single_element;
								}
								
								if (!ret_val) {
									return false;
								}
								
								else {
									(*constraint_symbol)[i] = '\0';
								}
							}
						}
						
						current_posn++;     // is_valid_CSSD presumes only 1 position for fp,tp,single of base triple
						char delta_CS[CS_len - current_posn + 1],
						     delta_pos_var[pos_var_len - current_posn + 1];
						// in this case add -1 to *_len-current_posn, since base triple fp and tp are
						//  processed together, and therefore we need to truncate the trailing symbol
						GET_SUBSTRING (CS, current_posn, (short) (CS_len - current_posn), delta_CS);
						GET_SUBSTRING (pos_var, current_posn, (short) (pos_var_len - current_posn),
						               delta_pos_var);
						return convert_CSSD_substructure_to_model (delta_CS, delta_pos_var, model,
						                                        this_element, unbranched,
						                                        constraint_symbol, constraint,
						                                        constraint_param1, constraint_param2, constraint_param3, constraint_param4,
						                                        num_constraints);
					}
					
					else
						if (S_GET_INDEX (S_HAIRPIN_RESIDUE, CS[current_posn]) >= 0) {
							do {
								if (!this_pos_var && pos_var_len &&
								    S_GET_INDEX (S_WHITE_SPACE, pos_var[current_posn]) < 0) {
									this_pos_var = get_pos_var_value (pos_var[current_posn]);
								}
								
								current_posn++;
							}
							while (current_posn < CS_len &&
							       (S_GET_INDEX (S_HAIRPIN_RESIDUE, CS[current_posn]) >= 0));
							       
							ntp_element new_el;
							
							if (this_pos_var) {
								ntp_element e = initialize_element (unpaired,
								                                    (nt_element_count) (current_posn - 1),
								                                    (nt_element_count) (current_posn - 1 + this_pos_var));
								new_el = add_element_to_model (model, prev_el, e, prev_el_br_type);
							}
							
							else {
								new_el = add_element_to_model (model, prev_el, initialize_element (unpaired,
								                                        current_posn, current_posn), prev_el_br_type);
							}
							
							char delta_CS[CS_len - current_posn + 1],
							     delta_pos_var[pos_var_len - current_posn + 1];
							GET_SUBSTRING (CS, current_posn, CS_len - current_posn, delta_CS);
							GET_SUBSTRING (pos_var, current_posn, pos_var_len - current_posn,
							               delta_pos_var);
							return convert_CSSD_substructure_to_model (delta_CS, delta_pos_var, model,
							                                        new_el, unbranched,
							                                        constraint_symbol, constraint,
							                                        constraint_param1, constraint_param2, constraint_param3, constraint_param4,
							                                        num_constraints);
						}
						
						else
							if (S_GET_INDEX (S_MULTI_RESIDUE, CS[current_posn]) >= 0) {
								do {
									if (!this_pos_var && pos_var_len &&
									    S_GET_INDEX (S_WHITE_SPACE, pos_var[current_posn]) < 0) {
										this_pos_var = get_pos_var_value (pos_var[current_posn]);
									}
									
									current_posn++;
								}
								while (current_posn < CS_len &&
								       (S_GET_INDEX (S_MULTI_RESIDUE, CS[current_posn]) >= 0));
								       
								ntp_element new_el;
								
								if (this_pos_var) {
									new_el = add_element_to_model (model, prev_el,
									                               initialize_element (unpaired, (nt_element_count) (current_posn - 1),
									                                                                       (nt_element_count) (current_posn - 1 + this_pos_var)), prev_el_br_type);
								}
								
								else {
									new_el = add_element_to_model (model, prev_el, initialize_element (unpaired,
									                                        current_posn, current_posn), prev_el_br_type);
								}
								
								char delta_CS[CS_len - current_posn + 1],
								     delta_pos_var[pos_var_len - current_posn + 1];
								GET_SUBSTRING (CS, current_posn, CS_len - current_posn, delta_CS);
								GET_SUBSTRING (pos_var, current_posn, pos_var_len - current_posn,
								               delta_pos_var);
								return convert_CSSD_substructure_to_model (delta_CS, delta_pos_var, model,
								                                        new_el, unbranched,
								                                        constraint_symbol, constraint,
								                                        constraint_param1, constraint_param2, constraint_param3, constraint_param4,
								                                        num_constraints);
							}
							
							else
								if (S_GET_INDEX (S_UNSTRUCTURED_RESIDUE, CS[current_posn]) >= 0) {
									do {
										if (!this_pos_var && pos_var_len &&
										    S_GET_INDEX (S_WHITE_SPACE, pos_var[current_posn]) < 0) {
											this_pos_var = get_pos_var_value (pos_var[current_posn]);
										}
										
										current_posn++;
									}
									while (current_posn < CS_len &&
									       (S_GET_INDEX (S_UNSTRUCTURED_RESIDUE, CS[current_posn]) >= 0));
									       
									ntp_element new_el;
									
									if (this_pos_var) {
										new_el = add_element_to_model (model, prev_el,
										                               initialize_element (unpaired, (nt_element_count) (current_posn - 1),
										                                                                       (nt_element_count) (current_posn - 1 + this_pos_var)), prev_el_br_type);
									}
									
									else {
										new_el = add_element_to_model (model, prev_el, initialize_element (unpaired,
										                                        current_posn, current_posn), prev_el_br_type);
									}
									
									char delta_CS[CS_len - current_posn + 1],
									     delta_pos_var[pos_var_len - current_posn + 1];
									GET_SUBSTRING (CS, current_posn, CS_len - current_posn, delta_CS);
									GET_SUBSTRING (pos_var, current_posn, pos_var_len - current_posn,
									               delta_pos_var);
									return convert_CSSD_substructure_to_model (delta_CS, delta_pos_var, model,
									                                        new_el, unbranched,
									                                        constraint_symbol, constraint,
									                                        constraint_param1, constraint_param2, constraint_param3, constraint_param4,
									                                        num_constraints);
								}
								
								else
									if (S_GET_INDEX (S_INTERIOR_RESIDUE, CS[current_posn]) >= 0) {
										do {
											if (!this_pos_var && pos_var_len &&
											    S_GET_INDEX (S_WHITE_SPACE, pos_var[current_posn]) < 0) {
												this_pos_var = get_pos_var_value (pos_var[current_posn]);
											}
											
											current_posn++;
										}
										while (current_posn < CS_len &&
										       (S_GET_INDEX (S_INTERIOR_RESIDUE, CS[current_posn]) >= 0));
										       
										ntp_element new_el;
										
										if (this_pos_var) {
											new_el = add_element_to_model (model, prev_el,
											                               initialize_element (unpaired, (nt_element_count) (current_posn - 1),
											                                                                       (nt_element_count) (current_posn - 1 + this_pos_var)), prev_el_br_type);
										}
										
										else {
											new_el = add_element_to_model (model, prev_el, initialize_element (unpaired,
											                                        current_posn, current_posn), prev_el_br_type);
										}
										
										char delta_CS[CS_len - current_posn + 1],
										     delta_pos_var[pos_var_len - current_posn + 1];
										GET_SUBSTRING (CS, current_posn, CS_len - current_posn, delta_CS);
										GET_SUBSTRING (pos_var, current_posn, pos_var_len - current_posn,
										               delta_pos_var);
										return convert_CSSD_substructure_to_model (delta_CS, delta_pos_var, model,
										                                        new_el, unbranched,
										                                        constraint_symbol, constraint,
										                                        constraint_param1, constraint_param2, constraint_param3, constraint_param4,
										                                        num_constraints);
									}
									
	return true;
}

bool convert_CSSD_to_model (const char *restrict ss,
                            const char *restrict pos_var, ntp_model *restrict model,
                            char *restrict *err_msg) {
	COMMIT_DEBUG (REPORT_INFO, INTERFACE,
	              "initiating conversion of CSSD to a model in onvert_CSSD_to_model", true);
	#ifdef DEBUG_ON
	char msg[MAX_ERR_STRING_LEN + 100];
	#endif
	COMMIT_DEBUG (REPORT_INFO, INTERFACE,
	              "validating CSSD in convert_CSSD_to_model", true);
	              
	if (!is_valid_CSSD (ss, pos_var, err_msg)) {
		if (! (*err_msg)) {
			COMMIT_DEBUG (REPORT_ERRORS, INTERFACE,
			              "failed to validate CSSD (and to retrieve err msg) in convert_CSSD_to_model",
			              false);
		}
		
		else {
			#ifdef DEBUG_ON
			sprintf (msg, "failed to validate CSSD in convert_CSSD_to_model: %s", *err_msg);
			COMMIT_DEBUG (REPORT_ERRORS, INTERFACE, msg, false);
			#endif
		}
		
		return false;
	}
	
	else {
		COMMIT_DEBUG (REPORT_INFO, INTERFACE,
		              "CSSD validated successfully in convert_CSSD_to_model", false);
	}
	
	COMMIT_DEBUG (REPORT_INFO, INTERFACE,
	              "converting CSSD into model in convert_CSSD_to_model", true);
	*model = initialize_model();
	
	if (!*model) {
		COMMIT_DEBUG (REPORT_ERRORS, INTERFACE,
		              "failed to initialize model in convert_CSSD_to_model", false);
		return false;
	}
	
	// TODO: check allocation
	char *constraint_symbol =
	                    MALLOC_DEBUG (MAX_CONSTRAINT_MATCHES * sizeof (char),
	                                  "constraint_symbol for convert_CSSD_substructure_to_model in convert_CSSD_to_model");
	ntp_constraint *constraint =
	                    MALLOC_DEBUG (MAX_CONSTRAINT_MATCHES * sizeof (nt_constraint),
	                                  "constraint for convert_CSSD_substructure_to_model in convert_CSSD_to_model");
	void **constraint_param1 =
	                    MALLOC_DEBUG (MAX_CONSTRAINT_MATCHES * sizeof (void *),
	                                  "constraint_param1 for convert_CSSD_substructure_to_model in convert_CSSD_to_model");
	void **constraint_param2 =
	                    MALLOC_DEBUG (MAX_CONSTRAINT_MATCHES * sizeof (void *),
	                                  "constraint_param2 for convert_CSSD_substructure_to_model in convert_CSSD_to_model");
	void **constraint_param3 =
	                    MALLOC_DEBUG (MAX_CONSTRAINT_MATCHES * sizeof (void *),
	                                  "constraint_param3 for convert_CSSD_substructure_to_model in convert_CSSD_to_model");
	void **constraint_param4 =
	                    MALLOC_DEBUG (MAX_CONSTRAINT_MATCHES * sizeof (void *),
	                                  "constraint_param4 for convert_CSSD_substructure_to_model in convert_CSSD_to_model");
	ushort num_constraints = 0;
	// will use _param2, _param4 to test availability of
	// parameter, so no need to initialize _param1, _param3
	g_memset (constraint_param2, no_branch_type,
	          MAX_CONSTRAINT_MATCHES * sizeof (void *));
	g_memset (constraint_param4, no_branch_type,
	          MAX_CONSTRAINT_MATCHES * sizeof (void *));
	bool success;
	size_t ss_len = strlen (ss), pos_var_len = strlen (pos_var);
	
	if (pos_var_len < ss_len) {
		char new_pos_var[ss_len + 1];
		g_memcpy (new_pos_var, pos_var, pos_var_len);
		
		for (size_t i = pos_var_len; i < ss_len; i++) {
			new_pos_var[i] = ' ';
		}
		
		new_pos_var[ss_len] = '\0';
		success = convert_CSSD_substructure_to_model (ss, new_pos_var, *model, NULL,
		                                        unbranched, &constraint_symbol, &constraint,
		                                        &constraint_param1, &constraint_param2, &constraint_param3, &constraint_param4,
		                                        &num_constraints);
	}
	
	else {
		success = convert_CSSD_substructure_to_model (ss, pos_var, *model, NULL,
		                                        unbranched, &constraint_symbol, &constraint,
		                                        &constraint_param1, &constraint_param2, &constraint_param3, &constraint_param4,
		                                        &num_constraints);
	}
	
	if (!success) {
		COMMIT_DEBUG (REPORT_ERRORS, INTERFACE,
		              "failed to convert CSSD to model in convert_CSSD_to_model", false);
	}
	
	else {
		for (ushort i = 0; i < num_constraints; i++) {
			if (constraint_symbol[i] != '\0') {
				COMMIT_DEBUG1 (REPORT_ERRORS, INTERFACE,
				               "failed to build constraint \"%c\" when converting CSSD to model in convert_CSSD_to_model",
				               constraint_symbol[i], false);
				success = false;
				
				if (constraint[i]->type == pseudoknot) {
					FREE_DEBUG (constraint[i]->pseudoknot,
					            "pseudoknot constraint in convert_CSSD_to_model [failed to build constraint]");
				}
				
				else
					if (constraint[i]->type == base_triple) {
						FREE_DEBUG (constraint[i]->base_triple,
						            "base triple constraint in convert_CSSD_to_model [failed to build constraint]");
					}
			}
		}
	}
	
	FREE_DEBUG (constraint_symbol,
	            "constraint_symbol for convert_CSSD_substructure_to_model in convert_CSSD_to_model");
	FREE_DEBUG (constraint,
	            "constraint for convert_CSSD_substructure_to_model in convert_CSSD_to_model");
	FREE_DEBUG (constraint_param1,
	            "constraint_param1 for convert_CSSD_substructure_to_model in convert_CSSD_to_model");
	FREE_DEBUG (constraint_param2,
	            "constraint_param2 for convert_CSSD_substructure_to_model in convert_CSSD_to_model");
	FREE_DEBUG (constraint_param3,
	            "constraint_param3 for convert_CSSD_substructure_to_model in convert_CSSD_to_model");
	FREE_DEBUG (constraint_param4,
	            "constraint_param4 for convert_CSSD_substructure_to_model in convert_CSSD_to_model");
	            
	if (!success) {
		finalize_model (*model);
		*model = NULL;
		return false;
	}
	
	COMMIT_DEBUG (REPORT_INFO, INTERFACE,
	              "CSSD successfully converted to model in convert_CSSD_to_model", false);
	return true;
}

void write_out_model_part (ntp_constraint restrict constraint,
                           ntp_element restrict el, char *restrict model_string,
                           char *restrict pos_var_string) {
	ushort curr_len = (ushort) strlen (model_string);
	ushort var_min = 0;
	
	if (el->type == unpaired) {
		char ss_symbol = SS_NEUTRAL_UNKNOWN_SYMBOL;
		ntp_constraint this_constraint = constraint;
		
		while (this_constraint) {
			if (el->unpaired->i_constraint.reference == this_constraint) {
				switch (this_constraint->type) {
					case    pseudoknot:
						if (this_constraint->pseudoknot->fp_element == el) {
							ss_symbol = SS_NEUTRAL_OPEN_PK;
						}
						
						else {
							ss_symbol = SS_NEUTRAL_CLOSE_PK;
						}
						
						break;
						
					case    base_triple:
						if (this_constraint->base_triple->single_element == el) {
							ss_symbol = SS_NEUTRAL_BT_SINGLE;
						}
						
						else {
							ss_symbol = SS_NEUTRAL_BT_PAIR;
						}
						
					default:
						break;
				}
				
				break;
			}
			
			this_constraint = this_constraint->next;
		}
		
		var_min = el->unpaired->min;
		
		if (el->unpaired->min != el->unpaired->max) {
			var_min++;
		}
		
		for (ushort i = 0; i < var_min; i++) {
			model_string[curr_len + i] = ss_symbol;
			pos_var_string[curr_len + i] = S_WHITE_SPACE[0];
		}
		
		model_string[curr_len + var_min] = '\0';
		pos_var_string[curr_len + var_min] = '\0';
		
		if (el->unpaired->min != el->unpaired->max &&
		    (ss_symbol == SS_NEUTRAL_UNKNOWN_SYMBOL ||
		     ss_symbol == SS_NEUTRAL_OPEN_PK)) {  // don't write at SS_NEUTRAL_CLOSE_PK
			pos_var_string[curr_len] = write_pos_var_value (el->unpaired->max -
			                                        el->unpaired->min);
		}
		
		if (el->unpaired->next) {
			write_out_model_part (constraint, el->unpaired->next, model_string,
			                      pos_var_string);
		}
	}
	
	else
		if (el->type == paired) {
			var_min = el->paired->min;
			
			if (el->paired->min != el->paired->max) {
				var_min++;
			}
			
			for (ushort i = 0; i < var_min; i++) {
				model_string[curr_len + i] = SS_NEUTRAL_OPEN_TERM;
				pos_var_string[curr_len + i] = S_WHITE_SPACE[0];
			}
			
			model_string[curr_len + var_min] = '\0';
			pos_var_string[curr_len + var_min] = '\0';
			
			if (el->paired->min != el->paired->max) {
				pos_var_string[curr_len] = write_pos_var_value (el->paired->max -
				                                        el->paired->min);
			}
			
			write_out_model_part (constraint, el->paired->fp_next, model_string,
			                      pos_var_string);
			curr_len = (ushort) strlen (model_string);
			
			for (ushort i = 0; i < var_min; i++) {
				model_string[curr_len + i] = SS_NEUTRAL_CLOSE_TERM;
				pos_var_string[curr_len + i] = S_WHITE_SPACE[0];
			}
			
			model_string[curr_len + var_min] = '\0';
			pos_var_string[curr_len + var_min] = '\0';
			
			if (el->paired->tp_next) {
				write_out_model_part (constraint, el->paired->tp_next, model_string,
				                      pos_var_string);
			}
		}
}

bool write_out_model (ntp_model model, char **model_string,
                      char **pos_var_string) {
	COMMIT_DEBUG (REPORT_INFO, INTERFACE, "stringifying model in write_out_model",
	              true);
	              
	if (!model || !model->first_element) {
		COMMIT_DEBUG (REPORT_WARNINGS, INTERFACE,
		              "NULL or empty model found in write_out_model", false);
		return false;
	}
	
	*model_string = MALLOC_DEBUG (sizeof (char) * (MAX_MODEL_STRING_LEN + 1),
	                              "string for model in write_out_model");
	                              
	if (!*model_string) {
		COMMIT_DEBUG (REPORT_ERRORS, INTERFACE,
		              "cannot allocate memory for model_string in write_out_model", false);
		return false;
	}
	
	*model_string[0] = '\0';
	*pos_var_string = MALLOC_DEBUG (sizeof (char) * (MAX_MODEL_STRING_LEN + 1),
	                                "string for pos_var in write_out_model");
	                                
	if (!*pos_var_string) {
		FREE_DEBUG (*model_string,
		            "string for model in write_out_model [failed to allocate memory for pos_var_string]");
		COMMIT_DEBUG (REPORT_ERRORS, INTERFACE,
		              "cannot allocate memory for pos_var_string in write_out_model", false);
		return false;
	}
	
	*pos_var_string[0] = '\0';
	write_out_model_part (model->first_constraint, model->first_element,
	                      *model_string, *pos_var_string);
	                      
	if (!strlen (*model_string)) {
		COMMIT_DEBUG (REPORT_ERRORS, INTERFACE,
		              "could not stringify model in write_out_model", false);
		FREE_DEBUG (*model_string,
		            "string for model in write_out_model [failed to stringify model]");
		FREE_DEBUG (*pos_var_string,
		            "string for model in write_out_model [failed to stringify model]");
		return false;
	}
	
	return true;
}

static inline
bool get_model_largest_stack_el_part (nt_element *restrict el,
                                      ntp_stack_size stack_size, ntp_element *restrict largest_stack_el) {
	if (el->type == unpaired) {
		if (el->unpaired->next) {
			return get_model_largest_stack_el_part (el->unpaired->next, stack_size,
			                                        largest_stack_el);
		}
		
		else {
			return true;
		}
	}
	
	else
		if (el->type == paired) {
			if (el->paired->min > *stack_size) {
				*stack_size = el->paired->min;
				*largest_stack_el = el;
			}
			
			#ifndef NO_FULL_CHECKS
			
			if (!el->paired->fp_next) {
				COMMIT_DEBUG (REPORT_WARNINGS, INTERFACE,
				              "NULL fp_element found in get_model_limits_part", false);
			}
			
			else {
			#endif
			
				if (!get_model_largest_stack_el_part (el->paired->fp_next, stack_size,
				                                      largest_stack_el)) {
					return false;
				}
				
				#ifndef NO_FULL_CHECKS
			}
			
				#endif
			
			if (el->paired->tp_next &&
			    !get_model_largest_stack_el_part (el->paired->tp_next, stack_size,
			                                      largest_stack_el)) {
				return false;
			}
			
			return true;
		}
		
		else {
			COMMIT_DEBUG (REPORT_ERRORS, INTERFACE,
			              "element type is neither paired nor unpaired in get_model_largest_stack_el_part",
			              false);
			return false;
		}
}

static inline
bool get_model_largest_stack_el (ntp_model model, ntp_stack_size stack_size,
                                 ntp_element *restrict largest_stack_el) {
	COMMIT_DEBUG (REPORT_INFO, INTERFACE,
	              "finding model limits in get_model_largest_stack_el", true);
	              
	if (!model || !model->first_element) {
		COMMIT_DEBUG (REPORT_WARNINGS, INTERFACE,
		              "NULL or empty model found in get_model_largest_stack_el", false);
		return false;
	}
	
	return get_model_largest_stack_el_part (model->first_element, stack_size,
	                                        largest_stack_el);
}

static inline
bool get_model_limits_part (nt_element *restrict el,
                            nt_element *restrict largest_stack_el,
                            ntp_seg_size fp_lead_min_span, ntp_seg_size fp_lead_max_span,
                            ntp_seg_size tp_trail_min_span, ntp_seg_size tp_trail_max_span,
                            bool *seen_largest_stack_el) {
	if (el->type == unpaired) {
		if (*seen_largest_stack_el) {
			*tp_trail_min_span += el->unpaired->min;
			*tp_trail_max_span += el->unpaired->max;
		}
		
		else {
			*fp_lead_min_span += el->unpaired->min;
			*fp_lead_max_span += el->unpaired->max;
		}
		
		if (el->unpaired->next) {
			return get_model_limits_part (el->unpaired->next,
			                              largest_stack_el,
			                              fp_lead_min_span, fp_lead_max_span,
			                              tp_trail_min_span, tp_trail_max_span,
			                              seen_largest_stack_el);
		}
		
		else {
			return true;
		}
	}
	
	else
		if (el->type == paired) {
			// exclude min/max calculation for fp_element part of el, if it is the largest_stack_el
			if (el != largest_stack_el) {
				#ifndef NO_FULL_CHECKS
			
				if (!el->paired->fp_next) {
					COMMIT_DEBUG (REPORT_WARNINGS, INTERFACE,
					              "NULL fp_element found in get_model_limits_part", false);
				}
				
				else {
				#endif
				
					if (*seen_largest_stack_el) {
						*tp_trail_min_span += el->paired->min;
						*tp_trail_max_span += el->paired->max;
					}
					
					else {
						*fp_lead_min_span += el->paired->min;
						*fp_lead_max_span += el->paired->max;
					}
					
					if (!get_model_limits_part (el->paired->fp_next,
					                            largest_stack_el,
					                            fp_lead_min_span, fp_lead_max_span,
					                            tp_trail_min_span, tp_trail_max_span,
					                            seen_largest_stack_el)) {
						return false;
					}
					
					if (*seen_largest_stack_el) {
						*tp_trail_min_span += el->paired->min;
						*tp_trail_max_span += el->paired->max;
					}
					
					else {
						*fp_lead_min_span += el->paired->min;
						*fp_lead_max_span += el->paired->max;
					}
					
					#ifndef NO_FULL_CHECKS
				}
				
					#endif
			}
			
			else {
				*seen_largest_stack_el = true;
			}
			
			if (el->paired->tp_next) {
				return get_model_limits_part (el->paired->tp_next,
				                              largest_stack_el,
				                              fp_lead_min_span, fp_lead_max_span,
				                              tp_trail_min_span, tp_trail_max_span,
				                              seen_largest_stack_el);
			}
			
			return true;
		}
		
		else {
			COMMIT_DEBUG (REPORT_ERRORS, INTERFACE,
			              "element type is neither paired nor unpaired in get_model_largest_stack_el_part",
			              false);
			return false;
		}
}

bool get_model_limits (ntp_model model,
                       ntp_seg_size fp_lead_min_span, ntp_seg_size fp_lead_max_span,
                       ntp_stack_size stack_min_size, ntp_stack_size stack_max_size,
                       ntp_stack_idist stack_min_idist, ntp_stack_idist stack_max_idist,
                       ntp_seg_size tp_trail_min_span, ntp_seg_size tp_trail_max_span,
                       ntp_element *largest_stack_el) {
	COMMIT_DEBUG (REPORT_INFO, INTERFACE,
	              "finding model limits in get_model_limits", true);
	              
	if (!model || !model->first_element) {
		COMMIT_DEBUG (REPORT_WARNINGS, INTERFACE,
		              "NULL or empty model found in get_model_limits", false);
		return false;
	}
	
	*stack_min_size = 0;
	
	if (get_model_largest_stack_el (model, stack_min_size, largest_stack_el)) {
		if (*largest_stack_el == NULL) {
			COMMIT_DEBUG (REPORT_WARNINGS, INTERFACE,
			              "largest_stack_el is NULL in get_model_limits", false);
			return false;
		}
		
		#ifndef NO_FULL_CHECKS
		
		if ((*largest_stack_el)->paired->fp_next == NULL) {
			COMMIT_DEBUG (REPORT_WARNINGS, INTERFACE,
			              "found a largest_stack_el that has a NULL fp_element in get_model_limits",
			              false);
			return false;
		}
		
		#endif
		bool seen_largest_stack_el = false;
		*fp_lead_min_span = 0;
		*fp_lead_max_span = 0;
		*tp_trail_min_span = 0;
		*tp_trail_max_span = 0;
		
		if (get_model_limits_part (model->first_element,
		                           *largest_stack_el,
		                           fp_lead_min_span, fp_lead_max_span,
		                           tp_trail_min_span, tp_trail_max_span,
		                           &seen_largest_stack_el)
		    &&
		    seen_largest_stack_el) {
			// now to obtain the stack in between (loop) distances, we start traversing the model traversal from largest_stack_el's
			// fp_element, and with seen_largest_stack_el==true -> all min/max values will be placed in tp_trail_*_span (with the
			// latter replaced by stack_*_idist)
			*stack_min_idist = 0;
			*stack_max_idist = 0;
			
			if (!get_model_limits_part ((*largest_stack_el)->paired->fp_next,
			                            NULL,
			                            NULL, NULL,
			                            stack_min_idist, stack_max_idist,
			                            &seen_largest_stack_el)) {
				COMMIT_DEBUG (REPORT_WARNINGS, INTERFACE,
				              "could not get stack_min_idist and stack_max_idist limits in get_model_limits",
				              false);
				return false;
			}
			
			*stack_max_size = (*largest_stack_el)->paired->max;
			return true;
		}
		
		else {
			COMMIT_DEBUG (REPORT_WARNINGS, INTERFACE,
			              "could not get fp_lead and tp_trail limits in get_model_limits", false);
			return false;
		}
	}
	
	else {
		COMMIT_DEBUG (REPORT_WARNINGS, INTERFACE,
		              "cannot find largest_stack_el in get_model_limits", false);
	}
	
	return false;
}

bool compare_CSSD_model_strings (const char *restrict ss,
                                 const char *restrict pos_var, ntp_model model) {
	COMMIT_DEBUG (REPORT_INFO, INTERFACE,
	              "comparing input CSSD to write_out_model in compare_CSSD_model_strings", true);
	ushort cs_string_len;
	
	if (!ss  || ! (cs_string_len = (ushort) strlen (ss))) {
		COMMIT_DEBUG (REPORT_ERRORS, INTERFACE,
		              "input CSSD string is NULL or empty in compare_CSSD_model_strings", false);
		return false;
	}
	
	ushort pos_var_len = 0;
	
	if (pos_var) {
		pos_var_len = (ushort) strlen (pos_var);
	}
	
	char *model_string = NULL, *pos_var_string = NULL;
	
	if (write_out_model (model, &model_string, &pos_var_string)) {
		#ifdef INTERFACE_DETAIL
		char msg[MAX_MODEL_STRING_LEN + 1];
		sprintf (msg, "%s\n", ss);
		COMMIT_DEBUG_NNL (REPORT_INFO, INTERFACE, msg, false);
		sprintf (msg, "%s", pos_var);
		COMMIT_DEBUG_NNL (REPORT_INFO, INTERFACE, msg, false);
		short delta = (short) (strlen (ss) - strlen (pos_var));
		
		if (delta > 0) {
			for (ushort i = 0; i < delta; i++) {
				msg[i] = S_WHITE_SPACE[0];
			}
			
			msg[delta] = '\0';
			COMMIT_DEBUG_NNL (REPORT_INFO, INTERFACE, msg, false);
		}
		
		sprintf (msg, " (input model)\n");
		COMMIT_DEBUG_NNL (REPORT_INFO, INTERFACE, msg, false);
		sprintf (msg, "%s\n", model_string);
		COMMIT_DEBUG_NNL (REPORT_INFO, INTERFACE, msg, false);
		sprintf (msg, "%s", pos_var_string);
		COMMIT_DEBUG_NNL (REPORT_INFO, INTERFACE, msg, false);
		#endif
		ushort pos_var_string_len = 0;
		
		if (pos_var_string) {
			pos_var_string_len = (ushort) strlen (pos_var_string);
		}
		
		bool match = true;
		
		if (cs_string_len != strlen (model_string)) {
			COMMIT_DEBUG (REPORT_WARNINGS, INTERFACE,
			              "input CSSD and write_out_model strings have different lengths in compare_CSSD_model_strings",
			              false);
			match = false;
		}
		
		if (match) {
			// first check if retrieved model_string matches (S_ <-> SS_) input model string
			for (ushort i = 0; i < cs_string_len; i++) {
				if (do_symbols_match (ss[i], model_string[i])) {
					continue;
				}
				
				else {
					match = false;
					break;
				}
			}
			
			if (match) {
				if (pos_var_len) {
					if (!pos_var_string) {
						COMMIT_DEBUG (REPORT_ERRORS, INTERFACE,
						              "unexpected NULL pos_var_string returned from model in compare_CSSD_model_strings",
						              false);
						match = false;
					}
					
					else {
						// match retrieved pos_var's to input pos_var string (in a position independent manner, along the respective model component)
						ushort pos_var_string_posn = 0;
						
						for (ushort i = 0; i < pos_var_len; i++) {
							if (is_valid_pos_var (pos_var[i])) {
								for (ushort j = pos_var_string_posn; j < pos_var_string_len; j++) {
									if (is_valid_pos_var (pos_var_string[j])) {
										if (do_symbols_match (ss[i], model_string[j])) {
											// model symbols at posns i,j match for ss and retrieved model_string. now match actual pos_var symbols
											if (pos_var[i] == pos_var_string[j]) {
												pos_var_string_posn = (ushort) (j + 1);
												break; // success. continue at outer loop
											}
											
											else {
												#ifdef INTERFACE_DETAIL
												sprintf (msg,
												         "pos_vars for input \"%c\" (posn %d) and retrieved \"%c\" (posn %d) models are different",
												         pos_var[i], i + 1, pos_var_string[j], j + 1);
												COMMIT_DEBUG (REPORT_ERRORS, INTERFACE, msg, false);
												#endif
												match = false;
												break;
											}
										}
										
										else {
											#ifdef INTERFACE_DETAIL
											char msg[MAX_ERR_STRING_LEN];
											sprintf (msg,
											         "pos_vars for input \"%c\" (posn %d) and retrieved \"%c\" (posn %d) models are displaced",
											         pos_var[i], i + 1, pos_var_string[j], j + 1);
											COMMIT_DEBUG (REPORT_ERRORS, INTERFACE, msg, false);
											#endif
											match = false;
											break;
										}
									}
								}
								
								if (!match) {
									break;
								}
							}
						}
						
						if (match) {
							for (ushort i = pos_var_len; i < strlen (pos_var_string); i++) {
								if (pos_var_string[i] != S_WHITE_SPACE[0]) {
									COMMIT_DEBUG (REPORT_ERRORS, INTERFACE,
									              "found extraneous symbols in pos_var_string returned from model in compare_CSSD_model_strings",
									              false);
									match = false;
									break;
								}
							}
						}
					}
				}
			}
		}
		
		if (!match) {
			#ifdef INTERFACE_DETAIL
			sprintf (msg, "\t(generated model - does NOT match)\n");
			COMMIT_DEBUG_NNL (REPORT_INFO, INTERFACE, msg, false);
			#endif
			COMMIT_DEBUG (REPORT_WARNINGS, INTERFACE,
			              "input CSSD and write_out_model strings do not match in compare_CSSD_model_strings",
			              false);
		}
		
		else {
			#ifdef INTERFACE_DETAIL
			sprintf (msg, "\t(generated model - matches with input model)\n");
			COMMIT_DEBUG_NNL (REPORT_INFO, INTERFACE, msg, false);
			#endif
			COMMIT_DEBUG (REPORT_INFO, INTERFACE,
			              "input CSSD and write_out_model strings match up in compare_CSSD_model_strings",
			              false);
		}
		
		FREE_DEBUG (model_string, "string for model in compare_CSSD_model_strings");
		FREE_DEBUG (pos_var_string, "string for pos_var in compare_CSSD_model_strings");
		return match;
	}
	
	else {
		COMMIT_DEBUG (REPORT_ERRORS, INTERFACE,
		              "could not write_out_model in compare_CSSD_model_strings", false);
		return false;
	}
}

void join_cssd (const char *ss, const char *pos_var, char **cssd) {
	sprintf (*cssd, "%s\n%s", ss, pos_var);
}

void split_cssd (const char *cssd, char **ss, char **pos_var) {
	const char *ptr = strchr (cssd, '\n');
	const size_t cssd_len = strlen (cssd);
	
	if (ptr) {
		const size_t split_idx = ptr - cssd;
		g_memcpy (*ss, cssd, split_idx);
		(*ss)[split_idx] = '\0';
		g_memcpy (*pos_var, ptr + 1, cssd_len - split_idx);
		(*pos_var)[cssd_len - split_idx] = '\0';
	}
	
	else {
		g_memcpy (*ss, cssd, cssd_len);
		(*ss)[cssd_len] = '\0';
		(*pos_var)[0] = '\0';
	}
}

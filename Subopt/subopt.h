/* subopt.h */
typedef struct {
  float energy;                            /* energy of structure */
  char *structure;
} SOLUTION;

extern  SOLUTION *subopt (char *seq, char *sequence, int delta, FILE *fp);

extern  int sorted;                           /* sort output by energy */

/* End of file */

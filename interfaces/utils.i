/*#######################################*/
/* Utitlities section                    */
/*#######################################*/

/**********************************************/
/* BEGIN interface for generic utilities      */
/**********************************************/

%ignore get_line;
%ignore get_input_line;
%ignore get_ptypes;
%ignore get_indx;
%ignore get_iindx;
%ignore print_tty_input_seq;
%ignore print_tty_input_seq_str;
%ignore warn_user;
%ignore nrerror;
%ignore space;
%ignore xrealloc;
%ignore init_rand;
%ignore urn;
%ignore int_urn;
%ignore filecopy;
%ignore time_stamp;

%rename (init_rand) vrna_init_rand;
%rename (init_rand) vrna_init_rand_seed;
%rename (urn) vrna_urn;
%rename (int_urn) vrna_int_urn;


%include  <ViennaRNA/utils/basic.h>

/**********************************************/
/* BEGIN interface for string utilities       */
/**********************************************/

/* random string */
%ignore random_string;
%rename (random_string) vrna_random_string;
%newobject vrna_random_string;

/* hamming distance */
%rename (hamming_distance) vrna_hamming_distance;
%rename (hamming_distance_bound) vrna_hamming_distance_bound;

%ignore hamming;
%ignore hamming_bound;

%rename (hamming) my_hamming;
%{
  int
  my_hamming(const char *s1,
             const char *s2)
  {
    return vrna_hamming_distance(s1, s2);
  }
%}
int my_hamming(const char *s1, const char *s2);

%rename (hamming_bound) my_hamming_bound;
%{
  int
  my_hamming_bound(const char *s1,
                   const char *s2,
                   int n)
  {
    return vrna_hamming_distance_bound(s1, s2, n);
  }
%}
int my_hamming_bound(const char *s1, const char *s2, int n);

/* RNA -> DNA conversion */
%ignore str_DNA2RNA;

/* string uppercase
 * (there is surely a more efficient version in the target language,
 * so we do not wrap them)
 */
%ignore str_uppercase;

/* encoding / decoding of nucleotide sequences */

%{

#include <cstring>

short *
encode_seq(char *sequence)
{
  unsigned int i,l;
  short *S;
  l = strlen(sequence);
  S = (short *) vrna_alloc(sizeof(short)*(l+2));
  S[0] = (short) l;

  /* make numerical encoding of sequence */
  for (i=1; i<=l; i++)
    S[i]= (short) encode_char(toupper(sequence[i-1]));

  /* for circular folding add first base at position n+1 */
  S[l+1] = S[1];

  return S;
}
%}
short *encode_seq(char *sequence);

%rename (strtrim) my_strtrim;
%cstring_mutable(char *seq_mutable);

%{
  unsigned int
  my_strtrim(char          *seq_mutable,
             const char    *delimiters  = NULL,
             unsigned int  keep         = 0,
             unsigned int  options      = VRNA_TRIM_DEFAULT)
  {
    return  vrna_strtrim(seq_mutable,
                         delimiters,
                         keep,
                         options);
  }
%}

#ifdef SWIGPYTHON
%feature("autodoc")my_strtrim;
%feature("kwargs")my_strtrim;
#endif

unsigned int
my_strtrim(char          *seq_mutable,
           const char    *delimiters  = NULL,
           unsigned int  keep         = 0,
           unsigned int  options      = VRNA_TRIM_DEFAULT);


%constant unsigned int TRIM_LEADING     = VRNA_TRIM_LEADING;
%constant unsigned int TRIM_TRAILING    = VRNA_TRIM_TRAILING;
%constant unsigned int TRIM_IN_BETWEEN  = VRNA_TRIM_IN_BETWEEN;
%constant unsigned int TRIM_DEFAULT     = VRNA_TRIM_DEFAULT;
%constant unsigned int TRIM_ALL         = VRNA_TRIM_ALL;

%include  <ViennaRNA/utils/strings.h>
%include  <ViennaRNA/sequences/utils.h>

/**********************************************/
/* BEGIN interface for structure utilities    */
/**********************************************/

%include structure_utils.i

/**********************************************/
/* BEGIN interface for alignment utilities    */
/**********************************************/

%include aln_utils.i

/**********************************************/
/* BEGIN interface for Move_Set utilities    */
/**********************************************/

%ignore move_gradient;
%ignore move_first;
%ignore move_adaptive;
%ignore browse_neighs_pt;
%ignore browse_neighs;
%ignore print_stren;
%ignore print_str;
%ignore copy_arr;
%ignore allocopy;


%rename (move_standard) my_move_standard;

%{
  char *
  my_move_standard(int            *OUTPUT,
                   char           *seq,
                   char           *struc,
                   enum MOVE_TYPE type,
                   int            verbosity_level,
                   int            shifts,
                   int            noLP)
  {
    char *structure =  (char *)calloc(strlen(struc)+1,sizeof(char));
    strcpy(structure,struc);
    *OUTPUT = move_standard(seq,structure,type,verbosity_level,shifts,noLP);
    return structure;   
  }
%}
#ifdef SWIGPYTHON
%feature("autodoc")my_move_standard ;
%feature("kwargs") my_move_standard;
#endif

%newobject my_move_standard;
char *my_move_standard(int *OUTPUT, char *seq, char *struc, enum MOVE_TYPE type,int verbosity_level, int shifts, int noLP);
%ignore move_standard;


%include  <ViennaRNA/move_set.h>


/**********************************************/
/* BEGIN interface for File utilities         */
/**********************************************/

%rename (filename_sanitize) my_filename_sanitize;

%{
  std::string
  my_filename_sanitize(std::string name)
  {
    std::string s;
    char *name_sanitized = vrna_filename_sanitize(name.c_str(), NULL);
    if (name_sanitized)
      s = (const char *)name_sanitized;
    free(name_sanitized);
    return s;
  }

  std::string
  my_filename_sanitize(std::string  name,
                       char         c)
  {
    std::string s;
    char *name_sanitized = vrna_filename_sanitize(name.c_str(), &c);
    if (name_sanitized)
      s = (const char *)name_sanitized;
    free(name_sanitized);
    return s;
  }
%}

std::string my_filename_sanitize(std::string name);
std::string my_filename_sanitize(std::string name, char c);

%include  <ViennaRNA/io/utils.h>

/**********************************************/
/* BEGIN interface for logging utilities      */
/**********************************************/

/* allow for passing unsigned int instead of vrna_log_levels_e to the below functions */
%apply int { vrna_log_levels_e };

#ifdef SWIGPYTHON
%include callbacks-log.i
#endif

/* rename the logging system API */
%rename (log_level)       vrna_log_level;
%rename (log_level_set)   vrna_log_level_set;
%rename (log_options)     vrna_log_options;
%rename (log_options_set) vrna_log_options_set;
%rename (log_cb_num)      vrna_log_cb_num;
%rename (log_reset)       vrna_log_reset;
%rename (log_fp)          vrna_log_fp;
%rename (log_fp_set)      vrna_log_fp_set;


/* add logging-related constants */

%constant int  LOG_LEVEL_UNKNOWN  = VRNA_LOG_LEVEL_UNKNOWN;   /**< Unknown log level */
%constant int  LOG_LEVEL_DEBUG    = VRNA_LOG_LEVEL_DEBUG;     /**< Debug log level */
%constant int  LOG_LEVEL_INFO     = VRNA_LOG_LEVEL_INFO;      /**< Info log level */
%constant int  LOG_LEVEL_WARNING  = VRNA_LOG_LEVEL_WARNING;   /**< Warning log level */
%constant int  LOG_LEVEL_ERROR    = VRNA_LOG_LEVEL_ERROR;     /**< Error log level */
%constant int  LOG_LEVEL_CRITICAL = VRNA_LOG_LEVEL_CRITICAL;  /**< Critical log level */
%constant int  LOG_LEVEL_SILENT   = VRNA_LOG_LEVEL_SILENT;    /**< Silent log level */
%constant int  LOG_LEVEL_DEFAULT  = VRNA_LOG_LEVEL_DEFAULT;

%constant unsigned int LOG_OPTION_QUIET = VRNA_LOG_OPTION_QUIET;
%constant unsigned int LOG_OPTION_TRACE_CALL = VRNA_LOG_OPTION_TRACE_CALL;
%constant unsigned int LOG_OPTION_TRACE_TIME = VRNA_LOG_OPTION_TRACE_TIME;
%constant unsigned int LOG_OPTION_DEFAULT = VRNA_LOG_OPTION_DEFAULT;


%include  <ViennaRNA/utils/log.h>




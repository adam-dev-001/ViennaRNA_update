/*
 *                c Ronny Lorenz, ViennaRNA Package
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>
#include <ctype.h>
#include <unistd.h>
#include <string.h>

#include "ViennaRNA/plotting/probabilities.h"
#include "ViennaRNA/plotting/structures.h"
#include "ViennaRNA/fold_vars.h"
#include "ViennaRNA/params/basic.h"
#include "ViennaRNA/io/commands.h"
#include "ViennaRNA/constraints/basic.h"
#include "ViennaRNA/constraints/hard.h"
#include "ViennaRNA/constraints/soft.h"
#include "ViennaRNA/probing/SHAPE.h"
#include "ViennaRNA/io/file_formats.h"
#include "ViennaRNA/io/utils.h"
#include "ViennaRNA/cofold.h"
#include "ViennaRNA/mfe/global.h"
#include "ViennaRNA/part_func_co.h"
#include "ViennaRNA/partfunc/global.h"
#include "ViennaRNA/structures/centroid.h"
#include "ViennaRNA/structures/mea.h"
#include "ViennaRNA/eval/structures.h"
#include "ViennaRNA/utils/basic.h"
#include "ViennaRNA/utils/strings.h"
#include "ViennaRNA/structures/pairtable.h"
#include "ViennaRNA/utils/log.h"
#include "ViennaRNA/params/io.h"
#include "ViennaRNA/datastructures/char_stream.h"
#include "ViennaRNA/datastructures/stream_output.h"
#include "ViennaRNA/datastructures/string.h"
#include "ViennaRNA/concentrations.h"
#include "ViennaRNA/combinatorics/basic.h"
#include "ViennaRNA/wrap_dlib.h"

#include "RNAmultifold_cmdl.h"
#include "gengetopt_helpers.h"
#include "input_id_helpers.h"
#include "ViennaRNA/intern/color_output.h"
#include "parallel_helpers.h"


/* Extend the options structure with a new parameter for maximum interacting strands */
struct options {
  int             filename_full;
  char            *filename_delim;
  int             pf;
  int             doT;
  int             doC;
  int             noPS;
  int             noconv;
  int             centroid;
  int             MEA;
  double          MEAgamma;
  double          bppmThreshold;
  int             verbose;
  vrna_md_t       md;
  vrna_cmd_t      cmds;

  dataset_id      id_control;

  char            *concentration_file;
  int             concentration_absolute;

  char            *constraint_file;
  int             constraint_batch;
  int             constraint_enforce;
  int             constraint_canonical;

  int             shape;
  char            *shape_file;
  char            *shape_method;
  char            *shape_conversion;

  int             jobs;
  int             keep_order;
  unsigned int    next_record_number;
  vrna_ostream_t  output_queue;

  int             max_interacting_strands; /* New: if > 0, use this value instead of vc->strands */
};


struct record_data {
  unsigned int    number;
  char            *id;
  char            *sequence;
  char            *SEQ_ID;
  char            **rest;
  char            *input_filename;
  int             multiline_input;
  struct options  *options;
  int             tty;
};


struct output_stream {
  vrna_cstr_t data;
  vrna_cstr_t err;
};


static void
process_record(struct record_data *record);


PRIVATE double *
read_concentrations(FILE    *fp,
                    size_t  num_strands);


static int
process_input(FILE            *input_stream,
              const char      *input_filename,
              struct options  *opt);


void
postscript_layout(vrna_fold_compound_t  *fc,
                  const char            *orig_sequence,
                  const char            *structure,
                  const char            *SEQ_ID,
                  struct options        *opt);


static char *
get_filename(const char     *id,
             const char     *suffix,
             const char     *filename_default,
             struct options *opt);


static void
compute_centroid(vrna_fold_compound_t *fc,
                 vrna_cstr_t          rec_output);


static void
compute_MEA(vrna_fold_compound_t  *fc,
            double                MEAgamma,
            vrna_cstr_t           rec_output);


/*--------------------------------------------------------------------------*/

void
init_default_options(struct options *opt)
{
  opt->filename_full  = 0;
  opt->filename_delim = NULL;
  opt->pf             = 0;
  opt->doT            = 0;  /* compute dimer free energies etc. */
  opt->doC            = 0;  /* compute concentrations */
  opt->noPS           = 0;
  opt->noconv         = 0;
  opt->centroid       = 0;  /* off by default due to historical reasons */
  opt->MEA            = 0;
  opt->MEAgamma       = 1.;
  opt->bppmThreshold  = 1e-5;
  opt->verbose        = 0;
  opt->cmds           = NULL;
  opt->id_control     = NULL;
  set_model_details(&(opt->md));

  opt->doC                    = 0; /* toggle to compute concentrations */
  opt->concentration_file     = NULL;
  opt->concentration_absolute = 0;

  opt->constraint_file      = NULL;
  opt->constraint_batch     = 0;
  opt->constraint_enforce   = 0;
  opt->constraint_canonical = 0;

  opt->shape            = 0;
  opt->shape_file       = NULL;
  opt->shape_method     = NULL;
  opt->shape_conversion = NULL;

  opt->jobs               = 1;
  opt->keep_order         = 1;
  opt->next_record_number = 0;
  opt->output_queue       = NULL;

  opt->max_interacting_strands = 0; /* Default: use all available strands */
}


static char **
collect_unnamed_options(struct RNAmultifold_args_info *ggostruct,
                        int                           *num_files)
{
  char  **input_files = NULL;
  int   i;

  *num_files = 0;

  /* collect all unnamed options */
  if (ggostruct->inputs_num > 0) {
    input_files = (char **)vrna_realloc(input_files, sizeof(char *) * ggostruct->inputs_num);
    for (i = 0; i < ggostruct->inputs_num; i++)
      input_files[(*num_files)++] = strdup(ggostruct->inputs[i]);
  }

  return input_files;
}


void
flush_cstr_callback(void          *auxdata,
                    unsigned int  i,
                    void          *data)
{
  struct output_stream *s = (struct output_stream *)data;

  /* flush/free errors first */
  vrna_cstr_free(s->err);

  /* flush/free data[k] */
  vrna_cstr_free(s->data);

  free(s);
}


int
main(int  argc,
     char *argv[])
{
  struct  RNAmultifold_args_info  args_info;
  char                            **input_files;
  int                             num_input;
  struct  options                 opt;

  num_input = 0;

  init_default_options(&opt);

  /*
   #############################################
   # check the command line parameters
   #############################################
   */
  if (RNAmultifold_cmdline_parser(argc, argv, &args_info) != 0)
    exit(1);

  /* New: process max interacting strands parameter (assume gengetopt creates max_strands_given and max_strands_arg) */
  if (args_info.max_strands_given)
    opt.max_interacting_strands = args_info.max_strands_arg;

  /* prepare logging system and verbose mode */
  ggo_log_settings(args_info, opt.verbose);

  /* get basic set of model details */
  ggo_get_md_eval(args_info, opt.md);
  ggo_get_md_fold(args_info, opt.md);
  ggo_get_md_part(args_info, opt.md);

  /* temperature */
  ggo_get_temperature(args_info, opt.md.temperature);

  /* check dangle model */
  if ((opt.md.dangles < 0) || (opt.md.dangles > 3)) {
    vrna_log_warning("required dangle model not implemented, falling back to default dangles=2");
    opt.md.dangles = dangles = 2;
  }

  ggo_get_id_control(args_info, opt.id_control, "Sequence", "sequence", "_", 4, 1);

  /* do not convert DNA nucleotide "T" to appropriate RNA "U" */
  if (args_info.noconv_given)
    opt.noconv = 1;

  /* set the bppm threshold for the dotplot */
  if (args_info.bppmThreshold_given)
    opt.bppmThreshold = MIN2(1., MAX2(0., args_info.bppmThreshold_arg));

  /* partition function settings */
  if (args_info.partfunc_given) {
    opt.pf = 1;
    if (args_info.partfunc_arg != -1)
      opt.md.compute_bpp = args_info.partfunc_arg;
  }

  if (args_info.all_pf_given) {
    opt.doT = opt.pf = 1;
    if (args_info.all_pf_arg != 1)
      opt.md.compute_bpp = args_info.all_pf_arg;
    else
      opt.md.compute_bpp = 1;
  }

  if (args_info.concentrations_given) {
    opt.doC = opt.doT = opt.pf = 1;
    if (args_info.absolute_concentrations_given)
      opt.concentration_absolute = 1;
  }

  /* concentrations in file */
  if (args_info.concfile_given) {
    opt.concentration_file  = strdup(args_info.concfile_arg);
    opt.doC                 = opt.doT = opt.pf = 1;
    if (args_info.absolute_concentrations_given)
      opt.concentration_absolute = 1;
  }

  if (args_info.commands_given)
    opt.cmds = vrna_file_commands_read(args_info.commands_arg, VRNA_CMD_PARSE_DEFAULTS);

  /* filename sanitize delimiter */
  if (args_info.filename_delim_given)
    opt.filename_delim = strdup(args_info.filename_delim_arg);
  else if (get_id_delim(opt.id_control))
    opt.filename_delim = strdup(get_id_delim(opt.id_control));

  if ((opt.filename_delim) && isspace(*(opt.filename_delim))) {
    free(opt.filename_delim);
    opt.filename_delim = NULL;
  }

  /* full filename from FASTA header support */
  if (args_info.filename_full_given)
    opt.filename_full = 1;

  if (args_info.jobs_given) {
#if VRNA_WITH_PTHREADS
    int thread_max = max_user_threads();
    if (args_info.jobs_arg == 0) {
      /* use maximum of concurrent threads */
      int proc_cores, proc_cores_conf;
      if (num_proc_cores(&proc_cores, &proc_cores_conf)) {
        opt.jobs = MIN2(thread_max, proc_cores_conf);
      } else {
        vrna_log_warning("Could not determine number of available processor cores!\n"
                             "Defaulting to serial computation");
        opt.jobs = 1;
      }
    } else {
      opt.jobs = MIN2(thread_max, args_info.jobs_arg);
    }

    opt.jobs = MAX2(1, opt.jobs);
#else
    vrna_log_warning(
      "This version of RNAfold has been built without parallel input processing capabilities");
#endif

    if (args_info.unordered_given)
      opt.keep_order = 0;
  }

  ggo_geometry_settings(args_info, &(opt.md));

  input_files = collect_unnamed_options(&args_info, &num_input);

  /* free allocated memory of command line data structure */
  RNAmultifold_cmdline_parser_free(&args_info);

  /*
   #############################################
   # begin initializing
   #############################################
   */
  if (opt.pf && opt.md.gquad) {
    vrna_log_error(
      "G-Quadruplex support is currently not available for partition function computations");
    exit(EXIT_FAILURE);
  }
  if ((opt.verbose) && (opt.jobs > 1))
    vrna_log_info("Preparing %d parallel computation slots", opt.jobs);

  if (opt.keep_order)
    opt.output_queue = vrna_ostream_init(&flush_cstr_callback, NULL);

  /*
   ################################################
   # process input files or handle input from stdin
   ################################################
   */
  INIT_PARALLELIZATION(opt.jobs);

  if (num_input > 0) {
    int i, skip;
    for (skip = i = 0; i < num_input; i++) {
      if (!skip) {
        FILE *input_stream = fopen((const char *)input_files[i], "r");

        if (!input_stream) {
          vrna_log_error("Unable to open %d. input file \"%s\" for reading", i + 1,
                             input_files[i]);
          exit(EXIT_FAILURE);
        }

        if (opt.verbose) {
          vrna_log_info("Processing %d. input file \"%s\"",
                            i + 1,
                            input_files[i]);
        }

        if (process_input(input_stream, (const char *)input_files[i], &opt) == 0)
          skip = 1;

        fclose(input_stream);
      }

      free(input_files[i]);
    }
  } else {
    (void)process_input(stdin, NULL, &opt);
  }

  UNINIT_PARALLELIZATION
  /*
   ################################################
   # post processing
   ################################################
   */
  vrna_ostream_free(opt.output_queue);


  free(input_files);
  free(opt.constraint_file);
  free(opt.shape_file);
  free(opt.shape_method);
  free(opt.shape_conversion);
  free(opt.filename_delim);
  vrna_commands_free(opt.cmds);
  free(opt.concentration_file);

  free_id_data(opt.id_control);

  if (vrna_log_fp() != stderr)
    fclose(vrna_log_fp());

  return EXIT_SUCCESS;
}


static int
process_input(FILE            *input_stream,
              const char      *input_filename,
              struct options  *opt)
{
  unsigned int  read_opt;
  int           istty, istty_in, istty_out, ret;

  ret       = 1;
  read_opt  = 0;

  istty_in  = isatty(fileno(input_stream));
  istty_out = isatty(fileno(stdout));
  istty     = istty_in && istty_out;

  /* print user help if we get input from tty */
  if (istty) {
    if (fold_constrained) {
      vrna_message_constraint_options(
        VRNA_CONSTRAINT_DB_DOT | VRNA_CONSTRAINT_DB_X | VRNA_CONSTRAINT_DB_ANG_BRACK |
        VRNA_CONSTRAINT_DB_RND_BRACK);
      vrna_message_input_seq("Input sequence (upper or lower case) followed by structure constraint\n"
                             "Use '&' to connect sequences that shall form a complex.");
    } else {
      vrna_message_input_seq("Use '&' to connect sequences that shall form a complex.");
    }
  }

  /* set options we wanna pass to vrna_file_fasta_read_record() */
  if (istty)
    read_opt |= VRNA_INPUT_NOSKIP_BLANK_LINES;

  if (!fold_constrained)
    read_opt |= VRNA_INPUT_NO_REST;

  /*
   #############################################
   # main loop: continue until end of file
   #############################################
   */
  do {
    char          *rec_sequence, *rec_id, **rec_rest;
    unsigned int  rec_type;
    int           maybe_multiline;

    rec_id          = NULL;
    rec_rest        = NULL;
    maybe_multiline = 0;

    rec_type = vrna_file_fasta_read_record(&rec_id,
                                           &rec_sequence,
                                           &rec_rest,
                                           input_stream,
                                           read_opt);

    if (rec_type & (VRNA_INPUT_ERROR | VRNA_INPUT_QUIT))
      break;

    /*
     ########################################################
     # init everything according to the data we've read
     ########################################################
     */
    if (rec_id) {
      maybe_multiline = 1;
      /* remove '>' from FASTA header */
      rec_id = memmove(rec_id, rec_id + 1, strlen(rec_id));
    }

    /* construct the sequence ID */
    set_next_id(&rec_id, opt->id_control);

    struct record_data *record = (struct record_data *)vrna_alloc(sizeof(struct record_data));

    record->number          = opt->next_record_number;
    record->sequence        = rec_sequence;
    record->SEQ_ID          = fileprefix_from_id(rec_id, opt->id_control, opt->filename_full);
    record->id              = rec_id;
    record->rest            = rec_rest;
    record->multiline_input = maybe_multiline;
    record->options         = opt;
    record->tty             = istty;
    record->input_filename  = (input_filename) ? strdup(input_filename) : NULL;

    if (opt->output_queue)
      vrna_ostream_request(opt->output_queue, opt->next_record_number++);

    RUN_IN_PARALLEL(process_record, record);

    if (opt->shape || (opt->constraint_file && (!opt->constraint_batch))) {
      ret = 0;
      break;
    }

    /* print user help for the next round if we get input from tty */
    if (istty) {
      printf("Use '&' to connect sequences that shall form a complex.\n");
      if (fold_constrained) {
        vrna_message_constraint_options(
          VRNA_CONSTRAINT_DB_DOT | VRNA_CONSTRAINT_DB_X | VRNA_CONSTRAINT_DB_ANG_BRACK |
          VRNA_CONSTRAINT_DB_RND_BRACK);
        vrna_message_input_seq(
          "Input sequence (upper or lower case) followed by structure constraint\n");
      } else {
        vrna_message_input_seq_simple();
      }
    }
  } while (1);

  return ret;
}


static void
process_record(struct record_data *record)
{
  char                  *mfe_structure, *sequence, **rec_rest;
  unsigned int          n, i;
  double                min_en, kT, *concentrations;
  vrna_ep_t             *prAB, *prAA, *prBB, *prA, *prB, *mfAB, *mfAA, *mfBB, *mfA, *mfB;
  struct options        *opt;
  struct output_stream  *o_stream;

  mfAB            = mfAA = mfBB = mfA = mfB = NULL;
  prAB            = prAA = prBB = prA = prB = NULL;
  concentrations  = NULL;
  opt             = record->options;
  o_stream        = (struct output_stream *)vrna_alloc(sizeof(struct output_stream));
  sequence        = strdup(record->sequence);
  rec_rest        = record->rest;

  /* convert DNA alphabet to RNA if not explicitely switched off */
  if (!opt->noconv) {
    vrna_seq_toRNA(sequence);
    vrna_seq_toRNA(record->sequence);
  }

  /* convert sequence to uppercase letters only */
  vrna_seq_toupper(sequence);

  vrna_fold_compound_t *vc = vrna_fold_compound(sequence,
                                                &(opt->md),
                                                VRNA_OPTION_DEFAULT);

  n = vc->length;

  /* Determine effective number of strands to use */
  int effective_strands;
  if (opt->max_interacting_strands > 0) {
    if (opt->max_interacting_strands > vc->strands) {
      vrna_log_warning("Specified max interacting strands (%d) exceeds available strands (%d). Using %d.",
                       opt->max_interacting_strands, vc->strands, vc->strands);
      effective_strands = vc->strands;
    } else {
      effective_strands = opt->max_interacting_strands;
    }
  } else {
    effective_strands = vc->strands;
  }
  size_t eff_strands = (size_t) effective_strands;

  /* retrieve string stream bound to stdout, 6*length should be enough memory to start with */
  o_stream->data = vrna_cstr(6 * n, stdout);
  /* retrieve string stream bound to stderr for any info messages */
  o_stream->err = vrna_cstr(n, stderr);

  if (record->tty) {
    char          *tmp;
    vrna_string_t lstring;

    lstring = vrna_string_make("length(s) = {");
    tmp     = vrna_strdup_printf(" %u", 1 + vc->strand_end[0] - vc->strand_start[0]);

    lstring = vrna_string_append_cstring(lstring, tmp);
    free(tmp);

    for (unsigned int i = 1; i < vc->strands; i++) {
      char *tmp = vrna_strdup_printf(", %u", 1 + vc->strand_end[i] - vc->strand_start[i]);
      lstring = vrna_string_append_cstring(lstring, tmp);
      free(tmp);
    }

    tmp     = vrna_strdup_printf(" }, total = %u, sequences = %u\n", vc->length, vc->strands);
    lstring = vrna_string_append_cstring(lstring, tmp);
    free(tmp);

    printf("%s", lstring);
    vrna_string_free(lstring);
  }

  if (vc->strands > 1) {
    unsigned int r = vrna_rotational_symmetry(vc->sequence);
    if (r > 1) {
      vrna_cstr_message_warning(o_stream->err,
                                "The input sequences show a rotational symmetry of %u! "
                                "Symmetry correction might be required to compute actual MFE!",
                                r);
    }
  }

  mfe_structure = (char *)vrna_alloc(sizeof(char) * (n + 1));

  /* parse the rest of the current dataset to obtain a structure constraint */
  if (fold_constrained) {
    if (opt->constraint_file) {
      vrna_constraints_add(vc, opt->constraint_file, VRNA_OPTION_DEFAULT | VRNA_OPTION_HYBRID);
    } else {
      char          *cstruc   = NULL;
      unsigned int  cl        = 0;
      unsigned int  coptions  = (record->multiline_input) ? VRNA_OPTION_MULTILINE : 0;
      int           cp        = -1;

      cstruc  = vrna_extract_record_rest_structure((const char **)rec_rest, 0, coptions);

      char **structures = vrna_strsplit(cstruc, "&");
      for (unsigned int a = 0; a < vc->strands; a++) {
        if (!structures[a]) {
          vrna_log_error("Sequence and Structure have different number of strand delimiters");
          exit(EXIT_FAILURE);
        }

        unsigned int l = strlen(structures[a]);
        cl += l;

        switch (vc->type) {
          case VRNA_FC_TYPE_SINGLE:
            if (vc->nucleotides[a].length != l) {
              vrna_log_error(
                "Structure and sequence part of strand %u differ in length (%u vs. %u)",
                a,
                l,
                vc->nucleotides[a].length);
              exit(EXIT_FAILURE);
            }

            break;

          case VRNA_FC_TYPE_COMPARATIVE:
            if (vc->alignment[a].sequences[0].length != l) {
              vrna_log_error(
                "Structure and sequence part of strand %u differ in length (%u vs. %u)",
                a,
                l,
                vc->alignment[a].sequences[0].length);
              exit(EXIT_FAILURE);
            }
            break;
        }
      }

      for (unsigned int a = 0; a < vc->strands; a++)
        free(structures[a]);

      free(structures);

      if (cl == 0)
        vrna_log_warning("Structure constraint is missing");
      else if (cl < n)
        vrna_log_warning("Structure constraint is shorter than sequence(s)");
      else if (cl > n) {
        vrna_log_error("Structure constraint is too long");
        exit(EXIT_FAILURE);
      }

      if (cstruc) {
        unsigned int constraint_options = VRNA_CONSTRAINT_DB_DEFAULT;

        if (opt->constraint_enforce)
          constraint_options |= VRNA_CONSTRAINT_DB_ENFORCE_BP;

        if (opt->constraint_canonical)
          constraint_options |= VRNA_CONSTRAINT_DB_CANONICAL_BP;

        vrna_constraints_add(vc, (const char *)cstruc, constraint_options);
      }

      free(cstruc);
    }
  }

  if (opt->shape) {
    vrna_constraints_add_SHAPE(vc,
                               opt->shape_file,
                               opt->shape_method,
                               opt->shape_conversion,
                               opt->verbose,
                               VRNA_OPTION_DEFAULT | VRNA_OPTION_HYBRID);
  }

  if (opt->cmds)
    vrna_commands_apply(vc, opt->cmds, VRNA_CMD_PARSE_HC | VRNA_CMD_PARSE_SC);

  if (opt->doC) {
    if (opt->concentration_file) {
      /* read from file */
      FILE *fp = fopen(opt->concentration_file, "r");
      if (fp == NULL) {
        vrna_log_error("could not open concentration file %s", opt->concentration_file);
        exit(EXIT_FAILURE);
      }

      concentrations = read_concentrations(fp, eff_strands);
      fclose(fp);
    } else {
      printf("Please enter concentrations [mol/l]\n format: ConcA ConcB\n return to end\n");
      concentrations = read_concentrations(stdin, eff_strands);
    }
  }

  /*
   ########################################################
   # begin actual computations
   ########################################################
   */

  /* compute mfe of AB dimer */
  for (i = 0; i < n; i++)
    mfe_structure[i] = '.';

  min_en  = vrna_mfe(vc, mfe_structure);
  mfAB    = vrna_plist(mfe_structure, 0.95);

  /* check whether the constraint allows for any solution */
  if ((fold_constrained) || (opt->cmds)) {
    if (min_en == (double)(INF / 100.)) {
      vrna_log_error(
        "Supplied structure constraints create empty solution set for sequence:\n%s",
        record->sequence);
      exit(EXIT_FAILURE);
    }
  }

  {
    char  *pstruct    = NULL;
    char  *tmp_struct = strdup(mfe_structure);

    if (vc->strands == 1) {
      pstruct = tmp_struct;
    } else {
      for (unsigned int i = 1; i < vc->strands; i++) {
        pstruct = vrna_cut_point_insert(tmp_struct, (int)vc->strand_start[i] + (i - 1));
        free(tmp_struct);
        tmp_struct = pstruct;
      }
    }

    vrna_cstr_print_fasta_header(o_stream->data, record->id);
    vrna_cstr_printf(o_stream->data, "%s\n", record->sequence);

    vrna_cstr_printf_structure(o_stream->data,
                               pstruct,
                               record->tty ?  "\n minimum free energy = %6.2f kcal/mol" : " (%6.2f)",
                               min_en);

    free(pstruct);
  }

  if (n > 2000)
    vrna_mx_mfe_free(vc);

  /* compute partition function */
  if (opt->pf) {
    char *pairing_propensity;

    prAB                = NULL;
    pairing_propensity  = (char *)vrna_alloc(sizeof(char) * (n + 1));

    if (opt->md.dangles == 1) {
      vc->params->model_details.dangles = 2;   /* recompute with dangles as in pf_fold() */
      min_en                            = vrna_eval_structure(vc, mfe_structure);
      vc->params->model_details.dangles = 1;
    }

    vrna_exp_params_rescale(vc, &min_en);
    kT = vc->exp_params->kT / 1000.;

    if (n > 2000)
      vrna_cstr_message_info(o_stream->err,
                             "scaling factor %f",
                             vc->exp_params->pf_scale);

    /* compute partition function */
    FLT_OR_DBL dG;
    dG = vrna_pf(vc, pairing_propensity);

    if (opt->md.compute_bpp) {
      char *filename_dot, *comment, *costruc = NULL;
      prAB = vrna_plist_from_probs(vc, opt->bppmThreshold);

      filename_dot = get_filename(record->SEQ_ID, "dp.ps", "dot.ps", opt);

      /*AB dot_plot*/
      comment = vrna_strdup_printf("Heterodimer AB FreeEnergy= %.9f", dG);
      THREADSAFE_FILE_OUTPUT(
        (void)vrna_plot_dp_PS_list(record->sequence,
                                   vc->cutpoint,
                                   filename_dot,
                                   prAB,
                                   mfAB,
                                   comment));

      free(comment);
      free(filename_dot);

      char *tmp_struct = strdup(pairing_propensity);

      if (vc->strands == 1) {
        costruc = tmp_struct;
      } else {
        for (unsigned int i = 1; i < vc->strands; i++) {
          costruc = vrna_cut_point_insert(tmp_struct, (int)vc->strand_start[i] + (i - 1));
          free(tmp_struct);
          tmp_struct = costruc;
        }
      }

      vrna_cstr_printf_structure(o_stream->data,
                                 costruc,
                                 record->tty ? "\n free energy of connected ensemble = %6.2f kcal/mol" : " [%6.2f]",
                                 dG);

      free(costruc);

      /* compute MEA structure */
      if (opt->centroid)
        compute_centroid(vc,
                         o_stream->data);

      /* compute MEA structure */
      if (opt->MEA) {
        compute_MEA(vc,
                    opt->MEAgamma,
                    o_stream->data);
      }
    } else {
      vrna_cstr_printf_structure(o_stream->data,
                                 NULL,
                                 " free energy of connected ensemble = %g kcal/mol",
                                 dG);
    }

    if (opt->doT) {
      /* generate all complexes using the effective number of strands */
      size_t        max_interacting_strands = eff_strands;

      unsigned int  ***complexes = (unsigned int ***)vrna_alloc(
        sizeof(unsigned int **) * max_interacting_strands);
      double        **dG_complexes = (double **)vrna_alloc(
        sizeof(double *) * max_interacting_strands);

      complexes     -= 1;
      dG_complexes  -= 1;

      for (size_t k = 1; k <= max_interacting_strands; k++) {
        size_t num_complexes = 0;

        /* enumerate all complexes of current size */
        complexes[k] = vrna_n_multichoose_k(eff_strands, k);

        /* count number of complexes of current size */
        for (; complexes[k][num_complexes] != NULL; num_complexes++);

        dG_complexes[k] = (double *)vrna_alloc(sizeof(double) * num_complexes);

        char *verbose_line = NULL;

        if (opt->verbose)
          fprintf(stderr, "Processing complexes of size %lu\n", k);

        for (size_t c_cnt = 0; c_cnt < num_complexes; c_cnt++) {
          if (opt->verbose) {
            vrna_strcat_printf(&verbose_line,
                               "Complex %u/%lu (size %lu) ",
                               c_cnt + 1,
                               num_complexes, k);
          }

          /* Now, enumerate all non-cyclic permutations for current complex */

          /* first, compose a list of species counts */
          unsigned int  *species = (unsigned int *)vrna_alloc(sizeof(unsigned int) * (k + 1));
          unsigned int  *species_count = (unsigned int *)vrna_alloc(sizeof(unsigned int) * eff_strands);
          unsigned int  *mapping = (unsigned int *)vrna_alloc(sizeof(unsigned int) * eff_strands);
          size_t        known_species = 0;
          for (size_t kk = 0; kk < k; kk++)
            species_count[complexes[k][c_cnt][kk]]++;

          for (size_t kk = 0; kk < eff_strands; kk++) {
            if (species_count[kk] > 0) {
              mapping[known_species]  = kk;
              species[known_species]  = species_count[kk];

              if (opt->verbose) {
                vrna_strcat_printf(&verbose_line,
                                   ", %u x %c",
                                   species_count[kk],
                                   kk < 26 ? kk + 'A' : kk + 'a');
              }

              known_species++;
            }
          }
          species[known_species] = 0;

          /* enumerate all non-cyclic permutations of current complex */
          unsigned int  **permutations = vrna_enumerate_necklaces(species);

          double        dG_current  = 0.;
          size_t        num_perm    = 0;

          if (opt->verbose)
            for (; permutations[num_perm]; num_perm++);

          for (size_t i = 0; permutations[i]; i++) {
            char *current_sequence = NULL;

            if (opt->verbose) {
              fprintf(stderr, "\r%s, permutation %lu/%lu                    ",
                      verbose_line,
                      i + 1, num_perm);
              fflush(stderr);
            }

            vrna_strcat_printf(&current_sequence,
                               "%s",
                               vc->nucleotides[mapping[permutations[i][1]]].string);

            for (size_t j = 2; j <= k; j++) {
              vrna_strcat_printf(&current_sequence,
                                 "&%s",
                                 vc->nucleotides[mapping[permutations[i][j]]].string);
            }

            /* for now, do not compute base pair probs */
            int bpp_comp = opt->md.compute_bpp;
            opt->md.compute_bpp = 0;

            /* compute MFE and PF for current permutation */
            vrna_fold_compound_t  *fc_current = vrna_fold_compound(current_sequence,
                                                                   &(opt->md),
                                                                   VRNA_OPTION_DEFAULT);

            double                mfe_current = vrna_mfe(fc_current, NULL);

            vrna_exp_params_rescale(fc_current, &mfe_current);

            double                F = vrna_pf(fc_current, NULL);

            /* store, or add up contribution from current permutation */
            dG_current = (i == 0) ? F : vrna_pf_add(dG_current, F, kT);

            free(current_sequence);
            vrna_fold_compound_free(fc_current);

            /* restore original base pair probs behavior */
            opt->md.compute_bpp = bpp_comp;

            free(permutations[i]);
          }

          free(species);
          free(species_count);
          free(mapping);
          free(permutations);

          dG_complexes[k][c_cnt] = dG_current;
          if (opt->verbose) {
            free(verbose_line);
            verbose_line = NULL;
          }
        }
        if (opt->verbose)
          fprintf(stderr, "\n");
      }

      vrna_cstr_printf_comment(o_stream->data, "Free Energies:");

      /* construct ASCII complex strings in reverse size order, i.e. largest complexes first */
      char  *monomer_string       = NULL;
      char  *complex_string       = NULL;
      char  *dG_string            = NULL;
      char  *dG_string_monomers   = NULL;
      char  *curr_complex_string  =
        (char *)vrna_alloc(sizeof(char) * (max_interacting_strands + 1));
      for (size_t i = max_interacting_strands; i > 1; i--) {
        for (size_t j = 0; complexes[i][j] != NULL; j++) {
          for (size_t k = 0; k < i; k++) {
            curr_complex_string[k]  = complexes[i][j][k];
            curr_complex_string[k]  += (curr_complex_string[k] > 25) ? 'a' : 'A';
          }
          curr_complex_string[i] = '\0';
          if ((i == max_interacting_strands) && (j == 0)) {
            vrna_strcat_printf(&complex_string, "%s", curr_complex_string);
            vrna_strcat_printf(&dG_string, "%6f", dG_complexes[i][j]);
          } else {
            vrna_strcat_printf(&complex_string, "\t\t%s", curr_complex_string);
            vrna_strcat_printf(&dG_string, "\t%6f", dG_complexes[i][j]);
          }
        }
      }

      for (size_t j = 0; complexes[1][j] != NULL; j++) {
        curr_complex_string[0]  = complexes[1][j][0];
        curr_complex_string[0]  += (curr_complex_string[0] > 25) ? 'a' : 'A';
        curr_complex_string[1]  = '\0';
        if (j == 0) {
          vrna_strcat_printf(&monomer_string, "%s", curr_complex_string);
          vrna_strcat_printf(&dG_string_monomers, "%6f", dG_complexes[1][j]);
        } else {
          vrna_strcat_printf(&monomer_string, "\t\t%s", curr_complex_string);
          vrna_strcat_printf(&dG_string_monomers, "\t%6f", dG_complexes[1][j]);
        }
      }

      free(curr_complex_string);

      vrna_strcat_printf(&complex_string, "\t\t%s", monomer_string);
      vrna_strcat_printf(&dG_string, "\t%s", dG_string_monomers);

      vrna_cstr_printf_thead(o_stream->data, complex_string);
      vrna_cstr_printf_tbody(o_stream->data, dG_string);

      free(dG_string_monomers);
      free(dG_string);

      /* concentration computations */
      if (opt->doC) {
        size_t  num_strands = eff_strands;

        /* count number of true complexes */
        size_t  num_true_complexes = 0;
        for (size_t s = max_interacting_strands; s > 1; s--)
          for (size_t i = 0; complexes[s][i] != NULL; i++)
            num_true_complexes++;

        /* create complex-strand association matrix */
        unsigned int  **A = (unsigned int **)vrna_alloc(sizeof(unsigned int *) * num_strands);
        for (size_t a = 0; a < num_strands; a++)
          A[a] = (unsigned int *)vrna_alloc(sizeof(unsigned int) * num_true_complexes);

        /* fill complex-strand association matrix */
        size_t        curr_complex = 0;
        for (size_t s = max_interacting_strands; s > 1; s--)
          for (size_t i = 0; complexes[s][i] != NULL; i++) {
            for (size_t j = 0; j < s; j++)
              A[complexes[s][i][j]][curr_complex]++;

            curr_complex++;
          }

        /* create F_complexes and F_monomers arrays to compute equilibrium constants K */
        double  *equilibrium_constants_complexes;
        double  *F_monomers   = (double *)vrna_alloc(sizeof(double) * num_strands);
        double  *F_complexes  = (double *)vrna_alloc(sizeof(double) * num_true_complexes);

        for (size_t s = 0; s < num_strands; s++)
          F_monomers[s] = dG_complexes[1][s];

        curr_complex = 0;
        for (size_t s = max_interacting_strands; s > 1; s--)
          for (size_t i = 0; complexes[s][i] != NULL; i++) {
            F_complexes[curr_complex] = dG_complexes[s][i];
            curr_complex++;
          }

        equilibrium_constants_complexes = vrna_equilibrium_constants((const double *)F_complexes,
                                                                     (const double *)F_monomers,
                                                                     (const unsigned int **)A,
                                                                     kT,
                                                                     num_strands,
                                                                     num_true_complexes);

#if DEBUG
        for (size_t i = 0; i < num_true_complexes; i++)
          printf("K_%u = %g\n", equilibrium_constants_complexes[i]);
#endif

        /* count number of concentration computations */
        size_t num_conc = 0;
        for (; concentrations[(num_conc * num_strands)] != 0.; num_conc++);

        vrna_cstr_printf_thead(o_stream->data,
                               "Initial concentrations\t\trelative Equilibrium concentrations\n"
                               "%s\t\t%s", monomer_string, complex_string);

        double  *cc = (double *)vrna_alloc(sizeof(double) * num_strands);
        double  *conc_complexes;

        for (size_t c_i = 0; c_i < num_conc; c_i++) {
          char    *line = NULL;
          double  tot   = 0.;

          memcpy(cc, concentrations + (c_i * num_strands), sizeof(double) * num_strands);

          tot = cc[0];

          /* prepare output line with initial concentration data */
          vrna_strcat_printf(&line, "%-10g", cc[0]);

          for (size_t i = 1; i < num_strands; i++) {
            vrna_strcat_printf(&line, "\t%-10g", cc[i]);
            tot += cc[i];
          }

          conc_complexes = vrna_equilibrium_conc(equilibrium_constants_complexes,
                                                 cc,
                                                 (const unsigned int **)A,
                                                 num_strands,
                                                 num_true_complexes);

          /* append complex concentrations to output line */
          if (opt->concentration_absolute) {
            for (size_t i = 0; i < num_true_complexes; i++)
              vrna_strcat_printf(&line, "\t%.6g", conc_complexes[i]);

            /* append monomer concentrations to output line */
            for (size_t i = 0; i < num_strands; i++)
              vrna_strcat_printf(&line, "\t%.6g", cc[i]);
          } else {
            for (size_t i = 0; i < num_true_complexes; i++)
              vrna_strcat_printf(&line, "\t%.6g", conc_complexes[i] / tot);

            /* append monomer concentrations to output line */
            for (size_t i = 0; i < num_strands; i++)
              vrna_strcat_printf(&line, "\t%.6g", cc[i] / tot);
          }

          vrna_cstr_printf_tbody(o_stream->data, line);

          free(line);
          free(conc_complexes);
        }

        free(cc);
        free(equilibrium_constants_complexes);
        free(F_monomers);
        free(F_complexes);
        for (size_t a = 0; a < num_strands; a++)
          free(A[a]);
        free(A);
      }

      free(complex_string);
      free(monomer_string);

      /* major cleanup */
      for (size_t k = 1; k <= max_interacting_strands; k++) {
        for (size_t i = 0; complexes[k][i] != NULL; i++)
          free(complexes[k][i]);
        free(complexes[k]);
        free(dG_complexes[k]);
      }

      complexes     += 1;
      dG_complexes  += 1;
      free(complexes);
      free(dG_complexes);
    }

    free(prAB);
    free(pairing_propensity);
  }   /*end if(pf)*/

  if (opt->output_queue)
    vrna_ostream_provide(opt->output_queue, record->number, (void *)o_stream);
  else
    flush_cstr_callback(NULL, 0, (void *)o_stream);

  /* clean up */
  free(mfAB);
  free(record->SEQ_ID);
  free(record->id);
  free(sequence);
  free(record->sequence);
  free(mfe_structure);
  /* free the rest of current dataset */
  if (record->rest) {
    for (i = 0; record->rest[i]; i++)
      free(record->rest[i]);
    free(record->rest);
  }

  vrna_fold_compound_free(vc);

  free(record);
}


static void
compute_MEA(vrna_fold_compound_t  *fc,
            double                MEAgamma,
            vrna_cstr_t           rec_output)
{
  char  *structure, *mea_structure;
  float mea, mea_en;

  structure = vrna_MEA(fc, MEAgamma, &mea);

  mea_en = vrna_eval_structure(fc, (const char *)structure);

  /* insert cut point */
  mea_structure = vrna_cut_point_insert(structure, fc->cutpoint);

  vrna_cstr_printf_structure(rec_output, mea_structure, " {%6.2f MEA=%.2f}", mea_en, mea);

  free(structure);
  free(mea_structure);
}


static void
compute_centroid(vrna_fold_compound_t *fc,
                 vrna_cstr_t          rec_output)
{
  char    *cent, *centroid_structure;
  double  cent_en, dist;

  cent    = vrna_centroid(fc, &dist);
  cent_en = vrna_eval_structure(fc, (const char *)cent);

  /* insert cut point */
  centroid_structure = vrna_cut_point_insert(cent, fc->cutpoint);

  vrna_cstr_printf_structure(rec_output, centroid_structure, " {%6.2f d=%.2f}", cent_en, dist);

  free(cent);
  free(centroid_structure);
}


static char *
get_filename(const char     *id,
             const char     *suffix,
             const char     *filename_default,
             struct options *opt)
{
  char *tmp_string, *filename = NULL;

  if (id) {
    filename = vrna_strdup_printf("%s%s%s", id, opt->filename_delim, suffix);
    /* sanitize file names */
    tmp_string = vrna_filename_sanitize(filename, opt->filename_delim);
    free(filename);
    filename = tmp_string;
  } else {
    filename = vrna_strdup_printf("%s", filename_default);
  }

  return filename;
}


void
postscript_layout(vrna_fold_compound_t  *fc,
                  const char            *orig_sequence,
                  const char            *structure,
                  const char            *SEQ_ID,
                  struct options        *opt)
{
  char *filename_plot, *annot, *tmp_string;

  filename_plot = NULL;
  annot         = NULL;

  if (SEQ_ID) {
    filename_plot = vrna_strdup_printf("%s%sss.ps", SEQ_ID, opt->filename_delim);
    tmp_string    = vrna_filename_sanitize(filename_plot, opt->filename_delim);
    free(filename_plot);
    filename_plot = tmp_string;
  } else {
    filename_plot = strdup("rna.ps");
  }

  if (fc->cutpoint >= 0) {
    annot = vrna_strdup_printf("1 %d 9  0 0.9 0.2 omark\n"
                               "%d %d 9  1 0.1 0.2 omark\n",
                               fc->cutpoint - 1,
                               fc->cutpoint + 1,
                               fc->length + 1);
  }

  if (filename_plot) {
    THREADSAFE_FILE_OUTPUT(
      (void)vrna_file_PS_rnaplot_a(orig_sequence,
                                   structure,
                                   filename_plot,
                                   annot,
                                   NULL,
                                   &(opt->md)));
  }

  free(filename_plot);
  free(annot);
}


PRIVATE double *
read_concentrations(FILE    *fp,
                    size_t  num_strands)
{
  /* reads concentrations, returns list of double, -1. marks end */
  char    *line;
  double  *startc;
  int     i = 0, n = 2;

  startc = (double *)vrna_alloc((num_strands * n + 1) * sizeof(double));

  while ((line = vrna_read_line(fp)) != NULL) {
    int c;
    if (i == n) {
      n       *= 2;
      startc  = (double *)vrna_realloc(startc, (num_strands * n + 1) * sizeof(double));
    }

    char    **tok = vrna_strsplit(line, "\t");

    size_t  s = 0;
    double  concentration;
    for (char **ptr = tok; *ptr; ptr++, s++) {
      c = sscanf(*ptr, "%lf", &concentration);
      if (c)
        startc[(i * num_strands) + s] = concentration;
      else
        break;

      free(*ptr);
    }
    free(tok);

    if (s == num_strands)
      i++;
    else
      vrna_log_warning("Failed to parse all concentrations (%u) from line %d:\n%s\n",
                           num_strands,
                           i,
                           line);

    free(line);
  }

  for (size_t s = 0; s < num_strands; s++)
    startc[(i * num_strands) + s] = 0;

  return startc;
}

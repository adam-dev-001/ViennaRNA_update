/*
 *              Ineractive access to suboptimal folding
 *
 *                         c Ivo L Hofacker
 *                        Vienna RNA package
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <config.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <ctype.h>
#include <string.h>
#include "ViennaRNA/partfunc/global.h"
#include "ViennaRNA/sampling/basic.h"
#include "ViennaRNA/mfe/global.h"
#include "ViennaRNA/eval/structures.h"
#include "ViennaRNA/fold_vars.h"
#include "ViennaRNA/utils/basic.h"
#include "ViennaRNA/utils/strings.h"
#include "ViennaRNA/utils/log.h"
#include "ViennaRNA/params/io.h"
#include "ViennaRNA/subopt/wuchty.h"
#include "ViennaRNA/subopt/zuker.h"
#include "ViennaRNA/params/basic.h"
#include "ViennaRNA/constraints/basic.h"
#include "ViennaRNA/probing/SHAPE.h"
#include "ViennaRNA/constraints/soft_special.h"
#include "ViennaRNA/io/file_formats.h"
#include "ViennaRNA/io/utils.h"
#include "ViennaRNA/io/commands.h"
#include "ViennaRNA/datastructures/string.h"

#include "RNAsubopt_cmdl.h"
#include "gengetopt_helpers.h"
#include "input_id_helpers.h"
#include "modified_bases_helpers.h"

#include "ViennaRNA/intern/color_output.h"

PRIVATE void
putoutzuker(FILE                    *output,
            vrna_subopt_solution_t  *zukersolution);


struct nr_en_data {
  FILE                  *output;
  vrna_fold_compound_t  *fc;
  double                kT;
  double                ens_en;
};


PRIVATE void
print_samples(const char  *structure,
              void        *data);


PRIVATE void
print_samples_en(const char *structure,
                 void       *data);


int
main(int  argc,
     char *argv[])
{
  FILE                                *input, *output;
  struct          RNAsubopt_args_info args_info;
  char                                *rec_sequence, *rec_id,
                                      **rec_rest, *orig_sequence, *constraints_file, *cstruc,
                                      *structure, *shape_file, *shape_method, *shape_conversion,
                                      *infile, *outfile, *filename_delim;
  unsigned int                        rec_type, read_opt;
  int                                 i, length, cl, istty, delta, n_back, noconv, dos, zuker,
                                      with_shapes, verbose, enforceConstraints, st_back_en, batch,
                                      tofile, filename_full, canonicalBPonly, nonRedundant;
  double                              deltap;
  vrna_md_t                           md;
  dataset_id                          id_control;
  vrna_cmd_t                          commands;
  vrna_sc_mod_param_t                 *mod_params;

  do_backtrack        = 1;
  delta               = 100;
  deltap              = n_back = noconv = dos = zuker = 0;
  rec_type            = read_opt = 0;
  rec_id              = rec_sequence = orig_sequence = NULL;
  rec_rest            = NULL;
  cstruc              = structure = NULL;
  st_back_en          = 0;
  infile              = NULL;
  outfile             = NULL;
  output              = NULL;
  tofile              = 0;
  filename_full       = 0;
  canonicalBPonly     = 0;
  commands            = NULL;
  nonRedundant        = 0;
  mod_params          = NULL;

  set_model_details(&md);

  /* switch on unique multibranch loop decomposition */
  md.uniq_ML = 1;

  /*
   #############################################
   # check the command line parameters
   #############################################
   */
  if (RNAsubopt_cmdline_parser(argc, argv, &args_info) != 0)
    exit(1);

  /* prepare logging system and verbose mode */
  ggo_log_settings(args_info, verbose);

  /* parse options for ID manipulation */
  ggo_get_id_control(args_info, id_control, "Sequence", "sequence", "_", 4, 1);

  /* get basic set of model details */
  ggo_get_md_eval(args_info, md);
  ggo_get_md_fold(args_info, md);
  ggo_get_md_part(args_info, md);
  ggo_get_circ(args_info, md.circ);

  /* temperature */
  ggo_get_temperature(args_info, md.temperature);

  /* check dangle model */
  if ((md.dangles < 0) || (md.dangles > 3)) {
    vrna_log_warning("required dangle model not implemented, falling back to default dangles=2");
    md.dangles = dangles = 2;
  }

  /* SHAPE reactivity data */
  ggo_get_SHAPE(args_info, with_shapes, shape_file, shape_method, shape_conversion);

  ggo_get_constraints_settings(args_info,
                               fold_constrained,
                               constraints_file,
                               enforceConstraints,
                               batch);

  /* enforce canonical base pairs in any case? */
  if (args_info.canonicalBPonly_given)
    canonicalBPonly = 1;

  /* do not convert DNA nucleotide "T" to appropriate RNA "U" */
  if (args_info.noconv_given)
    noconv = 1;

  /* energy range */
  if (args_info.deltaEnergy_given)
    delta = (int)(0.1 + args_info.deltaEnergy_arg * 100);

  /* energy range after post evaluation */
  if (args_info.deltaEnergyPost_given)
    deltap = args_info.deltaEnergyPost_arg;

  /* sorted output */
  if (args_info.sorted_given) {
    subopt_sorted = VRNA_SORT_BY_ENERGY_LEXICOGRAPHIC_ASC;
    if (args_info.en_only_given)
      subopt_sorted = VRNA_SORT_BY_ENERGY_ASC;
  }

  /* stochastic backtracking */
  if (args_info.stochBT_given) {
    n_back = args_info.stochBT_arg;
    md.compute_bpp = 0;
    if (args_info.random_seed_given)
      vrna_init_rand_seed((unsigned int)args_info.random_seed_arg);
    else
      vrna_init_rand();
  }

  if (args_info.stochBT_en_given) {
    n_back          = args_info.stochBT_en_arg;
    st_back_en      = 1;
    md.compute_bpp  = 0;
    if (args_info.random_seed_given)
      vrna_init_rand_seed((unsigned int)args_info.random_seed_arg);
    else
      vrna_init_rand();
  }

  /* density of states */
  if (args_info.dos_given) {
    dos           = 1;
    print_energy  = -999999;
  }

  /* logarithmic multiloop energies */
  if (args_info.logML_given)
    md.logML = logML = 1;

  /* zuker subopts */
  if (args_info.zuker_given)
    zuker = 1;

  if (zuker) {
    if (md.circ) {
      vrna_log_warning("Sorry, zuker subopts not yet implemented for circfold");
      RNAsubopt_cmdline_parser_print_help();
      exit(1);
    } else if (n_back > 0) {
      vrna_log_warning("Can't do zuker subopts and stochastic subopts at the same time");
      RNAsubopt_cmdline_parser_print_help();
      exit(1);
    } else if (md.gquad) {
      vrna_log_warning("G-quadruplex support for Zuker subopts not implemented yet");
      RNAsubopt_cmdline_parser_print_help();
      exit(1);
    }
  }

  if (md.gquad && (n_back > 0)) {
    vrna_log_warning("G-quadruplex support for stochastic backtracking not implemented yet");
    RNAsubopt_cmdline_parser_print_help();
    exit(1);
  }

  if (args_info.infile_given)
    infile = strdup(args_info.infile_arg);

  if (args_info.outfile_given) {
    tofile = 1;
    if (args_info.outfile_arg)
      outfile = strdup(args_info.outfile_arg);
  }

  /* filename sanitize delimiter */
  if (args_info.filename_delim_given)
    filename_delim = strdup(args_info.filename_delim_arg);
  else if (get_id_delim(id_control))
    filename_delim = strdup(get_id_delim(id_control));
  else
    filename_delim = NULL;

  if ((filename_delim) && isspace(*filename_delim)) {
    free(filename_delim);
    filename_delim = NULL;
  }

  /* full filename from FASTA header support */
  if (args_info.filename_full_given)
    filename_full = 1;

  /* non-redundant backtracing */
  if (args_info.nonRedundant_given)
    nonRedundant = 1;

  if (args_info.commands_given)
    commands = vrna_file_commands_read(args_info.commands_arg,
                                       VRNA_CMD_PARSE_HC | VRNA_CMD_PARSE_SC);

  ggo_get_modified_base_settings(args_info,
                                 mod_params,
                                 &(md));

  ggo_geometry_settings(args_info, &md);

  /* free allocated memory of command line data structure */
  RNAsubopt_cmdline_parser_free(&args_info);

  /*
   #############################################
   # begin initializing
   #############################################
   */

  if (infile) {
    input = fopen((const char *)infile, "r");
    if (!input) {
      vrna_log_error("Could not read input file");
      goto exit_fail;
    }
  } else {
    input = stdin;
  }

  istty = (!infile) && isatty(fileno(stdout)) && isatty(fileno(stdin));

  /* print user help if we get input from tty */
  if (istty) {
    if (!zuker)
      print_comment(stdout, "Use '&' to connect sequences that shall form a complex.");

    if (fold_constrained) {
      vrna_message_constraint_options(
        VRNA_CONSTRAINT_DB_DOT | VRNA_CONSTRAINT_DB_X | VRNA_CONSTRAINT_DB_ANG_BRACK |
        VRNA_CONSTRAINT_DB_RND_BRACK);
      vrna_message_input_seq("Input sequence (upper or lower case) followed by structure constraint");
    } else {
      vrna_message_input_seq_simple();
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
  while (
    !((rec_type = vrna_file_fasta_read_record(&rec_id, &rec_sequence, &rec_rest, input, read_opt))
      & (VRNA_INPUT_ERROR | VRNA_INPUT_QUIT))) {
    size_t  **mod_positions, mod_param_sets;
    char    *SEQ_ID         = NULL;
    char    *v_file_name    = NULL;
    char    *tmp_string     = NULL;
    int     maybe_multiline = 0;

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
    set_next_id(&rec_id, id_control);
    SEQ_ID = fileprefix_from_id(rec_id, id_control, filename_full);

    if (tofile) {
      /* prepare the file name */
      if (outfile)
        v_file_name = vrna_strdup_printf("%s", outfile);
      else
        v_file_name = (SEQ_ID) ?
                      vrna_strdup_printf("%s.sub", SEQ_ID) :
                      vrna_strdup_printf("RNAsubopt_output.sub");

      tmp_string = vrna_filename_sanitize(v_file_name, filename_delim);
      free(v_file_name);
      v_file_name = tmp_string;

      if (infile && !strcmp(infile, v_file_name)) {
        vrna_log_error("Input and output file names are identical");
        goto exit_fail;
      }
      output = fopen((const char *)v_file_name, "a");
      if (!output) {
        vrna_log_error("Failed to open file for writing");
        goto exit_fail;
      }
    } else {
      output = stdout;
    }

    /* convert DNA alphabet to RNA if not explicitely switched off */
    if (!noconv)
      vrna_seq_toRNA(rec_sequence);

    /* store case-unmodified sequence */
    orig_sequence = strdup(rec_sequence);

    mod_positions = mod_positions_seq_prepare(rec_sequence,
                                              mod_params,
                                              verbose,
                                              &mod_param_sets);

    /* convert sequence to uppercase letters only */
    vrna_seq_toupper(rec_sequence);

    vrna_fold_compound_t *vc = vrna_fold_compound(rec_sequence, &md, VRNA_OPTION_DEFAULT);
    length = vc->length;

    structure = (char *)vrna_alloc(sizeof(char) * (length + 1));

    /* parse the rest of the current dataset to obtain a structure constraint */
    if (fold_constrained) {
      if (constraints_file) {
        vrna_constraints_add(vc, constraints_file, VRNA_OPTION_DEFAULT);
      } else {
        char          *constraint_ptr, **constraints;
        unsigned int  options;

        options  = (maybe_multiline) ? VRNA_OPTION_MULTILINE : 0;

        constraint_ptr = vrna_extract_record_rest_structure((const char **)rec_rest, 0, options);
        constraints = vrna_strsplit(constraint_ptr, NULL);
        free(constraint_ptr);

        /* collect strand-wise constraints */
        if (constraints) {
          unsigned int  i = 0, strand_cnt = 1;
          vrna_string_t constraint;
          
          constraint = vrna_string_make(NULL);

          for (char **ptr = constraints; *ptr != NULL; ptr++, strand_cnt) {
            if (strand_cnt > vc->strands) {
              vrna_log_error("Structure constraint contains too many strands (expected %u, got at least %u)\n",
                                 vc->strands,
                                 strand_cnt);
              goto exit_fail;
            }
            unsigned int l = strlen(*ptr);
            if (vc->strand_end[strand_cnt] != i + l) {
              vrna_log_error("Length of structure constraint for strand %u differs from sequence (expected %u, got %u)\n",
                                 strand_cnt,
                                 vc->strand_end[strand_cnt] - i,
                                 l);
              goto exit_fail;
            }
            vrna_string_append_cstring(constraint, *ptr);
          }

          /* convert pseudo-dot-bracket to actual hard constraints */
          unsigned int constraint_options = VRNA_CONSTRAINT_DB_DEFAULT;

          if (enforceConstraints)
            constraint_options |= VRNA_CONSTRAINT_DB_ENFORCE_BP;

          if (canonicalBPonly)
            constraint_options |= VRNA_CONSTRAINT_DB_CANONICAL_BP;

          vrna_constraints_add(vc, (const char *)constraint, constraint_options);

          vrna_string_free(constraint);
        }
      }
    }

    if (with_shapes) {
      vrna_constraints_add_SHAPE(vc,
                                 shape_file,
                                 shape_method,
                                 shape_conversion,
                                 verbose,
                                 VRNA_OPTION_MFE | ((n_back > 0) ? VRNA_OPTION_PF : 0));
    }

    if (commands)
      vrna_commands_apply(vc,
                          commands,
                          VRNA_CMD_PARSE_HC | VRNA_CMD_PARSE_SC);

    /* apply modified base support if requested */
    mod_bases_apply(vc,
                    mod_param_sets,
                    mod_positions,
                    mod_params);

    if (istty) {
      if (vc->strands == 1) {
        printf("length = %d", length);
      } else {
        printf("length1 = %d\nlength2 = %d",
               cut_point - 1,
               length - cut_point + 1);
      }
    }

    /*
     ########################################################
     # begin actual computations
     ########################################################
     */

    if ((logML != 0 || md.dangles == 1 || md.dangles == 3) && dos == 0)
      if (deltap <= 0)
        deltap = delta / 100. + 0.001;

    if (deltap > 0)
      print_energy = deltap;

    /* stochastic backtracking */
    if (n_back > 0) {
      double        mfe, kT, ens_en;
      unsigned int  options = (nonRedundant) ?
                              VRNA_PBACKTRACK_NON_REDUNDANT :
                              VRNA_PBACKTRACK_DEFAULT;

      if (vc->strands > 1) {
        vrna_log_error(
          "Boltzmann sampling for multiple interacting sequences not implemented (yet)!");
        goto exit_fail;
      }
      print_fasta_header(output, rec_id);

      fprintf(output, "%s\n", orig_sequence);

      mfe = vrna_mfe(vc, structure);
      /* rescale Boltzmann factors according to predicted MFE */
      vrna_exp_params_rescale(vc, &mfe);

      vrna_mx_mfe_free(vc);

      /* ignore return value, we are not interested in the free energy */
      ens_en  = vrna_pf(vc, structure);
      kT      = vc->exp_params->kT / 1000.;

      if (st_back_en) {
        struct nr_en_data dat;
        dat.output  = output;
        dat.fc      = vc;
        dat.kT      = kT;
        dat.ens_en  = ens_en;

        vrna_pbacktrack_cb(vc,
                           n_back,
                           &print_samples_en,
                           (void *)&dat,
                           options);
      } else {
        vrna_pbacktrack_cb(vc,
                           n_back,
                           &print_samples,
                           (void *)output,
                           options);
      }
    }
    /* normal subopt */
    else if (!zuker) {
      /* first lines of output (suitable  for sort +1n) */
      if (rec_id) {
        char *head = vrna_strdup_printf("%s [%d]", rec_id, delta);
        print_fasta_header(output, head);
        free(head);
      }

      vrna_subopt(vc, delta, subopt_sorted, output);

      if (dos) {
        int i;
        for (i = 0; i <= MAXDOS && i <= delta / 10; i++) {
          char *tline = vrna_strdup_printf("%4d %6d", i, density_of_states[i]);
          print_table(output, NULL, tline);
          free(tline);
        }
      }
    }
    /* Zuker suboptimals */
    else {
      vrna_subopt_solution_t  *zr;

      if (vc->strands > 1) {
        vrna_log_error("Sorry, zuker subopts not yet implemented for cofold");
        goto exit_fail;
      }
      int                     i;
      print_fasta_header(output, rec_id);

      fprintf(output, "%s\n", orig_sequence);

      zr = vrna_subopt_zuker(vc);

      putoutzuker(output, zr);
      (void)fflush(output);
      for (i = 0; zr[i].structure; i++)
        free(zr[i].structure);
      free(zr);
    }

    (void)fflush(output);

    /* clean up */
    vrna_fold_compound_free(vc);

    free(cstruc);

    free(rec_id);
    free(SEQ_ID);
    free(rec_sequence);
    free(orig_sequence);
    free(structure);

    /* free the rest of current dataset */
    if (rec_rest) {
      for (i = 0; rec_rest[i]; i++)
        free(rec_rest[i]);
      free(rec_rest);
    }

    rec_id    = rec_sequence = orig_sequence = structure = cstruc = NULL;
    rec_rest  = NULL;

    if (tofile && output) {
      fclose(output);
      output = NULL;
    }

    free(v_file_name);

    if (with_shapes || (constraints_file && (!batch)))
      break;

    /* print user help for the next round if we get input from tty */
    if (istty) {
      if (!zuker)
        print_comment(stdout, "Use '&' to connect sequences that shall form a complex.");

      if (fold_constrained) {
        vrna_message_constraint_options(
          VRNA_CONSTRAINT_DB_DOT | VRNA_CONSTRAINT_DB_X | VRNA_CONSTRAINT_DB_ANG_BRACK |
          VRNA_CONSTRAINT_DB_RND_BRACK);
        vrna_message_input_seq(
          "Input sequence (upper or lower case) followed by structure constraint");
      } else {
        vrna_message_input_seq_simple();
      }
    }
  }

  if (infile && input)
    fclose(input);

  free(shape_method);
  free(shape_conversion);
  free(filename_delim);
  vrna_commands_free(commands);

  if (mod_params) {
    for (vrna_sc_mod_param_t *ptr = mod_params; *ptr != NULL; ptr++)
      vrna_sc_mod_parameters_free(*ptr);

    free(mod_params);
  }

  free_id_data(id_control);

  if (vrna_log_fp() != stderr)
    fclose(vrna_log_fp());

  return EXIT_SUCCESS;

  exit_fail:
    return EXIT_FAILURE;
}


PRIVATE void
print_samples(const char  *structure,
              void        *data)
{
  if (structure)
    print_structure((FILE *)data, structure, NULL);
}


PRIVATE void
print_samples_en(const char *structure,
                 void       *data)
{
  if (structure) {
    struct nr_en_data     *d      = (struct nr_en_data *)data;
    FILE                  *output = d->output;
    vrna_fold_compound_t  *fc     = d->fc;
    double                kT      = d->kT;
    double                ens_en  = d->ens_en;

    double                e         = vrna_eval_structure(fc, structure);
    double                prob      = exp((ens_en - e) / kT);
    char                  *e_string = vrna_strdup_printf(" %6.2f %6g", e, prob);

    print_structure(output, structure, e_string);

    free(e_string);
  }
}


PRIVATE void
putoutzuker(FILE                    *output,
            vrna_subopt_solution_t  *zukersolution)
{
  int   i;
  char  *e_string;

  for (i = 0; zukersolution[i].structure; i++) {
    e_string = vrna_strdup_printf(" [%6.2f]", zukersolution[i].energy);
    print_structure(output, zukersolution[i].structure, e_string);
    free(e_string);
  }
  return;
}

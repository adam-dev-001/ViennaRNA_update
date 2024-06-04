/*
 *    ViennaRNA/utils/basic.c
 *
 *               c  Ivo L Hofacker and Walter Fontana
 *                        Vienna RNA package
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdarg.h>
#include <errno.h>

/* isatty() is not available for Windows systems */
#ifndef _WIN32
#include <unistd.h>
#endif

#include "ViennaRNA/color_output.inc"

#include "ViennaRNA/datastructures/array.h"
#include "ViennaRNA/utils/basic.h"
#include "ViennaRNA/utils/log.h"

/*
 #################################
 # GLOBAL VARIABLES              #
 #################################
 */

/*
 #################################
 # PRIVATE VARIABLES             #
 #################################
 */
typedef struct {
  vrna_log_cb_f cb;
  void          *cb_data;
  int           level;
} logger_callback;

PRIVATE struct {
  FILE                        *default_file;
  int                         default_level;
  unsigned int                options;
  vrna_log_lock_f             lock;
  void                        *lock_data;
  vrna_array(logger_callback) callbacks;
} logger = {
  .default_file   = NULL,
  .default_level  = VRNA_LOG_LEVEL_DEFAULT,
  .options        = VRNA_LOG_OPTIONS_DEFAULT,
  .lock           = NULL,
  .lock_data      = NULL,
  .callbacks      = NULL
};

/*
 #################################
 # PRIVATE FUNCTION DECLARATIONS #
 #################################
 */
PRIVATE void
log_v(vrna_log_event_t *event);


PRIVATE void
log_stderr(vrna_log_event_t *event);

PRIVATE void
lock(void);


PRIVATE void
unlock(void);


PRIVATE const char *
get_log_level_color(int level);

/*
 #################################
 # BEGIN OF FUNCTION DEFINITIONS #
 #################################
 */

PUBLIC void
vrna_log(int level,
         const char *file_name,
         int        line_number,
         const char *format_string,
         ...)
{
  vrna_log_event_t  event = {
    .format_string = format_string,
    .level = level,
    .line_number = line_number,
    .file_name = file_name
  };

  va_start(event.params, format_string); 
  log_v(&event); 
  va_end(event.params);
}


PUBLIC int
vrna_log_level(void)
{
  return logger.default_level;
}


PUBLIC int
vrna_log_level_set(int level)
{
  switch (level) {
    case VRNA_LOG_LEVEL_DEBUG:
      /* fall through */
    case VRNA_LOG_LEVEL_INFO:
      /* fall through */
    case VRNA_LOG_LEVEL_WARNING:
      /* fall through */
    case VRNA_LOG_LEVEL_ERROR:
      /* fall through */
    case VRNA_LOG_LEVEL_CRITICAL:
      logger.default_level = level;
      break;
    default:
      vrna_log_warning("unkown log level specified! Not doing anything");
      level = VRNA_LOG_LEVEL_UNKNOWN;
      break;
  }

  return level;
}


PUBLIC unsigned int
vrna_log_options(void)
{
  return logger.options;
}


PUBLIC void
vrna_log_options_set(unsigned int options)
{
  logger.options = options;
}



PRIVATE void
log_v(vrna_log_event_t *event)
{
  lock();

  if (logger.callbacks == NULL)
    /* initialize callbacks if not done so far */
    vrna_array_init(logger.callbacks);

  if (!(logger.options & VRNA_LOG_OPTIONS_QUIET)) {
    /* print log if not in quiet mode */
    if (event->level >= logger.default_level)
      log_stderr(event);
  }

  for (size_t i = 0; i < vrna_array_size(logger.callbacks); i++) {
    logger_callback *cb = &(logger.callbacks[i]);
    if (event->level >= cb->level) {
      cb->cb(event, cb->cb_data);
    }
  } 

  unlock();
}


PRIVATE void
lock(void)
{

}

PRIVATE void
unlock(void)
{

}


PRIVATE const char *
get_log_level_string(int level)
{
  switch (level) {
    case VRNA_LOG_LEVEL_DEBUG:
      return "[DEBUG]";
    case VRNA_LOG_LEVEL_INFO:
      return "[INFO]";
    case VRNA_LOG_LEVEL_WARNING:
      return "[WARNING]";
    case VRNA_LOG_LEVEL_ERROR:
      return "[ERROR]";
    case VRNA_LOG_LEVEL_CRITICAL:
      return "[FATAL]";
    default:
      return "[UNKNOWN]";
  }
}


PRIVATE const char *
get_log_level_color(int level)
{
  switch (level) {
    case VRNA_LOG_LEVEL_DEBUG:
      return ANSI_COLOR_CYAN_B;
    case VRNA_LOG_LEVEL_INFO:
      return ANSI_COLOR_BLUE_B;
    case VRNA_LOG_LEVEL_WARNING:
      return ANSI_COLOR_YELLOW_B;
    case VRNA_LOG_LEVEL_ERROR:
      return ANSI_COLOR_RED_B;
    case VRNA_LOG_LEVEL_CRITICAL:
      return ANSI_COLOR_MAGENTA_B;
    default:
      return ANSI_COLOR_RESET;
  }
}



PUBLIC void
vrna_message_error(const char *format,
                   ...)
{
  va_list args;

  va_start(args, format);
  vrna_message_verror(format, args);
  va_end(args);
}


PUBLIC void
vrna_message_verror(const char  *format,
                    va_list     args)
{
#ifndef VRNA_WITHOUT_TTY_COLORS
  if (isatty(fileno(stderr))) {
    fprintf(stderr, ANSI_COLOR_RED_B "ERROR: " ANSI_COLOR_RESET ANSI_COLOR_BRIGHT);
    vfprintf(stderr, format, args);
    fprintf(stderr, ANSI_COLOR_RESET "\n");
  } else {
#endif
  fprintf(stderr, "ERROR: ");
  vfprintf(stderr, format, args);
  fprintf(stderr, "\n");
#ifndef VRNA_WITHOUT_TTY_COLORS
}


#endif

#ifdef EXIT_ON_ERROR
  exit(EXIT_FAILURE);
#endif
}


PUBLIC void
vrna_message_warning(const char *format,
                     ...)
{
#if 1
  vrna_log_event_t  event = {
    .format_string = format,
    .level = VRNA_LOG_LEVEL_WARNING,
    .line_number = __LINE__,
    .file_name = __FILE__
  };
  va_start(event.params, format); 
  log_v(&event); 
  va_end(event.params);
#else
  va_list args;

  va_start(args, format);
  vrna_message_vwarning(format, args);
  va_end(args);
#endif
}


PUBLIC void
vrna_message_vwarning(const char  *format,
                      va_list     args)
{
#ifndef VRNA_WITHOUT_TTY_COLORS
  if (isatty(fileno(stderr))) {
    fprintf(stderr, ANSI_COLOR_MAGENTA_B "WARNING: " ANSI_COLOR_RESET ANSI_COLOR_BRIGHT);
    vfprintf(stderr, format, args);
    fprintf(stderr, ANSI_COLOR_RESET "\n");
  } else {
#endif
  fprintf(stderr, "WARNING: ");
  vfprintf(stderr, format, args);
  fprintf(stderr, "\n");
#ifndef VRNA_WITHOUT_TTY_COLORS
}


#endif
}


PUBLIC void
vrna_message_info(FILE        *fp,
                  const char  *format,
                  ...)
{
#if 1
  vrna_log_event_t  event = {
    .format_string = format,
    .level = VRNA_LOG_LEVEL_INFO,
    .line_number = __LINE__,
    .file_name = __FILE__
  };
  va_start(event.params, format); 
  log_v(&event); 
  va_end(event.params);
#else
  va_list args;

  va_start(args, format);
  vrna_message_vinfo(fp, format, args);
  va_end(args);
#endif
}


PUBLIC void
vrna_message_vinfo(FILE       *fp,
                   const char *format,
                   va_list    args)
{
  if (!fp)
    fp = stdout;

#ifndef VRNA_WITHOUT_TTY_COLORS
  if (isatty(fileno(fp))) {
    fprintf(fp, ANSI_COLOR_BLUE_B);
    vfprintf(fp, format, args);
    fprintf(fp, ANSI_COLOR_RESET "\n");
  } else {
#endif
    vfprintf(fp, format, args);
    fprintf(fp, "\n");
#ifndef VRNA_WITHOUT_TTY_COLORS
  }
#endif
}


PRIVATE void
log_stderr(vrna_log_event_t *event)
{
  FILE *fp = stderr;

  if (logger.default_file)
    fp = logger.default_file;

  /* print time unless turned off explicitely */
  if (logger.options & VRNA_LOG_OPTIONS_TRACE_TIME) {
    char timebuf[64];
    time_t t = time(NULL);
    timebuf[strftime(timebuf, sizeof(timebuf), "%H:%M:%S", localtime(&t))] = '\0';
    fprintf(fp, "%s ", timebuf);
  }

  /* print log level */
#ifndef VRNA_WITHOUT_TTY_COLORS
  if (isatty(fileno(fp))) {
    fprintf(fp,
            "%s%-9s" ANSI_COLOR_RESET " ",
            get_log_level_color(event->level),
            get_log_level_string(event->level));
  } else {
#endif
    fprintf(fp,
            "-9s ",
            get_log_level_string(event->level));
#ifndef VRNA_WITHOUT_TTY_COLORS
  }
#endif

  /* print file name / line number trace unless turned off explicitely */
  if (logger.options & VRNA_LOG_OPTIONS_TRACE_CALL) {
#ifndef VRNA_WITHOUT_TTY_COLORS
    if (isatty(fileno(fp))) {
      fprintf(fp,
              "\x1b[90m%s:%d:" ANSI_COLOR_RESET " ",
              event->file_name,
              event->line_number);
    } else {
#endif
      fprintf(fp, "%s:%d: ",
              event->file_name,
              event->line_number);
#ifndef VRNA_WITHOUT_TTY_COLORS
    }
#endif
  }

  /* print actual message */
  vfprintf(fp, event->format_string, event->params);
  fprintf(fp, "\n");
}


#ifndef VRNA_DISABLE_BACKWARD_COMPATIBILITY

/*
 * ###########################################
 * # deprecated functions below              #
 *###########################################
 */


PUBLIC void
warn_user(const char message[])
{
  vrna_message_warning(message);
}


PUBLIC void
nrerror(const char message[])
{
  vrna_message_error(message);
}

#endif

#include "backlight.c"
#include "colord-utils.c"
#include <bits/getopt_core.h>
#include <colord.h>
#include <getopt.h>
#include <lcms2.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>

#ifndef VERSION
#define VERSION "version not defined"
#endif

const char *program_name;
char *template;

static const double min_brightness_fallback = 0.2;
static const CdObjectScope CdObjectScope_fallback = CD_OBJECT_SCOPE_NORMAL;
struct {
  int version_flag;
  int min_brightness_flag;
  int cd_obj_scope_temp_flag;
  int func_watch_flag;
  int func_list_flag;
  int func_apply_brightness_flag;

  double *brightness;
  double *min_brightness;
  CdObjectScope *cdObjectScope;
} options;

#define BUF_LEN (10 * (sizeof(struct inotify_event) + NAME_MAX + 1))

/*
get mapped brightness in case the screen is too dark
make sure you have called has_sysfs_backlight before calling this function
*/
static double get_mapped_brightness(double brightness) {
  return *options.min_brightness + (1 - *options.min_brightness) * brightness;
}

/* Setup inotify notifications (IN) mask. All these defined in inotify.h. */
static int event_mask =
    (IN_ACCESS |        /* File accessed */
     IN_ATTRIB |        /* File attributes changed */
     IN_OPEN |          /* File was opened */
     IN_CLOSE_WRITE |   /* Writtable File closed */
     IN_CLOSE_NOWRITE | /* Unwrittable File closed */
     IN_CREATE |        /* File created in directory */
     IN_DELETE |        /* File deleted in directory */
     IN_DELETE_SELF |   /* Directory deleted */
     IN_MODIFY |        /* File modified */
     IN_MOVE_SELF |     /* Directory moved */
     IN_MOVED_FROM |    /* File moved away from the directory */
     IN_MOVED_TO);      /* File moved into the directory */

int watch_brightness_change_daemon() {
  int inotifyFd, wd;
  char buf[BUF_LEN];
  ssize_t numRead;
  char *p;
  struct inotify_event *event;

  static int actual_brightness;
  static int previous_actual_brightness;

  if (!has_sysfs_backlight()) {
    perror("no sysfs backlight");
    return 1;
  }

  /* Create temporary dir to store icc files */
  umask(0000);
  errno = 0;
  int ret = mkdir("/tmp/icc-brightness", ALLPERMS);
  if (ret == -1) {
    switch (errno) {
    case EACCES:
      printf("mkdir: the parent directory does not allow write\n");
      exit(EXIT_FAILURE);
    case EEXIST:
      printf("mkdir: /tmp/icc-brightness already exists\n");
      break;
    case ENAMETOOLONG:
      printf("mkdir: pathname is too long\n");
      exit(EXIT_FAILURE);
    default:
      perror("mkdir");
      exit(EXIT_FAILURE);
    }
  }

  get_actual_brightness(&actual_brightness);
  get_brightness();

  /* Apply icc brightness profile once at start */
  previous_actual_brightness = actual_brightness;
  // cdutils_icc_change_brightness(brightness);
  cdutils_icc_change_brightness(get_mapped_brightness(get_brightness()),
                                *options.cdObjectScope);

  printf("start watching brightness change\n");

  /* Initializing inotify instance */
  inotifyFd = inotify_init();
  if (inotifyFd == -1)
    exit(1);

  wd = inotify_add_watch(inotifyFd, actual_brightness_value, event_mask);
  if (wd == -1) {
    exit(2);
  };

  while (1) { /* Read events forever */

    numRead = read(inotifyFd, buf, BUF_LEN);
    if (numRead == 0) {
      printf("read() from inotify fd returned 0!");
      exit(4);
    }

    if (numRead == -1)
      exit(3);

    // printf("Read %ld bytes from inotify fd\n", (long)numRead);

    /* Process all of the events in buffer returned by read() */

    for (p = buf; p < buf + numRead;) {
      event = (struct inotify_event *)p;
      if (event->mask & IN_MODIFY) {
        actual_brightness = get_actual_brightness();
        if (actual_brightness != previous_actual_brightness) {
          printf("\033c");
          printf("Read %ld bytes from inotify fd\n", (long)numRead);
          previous_actual_brightness = actual_brightness;
          printf("brightness: %0.2f\n", get_brightness());
          cdutils_icc_change_brightness(get_mapped_brightness(get_brightness()),
                                        *options.cdObjectScope);
        }
      }

      p += sizeof(struct inotify_event) + event->len;
    }
  }

  exit(EXIT_SUCCESS);
}

void show_version() {
  fprintf(stderr, "\
icc-brightness %s\n\
\n\
Written by ZongOu Huang 2023 in ZheJiang China.\n\
Home: https://github.com/zongou/icc-brightness\n\
",
          VERSION);
}

void show_help() {
  fprintf(stderr, "\
Change OLED brightness by applying ICC profiles.\n\
Usage:\n\
  %s [options]\n\n",
          program_name);

  fprintf(stderr, "\
Options:\n\
  -l, --list                 \tlist display devices.\n\
  -w, --watch                \twatch brightness change and apply icc profile.\n\
  -b, --brightness [val]     \tapply brightness profile.\n\
  --min-brightness [val]     \tset the min-brightness. (default: 0.2).\n\
  --tmp                      \tapply temporary icc profile, revert after quit.\n\
\n\
  -h, --help                 \tshow this help.\n\
  -v, --version              \tshow version.\n\
\n");
}

int main(int argc, char **argv) {

  if (argc == 1) {
    fprintf(stderr, "Try '%s --help' to show help\n", argv[0]);
    exit(1);
  }

  program_name = argv[0];

  int c;
  while (1) {
    static struct option long_options[] = {
        /*
        {*name, has_arg, *flag, val}
        int getopt_long{
          if (flag == NULL){
            return val;
          }else{
            *flag=val;
            return 0
          }
        }

        name <= long_options[option_index].name
        val <= getopt_long()
        optarg <= argv[optind-1]
        */

        /* These options set a flag. */
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'v'},
        {"list", no_argument, 0, 'l'},
        {"watch", no_argument, 0, 'w'},
        // {"brief", no_argument, &verbose_flag, 0},
        /* These options don’t set a flag.
           We distinguish them by their indices. */
        {"brightness", required_argument, 0, 'b'},
        {"min-brightness", required_argument, &options.min_brightness_flag, 1},
        {"tmp", no_argument, &options.cd_obj_scope_temp_flag, 1},
        {0, 0, 0, 0}};
    /* getopt_long stores the option index here. */
    int option_index = 0;

    c = getopt_long(argc, argv, "hvlwb:", long_options, &option_index);

    /* Detect the end of the options. */
    if (c == -1)
      break;

    // printf("option_index: %d, ", option_index);
    // printf("name: %s, ", long_options[option_index].name);
    // printf("has_arg: %s, ",
    //        long_options[option_index].has_arg == no_argument ? "no_argument"
    //        : long_options[option_index].has_arg == required_argument
    //            ? "required_argument"
    //        : long_options[option_index].has_arg == optional_argument
    //            ? "optional_argument"
    //            : "unexpected");
    // if (long_options[option_index].flag != NULL) {
    //   printf("flag: %d, ", *long_options[option_index].flag);
    // } else {
    //   printf("flag: NULL, ");
    // }
    // printf("ret: %d, ", c);
    // printf("ret2char: [%c], ", c);
    // printf("optind: %d, ", optind);
    // printf("optarg: %s, ", optarg);
    // printf("argv[optind-1]: %s, ", argv[optind - 1]);
    // printf("\n");

    switch (c) {
    case 0:
      /* If this option set a flag, do nothing else now. */
      // if (long_options[option_index].flag != 0)
      //   break;
      // printf("option %s", long_options[option_index].name);
      // if (optarg)
      //   printf(" with arg %s", optarg);
      // printf("\n");

      if (options.min_brightness_flag) {
        options.min_brightness = malloc(sizeof(void *));
        *options.min_brightness = strtod(optarg, NULL);
        printf("min_brightness: %0.2f\n", *options.min_brightness);
        options.min_brightness_flag = 0;
      }

      if (options.cd_obj_scope_temp_flag) {
        options.cdObjectScope = malloc(sizeof(void *));
        *options.cdObjectScope = CD_OBJECT_SCOPE_TEMP;
        options.cd_obj_scope_temp_flag = 0;
      }

      break;

    case 'h':
      show_help();
      exit(0);

    case 'v':
      options.version_flag = 1;
      break;

    case 'l':
      options.func_list_flag = 1;
      break;

    case 'w':
      options.func_watch_flag = 1;
      break;

    case 'b':
      options.func_apply_brightness_flag = 1;
      options.brightness = malloc(sizeof(void *));
      *options.brightness = strtod(optarg, NULL);
      if (*options.brightness > 1) {
        printf("brightness available range [0-1]\n");
        exit(1);
      }
      break;

    default:
      abort();
    }
  }

  /* Instead of reporting ‘--verbose’
     and ‘--brief’ as they are encountered,
     we report the final status resulting from them. */
  // if (options.verbose_flag) {
  //   printf("verbose flag is set %d\n", options.verbose_flag);
  // }

  /* Print any remaining command line arguments (not options). */
  if (optind < argc) {
    printf("non-option ARGV-elements: ");
    while (optind < argc)
      printf("%s ", argv[optind++]);
    putchar('\n');
  }

  /* Set fallback value if neccesary */
  if (options.cdObjectScope == NULL) {
    options.cdObjectScope = malloc(sizeof(void *));
    *options.cdObjectScope = CdObjectScope_fallback;
  }

  if (options.min_brightness == NULL) {
    options.min_brightness = malloc(sizeof(void *));
    *options.min_brightness = min_brightness_fallback;
    if (*options.min_brightness >= 1) {
      printf("min-brightness available range [0-1]\n");
      exit(1);
    }
  }

  /* Start functions */
  if (options.version_flag) {
    show_version();
  } else if (options.func_list_flag) {
    cdutils_list_devices(NULL);
  } else if (options.func_apply_brightness_flag) {
    if (has_sysfs_backlight()) {
      cdutils_icc_change_brightness(*options.brightness,
                                    CD_OBJECT_SCOPE_NORMAL);
    }
  } else if (options.func_watch_flag) {
    watch_brightness_change_daemon();
  }

  exit(0);
}

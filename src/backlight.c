#define _DEFAULT_SOURCE // exposes u_short in glibc, needed by fts(3)
#include <ctype.h>
#include <fts.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h> // for MAXPATHLEN
#include <sys/stat.h>
#include <sys/types.h>

char backlight_interface[MAXPATHLEN];
char actual_brightness_value[MAXPATHLEN];
char maximum_brightness_value[MAXPATHLEN];

bool has_sysfs_backlight(void) {
  bool found = false;
  char *path_argv[] = {"/sys/class/backlight", NULL};
  FTS *ftsp = fts_open(path_argv, FTS_LOGICAL | FTS_NOSTAT, NULL);
  if (ftsp != NULL) {
    fts_read(ftsp);

    struct stat statb;
    FTSENT *cur = fts_children(ftsp, FTS_NAMEONLY);
    while (!found && cur != NULL) {
      snprintf(actual_brightness_value, sizeof(actual_brightness_value),
               "%s%s/actual_brightness", cur->fts_path, cur->fts_name);
      if (stat(actual_brightness_value, &statb) == 0 &&
          S_ISREG(statb.st_mode)) {
        found = true;
        snprintf(maximum_brightness_value, sizeof(maximum_brightness_value),
                 "%s%s/max_brightness", cur->fts_path, cur->fts_name);
        snprintf(backlight_interface, sizeof(backlight_interface),
                 "%s%s/brightness", cur->fts_path, cur->fts_name);
      }
      cur = cur->fts_link;
    }
    fts_close(ftsp);
  }
  return found;
}

bool _get_brightness_file_val(char *path, int *v) {
  int val;
  FILE *src = fopen(path, "r");
  if (src != NULL) {
    int nv = fscanf(src, "%i", &val);
    if (nv == 1)
      *v = val;
    else
      return false;
    fclose(src);
  } else {
    return false;
  }
  return true;
}

int get_brightness_file_val(char *path) {
  int v;
  if (_get_brightness_file_val(path, &v)) {
    return v;
  } else {
    printf("get_brightness_file_val: fail\n");
    exit(1);
  }
}

int get_actual_brightness() {
  return get_brightness_file_val(actual_brightness_value);
}

int get_max_brightness() {
  return get_brightness_file_val(maximum_brightness_value);
}

double get_brightness() {
  return (double)get_actual_brightness() / get_max_brightness();
}

// int main() {
//   if (has_sysfs_backlight()) {
//     printf("brightness: %0.2f\n", get_brightness());
//   }
// }
/* Colord uitls */
#include "backlight.h"
#include <colord.h>
#include <lcms2.h>
#include <locale.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <uuid/uuid.h>

#define props_key_creator "Creator"
#define props_value_creator "icc-brightness"

typedef struct {
  CdClient *client;
  GPtrArray *devices;
} CdUtilConnection;

/* Check if this profile is created by us
make sure you have connected to this proifle before calling this function
*/
static gboolean cdutils_is_profile_created_by_us(CdProfile *profile) {
  if (CD_IS_PROFILE(profile)) {
    g_autoptr(GHashTable) metadata = cd_profile_get_metadata(profile);
    if (metadata != NULL) {
      gchar *generator =
          (char *)g_hash_table_lookup(metadata, props_key_creator);
      if (g_strcmp0(props_value_creator, generator) == 0) {
        return TRUE;
      }
    }
  }
  return FALSE;
}

static gchar *get_uuid() {
  uuid_t uuid;
  gchar str[36];

  uuid_generate(uuid);
  uuid_unparse(uuid, str);
  return g_strdup(str);
}

/* Create icc profile with Little CMS, alternative */
cmsHPROFILE cdutils_create_brightness_profile_lcms(double brightness) {
  cmsHPROFILE hsRGB;
  cmsContext context_id;
  char description[20];
  cmsMLU *mlu;

  hsRGB = cmsCreate_sRGBProfile();
  context_id = cmsCreateContext(NULL, NULL);

  /* description */
  mlu = cmsMLUalloc(NULL, 1);
  snprintf(description, 20, "Brightness %0.2f", brightness);
  cmsMLUsetASCII(mlu, "en", "US", description);
  cmsWriteTag(hsRGB, cmsSigProfileDescriptionTag, mlu);
  cmsMLUfree(mlu);

  /* metadata */
  // cmsDICTentry *hDict = cmsDictAlloc(context_id);
  // cmsDictAddEntry(hDict, L"key", L"value", NULL, NULL);
  // cmsWriteTag(hsRGB, cmsSigMetaTag, hDict);
  // cmsDictFree(hDict);

  /* vcgt */
  /* map the brightness in case it is too dark */
  double curve[] = {1.0, brightness, 0.0}; // gamma, a, b for (a X +b)^gamma
  cmsToneCurve *tone_curve[3] = {
      cmsBuildParametricToneCurve(context_id, 2, curve),
      cmsBuildParametricToneCurve(context_id, 2, curve),
      cmsBuildParametricToneCurve(context_id, 2, curve),
  };
  cmsWriteTag(hsRGB, cmsSigVcgtTag, tone_curve);
  cmsFreeToneCurve(tone_curve[0]);
  cmsFreeToneCurve(tone_curve[1]);
  cmsFreeToneCurve(tone_curve[2]);

  return hsRGB;
}

/* Create vcgt data for icc */
static GPtrArray *cdutils_create_vcgt(double brightness) {
  GPtrArray *array = cd_color_rgb_array_new();
  CdColorRGB *data = NULL;
  double smin = 0, smax = 1, dmin = 0, dmax = 1;

  dmax = brightness;

  int size = 256;
  /* create array */
  // array = g_ptr_array_new_with_free_func((GDestroyNotify)cd_color_rgb_free);
  for (int i = 0; i < size; i++) {
    data = cd_color_rgb_new();
    float in = (gdouble)i / (gdouble)(size - 1);
    // (x - smin) / (smax - smin) * (brightness - dmin) + dmin
    float toneCurveVal = (in - smin) / (smax - smin) * (dmax - dmin) + dmin;
    // printf("in: %f %f\n", in, toneCurveVal);
    cd_color_rgb_set(data, toneCurveVal, toneCurveVal, toneCurveVal);
    g_ptr_array_add(array, data);
  }

  return array;
}

/* Create icc profile with colord */
CdIcc *cdutils_create_brightness_profile_colord(double brightness,
                                                GError *error) {
  CdIcc *icc = cd_icc_new();
  char description[20];
  gpointer context = cd_icc_get_context(icc);
  cmsHPROFILE hsRGB = cmsCreate_sRGBProfileTHR(context);
  if (!cd_icc_load_handle(icc, hsRGB, CD_ICC_LOAD_FLAGS_NONE, &error)) {
    return NULL;
  }

  if (!cd_icc_set_vcgt(icc, cdutils_create_vcgt(brightness), &error)) {
    return NULL;
  }
  sprintf(description, "Brightness %0.2f", brightness);
  cd_icc_set_description(icc, NULL, description);

  return icc;
}

/* Make sure you have connected to device before you show it */
static void cdutils_show_device(CdDevice *device) {
  if (device == NULL) {
    printf("cdutils_show_device: device is null\n");
    return;
  }

  printf("id: %s\n", cd_device_get_id(device));
}

/* Make sure you have connected to profile before you show it */
static void cdutils_show_profile(CdProfile *profile) {
  if (profile == NULL) {
    printf("cdutils_show_profile: profile is null\n");
    return;
  }

  printf("id: %s\n", cd_profile_get_id(profile));
  printf("title: %s\n", cd_profile_get_title(profile));
  printf("filename: %s\n", cd_profile_get_filename(profile));
  printf("has vcgt: %s\n", cd_profile_get_has_vcgt(profile) ? "true" : "false");
  printf("created by us: %s\n",
         cdutils_is_profile_created_by_us(profile) ? "true" : "false");
}

static CdUtilConnection *cdutils_create_connection(GError **error) {
  CdUtilConnection *connection = NULL;
  CdClient *client = NULL;
  GPtrArray *devices = NULL;

  /* Connecto to colord */
  client = cd_client_new();
  if (!cd_client_connect_sync(client, NULL, error)) {
    printf("cannot connect to colord\n");
    goto out;
  }

  /* Get all display devices */
  devices = cd_client_get_devices_by_kind_sync(client, CD_DEVICE_KIND_DISPLAY,
                                               NULL, error);
  if (devices == NULL) {
    goto out;
  }

  connection = malloc(sizeof(CdUtilConnection));
  connection->client = client;
  connection->devices = devices;

  return connection;

out:
  if (*error != NULL) {
    printf("error: %s\n", (*error)->message);
    g_error_free(*error);
  }

  if (client != NULL) {
    g_object_unref(client);
  }

  if (devices != NULL) {
    g_ptr_array_unref(devices);
  }

  return NULL;
}

/*
1. Connect to colord
2. Get all display devices
3. Connect to first display device
4. Create new icc
5. Save icc file
6. Create profile with icc
7. Device add profile
8. Device make profile default
9. Remove previous profile
 */
static gboolean cdutils_icc_change_brightness(double brightness,
                                              CdObjectScope cdObjectScope) {
  GError *error = NULL;
  CdUtilConnection *connection;
  CdDevice *device = NULL;
  CdProfile *default_profile = NULL;
  CdProfile *new_profile = NULL;
  gboolean ret = FALSE;    // stores returned value by other functions
  gboolean retVal = FALSE; // the value this function returns
  char *uid;
  char filepath[255];
  gchar filename[255];
  g_autoptr(GHashTable) profile_props = NULL;

  connection = cdutils_create_connection(&error);
  if (connection == NULL) {
    goto out;
  }

  /* Connect to first display device */
  device = g_ptr_array_index(connection->devices, 0);
  if (!cd_device_connect_sync(device, NULL, &error)) {
    goto out;
  }

  /* Show first display */
  printf("========== Show current device ==========\n");
  cdutils_show_device(device);

  default_profile = cd_device_get_default_profile(device);

  /* Show current default profile */
  // if (default_profile != NULL) {
  //   printf("get default profile success\n");
  //   if (cd_profile_connect_sync(default_profile, NULL, &error)) {
  //     cdutils_show_profile(default_profile);
  //   }
  // }

  /* Create new profile */
  printf("========== Creating New profile ==========\n");

  uid = get_uuid();
  sprintf(filename, "brightness-%0.2f-%s", brightness, uid);
  sprintf(filepath, "/tmp/icc-brightness/%s", strdup(filename));

  // printf("uid: %s\n", uid);
  // printf("filename: %s\n", filename);
  // printf("filepth: %s\n", filepath);

  /* create profile with colord */
  ret = cd_icc_save_file(
      cdutils_create_brightness_profile_colord(brightness, error),
      g_file_new_for_path(filepath), CD_ICC_SAVE_FLAGS_NONE, NULL, &error);

  /* create profile with Little CMS, alternative */
  // ret =
  // cmsSaveProfileToFile(cdutils_create_brightness_profile_lcms(brightness),
  //                            filepath);

  if (ret) {
    printf("save icc success\n");
  } else {
    printf("save icc fail\n");
    goto out;
  }

  /* Create new profile by cd_client_create_profile_sync */
  profile_props = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, NULL);
  g_hash_table_insert(profile_props, (gpointer)CD_PROFILE_PROPERTY_FILENAME,
                      filepath);
  g_hash_table_insert(profile_props, (gpointer)props_key_creator,
                      (gpointer)props_value_creator);
  g_hash_table_insert(profile_props, (gpointer) "Profile brightness",
                      (gpointer)g_strdup_printf("%0.2f", brightness));

  new_profile = cd_client_create_profile_sync(
      connection->client, filename, cdObjectScope, profile_props, NULL, &error);

  /* Device add profile and make profile default */
  if (CD_IS_PROFILE(new_profile)) {
    printf("create new profile success\n");
    if (cd_device_add_profile_sync(device, CD_DEVICE_RELATION_HARD, new_profile,
                                   NULL, &error)) {
      printf("device add new_profile success\n");
    }

    if (cd_device_make_profile_default_sync(device, new_profile, NULL,
                                            &error)) {
      printf("device make new_profile default success\n");
    }
  }

  /* Delete our previous profile */
  if (CD_IS_PROFILE(default_profile)) {
    if (cd_profile_connect_sync(default_profile, NULL, &error)) {
      if (cdutils_is_profile_created_by_us(default_profile)) {
        if (cd_client_delete_profile_sync(connection->client, default_profile,
                                          NULL, &error)) {
          printf("delete previous profile succes\n");
        }
        const gchar *previous_profile_filename = NULL;
        previous_profile_filename = cd_profile_get_filename(default_profile);
        if (remove(previous_profile_filename) == 0) {
          printf("delete previous icc file success\n");
        }
      }
    }
  }

  /* Print current default profile */
  printf("========== Show current default profile ==========\n");
  default_profile = cd_device_get_default_profile(device);
  if (default_profile != NULL) {
    if (cd_profile_connect_sync(default_profile, NULL, &error)) {
      cdutils_show_profile(default_profile);
    }
  } else {
    printf("(no default profile)");
  }

  retVal = TRUE;
  goto out;

out:
  if (error != NULL) {
    printf("error: %s\n", error->message);
    g_error_free(error);
  }

  if (connection->client != NULL) {
    g_object_unref(connection->client);
  }

  if (connection->devices != NULL) {
    g_ptr_array_unref(connection->devices);
  }

  if (default_profile != NULL) {
    g_object_unref(default_profile);
  }

  if (new_profile != NULL) {
    g_object_unref(new_profile);
  }

  return retVal;
}

static gboolean cdutils_list_devices(GError **error) {
  gboolean retVal = FALSE;
  CdUtilConnection *connection = cdutils_create_connection(error);

  if (connection == NULL) {
    goto out;
  }

  for (guint i = 0; i < connection->devices->len; i++) {
    CdDevice *device = g_ptr_array_index(connection->devices, i);
    if (cd_device_connect_sync(device, NULL, error)) {
      cdutils_show_device(device);
    }
  }

  retVal = true;
  return retVal;

out:
  return retVal;
}
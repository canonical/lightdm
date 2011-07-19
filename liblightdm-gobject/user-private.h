#ifndef _LDM_USER_PRIVATE_H_
#define _LDM_USER_PRIVATE_H_

#include "lightdm/greeter.h"
#include "lightdm/user.h"

LightDMUser *lightdm_user_new (LightDMGreeter *greeter, const gchar *name, const gchar *real_name, const gchar *home_directory, const gchar *image, gboolean logged_in);

gboolean lightdm_user_update (LightDMUser *user, const gchar *real_name, const gchar *home_directory, const gchar *image, gboolean logged_in);

void lightdm_user_set_name (LightDMUser *user, const gchar *name);

void lightdm_user_set_real_name (LightDMUser *user, const gchar *real_name);

void lightdm_user_set_home_directory (LightDMUser *user, const gchar *home_directory);

void lightdm_user_set_image (LightDMUser *user, const gchar *image);

void lightdm_user_set_logged_in (LightDMUser *user, gboolean logged_in);

#endif /* _LDM_USER_PRIVATE_H_ */

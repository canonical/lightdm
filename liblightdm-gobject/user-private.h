#ifndef _LDM_USER_PRIVATE_H_
#define _LDM_USER_PRIVATE_H_

#include "lightdm/greeter.h"
#include "lightdm/user.h"

LdmUser *ldm_user_new (LdmGreeter *greeter, const gchar *name, const gchar *real_name, const gchar *home_directory, const gchar *image, gboolean logged_in);

void ldm_user_set_name (LdmUser *user, const gchar *name);

void ldm_user_set_real_name (LdmUser *user, const gchar *real_name);

void ldm_user_set_home_directory (LdmUser *user, const gchar *home_directory);

void ldm_user_set_image (LdmUser *user, const gchar *image);

void ldm_user_set_logged_in (LdmUser *user, gboolean logged_in);

#endif /* _LDM_USER_PRIVATE_H_ */

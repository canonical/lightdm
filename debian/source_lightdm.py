import os
import re

from apport.hookutils import *

def add_info(report, ui):

    if ui:
        display_manager_files = {}
        if os.path.lexists('/var/log/lightdm'):
            display_manager_files['LightdmLog'] = \
                'cat /var/log/lightdm/lightdm.log'
            display_manager_files['LightdmDisplayLog'] = \
                'cat /var/log/lightdm/x-0.log'
            display_manager_files['LightdmGreeterLog'] = \
                 'cat /var/log/lightdm/x-0-greeter.log'
            display_manager_files['LightdmGreeterLogOld'] = \
                 'cat /var/log/lightdm/x-0-greeter.log.old'
            display_manager_files['LightdmConfig'] = \
                'cat /etc/lightdm/lightdm.conf'
            display_manager_files['LightdmUsersConfig'] = \
                'cat /etc/lightdm/users.conf'

        if ui.yesno("Your display manager log files may help developers"\
                    " diagnose the bug, but may contain sensitive information"\
                    " such as your hostname or username.  Do you want to"\
                    " include these logs in your bug report?") == True:
            attach_root_command_outputs(report, display_manager_files)

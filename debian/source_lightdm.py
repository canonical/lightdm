import os
import re

import apport.packaging
import apport.hookutils

def add_info(report, ui):

    report["Log"] = apport.hookutils.read_file("/var/log/lightdm/:0.log")
    report["LightdmLog"] = apport.hookutils.read_file("/var/log/lightdm/lightdm.log")
    report["GreeterLog"] = apport.hookutils.read_file("/var/log/lightdm/:0-greeter.log")
    report["XorgLog"] = apport.hookutils.read_file("/var/log/Xorg.0.log")


#!/usr/bin/python

import os
import sys
import shutil
import platform
import subprocess

def call( cmdline ):
    p = subprocess.Popen( cmdline.split(), stderr=subprocess.PIPE )
    (out, err) = p.communicate()
    if not p.returncode == 0:
        print ("%s failed, returned %d" % (cmdline[0], p.returncode))
        exit( p.returncode )

def install_apk( apk_path ):
    if not os.path.exists(apk_path):
        print ("%s does not exist, exiting" % apk_path )
        exit( 1 )
    installcmd = "adb install -r " + apk_path
    print("Executing: %s " % installcmd)
    call( installcmd )
        
def sdk_install_apks():
    root_dir='./'
    for root, dirs, files in os.walk(root_dir):
        for name in files:
            if name.endswith('.apk'):
                path=os.path.join(root,name)
                install_apk(path)
                
    
def sdk_install_media( mediaDirName ):
    if os.path.exists(mediaDirName):
        installcmd = "adb push " + mediaDirName + " /sdcard/"
        print("Executing: %s " % installcmd)
        call( installcmd )

if __name__ == '__main__':
    sdk_install_apks()
    sdk_install_media( 'sdcard_SDK' )

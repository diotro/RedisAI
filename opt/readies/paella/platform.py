
from __future__ import absolute_import
import platform
import os
import re

#----------------------------------------------------------------------------------------------

class Platform:
    #------------------------------------------------------------------------------------------
    class OSRelease():
        def __init__(self):
            self.defs = {}
            with open("/etc/os-release") as f:
                for line in f:
                    try:
                        k, v = line.rstrip().split("=")
                        self.defs[k] = v.strip('"').strip("'")
                    except:
                        pass

        def distname(self):
            return self.defs["ID"].lower()

        def version(self):
            return self.defs["VERSION_ID"]

        def osnick(self):
            try:
                return self.defs["VERSION_CODENAME"]
            except:
                return ""

    #------------------------------------------------------------------------------------------

    def __init__(self, strict=False):
        self.os = self.dist = self.os_ver = self.full_os_ver = self.osnick = self.arch = '?'

        # self.os = os.getenv("READIES_PLATFORM_OS", '?')
        # self.dist = os.getenv("READIES_PLATFORM_DIST", '?')
        # self.os_ver = os.getenv("READIES_PLATFORM_OS_VER", '?')
        # self.full_os_ver = os.getenv("READIES_PLATFORM_FULL_OS_VER", '?')
        # self.osnick = os.getenv("READIES_PLATFORM_OSNICK", '?')
        # self.arch = os.getenv("READIES_PLATFORM_ARCH", '?')

        self.os = platform.system().lower()
        if self.os == 'linux':
            if False:
                dist = platform.linux_distribution()
                distname = dist[0].lower()
                self.os_ver = self.full_os_ver = dist[1]
            else:
                try:
                    os_release = Platform.OSRelease()
                    distname = os_release.distname()
                    self.os_ver = self.full_os_ver = os_release.version()
                    self.osnick = os_release.osnick()
                    if self.osnick == "":
                        self.osnick = distname + str(self.os_ver)
                except:
                    if strict:
                        assert(False), "Cannot determine distribution"
                    distname = 'unknown'
                    self.os_ver = self.full_os_ver = 'unknown'
            if distname == 'fedora' or distname == 'ubuntu' or  distname == 'debian' or distname == 'arch':
                pass
            elif distname.startswith('centos'):
                distname = 'centos'
            elif distname.startswith('redhat') or distname == 'rhel':
                distname = 'redhat'
            elif distname.startswith('suse'):
                distname = 'suse'
            elif distname.startswith('amzn'):
                distname = 'amzn'
                self.osnick = 'amzn' + str(os_release.version())
            else:
                if strict:
                    assert(False), "Cannot determine distribution"
            self.dist = distname
        elif self.os == 'darwin':
            self.os = 'macosx'
            self.dist = ''
            mac_ver = platform.mac_ver()
            self.full_os_ver = mac_ver[0] # e.g. 10.14, but also 10.5.8
            self.os_ver = '.'.join(self.full_os_ver.split('.')[:2]) # major.minor
            # self.arch = mac_ver[2] # e.g. x64_64
            self.osnick = self.os + str(self.full_os_ver.split('.')[1])
            nicks = {
                "10.15": "catalina",
                "10.14": "mojave",
                "10.13": "highsierra",
                "10.12": "sierra",
                "10.11": "elcapitan",
                "10.10": "yosemite",
                "10.9": "mavericks",
                "10.8": "mountainlion",
                "10.7": "lion",
                "10.6": "snowleopard",
                "10.5": "leopard",
                "10.4": "tiger",
                "10.3": "panther",
                "10.2": "jaguar",
                "10.1": "puma",
                "10.0": "cheetah"
            }
            if self.os_ver in nicks:
                self.osnick = nicks[self.os_ver]
        elif self.os == 'windows':
            self.dist = self.os
            self.os_ver = platform.release()
            self.full_os_ver = os.version()
        elif self.os == 'sunos':
            self.os = 'solaris'
            self.os_ver = ''
            self.dist = ''
        elif self.os == 'freebsd':
            self.dist = ''
            ver = sh('freebsd-version')
            m = re.search(r'([^-]*)-(.*)', ver)
            self.os_ver = self.full_os_ver = m.group(1)
            self.osnick = self.os + self.os_ver
        else:
            if strict:
                assert(False), "Cannot determine OS"
            self.os_ver = ''
            self.dist = ''

        self.arch = platform.machine().lower()
        if self.arch == 'amd64' or self.arch == 'x86_64':
            self.arch = 'x64'
        elif self.arch == 'i386' or self.arch == 'i686' or self.arch == 'i86pc':
            self.arch = 'x86'
        elif self.arch == 'aarch64':
            self.arch = 'arm64v8'
        elif self.arch == 'armv7l':
            self.arch = 'arm32v7'

    def is_debian_compat(self):
        return self.dist == 'debian' or self.dist == 'ubuntu' or self.dist == 'linuxmint'

    def is_redhat_compat(self):
        return self.dist == 'redhat' or self.dist == 'centos' or self.dist == 'amzn'

    def is_container(self):
        with open('/proc/1/cgroups', 'r') as conf:
            for line in conf:
                if re.search('docker', line):
                    return True
        return False

    def report(self):
        print("This system is " + self.distname + " " + self.distver + ".\n")

#----------------------------------------------------------------------------------------------

class OnPlatform:
    def __init__(self):
        self.stages = [0]
        self.platform = Platform()

    def invoke(self):
        os = self.os = self.platform.os
        dist = self.dist = self.platform.dist
        self.ver = self.platform.os_ver
        self.common_first()

        for stage in self.stages:
            self.stage = stage
            self.common()
            if os == 'linux':
                self.linux()

                if self.platform.is_debian_compat():
                    self.debian_compat()
                if self.platform.is_redhat_compat():
                    self.redhat_compat()

                if dist == 'fedora':
                    self.fedora()
                elif dist == 'ubuntu':
                    self.ubuntu()
                elif dist == 'debian':
                    self.debian()
                elif dist == 'centos':
                    self.centos()
                elif dist == 'redhat':
                    self.redhat()
                elif dist == 'suse':
                    self.suse()
                elif dist == 'arch':
                    self.arch()
                elif dist == 'linuxmint':
                    self.linuxmint()
                elif dist == 'amzn':
                    self.amzn()
                else:
                    assert(False), "Cannot determine installer"
            elif os == 'macosx':
                self.macosx()
            elif os == 'freebsd':
                self.freebsd()

        self.common_last()

    def common(self):
        pass

    def common_first(self):
        pass

    def common_last(self):
        pass

    def linux(self):
        pass

    def arch(self):
        pass

    def debian_compat(self): # debian, ubuntu, etc
        pass

    def debian(self):
        pass

    def centos(self):
        pass

    def fedora(self):
        pass

    def redhat_compat(self): # centos, rhel, amzn, etc
        pass

    def redhat(self):
        pass

    def ubuntu(self):
        pass

    def suse(self):
        pass

    def macosx(self):
        pass

    def windows(self):
        pass

    def bsd_compat(self):
        pass

    def freebsd(self):
        pass

    def linuxmint(self):
        pass

    def amzn(self):
        pass

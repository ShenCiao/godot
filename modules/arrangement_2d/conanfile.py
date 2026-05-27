from conan import ConanFile
from conan.tools.scons import SConsDeps


class CGALConsumerConan(ConanFile):
    generators = "SConsDeps"

    def requirements(self):
        self.requires("cgal/6.1")

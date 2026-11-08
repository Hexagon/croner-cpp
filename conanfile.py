from conan import ConanFile
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import cmake_layout
from conan.tools.files import copy
import os


class CronerConan(ConanFile):
    name = "croner"
    version = "0.2.0"
    license = "MIT"
    url = "https://github.com/Hexagon/croner-cpp"
    homepage = "https://croner.56k.guru"
    description = ("Cron expression parser and scheduler with time zone and DST support. "
                   "Header-only, C++17, no dependencies.")
    topics = ("cron", "scheduler", "crontab", "timezone", "header-only")
    package_type = "header-library"
    settings = "os", "arch", "compiler", "build_type"
    exports_sources = "include/*", "CMakeLists.txt", "cmake/*", "LICENSE", "tests/*", "examples/*"
    no_copy_source = True

    def layout(self):
        cmake_layout(self)

    def validate(self):
        check_min_cppstd(self, 17)

    def package_id(self):
        self.info.clear()

    def package(self):
        copy(self, "LICENSE", self.source_folder, os.path.join(self.package_folder, "licenses"))
        copy(self, "*.hpp", os.path.join(self.source_folder, "include"),
             os.path.join(self.package_folder, "include"))

    def package_info(self):
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []
        self.cpp_info.set_property("cmake_file_name", "croner")
        self.cpp_info.set_property("cmake_target_name", "croner::croner")
        if self.settings.os in ("Linux", "FreeBSD"):
            self.cpp_info.system_libs = ["pthread"]

from conans import CMake, ConanFile, tools


class RamulatorConan(ConanFile):
    name = "ramulator"
    version = "2.0.0"
    description = "A modern, modular, and extensible cycle-accurate DRAM simulator"
    url = "https://github.com/CMU-SAFARI/ramulator2"
    license = "MIT"

    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}
    generators = "cmake"

    requires = (
    )

    exports_sources = (
        "include/*",
        "src/*",
        "test/*",
        "CMakeLists.txt",
        "compose-dev.yaml",
        "example_config.yaml",
        "example_inst.trace",
        "ext/*",
        "perf_comparison",
        "rh_study",
        "verilog_verification",
        "LICENSE",
        "README.md",
    )
    no_copy_source = True
    generators = "cmake", "cmake_find_package"

    __cmake = None

    @property
    def _cmake(self):
        if self.__cmake is None:
            self.__cmake = CMake(self)
        return self.__cmake

    def build(self):
        self._cmake.configure(source_dir=self.source_folder)
        self._cmake.build()

    def package(self):
        self._cmake.install()

    def package_id(self):
        self.info.shared_library_package_id()

    def package_info(self):
        self.cpp_info.names["cmake_find_package"] = "ramulator"
        self.cpp_info.components["libramulator"].names["cmake_find_package"] = "ramulator"
        self.cpp_info.components["libramulator"].libs = ["ramulator"]

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
    generators = "cmake", "cmake_find_package"

    requires = (
        "argparse/2.9",
        "fmt/10.1.0",
        "spdlog/1.12.0",
        "yaml-cpp/0.8.0",
    )

    exports_sources = (
        "CMakeLists.txt",
        "LICENSE",
        "README.md",
        "compose-dev.yaml",
        "conan.cmake",
        "example_config.yaml",
        "example_inst.trace",
        "ext/*",
        "include/*",
        "perf_comparison",
        "rh_study",
        "src/*",
        "test/*",
        "verilog_verification",
    )
    no_copy_source = True

    __cmake = None

    @property
    def _cmake(self):
        if self.__cmake is None:
            self.__cmake = CMake(self)
        return self.__cmake

    def build(self):
        self._cmake.generator="Ninja"
        self._cmake.configure(source_dir=self.source_folder)
        self._cmake.build()

    def package(self):
        self._cmake.install()

    def package_id(self):
        self.info.shared_library_package_id()

    def package_info(self):
        self.cpp_info.set_property("cmake_find_mode", "both")
        self.cpp_info.set_property("cmake_file_name", "ramulator")

    def package_info(self):
        self.cpp_info.names["cmake_find_package"] = "ramulator"
        self.cpp_info.components["libramulator"].names["cmake_find_package"] = "ramulator"
        self.cpp_info.components["libramulator"].libs = ["ramulator"]

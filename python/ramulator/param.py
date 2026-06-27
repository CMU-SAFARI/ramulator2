"""Typed parameter descriptors for Ramulator component configuration."""


class Param:
    """Descriptor for a typed component parameter.

    Usage in a Component subclass:
        clock_ratio = Param(int, required=True)
        ipc = Param(int, default=4)
        max_addr = Param(int, required=True, cpp_type="Addr_t")
    """

    _TYPE_MAP = {
        int: "int",
        float: "float",
        str: "std::string",
        bool: "bool",
        list: "std::vector<std::string>",
    }

    def __init__(self, type, *, required=False, default=None, cpp_type=None):
        self.type = type
        self.required = required
        self.default = default
        self.cpp_type = cpp_type or self._TYPE_MAP.get(type, "int")


class Child:
    """Slot for a sub-component.

    config_key is the key name used in the C++ config dict.

    Usage in a Component subclass:
        translation = Child("Translation")
    """

    def __init__(self, config_key):
        self.config_key = config_key


class ChildList:
    """Slot for a list of sub-components of the same interface type.

    Usage in a Component subclass:
        controllers = ChildList("controller")
    """

    def __init__(self, config_key):
        self.config_key = config_key
        self.config_key_plural = config_key + "s"

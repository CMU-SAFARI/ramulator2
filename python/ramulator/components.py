"""Base class for all Ramulator components."""

from .param import Child, ChildList, Param


def _get_descriptors(cls):
    """Collect all Param, Child, and ChildList descriptors from the class hierarchy."""
    descriptors = {}
    for klass in reversed(cls.__mro__):
        for name, obj in klass.__dict__.items():
            if isinstance(obj, (Param, Child, ChildList)):
                descriptors[name] = obj
    return descriptors


class Component:
    """Base class for Ramulator components.

    Subclasses define their configuration schema via Param and Child
    class attributes. Instances hold concrete values that serialize
    to a config dict for the C++ backend.

    Example:
        class MyComponent(Component):
            impl = "MyImpl"
            clock_ratio = Param(int, required=True)
            child = Child("ChildSection")
    """

    impl = None  # C++ implementation name — set by each subclass

    def __init__(self, **kwargs):
        descriptors = _get_descriptors(type(self))

        for name, desc in descriptors.items():
            if isinstance(desc, Param):
                if name in kwargs:
                    setattr(self, name, kwargs.pop(name))
                elif desc.default is not None:
                    setattr(self, name, desc.default)
                elif desc.required:
                    raise ValueError(f"{type(self).__name__}: parameter '{name}' is required")
            elif isinstance(desc, Child):
                if name in kwargs:
                    setattr(self, name, kwargs.pop(name))
            elif isinstance(desc, ChildList):
                if name in kwargs:
                    setattr(self, name, kwargs.pop(name))

        if kwargs:
            raise ValueError(f"{type(self).__name__}: unknown parameters {list(kwargs)}")

    def to_config(self):
        """Serialize this component to a dict for C++ ConfigNode consumption."""
        result = {}
        if self.impl is not None:
            result["impl"] = self.impl

        descriptors = _get_descriptors(type(self))
        for name, desc in descriptors.items():
            if isinstance(desc, Param) and hasattr(self, name):
                val = getattr(self, name)
                # Recursively serialize Component values (e.g., org/timing objects)
                if isinstance(val, Component):
                    val = val.to_config()
                result[name] = val
            elif isinstance(desc, Child) and hasattr(self, name):
                child = getattr(self, name)
                if isinstance(child, Component):
                    result[desc.config_key] = child.to_config()
                else:
                    result[desc.config_key] = child
            elif isinstance(desc, ChildList):
                children = getattr(self, name, None)
                if isinstance(children, list):
                    result[desc.config_key_plural] = [
                        c.to_config() if isinstance(c, Component) else c for c in children
                    ]

        return result

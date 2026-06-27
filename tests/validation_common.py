"""Private helpers shared by device_timings and controller_scheduling."""


def _request_type_ids(dram):
    names = list(type(dram).supported_requests.keys())
    return {name: idx for idx, name in enumerate(names)}


def _metadata_from_dram(dram, cpp_obj):
    raw_org, raw_timings = dram.resolve()
    tick_multiplier = getattr(type(dram), "tick_multiplier", 1)
    return {
        "level_names": list(cpp_obj.level_names),
        "command_names": list(cpp_obj.command_names),
        "timings": dict(cpp_obj.timings),
        "org": raw_org,
        "raw_timings": raw_timings,
        "tick_multiplier": tick_multiplier,
        "time_unit_ns": raw_timings["tCK_ps"] / 1000.0 / tick_multiplier,
    }


def build_addr_vec(level_names, *, wildcard, **levels) -> list[int]:
    names = list(level_names)
    unknown = sorted(set(levels) - set(names))
    if unknown:
        raise ValueError(f"Unknown addr_vec levels: {unknown}")

    vec = [0] * len(names)
    for idx, name in enumerate(names):
        if name in levels:
            value = levels[name]
            if not isinstance(value, int):
                raise TypeError(f"addr_vec level '{name}' expects an int, got {type(value).__name__}")
            if value < 0 and value != wildcard:
                raise ValueError(
                    f"addr_vec level '{name}' expects a non-negative int or dut.ALL, got {value}"
                )
            vec[idx] = value
    return vec

"""Core DSL for defining DRAM standards.

Each DRAM standard (DDR4, DDR5, HBM3, ...) is defined as a subclass of
DRAMStandard using plain Python data structures:

    levels           — dict[str, str]: level name → init state (e.g. {"Bank": "Closed"})
    commands         — list[str]: command names (e.g. ["ACT", "PREpb", "RD", ...])
    states           — list[str]: device states (e.g. ["Opened", "Closed", "N_A"])
    timing_params    — list[str]: timing parameter names
    timing_constraints — list[TimingConstraint]
    org_presets      — dict[str, dict]: org preset name → {density, dq, level counts}

Example:
    class DDR4(DRAMStandard):
        name = "DDR4"
        levels = {"Channel": "N_A", "Rank": "N_A", "BankGroup": "N_A",
                  "Bank": "Closed", "Row": "Closed", "Column": "N_A"}
        commands = ["ACT", "PREpb", "PREab", "RD", "WR", "RDA", "WRA", "REFab"]
        states = ["Opened", "Closed", "N_A"]
        ...
"""

import re

from ramulator.components import Component
from ramulator.param import Param


class TimingConstraint:
    """A timing constraint between commands at a specific hierarchy level.

    The latency is an expression string using timing parameter names,
    e.g. "nCL + nBL + 2 - nCWL". It is evaluated by Python at config time
    using the resolved timing values.
    """

    def __init__(
        self,
        level: str,
        preceding: list,
        following: list,
        latency: str,
        window: int = 1,
        sibling: bool = False,
    ):
        self.level = level
        self.preceding = list(preceding)
        self.following = list(following)
        self.latency = latency
        self.window = window
        self.sibling = sibling

    def __repr__(self):
        args = [repr(self.level), repr(self.preceding), repr(self.following), repr(self.latency)]
        if self.window != 1:
            args.append(f"window={self.window}")
        if self.sibling:
            args.append("sibling=True")
        return f"TimingConstraint({', '.join(args)})"


class DRAMStandard(Component):
    """Base class for all DRAM standards.

    Subclasses declare structure as class attributes (read by codegen)
    and presets for runtime config (read by to_config).
    """

    # ---- Class-level: structural (read by codegen) ----
    name = None  # type: str  — C++ class name, e.g. "DDR4"
    levels = {}  # type: dict[str, str]  — level name → init state
    commands = []  # type: list[str]  — command names
    states = []  # type: list[str]  — state names
    timing_params = []  # type: list[str]
    supported_requests = {}  # type: dict[str, str]
    timing_constraints = []  # type: list[TimingConstraint]
    command_cycles = {}  # type: dict[str, float] — CA bus cycles per command (default 1 CK)
    tick_multiplier = 1  # ticks per CK cycle (e.g. 2 for HBM3 half-CK ticks)
    internal_prefetch_size = 8
    data_payload_bytes = None
    read_latency = "nCL + nBL"
    row_commands = []  # type: list[str]  — commands on the row bus (dual-bus standards)
    column_commands = []  # type: list[str]  — commands on the column bus (dual-bus standards)

    # ---- Class-level: presets ----
    org_presets = {}  # type: dict[str, dict]
    timing_presets = {}  # type: dict[str, dict]

    # ---- Instance config ----
    org_preset = Param(str, required=True)
    timing_preset = Param(str, required=True)

    # ---- Auto-registration for codegen discovery ----
    _registry = {}

    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__(**kwargs)
        if isinstance(getattr(cls, "name", None), str):
            DRAMStandard._registry[cls.name] = cls

    def __init__(self, *, org_preset, timing_preset, **overrides):
        super().__init__(org_preset=org_preset, timing_preset=timing_preset)
        self._overrides = overrides

    @classmethod
    def resolve_secondary_timings(cls, timing_dict, org_dict):
        """Fill derived timing values in-place. Subclasses override."""
        pass

    def resolve(self):
        """Resolve preset names + overrides into (org_dict, timing_dict)."""
        cls = type(self)

        if self.org_preset not in cls.org_presets:
            raise ValueError(f"Unknown {cls.name} org preset: {self.org_preset}")
        if self.timing_preset not in cls.timing_presets:
            raise ValueError(f"Unknown {cls.name} timing preset: {self.timing_preset}")

        org_dict = dict(cls.org_presets[self.org_preset])

        # Apply org-level overrides (e.g. rank=2)
        level_names = {name.lower() for name in cls.levels}
        overrides = dict(self._overrides)
        if "channel" in overrides:
            raise ValueError(
                f"{cls.name}: 'channel' cannot be overridden — "
                f"multi-channel is configured at the system level, "
                f"not in the DRAM spec."
            )
        # channel_width is an org-level param (alongside dq)
        org_names = level_names | {"channel_width"}
        for name in list(overrides):
            if name in org_names:
                org_dict[name] = overrides.pop(name)

        # Build timing dict from preset
        timing_dict = dict(cls.timing_presets[self.timing_preset])

        # Resolve secondary timings
        cls.resolve_secondary_timings(timing_dict, org_dict)

        # Apply timing overrides
        for k, v in overrides.items():
            if k in timing_dict:
                timing_dict[k] = v
            else:
                raise ValueError(f"Unknown {cls.name} override: {k}")

        return org_dict, timing_dict

    def to_config(self):
        """Serialize with ALL values pre-computed — C++ just parses data."""
        cls = type(self)
        org_dict, timing_dict = self.resolve()

        # Validate no unresolved timings
        unresolved = [k for k, v in timing_dict.items() if v == -1]
        if unresolved:
            raise ValueError(
                f"{cls.name}: unresolved timings {unresolved}. Set them explicitly via overrides."
            )

        # Validate channel_width
        cw = org_dict["channel_width"]
        dq = org_dict["dq"]
        if cw <= 0:
            raise ValueError(f"{cls.name}: channel_width must be positive, got {cw}")
        if cls.data_payload_bytes is None and (cw & (cw - 1)) != 0:
            raise ValueError(f"{cls.name}: channel_width must be a positive power of 2, got {cw}")
        if cw % dq != 0:
            raise ValueError(f"{cls.name}: channel_width ({cw}) must be a multiple of dq ({dq})")
        if cls.data_payload_bytes is not None and cls.data_payload_bytes <= 0:
            raise ValueError(f"{cls.name}: data_payload_bytes must be positive, got {cls.data_payload_bytes}")

        # ---- Single-place CK → tick conversion ----
        # All Python-facing values (presets, command_cycles, constraint
        # expressions) are in CK units.  When tick_multiplier > 1 (e.g. HBM3
        # half-CK ticks), scale timing_dict here so every downstream consumer
        # (expressions, serialization, read_latency) works in ticks unchanged.
        tick_mult = cls.tick_multiplier
        if tick_mult != 1:
            for k in list(timing_dict):
                if k == "rate":
                    pass
                elif k == "tCK_ps":
                    timing_dict[k] //= tick_mult
                else:
                    timing_dict[k] = int(timing_dict[k] * tick_mult)
        cmd_cycles = {cmd: int(c * tick_mult) for cmd, c in cls.command_cycles.items()}

        # Build index lookups
        level_idx = {name: i for i, name in enumerate(cls.levels)}
        cmd_idx = {c: i for i, c in enumerate(cls.commands)}

        # Pre-compute timing constraints with multi-cycle command adjustment.
        # JEDEC timings are measured between when the DRAM finishes receiving
        # each command.  In Ramulator we record commands at their 1st cycle,
        # so for a 2-cycle preceding command the DRAM starts 1 cycle later
        # (+1), and for a 2-cycle following command it arrives 1 cycle later
        # (-1).  When both are the same cycle count the offsets cancel.
        constraints = []
        for tc in cls.timing_constraints:
            nominal = cls._eval_expr(tc.latency, timing_dict)
            level = level_idx[tc.level]

            # Group (preceding, following) pairs by adjusted latency
            lat_groups = {}  # adjusted_latency -> ([p_ids], set(f_ids))
            for p_cmd in tc.preceding:
                p_off = cmd_cycles.get(p_cmd, tick_mult) - 1
                for f_cmd in tc.following:
                    f_off = cmd_cycles.get(f_cmd, tick_mult) - 1
                    adjusted = nominal + p_off - f_off
                    if adjusted not in lat_groups:
                        lat_groups[adjusted] = ([], set())
                    p_id = cmd_idx[p_cmd]
                    if p_id not in lat_groups[adjusted][0]:
                        lat_groups[adjusted][0].append(p_id)
                    lat_groups[adjusted][1].add(cmd_idx[f_cmd])

            for latency, (p_ids, f_ids) in lat_groups.items():
                entry = [level, p_ids, sorted(f_ids), latency]
                if tc.window != 1 or tc.sibling:
                    entry.append(tc.window)
                if tc.sibling:
                    entry.append(True)
                constraints.append(entry)

        # Auto-generate bus occupancy constraints from command_cycles + bus
        # classification.  Each command occupies its bus for command_cycles[cmd]
        # CK (default 1 CK), blocking all other commands on the same bus.
        bus = cls._generate_bus_constraints(cmd_idx, cmd_cycles, tick_mult)
        constraints = bus + constraints

        # Validate all required org levels are present
        org_counts = []
        for name in list(cls.levels)[1:]:  # skip Channel
            key = name.lower()
            if key not in org_dict:
                raise ValueError(
                    f"{cls.name}: org preset '{self.org_preset}' missing required level '{name}'"
                )
            org_counts.append(org_dict[key])

        config = {
            "impl": cls.name,
            "org": {
                "dq": org_dict["dq"],
                # Channel count is always 1 — multi-channel is system-level
                "count": [1] + org_counts,
            },
            "timing": [timing_dict[k] for k in cls.timing_params],
            "command_cycles": [cmd_cycles.get(cmd, tick_mult) for cmd in cls.commands],
            "channel_width": org_dict["channel_width"],
            "read_latency": cls._eval_expr(cls.read_latency, timing_dict),
            "timing_constraints": constraints,
        }
        if cls.data_payload_bytes is not None:
            config["data_payload_bytes"] = cls.data_payload_bytes
        return config

    @classmethod
    def _generate_bus_constraints(cls, cmd_idx, cmd_cycles, tick_mult):
        """Generate Channel-level bus occupancy constraints.

        Each command occupies its bus for command_cycles[cmd] CK (default 1).
        For dual-bus standards (row_commands + column_commands set), generates
        separate constraints per bus with no cross-bus blocking.
        For single-bus standards, all commands share one bus.
        Automatically skips commands with 1-tick occupancy (controller already
        serializes at 1 cmd/tick), so standards with no multi-cycle commands
        get no constraints.
        """

        # Determine bus groups
        if cls.row_commands and cls.column_commands:
            buses = [cls.row_commands, cls.column_commands]
        else:
            buses = [cls.commands]

        constraints = []
        for bus_cmds in buses:
            # Group commands by their cycle count (in ticks)
            cycle_groups = {}  # ticks -> [cmd_names]
            for cmd in bus_cmds:
                ticks = cmd_cycles.get(cmd, tick_mult)
                cycle_groups.setdefault(ticks, []).append(cmd)

            all_bus_ids = sorted(cmd_idx[c] for c in bus_cmds)

            for ticks, cmds in cycle_groups.items():
                if ticks == 1:
                    continue  # 1-tick commands: controller already issues one cmd per tick
                p_ids = [cmd_idx[c] for c in cmds]
                constraints.append([0, p_ids, all_bus_ids, ticks])

        return constraints

    @classmethod
    def _eval_expr(cls, expr, timing_dict):
        """Evaluate 'nCL + nBL + 2 - nCWL' using resolved timing values."""

        def replace_param(match):
            name = match.group(0)
            if name in timing_dict:
                return str(timing_dict[name])
            return name

        substituted = re.sub(r"[a-zA-Z_]\w*", replace_param, expr)
        try:
            result = eval(substituted)
        except NameError as e:
            raise ValueError(
                f"{cls.name}: unresolved parameter in expression '{expr}': {e}"
            ) from e
        return int(result) if result == int(result) else result

    @classmethod
    def validate(cls):
        """Check structural consistency. Raises ValueError on problems."""
        state_names = set(cls.states)
        level_names = set(cls.levels.keys())
        cmd_names = set(cls.commands)

        for lv_name, init_state in cls.levels.items():
            if init_state not in state_names:
                raise ValueError(
                    f"Level '{lv_name}' init_state '{init_state}' not in states {state_names}"
                )

        for tc in cls.timing_constraints:
            if tc.level not in level_names:
                raise ValueError(f"TimingConstraint references unknown level '{tc.level}'")
            for c in tc.preceding + tc.following:
                if c not in cmd_names:
                    raise ValueError(f"TimingConstraint references unknown command '{c}'")

        # Validate supported_requests ordering: Read and Write remain the built-in
        # leading request names for controller compatibility.
        req_keys = list(cls.supported_requests.keys())
        if len(req_keys) < 2 or req_keys[:2] != ["Read", "Write"]:
            raise ValueError(
                f"supported_requests must start with 'Read' and 'Write', got {req_keys[:2]}"
            )
        for req_type, cmd_name in cls.supported_requests.items():
            if cmd_name not in cmd_names:
                raise ValueError(
                    f"supported_requests['{req_type}'] -> '{cmd_name}' not in commands"
                )

    def request_type_id(self, name: str) -> int:
        """Return the integer type_id for a named request type.

        Used by Python config to pass named request type IDs to frontends
        without compile-time coupling to the DRAM standard header.
        """
        keys = list(type(self).supported_requests.keys())
        return keys.index(name)

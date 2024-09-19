import m5
import os
import configparser

from m5.objects import Ramulator2, AddrRange, Port, MemCtrl
from m5.util.convert import toMemorySize

from ...utils.override import overrides
from ..boards.abstract_board import AbstractBoard
from .abstract_memory_system import AbstractMemorySystem


from typing import Optional, Tuple, Sequence, List

class Ramulator2MemCtrl(Ramulator2):
    """
    A Ramulator2 Memory Controller.

    The class serves as a SimObject object wrapper, utiliszing the Ramulator2 
    configuratons.
    """

    def __init__(self, config_path: str) -> None:
        """
        :param mem_name: The name of the type  of memory to be configured.
        :param num_chnls: The number of channels.
        """
        super().__init__()
        self.config_path = config_path


class Ramulator2System (AbstractMemorySystem):

    def __init__(self, config_path: str, size: Optional[str]):
        """
        :param mem_name: The name of the type  of memory to be configured.
        :param num_chnls: The number of channels.
        """
        super().__init__()
        self.mem_ctrl = Ramulator2MemCtrl(config_path)
        self._size = toMemorySize(size)

        if not size:
            raise NotImplementedError(
                "Ramulator2 memory controller requires a size parameter."
            )

    @overrides(AbstractMemorySystem)
    def incorporate_memory(self, board: AbstractBoard) -> None:
        pass

    @overrides(AbstractMemorySystem)
    def get_mem_ports(self) -> Tuple[Sequence[AddrRange], Port]:
        return [(self.mem_ctrl.range, self.mem_ctrl.port)]

    @overrides(AbstractMemorySystem)
    def get_memory_controllers(self) -> List[MemCtrl]:
        return [self.mem_ctrl]

    @overrides(AbstractMemorySystem)
    def get_size(self) -> int:
        return self._size

    @overrides(AbstractMemorySystem)
    def set_memory_range(self, ranges: List[AddrRange]) -> None:
        if len(ranges)!=1 or ranges[0].size() != self._size:
            raise Exception(
                "Single channel Ramulator2 memory controller requires a single "
                "range which matches the memory's size."
            )
        #self._mem_range = ranges[0]
        #self.mem_ctrl.range = AddrRange(
        #    start=self._mem_range.start,
        #    size=self._mem_range.size(),
        #    intlvHighBit=6+0-1,
        #    xorHighBit=0,
        #    intlvBits=0,
        #    intlvMatch=0,
        #)
        self.mem_ctrl.range = ranges[0]        



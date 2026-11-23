"""GDB pretty-printers for the oat containers.

Load once per session, or from ~/.gdbinit:

    source /path/to/oat/debug/oat_gdb.py

Covers Array, FixedArray, RingBuffer, Map, Set, StrBuf, Pool and Handle.
"""
import gdb
import gdb.printing

MAX_CHILDREN = 5000
POOL_CHUNK_SLOTS = 64


class ArrayPrinter:
    def __init__(self, value):
        self.value = value

    def to_string(self):
        return "len=%d cap=%d" % (int(self.value["len"]), int(self.value["cap"]))

    def children(self):
        data = self.value["data"]
        for i in range(min(int(self.value["len"]), MAX_CHILDREN)):
            yield "[%d]" % i, data[i]

    def display_hint(self):
        return "array"


class FixedArrayPrinter:
    def __init__(self, value):
        self.value = value

    def to_string(self):
        return "len=%d" % int(self.value["len"])

    def children(self):
        elem = self.value.type.strip_typedefs().template_argument(0)
        data = self.value["storage"].address.cast(elem.pointer())
        for i in range(min(int(self.value["len"]), MAX_CHILDREN)):
            yield "[%d]" % i, data[i]

    def display_hint(self):
        return "array"


class RingBufferPrinter:
    def __init__(self, value):
        self.value = value

    def to_string(self):
        return "len=%d cap=%d" % (int(self.value["len"]), int(self.value["cap"]))

    def children(self):
        data = self.value["data"]
        cap = int(self.value["cap"])
        head = int(self.value["head"])
        # Index 0 is the oldest element.
        for i in range(min(int(self.value["len"]), MAX_CHILDREN)):
            yield "[%d]" % i, data[(head + i) % cap]

    def display_hint(self):
        return "array"


def _map_entries(map_value):
    """Yields the MapEntry of every occupied slot."""
    # Map<K, V> stores MapEntry<K, V>; the names differ only in the template name.
    name = str(map_value.type.strip_typedefs())
    entry_type = gdb.lookup_type(name.replace("oat::Map<", "oat::MapEntry<", 1))
    raw = map_value["raw"]
    entries = raw["entries"].cast(entry_type.pointer())
    hashes = raw["hashes"]
    yielded = 0
    for slot in range(int(raw["cap"])):
        if yielded >= MAX_CHILDREN:
            return
        # A stored hash of 0 marks an empty slot.
        if int(hashes[slot]) != 0:
            yielded += 1
            yield entries[slot]


class MapPrinter:
    def __init__(self, value):
        self.value = value

    def to_string(self):
        return "len=%d" % int(self.value["raw"]["len"])

    def children(self):
        for i, entry in enumerate(_map_entries(self.value)):
            yield "key%d" % i, entry["key"]
            yield "value%d" % i, entry["value"]

    def display_hint(self):
        return "map"


class SetPrinter:
    def __init__(self, value):
        self.value = value

    def to_string(self):
        return "len=%d" % int(self.value["map"]["raw"]["len"])

    def children(self):
        for i, entry in enumerate(_map_entries(self.value["map"])):
            yield "[%d]" % i, entry["key"]

    def display_hint(self):
        return "array"


class PoolPrinter:
    def __init__(self, value):
        self.value = value

    def to_string(self):
        return "len=%d" % int(self.value["raw"]["len"])

    def children(self):
        raw = self.value["raw"]
        elem = self.value.type.strip_typedefs().template_argument(0)
        char_ptr = gdb.lookup_type("char").pointer()
        slot_count = int(raw["slot_count"])
        items_offset = int(raw["items_offset"])
        yielded = 0
        for c in range(int(raw["chunk_count"])):
            chunk = raw["chunks"][c]
            live = int(chunk["live"])
            items = (chunk.cast(char_ptr) + items_offset).cast(elem.pointer())
            for slot in range(POOL_CHUNK_SLOTS):
                index = c * POOL_CHUNK_SLOTS + slot
                if index >= slot_count or yielded >= MAX_CHILDREN:
                    return
                if (live >> slot) & 1:
                    yielded += 1
                    yield "[%d]" % index, items[slot]


class StrBufPrinter:
    def __init__(self, value):
        self.value = value

    def to_string(self):
        data = self.value["data"]
        if int(data) == 0:
            return ""
        return data.string("utf-8", "replace", int(self.value["len"]))

    def display_hint(self):
        return "string"


class HandlePrinter:
    def __init__(self, value):
        self.value = value

    def to_string(self):
        generation = int(self.value["generation"])
        if generation == 0:
            return "null"
        return "index=%d generation=%d" % (int(self.value["index"]), generation)


def build_printer():
    printer = gdb.printing.RegexpCollectionPrettyPrinter("oat")
    printer.add_printer("Array", r"^oat::Array<.*>$", ArrayPrinter)
    printer.add_printer("FixedArray", r"^oat::FixedArray<.*>$", FixedArrayPrinter)
    printer.add_printer("RingBuffer", r"^oat::RingBuffer<.*>$", RingBufferPrinter)
    printer.add_printer("Map", r"^oat::Map<.*>$", MapPrinter)
    printer.add_printer("Set", r"^oat::Set<.*>$", SetPrinter)
    printer.add_printer("Pool", r"^oat::Pool<.*>$", PoolPrinter)
    printer.add_printer("StrBuf", r"^oat::StrBuf$", StrBufPrinter)
    printer.add_printer("Handle", r"^oat::Handle<.*>$", HandlePrinter)
    return printer


gdb.printing.register_pretty_printer(gdb.current_objfile(), build_printer(), replace=True)

"""LLDB formatters for the oat containers.

Load once per session, or from ~/.lldbinit:

    command script import /path/to/oat/debug/oat_lldb.py

Covers Array, FixedArray, RingBuffer, Map, Set, StrBuf, Pool and Handle.
"""
import struct

import lldb

MAX_CHILDREN = 5000
POOL_CHUNK_SLOTS = 64


def _unsigned(value, name):
    return value.GetChildMemberWithName(name).GetValueAsUnsigned(0)


def _read(value, address, size):
    """Reads target memory; returns None if it cannot be read."""
    if size == 0:
        return b""
    error = lldb.SBError()
    data = value.GetProcess().ReadMemory(address, size, error)
    return data if error.Success() else None


class _Synth:
    """Shared plumbing: subclasses fill self.children with (name, address, type)."""

    def __init__(self, value, _dict):
        self.value = value
        self.children = []

    def num_children(self):
        return len(self.children)

    def has_children(self):
        return True

    def get_child_index(self, name):
        for i, child in enumerate(self.children):
            if child[0] == name:
                return i
        return -1

    def get_child_at_index(self, index):
        if index < 0 or index >= len(self.children):
            return None
        name, address, type_ = self.children[index]
        return self.value.CreateValueFromAddress(name, address, type_)

    def update(self):
        self.children = []
        try:
            self.fill()
        except Exception:  # never let a formatter take the debugger down
            self.children = []
        return False


class ArraySynth(_Synth):
    def fill(self):
        data = self.value.GetChildMemberWithName("data")
        elem = data.GetType().GetPointeeType()
        base = data.GetValueAsUnsigned(0)
        count = min(_unsigned(self.value, "len"), MAX_CHILDREN)
        size = elem.GetByteSize()
        self.children = [("[%d]" % i, base + i * size, elem) for i in range(count)]


class FixedArraySynth(_Synth):
    def fill(self):
        elem = self.value.GetType().GetTemplateArgumentType(0)
        base = self.value.GetChildMemberWithName("storage").GetLoadAddress()
        count = min(_unsigned(self.value, "len"), MAX_CHILDREN)
        size = elem.GetByteSize()
        self.children = [("[%d]" % i, base + i * size, elem) for i in range(count)]


class RingBufferSynth(_Synth):
    def fill(self):
        data = self.value.GetChildMemberWithName("data")
        elem = data.GetType().GetPointeeType()
        base = data.GetValueAsUnsigned(0)
        cap = _unsigned(self.value, "cap")
        head = _unsigned(self.value, "head")
        count = min(_unsigned(self.value, "len"), MAX_CHILDREN)
        size = elem.GetByteSize()
        # Index 0 is the oldest element.
        self.children = [("[%d]" % i, base + ((head + i) % cap) * size, elem)
                         for i in range(count)]


def _map_entries(value, raw):
    """Yields (address, entry type) for every occupied slot of a Map's storage."""
    # Map<K, V> stores MapEntry<K, V>; the names differ only in the template name.
    name = value.GetType().GetCanonicalType().GetName()
    entry_name = name.replace("oat::Map<", "oat::MapEntry<", 1)
    entry = value.GetTarget().FindFirstType(entry_name)
    if not entry.IsValid():
        return
    cap = _unsigned(raw, "cap")
    entries = raw.GetChildMemberWithName("entries").GetValueAsUnsigned(0)
    hashes = _read(value, raw.GetChildMemberWithName("hashes").GetValueAsUnsigned(0), cap * 4)
    if hashes is None:
        return
    size = entry.GetByteSize()
    for slot in range(cap):
        # A stored hash of 0 marks an empty slot.
        if struct.unpack_from("<I", hashes, slot * 4)[0] != 0:
            yield entries + slot * size, entry


class MapSynth(_Synth):
    def fill(self):
        raw = self.value.GetChildMemberWithName("raw")
        for address, entry in _map_entries(self.value, raw):
            if len(self.children) >= MAX_CHILDREN:
                break
            self.children.append(("[%d]" % len(self.children), address, entry))


class SetSynth(_Synth):
    def fill(self):
        inner = self.value.GetChildMemberWithName("map")
        key = self.value.GetType().GetTemplateArgumentType(0)
        for address, _entry in _map_entries(inner, inner.GetChildMemberWithName("raw")):
            if len(self.children) >= MAX_CHILDREN:
                break
            self.children.append(("[%d]" % len(self.children), address, key))  # key is at offset 0


class PoolSynth(_Synth):
    def fill(self):
        raw = self.value.GetChildMemberWithName("raw")
        elem = self.value.GetType().GetTemplateArgumentType(0)
        size = elem.GetByteSize()
        slot_count = _unsigned(raw, "slot_count")
        chunk_count = _unsigned(raw, "chunk_count")
        items_offset = _unsigned(raw, "items_offset")
        pointer_size = self.value.GetProcess().GetAddressByteSize()
        table = _read(self.value, raw.GetChildMemberWithName("chunks").GetValueAsUnsigned(0),
                      chunk_count * pointer_size)
        if table is None:
            return
        for c in range(chunk_count):
            chunk = struct.unpack_from("<Q" if pointer_size == 8 else "<I", table,
                                       c * pointer_size)[0]
            live_bytes = _read(self.value, chunk, 8)  # the chunk starts with its live mask
            if live_bytes is None:
                return
            live = struct.unpack("<Q", live_bytes)[0]
            for slot in range(POOL_CHUNK_SLOTS):
                index = c * POOL_CHUNK_SLOTS + slot
                if index >= slot_count or len(self.children) >= MAX_CHILDREN:
                    break
                if (live >> slot) & 1:
                    self.children.append(("[%d]" % index, chunk + items_offset + slot * size, elem))


def array_summary(value, _dict):
    raw = value.GetNonSyntheticValue()
    return "len=%d cap=%d" % (_unsigned(raw, "len"), _unsigned(raw, "cap"))


def fixed_array_summary(value, _dict):
    return "len=%d" % _unsigned(value.GetNonSyntheticValue(), "len")


def map_summary(value, _dict):
    raw = value.GetNonSyntheticValue().GetChildMemberWithName("raw")
    return "len=%d" % _unsigned(raw, "len")


def set_summary(value, _dict):
    raw = value.GetNonSyntheticValue().GetChildMemberWithName("map").GetChildMemberWithName("raw")
    return "len=%d" % _unsigned(raw, "len")


def pool_summary(value, _dict):
    raw = value.GetNonSyntheticValue().GetChildMemberWithName("raw")
    return "len=%d" % _unsigned(raw, "len")


def strbuf_summary(value, _dict):
    length = _unsigned(value, "len")
    address = value.GetChildMemberWithName("data").GetValueAsUnsigned(0)
    if address == 0 or length == 0:
        return '""'
    data = _read(value, address, min(length, 4096))
    if data is None:
        return "<unreadable>"
    text = data.decode("utf-8", "replace").replace("\\", "\\\\").replace('"', '\\"')
    return '"%s"%s' % (text, "..." if length > 4096 else "")


def handle_summary(value, _dict):
    generation = _unsigned(value, "generation")
    if generation == 0:
        return "null"
    return "index=%d generation=%d" % (_unsigned(value, "index"), generation)


def __lldb_init_module(debugger, _dict):
    def run(command):
        debugger.HandleCommand(command)

    synths = [
        ("ArraySynth", "array_summary", r"^oat::Array<.+>$"),
        ("FixedArraySynth", "fixed_array_summary", r"^oat::FixedArray<.+>$"),
        ("RingBufferSynth", "array_summary", r"^oat::RingBuffer<.+>$"),
        ("MapSynth", "map_summary", r"^oat::Map<.+>$"),
        ("SetSynth", "set_summary", r"^oat::Set<.+>$"),
        ("PoolSynth", "pool_summary", r"^oat::Pool<.+>$"),
    ]
    for synth, summary, regex in synths:
        run('type synthetic add -w oat -x "%s" -l %s.%s' % (regex, __name__, synth))
        run('type summary add -w oat -e -x "%s" -F %s.%s' % (regex, __name__, summary))
    run('type summary add -w oat "oat::StrBuf" -F %s.strbuf_summary' % __name__)
    run('type summary add -w oat -x "^oat::Handle<.+>$" -F %s.handle_summary' % __name__)
    run('type summary add -w oat "oat::Sym" --summary-string "id=${var.id}"')
    run("type category enable oat")

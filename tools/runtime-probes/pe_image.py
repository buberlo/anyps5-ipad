"""Small bounds-checked PE32+ import/export reader; no external Python packages."""
import struct
from pathlib import Path


class PEImage:
    def __init__(self, path):
        self.path = Path(path)
        self.data = self.path.read_bytes()
        if self.data[:2] != b"MZ":
            raise ValueError(f"{self.path}: not a PE image (link-only ELF stubs are not runtime libraries)")
        header = self.unpack("I", 0x3c)[0]
        if self.take(header, 4) != b"PE\0\0" or self.unpack("H", header + 4)[0] != 0x8664:
            raise ValueError(f"{self.path}: expected an x86-64 PE image")
        count = self.unpack("H", header + 6)[0]
        optional_size = self.unpack("H", header + 20)[0]
        optional = header + 24
        self.take(optional, optional_size)
        if self.unpack("H", optional)[0] != 0x20b or optional_size < 112:
            raise ValueError(f"{self.path}: expected a PE32+ optional header")
        directories = self.unpack("I", optional + 108)[0]
        if directories > (optional_size - 112) // 8:
            raise ValueError("PE directory table exceeds optional header")
        self.directories = [self.unpack("II", optional + 112 + i*8) for i in range(directories)]
        self.sections = []
        for i in range(count):
            row = optional + optional_size + i*40
            self.take(row, 40)
            virtual_size, rva, raw_size, offset = self.unpack("IIII", row + 8)
            self.take(offset, raw_size)
            self.sections.append((rva, raw_size, offset, virtual_size))
        if self.directory(13) != (0, 0):
            raise ValueError(f"{self.path}: delay imports need explicit packaging support")

    def take(self, offset, size):
        if offset < 0 or size < 0 or offset > len(self.data) or size > len(self.data) - offset:
            raise ValueError(f"{self.path}: PE field outside file")
        return self.data[offset:offset+size]

    def unpack(self, kind, offset):
        return struct.unpack("<" + kind, self.take(offset, struct.calcsize("<" + kind)))

    def directory(self, index):
        return self.directories[index] if index < len(self.directories) else (0, 0)

    def offset(self, rva, size=1):
        for base, raw_size, offset, _ in self.sections:
            relative = rva - base
            if 0 <= relative < raw_size and size <= raw_size - relative:
                return offset + relative
        raise ValueError(f"{self.path}: unmapped PE RVA 0x{rva:x}")

    def string(self, rva):
        offset = self.offset(rva)
        end = self.data.find(b"\0", offset, min(len(self.data), offset + 65536))
        if end < 0:
            raise ValueError(f"{self.path}: unterminated PE string")
        self.offset(rva, end - offset + 1)
        return self.data[offset:end].decode("ascii")

    def imports(self):
        rva, size = self.directory(1)
        if not rva:
            return {}
        result = {}
        for index in range(min(size // 20, 4096)):
            original, stamp, chain, name, first = self.unpack("IIIII", self.offset(rva + index*20, 20))
            if not any((original, stamp, chain, name, first)):
                return result
            library = self.string(name)
            names = []
            table = original or first
            for n in range(1000000):
                value = self.unpack("Q", self.offset(table + n*8, 8))[0]
                if not value:
                    break
                names.append(int(value & 0xffff) if value & (1 << 63) else self.string(value + 2))
            else:
                raise ValueError("unterminated PE import thunk table")
            if library.lower() in {key.lower() for key in result}:
                raise ValueError(f"duplicate PE import descriptor: {library}")
            result[library] = names
        raise ValueError("unterminated PE import descriptor table")

    def exports(self):
        rva, size = self.directory(0)
        if not rva:
            return {}, {}
        table = self.offset(rva, 40)
        base, count, named, functions, names, ordinals = self.unpack("IIIIII", table + 16)
        if count > 1000000 or named > 1000000:
            raise ValueError("invalid PE export count")
        by_ordinal = {}
        for index in range(count):
            address = self.unpack("I", self.offset(functions + index*4, 4))[0]
            if address:
                by_ordinal[base + index] = self.string(address) if rva <= address < rva + size else None
        by_name = {}
        for index in range(named):
            name = self.string(self.unpack("I", self.offset(names + index*4, 4))[0])
            ordinal = base + self.unpack("H", self.offset(ordinals + index*2, 2))[0]
            if ordinal not in by_ordinal or name in by_name:
                raise ValueError("invalid or duplicate PE named export")
            by_name[name] = ordinal
        return by_name, by_ordinal


def safe_library_name(name):
    if not name or Path(name).name != name or any(c in name for c in "\\/:\0") or name in (".", ".."):
        raise ValueError(f"unsafe dependency filename: {name!r}")
    return name

"""Read-only PE inspector used to analyse the GTA Vice City executables.

Usage:
    python tools/inspect_pe.py <path-to-exe-or-dll>

Prints architecture, entry point, sections (with entropy), TLS callbacks
and the import table grouped by DLL. Imported DLLs that are not in the
KnownDLLs list are marked as proxy candidates: Windows searches the exe's
own folder for those first, so a same-named DLL dropped there gets loaded.

Never writes to the file; it is opened read-only and only parsed.
"""

import argparse
import sys

try:
    import pefile
except ImportError:
    sys.exit("pefile is missing: run `python -m pip install pefile`")

ENTROPY_WARN = 7.2
NORMAL_SECTIONS = {
    ".text", ".rdata", ".data", ".rsrc", ".reloc", ".idata", ".edata",
    ".pdata", ".tls", ".bss", ".CRT", ".gfids", ".00cfg", ".didat",
    "CODE", "DATA", "BSS",
}


def known_dlls():
    """KnownDLLs are always loaded from System32/SysWOW64, so they can't be proxied."""
    names = {"ntdll.dll"}  # always preloaded, not listed in the key
    try:
        import winreg
        key = winreg.OpenKey(
            winreg.HKEY_LOCAL_MACHINE,
            r"SYSTEM\CurrentControlSet\Control\Session Manager\KnownDLLs")
        i = 0
        while True:
            try:
                _, value, _ = winreg.EnumValue(key, i)
            except OSError:
                break
            if isinstance(value, str) and value.lower().endswith(".dll"):
                names.add(value.lower())
            i += 1
    except OSError:
        pass  # not on Windows, or no access: just skip the marking
    return names


def section_name(s):
    return s.Name.rstrip(b"\x00").decode("latin-1")


def section_for_rva(pe, rva):
    for s in pe.sections:
        size = max(s.Misc_VirtualSize, s.SizeOfRawData)
        if s.VirtualAddress <= rva < s.VirtualAddress + size:
            return s
    return None


def print_header(pe, path):
    machine = pe.FILE_HEADER.Machine
    arch = {0x14C: "x86 (32-bit)", 0x8664: "x64 (64-bit)", 0xAA64: "ARM64"}.get(
        machine, f"unknown (0x{machine:04X})")
    opt = pe.OPTIONAL_HEADER
    print(f"File:            {path}")
    print(f"Architecture:    {arch}  (PE32{'+' if opt.Magic == 0x20B else ''})")
    print(f"Image base:      0x{opt.ImageBase:08X}")
    dllchars = opt.DllCharacteristics
    print(f"ASLR (dynamic base): {'yes' if dllchars & 0x40 else 'no'}   "
          f"DEP (NX compat): {'yes' if dllchars & 0x100 else 'no'}")
    print(f"Timestamp:       0x{pe.FILE_HEADER.TimeDateStamp:08X}")

    ep = opt.AddressOfEntryPoint
    s = section_for_rva(pe, ep)
    where = section_name(s) if s else "<outside all sections!>"
    print(f"Entry point RVA: 0x{ep:08X}  (VA 0x{opt.ImageBase + ep:08X}) in section '{where}'")
    if s is None or section_name(s) not in (".text", "CODE"):
        print("  !! Entry point is not in .text: likely a packer/protection stub.")

    try:
        first = pe.get_data(ep, 16)
        print(f"Entry bytes:     {first.hex(' ')}")
        if first[:1] == b"\xE8" and first[5:6] == b"\xE9":
            print("  -> looks like MSVC CRT startup (call __security_init_cookie; jmp __tmainCRTStartup)")
        elif first[:3] == b"\x55\x8B\xEC" or first[:3] == b"\x6A\x60\x68" or first[:2] == b"\x6A\x18":
            print("  -> looks like an ordinary compiler-generated prologue")
        elif first[:1] in (b"\x60", b"\x9C"):
            print("  !! starts with pushad/pushfd: classic packer stub pattern")
    except pefile.PEFormatError:
        print("Entry bytes:     <unreadable>")


def print_sections(pe):
    print("\nSections:")
    print(f"  {'name':<10}{'VirtAddr':>10}{'VirtSize':>10}{'RawSize':>10}{'entropy':>9}  flags")
    for s in pe.sections:
        name = section_name(s)
        ent = s.get_entropy()
        c = s.Characteristics
        flags = ("R" if c & 0x40000000 else "-") + ("W" if c & 0x80000000 else "-") + \
                ("X" if c & 0x20000000 else "-")
        notes = []
        if ent > ENTROPY_WARN:
            notes.append("HIGH ENTROPY")
        if name not in NORMAL_SECTIONS:
            notes.append("unusual name")
        if c & 0x20000000 and c & 0x80000000:
            notes.append("writable+exec")
        if s.SizeOfRawData == 0 and s.Misc_VirtualSize > 0 and c & 0x20000000:
            notes.append("empty exec section (unpacked at runtime?)")
        note = ("  <-- " + ", ".join(notes)) if notes else ""
        print(f"  {name:<10}{s.VirtualAddress:>#10x}{s.Misc_VirtualSize:>#10x}"
              f"{s.SizeOfRawData:>#10x}{ent:>9.3f}  {flags}{note}")


def print_tls(pe):
    if not hasattr(pe, "DIRECTORY_ENTRY_TLS"):
        print("\nTLS callbacks:   none")
        return
    tls = pe.DIRECTORY_ENTRY_TLS.struct
    base = pe.OPTIONAL_HEADER.ImageBase
    callbacks = []
    if tls.AddressOfCallBacks:
        rva = tls.AddressOfCallBacks - base
        for i in range(32):
            try:
                va = pe.get_dword_at_rva(rva + 4 * i)
            except pefile.PEFormatError:
                break
            if not va:
                break
            callbacks.append(va)
    if callbacks:
        print("\nTLS callbacks (run BEFORE the entry point):")
        for va in callbacks:
            s = section_for_rva(pe, va - base)
            print(f"  0x{va:08X} in '{section_name(s) if s else '?'}'")
    else:
        print("\nTLS callbacks:   TLS directory present, no callbacks")


def print_imports(pe):
    known = known_dlls()

    def dump(entries, label):
        if not entries:
            return
        print(f"\n{label}:")
        for entry in entries:
            dll = entry.dll.decode("latin-1")
            low = dll.lower()
            if low in known:
                tag = "[KnownDLL - cannot proxy]"
            elif low.startswith("api-ms-win-") or low.startswith("ext-ms-"):
                tag = "[API set - cannot proxy]"
            else:
                tag = "[proxy candidate]"
            print(f"  {dll}  ({len(entry.imports)} imports)  {tag}")
            for imp in entry.imports:
                if imp.name:
                    print(f"      {imp.name.decode('latin-1')}")
                else:
                    print(f"      ordinal #{imp.ordinal}")

    pe.parse_data_directories(directories=[
        pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"],
        pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT"],
    ])
    dump(getattr(pe, "DIRECTORY_ENTRY_IMPORT", []), "Imports")
    dump(getattr(pe, "DIRECTORY_ENTRY_DELAY_IMPORT", []), "Delay-load imports")
    if not hasattr(pe, "DIRECTORY_ENTRY_IMPORT"):
        print("\n  !! No import table: almost certainly packed.")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("path")
    args = ap.parse_args()

    # fast_load skips data directories until we ask for them.
    pe = pefile.PE(args.path, fast_load=True)
    pe.parse_data_directories(directories=[
        pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_TLS"]])
    print_header(pe, args.path)
    print_sections(pe)
    print_tls(pe)
    print_imports(pe)
    pe.close()


if __name__ == "__main__":
    main()

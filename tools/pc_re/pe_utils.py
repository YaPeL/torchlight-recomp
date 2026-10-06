#!/usr/bin/env python3
import struct
import hashlib
import os

class PE:
    def __init__(self, filepath):
        self.filepath = filepath
        with open(filepath, "rb") as f:
            self.data = f.read()
        self.sha256 = hashlib.sha256(self.data).hexdigest()
        self.filesize = len(self.data)
        self._parse()

    def _parse(self):
        if len(self.data) < 64:
            raise ValueError("File too small for DOS header")
        if self.data[:2] != b"MZ":
            raise ValueError("Not a valid MZ header")
        e_lfanew = struct.unpack_from("<I", self.data, 0x3c)[0]
        self.e_lfanew = e_lfanew
        if self.data[e_lfanew:e_lfanew+4] != b"PE\x00\x00":
            raise ValueError("Not a valid PE header")

        coff_offset = e_lfanew + 4
        (machine, num_sections, time_date, ptr_sym, num_sym, opt_hdr_size, chars) = struct.unpack_from("<HHIIIHH", self.data, coff_offset)
        self.machine = machine
        self.num_sections = num_sections
        self.time_date_stamp = time_date
        self.characteristics = chars

        opt_offset = coff_offset + 20
        magic = struct.unpack_from("<H", self.data, opt_offset)[0]
        self.magic = magic
        if magic == 0x10b: # PE32
            (magic, maj_link, min_link, size_code, size_init_data, size_uninit_data,
             entry_point, base_of_code, base_of_data, image_base,
             sec_align, file_align, maj_os, min_os, maj_img, min_img,
             maj_sub, min_sub, win32_ver, size_of_image, size_of_headers,
             checksum, subsystem, dll_chars, size_stack_res, size_stack_com,
             size_heap_res, size_heap_com, loader_flags, num_rvas) = struct.unpack_from("<HBBIIIIIIIIIHHHHHHIIIIHHIIIIII", self.data, opt_offset)
            self.entry_point = entry_point
            self.image_base = image_base
            self.size_of_image = size_of_image
            self.subsystem = subsystem
            self.base_of_code = base_of_code
            self.base_of_data = base_of_data
            rva_offset = opt_offset + 96
        elif magic == 0x20b: # PE32+
            (magic, maj_link, min_link, size_code, size_init_data, size_uninit_data,
             entry_point, base_of_code, image_base,
             sec_align, file_align, maj_os, min_os, maj_img, min_img,
             maj_sub, min_sub, win32_ver, size_of_image, size_of_headers,
             checksum, subsystem, dll_chars, size_stack_res, size_stack_com,
             size_heap_res, size_heap_com, loader_flags, num_rvas) = struct.unpack_from("<HBBIIIIQIIHHHHHHIIIIHHQQQQII", self.data, opt_offset)
            self.entry_point = entry_point
            self.image_base = image_base
            self.size_of_image = size_of_image
            self.subsystem = subsystem
            self.base_of_code = base_of_code
            self.base_of_data = None
            rva_offset = opt_offset + 112
        else:
            raise ValueError(f"Unknown optional header magic: {hex(magic)}")

        self.directories = []
        for i in range(num_rvas):
            va, sz = struct.unpack_from("<II", self.data, rva_offset + i * 8)
            self.directories.append((va, sz))

        sec_table_offset = opt_offset + opt_hdr_size
        self.sections = []
        for i in range(num_sections):
            sec_bytes = self.data[sec_table_offset + i * 40 : sec_table_offset + (i+1) * 40]
            name = sec_bytes[:8].split(b"\x00")[0].decode("ascii", errors="replace")
            vsize, vaddr, raw_size, raw_ptr, _, _, _, _, chars = struct.unpack_from("<IIIIIIHHI", sec_bytes, 8)
            self.sections.append({
                "name": name,
                "virtual_size": vsize,
                "virtual_address": vaddr,
                "raw_size": raw_size,
                "raw_ptr": raw_ptr,
                "characteristics": chars
            })

    def rva_to_offset(self, rva):
        for s in self.sections:
            if s["virtual_address"] <= rva < s["virtual_address"] + max(s["virtual_size"], s["raw_size"]):
                return s["raw_ptr"] + (rva - s["virtual_address"])
        return None

    def get_exports(self):
        if len(self.directories) < 1:
            return []
        exp_va, exp_sz = self.directories[0]
        if exp_va == 0 or exp_sz == 0:
            return []
        offset = self.rva_to_offset(exp_va)
        if offset is None:
            return []
        (flags, time_stamp, maj_ver, min_ver, name_rva, ordinal_base,
         num_funcs, num_names, funcs_rva, names_rva, ordinals_rva) = struct.unpack_from("<IIHHIIIIIII", self.data, offset)
        
        name_offset = self.rva_to_offset(name_rva)
        dll_name = ""
        if name_offset:
            dll_name = self.data[name_offset:].split(b"\x00")[0].decode("ascii", errors="replace")

        func_offsets = self.rva_to_offset(funcs_rva)
        names_offsets = self.rva_to_offset(names_rva)
        ords_offsets = self.rva_to_offset(ordinals_rva)

        functions = []
        if func_offsets:
            for i in range(num_funcs):
                f_rva = struct.unpack_from("<I", self.data, func_offsets + i * 4)[0]
                functions.append(f_rva)

        named_exports = {}
        if names_offsets and ords_offsets:
            for i in range(num_names):
                n_rva = struct.unpack_from("<I", self.data, names_offsets + i * 4)[0]
                ord_idx = struct.unpack_from("<H", self.data, ords_offsets + i * 2)[0]
                n_off = self.rva_to_offset(n_rva)
                func_name = self.data[n_off:].split(b"\x00")[0].decode("ascii", errors="replace")
                named_exports[ord_idx] = func_name

        exports = []
        for i, f_rva in enumerate(functions):
            if f_rva != 0:
                name = named_exports.get(i, f"ordinal_{i + ordinal_base}")
                exports.append({
                    "ordinal": i + ordinal_base,
                    "rva": f_rva,
                    "name": name
                })
        return exports

    def get_imports(self):
        if len(self.directories) < 2:
            return {}
        imp_va, imp_sz = self.directories[1]
        if imp_va == 0 or imp_sz == 0:
            return {}
        offset = self.rva_to_offset(imp_va)
        if offset is None:
            return {}

        imports = {}
        idx = 0
        while True:
            desc_offset = offset + idx * 20
            (original_first_thunk, time_date, forwarder, name_rva, first_thunk) = struct.unpack_from("<IIIII", self.data, desc_offset)
            if name_rva == 0 and first_thunk == 0:
                break
            n_off = self.rva_to_offset(name_rva)
            if not n_off:
                break
            mod_name = self.data[n_off:].split(b"\x00")[0].decode("ascii", errors="replace")
            
            thunk_rva = original_first_thunk if original_first_thunk != 0 else first_thunk
            thunk_off = self.rva_to_offset(thunk_rva)
            
            funcs = []
            if thunk_off:
                thunk_idx = 0
                while True:
                    if self.magic == 0x10b:
                        val = struct.unpack_from("<I", self.data, thunk_off + thunk_idx * 4)[0]
                        if val == 0:
                            break
                        if val & 0x80000000: # ordinal
                            funcs.append(f"ordinal_{val & 0xffff}")
                        else:
                            hint_off = self.rva_to_offset(val)
                            if hint_off:
                                hint, fname = struct.unpack_from("<H", self.data, hint_off)[0], self.data[hint_off+2:].split(b"\x00")[0].decode("ascii", errors="replace")
                                funcs.append(fname)
                            else:
                                funcs.append(f"rva_0x{val:08x}")
                    else:
                        val = struct.unpack_from("<Q", self.data, thunk_off + thunk_idx * 8)[0]
                        if val == 0:
                            break
                        if val & 0x8000000000000000:
                            funcs.append(f"ordinal_{val & 0xffff}")
                        else:
                            hint_off = self.rva_to_offset(val)
                            if hint_off:
                                fname = self.data[hint_off+2:].split(b"\x00")[0].decode("ascii", errors="replace")
                                funcs.append(fname)
                            else:
                                funcs.append(f"rva_0x{val:016x}")
                    thunk_idx += 1

            imports[mod_name] = funcs
            idx += 1
        return imports

if __name__ == "__main__":
    import sys
    pe = PE(sys.argv[1])
    print(f"File: {sys.argv[1]}")
    print(f"Size: {pe.filesize} bytes")
    print(f"SHA256: {pe.sha256}")
    print(f"Machine: 0x{pe.machine:x} ({'i386' if pe.machine == 0x14c else 'other'})")
    print(f"ImageBase: 0x{pe.image_base:08x}")
    print(f"EntryPoint: 0x{pe.entry_point:08x}")
    print(f"Sections ({pe.num_sections}):")
    for s in pe.sections:
        print(f"  {s['name']:<8} RVA: 0x{s['virtual_address']:08x} VSize: 0x{s['virtual_size']:08x} RawSize: 0x{s['raw_size']:08x}")
    exps = pe.get_exports()
    print(f"Exports ({len(exps)}):")
    for e in exps[:20]:
        print(f"  [{e['ordinal']}] 0x{e['rva']:08x}: {e['name']}")
    imps = pe.get_imports()
    print(f"Imports ({len(imps)} DLLs):")
    for m, flist in imps.items():
        print(f"  {m} ({len(flist)} funcs)")

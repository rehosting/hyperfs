// mod link_sys;

use ctor::ctor;
use lazy_static::lazy_static;
use rustix::mm::mprotect;
use rustix::mm::MprotectFlags;
use std::collections::HashMap;
use std::ffi::c_void;
use std::ffi::CStr;
use std::ffi::OsStr;
use std::io::Write;
use std::os::unix::ffi::OsStrExt;
use std::path::Path;

// #[cfg(target_pointer_width = "32")]
// mod elf_types {
//     pub type ElfPhdr = crate::link_sys::Elf32_Phdr;
//     pub type ElfDyn = crate::link_sys::Elf32_Dyn;
// }
// #[cfg(target_pointer_width = "64")]
// mod elf_types {
//     pub type ElfPhdr = crate::link_sys::Elf64_Phdr;
//     pub type ElfDyn = crate::link_sys::Elf64_Dyn;
// }
// use elf_types::*;

// extern "C" {
//     // Unfortunately musl doesn't declare this in a header,
//     // so we need to declare it manually here
//     static _DYNAMIC: [ElfDyn; 0];
// }

#[repr(C)]
#[derive(Debug, Copy, Clone)]
struct CLinkMap {
    addr: usize,
    name: *mut libc::c_char,
    ld: *mut libc::c_void,
    next: *mut CLinkMap,
    prev: *mut CLinkMap,
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
struct CLinkDebug {
    version: libc::c_int,
    map: *mut CLinkMap,
    brk: usize,
    state: libc::c_uint,
    ldbase: usize,
}

lazy_static! {
    static ref SHIMS: HashMap<String, String> = std::env::var("SHIMGUIN_SHIMS")
        .expect("no SHIMGUIN_SHIMS environment variable found")
        .split(',')
        .filter(|shim| !shim.is_empty())
        .map(|shim| {
            let (sym, repl) = shim
                .split_once("->")
                .expect("no '->' found in SHIMGUIN_SHIMS rule");
            (sym.to_owned(), repl.to_owned())
        })
        .collect();
}

static mut R_DEBUG: *const CLinkDebug = std::ptr::null();

fn add_path_to_error(e: impl std::error::Error, path: &Path) -> String {
    format!("path='{}', error=({e})", path.display())
}

unsafe fn shim_lib(path: &Path, base_addr: usize) {
    let obj = std::fs::read(path)
            .map_err(|e| add_path_to_error(e, path))
            .expect("failed opening library to parse its symbol table");
    let obj = elf::ElfBytes::<elf::endian::AnyEndian>::minimal_parse(&obj)
        .map_err(|e| add_path_to_error(e, path)).expect("failed parsing library");

    // Patch `_dl_debug_state` if exists
    // if let Some((sym_tab, str_tab)) = obj.symbol_table().expect("failed parsing symbol table") {
    //     for sym in sym_tab {
    //         let sym_addr = base_addr + sym.st_value as usize;
    //         let sym_name = str_tab.get(sym.st_name as usize).expect("failed getting a symbol name");
    //         if sym_name == "_dl_debug_state" {
    //             dbg!(sym_addr);
    //             break
    //             // write_jmp_shellcode(sym_addr, shim_all_libs as usize);
    //         }
    //     }
    // }

    // Patch requested functions
    let (sym_tab, str_tab) = obj.dynamic_symbol_table().expect("failed parsing dynamic symbol table").expect("library has no dynamic symbol table");
    for sym in sym_tab {
        let sym_addr = base_addr + sym.st_value as usize;
        let sym_name = str_tab.get(sym.st_name as usize).expect("failed getting a symbol name");
        let Some(repl_name) = SHIMS.get(sym_name) else { continue };
        let repl_addr = dlsym(repl_name).expect("failed finding TODO repl_name");
        write_jmp_shellcode(sym_addr, repl_addr);
    }
}

unsafe fn dlsym(name: &str) -> Option<usize> {
    let r = *R_DEBUG;
    let mut m = r.map;
    while !m.is_null() {
        let path = CStr::from_ptr((*m).name).to_bytes();
        if !path.is_empty() && path != b"linux-vdso.so.1" {
            let path = Path::new(OsStr::from_bytes(path));
            let obj = std::fs::read(path)
                .map_err(|e| add_path_to_error(e, path))
                .expect("failed opening library to parse its symbol table");
            let obj = elf::ElfBytes::<elf::endian::AnyEndian>::minimal_parse(&obj)
                .map_err(|e| add_path_to_error(e, path)).expect("failed parsing library");

            let (sym_tab, str_tab) = obj.dynamic_symbol_table().expect("failed parsing dynamic symbol table").expect("library has no dynamic symbol table");
            for sym in sym_tab {
                let sym_addr = (*m).addr + sym.st_value as usize;
                let sym_name = str_tab.get(sym.st_name as usize).expect("failed getting a symbol name");
                if sym_name == name { return Some(sym_addr) }
            }
        }
        m = (*m).next;
    }
    return None
}

// unsafe extern "C" fn shim_dl_phdr_info(info: *mut libc::dl_phdr_info, _: libc::size_t, _: *mut libc::c_void) -> libc::c_int {
//     let path = CStr::from_ptr((*info).dlpi_name).to_bytes();
//     if !path.is_empty() && path != b"linux-vdso.so.1" {
//         let path = Path::new(OsStr::from_bytes(path));
//         shim_lib(Path::new(path), (*info).dlpi_addr as usize);
//     }
//     0
// }

unsafe extern "C" fn shim_all_libs() {
    let r = *R_DEBUG;
    if r.state != 0 {
        return
    }
    write_jmp_shellcode(r.brk, shim_all_libs as usize);
    let mut m = r.map;
    while !m.is_null() {
        let path = CStr::from_ptr((*m).name).to_bytes();
        if !path.is_empty() && path != b"linux-vdso.so.1" {
            let path = Path::new(OsStr::from_bytes(path));
            shim_lib(Path::new(path), (*m).addr);
        }
        m = (*m).next;
    }
    // libc::dl_iterate_phdr(Some(shim_dl_phdr_info), std::ptr::null_mut());
}

fn make_jmp_shellcode(pc: usize, target: usize, mut out: &mut [u8]) -> std::io::Result<()> {
    if cfg!(all(target_arch = "x86_64", target_endian = "little")) {
        // `jmp target`
        let rel = target as isize - pc as isize - 5;
        out.write_all(&[0xe9])?;
        out.write_all(&rel.to_le_bytes())?;
    } else if cfg!(all(target_arch = "arm", target_endian = "little")) {
        // `b target`
        let rel = (target as isize - pc as isize) / 4 - 2;
        out.write_all(&rel.to_le_bytes()[..3])?;
        out.write_all(&[0xea])?;
    } else if cfg!(all(target_arch = "mips", target_endian = "little")) {
        // `lui $t9, HIGH(target)`
        out.write_all(&target.to_le_bytes()[2..])?;
        out.write_all(&[0x19, 0x3c])?;
        // `ori $t9, $t9, LOW(target)`
        out.write_all(&target.to_le_bytes()[..2])?;
        out.write_all(&[0x39, 0x37])?;
        // `jr $t9`
        out.write_all(&[0x08, 0x00, 0x20, 0x03])?;
        // `nop`
        out.write_all(&[0; 4])?;
    } else if cfg!(all(target_arch = "mips", target_endian = "big")) {
        // `lui $t9, HIGH(target)`
        out.write_all(&[0x3c, 0x19])?;
        out.write_all(&target.to_be_bytes()[..2])?;
        // `ori $t9, $t9, LOW(target)`
        out.write_all(&[0x37, 0x39])?;
        out.write_all(&target.to_be_bytes()[2..])?;
        // `jr $t9`
        out.write_all(&[0x03, 0x20, 0x00, 0x08])?;
        // `nop`
        out.write_all(&[0; 4])?;
    } else if cfg!(all(target_arch = "mips64", target_endian = "big")) {
        // `lui $t9, HIGH_16(target)`
        out.write_all(&[0x3c, 0x19])?;
        out.write_all(&target.to_be_bytes()[0..2])?;
        // `ori $t9, MIDDLE_HIGH_16(target)`
        out.write_all(&[0x37, 0x39])?;
        out.write_all(&target.to_be_bytes()[2..4])?;
        // `dsll $t9, $t9, 16`
        out.write_all(&[0x00, 0x19, 0xcc, 0x38])?;
        // `ori $t9, MIDDLE_LOW_16(target)`
        out.write_all(&[0x37, 0x39])?;
        out.write_all(&target.to_be_bytes()[4..6])?;
        // `dsll $t9, $t9, 16`
        out.write_all(&[0x00, 0x19, 0xcc, 0x38])?;
        // `ori $t9, LOW_16(target)`
        out.write_all(&[0x37, 0x39])?;
        out.write_all(&target.to_be_bytes()[6..8])?;
        // `jr $t9`
        out.write_all(&[0x03, 0x20, 0x00, 0x08])?;
        // `nop`
        out.write_all(&[0; 4])?;
    } else {
        unimplemented!("unsupported target arch");
    }
    Ok(())
}

unsafe fn write_jmp_shellcode(pc: usize, target: usize) {
    let page_size = rustix::param::page_size();
    let page_base = pc / page_size * page_size;
    mprotect(
        page_base as *mut c_void,
        page_size,
        MprotectFlags::READ | MprotectFlags::WRITE,
    )
    .expect("failed making memory writable for writing shellcode");
    let page_end = page_base + page_size;
    let buf = std::slice::from_raw_parts_mut(pc as *mut u8, page_end - pc);
    make_jmp_shellcode(pc, target, buf).expect("failed writing to shellcode buffer");
    mprotect(
        page_base as *mut c_void,
        page_size,
        MprotectFlags::READ | MprotectFlags::EXEC,
    )
    .expect("failed restoring memory to R+X");
}

unsafe fn get_r_debug() -> *const CLinkDebug {
    let r = libc::dlsym(libc::RTLD_DEFAULT, cstr::cstr!("_r_debug").as_ptr());
    if !r.is_null() {
        return r as *const CLinkDebug;
    }
    let r = libc::dlsym(libc::RTLD_DEFAULT, cstr::cstr!("_dl_debug_addr").as_ptr());
    if !r.is_null() {
        return *(r as *const *const CLinkDebug);
    }
    panic!("can't find r_debug")
}

// unsafe fn r_debug() -> link_sys::r_debug {
//     let phdr_addr = libc::getauxval(libc::AT_PHDR);
//     assert_ne!(phdr_addr, 0);
//     let phnum = libc::getauxval(libc::AT_PHNUM);
//     assert_ne!(phnum, 0);
//     assert_eq!(libc::getauxval(libc::AT_PHENT) as usize, std::mem::size_of::<ElfPhdr>());
//     let phdr_slice = std::slice::from_raw_parts(phdr_addr as *const ElfPhdr, phnum as usize);
//     let pt_phdr = phdr_slice.iter().find(|ph| ph.p_type == libc::PT_PHDR).expect("PT_PHDR not found in program headers");
//     let base_addr = phdr_addr - pt_phdr.p_vaddr;
//     let pt_dynamic = phdr_slice.iter().find(|ph| ph.p_type == libc::PT_DYNAMIC).expect("PT_DYNAMIC not found in program headers");

//     let mut dy = (base_addr + pt_dynamic.p_vaddr) as *const ElfDyn;
//     loop {
//         match (*dy).d_tag as u32 {
//             link_sys::DT_NULL => panic!("DT_DEBUG not found in .dynamic section"),
//             link_sys::DT_DEBUG => {
//                 let p = (*dy).d_un.d_ptr as *const link_sys::r_debug;
//                 assert!(!p.is_null());
//                 return *p
//             }
//             link_sys::DT_MIPS_RLD_MAP => {
//                 let p = (*dy).d_un.d_ptr as *const *const link_sys::r_debug;
//                 assert!(!p.is_null());
//                 assert!(!(*p).is_null());
//                 return **p
//             }
//             link_sys::DT_MIPS_RLD_MAP_REL => {
//                 let p = (dy as usize + (*dy).d_un.d_val as usize) as *const *const link_sys::r_debug;
//                 assert!(!p.is_null());
//                 assert!(!(*p).is_null(), "the linker didn't set the DT_MIPS_RLD_MAP_REL pointer (known issue with PIE binaries on older versions of musl)");
//                 return **p
//             }
//             _ => dy = dy.offset(1),
//         }
//     }
// }

#[no_mangle] pub extern "C" fn _Unwind_Resume() {}
#[no_mangle] pub extern "C" fn _Unwind_GetDataRelBase() {}
#[no_mangle] pub extern "C" fn _Unwind_SetGR() {}
#[no_mangle] pub extern "C" fn _Unwind_GetLanguageSpecificData() {}
#[no_mangle] pub extern "C" fn _Unwind_GetTextRelBase() {}
#[no_mangle] pub extern "C" fn _Unwind_SetIP() {}
#[no_mangle] pub extern "C" fn _Unwind_Backtrace() {}
#[no_mangle] pub extern "C" fn _Unwind_GetRegionStart() {}
#[no_mangle] pub extern "C" fn _Unwind_GetIPInfo() {}
#[no_mangle] pub extern "C" fn _Unwind_VRS_Set() {}
#[no_mangle] pub extern "C" fn _Unwind_VRS_Get() {}
#[no_mangle] pub extern "C" fn __gnu_unwind_frame() {}

#[no_mangle]
pub unsafe extern "C" fn memset(s: *mut u8, c: libc::c_int, n: libc::size_t) -> *mut u8 {
    for i in 0..n {
        *s.offset(i as isize) = c as u8;
    }
    s
}

#[no_mangle]
pub unsafe extern "C" fn memmove(dest: *mut u8, src: *const u8, n: libc::size_t) -> *mut u8 {
    let src = std::slice::from_raw_parts(src, n).to_vec();
    for i in 0..n {
        *dest.offset(i as isize) = src[i];
    }
    dest
}

#[no_mangle]
pub unsafe extern "C" fn memcpy(dest: *mut u8, src: *const u8, n: libc::size_t) -> *mut u8 {
    for i in 0..n {
        *dest.offset(i as isize) = *src.offset(i as isize);
    }
    dest
}

#[no_mangle]
pub unsafe extern "C" fn memcmp(s1: *const u8, s2: *const u8, n: libc::size_t) -> libc::c_int {
    for i in 0..n {
        let a = *s1.offset(i as isize) as libc::c_int;
        let b = *s2.offset(i as isize) as libc::c_int;
        if a != b {
            return a - b
        }
    }
    0
}

#[no_mangle]
pub unsafe extern "C" fn bcmp(s1: *const u8, s2: *const u8, n: libc::size_t) -> libc::c_int {
    memcmp(s1, s2, n)
}

unsafe fn load_all() {
    let r = *R_DEBUG;
    let mut m = r.map;
    while !m.is_null() {
        let path = CStr::from_ptr((*m).name).to_bytes();
        if !path.is_empty() && path != b"linux-vdso.so.1" {
            let path = Path::new(OsStr::from_bytes(path));
            let obj = std::fs::read(path)
                .map_err(|e| add_path_to_error(e, path))
                .expect("failed opening library to parse its symbol table");
            let obj = elf::ElfBytes::<elf::endian::AnyEndian>::minimal_parse(&obj)
                .map_err(|e| add_path_to_error(e, path)).expect("failed parsing library");

            let (sym_tab, str_tab) = obj.dynamic_symbol_table().expect("failed parsing dynamic symbol table").expect("library has no dynamic symbol table");
            for sym in sym_tab {
                let sym_name = str_tab.get(sym.st_name as usize).expect("failed getting a symbol name");
                let sym_name = std::ffi::CString::new(sym_name).unwrap();
                libc::dlsym(libc::RTLD_DEFAULT, sym_name.as_ptr());
            }
        }
        m = (*m).next;
    }
}

#[ctor]
unsafe fn init() {
    // std::io::copy(&mut std::fs::File::open("/proc/self/maps").unwrap(), &mut std::io::stdout()).unwrap();
    R_DEBUG = get_r_debug();
    assert_eq!((*R_DEBUG).state, 0);
    load_all();
    shim_all_libs();
}

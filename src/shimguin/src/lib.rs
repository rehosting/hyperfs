mod link_sys;

use cfg_if::cfg_if;
use ctor::ctor;
use lazy_static::lazy_static;
use object::read::Object;
use object::ObjectSymbol;
use rustix::mm::mprotect;
use rustix::mm::MprotectFlags;
use std::collections::HashMap;
use std::ffi::c_void;
use std::ffi::CStr;
use std::ffi::CString;
use std::ffi::OsStr;
use std::io::Write;
use std::os::unix::ffi::OsStrExt;
use std::path::Path;

extern "C" {
    static _r_debug: link_sys::r_debug;
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

fn add_path_to_error(e: impl std::error::Error, path: &Path) -> String {
    format!("path='{}', error=({e})", path.display())
}

unsafe fn shim_lib(path: &Path, base_addr: usize) {
    let obj = object::read::ReadCache::new(
        std::fs::File::open(path)
            .map_err(|e| add_path_to_error(e, path))
            .expect("failed opening library to parse its symbol table"),
    );
    let obj = object::read::File::parse(&obj)
        .map_err(|e| add_path_to_error(e, path))
        .expect("failed parsing library");
    for sym in obj.dynamic_symbols() {
        if let Some(repl_name) = SHIMS.get(sym.name().expect("symbol name not UTF-8")) {
            let repl_name_c = CString::new(repl_name.as_str()).unwrap();
            let repl_ptr = libc::dlsym(libc::RTLD_DEFAULT, repl_name_c.as_ptr());
            assert!(
                !repl_ptr.is_null(),
                "failed dlsym(RTLD_DEFAULT, \"{repl_name}\")"
            );
            write_jmp_shellcode(base_addr + sym.address() as usize, repl_ptr as usize);
        }
    }
}

unsafe extern "C" fn shim_all_libs() {
    let mut map = _r_debug.r_map;
    while !map.is_null() {
        let m = *map;
        let path = CStr::from_ptr(m.l_name).to_bytes();
        if !path.is_empty() && path != b"linux-vdso.so.1" {
            let path = Path::new(OsStr::from_bytes(path));
            shim_lib(Path::new(path), m.l_addr as usize);
        }
        map = m.l_next;
    }
}

fn make_jmp_shellcode(pc: usize, target: usize, mut out: &mut [u8]) -> std::io::Result<()> {
    cfg_if! {
        if #[cfg(all(target_arch = "x86_64", target_endian = "little"))] {
            let rel = target as isize - pc as isize - 5;
            out.write_all(&[0xe9])?;
            out.write_all(&rel.to_le_bytes())?;
        } else if #[cfg(all(target_arch = "arm", target_endian = "little"))] {
            let rel = target as isize - pc as isize - 2;
            out.write_all(&rel.to_le_bytes())?;
            out.write_all(&[0xea])?;
        } else if #[cfg(all(target_arch = "mips", target_endian = "little"))] {
            let rel = target as isize - pc as isize;
            out.write_all(&rel.to_le_bytes())?;
            out.write_all(&[0x08])?;
        } else if #[cfg(all(target_arch = "mips", target_endian = "big"))] {
            let rel = target as isize - pc as isize;
            out.write_all(&[0x08])?;
            out.write_all(&rel.to_be_bytes())?;
        } else {
            compile_error!("unsupported target arch");
        }
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

#[ctor]
unsafe fn init() {
    shim_all_libs();
    write_jmp_shellcode(_r_debug.r_brk as usize, shim_all_libs as usize);
}

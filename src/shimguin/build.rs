fn main() {
    let bindings = bindgen::Builder::default()
        .header_contents("link.h", "#include <link.h>")
        .generate()
        .expect("Unable to generate link.h bindings");
    let out_path = std::path::PathBuf::from(std::env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("link_bindings.rs"))
        .expect("Couldn't write link.h bindings");
}

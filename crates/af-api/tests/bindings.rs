//! `bindings/api.ts`, the TypeScript declarations of the contract, must be what
//! the Rust types generate. The test fails when they are stale; run it with
//! `AF_UPDATE_BINDINGS=1` to write them.

#[test]
fn the_typescript_bindings_are_current() {
    let path = concat!(env!("CARGO_MANIFEST_DIR"), "/bindings/api.ts");
    let generated = af_api::contract::typescript();
    if std::env::var_os("AF_UPDATE_BINDINGS").is_some() {
        if let Err(error) = std::fs::write(path, &generated) {
            panic!("{path}: {error}");
        }
        return;
    }
    let committed = std::fs::read_to_string(path).unwrap_or_default();
    assert!(
        committed.replace("\r\n", "\n") == generated,
        "{path} is stale. Run: AF_UPDATE_BINDINGS=1 cargo test -p af-api --test bindings"
    );
}

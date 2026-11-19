//! Goal G9's check: the application starts with the sample and its log says
//! what was read. Windows only, where the plan's check runs; macOS and Linux
//! come with packaging.

#[cfg(windows)]
#[test]
fn the_application_starts_with_the_sample_and_logs_what_it_read() {
    let sample = format!(
        "{}/../../testdata/sacm/sacm_example_core_argument.sacm.xmi",
        env!("CARGO_MANIFEST_DIR")
    );
    let output = std::process::Command::new(env!("CARGO_BIN_EXE_af-desktop"))
        .args(["--smoke", &sample])
        .output()
        .unwrap_or_else(|error| panic!("af-desktop did not start: {error}"));
    let log = String::from_utf8_lossy(&output.stderr);
    assert!(
        log.lines()
            .any(|line| line.starts_with("af-desktop opened ")
                && line.contains(" revision=1 nodes=8 edges=7 ")),
        "{log}"
    );
    assert!(log.contains("af-desktop smoke window=true"), "{log}");
    assert_eq!(output.status.code(), Some(0), "{log}");
}

#[cfg(windows)]
#[test]
fn a_file_that_does_not_open_fails_the_smoke_run() {
    let output = std::process::Command::new(env!("CARGO_BIN_EXE_af-desktop"))
        .args(["--smoke", "no-such-file.xmi"])
        .output()
        .unwrap_or_else(|error| panic!("af-desktop did not start: {error}"));
    let log = String::from_utf8_lossy(&output.stderr);
    assert!(
        log.contains("af-desktop unreadable file=no-such-file.xmi"),
        "{log}"
    );
    assert_eq!(output.status.code(), Some(2), "{log}");
}

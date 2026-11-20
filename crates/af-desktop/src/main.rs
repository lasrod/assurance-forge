//! `af-desktop [--smoke] [FILE]`: see the crate's documentation.

// A release build is a windowed application, with no console of its own; its
// log still reaches a parent that captures standard error.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

fn main() {
    let args = af_desktop::Args::parse(std::env::args().skip(1));
    std::process::exit(af_desktop::run(args));
}

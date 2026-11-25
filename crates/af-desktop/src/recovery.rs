//! The recovery copy on disk: the working folder beside the file (plan
//! decision R4), `<file name>.af/`, and the copy in it, `recovery.json`.
//!
//! The copy's bytes are the core's (`Core::recovery_copy`); this only puts
//! them on disk whole or not at all, finds them again, removes them, and
//! sets aside a copy the core cannot read (R7). The folder is made with the
//! first copy and removed when it is empty again, so ordinary use leaves
//! nothing beside the file.

use std::path::{Path, PathBuf};

/// The copy's name in the working folder.
const COPY: &str = "recovery.json";

/// The working folder of the file at `path`: `<file name>.af` beside it.
pub fn working_folder(path: &Path) -> Option<PathBuf> {
    let name = path.file_name()?;
    let mut folder = name.to_os_string();
    folder.push(".af");
    Some(match path.parent() {
        Some(parent) => parent.join(folder),
        None => PathBuf::from(folder),
    })
}

/// Where the copy of the file at `path` is kept.
pub fn copy_path(path: &Path) -> Option<PathBuf> {
    working_folder(path).map(|folder| folder.join(COPY))
}

/// Writes the copy of the file at `path`, whole or not at all, making the
/// working folder if it is not there.
///
/// # Errors
///
/// What the system refused: making the folder, or writing the copy.
pub fn keep(path: &Path, bytes: &[u8]) -> std::io::Result<()> {
    let folder = working_folder(path).ok_or(std::io::ErrorKind::InvalidInput)?;
    std::fs::create_dir_all(&folder)?;
    crate::write_whole(&folder.join(COPY), bytes)
}

/// The copy kept for the file at `path`, if there is one that can be read.
pub fn find(path: &Path) -> Option<Vec<u8>> {
    std::fs::read(copy_path(path)?).ok()
}

/// Removes the copy of the file at `path`, and its working folder if that is
/// empty then. A copy that is not there is removed already.
pub fn remove(path: &Path) {
    let Some(folder) = working_folder(path) else {
        return;
    };
    let _ = std::fs::remove_file(folder.join(COPY));
    // Fails, and leaves it, when anything else is in it.
    let _ = std::fs::remove_dir(&folder);
}

/// Sets the copy of the file at `path` aside, under a name no copy is
/// looked for by, so it is neither offered again nor written over (R7).
/// Answers where it now is.
pub fn set_aside(path: &Path, now: u64) -> Option<PathBuf> {
    let folder = working_folder(path)?;
    let aside = folder.join(format!("recovery.unreadable-{now}.json"));
    std::fs::rename(folder.join(COPY), &aside).ok()?;
    Some(aside)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn scratch(test: &str) -> PathBuf {
        let directory =
            std::env::temp_dir().join(format!("af-desktop-recovery-{test}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&directory);
        assert!(std::fs::create_dir_all(&directory).is_ok());
        directory
    }

    #[test]
    fn the_working_folder_is_beside_the_file_and_named_after_it() {
        let path = Path::new("cases").join("brake-system.sacm");
        assert_eq!(
            working_folder(&path),
            Some(Path::new("cases").join("brake-system.sacm.af"))
        );
        assert_eq!(
            copy_path(&path),
            Some(
                Path::new("cases")
                    .join("brake-system.sacm.af")
                    .join("recovery.json")
            )
        );
    }

    #[test]
    fn a_copy_is_kept_found_and_removed_with_its_folder() {
        let directory = scratch("keep");
        let file = directory.join("case.sacm.xmi");
        assert!(keep(&file, b"first").is_ok());
        assert!(keep(&file, b"second").is_ok());
        assert_eq!(find(&file), Some(b"second".to_vec()));
        // Nothing else is left in the folder by writing it whole.
        let entries = std::fs::read_dir(directory.join("case.sacm.xmi.af"))
            .map(|entries| entries.count())
            .unwrap_or_default();
        assert_eq!(entries, 1);
        remove(&file);
        assert_eq!(find(&file), None);
        assert!(!directory.join("case.sacm.xmi.af").exists());
        // Removing what is not there is nothing.
        remove(&file);
        let _ = std::fs::remove_dir_all(&directory);
    }

    #[test]
    fn a_folder_that_holds_more_than_the_copy_stays() {
        let directory = scratch("stays");
        let file = directory.join("case.sacm.xmi");
        assert!(keep(&file, b"copy").is_ok());
        let other = directory.join("case.sacm.xmi.af").join("draft.sacm");
        assert!(std::fs::write(&other, b"draft").is_ok());
        remove(&file);
        assert_eq!(find(&file), None);
        assert!(other.exists());
        let _ = std::fs::remove_dir_all(&directory);
    }

    #[test]
    fn a_copy_set_aside_is_kept_and_not_found_again() {
        let directory = scratch("aside");
        let file = directory.join("case.sacm.xmi");
        assert!(keep(&file, b"unreadable").is_ok());
        let aside = set_aside(&file, 42);
        assert_eq!(
            aside,
            Some(
                directory
                    .join("case.sacm.xmi.af")
                    .join("recovery.unreadable-42.json")
            )
        );
        assert_eq!(find(&file), None);
        // Removing the copy leaves the one set aside, and so the folder.
        remove(&file);
        assert!(aside.is_some_and(|aside| std::fs::read(aside).is_ok_and(|b| b == b"unreadable")));
        let _ = std::fs::remove_dir_all(&directory);
    }
}

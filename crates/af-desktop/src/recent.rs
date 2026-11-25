//! The files opened lately, which the welcome page lists.
//!
//! Kept as JSON in the application's own configuration directory, on this
//! machine only. A list that cannot be read is an empty one, and a list that
//! cannot be written is kept for the session: recent files are a convenience.

use std::path::{Path, PathBuf};

use af_api::contract::OpenResult;
use serde::{Deserialize, Serialize};

/// How many files the list keeps.
pub const LIMIT: usize = 10;

/// A file that was opened, as the page lists it (`RecentFile` in
/// `ui/src/core/Core.ts`).
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct RecentFile {
    /// What names it to `open_recent`: here, its path.
    pub id: String,
    /// Its file name.
    pub name: String,
    /// Where it is: its path.
    pub location: Option<String>,
    /// When it was last opened, in milliseconds since 1970.
    pub opened_at: u64,
    pub nodes: u32,
    pub edges: u32,
}

/// The list, most recent first, and the file it is kept in.
#[derive(Debug, Default)]
pub struct RecentFiles {
    store: Option<PathBuf>,
    files: Vec<RecentFile>,
}

impl RecentFiles {
    /// The list kept in `store`, or an empty one when there is none yet or
    /// it cannot be read.
    pub fn load(store: PathBuf) -> Self {
        let files = std::fs::read(&store)
            .ok()
            .and_then(|bytes| serde_json::from_slice::<Vec<RecentFile>>(&bytes).ok())
            .unwrap_or_default();
        Self {
            store: Some(store),
            files,
        }
    }

    pub fn files(&self) -> &[RecentFile] {
        &self.files
    }

    /// Puts `path` first, once, when `result` says it opened. A refused file
    /// is not a recent file.
    pub fn record(&mut self, path: &Path, result: &OpenResult, now: u64) {
        let OpenResult::Opened { nodes, edges, .. } = result else {
            return;
        };
        self.record_counts(path, *nodes, *edges, now);
    }

    /// Puts `path` first, once, holding `nodes` and `edges`: a file opened,
    /// or one saved as.
    pub fn record_counts(&mut self, path: &Path, nodes: u32, edges: u32, now: u64) {
        let path = known_path(path);
        let id = path.display().to_string();
        // A file is listed once, whichever spelling of its path it was opened
        // by now or was kept under before.
        self.files
            .retain(|file| file.id != id && known_path(Path::new(&file.id)) != path);
        self.files.insert(
            0,
            RecentFile {
                name: path
                    .file_name()
                    .map_or_else(|| id.clone(), |name| name.to_string_lossy().into_owned()),
                location: Some(id.clone()),
                id,
                opened_at: now,
                nodes,
                edges,
            },
        );
        self.files.truncate(LIMIT);
        self.save();
    }

    /// Takes the file with this id off the list.
    pub fn forget(&mut self, id: &str) {
        let before = self.files.len();
        self.files.retain(|file| file.id != id);
        if self.files.len() != before {
            self.save();
        }
    }

    fn save(&self) {
        let Some(store) = &self.store else {
            return;
        };
        if let Some(directory) = store.parent() {
            let _ = std::fs::create_dir_all(directory);
        }
        if let Ok(json) = serde_json::to_vec_pretty(&self.files) {
            let _ = std::fs::write(store, json);
        }
    }
}

/// The path a file is known by: its canonical path, so that one file has one
/// id whatever case, separators or `..` it was named with, or its absolute
/// path when it cannot be resolved (it is gone).
fn known_path(path: &Path) -> PathBuf {
    match std::fs::canonicalize(path) {
        Ok(canonical) => without_verbatim_prefix(canonical),
        Err(_) => std::path::absolute(path).unwrap_or_else(|_| path.to_path_buf()),
    }
}

/// Windows resolves a path to its verbatim form (`\\?\C:\…`), which no one
/// types and the list shows: a drive or network path is given back without it.
fn without_verbatim_prefix(path: PathBuf) -> PathBuf {
    let Some(text) = path.to_str() else {
        return path;
    };
    if let Some(unc) = text.strip_prefix(r"\\?\UNC\") {
        return PathBuf::from(format!(r"\\{unc}"));
    }
    match text.strip_prefix(r"\\?\") {
        Some(drive) if drive.as_bytes().get(1) == Some(&b':') => PathBuf::from(drive),
        _ => path,
    }
}

/// Now, in milliseconds since 1970.
pub fn now() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map_or(0, |since| {
            u64::try_from(since.as_millis()).unwrap_or(u64::MAX)
        })
}

#[cfg(test)]
mod tests {
    use af_api::contract::{DiagnosticCounts, Refusal};

    use super::*;

    fn opened(nodes: u32) -> OpenResult {
        OpenResult::Opened {
            revision: 1,
            diagnostics: DiagnosticCounts::default(),
            nodes,
            edges: nodes.saturating_sub(1),
            view_codes: 0,
        }
    }

    fn store(name: &str) -> PathBuf {
        let directory =
            std::env::temp_dir().join(format!("af-desktop-recent-{}-{name}", std::process::id()));
        let _ = std::fs::remove_dir_all(&directory);
        directory.join("nested").join("recent.json")
    }

    #[test]
    fn the_file_opened_last_is_first_and_is_listed_once() {
        let mut recent = RecentFiles::default();
        recent.record(Path::new("a.xmi"), &opened(8), 1);
        recent.record(Path::new("b.xmi"), &opened(3), 2);
        recent.record(Path::new("a.xmi"), &opened(9), 3);
        let names: Vec<(&str, u32, u64)> = recent
            .files()
            .iter()
            .map(|file| (file.name.as_str(), file.nodes, file.opened_at))
            .collect();
        assert_eq!(names, [("a.xmi", 9, 3), ("b.xmi", 3, 2)]);
        // The id is the whole path, which is where the file is.
        let first = &recent.files()[0];
        assert!(Path::new(&first.id).is_absolute(), "{}", first.id);
        assert_eq!(first.location.as_deref(), Some(first.id.as_str()));
    }

    #[test]
    fn one_file_is_listed_once_whatever_its_path_is_spelt() {
        let store = store("spelling");
        let Some(directory) = store.parent() else {
            return;
        };
        assert!(std::fs::create_dir_all(directory.join("sub")).is_ok());
        let file = directory.join("Case.xmi");
        assert!(std::fs::write(&file, b"").is_ok());

        let mut recent = RecentFiles::default();
        recent.record(&file, &opened(8), 1);
        recent.record(
            &directory.join("sub").join("..").join("Case.xmi"),
            &opened(8),
            2,
        );
        // Windows does not tell case apart in a path.
        if cfg!(windows) {
            recent.record(&directory.join("case.XMI"), &opened(8), 3);
        }
        assert_eq!(recent.files().len(), 1, "{:?}", recent.files());
        let id = &recent.files()[0].id;
        assert!(!id.starts_with(r"\\?\"), "{id}");
        assert!(id.ends_with("Case.xmi"), "{id}");
        if let Some(parent) = directory.parent() {
            let _ = std::fs::remove_dir_all(parent);
        }
    }

    #[test]
    fn a_verbatim_path_is_given_back_as_it_is_typed() {
        assert_eq!(
            without_verbatim_prefix(PathBuf::from(r"\\?\C:\cases\a.xmi")),
            PathBuf::from(r"C:\cases\a.xmi")
        );
        assert_eq!(
            without_verbatim_prefix(PathBuf::from(r"\\?\UNC\server\share\a.xmi")),
            PathBuf::from(r"\\server\share\a.xmi")
        );
        assert_eq!(
            without_verbatim_prefix(PathBuf::from("/home/a.xmi")),
            PathBuf::from("/home/a.xmi")
        );
    }

    #[test]
    fn a_refused_file_is_not_recorded() {
        let mut recent = RecentFiles::default();
        let refused = OpenResult::Refused(Refusal {
            code: "SACM-XML-001".to_owned(),
            template: String::new(),
            arguments: Vec::new(),
            line: None,
            column: None,
        });
        recent.record(Path::new("a.xmi"), &refused, 1);
        assert!(recent.files().is_empty());
    }

    #[test]
    fn the_list_keeps_the_latest_ten() {
        let mut recent = RecentFiles::default();
        for n in 0..12u64 {
            recent.record(Path::new(&format!("{n}.xmi")), &opened(1), n);
        }
        assert_eq!(recent.files().len(), LIMIT);
        assert_eq!(recent.files()[0].name, "11.xmi");
        assert_eq!(recent.files()[LIMIT - 1].name, "2.xmi");
    }

    #[test]
    fn the_list_is_kept_between_runs_and_a_forgotten_file_stays_forgotten() {
        let store = store("kept");
        let mut recent = RecentFiles::load(store.clone());
        assert!(recent.files().is_empty());
        recent.record(Path::new("a.xmi"), &opened(8), 1);
        recent.record(Path::new("b.xmi"), &opened(3), 2);

        let mut again = RecentFiles::load(store.clone());
        assert_eq!(again.files(), recent.files());
        let id = again.files()[0].id.clone();
        again.forget(&id);
        again.forget("not on the list");
        let last = RecentFiles::load(store.clone());
        assert_eq!(last.files().len(), 1);
        assert_eq!(last.files()[0].name, "a.xmi");
        if let Some(directory) = store.parent().and_then(Path::parent) {
            let _ = std::fs::remove_dir_all(directory);
        }
    }

    #[test]
    fn a_list_that_cannot_be_read_is_empty() {
        let store = store("broken");
        if let Some(directory) = store.parent() {
            let _ = std::fs::create_dir_all(directory);
        }
        let _ = std::fs::write(&store, b"not json");
        assert!(RecentFiles::load(store.clone()).files().is_empty());
        if let Some(directory) = store.parent().and_then(Path::parent) {
            let _ = std::fs::remove_dir_all(directory);
        }
    }
}

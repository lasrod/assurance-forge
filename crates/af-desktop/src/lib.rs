//! The desktop application: the interface (`ui/`) in a Tauri window, on the
//! core (`af-api`).
//!
//! This shell, `af-mcp` and `af-wasm` are the only crates that touch the
//! outside world. Here that is the file system (a file named on the command
//! line, the open and save dialogs, saving, the list of recent files) and the
//! window. The page loads DATUM's measured faces once and hands the same bytes
//! to the core, as it does in the browser.
//!
//! A save writes what the core gives (`Core::write`, checked to read back as
//! the document) to a temporary file beside the target and renames it over
//! the target, so a failed save leaves the file as it was (plan decision
//! S4). A file that has changed on disk since it was read or last written is
//! not overwritten (S5). Closing the window with changes that are not saved
//! is held here and asked about in the page (S9).
//!
//! The commands are the browser core's (`ui/src/core/Core.ts`), so the
//! interface is the same code on both (plan decision D1).
//!
//! `af-desktop [--smoke] [FILE]` opens `FILE` at start and writes one line
//! saying what was read to standard error. `--smoke` exits after that line:
//! it is how CI checks that the application starts.

pub mod recent;

use std::hash::{DefaultHasher, Hash as _, Hasher as _};
use std::io::Write as _;
use std::path::{Path, PathBuf};
use std::sync::Mutex;
use std::sync::atomic::{AtomicBool, Ordering};

use af_api::Core;
use af_api::contract::{OpenResult, OpenedFile as Opened, SaveResult, WriteResult};
use af_layout::{FaceName, Fonts, Style};
use serde::Deserialize;
use tauri::ipc::{InvokeBody, Request, Response};
use tauri::menu::{Menu, MenuEvent, MenuItem, PredefinedMenuItem, Submenu};
use tauri::{Emitter, Manager, State, WindowEvent};
use tauri_plugin_dialog::DialogExt;

use recent::{RecentFile, RecentFiles};

/// What the command line asks for.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Args {
    /// A file to open at start.
    pub file: Option<PathBuf>,
    /// Exit once the file named has been read and logged.
    pub smoke: bool,
}

impl Args {
    /// The arguments after the program's name. The first one that is not a
    /// flag is the file; flags the shell does not know are ignored.
    pub fn parse<I: IntoIterator<Item = String>>(args: I) -> Self {
        let mut parsed = Self::default();
        for arg in args {
            if arg == "--smoke" {
                parsed.smoke = true;
            } else if !arg.starts_with("--") && parsed.file.is_none() {
                parsed.file = Some(PathBuf::from(arg));
            }
        }
        parsed
    }
}

/// The line written to standard error for a file named on the command line:
/// `af-desktop opened file=… revision=1 nodes=8 edges=7 errors=0 warnings=0`,
/// or `refused` with the code and location. Codes, not sentences: it is read
/// by scripts.
pub fn log_line(path: &Path, result: &OpenResult) -> String {
    let file = path.display();
    match result {
        OpenResult::Opened {
            revision,
            diagnostics,
            nodes,
            edges,
            ..
        } => format!(
            "af-desktop opened file={file} revision={revision} nodes={nodes} edges={edges} errors={} warnings={}",
            diagnostics.errors, diagnostics.warnings
        ),
        OpenResult::Refused(refusal) => format!(
            "af-desktop refused file={file} code={} line={} column={}",
            refusal.code,
            refusal
                .line
                .map_or_else(|| "-".to_owned(), |line| line.to_string()),
            refusal
                .column
                .map_or_else(|| "-".to_owned(), |column| column.to_string())
        ),
    }
}

/// The five measured faces, sent by the page as one body with their lengths
/// in a header, in [`Fonts::new`]'s order.
///
/// # Errors
///
/// `font-lengths` when the lengths do not add up to the body, or
/// `font-unreadable:<face>` for a face the shaper cannot read: the codes
/// `af-wasm` gives.
pub fn unpack_fonts(body: &[u8], lengths: &str) -> Result<Fonts, String> {
    let lengths: Vec<usize> = lengths
        .split(',')
        .map(|length| length.trim().parse::<usize>())
        .collect::<Result<_, _>>()
        .map_err(|_| "font-lengths".to_owned())?;
    if lengths.len() != 5 || lengths.iter().sum::<usize>() != body.len() {
        return Err("font-lengths".to_owned());
    }
    let mut faces = Vec::with_capacity(5);
    let mut start = 0;
    for length in lengths {
        faces.push(body[start..start + length].to_vec());
        start += length;
    }
    let [
        sans_regular,
        sans_semibold,
        japanese_regular,
        japanese_semibold,
        mono_regular,
    ] = <[Vec<u8>; 5]>::try_from(faces).map_err(|_| "font-lengths".to_owned())?;
    Fonts::new(
        sans_regular,
        sans_semibold,
        japanese_regular,
        japanese_semibold,
        mono_regular,
    )
    .map_err(|error| format!("font-unreadable:{}", face_name(error.face)))
}

fn face_name(face: FaceName) -> &'static str {
    match face {
        FaceName::SansRegular => "SansRegular",
        FaceName::SansSemiBold => "SansSemiBold",
        FaceName::JapaneseRegular => "JapaneseRegular",
        FaceName::JapaneseSemiBold => "JapaneseSemiBold",
        FaceName::MonoRegular => "MonoRegular",
    }
}

/// The name a window shows for `path`: its file name.
fn file_name(path: &Path) -> String {
    path.file_name().map_or_else(
        || path.display().to_string(),
        |name| name.to_string_lossy().into_owned(),
    )
}

/// What tells one content of a file from another: a hash of its bytes. It is
/// compared only within one run of the application.
fn digest(bytes: &[u8]) -> u64 {
    let mut hasher = DefaultHasher::new();
    bytes.hash(&mut hasher);
    hasher.finish()
}

/// The file the open document was read from or last saved to.
#[derive(Clone, Debug, PartialEq, Eq)]
struct FileOnDisk {
    path: PathBuf,
    /// What it held then, as `digest` tells it.
    content: u64,
}

/// Writes `bytes` to `path` whole or not at all: to a temporary file in the
/// same folder, flushed to disk, then renamed over `path`. On failure the
/// temporary file is removed and `path` is as it was.
///
/// A link is followed: the file it names is the one written, and the link
/// stays. A read-only file is not written. The new file has the old one's
/// permissions.
///
/// # Errors
///
/// What the system refused: creating, writing or renaming the file;
/// `PermissionDenied` for a read-only file.
pub fn write_whole(path: &Path, bytes: &[u8]) -> std::io::Result<()> {
    let target = match std::fs::canonicalize(path) {
        Ok(target) => target,
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => path.to_owned(),
        Err(error) => return Err(error),
    };
    let existing = std::fs::metadata(&target)
        .ok()
        .filter(std::fs::Metadata::is_file);
    if existing
        .as_ref()
        .is_some_and(|metadata| metadata.permissions().readonly())
    {
        return Err(std::io::ErrorKind::PermissionDenied.into());
    }
    let name = target
        .file_name()
        .ok_or(std::io::ErrorKind::InvalidInput)?
        .to_string_lossy();
    let folder = target
        .parent()
        .filter(|parent| !parent.as_os_str().is_empty())
        .unwrap_or_else(|| Path::new("."));
    let temporary = folder.join(format!(".{name}.af-save-{}", std::process::id()));
    let written = (|| {
        let mut file = std::fs::File::create(&temporary)?;
        file.write_all(bytes)?;
        file.sync_all()?;
        drop(file);
        if let Some(metadata) = &existing {
            std::fs::set_permissions(&temporary, metadata.permissions())?;
        }
        std::fs::rename(&temporary, &target)
    })();
    if written.is_err() {
        let _ = std::fs::remove_file(&temporary);
    }
    written
}

/// AF-SAVE-004: the file has changed on disk since it was read or saved.
const CHANGED_ON_DISK: &str = "AF-SAVE-004";
/// AF-SAVE-005: the system refused to write the file.
const NOT_WRITABLE: &str = "AF-SAVE-005";
/// AF-SAVE-006: the document changed while it was being saved.
const CHANGED_MEANWHILE: &str = "AF-SAVE-006";

fn save_refused(code: &str) -> SaveResult {
    SaveResult::Refused {
        code: code.to_owned(),
    }
}

/// What the window's commands share: the core, the file named on the
/// command line until the page has taken it, the file the open document is
/// saved to, and the recent files.
pub struct Desktop {
    core: Core,
    initial: Option<Opened>,
    /// The file named on the command line, when it opened: recorded as a
    /// recent file once the list is loaded.
    initial_path: Option<(PathBuf, OpenResult)>,
    /// Where Save writes; `None` when nothing is open, or the open document
    /// came without a path (a file dropped on the window): Save is then
    /// Save As… (S6).
    file: Option<FileOnDisk>,
    recent: RecentFiles,
}

impl Desktop {
    /// A core with no fonts yet, with `args.file` opened if there is one.
    /// Returns the log line for that file.
    pub fn start(args: &Args) -> (Self, Option<String>) {
        let mut core = Core::without_fonts(Style::default(), "en");
        let mut initial = None;
        let mut initial_path = None;
        let mut file = None;
        let mut line = None;
        if let Some(path) = &args.file {
            match std::fs::read(path) {
                Ok(bytes) => {
                    let result = core.open(&bytes);
                    if matches!(result, OpenResult::Opened { .. }) {
                        file = Some(FileOnDisk {
                            path: path.clone(),
                            content: digest(&bytes),
                        });
                    }
                    line = Some(log_line(path, &result));
                    initial_path = Some((path.clone(), result.clone()));
                    initial = Some(Opened {
                        name: file_name(path),
                        result,
                    });
                }
                Err(error) => {
                    line = Some(format!(
                        "af-desktop unreadable file={} error={:?}",
                        path.display(),
                        error.kind()
                    ));
                }
            }
        }
        (
            Self {
                core,
                initial,
                initial_path,
                file,
                recent: RecentFiles::default(),
            },
            line,
        )
    }

    /// Reads the recent files kept in `store`, and puts the file named on the
    /// command line first if it opened.
    pub fn load_recent(&mut self, store: PathBuf) {
        self.recent = RecentFiles::load(store);
        if let Some((path, result)) = self.initial_path.take() {
            self.recent.record(&path, &result, recent::now());
        }
    }

    /// Reads the file at `path` and opens it. A file that opens becomes the
    /// most recent one.
    ///
    /// # Errors
    ///
    /// `file-unreadable` when the file cannot be read.
    pub fn open_path(&mut self, path: &Path) -> Result<Opened, String> {
        let bytes = std::fs::read(path).map_err(|_| "file-unreadable".to_owned())?;
        Ok(self.open_bytes(path, &bytes))
    }

    /// Opens the bytes read from `path`. A file that opens becomes the most
    /// recent one, and the one Save writes.
    fn open_bytes(&mut self, path: &Path, bytes: &[u8]) -> Opened {
        let result = self.core.open(bytes);
        self.recent.record(path, &result, recent::now());
        self.file = matches!(result, OpenResult::Opened { .. }).then(|| FileOnDisk {
            path: path.to_owned(),
            content: digest(bytes),
        });
        Opened {
            name: file_name(path),
            result,
        }
    }

    /// Opens bytes the page read itself (a drop): there is no path to save
    /// to, so Save is Save As… (S6).
    pub fn open_unplaced(&mut self, bytes: &[u8]) -> OpenResult {
        self.file = None;
        self.core.open(bytes)
    }

    /// Closes what is open: nothing to save after.
    pub fn close(&mut self) {
        self.file = None;
        self.core.close();
    }

    /// Where Save writes, or `None` when Save is Save As….
    pub fn save_path(&self) -> Option<&Path> {
        self.file.as_ref().map(|file| file.path.as_path())
    }

    /// Whether the open document has changes that are not saved.
    pub fn is_modified(&self) -> bool {
        self.core.history().modified
    }

    /// Saves the open document, as it was at `revision` when the page asked,
    /// to `path`, whole or not at all (S4). A document that has changed
    /// since, or another file opened meanwhile, is not saved (AF-SAVE-006):
    /// what is written is what the user asked to save.
    ///
    /// `chosen` is whether the user chose `path` in the save dialog, which
    /// asks before it replaces a file. When not (File ▸ Save), the file is
    /// not overwritten if it holds something other than what it held when it
    /// was read or last written (S5). The file saved to is the one Save
    /// writes from then on, and the most recent file.
    pub fn save_to(&mut self, path: &Path, revision: u32, chosen: bool) -> SaveResult {
        if revision != self.core.revision() {
            return save_refused(CHANGED_MEANWHILE);
        }
        let (revision, xmi) = match self.core.write() {
            WriteResult::Written { revision, xmi } => (revision, xmi),
            WriteResult::Refused { code } => return SaveResult::Refused { code },
        };
        if !chosen && self.changed_on_disk(path) {
            return save_refused(CHANGED_ON_DISK);
        }
        if write_whole(path, xmi.as_bytes()).is_err() {
            return save_refused(NOT_WRITABLE);
        }
        if !self.core.mark_saved(revision) {
            return save_refused(CHANGED_MEANWHILE);
        }
        self.file = Some(FileOnDisk {
            path: path.to_owned(),
            content: digest(xmi.as_bytes()),
        });
        if let Some((nodes, edges)) = self.core.counts() {
            self.recent.record_counts(path, nodes, edges, recent::now());
        }
        SaveResult::Saved {
            revision,
            name: file_name(path),
        }
    }

    /// Whether the file at `path` holds something other than what the
    /// document was read from or last written as: another program has
    /// changed it (S5). What it holds is compared, not its time or length,
    /// so a change of the same length is seen and a file touched but not
    /// changed is not taken as changed. A file that is not there has nothing
    /// to lose; one that cannot be read cannot be written either, and the
    /// write is refused for it.
    fn changed_on_disk(&self, path: &Path) -> bool {
        let Ok(now) = std::fs::read(path) else {
            return false;
        };
        self.file
            .as_ref()
            .filter(|file| same_file(&file.path, path))
            .is_none_or(|file| digest(&now) != file.content)
    }

    /// Opens the recent file with this id. A file that is no longer there is
    /// taken off the list, and the answer is `None`; what is open stays open.
    ///
    /// # Errors
    ///
    /// `file-unreadable` when the file is there but cannot be read (it is
    /// locked, or on a drive that is not connected): it stays on the list.
    pub fn open_recent(&mut self, id: &str) -> Result<Option<Opened>, String> {
        if !self.recent.files().iter().any(|file| file.id == id) {
            return Ok(None);
        }
        let path = Path::new(id);
        match std::fs::read(path) {
            Ok(bytes) => Ok(Some(self.open_bytes(path, &bytes))),
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => {
                self.recent.forget(id);
                Ok(None)
            }
            Err(_) => Err("file-unreadable".to_owned()),
        }
    }

    pub fn recent_files(&self) -> &[RecentFile] {
        self.recent.files()
    }

    /// Whether the file named on the command line opened.
    pub fn initial_opened(&self) -> bool {
        matches!(
            self.initial,
            Some(Opened {
                result: OpenResult::Opened { .. },
                ..
            })
        )
    }
}

/// Whether `a` and `b` name one file, whichever spelling of its path.
fn same_file(a: &Path, b: &Path) -> bool {
    a == b
        || matches!(
            (std::fs::canonicalize(a), std::fs::canonicalize(b)),
            (Ok(a), Ok(b)) if a == b
        )
}

type Shared<'a> = State<'a, Mutex<Desktop>>;

fn lock<'a>(state: &'a Shared<'_>) -> Result<std::sync::MutexGuard<'a, Desktop>, String> {
    state.lock().map_err(|_| "core-poisoned".to_owned())
}

fn raw(request: &Request<'_>) -> Result<Vec<u8>, String> {
    match request.body() {
        InvokeBody::Raw(bytes) => Ok(bytes.clone()),
        InvokeBody::Json(_) => Err("body-not-bytes".to_owned()),
    }
}

fn header<'a>(request: &'a Request<'_>, name: &str) -> Option<&'a str> {
    request.headers().get(name)?.to_str().ok()
}

/// The page has loaded the fonts: they arrive as one body (see
/// [`unpack_fonts`]), with the interface language in a header.
#[tauri::command]
fn start(request: Request<'_>, state: Shared<'_>) -> Result<(), String> {
    let lengths = header(&request, "af-font-lengths").ok_or("font-lengths")?;
    let fonts = unpack_fonts(&raw(&request)?, lengths)?;
    let mut desktop = lock(&state)?;
    desktop.core.set_fonts(fonts);
    if let Some(language) = header(&request, "af-language") {
        desktop.core.set_language(language);
    }
    Ok(())
}

/// The file named on the command line, once: what the page shows first.
#[tauri::command]
fn initial_file(state: Shared<'_>) -> Result<Option<Opened>, String> {
    Ok(lock(&state)?.initial.take())
}

/// Reads bytes the page has, from a drop or a file input.
#[tauri::command]
fn open(request: Request<'_>, state: Shared<'_>) -> Result<OpenResult, String> {
    let bytes = raw(&request)?;
    Ok(lock(&state)?.open_unplaced(&bytes))
}

/// Shows the system's open dialog and opens the file chosen, or answers
/// `None` when the dialog is cancelled. Not on the main thread: the dialog
/// blocks.
#[tauri::command]
async fn open_dialog(
    app: tauri::AppHandle,
    window: tauri::WebviewWindow,
    state: Shared<'_>,
) -> Result<Option<Opened>, String> {
    // The window is the dialog's parent, so it takes no input while the
    // dialog is open.
    let Some(chosen) = app
        .dialog()
        .file()
        .set_parent(&window)
        .add_filter("SACM", &["xmi", "sacm", "xml"])
        .blocking_pick_file()
    else {
        return Ok(None);
    };
    let path = chosen
        .into_path()
        .map_err(|_| "file-unreadable".to_owned())?;
    lock(&state)?.open_path(&path).map(Some)
}

/// The files opened lately, most recent first.
#[tauri::command]
fn recent_files(state: Shared<'_>) -> Result<Vec<RecentFile>, String> {
    Ok(lock(&state)?.recent_files().to_vec())
}

/// Opens a recent file, or answers `None` when it is no longer there: it is
/// then off the list.
#[tauri::command]
fn open_recent(id: String, state: Shared<'_>) -> Result<Option<Opened>, String> {
    lock(&state)?.open_recent(&id)
}

#[tauri::command]
fn forget_recent(id: String, state: Shared<'_>) -> Result<(), String> {
    lock(&state)?.recent.forget(&id);
    Ok(())
}

#[tauri::command]
fn outline(state: Shared<'_>) -> Result<Option<af_api::contract::Outline>, String> {
    Ok(lock(&state)?.core.outline())
}

#[tauri::command]
fn packages(state: Shared<'_>) -> Result<Vec<af_api::contract::Package>, String> {
    Ok(lock(&state)?.core.packages())
}

#[tauri::command]
fn close(state: Shared<'_>) -> Result<(), String> {
    lock(&state)?.close();
    Ok(())
}

/// The system's save dialog, starting at the file Save writes, or with
/// `name` when there is none; `None` when it is cancelled. The window is its
/// parent, so nothing else can be done in the window while it is open. It
/// blocks: not on the main thread.
fn choose_save_path(
    app: &tauri::AppHandle,
    window: &tauri::WebviewWindow,
    start: Option<PathBuf>,
    name: &str,
) -> Option<PathBuf> {
    let mut dialog = app
        .dialog()
        .file()
        .set_parent(window)
        .add_filter("SACM", &["xmi", "sacm", "xml"]);
    match start {
        Some(path) => {
            if let Some(folder) = path.parent() {
                dialog = dialog.set_directory(folder);
            }
            dialog = dialog.set_file_name(file_name(&path));
        }
        None => dialog = dialog.set_file_name(name),
    }
    dialog.blocking_save_file()?.into_path().ok()
}

/// File ▸ Save of the document at `revision`: to the file it was read from
/// or last saved to; Save As… when there is none (S6). `name` is the file
/// name the page shows, which the save dialog then starts with.
#[tauri::command]
async fn save(
    app: tauri::AppHandle,
    window: tauri::WebviewWindow,
    name: String,
    revision: u32,
    state: Shared<'_>,
) -> Result<SaveResult, String> {
    let known = lock(&state)?.save_path().map(Path::to_owned);
    let (path, chosen) = match known {
        Some(path) => (path, false),
        None => match choose_save_path(&app, &window, None, &name) {
            Some(path) => (path, true),
            None => return Ok(SaveResult::Cancelled),
        },
    };
    Ok(lock(&state)?.save_to(&path, revision, chosen))
}

/// File ▸ Save As… of the document at `revision`: to a file the user names
/// in the save dialog.
#[tauri::command]
async fn save_as(
    app: tauri::AppHandle,
    window: tauri::WebviewWindow,
    name: String,
    revision: u32,
    state: Shared<'_>,
) -> Result<SaveResult, String> {
    let known = lock(&state)?.save_path().map(Path::to_owned);
    let Some(path) = choose_save_path(&app, &window, known, &name) else {
        return Ok(SaveResult::Cancelled);
    };
    Ok(lock(&state)?.save_to(&path, revision, true))
}

/// The page has asked about the changes that are not saved, and the window
/// may close: it closes without asking again.
#[tauri::command]
fn quit(window: tauri::WebviewWindow) -> Result<(), String> {
    window.destroy().map_err(|_| "window-not-closed".to_owned())
}

/// The event the page hears when the window is to close with changes that
/// are not saved: it says it heard (`close_heard`), asks, and answers with
/// `quit` or not at all.
pub const CLOSE_REQUESTED: &str = "af-close-requested";

/// Whether the page heard the last close of the window that was held for
/// it. A page that has stopped (an error took it down, or it is reloading)
/// hears nothing, and would hold the window open for good: the next close
/// is then not held.
#[derive(Debug, Default)]
pub struct CloseHold {
    unheard: AtomicBool,
}

impl CloseHold {
    /// Whether to hold a close of the window, which has changes that are
    /// not saved when `modified`, for the page to ask about first (S9).
    pub fn hold(&self, modified: bool) -> bool {
        modified && !self.unheard.swap(true, Ordering::SeqCst)
    }

    /// The page heard the close held for it.
    pub fn heard(&self) {
        self.unheard.store(false, Ordering::SeqCst);
    }
}

/// The page heard the window's close, and asks about the changes.
#[tauri::command]
fn close_heard(hold: State<'_, CloseHold>) {
    hold.heard();
}

/// The id of the application's own Quit item (macOS).
const QUIT_ITEM: &str = "af-quit";

/// The application's menu on macOS: Tauri's default, but for Quit, which is
/// the application's own item. The system's Quit ends the process with no
/// close of the window, so changes that are not saved would be lost without
/// asking; this one closes the window as its close button does (S9).
#[cfg_attr(not(target_os = "macos"), allow(dead_code))]
fn macos_menu<R: tauri::Runtime>(app: &tauri::AppHandle<R>) -> tauri::Result<Menu<R>> {
    let name = app.package_info().name.clone();
    let quit = MenuItem::with_id(
        app,
        QUIT_ITEM,
        format!("Quit {name}"),
        true,
        Some("CmdOrCtrl+Q"),
    )?;
    let application = Submenu::with_items(
        app,
        name,
        true,
        &[
            &PredefinedMenuItem::about(app, None, None)?,
            &PredefinedMenuItem::separator(app)?,
            &PredefinedMenuItem::services(app, None)?,
            &PredefinedMenuItem::separator(app)?,
            &PredefinedMenuItem::hide(app, None)?,
            &PredefinedMenuItem::hide_others(app, None)?,
            &PredefinedMenuItem::separator(app)?,
            &quit,
        ],
    )?;
    let edit = Submenu::with_items(
        app,
        "Edit",
        true,
        &[
            &PredefinedMenuItem::undo(app, None)?,
            &PredefinedMenuItem::redo(app, None)?,
            &PredefinedMenuItem::separator(app)?,
            &PredefinedMenuItem::cut(app, None)?,
            &PredefinedMenuItem::copy(app, None)?,
            &PredefinedMenuItem::paste(app, None)?,
            &PredefinedMenuItem::select_all(app, None)?,
        ],
    )?;
    let view = Submenu::with_items(
        app,
        "View",
        true,
        &[&PredefinedMenuItem::fullscreen(app, None)?],
    )?;
    let window = Submenu::with_items(
        app,
        "Window",
        true,
        &[
            &PredefinedMenuItem::minimize(app, None)?,
            &PredefinedMenuItem::maximize(app, None)?,
            &PredefinedMenuItem::separator(app)?,
            &PredefinedMenuItem::close_window(app, None)?,
        ],
    )?;
    Menu::with_items(app, &[&application, &edit, &view, &window])
}

/// The application's own Quit: closes the window, which asks first about
/// changes that are not saved, and the application ends with it.
#[cfg_attr(not(target_os = "macos"), allow(dead_code))]
fn on_menu<R: tauri::Runtime>(app: &tauri::AppHandle<R>, event: &MenuEvent) {
    if event.id() == QUIT_ITEM {
        match app.get_webview_window("main") {
            Some(window) => {
                let _ = window.close();
            }
            None => app.exit(0),
        }
    }
}

/// `DiagramAt` as JSON, written from the core's diagram without copying it.
#[tauri::command]
fn diagram(state: Shared<'_>) -> Result<Response, String> {
    let mut desktop = lock(&state)?;
    let json = match desktop.core.diagram_ref() {
        Some(at) => serde_json::to_string(&at),
        None => serde_json::to_string(&None::<()>),
    }
    .map_err(|_| "core-serialize".to_owned())?;
    Ok(Response::new(json))
}

#[tauri::command]
fn element(
    id: String,
    state: Shared<'_>,
) -> Result<Option<af_api::contract::ElementDetails>, String> {
    Ok(lock(&state)?.core.element(&id))
}

#[tauri::command]
fn relationship(
    id: String,
    state: Shared<'_>,
) -> Result<Option<af_api::contract::RelationshipDetails>, String> {
    Ok(lock(&state)?.core.relationship(&id))
}

#[tauri::command]
fn add_choices(
    parent: Option<String>,
    state: Shared<'_>,
) -> Result<Option<af_api::contract::AddChoices>, String> {
    Ok(lock(&state)?.core.add_choices(parent.as_deref()))
}

#[tauri::command]
fn add(
    parent: Option<String>,
    kind: af_api::contract::ElementKind,
    revision: u32,
    state: Shared<'_>,
) -> Result<af_api::contract::EditResult, String> {
    Ok(lock(&state)?.core.add(parent.as_deref(), kind, revision))
}

#[tauri::command]
fn removal(id: String, state: Shared<'_>) -> Result<Option<af_api::contract::Removal>, String> {
    Ok(lock(&state)?.core.removal(&id))
}

#[tauri::command]
fn remove(
    id: String,
    revision: u32,
    state: Shared<'_>,
) -> Result<af_api::contract::EditResult, String> {
    Ok(lock(&state)?.core.remove(&id, revision))
}

#[tauri::command]
fn set_text(
    id: String,
    field: af_api::contract::TextField,
    text: String,
    state: Shared<'_>,
) -> Result<af_api::contract::EditResult, String> {
    Ok(lock(&state)?.core.set_text(&id, field, &text))
}

#[tauri::command]
fn undo(state: Shared<'_>) -> Result<af_api::contract::EditResult, String> {
    Ok(lock(&state)?.core.undo())
}

#[tauri::command]
fn redo(state: Shared<'_>) -> Result<af_api::contract::EditResult, String> {
    Ok(lock(&state)?.core.redo())
}

#[tauri::command]
fn history(state: Shared<'_>) -> Result<af_api::contract::History, String> {
    Ok(lock(&state)?.core.history())
}

#[tauri::command]
fn set_language(language: String, state: Shared<'_>) -> Result<(), String> {
    lock(&state)?.core.set_language(&language);
    Ok(())
}

/// The text sizes in CSS px, from DATUM's type tokens as the page resolved them.
#[derive(Clone, Copy, Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
struct TextStyle {
    text_size: f32,
    text_line_height: f32,
    identifier_size: f32,
    identifier_line_height: f32,
}

#[tauri::command]
fn set_style(style: TextStyle, state: Shared<'_>) -> Result<(), String> {
    lock(&state)?.core.set_style(Style {
        text_size: style.text_size,
        text_line_height: style.text_line_height,
        identifier_size: style.identifier_size,
        identifier_line_height: style.identifier_line_height,
    });
    Ok(())
}

/// Starts the application with `args`. Returns the process's exit code.
pub fn run(args: Args) -> i32 {
    let (desktop, line) = Desktop::start(&args);
    if let Some(line) = &line {
        eprintln!("{line}");
    }
    // A smoke run fails when the file it names did not open.
    let smoke_code = if args.file.is_none() || desktop.initial_opened() {
        0
    } else {
        2
    };
    let smoke = args.smoke;
    let builder = tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .manage(Mutex::new(desktop))
        .manage(CloseHold::default())
        .invoke_handler(tauri::generate_handler![
            start,
            initial_file,
            open,
            open_dialog,
            recent_files,
            open_recent,
            forget_recent,
            outline,
            packages,
            close,
            diagram,
            element,
            relationship,
            add_choices,
            add,
            removal,
            remove,
            set_text,
            undo,
            redo,
            history,
            save,
            save_as,
            quit,
            close_heard,
            set_language,
            set_style
        ])
        .on_window_event(|window, event| {
            // Changes that are not saved: the page asks first (S9).
            if let WindowEvent::CloseRequested { api, .. } = event {
                let state = window.state::<Mutex<Desktop>>();
                let modified = state.lock().is_ok_and(|desktop| desktop.is_modified());
                if window.state::<CloseHold>().hold(modified) {
                    api.prevent_close();
                    let _ = window.emit(CLOSE_REQUESTED, ());
                }
            }
        })
        .setup(move |app| {
            // A smoke run leaves the user's recent files alone.
            if !smoke {
                if let Ok(directory) = app.path().app_config_dir() {
                    let state = app.state::<Mutex<Desktop>>();
                    if let Ok(mut desktop) = state.lock() {
                        desktop.load_recent(directory.join("recent.json"));
                    }
                }
            }
            if smoke {
                // The window exists by now: the shell has started.
                let window = app.get_webview_window("main").is_some();
                eprintln!("af-desktop smoke window={window}");
                app.handle().exit(if window { smoke_code } else { 3 });
            }
            Ok(())
        });
    #[cfg(target_os = "macos")]
    let builder = builder
        .menu(|app| macos_menu(app))
        .on_menu_event(|app, event| on_menu(app, &event));
    let built = builder.build(tauri::generate_context!());
    let app = match built {
        Ok(app) => app,
        Err(error) => {
            eprintln!("af-desktop failed error={error}");
            return 1;
        }
    };
    app.run_return(|_, _| {})
}

#[cfg(test)]
mod tests {
    use super::*;

    fn testdata(path: &str) -> PathBuf {
        PathBuf::from(format!(
            "{}/../../testdata/{path}",
            env!("CARGO_MANIFEST_DIR")
        ))
    }

    #[test]
    fn the_first_argument_that_is_not_a_flag_is_the_file() {
        let args = |list: &[&str]| Args::parse(list.iter().map(|arg| (*arg).to_owned()));
        assert_eq!(args(&[]), Args::default());
        assert_eq!(
            args(&["--smoke", "a.xmi", "b.xmi"]),
            Args {
                file: Some(PathBuf::from("a.xmi")),
                smoke: true
            }
        );
        assert_eq!(
            args(&["--unknown", "a.xmi"]),
            Args {
                file: Some(PathBuf::from("a.xmi")),
                smoke: false
            }
        );
    }

    #[test]
    fn a_file_named_at_start_opens_before_the_fonts_and_is_logged() {
        let path = testdata("sacm/sacm_example_core_argument.sacm.xmi");
        let (mut desktop, line) = Desktop::start(&Args {
            file: Some(path.clone()),
            smoke: false,
        });
        assert!(desktop.initial_opened());
        let line = line.unwrap_or_default();
        assert!(line.starts_with("af-desktop opened file="), "{line}");
        assert!(
            line.ends_with("revision=1 nodes=8 edges=7 errors=0 warnings=0"),
            "{line}"
        );
        let initial = desktop.initial.take();
        assert_eq!(
            initial.map(|opened| opened.name),
            Some("sacm_example_core_argument.sacm.xmi".to_owned())
        );
        // Once the list is loaded, the file is its most recent one.
        assert!(desktop.recent_files().is_empty());
        let store = std::env::temp_dir().join(format!(
            "af-desktop-initial-{}/recent.json",
            std::process::id()
        ));
        desktop.load_recent(store.clone());
        let names: Vec<&str> = desktop
            .recent_files()
            .iter()
            .map(|file| file.name.as_str())
            .collect();
        assert_eq!(names, ["sacm_example_core_argument.sacm.xmi"]);
        assert_eq!(desktop.recent_files()[0].nodes, 8);
        if let Some(directory) = store.parent() {
            let _ = std::fs::remove_dir_all(directory);
        }
    }

    #[test]
    fn a_recent_file_opens_again_and_one_that_is_gone_leaves_the_list() {
        let directory =
            std::env::temp_dir().join(format!("af-desktop-reopen-{}", std::process::id()));
        let _ = std::fs::create_dir_all(&directory);
        let copy = directory.join("copy.sacm.xmi");
        let sample = testdata("sacm/sacm_example_core_argument.sacm.xmi");
        assert!(std::fs::copy(&sample, &copy).is_ok());

        let (mut desktop, _) = Desktop::start(&Args::default());
        assert_eq!(
            desktop
                .open_path(&directory.join("missing.xmi"))
                .err()
                .as_deref(),
            Some("file-unreadable")
        );
        assert!(desktop.recent_files().is_empty());
        assert!(desktop.open_path(&copy).is_ok());
        let id = desktop.recent_files()[0].id.clone();

        desktop.core.close();
        let again = desktop.open_recent(&id).ok().flatten();
        assert!(
            matches!(
                again,
                Some(Opened {
                    result: OpenResult::Opened { nodes: 8, .. },
                    ..
                })
            ),
            "{again:?}"
        );
        // An id that is not on the list opens nothing, whatever file it names.
        assert_eq!(desktop.open_recent(&sample.display().to_string()), Ok(None));

        assert!(std::fs::remove_file(&copy).is_ok());
        assert_eq!(desktop.open_recent(&id), Ok(None));
        assert!(desktop.recent_files().is_empty());
        let _ = std::fs::remove_dir_all(&directory);
    }

    #[test]
    fn a_recent_file_that_is_there_but_cannot_be_read_stays_on_the_list() {
        // A directory is there, and reading it as a file fails, as a locked
        // file or one on a drive that is not connected does.
        let directory =
            std::env::temp_dir().join(format!("af-desktop-unreadable-{}", std::process::id()));
        assert!(std::fs::create_dir_all(&directory).is_ok());
        let (mut desktop, _) = Desktop::start(&Args::default());
        let opened = OpenResult::Opened {
            revision: 1,
            diagnostics: af_api::contract::DiagnosticCounts::default(),
            nodes: 1,
            edges: 0,
            view_codes: 0,
        };
        desktop.recent.record(&directory, &opened, 1);
        let id = desktop.recent_files()[0].id.clone();

        assert_eq!(desktop.open_recent(&id), Err("file-unreadable".to_owned()));
        assert_eq!(desktop.recent_files().len(), 1);
        let _ = std::fs::remove_dir_all(&directory);
    }

    #[test]
    fn a_refused_or_missing_file_is_logged_with_its_code() {
        let refused = OpenResult::Refused(af_api::contract::Refusal {
            code: "SACM-XMI-001".to_owned(),
            template: String::new(),
            arguments: Vec::new(),
            line: Some(3),
            column: None,
        });
        assert_eq!(
            log_line(Path::new("x.xmi"), &refused),
            "af-desktop refused file=x.xmi code=SACM-XMI-001 line=3 column=-"
        );
        let (desktop, line) = Desktop::start(&Args {
            file: Some(testdata("sacm/no-such-file.xmi")),
            smoke: true,
        });
        assert!(!desktop.initial_opened());
        assert!(
            line.unwrap_or_default()
                .starts_with("af-desktop unreadable file=")
        );
    }

    /// A folder of its own for one test, with a copy of the core sample.
    fn scratch(test: &str) -> (PathBuf, PathBuf) {
        let directory =
            std::env::temp_dir().join(format!("af-desktop-{test}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&directory);
        assert!(std::fs::create_dir_all(&directory).is_ok());
        let copy = directory.join("case.sacm.xmi");
        assert!(std::fs::copy(testdata("sacm/sacm_example_core_argument.sacm.xmi"), &copy).is_ok());
        (directory, copy)
    }

    /// Adds a top-level Goal: a change to save.
    fn change(desktop: &mut Desktop) {
        let revision = desktop.core.revision();
        assert!(matches!(
            desktop
                .core
                .add(None, af_api::contract::ElementKind::Goal, revision),
            af_api::contract::EditResult::Applied { .. }
        ));
    }

    fn strict(bytes: &[u8]) -> Option<sacm::model::Document> {
        let options = sacm::xmi::ReadOptions::default().with_mode(sacm::xmi::Mode::Strict);
        sacm::xmi::read(bytes, &options)
            .ok()
            .map(|output| output.into_document())
    }

    /// File ▸ Save of the document as it is now, to `path`.
    fn save(desktop: &mut Desktop, path: &Path) -> SaveResult {
        let revision = desktop.core.revision();
        desktop.save_to(path, revision, false)
    }

    #[test]
    fn save_writes_the_file_it_was_opened_from_and_it_reads_back() {
        let (directory, copy) = scratch("save");
        let (mut desktop, _) = Desktop::start(&Args::default());
        assert!(desktop.open_path(&copy).is_ok());
        assert_eq!(desktop.save_path(), Some(copy.as_path()));
        change(&mut desktop);
        assert!(desktop.is_modified());

        let saved = save(&mut desktop, &copy);
        assert!(
            matches!(saved, SaveResult::Saved { ref name, .. } if name == "case.sacm.xmi"),
            "{saved:?}"
        );
        assert!(!desktop.is_modified());
        let on_disk = std::fs::read(&copy).unwrap_or_default();
        let WriteResult::Written { xmi, .. } = desktop.core.write() else {
            panic!("not written");
        };
        assert_eq!(on_disk, xmi.as_bytes(), "the file is what the core wrote");
        assert!(strict(&on_disk).is_some());
        // No temporary file is left beside it.
        let left: Vec<_> = std::fs::read_dir(&directory)
            .map(|entries| entries.flatten().map(|entry| entry.file_name()).collect())
            .unwrap_or_default();
        assert_eq!(left, ["case.sacm.xmi"]);

        // Saved again with no change between: the file Save writes is still
        // the one it last wrote, with what it wrote.
        change(&mut desktop);
        assert!(matches!(
            save(&mut desktop, &copy),
            SaveResult::Saved { .. }
        ));

        // A file gone from disk since has nothing to lose: Save writes it again.
        change(&mut desktop);
        assert!(std::fs::remove_file(&copy).is_ok());
        assert!(matches!(
            save(&mut desktop, &copy),
            SaveResult::Saved { .. }
        ));
        assert!(strict(&std::fs::read(&copy).unwrap_or_default()).is_some());
        let _ = std::fs::remove_dir_all(&directory);
    }

    #[test]
    fn a_save_asked_for_before_the_document_changed_is_not_made() {
        let (directory, copy) = scratch("stale");
        let (mut desktop, _) = Desktop::start(&Args::default());
        assert!(desktop.open_path(&copy).is_ok());
        let before = std::fs::read(&copy).unwrap_or_default();
        change(&mut desktop);
        let asked = desktop.core.revision();
        // While the save dialog was open, the document changed, or another
        // file was opened: what would be written is not what was asked for.
        change(&mut desktop);
        assert_eq!(
            desktop.save_to(&copy, asked, true),
            SaveResult::Refused {
                code: "AF-SAVE-006".to_owned()
            }
        );
        assert_eq!(std::fs::read(&copy).unwrap_or_default(), before);
        assert!(desktop.is_modified());
        let _ = std::fs::remove_dir_all(&directory);
    }

    #[test]
    fn a_file_changed_on_disk_since_it_was_opened_is_not_overwritten() {
        let (directory, copy) = scratch("changed");
        let (mut desktop, _) = Desktop::start(&Args::default());
        assert!(desktop.open_path(&copy).is_ok());
        change(&mut desktop);
        // Another program writes the file meanwhile.
        let theirs = b"<changed by another program/>";
        assert!(std::fs::write(&copy, theirs).is_ok());

        assert_eq!(
            save(&mut desktop, &copy),
            SaveResult::Refused {
                code: "AF-SAVE-004".to_owned()
            }
        );
        assert_eq!(std::fs::read(&copy).unwrap_or_default(), theirs);
        assert!(desktop.is_modified(), "nothing was saved");

        // Save As… to another file still saves, and Save then writes there.
        let other = directory.join("other.sacm.xmi");
        let revision = desktop.core.revision();
        assert!(matches!(
            desktop.save_to(&other, revision, true),
            SaveResult::Saved { ref name, .. } if name == "other.sacm.xmi"
        ));
        assert_eq!(desktop.save_path(), Some(other.as_path()));
        assert_eq!(desktop.recent_files()[0].name, "other.sacm.xmi");
        assert_eq!(std::fs::read(&copy).unwrap_or_default(), theirs);

        // Save As… to the changed file itself: the save dialog asked before
        // replacing it, so it is replaced.
        change(&mut desktop);
        let revision = desktop.core.revision();
        assert!(matches!(
            desktop.save_to(&copy, revision, true),
            SaveResult::Saved { .. }
        ));
        assert!(strict(&std::fs::read(&copy).unwrap_or_default()).is_some());
        let _ = std::fs::remove_dir_all(&directory);
    }

    #[test]
    fn a_change_on_disk_of_the_same_length_is_seen_and_a_file_touched_is_not_changed() {
        let (directory, copy) = scratch("same-length");
        let (mut desktop, _) = Desktop::start(&Args::default());
        assert!(desktop.open_path(&copy).is_ok());
        let ours = std::fs::read(&copy).unwrap_or_default();

        // Rewritten with what it held: not a change, and Save writes.
        assert!(std::fs::write(&copy, &ours).is_ok());
        change(&mut desktop);
        assert!(matches!(
            save(&mut desktop, &copy),
            SaveResult::Saved { .. }
        ));

        // One byte changed, the length the same, straight after the save:
        // the time on disk may not tell it, what it holds does.
        let mut theirs = std::fs::read(&copy).unwrap_or_default();
        let last = theirs.len() - 1;
        theirs[last] = if theirs[last] == b' ' { b'\n' } else { b' ' };
        assert!(std::fs::write(&copy, &theirs).is_ok());
        change(&mut desktop);
        assert_eq!(
            save(&mut desktop, &copy),
            SaveResult::Refused {
                code: "AF-SAVE-004".to_owned()
            }
        );
        assert_eq!(std::fs::read(&copy).unwrap_or_default(), theirs);
        let _ = std::fs::remove_dir_all(&directory);
    }

    #[test]
    fn a_read_only_file_is_not_written() {
        let (directory, copy) = scratch("read-only");
        let (mut desktop, _) = Desktop::start(&Args::default());
        assert!(desktop.open_path(&copy).is_ok());
        let before = std::fs::read(&copy).unwrap_or_default();
        let set_read_only = |read_only: bool| {
            let mut permissions = std::fs::metadata(&copy).map(|m| m.permissions());
            if let Ok(permissions) = &mut permissions {
                permissions.set_readonly(read_only);
                assert!(std::fs::set_permissions(&copy, permissions.clone()).is_ok());
            }
        };
        set_read_only(true);
        change(&mut desktop);
        let revision = desktop.core.revision();
        // Not by Save, and not by Save As… onto it either: the folder may be
        // writable, but the file is not.
        for chosen in [false, true] {
            assert_eq!(
                desktop.save_to(&copy, revision, chosen),
                SaveResult::Refused {
                    code: "AF-SAVE-005".to_owned()
                }
            );
        }
        assert_eq!(std::fs::read(&copy).unwrap_or_default(), before);
        assert!(desktop.is_modified());
        set_read_only(false);
        let _ = std::fs::remove_dir_all(&directory);
    }

    #[cfg(unix)]
    #[test]
    fn a_save_through_a_link_writes_the_file_it_names_and_keeps_its_permissions() {
        use std::os::unix::fs::PermissionsExt as _;
        let (directory, copy) = scratch("link");
        assert!(std::fs::set_permissions(&copy, std::fs::Permissions::from_mode(0o640)).is_ok());
        let link = directory.join("link.sacm.xmi");
        assert!(std::os::unix::fs::symlink(&copy, &link).is_ok());
        let (mut desktop, _) = Desktop::start(&Args::default());
        assert!(desktop.open_path(&link).is_ok());
        change(&mut desktop);
        assert!(matches!(
            save(&mut desktop, &link),
            SaveResult::Saved { .. }
        ));
        let linked = std::fs::symlink_metadata(&link).map(|m| m.file_type().is_symlink());
        assert!(matches!(linked, Ok(true)), "the link is still a link");
        let WriteResult::Written { xmi, .. } = desktop.core.write() else {
            panic!("not written");
        };
        assert_eq!(std::fs::read(&copy).unwrap_or_default(), xmi.as_bytes());
        let mode = std::fs::metadata(&copy).map(|m| m.permissions().mode() & 0o777);
        assert!(matches!(mode, Ok(0o640)), "{mode:?}");
        let _ = std::fs::remove_dir_all(&directory);
    }

    #[test]
    fn a_close_is_held_for_the_page_until_the_page_stops_hearing_it() {
        let hold = CloseHold::default();
        assert!(!hold.hold(false), "nothing to lose: the window closes");
        // The page hears each close held, asks, and the user cancels.
        assert!(hold.hold(true));
        hold.heard();
        assert!(hold.hold(true));
        hold.heard();
        // The page has stopped: the close held last was not heard, so the
        // next one closes the window rather than leave it stuck open.
        assert!(hold.hold(true));
        assert!(!hold.hold(true));
        // A page that hears again holds again.
        hold.heard();
        assert!(hold.hold(true));
    }

    #[test]
    fn a_save_the_system_refuses_leaves_the_file_as_it_was() {
        let (directory, copy) = scratch("refused");
        let (mut desktop, _) = Desktop::start(&Args::default());
        assert!(desktop.open_path(&copy).is_ok());
        change(&mut desktop);
        // A folder where the file would go: the rename over it fails.
        let blocked = directory.join("blocked.sacm.xmi");
        assert!(std::fs::create_dir_all(blocked.join("inside")).is_ok());
        let revision = desktop.core.revision();
        assert_eq!(
            desktop.save_to(&blocked, revision, true),
            SaveResult::Refused {
                code: "AF-SAVE-005".to_owned()
            }
        );
        assert!(blocked.join("inside").is_dir(), "what was there is there");
        assert!(desktop.is_modified());
        assert_eq!(desktop.save_path(), Some(copy.as_path()));
        let mut left: Vec<_> = std::fs::read_dir(&directory)
            .map(|entries| entries.flatten().map(|entry| entry.file_name()).collect())
            .unwrap_or_default();
        left.sort();
        assert_eq!(
            left,
            ["blocked.sacm.xmi", "case.sacm.xmi"],
            "no temporary file"
        );
        let _ = std::fs::remove_dir_all(&directory);
    }

    #[test]
    fn a_dropped_file_has_no_path_to_save_to_and_close_forgets_the_path() {
        let (directory, copy) = scratch("unplaced");
        let (mut desktop, _) = Desktop::start(&Args::default());
        let bytes = std::fs::read(&copy).unwrap_or_default();
        assert!(matches!(
            desktop.open_unplaced(&bytes),
            OpenResult::Opened { .. }
        ));
        assert_eq!(desktop.save_path(), None, "Save is Save As…");
        assert!(desktop.open_path(&copy).is_ok());
        assert_eq!(desktop.save_path(), Some(copy.as_path()));
        desktop.close();
        assert_eq!(desktop.save_path(), None);
        assert_eq!(
            save(&mut desktop, &copy),
            SaveResult::Refused {
                code: "AF-SAVE-001".to_owned()
            }
        );
        assert_eq!(std::fs::read(&copy).unwrap_or_default(), bytes);
        let _ = std::fs::remove_dir_all(&directory);
    }

    #[test]
    fn the_fonts_arrive_as_one_body_with_their_lengths() {
        let read =
            |name: &str| std::fs::read(testdata(&format!("fonts/{name}"))).unwrap_or_default();
        let faces = [
            read("IBMPlexSans-Regular.ttf"),
            read("IBMPlexSans-SemiBold.ttf"),
            read("AFTestSansJP-Regular.ttf"),
            read("AFTestSansJP-SemiBold.ttf"),
            read("IBMPlexMono-Regular.ttf"),
        ];
        let body = faces.concat();
        let lengths: Vec<String> = faces.iter().map(|face| face.len().to_string()).collect();
        assert!(unpack_fonts(&body, &lengths.join(",")).is_ok());
        assert_eq!(
            unpack_fonts(&body, "1,2,3").err().as_deref(),
            Some("font-lengths")
        );
        // The Mono face replaced by bytes that are not a font: named.
        let mut broken = faces[..4].concat();
        broken.extend_from_slice(b"not a font");
        let mut lengths = lengths[..4].to_vec();
        lengths.push("10".to_owned());
        assert_eq!(
            unpack_fonts(&broken, &lengths.join(",")).err().as_deref(),
            Some("font-unreadable:MonoRegular")
        );
    }
}

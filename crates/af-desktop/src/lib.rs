//! The desktop application: the interface (`ui/`) in a Tauri window, on the
//! core (`af-api`).
//!
//! This shell, `af-mcp` and `af-wasm` are the only crates that touch the
//! outside world. Here that is the file system (a file named on the command
//! line, the open dialog, the list of recent files) and the window. The page loads DATUM's measured
//! faces once and hands the same bytes to the core, as it does in the browser.
//!
//! The commands are the browser core's (`ui/src/core/Core.ts`), so the
//! interface is the same code on both (plan decision D1).
//!
//! `af-desktop [--smoke] [FILE]` opens `FILE` at start and writes one line
//! saying what was read to standard error. `--smoke` exits after that line:
//! it is how CI checks that the application starts.

pub mod recent;

use std::path::{Path, PathBuf};
use std::sync::Mutex;

use af_api::Core;
use af_api::contract::{OpenResult, OpenedFile as Opened};
use af_layout::{FaceName, Fonts, Style};
use serde::Deserialize;
use tauri::ipc::{InvokeBody, Request, Response};
use tauri::{Manager, State};
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

/// What the window's commands share: the core, the file named on the
/// command line until the page has taken it, and the recent files.
pub struct Desktop {
    core: Core,
    initial: Option<Opened>,
    /// The file named on the command line, when it opened: recorded as a
    /// recent file once the list is loaded.
    initial_path: Option<(PathBuf, OpenResult)>,
    recent: RecentFiles,
}

impl Desktop {
    /// A core with no fonts yet, with `args.file` opened if there is one.
    /// Returns the log line for that file.
    pub fn start(args: &Args) -> (Self, Option<String>) {
        let mut core = Core::without_fonts(Style::default(), "en");
        let mut initial = None;
        let mut initial_path = None;
        let mut line = None;
        if let Some(path) = &args.file {
            match std::fs::read(path) {
                Ok(bytes) => {
                    let result = core.open(&bytes);
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
    /// recent one.
    fn open_bytes(&mut self, path: &Path, bytes: &[u8]) -> Opened {
        let result = self.core.open(bytes);
        self.recent.record(path, &result, recent::now());
        Opened {
            name: file_name(path),
            result,
        }
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
    Ok(lock(&state)?.core.open(&bytes))
}

/// Shows the system's open dialog and opens the file chosen, or answers
/// `None` when the dialog is cancelled. Not on the main thread: the dialog
/// blocks.
#[tauri::command]
async fn open_dialog(app: tauri::AppHandle, state: Shared<'_>) -> Result<Option<Opened>, String> {
    let Some(chosen) = app
        .dialog()
        .file()
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
    lock(&state)?.core.close();
    Ok(())
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
    state: Shared<'_>,
) -> Result<af_api::contract::EditResult, String> {
    Ok(lock(&state)?.core.add(parent.as_deref(), kind))
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
    let built = tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .manage(Mutex::new(desktop))
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
            undo,
            redo,
            history,
            set_language,
            set_style
        ])
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
        })
        .build(tauri::generate_context!());
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

//! The web application's core: [`af_api::Core`] for the browser.
//!
//! Each method answers with the contract's JSON (`crates/af-api/bindings/api.ts`
//! declares its shapes), which the interface parses. The page reads files and
//! fonts and hands in bytes; nothing here touches the network or storage.
//! The page keeps the recovery copies the core makes, in IndexedDB.

use af_api::Core;
use af_api::contract::ElementKind;
use af_layout::{Fonts, Style};
use serde::Serialize;
use wasm_bindgen::prelude::*;

/// The core, as the page holds it.
#[wasm_bindgen]
pub struct WasmCore {
    core: Core,
}

/// The contract's JSON. A failure is the core's, not the file's: the page
/// hears it as an error named `core-serialize`.
fn json<T: Serialize>(value: &T) -> Result<String, JsError> {
    serde_json::to_string(value).map_err(|_| JsError::new("core-serialize"))
}

#[wasm_bindgen]
impl WasmCore {
    /// A core with nothing open, laying text out with the five measured faces
    /// (whole TrueType files) and naming the interface language.
    ///
    /// # Errors
    ///
    /// When a font is not a face the shaper reads: the error names the face.
    #[wasm_bindgen(constructor)]
    pub fn new(
        sans_regular: Vec<u8>,
        sans_semibold: Vec<u8>,
        japanese_regular: Vec<u8>,
        japanese_semibold: Vec<u8>,
        mono_regular: Vec<u8>,
        language: &str,
    ) -> Result<WasmCore, JsError> {
        let fonts = Fonts::new(
            sans_regular,
            sans_semibold,
            japanese_regular,
            japanese_semibold,
            mono_regular,
        )
        .map_err(|error| JsError::new(&format!("font-unreadable:{:?}", error.face)))?;
        Ok(Self {
            core: Core::new(fonts, Style::default(), language),
        })
    }

    /// `OpenResult` as JSON.
    pub fn open(&mut self, bytes: &[u8]) -> Result<String, JsError> {
        json(&self.core.open(bytes))
    }

    /// `DiagramAt` as JSON, or `None` when nothing is open.
    pub fn diagram(&mut self) -> Result<Option<String>, JsError> {
        self.core.diagram_ref().map(|at| json(&at)).transpose()
    }

    /// `ElementDetails` as JSON, or `None` when no node has the id.
    pub fn element(&self, id: &str) -> Result<Option<String>, JsError> {
        self.core
            .element(id)
            .map(|details| json(&details))
            .transpose()
    }

    /// `RelationshipDetails` as JSON, or `None` when no edge is drawn for the id.
    pub fn relationship(&self, id: &str) -> Result<Option<String>, JsError> {
        self.core
            .relationship(id)
            .map(|details| json(&details))
            .transpose()
    }

    /// `AddChoices` as JSON for the node `parent`, or for the empty canvas
    /// when `None`; `None` when nothing is open.
    #[wasm_bindgen(js_name = addChoices)]
    pub fn add_choices(&self, parent: Option<String>) -> Result<Option<String>, JsError> {
        self.core
            .add_choices(parent.as_deref())
            .map(|choices| json(&choices))
            .transpose()
    }

    /// Adds an element of `kind` (an `ElementKind`, such as `goal`) under the
    /// node `parent`, or at the top level when `None`, from the add menu of
    /// `revision`. `EditResult` as JSON.
    pub fn add(
        &mut self,
        parent: Option<String>,
        kind: &str,
        revision: u32,
    ) -> Result<String, JsError> {
        let kind: ElementKind = serde_json::from_value(serde_json::Value::String(kind.to_owned()))
            .map_err(|_| JsError::new("core-kind"))?;
        json(&self.core.add(parent.as_deref(), kind, revision))
    }

    /// `Removal` as JSON for the node `id`: what removing it would delete;
    /// `None` when nothing is open.
    pub fn removal(&self, id: &str) -> Result<Option<String>, JsError> {
        self.core
            .removal(id)
            .map(|removal| json(&removal))
            .transpose()
    }

    /// Removes the node `id` and what goes with it, as the removal of
    /// `revision` said. `EditResult` as JSON.
    pub fn remove(&mut self, id: &str, revision: u32) -> Result<String, JsError> {
        json(&self.core.remove(id, revision))
    }

    /// `MoveTargets` as JSON for the node `id`: where it may be moved;
    /// `None` when nothing is open.
    #[wasm_bindgen(js_name = moveTargets)]
    pub fn move_targets(&self, id: &str) -> Result<Option<String>, JsError> {
        self.core
            .move_targets(id)
            .map(|targets| json(&targets))
            .transpose()
    }

    /// Moves the node `id`, with what is under it, under the node `to`, or
    /// to the top level when `to` is `None`, as the targets of `revision`
    /// said. `EditResult` as JSON.
    #[wasm_bindgen(js_name = moveNode)]
    pub fn move_node(
        &mut self,
        id: &str,
        to: Option<String>,
        revision: u32,
    ) -> Result<String, JsError> {
        json(&self.core.move_node(id, to.as_deref(), revision))
    }

    /// Sets the text `field` (a `TextField`, such as `name`) of the node
    /// `id` to `text`. `EditResult` as JSON.
    #[wasm_bindgen(js_name = setText)]
    pub fn set_text(&mut self, id: &str, field: &str, text: &str) -> Result<String, JsError> {
        let field: af_api::contract::TextField =
            serde_json::from_value(serde_json::Value::String(field.to_owned()))
                .map_err(|_| JsError::new("core-field"))?;
        json(&self.core.set_text(id, field, text))
    }

    /// Takes back the last command. `EditResult` as JSON.
    pub fn undo(&mut self) -> Result<String, JsError> {
        json(&self.core.undo())
    }

    /// Makes again the last command undo took back. `EditResult` as JSON.
    pub fn redo(&mut self) -> Result<String, JsError> {
        json(&self.core.redo())
    }

    /// `History` as JSON: whether Undo and Redo can be chosen.
    pub fn history(&self) -> Result<String, JsError> {
        json(&self.core.history())
    }

    /// `WriteResult` as JSON: the open document as SACM 2.3 XMI, checked to
    /// read back as the document. Nothing is marked saved.
    pub fn write(&mut self) -> Result<String, JsError> {
        json(&self.core.write())
    }

    /// `RecoveryCopy` as JSON: the bytes to keep for the file `name` while
    /// the document has changes that are not saved, written at `written_at`
    /// (milliseconds since 1970), or that none is needed.
    #[wasm_bindgen(js_name = recoveryCopy)]
    pub fn recovery_copy(&self, name: &str, written_at: f64) -> Result<String, JsError> {
        json(&self.core.recovery_copy(name, written_at))
    }

    /// `FoundRecovery` as JSON: what the recovery copy `copy`, found for the
    /// file just opened, is.
    #[wasm_bindgen(js_name = readRecovery)]
    pub fn read_recovery(&self, copy: &[u8]) -> Result<String, JsError> {
        json(&self.core.read_recovery(copy))
    }

    /// Replaces the document by the one the recovery copy `copy` kept, as
    /// one command undo takes back. `EditResult` as JSON.
    pub fn restore(&mut self, copy: &[u8]) -> Result<String, JsError> {
        json(&self.core.restore(copy))
    }

    /// The page has saved (downloaded) what `write` gave at `revision`.
    /// `false` when the document has changed since.
    #[wasm_bindgen(js_name = markSaved)]
    pub fn mark_saved(&mut self, revision: u32) -> bool {
        self.core.mark_saved(revision)
    }

    /// `Problems` as JSON: the problem list at the revision, or `None` when
    /// nothing is open.
    pub fn problems(&mut self) -> Result<Option<String>, JsError> {
        self.core
            .problems()
            .map(|problems| json(&problems))
            .transpose()
    }

    /// `Outline` as JSON, or `None` when nothing is open.
    pub fn outline(&self) -> Result<Option<String>, JsError> {
        self.core
            .outline()
            .map(|outline| json(&outline))
            .transpose()
    }

    /// The open file's packages, `Package[]` as JSON.
    pub fn packages(&self) -> Result<String, JsError> {
        json(&self.core.packages())
    }

    /// Closes what is open.
    pub fn close(&mut self) {
        self.core.close();
    }

    #[wasm_bindgen(js_name = setLanguage)]
    pub fn set_language(&mut self, language: &str) {
        self.core.set_language(language);
    }

    /// The text sizes in CSS pixels, from DATUM's type tokens as the page
    /// resolved them.
    #[wasm_bindgen(js_name = setStyle)]
    pub fn set_style(
        &mut self,
        text_size: f32,
        text_line_height: f32,
        identifier_size: f32,
        identifier_line_height: f32,
    ) {
        self.core.set_style(Style {
            text_size,
            text_line_height,
            identifier_size,
            identifier_line_height,
        });
    }
}

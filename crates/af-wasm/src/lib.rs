//! The web application's core: [`af_api::Core`] for the browser.
//!
//! Each method answers with the contract's JSON (`crates/af-api/bindings/api.ts`
//! declares its shapes), which the interface parses. The page reads files and
//! fonts and hands in bytes; nothing here touches the network or storage.

use af_api::Core;
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

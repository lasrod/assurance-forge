//! Text measurement, diagram layout and the render model.
//!
//! [`Engine::layout`] turns the GSN view of a document ([`af_gsn::view::View`])
//! into a [`Diagram`]: every node's shape and wrapped text, and every edge's
//! line and arrowhead, positioned. The canvas, hit testing and the SVG export
//! all draw this one geometry.
//!
//! It does no I/O and builds for `wasm32-unknown-unknown`. The shell hands in
//! the fonts as bytes ([`Fonts`]).
//!
//! The layout is a layered tree (plan decision D7): SupportedBy children in
//! the row below their parent, InContextOf attachments beside it, siblings in
//! document order, straight edges.

mod layout;
mod model;
mod text;

use std::collections::HashMap;

pub use layout::{OutlineRow, outline};
pub use model::{
    Arrowhead, Decorator, Diagram, DiagramEdge, DiagramNode, LineStyle, Placement, Point, Rect,
    Shape, ShapeKind, TextLine, TextRole,
};
pub use text::{FaceName, Font, FontError, Fonts, Line, Weight};

/// Text sizes, in CSS pixels. They come from DATUM's type tokens and follow
/// the user's text size, so the shell passes them in.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Style {
    /// `type.control`: a node's name and statement.
    pub text_size: f32,
    pub text_line_height: f32,
    /// `type.metadata`: the identifier, and the letter of an Assumption or
    /// Justification.
    pub identifier_size: f32,
    pub identifier_line_height: f32,
}

impl Default for Style {
    /// DATUM's sizes at the browser's default text size: 14/20 and 12/16.
    fn default() -> Self {
        Self {
            text_size: 14.0,
            text_line_height: 20.0,
            identifier_size: 12.0,
            identifier_line_height: 16.0,
        }
    }
}

/// Lays out diagrams, keeping each text's wrapped lines between layouts.
pub struct Engine {
    fonts: Fonts,
    style: Style,
    cache: Cache,
}

/// Wrapped lines by text, role and wrap width.
pub(crate) type Cache = HashMap<(String, TextRole, u32), Vec<Line>>;

impl Engine {
    pub fn new(fonts: Fonts, style: Style) -> Self {
        Self {
            fonts,
            style,
            cache: HashMap::new(),
        }
    }

    pub fn style(&self) -> Style {
        self.style
    }

    /// Changes the text sizes, which drops every kept measurement.
    /// Says whether anything changed. A size that is not a finite number
    /// greater than 0, such as a token that did not resolve, changes nothing.
    pub fn set_style(&mut self, style: Style) -> bool {
        if style == self.style || !Self::style_is_usable(style) {
            return false;
        }
        self.style = style;
        self.cache.clear();
        true
    }

    /// Whether every size in `style` is a finite number greater than 0.
    pub fn style_is_usable(style: Style) -> bool {
        [
            style.text_size,
            style.text_line_height,
            style.identifier_size,
            style.identifier_line_height,
        ]
        .iter()
        .all(|size| size.is_finite() && *size > 0.0)
    }

    /// The diagram for `view`. The same view gives the same diagram.
    pub fn layout(&mut self, view: &af_gsn::view::View) -> Diagram {
        let shaper = text::Shaper::new(&self.fonts);
        layout::layout(view, &shaper, self.style, &mut self.cache)
    }

    /// Drops every kept measurement. The core calls it when a document is
    /// opened or the language changes, so the cache holds one document's text.
    pub fn clear_cache(&mut self) {
        self.cache.clear();
    }

    /// How many wrapped texts are kept.
    pub fn cached_texts(&self) -> usize {
        self.cache.len()
    }

    /// The width of one line of `text`, in CSS pixels, as the diagram measures
    /// it.
    pub fn measure(&self, text: &str, size: f32, font: Font) -> f32 {
        text::Shaper::new(&self.fonts).width(text, size, font)
    }

    /// `text` wrapped to `max_width`, as the diagram wraps it.
    pub fn wrap(&self, text: &str, size: f32, font: Font, max_width: f32) -> Vec<Line> {
        text::Shaper::new(&self.fonts).wrap(text, size, font, max_width)
    }
}

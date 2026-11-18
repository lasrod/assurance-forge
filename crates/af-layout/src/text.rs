//! Measuring and wrapping text with the fonts the canvas draws with.
//!
//! The diagram's text uses DATUM's measured faces in the interface's order:
//! Plex Sans, then Plex Sans JP (plan decision D6). Each character comes from
//! the first face that has it, as in the browser, and a line is shaped as runs
//! of one face each, so kerning applies within a run and never across a change
//! of face. Kana is shaped without kerning, which is what browsers drew in the
//! canvas spike; the browser build's text-width check (goal G7) confirms it
//! against these faces.

use rustybuzz::ttf_parser::Tag;
use rustybuzz::{Face, Feature, UnicodeBuffer};
use unicode_linebreak::{BreakOpportunity, linebreaks};

/// A font weight the diagram uses.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Weight {
    Regular,
    SemiBold,
}

/// Which face could not be read, for [`FontError`].
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FaceName {
    SansRegular,
    SansSemiBold,
    JapaneseRegular,
    JapaneseSemiBold,
}

/// A font file that is not a face the shaper can read.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct FontError {
    pub face: FaceName,
}

/// The four faces, as bytes the shell hands in: whole TrueType or OpenType
/// files, not woff2.
#[derive(Clone, Debug)]
pub struct Fonts {
    sans: [Vec<u8>; 2],
    japanese: [Vec<u8>; 2],
}

impl Fonts {
    /// The faces in fallback order: the Latin face first, then the Japanese.
    ///
    /// # Errors
    ///
    /// A [`FontError`] naming the first file the shaper cannot read.
    pub fn new(
        sans_regular: Vec<u8>,
        sans_semibold: Vec<u8>,
        japanese_regular: Vec<u8>,
        japanese_semibold: Vec<u8>,
    ) -> Result<Self, FontError> {
        for (bytes, face) in [
            (&sans_regular, FaceName::SansRegular),
            (&sans_semibold, FaceName::SansSemiBold),
            (&japanese_regular, FaceName::JapaneseRegular),
            (&japanese_semibold, FaceName::JapaneseSemiBold),
        ] {
            if Face::from_slice(bytes, 0).is_none() {
                return Err(FontError { face });
            }
        }
        Ok(Self {
            sans: [sans_regular, sans_semibold],
            japanese: [japanese_regular, japanese_semibold],
        })
    }
}

/// One wrapped line and its width in CSS pixels.
#[derive(Clone, Debug, PartialEq)]
pub struct Line {
    pub text: String,
    pub width: f32,
}

/// The faces parsed for one layout.
pub(crate) struct Shaper<'a> {
    /// `[family][weight]`: family 0 is the Latin face, 1 the Japanese.
    faces: [[Face<'a>; 2]; 2],
}

const FAMILIES: usize = 2;

impl<'a> Shaper<'a> {
    pub(crate) fn new(fonts: &'a Fonts) -> Self {
        // `Fonts::new` checked that every face parses.
        let parse = |bytes: &'a [u8]| match Face::from_slice(bytes, 0) {
            Some(face) => face,
            None => unreachable!("Fonts::new checked that the face parses"),
        };
        Self {
            faces: [
                [parse(&fonts.sans[0]), parse(&fonts.sans[1])],
                [parse(&fonts.japanese[0]), parse(&fonts.japanese[1])],
            ],
        }
    }

    fn face(&self, family: usize, weight: Weight) -> &Face<'a> {
        &self.faces[family][weight as usize]
    }

    /// The Latin face's ascent and descent, in units of the size, which set
    /// a line's baseline.
    pub(crate) fn ascent_descent(&self) -> (f32, f32) {
        let face = self.face(0, Weight::Regular);
        let units = (face.units_per_em() as f32).max(1.0);
        (
            f32::from(face.ascender()) / units,
            f32::from(face.descender()).abs() / units,
        )
    }

    /// The first family that has a glyph for `c`, else the Latin face, whose
    /// missing glyph is then drawn.
    fn family(&self, c: char) -> usize {
        (0..FAMILIES)
            .find(|family| self.face(*family, Weight::Regular).glyph_index(c).is_some())
            .unwrap_or(0)
    }

    /// The width of `text` on one line, in CSS pixels at `size`.
    pub(crate) fn width(&self, text: &str, size: f32, weight: Weight) -> f32 {
        let mut total = 0.0;
        let mut start = 0;
        let mut current: Option<(usize, bool)> = None;
        for (index, c) in text.char_indices() {
            let key = (self.family(c), is_kana(c));
            match current {
                Some(run) if run == key => {}
                Some(run) => {
                    total += self.run_width(&text[start..index], run, size, weight);
                    start = index;
                    current = Some(key);
                }
                None => current = Some(key),
            }
        }
        if let Some(run) = current {
            total += self.run_width(&text[start..], run, size, weight);
        }
        total
    }

    fn run_width(
        &self,
        run: &str,
        (family, kana): (usize, bool),
        size: f32,
        weight: Weight,
    ) -> f32 {
        let face = self.face(family, weight);
        let mut buffer = UnicodeBuffer::new();
        buffer.push_str(run);
        buffer.guess_segment_properties();
        let no_kern = [Feature::new(Tag::from_bytes(b"kern"), 0, ..)];
        let features: &[Feature] = if kana { &no_kern } else { &[] };
        let shaped = rustybuzz::shape(face, features, buffer);
        let advance: i64 = shaped
            .glyph_positions()
            .iter()
            .map(|position| i64::from(position.x_advance))
            .sum();
        let units = (face.units_per_em() as f32).max(1.0);
        advance as f32 * size / units
    }

    /// `text` wrapped to `max_width`, by the Unicode line breaking algorithm.
    /// A word wider than the line is broken between characters. Spaces at the
    /// end of a line are not part of it; a mandatory break (a newline) ends a
    /// line.
    pub(crate) fn wrap(&self, text: &str, size: f32, weight: Weight, max_width: f32) -> Vec<Line> {
        let mut lines = Vec::new();
        let mut line = String::new();
        // The widths of the segments in `line`, to decide without shaping the
        // whole line at every break opportunity.
        let mut estimate = 0.0;
        let mut start = 0;
        for (end, opportunity) in linebreaks(text) {
            let segment = &text[start..end];
            start = end;
            let mandatory = opportunity == BreakOpportunity::Mandatory;
            let segment = segment.trim_end_matches(['\n', '\r', '\u{2028}', '\u{2029}', '\u{85}']);
            let trimmed = self.width(segment.trim_end(), size, weight);
            let full = self.width(segment, size, weight);

            if !line.is_empty() && estimate + trimmed > max_width {
                self.finish(&mut lines, &mut line, size, weight, max_width);
                estimate = 0.0;
            }
            if line.is_empty() && trimmed > max_width {
                // A word wider than the line.
                let (broken, rest) = self.break_word(segment, size, weight, max_width);
                lines.extend(broken);
                line.push_str(rest);
                estimate = self.width(rest, size, weight);
            } else {
                line.push_str(segment);
                estimate += full;
            }
            if mandatory {
                self.finish(&mut lines, &mut line, size, weight, max_width);
                estimate = 0.0;
            }
        }
        if !line.trim_end().is_empty() {
            self.finish(&mut lines, &mut line, size, weight, max_width);
        }
        lines
    }

    /// Ends `line`, measured whole. Kerning across a break opportunity can
    /// make it wider than the estimate; then its last segment goes to the next
    /// line, which is returned in `line`.
    fn finish(
        &self,
        lines: &mut Vec<Line>,
        line: &mut String,
        size: f32,
        weight: Weight,
        max_width: f32,
    ) {
        let text = line.trim_end().to_owned();
        let width = self.width(&text, size, weight);
        if width > max_width {
            if let Some(split) = last_break(&text) {
                let (head, tail) = text.split_at(split);
                let head = head.trim_end().to_owned();
                let head_width = self.width(&head, size, weight);
                lines.push(Line {
                    text: head,
                    width: head_width,
                });
                *line = tail.to_owned();
                let tail = line.trim_end().to_owned();
                let tail_width = self.width(&tail, size, weight);
                lines.push(Line {
                    text: tail,
                    width: tail_width,
                });
                line.clear();
                return;
            }
        }
        lines.push(Line { text, width });
        line.clear();
    }

    /// Breaks a word wider than the line between characters. Returns the full
    /// lines and what is left for the next.
    fn break_word<'s>(
        &self,
        word: &'s str,
        size: f32,
        weight: Weight,
        max_width: f32,
    ) -> (Vec<Line>, &'s str) {
        let mut lines = Vec::new();
        let mut rest = word;
        while self.width(rest.trim_end(), size, weight) > max_width {
            // The longest prefix that fits, at least one character.
            let mut fit = rest
                .char_indices()
                .nth(1)
                .map_or(rest.len(), |(index, _)| index);
            for (index, c) in rest.char_indices().skip(1) {
                let end = index + c.len_utf8();
                if self.width(&rest[..end], size, weight) > max_width {
                    break;
                }
                fit = end;
            }
            let (head, tail) = rest.split_at(fit);
            lines.push(Line {
                text: head.to_owned(),
                width: self.width(head, size, weight),
            });
            rest = tail;
            if rest.is_empty() {
                break;
            }
        }
        (lines, rest)
    }
}

/// The last allowed break in `text` before its end, if any.
fn last_break(text: &str) -> Option<usize> {
    linebreaks(text)
        .map(|(index, _)| index)
        .filter(|index| *index > 0 && *index < text.len())
        .last()
}

/// Hiragana, katakana and their extensions, which are shaped without kerning.
fn is_kana(c: char) -> bool {
    matches!(c, '\u{3040}'..='\u{309F}' | '\u{30A0}'..='\u{30FF}' | '\u{31F0}'..='\u{31FF}' | '\u{FF65}'..='\u{FF9F}')
}

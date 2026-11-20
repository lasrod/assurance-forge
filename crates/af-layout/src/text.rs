//! Measuring and wrapping text with the fonts the canvas draws with.
//!
//! The diagram's text uses DATUM's measured faces in the interface's order:
//! Plex Sans, then Plex Sans JP (plan decision D6). An identifier is set in
//! Plex Mono, with the two sans faces after it for what it lacks (DATUM
//! decision 0027). Each character comes from the first face of its stack that
//! has it, as in the browser, and a line is shaped as runs of one face each,
//! so kerning applies within a run and never across a change of face. Kana is shaped without kerning, which is what browsers drew in the
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

/// What a text is set in: a weight of the sans stack, or the monospace stack
/// an identifier uses. Monospace is only ever regular in DATUM.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Font {
    /// `'DATUM Measured Sans', 'DATUM Measured Sans JP'` at 400.
    Regular,
    /// The same stack at 600.
    SemiBold,
    /// `'DATUM Measured Mono', 'DATUM Measured Sans', 'DATUM Measured Sans JP'`
    /// at 400.
    Mono,
}

impl Font {
    /// The weight the text is drawn at.
    pub fn weight(self) -> Weight {
        match self {
            Font::Regular | Font::Mono => Weight::Regular,
            Font::SemiBold => Weight::SemiBold,
        }
    }

    /// The faces to take a character from, first to last, as indices into
    /// the shaper's faces.
    fn stack(self) -> &'static [usize] {
        match self {
            Font::Regular => &[SANS_REGULAR, JAPANESE_REGULAR],
            Font::SemiBold => &[SANS_SEMIBOLD, JAPANESE_SEMIBOLD],
            Font::Mono => &[MONO_REGULAR, SANS_REGULAR, JAPANESE_REGULAR],
        }
    }
}

const SANS_REGULAR: usize = 0;
const SANS_SEMIBOLD: usize = 1;
const JAPANESE_REGULAR: usize = 2;
const JAPANESE_SEMIBOLD: usize = 3;
const MONO_REGULAR: usize = 4;

/// Which face could not be read, for [`FontError`].
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FaceName {
    SansRegular,
    SansSemiBold,
    JapaneseRegular,
    JapaneseSemiBold,
    MonoRegular,
}

/// A font file that is not a face the shaper can read.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct FontError {
    pub face: FaceName,
}

/// The five faces, as bytes the shell hands in: whole TrueType or OpenType
/// files, not woff2.
#[derive(Clone, Debug)]
pub struct Fonts {
    /// In the order of the `*_REGULAR` and `*_SEMIBOLD` indices.
    faces: [Vec<u8>; 5],
}

impl Fonts {
    /// DATUM's measured faces: Plex Sans, Plex Sans JP and Plex Mono.
    ///
    /// # Errors
    ///
    /// A [`FontError`] naming the first file the shaper cannot read.
    pub fn new(
        sans_regular: Vec<u8>,
        sans_semibold: Vec<u8>,
        japanese_regular: Vec<u8>,
        japanese_semibold: Vec<u8>,
        mono_regular: Vec<u8>,
    ) -> Result<Self, FontError> {
        let faces = [
            sans_regular,
            sans_semibold,
            japanese_regular,
            japanese_semibold,
            mono_regular,
        ];
        let names = [
            FaceName::SansRegular,
            FaceName::SansSemiBold,
            FaceName::JapaneseRegular,
            FaceName::JapaneseSemiBold,
            FaceName::MonoRegular,
        ];
        for (bytes, face) in faces.iter().zip(names) {
            if Face::from_slice(bytes, 0).is_none() {
                return Err(FontError { face });
            }
        }
        Ok(Self { faces })
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
    /// In the order of the `*_REGULAR` and `*_SEMIBOLD` indices.
    faces: [Face<'a>; 5],
}

impl<'a> Shaper<'a> {
    pub(crate) fn new(fonts: &'a Fonts) -> Self {
        // `Fonts::new` checked that every face parses.
        let parse = |bytes: &'a Vec<u8>| match Face::from_slice(bytes, 0) {
            Some(face) => face,
            None => unreachable!("Fonts::new checked that the face parses"),
        };
        Self {
            faces: fonts.faces.each_ref().map(parse),
        }
    }

    /// The Latin face's ascent and descent, in units of the size, which set
    /// a line's baseline. Plex Mono's are the same (DATUM decision 0027), so
    /// an identifier and a name share a baseline.
    pub(crate) fn ascent_descent(&self) -> (f32, f32) {
        let face = &self.faces[SANS_REGULAR];
        let units = (face.units_per_em() as f32).max(1.0);
        (
            f32::from(face.ascender()) / units,
            f32::from(face.descender()).abs() / units,
        )
    }

    /// The first face of `font`'s stack that has a glyph for `c`, else the
    /// stack's first face, whose missing glyph is then drawn.
    fn face_for(&self, c: char, font: Font) -> usize {
        let stack = font.stack();
        stack
            .iter()
            .copied()
            .find(|&face| self.faces[face].glyph_index(c).is_some())
            .unwrap_or(stack[0])
    }

    /// The width of `text` on one line, in CSS pixels at `size`.
    pub(crate) fn width(&self, text: &str, size: f32, font: Font) -> f32 {
        let mut total = 0.0;
        let mut start = 0;
        let mut current: Option<(usize, bool)> = None;
        for (index, c) in text.char_indices() {
            let key = (self.face_for(c, font), is_kana(c));
            match current {
                Some(run) if run == key => {}
                Some(run) => {
                    total += self.run_width(&text[start..index], run, size);
                    start = index;
                    current = Some(key);
                }
                None => current = Some(key),
            }
        }
        if let Some(run) = current {
            total += self.run_width(&text[start..], run, size);
        }
        total
    }

    fn run_width(&self, run: &str, (face, kana): (usize, bool), size: f32) -> f32 {
        let face = &self.faces[face];
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
    /// end of a line are not part of it. A mandatory break (a newline) ends a
    /// line; an empty line between two is kept, and one at the start or end
    /// is not. Spaces at the start of a line are not part of it either.
    pub(crate) fn wrap(&self, text: &str, size: f32, font: Font, max_width: f32) -> Vec<Line> {
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
            let full = self.width(segment, size, font);
            let trimmed = if segment.ends_with(char::is_whitespace) {
                self.width(segment.trim_end(), size, font)
            } else {
                full
            };

            // Ends lines until the segment fits after what is left: an overflow
            // in `finish` carries a segment, which may leave no room either.
            while !line.trim_end().is_empty() && estimate + trimmed > max_width {
                self.finish(&mut lines, &mut line, size, font, max_width);
                estimate = self.width(&line, size, font);
            }
            // A line does not start with spaces: an indented statement is
            // laid out as its words.
            if line.trim_end().is_empty() {
                line.clear();
                estimate = 0.0;
            }
            let starts = line.is_empty() && segment.starts_with(char::is_whitespace);
            let segment = if starts {
                segment.trim_start()
            } else {
                segment
            };
            // Measured again only when the spaces at its start were removed.
            let (full, trimmed) = if starts {
                let full = self.width(segment, size, font);
                let trimmed = if segment.ends_with(char::is_whitespace) {
                    self.width(segment.trim_end(), size, font)
                } else {
                    full
                };
                (full, trimmed)
            } else {
                (full, trimmed)
            };
            if line.is_empty() && trimmed > max_width {
                // A word wider than the line.
                let (broken, rest) = self.break_word(segment, size, font, max_width);
                lines.extend(broken);
                line.push_str(rest);
                estimate = self.width(rest, size, font);
            } else {
                line.push_str(segment);
                estimate += full;
            }
            if mandatory {
                if line.trim_end().is_empty() {
                    // An empty line: kept between lines, not at the start or end.
                    if end < text.len() && !lines.is_empty() {
                        lines.push(Line {
                            text: String::new(),
                            width: 0.0,
                        });
                    }
                } else {
                    while !line.trim_end().is_empty() {
                        self.finish(&mut lines, &mut line, size, font, max_width);
                    }
                }
                line.clear();
                estimate = 0.0;
            }
        }
        // Blank lines before nothing but spaces or newlines are at the end.
        while lines.last().is_some_and(|line| line.text.is_empty()) {
            lines.pop();
        }
        lines
    }

    /// Ends `line`, measured whole. Kerning across a break opportunity can
    /// make it wider than the segments' sum; then the line ends at the last
    /// break whose head fits (at the first break if none does), and what
    /// follows, its spaces kept, is left in `line` to start the next line.
    fn finish(
        &self,
        lines: &mut Vec<Line>,
        line: &mut String,
        size: f32,
        font: Font,
        max_width: f32,
    ) {
        let text = line.trim_end().to_owned();
        let width = self.width(&text, size, font);
        if width > max_width {
            let breaks = break_points(&text);
            let chosen = breaks
                .iter()
                .rev()
                .map(|&split| (split, self.width(text[..split].trim_end(), size, font)))
                .find(|&(_, head)| head <= max_width)
                .or_else(|| {
                    breaks
                        .first()
                        .map(|&split| (split, self.width(text[..split].trim_end(), size, font)))
                });
            if let Some((split, head_width)) = chosen {
                lines.push(Line {
                    text: text[..split].trim_end().to_owned(),
                    width: head_width,
                });
                // `line` starts with `text`, so the split is a boundary in both.
                *line = line[split..].to_owned();
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
        font: Font,
        max_width: f32,
    ) -> (Vec<Line>, &'s str) {
        let mut lines = Vec::new();
        let mut rest = word;
        while self.width(rest.trim_end(), size, font) > max_width {
            // The longest prefix that fits, at least one character, by binary
            // search over the character boundaries: a prefix's width grows
            // with its length.
            let ends: Vec<usize> = rest
                .char_indices()
                .map(|(index, c)| index + c.len_utf8())
                .collect();
            let (mut low, mut high) = (0, ends.len() - 1);
            while low < high {
                let middle = (low + high).div_ceil(2);
                if self.width(&rest[..ends[middle]], size, font) <= max_width {
                    low = middle;
                } else {
                    high = middle - 1;
                }
            }
            let (head, tail) = rest.split_at(ends[low]);
            lines.push(Line {
                text: head.to_owned(),
                width: self.width(head, size, font),
            });
            rest = tail;
            if rest.is_empty() {
                break;
            }
        }
        (lines, rest)
    }
}

/// The allowed breaks inside `text`, in order: not at its start or end.
fn break_points(text: &str) -> Vec<usize> {
    linebreaks(text)
        .map(|(index, _)| index)
        .filter(|index| *index > 0 && *index < text.len())
        .collect()
}

/// Hiragana, katakana and their extensions, which are shaped without kerning.
fn is_kana(c: char) -> bool {
    matches!(c, '\u{3040}'..='\u{309F}' | '\u{30A0}'..='\u{30FF}' | '\u{31F0}'..='\u{31FF}' | '\u{FF65}'..='\u{FF9F}')
}

#[cfg(test)]
mod tests {
    use super::{Font, Fonts, Shaper};

    fn fonts() -> Fonts {
        let read = |name: &str| {
            let path = format!("{}/../../testdata/fonts/{name}", env!("CARGO_MANIFEST_DIR"));
            std::fs::read(&path).unwrap_or_else(|error| panic!("setup: {path}: {error}"))
        };
        match Fonts::new(
            read("IBMPlexSans-Regular.ttf"),
            read("IBMPlexSans-SemiBold.ttf"),
            read("AFTestSansJP-Regular.ttf"),
            read("AFTestSansJP-SemiBold.ttf"),
            read("IBMPlexMono-Regular.ttf"),
        ) {
            Ok(fonts) => fonts,
            Err(error) => panic!("setup: {error:?}"),
        }
    }

    #[test]
    fn a_line_too_wide_ends_at_the_last_break_that_fits_and_keeps_the_space() {
        let fonts = fonts();
        let shaper = Shaper::new(&fonts);
        let width = |text: &str| shaper.width(text, 14.0, Font::Regular);
        // Wide enough for "aaa bbb", not for "aaa bbb ccc".
        let max = width("aaa bbb") + 1.0;
        let mut lines = Vec::new();
        let mut line = String::from("aaa bbb ccc ");
        shaper.finish(&mut lines, &mut line, 14.0, Font::Regular, max);
        let texts: Vec<&str> = lines.iter().map(|line| line.text.as_str()).collect();
        assert_eq!(texts, ["aaa bbb"]);
        // What is carried keeps its space, so the next segment cannot join it.
        assert_eq!(line, "ccc ");
        // Narrower than "aaa bbb": the earlier break.
        let mut lines = Vec::new();
        let mut line = String::from("aaa bbb ccc ");
        shaper.finish(
            &mut lines,
            &mut line,
            14.0,
            Font::Regular,
            width("aaa") + 1.0,
        );
        assert_eq!(lines[0].text, "aaa");
        assert_eq!(line, "bbb ccc ");
    }
}

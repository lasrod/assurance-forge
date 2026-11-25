//! The recovery copy's envelope (plan decision R1): the document as SACM 2.3
//! XMI, with what tells it from a copy of another file's content.
//!
//! The core makes and reads it; the shells keep its bytes and do not read
//! them. It is JSON, one file, so it cannot be half new.

use serde::{Deserialize, Serialize};
use sha2::{Digest as _, Sha256};

/// What names the format, so another JSON file is not taken for a copy.
const FORMAT: &str = "assurance-forge-recovery";
/// The version this core writes and reads. A copy of a later one is set
/// aside, not read wrongly (R7).
const VERSION: u32 = 1;

/// A recovery copy, as it is kept.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct Envelope {
    format: String,
    version: u32,
    /// The file's name, as the page shows it.
    pub(crate) name: String,
    /// A digest of what the file held when these changes began (R6).
    pub(crate) base: String,
    /// When the copy was written, in milliseconds since 1970.
    pub(crate) written_at: f64,
    /// The document, by the checked write (S1).
    pub(crate) xmi: String,
}

impl Envelope {
    pub(crate) fn new(name: &str, base: &str, written_at: f64, xmi: String) -> Self {
        Self {
            format: FORMAT.to_owned(),
            version: VERSION,
            name: name.to_owned(),
            base: base.to_owned(),
            written_at,
            xmi,
        }
    }

    /// The bytes kept.
    pub(crate) fn to_json(&self) -> Option<String> {
        serde_json::to_string(self).ok()
    }

    /// The envelope in `bytes`, if they are one of this version.
    pub(crate) fn read(bytes: &[u8]) -> Option<Self> {
        let envelope: Self = serde_json::from_slice(bytes).ok()?;
        (envelope.format == FORMAT && envelope.version == VERSION).then_some(envelope)
    }
}

/// What tells one content of a file from another, across runs and across
/// shells: its SHA-256, in hex.
pub(crate) fn digest(bytes: &[u8]) -> String {
    let hash = Sha256::digest(bytes);
    let mut out = String::with_capacity(7 + hash.len() * 2);
    out.push_str("sha256:");
    for byte in hash {
        out.push(char::from(HEX[usize::from(byte >> 4)]));
        out.push(char::from(HEX[usize::from(byte & 0xf)]));
    }
    out
}

const HEX: &[u8; 16] = b"0123456789abcdef";

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_digest_is_sha256_in_hex() {
        assert_eq!(
            digest(b"abc"),
            "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
        );
    }

    #[test]
    fn an_envelope_reads_back_and_another_version_or_format_does_not() {
        let envelope = Envelope::new("a.sacm", "sha256:00", 1.5e12, "<x/>".to_owned());
        let json = envelope.to_json().unwrap_or_default();
        assert_eq!(Envelope::read(json.as_bytes()), Some(envelope));
        let later = json.replace("\"version\":1", "\"version\":2");
        assert_ne!(later, json, "setup: the version is written");
        assert_eq!(Envelope::read(later.as_bytes()), None);
        let other = json.replace(FORMAT, "something-else");
        assert_eq!(Envelope::read(other.as_bytes()), None);
        assert_eq!(Envelope::read(b"not json"), None);
    }
}

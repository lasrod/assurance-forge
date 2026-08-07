// Preservation evidence that does not run through the model (issue #292).
//
// The existing round-trip helper loads a file, saves it, loads the result, and
// compares the two documents. Both sides come out of the same reader, so it
// cannot see anything the reader drops: the attribute is absent from the first
// document, never written, absent from the second, and the comparison finds
// them equal. The test passes while the file's content is gone.
//
// That is not hypothetical. Making the reader discard `abstractForm` and
// running the whole repository suite -- 1,228 tests -- produced no failure
// attributable to it. Making it discard `isCitation` left all 36 round-trip
// tests passing; one unrelated test happened to catch that one.
//
// The two tests here close that in the two ways it can be closed:
//
//   1. Compare the source XML with the saved XML directly, through pugixml
//      rather than through the model, so a dropped attribute has nowhere to
//      hide. An independent inventory, in the language of #292.
//
//   2. Require every attribute the library claims to know to be exercised by
//      at least one fixture. Test 1 can only speak about attributes some
//      fixture actually contains; without this guard it stays silent about the
//      rest and that silence reads as coverage. `abstractForm` was in exactly
//      that position -- implemented on both sides, exercised by nothing.

#include "sacm/io/xmi.h"
#include "sacm/model/document.h"

#include "io/name_tables.h"

#include <gtest/gtest.h>

#include <pugixml.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::filesystem::path fixture_directory() {
    return std::filesystem::path(SACM_TEST_DATA_DIR) / "sacm23";
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

// Fixtures that are valid documents and are expected to survive a strict save.
// The `invalid/` subdirectory is deliberately excluded: those files exist to be
// rejected, and a rejected load has no output to compare against.
std::vector<std::filesystem::path> valid_fixtures() {
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(fixture_directory())) {
        if (entry.is_regular_file() && entry.path().string().ends_with(".sacm.xmi")) {
            paths.push_back(entry.path());
        }
    }
    std::ranges::sort(paths);
    return paths;
}

std::string local_name(std::string_view qualified) {
    const std::size_t colon = qualified.rfind(':');
    return std::string(colon == std::string_view::npos ? qualified : qualified.substr(colon + 1));
}

// Every attribute in the document, keyed by the element's xmi:id.
//
// Elements without an id are skipped rather than guessed at: the EMF dialect
// omits xmi:id entirely and refers positionally, so there is no stable key to
// match an element in the source against the same element in the output.
// Those files are still covered for their id-bearing elements.
std::map<std::string, std::set<std::string>> attributes_by_element_id(const pugi::xml_document& document) {
    std::map<std::string, std::set<std::string>> collected;
    for (const pugi::xpath_node& hit : document.select_nodes("//*")) {
        const pugi::xml_node node = hit.node();
        const pugi::xml_attribute id = node.attribute("xmi:id");
        if (!id) {
            continue;
        }
        std::set<std::string>& names = collected[id.value()];
        for (const pugi::xml_attribute& attribute : node.attributes()) {
            const std::string name = attribute.name();
            // Namespace declarations are not content; the writer is free to
            // declare prefixes where it needs them rather than where the source
            // did, and the vendor-extension tests already assert the ones that
            // must survive.
            if (name.starts_with("xmlns")) {
                continue;
            }
            names.insert(name);
        }
    }
    return collected;
}

// Attribute names the strict writer is allowed not to reproduce, each with the
// reason. Anything not listed here must survive.
bool is_permitted_to_disappear(const std::string& qualified_name) {
    const std::string name = local_name(qualified_name);
    // Legacy GSN shorthand. The reader normalizes undeveloped="true" to
    // assertionDeclaration=needsSupport and reports SACM23-COMPAT-001, so the
    // information is carried forward under its SACM name rather than lost.
    // docs/sacm/sacm-gsn-mapping.md records the mapping.
    if (name == "undeveloped") {
        return true;
    }
    // Tolerant-mode shorthands for content that SACM models as a child element.
    // The reader accepts name="x" / description="x" / key="x" and the writer
    // emits the child form, which is the normalized spelling rather than a
    // loss. test_xmi_io covers the promotion itself.
    if (name == "name" || name == "description" || name == "key") {
        return true;
    }
    // ptc/22-03-13 misspells Event.date; both spellings are accepted on read
    // and normalized to `date` on write.
    if (name == "occurece" || name == "occurence") {
        return true;
    }
    // Aliases the reader accepts and normalizes to the spec spelling.
    if (name == "referencedArtifact") {
        return true;
    }
    return false;
}

} // namespace

// An attribute present in the source must be present on the same element in the
// output. Read from the XML on both sides, so nothing the reader drops can hide
// behind the reader's own view of the document.
TEST(Sacm23AttributePreservation, SACM23_RT_001_SourceAttributesSurviveIntoTheSavedXml) {
    const std::vector<std::filesystem::path> fixtures = valid_fixtures();
    ASSERT_FALSE(fixtures.empty()) << "no fixtures found under " << fixture_directory().string();

    std::size_t attributes_compared = 0;
    std::size_t elements_compared = 0;

    for (const std::filesystem::path& path : fixtures) {
        const sacm::io::LoadResult loaded = sacm::io::load_xmi_file(path);
        if (!loaded.ok) {
            continue; // covered by the tests that assert why it is rejected
        }
        // Tolerant, not strict. Strict save normalizes and deliberately refuses
        // to emit foreign namespaces, so comparing a source against it would
        // report designed behaviour as loss. Tolerant is the mode that promises
        // to preserve what it read, which is the promise under test.
        const sacm::io::SaveResult saved =
            sacm::io::save_xmi_string(*loaded.document, sacm::io::SaveOptions{.mode = sacm::io::Mode::Tolerant});
        ASSERT_TRUE(saved.ok) << path.filename().string();

        pugi::xml_document source_xml;
        ASSERT_TRUE(source_xml.load_string(read_file(path).c_str())) << path.filename().string();
        pugi::xml_document output_xml;
        ASSERT_TRUE(output_xml.load_string(saved.xml.c_str())) << path.filename().string();

        const auto source = attributes_by_element_id(source_xml);
        const auto output = attributes_by_element_id(output_xml);

        for (const auto& [element_id, names] : source) {
            const auto match = output.find(element_id);
            if (match == output.end()) {
                ADD_FAILURE() << path.filename().string() << ": element '" << element_id
                              << "' is in the source and not in the saved output";
                continue;
            }
            ++elements_compared;
            for (const std::string& name : names) {
                if (is_permitted_to_disappear(name)) {
                    continue;
                }
                ++attributes_compared;
                EXPECT_TRUE(match->second.contains(name))
                    << path.filename().string() << ": element '" << element_id << "' lost attribute '" << name
                    << "' on save. The round-trip comparison cannot see this, because both of its "
                       "documents come from the same reader.";
            }
        }
    }

    // Non-vacuity. Everything above is a loop, and a loop over nothing passes.
    // If a refactor breaks fixture discovery or makes every load fail, the
    // assertions above go quiet and this test would otherwise report success.
    EXPECT_GT(elements_compared, 50u) << "too few elements compared; the comparison is not doing its job";
    EXPECT_GT(attributes_compared, 100u) << "too few attributes compared; the comparison is not doing its job";
}

// Every attribute the library claims to know must appear in some fixture.
//
// Without this, the test above is silent about any attribute no fixture
// contains, and silence is indistinguishable from coverage. `abstractForm` sat
// in that gap: read, written, and provably droppable without a single test
// failing.
TEST(Sacm23AttributePreservation, SACM23_RT_001_EveryKnownAttributeIsExercisedByAFixture) {
    std::set<std::string> exercised;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(fixture_directory())) {
        if (!entry.is_regular_file()) {
            continue;
        }
        pugi::xml_document document;
        if (!document.load_string(read_file(entry.path()).c_str())) {
            continue; // the invalid/ fixtures include deliberately malformed XML
        }
        for (const pugi::xpath_node& hit : document.select_nodes("//*")) {
            for (const pugi::xml_attribute& attribute : hit.node().attributes()) {
                exercised.insert(local_name(attribute.name()));
            }
        }
    }
    ASSERT_FALSE(exercised.empty()) << "no attributes found in any fixture; discovery is broken";

    // XMI serialization infrastructure the reader tolerates but that no SACM
    // document needs to carry. These are in the losslessness table so that a
    // file using them is not reported as vendor content, which is a different
    // claim from "a fixture exercises them".
    const std::set<std::string> infrastructure = {"idref", "uuid", "label", "type", "id", "ref", "href"};

    std::vector<std::string> unexercised;
    for (const std::string_view name : sacm::io::detail::known_sacm_attributes()) {
        const std::string attribute(name);
        if (infrastructure.contains(attribute) || exercised.contains(attribute)) {
            continue;
        }
        unexercised.push_back(attribute);
    }

    EXPECT_TRUE(unexercised.empty()) << [&] {
        std::ostringstream message;
        message << unexercised.size() << " attribute(s) the library knows are exercised by no fixture, so "
                << "nothing would notice if they were silently dropped:\n";
        for (const std::string& attribute : unexercised) {
            message << "  " << attribute << "\n";
        }
        message << "Add a fixture using each, or move it to the infrastructure list with a reason.";
        return message.str();
    }();
}

/**
 * test_story_graph_format_version.cpp — C04: daha yeni format_version reddedilir,
 * eksik (legacy) ve mevcut surum kabul edilir; chapter index/dosyasi da ayni politika.
 */
#include "rowl_test_harness.hpp"
#include "rowl/core/chapter_loader.hpp"
#include "rowl/core/story_graph_parser.hpp"

#include <string>

void test_story_graph_format_version() {
    using Rowl::Core::ChapterLoader;
    using Rowl::Core::StoryGraphParser;
    using Rowl::Core::kStoryGraphFormatVersion;

    TEST_SECTION("Story Graph Format Version Guard");

    const std::string nodes = R"("start_node_id":101,"nodes":[{"id":101}])";
    auto graph = [&](const std::string& version) {
        return StoryGraphParser::parse("{" + version + nodes + "}");
    };

    const std::string current = "\"format_version\":" + std::to_string(kStoryGraphFormatVersion) + ",";
    const std::string newer = "\"format_version\":" + std::to_string(kStoryGraphFormatVersion + 1) + ",";
    if (!graph("").succeeded() || !graph("\"format_version\":2,").succeeded() ||
        !graph(current).succeeded()) {
        std::cerr << "Legacy/current format_version graph was rejected" << std::endl;
        exit(1);
    }
    const auto rejected = graph(newer);
    if (rejected.succeeded() || rejected.message.find("newer") == std::string::npos) {
        std::cerr << "Newer format_version graph was not rejected" << std::endl;
        exit(1);
    }
    if (graph("\"format_version\":99999999999999999999,").succeeded()) {
        std::cerr << "Huge format_version graph was not rejected" << std::endl;
        exit(1);
    }
    TEST_PASS("Parser rejects newer graph, accepts legacy and current");

    ChapterLoader loader;
    std::string error;
    if (!loader.loadFullGraphJson("{" + current + nodes + "}", error)) {
        std::cerr << "Current full graph rejected: " << error << std::endl;
        exit(1);
    }
    error.clear();
    if (loader.loadFullGraphJson("{" + newer + nodes + "}", error) || error.empty()) {
        std::cerr << "Loader accepted newer full graph" << std::endl;
        exit(1);
    }
    error.clear();
    if (loader.loadIndexJson("{" + newer + R"("chapters":[{"id":"a"}]})", error) || error.empty()) {
        std::cerr << "Loader accepted newer chapter index" << std::endl;
        exit(1);
    }
    error.clear();
    if (loader.appendChapterFileJson("{" + newer + R"("chapter_id":"a","nodes":[{"id":5}]})", error) ||
        error.empty()) {
        std::cerr << "Loader accepted newer chapter file" << std::endl;
        exit(1);
    }
    error.clear();
    if (!loader.loadIndexJson("{" + current + R"("chapters":[{"id":"a"}]})", error) ||
        !loader.appendChapterFileJson("{" + current + R"("chapter_id":"a","nodes":[{"id":5}]})", error)) {
        std::cerr << "Current chapter index/file rejected: " << error << std::endl;
        exit(1);
    }
    TEST_PASS("ChapterLoader rejects newer graph, index and chapter file");
}

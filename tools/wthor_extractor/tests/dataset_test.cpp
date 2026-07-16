#include "wthor_extractor/dataset.hpp"

#include "reversi/pattern.hpp"
#include "reversi/position.hpp"

#include <algorithm>
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>

namespace wthor {
namespace {

std::vector<std::string> splitWhitespace(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string tok;
    while (iss >> tok) {
        tokens.push_back(tok);
    }
    return tokens;
}

TEST(WriteDatasetHeader, EmitsOneCommentLinePerPatternClass) {
    std::ostringstream out;
    writeDatasetHeader(out);
    const std::string header = out.str();

    // Every non-blank line must be a '%' comment (the format this project already uses for
    // provenance headers - see tests/data/ffo_easy.txt).
    std::istringstream lines(header);
    std::string line;
    int commentLines = 0;
    while (std::getline(lines, line)) {
        if (line.empty()) {
            continue;
        }
        EXPECT_EQ(line[0], '%') << "non-comment line in dataset header: " << line;
        ++commentLines;
    }
    // At least one line per pattern class, plus the descriptive lines above them.
    EXPECT_GE(commentLines, static_cast<int>(reversi::pattern::allPatternClasses().size()));
}

TEST(WriteDatasetHeader, ListsEveryShapeIdWithCorrectStateCount) {
    std::ostringstream out;
    writeDatasetHeader(out);
    const std::string header = out.str();
    // shapeId 11 (corner3x3, length 9) must appear with states = 3^9 = 19683.
    EXPECT_NE(header.find("11 corner3x3 9 19683"), std::string::npos) << header;
    // shapeId 9 (diag8, length 8) must appear with states = 3^8 = 6561.
    EXPECT_NE(header.find("9 diag8 8 6561"), std::string::npos) << header;
}

TEST(WriteDatasetLine, TargetScoreAndEmptyCountAreTheFirstTwoFields) {
    reversi::Position pos = reversi::Position::start();
    std::ostringstream out;
    // Black to move, final result 40-24 in Black's favor: mover-relative target = 16.
    writeDatasetLine(pos, /*posMoverIsBlack=*/true, /*finalBlackDiscs=*/40, /*finalWhiteDiscs=*/24,
                     out);
    const std::vector<std::string> tokens = splitWhitespace(out.str());
    ASSERT_GE(tokens.size(), std::size_t{2});
    EXPECT_EQ(tokens[0], "16");
    EXPECT_EQ(tokens[1], std::to_string(pos.emptyCount()));
}

TEST(WriteDatasetLine, EmitsExactlyOneTokenPerPatternInstance) {
    const reversi::Position pos = reversi::Position::start();
    std::ostringstream out;
    writeDatasetLine(pos, true, 32, 32, out);
    const std::vector<std::string> tokens = splitWhitespace(out.str());

    std::size_t expectedInstanceCount = 0;
    for (const reversi::pattern::PatternClass& cls : reversi::pattern::allPatternClasses()) {
        expectedInstanceCount += cls.instances.size();
    }
    // 2 header fields (score, emptyCount) + one "shapeId:index" token per pattern instance.
    ASSERT_EQ(tokens.size(), 2 + expectedInstanceCount);
    for (std::size_t i = 2; i < tokens.size(); ++i) {
        EXPECT_NE(tokens[i].find(':'), std::string::npos) << tokens[i];
    }
}

TEST(WriteDatasetLine, ShapeIdTokenMatchesDirectTernaryIndexComputation) {
    // Cross-check against reversi::pattern::ternaryIndex directly, not just trusting the
    // writer's own internal loop - the corner3x3 class anchored at a1 (shapeId 11) on the
    // start position, where a1 is empty and b2/... vary.
    const reversi::Position pos = reversi::Position::start();
    std::ostringstream out;
    writeDatasetLine(pos, true, 32, 32, out);
    const std::vector<std::string> tokens = splitWhitespace(out.str());

    const reversi::pattern::PatternClass& corner = reversi::pattern::allPatternClasses()[11];
    ASSERT_EQ(corner.name, "corner3x3");
    const int expectedIndex = reversi::pattern::ternaryIndex(pos, corner.instances[0]);
    const std::string expectedToken = "11:" + std::to_string(expectedIndex);
    EXPECT_NE(std::find(tokens.begin(), tokens.end(), expectedToken), tokens.end())
        << "expected token " << expectedToken << " not found";
}

TEST(WriteDatasetLines, WritesOneLinePerReplayedPosition) {
    GameRecord record;
    record.moves = {*reversi::squareFromString("f5"), *reversi::squareFromString("d6"),
                    *reversi::squareFromString("c3"), *reversi::squareFromString("f3")};
    const ReplayedGame game = replayGame(record);

    std::ostringstream out;
    writeDatasetLines(game, out);

    int lineCount = 0;
    std::istringstream lines(out.str());
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty()) {
            ++lineCount;
        }
    }
    EXPECT_EQ(lineCount, static_cast<int>(game.positions.size()));
}

} // namespace
} // namespace wthor

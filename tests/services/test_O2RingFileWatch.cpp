#include <gtest/gtest.h>

#include "services/O2RingFileWatch.h"

TEST(O2RingFileWatch, NothingIsAskedWhileTheRingHasNotBeenWorn) {
    O2RingFileWatch w;
    for (int i = 0; i < 5; ++i) EXPECT_FALSE(w.shouldCollect(false, true));
}

TEST(O2RingFileWatch, ASessionThatEndsIsFollowedByARecheckUntilAFileComes) {
    O2RingFileWatch w;
    EXPECT_FALSE(w.shouldCollect(true, true));
    EXPECT_TRUE(w.shouldCollect(false, true));    // the end of the session
    w.collected(false);                           // the file is not listed yet
    EXPECT_TRUE(w.shouldCollect(false, true));    // so it is asked again
    w.collected(true);                            // now it is there
    EXPECT_FALSE(w.shouldCollect(false, true));   // and the wait is over
}

TEST(O2RingFileWatch, TheRecheckIsLimited) {
    O2RingFileWatch w;
    w.shouldCollect(true, true);
    int asked = 0;
    for (int i = 0; i < 100; ++i)
        if (w.shouldCollect(false, true)) { w.collected(false); ++asked; }
    EXPECT_EQ(asked, O2RingFileWatch::kRecheckPolls);
}

TEST(O2RingFileWatch, AnUnreachableRingIsNeitherAskedNorCountedAgainstTheWait) {
    O2RingFileWatch w;
    w.shouldCollect(true, true);
    // Out of range right after the session: no answer, nothing lost.
    for (int i = 0; i < 40; ++i) EXPECT_FALSE(w.shouldCollect(false, false));
    EXPECT_TRUE(w.shouldCollect(false, true));
    EXPECT_EQ(w.pollsLeft(), O2RingFileWatch::kRecheckPolls - 1);
}

TEST(O2RingFileWatch, WearingItAgainStopsTheWaitAndAnotherEndStartsOne) {
    O2RingFileWatch w;
    w.shouldCollect(true, true);
    EXPECT_TRUE(w.shouldCollect(false, true));
    w.collected(false);
    EXPECT_FALSE(w.shouldCollect(true, true));    // back on the finger
    EXPECT_EQ(w.pollsLeft(), 0);
    EXPECT_TRUE(w.shouldCollect(false, true));    // ended again: a fresh wait
    EXPECT_EQ(w.pollsLeft(), O2RingFileWatch::kRecheckPolls - 1);
}

#pragma once

/**
 * When to ask an O2 Ring for its files.
 *
 * A ring does not always have the recording listed the moment it goes off the
 * finger: the file can show up a few minutes later. Looking once, at the
 * active-to-inactive change, then misses it, and nothing else asks until the
 * next wear. So after a session ends the ring is asked again on each poll,
 * for a limited number of polls, until a new file arrives.
 */
class O2RingFileWatch {
public:
    /// Polls (one per burst cycle, about a minute each) a session's end keeps
    /// the ring being asked.
    static constexpr int kRecheckPolls = 15;

    /// Called once per poll with what the poll saw; true means look at the
    /// ring's files now.
    bool shouldCollect(bool active, bool reachable) {
        if (active) {                 // on the finger: a session is under way
            was_active_ = true;
            left_ = 0;
            return false;
        }
        if (!reachable) return false;  // no answer says nothing; keep waiting
        if (was_active_) {            // the session has just ended
            was_active_ = false;
            left_ = kRecheckPolls;
        }
        if (left_ <= 0) return false;
        --left_;
        return true;
    }

    /// What the files look came to: a new file ends the wait.
    void collected(bool found_new) {
        if (found_new) left_ = 0;
    }

    int pollsLeft() const { return left_; }

private:
    bool was_active_ = false;
    int left_ = 0;
};
